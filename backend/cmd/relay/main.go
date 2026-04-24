package main

import (
	"context"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"log"
	"math"
	"math/rand"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
	"github.com/hraban/opus"
)

const (
	sampleRate      = 8000
	audioChannels   = 1
	frameDur        = 20 * time.Millisecond
	frameSamples    = sampleRate / 50
	maxUDPDatagram  = 1500
	opusMaxFrameLen = 512
)

type wsMessage struct {
	Type    string `json:"type"`
	Channel string `json:"channel,omitempty"`
	UDPPort int    `json:"udpPort,omitempty"`
}

type wsServerMessage struct {
	Type      string `json:"type"`
	SessionID uint32 `json:"sessionId,omitempty"`
	Channel   string `json:"channel,omitempty"`
	Info      string `json:"info,omitempty"`
}

type client struct {
	sessionID uint32
	conn      *websocket.Conn

	mu      sync.RWMutex
	channel string
	udpAddr *net.UDPAddr
}

func (c *client) setChannel(ch string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.channel = ch
}

func (c *client) getChannel() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.channel
}

func (c *client) setUDPAddr(addr *net.UDPAddr) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.udpAddr = addr
}

func (c *client) getUDPAddr() *net.UDPAddr {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.udpAddr
}

type audioPacket struct {
	sessionID      uint32
	seq            uint32
	signalStrength uint8
	opus           []byte
	srcAddr        *net.UDPAddr
}

// audioProcessContext is the pluggable processing contract for channel audio modules.
// New DSP modules can be injected by implementing audioModule and appending it to channelMixer.modules.
type audioProcessContext struct {
	Mixed          []float64
	AvgSignalByte  float64
	ActiveSpeakers int
	NoiseLevelDB   float64
	EmitFrame      bool
	DropFrame      bool
}

type audioModule interface {
	Name() string
	Process(ctx *audioProcessContext)
}

type relayHub struct {
	clientsMu sync.RWMutex
	clients   map[uint32]*client

	channelMu sync.RWMutex
	channels  map[string]*channelMixer

	udpConn  *net.UDPConn
	loopback bool
}

func newRelayHub(udpConn *net.UDPConn, loopback bool) *relayHub {
	return &relayHub{
		clients:  make(map[uint32]*client),
		channels: make(map[string]*channelMixer),
		udpConn:  udpConn,
		loopback: loopback,
	}
}

func (h *relayHub) addClient(c *client) {
	h.clientsMu.Lock()
	defer h.clientsMu.Unlock()
	h.clients[c.sessionID] = c
}

func (h *relayHub) removeClient(sessionID uint32) {
	h.clientsMu.Lock()
	c, ok := h.clients[sessionID]
	if ok {
		delete(h.clients, sessionID)
	}
	h.clientsMu.Unlock()
	if ok {
		ch := c.getChannel()
		if ch != "" {
			h.getOrCreateChannel(ch).removeParticipant(sessionID)
		}
	}
}

func (h *relayHub) getClient(sessionID uint32) *client {
	h.clientsMu.RLock()
	defer h.clientsMu.RUnlock()
	return h.clients[sessionID]
}

func (h *relayHub) switchChannel(c *client, newChannel string) {
	oldChannel := c.getChannel()
	if oldChannel == newChannel {
		return
	}
	if oldChannel != "" {
		h.getOrCreateChannel(oldChannel).removeParticipant(c.sessionID)
	}
	c.setChannel(newChannel)
	h.getOrCreateChannel(newChannel).addParticipant(c.sessionID)
}

func (h *relayHub) getOrCreateChannel(name string) *channelMixer {
	h.channelMu.RLock()
	ch, ok := h.channels[name]
	h.channelMu.RUnlock()
	if ok {
		return ch
	}

	h.channelMu.Lock()
	defer h.channelMu.Unlock()
	if existing, found := h.channels[name]; found {
		return existing
	}
	m := newChannelMixer(name, h)
	h.channels[name] = m
	go m.run()
	return m
}

func (h *relayHub) routePacket(pkt *audioPacket) {
	c := h.getClient(pkt.sessionID)
	if c == nil {
		return
	}
	c.setUDPAddr(pkt.srcAddr)
	channelName := c.getChannel()
	if channelName == "" {
		return
	}
	h.getOrCreateChannel(channelName).push(pkt)
}

func (h *relayHub) broadcastMixed(channel string, excludeSession uint32, mixedOpus []byte, seq uint32, signalStrength uint8) {
	header := make([]byte, 9)
	binary.BigEndian.PutUint32(header[0:4], 0)
	binary.BigEndian.PutUint32(header[4:8], seq)
	header[8] = signalStrength
	payload := append(header, mixedOpus...)

	h.clientsMu.RLock()
	defer h.clientsMu.RUnlock()
	for _, c := range h.clients {
		if c.getChannel() != channel {
			continue
		}
		if !h.loopback && excludeSession != 0 && c.sessionID == excludeSession {
			continue
		}
		addr := c.getUDPAddr()
		if addr == nil {
			continue
		}
		if _, err := h.udpConn.WriteToUDP(payload, addr); err != nil {
			log.Printf("udp broadcast failed to %s: %v", addr, err)
		}
	}
}

type channelMixer struct {
	name string
	hub  *relayHub

	input chan *audioPacket

	participantsMu sync.RWMutex
	participants   map[uint32]struct{}

	decoders map[uint32]*opus.Decoder
	encoder  *opus.Encoder

	seq          uint32
	noiseLevelDB float64
	lastSignal   float64
	filter       *bandPass
	modules      []audioModule
}

func newChannelMixer(name string, hub *relayHub) *channelMixer {
	enc, err := opus.NewEncoder(sampleRate, audioChannels, opus.AppVoIP)
	if err != nil {
		panic(fmt.Sprintf("create encoder for channel %s: %v", name, err))
	}
	return &channelMixer{
		name:         name,
		hub:          hub,
		input:        make(chan *audioPacket, 256),
		participants: make(map[uint32]struct{}),
		decoders:     make(map[uint32]*opus.Decoder),
		encoder:      enc,
		noiseLevelDB: -30.0,
		lastSignal:   255.0,
		filter:       newBandPass(sampleRate, 300.0, 3000.0),
		modules: []audioModule{
			newWhiteNoiseSquelchModule(frameDur),
		},
	}
}

func (m *channelMixer) addParticipant(sessionID uint32) {
	m.participantsMu.Lock()
	defer m.participantsMu.Unlock()
	m.participants[sessionID] = struct{}{}
}

func (m *channelMixer) removeParticipant(sessionID uint32) {
	m.participantsMu.Lock()
	defer m.participantsMu.Unlock()
	delete(m.participants, sessionID)
	delete(m.decoders, sessionID)
}

func (m *channelMixer) push(pkt *audioPacket) {
	select {
	case m.input <- pkt:
	default:
		// Drop oldest-ish behavior for low latency preference.
		select {
		case <-m.input:
		default:
		}
		select {
		case m.input <- pkt:
		default:
		}
	}
}

func (m *channelMixer) run() {
	ticker := time.NewTicker(frameDur)
	defer ticker.Stop()

	latestBySpeaker := make(map[uint32]*audioPacket)

	for {
		select {
		case pkt := <-m.input:
			latestBySpeaker[pkt.sessionID] = pkt
		case <-ticker.C:
			m.mixAndBroadcast(latestBySpeaker)
			clear(latestBySpeaker)
		}
	}
}

func (m *channelMixer) mixAndBroadcast(frames map[uint32]*audioPacket) {
	mixed := make([]float64, frameSamples)
	activeSpeakers := make([]uint32, 0, len(frames))
	var signalSum float64

	for sessionID, pkt := range frames {
		dec := m.decoders[sessionID]
		if dec == nil {
			var err error
			dec, err = opus.NewDecoder(sampleRate, audioChannels)
			if err != nil {
				log.Printf("decoder create failed for %d: %v", sessionID, err)
				continue
			}
			m.decoders[sessionID] = dec
		}

		pcm := make([]int16, frameSamples)
		n, err := dec.Decode(pkt.opus, pcm)
		if err != nil {
			log.Printf("opus decode failed for %d: %v", sessionID, err)
			continue
		}
		if n <= 0 {
			continue
		}

		activeSpeakers = append(activeSpeakers, sessionID)
		signalSum += float64(pkt.signalStrength)

		maxN := n
		if maxN > frameSamples {
			maxN = frameSamples
		}
		for i := 0; i < maxN; i++ {
			mixed[i] += float64(pcm[i])
		}
	}

	if len(activeSpeakers) == 0 {
		signalSum = m.lastSignal
	} else {
		m.lastSignal = signalSum / float64(len(activeSpeakers))
	}

	ctx := &audioProcessContext{
		Mixed:          mixed,
		AvgSignalByte:  signalSum / float64(maxInt(len(activeSpeakers), 1)),
		ActiveSpeakers: len(activeSpeakers),
		NoiseLevelDB:   m.noiseLevelDB,
		EmitFrame:      len(activeSpeakers) > 0,
	}
	for _, mod := range m.modules {
		mod.Process(ctx)
		if ctx.DropFrame {
			m.noiseLevelDB = ctx.NoiseLevelDB
			return
		}
	}
	m.noiseLevelDB = ctx.NoiseLevelDB
	if !ctx.EmitFrame {
		return
	}

	outPCM := make([]int16, frameSamples)
	for i := 0; i < frameSamples; i++ {
		// Saturated character: hard clip after sum.
		outPCM[i] = hardClipFloat(mixed[i], 15000.0)
	}
	m.filter.process(outPCM)

	opusBuf := make([]byte, opusMaxFrameLen)
	n, err := m.encoder.Encode(outPCM, opusBuf)
	if err != nil {
		log.Printf("opus encode failed in channel %s: %v", m.name, err)
		return
	}
	if n <= 0 {
		return
	}
	m.seq++
	exclude := uint32(0)
	if len(activeSpeakers) == 1 {
		exclude = activeSpeakers[0]
	}
	m.hub.broadcastMixed(m.name, exclude, opusBuf[:n], m.seq, uint8(ctx.AvgSignalByte))
}

func hardClip(v int32, threshold int32) int16 {
	if v > threshold {
		return int16(threshold)
	}
	if v < -threshold {
		return int16(-threshold)
	}
	return int16(v)
}

func hardClipFloat(v float64, threshold float64) int16 {
	if v > threshold {
		return int16(threshold)
	}
	if v < -threshold {
		return int16(-threshold)
	}
	return int16(v)
}

type whiteNoiseSquelchModule struct {
	white              *whiteNoise
	frameDuration      time.Duration
	squelchBurstRemain time.Duration
	squelchLatched     bool
	lastActive         bool
}

func newWhiteNoiseSquelchModule(frameDuration time.Duration) *whiteNoiseSquelchModule {
	return &whiteNoiseSquelchModule{
		white:         newWhiteNoise(),
		frameDuration: frameDuration,
	}
}

func (m *whiteNoiseSquelchModule) Name() string {
	return "white_noise_squelch"
}

func (m *whiteNoiseSquelchModule) Process(ctx *audioProcessContext) {
	if ctx.ActiveSpeakers <= 0 {
		// End-of-transmission tail burst: jump to 0 dB and emit white hiss briefly.
		if m.lastActive && m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(15, 60)
		}
		m.lastActive = false
		if m.squelchBurstRemain <= 0 {
			m.squelchLatched = false
			ctx.EmitFrame = false
			return
		}
		noiseAmplitude := 1200.0 * dbToLinear(0.0)
		for i := range ctx.Mixed {
			ctx.Mixed[i] += m.white.next() * noiseAmplitude
		}
		ctx.EmitFrame = true
		m.squelchBurstRemain -= m.frameDuration
		if m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = 0
		}
		return
	}
	m.lastActive = true

	noiseDB := mapSignalByteToNoiseDB(ctx.AvgSignalByte)
	ctx.NoiseLevelDB = noiseDB
	noiseAmplitude := 1200.0 * dbToLinear(noiseDB)

	// Squelch behavior: when the line is too weak, emit a short burst of hiss then gate output.
	if noiseDB >= -3.0 {
		if m.squelchLatched {
			ctx.DropFrame = true
			return
		}
		if m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(50, 150)
		}
		for i := range ctx.Mixed {
			ctx.Mixed[i] += m.white.next() * noiseAmplitude
		}
		m.squelchBurstRemain -= m.frameDuration
		if m.squelchBurstRemain <= 0 {
			m.squelchLatched = true
			ctx.DropFrame = true
		}
		return
	}

	m.squelchLatched = false
	m.squelchBurstRemain = 0
	for i := range ctx.Mixed {
		ctx.Mixed[i] += m.white.next() * noiseAmplitude
	}
}

func mapSignalByteToNoiseDB(signalByte float64) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return -3.0
	}
	if pct >= 100.0 {
		return -20.0
	}
	// Linear interpolation in dB domain:
	// 10% -> -3 dB, 100% -> -20 dB.
	return -3.0 - ((pct-10.0)/90.0)*17.0
}

func dbToLinear(db float64) float64 {
	return math.Pow(10.0, db/20.0)
}

func randomDurationMs(minMs int, maxMs int) time.Duration {
	if maxMs <= minMs {
		return time.Duration(minMs) * time.Millisecond
	}
	return time.Duration(minMs+rand.Intn(maxMs-minMs+1)) * time.Millisecond
}

func maxInt(a int, b int) int {
	if a > b {
		return a
	}
	return b
}

type whiteNoise struct{}

func newWhiteNoise() *whiteNoise {
	return &whiteNoise{}
}

func (w *whiteNoise) next() float64 {
	return rand.Float64()*2 - 1
}

type onePoleHP struct {
	alpha float64
	prevX float64
	prevY float64
}

func newOnePoleHP(sr int, cutoff float64) *onePoleHP {
	dt := 1.0 / float64(sr)
	rc := 1.0 / (2.0 * math.Pi * cutoff)
	alpha := rc / (rc + dt)
	return &onePoleHP{alpha: alpha}
}

func (f *onePoleHP) process(x float64) float64 {
	y := f.alpha * (f.prevY + x - f.prevX)
	f.prevX = x
	f.prevY = y
	return y
}

type onePoleLP struct {
	alpha float64
	prevY float64
}

func newOnePoleLP(sr int, cutoff float64) *onePoleLP {
	dt := 1.0 / float64(sr)
	rc := 1.0 / (2.0 * math.Pi * cutoff)
	alpha := dt / (rc + dt)
	return &onePoleLP{alpha: alpha}
}

func (f *onePoleLP) process(x float64) float64 {
	f.prevY = f.prevY + f.alpha*(x-f.prevY)
	return f.prevY
}

type bandPass struct {
	hp *onePoleHP
	lp *onePoleLP
}

func newBandPass(sr int, lowCut float64, highCut float64) *bandPass {
	return &bandPass{
		hp: newOnePoleHP(sr, lowCut),
		lp: newOnePoleLP(sr, highCut),
	}
}

func (b *bandPass) process(frame []int16) {
	for i := range frame {
		x := float64(frame[i])
		y := b.hp.process(x)
		z := b.lp.process(y)
		frame[i] = hardClip(int32(z), 32767)
	}
}

func parseAudioPacket(buf []byte, n int, src *net.UDPAddr) (*audioPacket, error) {
	if n < 9 {
		return nil, errors.New("packet too short")
	}
	sessionID := binary.BigEndian.Uint32(buf[0:4])
	seq := binary.BigEndian.Uint32(buf[4:8])
	signalStrength := buf[8]
	opusPayload := make([]byte, n-9)
	copy(opusPayload, buf[9:n])
	return &audioPacket{
		sessionID:      sessionID,
		seq:            seq,
		signalStrength: signalStrength,
		opus:           opusPayload,
		srcAddr:        src,
	}, nil
}

func udpReader(ctx context.Context, conn *net.UDPConn, hub *relayHub) {
	buf := make([]byte, maxUDPDatagram)
	for {
		select {
		case <-ctx.Done():
			return
		default:
		}
		_ = conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		n, src, err := conn.ReadFromUDP(buf)
		if err != nil {
			if ne, ok := err.(net.Error); ok && ne.Timeout() {
				continue
			}
			log.Printf("udp read error: %v", err)
			continue
		}
		pkt, err := parseAudioPacket(buf, n, src)
		if err != nil {
			continue
		}
		hub.routePacket(pkt)
	}
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type server struct {
	hub *relayHub
	seq atomic.Uint32
}

func (s *server) nextSessionID() uint32 {
	base := uint32(time.Now().Unix() & 0xFFFF)
	return (base << 16) | (s.seq.Add(1) & 0xFFFF)
}

func (s *server) wsHandler(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("ws upgrade failed: %v", err)
		return
	}
	defer conn.Close()

	c := &client{
		sessionID: s.nextSessionID(),
		conn:      conn,
		channel:   "global",
	}
	s.hub.addClient(c)
	s.hub.switchChannel(c, "global")
	defer s.hub.removeClient(c.sessionID)

	_ = conn.WriteJSON(wsServerMessage{
		Type:      "welcome",
		SessionID: c.sessionID,
		Channel:   "global",
	})

	for {
		var msg wsMessage
		if err := conn.ReadJSON(&msg); err != nil {
			log.Printf("ws read ended for %d: %v", c.sessionID, err)
			return
		}
		switch strings.ToLower(msg.Type) {
		case "join", "switch_channel":
			ch := strings.TrimSpace(msg.Channel)
			if ch == "" {
				ch = "global"
			}
			s.hub.switchChannel(c, ch)
			_ = conn.WriteJSON(wsServerMessage{Type: "joined", Channel: ch})
		case "udp_hello":
			host, _, err := net.SplitHostPort(r.RemoteAddr)
			if err != nil {
				host = r.RemoteAddr
			}
			port := msg.UDPPort
			if port > 0 && port <= 65535 {
				c.setUDPAddr(&net.UDPAddr{
					IP:   net.ParseIP(host),
					Port: port,
				})
				_ = conn.WriteJSON(wsServerMessage{Type: "udp_registered", Info: host + ":" + strconv.Itoa(port)})
			}
		case "ping", "heartbeat":
			_ = conn.WriteJSON(wsServerMessage{Type: "pong"})
		default:
			_ = conn.WriteJSON(wsServerMessage{Type: "error", Info: "unknown message type"})
		}
	}
}

func main() {
	var wsAddr string
	var udpAddr string
	var loopback bool
	flag.StringVar(&wsAddr, "ws", ":8080", "WebSocket listen address")
	flag.StringVar(&udpAddr, "udp", ":5000", "UDP listen address")
	flag.BoolVar(&loopback, "loopback", false, "if true, mixed stream is also sent to source speaker")
	flag.Parse()

	rand.Seed(time.Now().UnixNano())

	udpConn, err := net.ListenUDP("udp", mustResolveUDP(udpAddr))
	if err != nil {
		log.Fatalf("udp listen failed: %v", err)
	}
	defer udpConn.Close()

	hub := newRelayHub(udpConn, loopback)
	s := &server{hub: hub}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go udpReader(ctx, udpConn, hub)

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", s.wsHandler)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ok"))
	})

	log.Printf("relay started ws=%s udp=%s loopback=%v", wsAddr, udpAddr, loopback)
	if err := http.ListenAndServe(wsAddr, mux); err != nil {
		log.Fatalf("ws server error: %v", err)
	}
}

func mustResolveUDP(addr string) *net.UDPAddr {
	ua, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		panic(err)
	}
	return ua
}
