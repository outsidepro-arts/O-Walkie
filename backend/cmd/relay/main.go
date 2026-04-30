package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"math/rand"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
	"github.com/hraban/opus"
)

const (
	defaultSampleRate  = 8000
	audioChannels      = 1
	maxUDPDatagram     = 1500
	opusMaxFrameLen    = 512
	configFilePath     = "config.json"
	defaultPacketMs    = 20
	protocolVersion    = 2
	scanActivityWindow = 10 * time.Second
	udpKeepaliveSignal = 255
	udpKeepaliveAck    = 254
)

var (
	configuredSampleRate = defaultSampleRate
	packetDur            = time.Duration(defaultPacketMs) * time.Millisecond
	packetSamples        = configuredSampleRate * defaultPacketMs / 1000
	configuredOpus       = opusConfig{
		Bitrate:     12000,
		Complexity:  5,
		FEC:         true,
		DTX:         false,
		Application: "voip",
	}
)

type appConfig struct {
	Server  serverConfig  `json:"server"`
	Modules modulesConfig `json:"modules"`
}

type serverConfig struct {
	WSAddr             string     `json:"ws_addr"`
	UDPAddr            string     `json:"udp_addr"`
	SampleRate         int        `json:"sample_rate"`
	PacketMs           int        `json:"packet_ms"`
	Opus               opusConfig `json:"opus"`
	HangoverMs         int        `json:"hangover_ms"`
	EOFTimeoutMs       int        `json:"eof_timeout_ms"`
	ConcealDecay       float64    `json:"conceal_decay"`
	JitterMinPkts      int        `json:"jitter_min_packets"`
	BusyMode           bool       `json:"busy_mode"`
	TransmitTimeoutSec int        `json:"transmit_timeout"`
}

type opusConfig struct {
	Bitrate     int    `json:"bitrate"`
	Complexity  int    `json:"complexity"`
	FEC         bool   `json:"fec"`
	DTX         bool   `json:"dtx"`
	Application string `json:"application"`
}

type modulesConfig struct {
	Noise      *noiseConfig      `json:"noise,omitempty"`
	Click      *clickConfig      `json:"click,omitempty"`
	Filter     *filterConfig     `json:"filter,omitempty"`
	Compressor *compressorConfig `json:"compressor,omitempty"`
	Distortion *distortionConfig `json:"distortion,omitempty"`
}

type noiseConfig struct {
	Enabled            bool    `json:"enabled"`
	SignalDependent    bool    `json:"signal_dependent"`
	MinNoiseDB         float64 `json:"min_noise_db"`
	MaxNoiseDB         float64 `json:"max_noise_db"`
	NoiseGain          float64 `json:"noise_gain"`
	SquelchThresholdDB float64 `json:"squelch_threshold_db"`
	SquelchMinMs       int     `json:"squelch_min_ms"`
	SquelchMaxMs       int     `json:"squelch_max_ms"`
	SquelchShotsMinS   int     `json:"squelch_shots_min_s"`
	SquelchShotsMaxS   int     `json:"squelch_shots_max_s"`
	TailNoiseDB        float64 `json:"tail_noise_db"`
	TailMinMs          int     `json:"tail_min_ms"`
	TailMaxMs          int     `json:"tail_max_ms"`
}

type compressorConfig struct {
	Enabled     bool    `json:"enabled"`
	ThresholdDB float64 `json:"threshold_db"`
	Ratio       float64 `json:"ratio"`
	AttackMs    float64 `json:"attack_ms"`
	ReleaseMs   float64 `json:"release_ms"`
	MakeupDB    float64 `json:"makeup_db"`
}

type clickConfig struct {
	Enabled             bool    `json:"enabled"`
	ClickDB             float64 `json:"click_db"`
	GlitchIntervalMaxMs int     `json:"glitch_interval_max_ms"`
	GlitchFreqMinHz     float64 `json:"glitch_freq_min_hz"`
	GlitchFreqMaxHz     float64 `json:"glitch_freq_max_hz"`
	GlitchLevelMinDB    float64 `json:"glitch_level_min_db"`
	GlitchLevelMaxDB    float64 `json:"glitch_level_max_db"`
}

type filterConfig struct {
	Enabled   bool    `json:"enabled"`
	LowCutHz  float64 `json:"low_cut_hz"`
	HighCutHz float64 `json:"high_cut_hz"`
}

type distortionConfig struct {
	Enabled bool    `json:"enabled"`
	Drive   float64 `json:"drive"`
	Mix     float64 `json:"mix"`
}

type wsMessage struct {
	Type            string `json:"type"`
	Channel         string `json:"channel,omitempty"`
	UDPPort         int    `json:"udpPort,omitempty"`
	RepeaterEnabled *bool  `json:"enabled,omitempty"`
}

type wsServerMessage struct {
	Type            string        `json:"type"`
	SessionID       uint32        `json:"sessionId,omitempty"`
	Channel         string        `json:"channel,omitempty"`
	Info            string        `json:"info,omitempty"`
	PacketMs        int           `json:"packetMs,omitempty"`
	SampleRate      int           `json:"sampleRate,omitempty"`
	Opus            *wsOpusConfig `json:"opus,omitempty"`
	ProtocolVersion int           `json:"protocolVersion,omitempty"`
	BusyMode        *bool         `json:"busyMode,omitempty"`
	Active          *bool         `json:"active,omitempty"`
}

type wsOpusConfig struct {
	Bitrate     int    `json:"bitrate"`
	Complexity  int    `json:"complexity"`
	FEC         bool   `json:"fec"`
	DTX         bool   `json:"dtx"`
	Application string `json:"application"`
}

type client struct {
	sessionID uint32
	conn      *websocket.Conn

	mu        sync.RWMutex
	wsWriteMu sync.Mutex
	channel   string
	udpAddr   *net.UDPAddr
	repeater  bool
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

func normalizeChannelName(ch string) string {
	return strings.ToLower(strings.TrimSpace(ch))
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

func (c *client) setRepeaterMode(enabled bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.repeater = enabled
}

func (c *client) isRepeaterMode() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.repeater
}

func (c *client) writeJSON(msg wsServerMessage) error {
	c.wsWriteMu.Lock()
	defer c.wsWriteMu.Unlock()
	return c.conn.WriteJSON(msg)
}

func (c *client) writeControl(messageType int, data []byte, deadline time.Time) error {
	c.wsWriteMu.Lock()
	defer c.wsWriteMu.Unlock()
	return c.conn.WriteControl(messageType, data, deadline)
}

type audioPacket struct {
	sessionID      uint32
	seq            uint32
	signalStrength uint8
	opus           []byte
	srcAddr        *net.UDPAddr
}

func (p *audioPacket) isUDPEOFMarker() bool {
	if p == nil {
		return false
	}
	// UDP EOF marker (protocol extension):
	// - empty opus payload
	// - signal byte = 0
	// - seq > 0 to avoid clashing with legacy keepalive(seq=0)
	return len(p.opus) == 0 && p.signalStrength == 0 && p.seq > 0
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

	activityMu      sync.RWMutex
	channelActivity map[string]time.Time
	activityWindow  time.Duration

	udpConn *net.UDPConn
	cfg     appConfig
}

type speakerStreamState struct {
	pending      *audioPacket
	jitter       *speakerJitterBuffer
	lastPacketAt time.Time
	lastSignal   float64
	lastPCM      []int16
	missCount    int
}

type speakerJitterBuffer struct {
	minStart int
	maxKeep  int
	packets  map[uint32]*audioPacket
	head     uint32
	started  bool
}

func newRelayHub(udpConn *net.UDPConn, cfg appConfig) *relayHub {
	return &relayHub{
		clients:         make(map[uint32]*client),
		channels:        make(map[string]*channelMixer),
		channelActivity: make(map[string]time.Time),
		activityWindow:  scanActivityWindow,
		udpConn:         udpConn,
		cfg:             cfg,
	}
}

func (h *relayHub) markChannelActivity(channel string) {
	channel = normalizeChannelName(channel)
	if channel == "" {
		return
	}
	h.activityMu.Lock()
	h.channelActivity[channel] = time.Now()
	h.activityMu.Unlock()
}

func (h *relayHub) channelHasRecentActivity(channel string) bool {
	channel = normalizeChannelName(channel)
	if channel == "" {
		return false
	}
	h.activityMu.RLock()
	lastAt, ok := h.channelActivity[channel]
	window := h.activityWindow
	h.activityMu.RUnlock()
	if !ok {
		return false
	}
	return time.Since(lastAt) <= window
}

func (h *relayHub) addClient(c *client) {
	h.clientsMu.Lock()
	defer h.clientsMu.Unlock()
	h.clients[c.sessionID] = c
}

func (h *relayHub) removeClient(sessionID uint32) {
	h.clientsMu.Lock()
	_, ok := h.clients[sessionID]
	if ok {
		delete(h.clients, sessionID)
	}
	h.clientsMu.Unlock()
	if !ok {
		return
	}

	// Defensive cleanup: remove disconnected client from all existing channels.
	// This avoids stale subscriptions even if channel bookkeeping got out of sync.
	h.channelMu.RLock()
	channels := make([]*channelMixer, 0, len(h.channels))
	for _, ch := range h.channels {
		channels = append(channels, ch)
	}
	h.channelMu.RUnlock()
	for _, ch := range channels {
		ch.removeParticipant(sessionID)
	}
}

func (h *relayHub) getClient(sessionID uint32) *client {
	h.clientsMu.RLock()
	defer h.clientsMu.RUnlock()
	return h.clients[sessionID]
}

func (h *relayHub) switchChannel(c *client, newChannel string) {
	newChannel = normalizeChannelName(newChannel)
	oldChannel := c.getChannel()
	if oldChannel == newChannel {
		// Ensure participant registration even when channel value is unchanged.
		// This covers initial connection cases where client.channel is pre-set.
		if newChannel != "" {
			h.getOrCreateChannel(newChannel).addParticipant(c.sessionID)
		}
		return
	}
	if oldChannel != "" {
		h.getOrCreateChannel(oldChannel).removeParticipant(c.sessionID)
	}
	c.setChannel(newChannel)
	h.getOrCreateChannel(newChannel).addParticipant(c.sessionID)
}

func (h *relayHub) getOrCreateChannel(name string) *channelMixer {
	name = normalizeChannelName(name)
	if name == "" {
		return h.getOrCreateChannel("default")
	}
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
	m := newChannelMixer(name, h, h.cfg)
	h.channels[name] = m
	go m.run()
	return m
}

func (h *relayHub) markTxEOF(sessionID uint32) {
	c := h.getClient(sessionID)
	if c == nil {
		return
	}
	chName := c.getChannel()
	if chName == "" {
		return
	}
	h.getOrCreateChannel(chName).markEOF(sessionID)
}

func (h *relayHub) routePacket(pkt *audioPacket) {
	c := h.getClient(pkt.sessionID)
	if c == nil {
		return
	}
	c.setUDPAddr(pkt.srcAddr)
	if pkt.isUDPEOFMarker() {
		h.markTxEOF(pkt.sessionID)
		return
	}
	if len(pkt.opus) == 0 {
		// Empty-opus datagrams are treated as UDP keepalive punches.
		if pkt.seq == 0 && pkt.signalStrength == udpKeepaliveSignal {
			h.sendUDPKeepaliveAck(pkt.sessionID, pkt.srcAddr)
		}
		return
	}
	channelName := normalizeChannelName(c.getChannel())
	if channelName == "" {
		return
	}
	h.markChannelActivity(channelName)
	h.getOrCreateChannel(channelName).push(pkt)
}

func (h *relayHub) sendUDPKeepaliveAck(sessionID uint32, addr *net.UDPAddr) {
	if addr == nil {
		return
	}
	payload := make([]byte, 9)
	binary.BigEndian.PutUint32(payload[0:4], sessionID)
	binary.BigEndian.PutUint32(payload[4:8], 0)
	payload[8] = udpKeepaliveAck
	if _, err := h.udpConn.WriteToUDP(payload, addr); err != nil {
		log.Printf("udp keepalive ack failed to %s: %v", addr, err)
	}
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
		if excludeSession != 0 && c.sessionID == excludeSession {
			continue
		}
		if c.isRepeaterMode() {
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

func (h *relayHub) sendToClient(sessionID uint32, payload []byte) {
	c := h.getClient(sessionID)
	if c == nil {
		return
	}
	addr := c.getUDPAddr()
	if addr == nil {
		return
	}
	if _, err := h.udpConn.WriteToUDP(payload, addr); err != nil {
		log.Printf("udp send failed to %d (%s): %v", sessionID, addr, err)
	}
}

type channelMixer struct {
	name string
	hub  *relayHub

	input chan *audioPacket
	eof   chan uint32

	participantsMu sync.RWMutex
	participants   map[uint32]struct{}

	decodersMu sync.Mutex
	decoders   map[uint32]*opus.Decoder
	encoder    *opus.Encoder

	seq          uint32
	noiseLevelDB float64
	lastSignal   float64
	modules      []audioModule
	modulesCfg   modulesConfig

	repeaterMu    sync.Mutex
	repeaterState map[uint32]*repeaterSession

	lastSingleSpeaker   uint32
	lastSingleSpeakerAt time.Time
	hangoverDur         time.Duration
	eofTimeoutDur       time.Duration
	concealDecay        float64
	jitterMinPackets    uint16
	busyMode            bool
	busyMu              sync.Mutex
	busySessionID       uint32
	busyLastPacketAt    time.Time
	busyExplicitEOF     bool
	transmitTimeout     time.Duration
	txMu                sync.Mutex
	txStartedAt         map[uint32]time.Time
	txForceStopped      map[uint32]bool
	txLastStopNoticeAt  map[uint32]time.Time

	lastFrameMu      sync.RWMutex
	lastFramePayload []byte
	lastFrameAt      time.Time
}

type repeaterSession struct {
	processor  *audioProcessor
	bufferPCM  []int16
	lastPacket time.Time
	collecting bool
	lastSignal float64
	eofMarked  bool
}

type audioProcessor struct {
	modules      []audioModule
	noiseLevelDB float64
	lastSignal   float64
}

func newAudioProcessor(mcfg modulesConfig) *audioProcessor {
	noiseFloor := -30.0
	if mcfg.Noise != nil {
		noiseFloor = mcfg.Noise.MinNoiseDB
	}
	return &audioProcessor{
		modules:      buildAudioModules(mcfg),
		noiseLevelDB: noiseFloor,
		lastSignal:   255.0,
	}
}

func (p *audioProcessor) process(input []int16, signalByte float64, active bool) ([]int16, bool) {
	mixed := make([]float64, packetSamples)
	if active {
		p.lastSignal = signalByte
		limit := len(input)
		if limit > packetSamples {
			limit = packetSamples
		}
		for i := 0; i < limit; i++ {
			mixed[i] = float64(input[i])
		}
	}
	ctx := &audioProcessContext{
		Mixed:          mixed,
		AvgSignalByte:  p.lastSignal,
		ActiveSpeakers: boolToInt(active),
		NoiseLevelDB:   p.noiseLevelDB,
		EmitFrame:      active,
	}
	for _, mod := range p.modules {
		mod.Process(ctx)
		if ctx.DropFrame {
			p.noiseLevelDB = ctx.NoiseLevelDB
			return nil, false
		}
	}
	p.noiseLevelDB = ctx.NoiseLevelDB
	if !ctx.EmitFrame {
		return nil, false
	}

	out := make([]int16, packetSamples)
	for i := 0; i < packetSamples; i++ {
		out[i] = hardClipFloat(ctx.Mixed[i], 15000.0)
	}
	return out, true
}

func newChannelMixer(name string, hub *relayHub, cfg appConfig) *channelMixer {
	mcfg := cfg.Modules
	enc, err := opus.NewEncoder(configuredSampleRate, audioChannels, resolveOpusApplication(configuredOpus.Application))
	if err != nil {
		panic(fmt.Sprintf("create encoder for channel %s: %v", name, err))
	}
	applyOpusEncoderConfig(enc)
	hangoverMs := maxInt(cfg.Server.HangoverMs, normalizePacketMs(cfg.Server.PacketMs)*2)
	eofTimeoutMs := maxInt(cfg.Server.EOFTimeoutMs, hangoverMs+normalizePacketMs(cfg.Server.PacketMs))
	return &channelMixer{
		name:               name,
		hub:                hub,
		input:              make(chan *audioPacket, 256),
		eof:                make(chan uint32, 64),
		participants:       make(map[uint32]struct{}),
		decoders:           make(map[uint32]*opus.Decoder),
		encoder:            enc,
		noiseLevelDB:       -30.0,
		lastSignal:         255.0,
		modulesCfg:         mcfg,
		modules:            buildAudioModules(mcfg),
		repeaterState:      make(map[uint32]*repeaterSession),
		hangoverDur:        time.Duration(hangoverMs) * time.Millisecond,
		eofTimeoutDur:      time.Duration(eofTimeoutMs) * time.Millisecond,
		concealDecay:       cfg.Server.ConcealDecay,
		jitterMinPackets:   uint16(normalizeJitterMinPackets(cfg.Server.JitterMinPkts)),
		busyMode:           cfg.Server.BusyMode,
		transmitTimeout:    time.Duration(maxInt(cfg.Server.TransmitTimeoutSec, 0)) * time.Second,
		txStartedAt:        make(map[uint32]time.Time),
		txForceStopped:     make(map[uint32]bool),
		txLastStopNoticeAt: make(map[uint32]time.Time),
	}
}

func (m *channelMixer) canAcceptTxPacket(sessionID uint32) bool {
	now := time.Now()
	m.txMu.Lock()
	shouldNotify := false
	accept := true
	if m.transmitTimeout > 0 {
		startedAt, started := m.txStartedAt[sessionID]
		if !started {
			m.txStartedAt[sessionID] = now
		} else if m.txForceStopped[sessionID] {
			shouldNotify = m.shouldNotifyTxStopLocked(sessionID, now)
			accept = false
		} else if now.Sub(startedAt) >= m.transmitTimeout {
			m.txForceStopped[sessionID] = true
			shouldNotify = m.shouldNotifyTxStopLocked(sessionID, now)
			accept = false
		}
	}
	m.txMu.Unlock()
	if shouldNotify {
		m.notifyTxStop(sessionID)
	}
	if !accept {
		return false
	}
	if !m.busyMode {
		return true
	}
	return m.tryAcceptPacket(sessionID)
}

func (m *channelMixer) shouldNotifyTxStopLocked(sessionID uint32, now time.Time) bool {
	last := m.txLastStopNoticeAt[sessionID]
	if !last.IsZero() && now.Sub(last) < time.Second {
		return false
	}
	m.txLastStopNoticeAt[sessionID] = now
	return true
}

func (m *channelMixer) notifyTxStop(sessionID uint32) {
	if c := m.hub.getClient(sessionID); c != nil {
		_ = c.writeJSON(wsServerMessage{
			Type: "tx_stop",
			Info: "transmit_timeout_reached",
		})
	}
}

func (m *channelMixer) tryAcceptPacket(sessionID uint32) bool {
	if !m.busyMode {
		return true
	}
	now := time.Now()
	m.busyMu.Lock()
	defer m.busyMu.Unlock()

	if m.busySessionID == 0 || m.busyWindowExpiredLocked(now) {
		m.busySessionID = sessionID
		m.busyLastPacketAt = now
		m.busyExplicitEOF = false
		return true
	}
	if m.busySessionID == sessionID {
		m.busyLastPacketAt = now
		m.busyExplicitEOF = false
		return true
	}
	return false
}

func (m *channelMixer) markBusyEOF(sessionID uint32) {
	if !m.busyMode {
		return
	}
	m.busyMu.Lock()
	defer m.busyMu.Unlock()
	if m.busySessionID != sessionID {
		return
	}
	m.busyExplicitEOF = true
	m.busyLastPacketAt = time.Now()
}

func (m *channelMixer) busyWindowExpiredLocked(now time.Time) bool {
	if m.busySessionID == 0 {
		return true
	}
	hold := m.hangoverDur
	if m.busyExplicitEOF {
		hold = packetDur
	}
	return now.Sub(m.busyLastPacketAt) > hold
}

func buildAudioModules(mcfg modulesConfig) []audioModule {
	mods := make([]audioModule, 0, 5)
	if mcfg.Noise != nil && mcfg.Noise.Enabled {
		mods = append(mods, newWhiteNoiseSquelchModule(packetDur, *mcfg.Noise))
	}
	if mcfg.Click != nil && mcfg.Click.Enabled {
		mods = append(mods, newClickModule(packetDur, *mcfg.Click))
	}
	if mcfg.Filter != nil && mcfg.Filter.Enabled {
		mods = append(mods, newBandPassModule(configuredSampleRate, *mcfg.Filter))
	}
	if mcfg.Compressor != nil && mcfg.Compressor.Enabled {
		mods = append(mods, newCompressorModule(configuredSampleRate, *mcfg.Compressor))
	}
	if mcfg.Distortion != nil && mcfg.Distortion.Enabled {
		mods = append(mods, newDistortionModule(*mcfg.Distortion))
	}
	return mods
}

func newSpeakerStreamState(jitterMinPackets uint16) *speakerStreamState {
	minStart := int(jitterMinPackets)
	if minStart < 1 {
		minStart = 1
	}
	maxKeep := minStart * 8
	if maxKeep < 32 {
		maxKeep = 32
	}
	return &speakerStreamState{
		jitter: &speakerJitterBuffer{
			minStart: minStart,
			maxKeep:  maxKeep,
			packets:  make(map[uint32]*audioPacket),
		},
	}
}

func (b *speakerJitterBuffer) reset() {
	b.packets = make(map[uint32]*audioPacket)
	b.head = 0
	b.started = false
}

func (b *speakerJitterBuffer) minSeq() (uint32, bool) {
	if len(b.packets) == 0 {
		return 0, false
	}
	var min uint32
	first := true
	for seq := range b.packets {
		if first || seq < min {
			min = seq
			first = false
		}
	}
	return min, true
}

func (b *speakerJitterBuffer) maxSeq() (uint32, bool) {
	if len(b.packets) == 0 {
		return 0, false
	}
	var max uint32
	first := true
	for seq := range b.packets {
		if first || seq > max {
			max = seq
			first = false
		}
	}
	return max, true
}

func (b *speakerJitterBuffer) push(pkt *audioPacket) {
	if b == nil || pkt == nil {
		return
	}
	if _, exists := b.packets[pkt.seq]; exists {
		return
	}
	b.packets[pkt.seq] = pkt
	if !b.started && len(b.packets) >= b.minStart {
		if minSeq, ok := b.minSeq(); ok {
			b.head = minSeq
			b.started = true
		}
	}
	for len(b.packets) > b.maxKeep {
		minSeq, ok := b.minSeq()
		if !ok {
			break
		}
		delete(b.packets, minSeq)
		if b.started && minSeq >= b.head {
			b.head = minSeq + 1
		}
	}
}

func (b *speakerJitterBuffer) pop() *audioPacket {
	if b == nil {
		return nil
	}
	if len(b.packets) == 0 {
		b.started = false
		return nil
	}
	if !b.started {
		if len(b.packets) < b.minStart {
			return nil
		}
		minSeq, ok := b.minSeq()
		if !ok {
			return nil
		}
		b.head = minSeq
		b.started = true
	}
	if pkt, ok := b.packets[b.head]; ok {
		delete(b.packets, b.head)
		b.head++
		return pkt
	}
	// Missing head: skip forward if queue has moved on.
	minSeq, minOk := b.minSeq()
	maxSeq, maxOk := b.maxSeq()
	if !minOk || !maxOk {
		b.started = false
		return nil
	}
	if b.head < minSeq {
		b.head = minSeq
		if pkt, ok := b.packets[b.head]; ok {
			delete(b.packets, b.head)
			b.head++
			return pkt
		}
	}
	if b.head+uint32(b.maxKeep/2) < maxSeq {
		b.head = minSeq
		if pkt, ok := b.packets[b.head]; ok {
			delete(b.packets, b.head)
			b.head++
			return pkt
		}
	}
	return nil
}

func (b *speakerJitterBuffer) isStarted() bool {
	if b == nil {
		return false
	}
	return b.started
}

func (b *speakerJitterBuffer) pendingCount() int {
	if b == nil {
		return 0
	}
	return len(b.packets)
}

func (b *speakerJitterBuffer) forceStartFromMinSeq() {
	if b == nil || len(b.packets) == 0 {
		return
	}
	minSeq, ok := b.minSeq()
	if !ok {
		return
	}
	b.head = minSeq
	b.started = true
}

func (m *channelMixer) pullNextPacket(sessionID uint32, st *speakerStreamState) *audioPacket {
	pkt := st.jitter.pop()
	if pkt == nil {
		return nil
	}
	return pkt
}

func (m *channelMixer) addParticipant(sessionID uint32) {
	m.participantsMu.Lock()
	defer m.participantsMu.Unlock()
	m.participants[sessionID] = struct{}{}
	go m.sendLatestFrameToParticipant(sessionID)
}

func (m *channelMixer) removeParticipant(sessionID uint32) {
	m.participantsMu.Lock()
	delete(m.participants, sessionID)
	m.participantsMu.Unlock()

	m.decodersMu.Lock()
	delete(m.decoders, sessionID)
	m.decodersMu.Unlock()

	m.clearRepeaterState(sessionID)
}

func (m *channelMixer) clearRepeaterState(sessionID uint32) {
	m.repeaterMu.Lock()
	defer m.repeaterMu.Unlock()
	delete(m.repeaterState, sessionID)
}

func (m *channelMixer) rememberLatestFrame(payload []byte) {
	if len(payload) == 0 {
		return
	}
	m.lastFrameMu.Lock()
	m.lastFramePayload = append(m.lastFramePayload[:0], payload...)
	m.lastFrameAt = time.Now()
	m.lastFrameMu.Unlock()
}

func (m *channelMixer) latestFramePayload(maxAge time.Duration) []byte {
	m.lastFrameMu.RLock()
	defer m.lastFrameMu.RUnlock()
	if len(m.lastFramePayload) == 0 {
		return nil
	}
	if maxAge > 0 && time.Since(m.lastFrameAt) > maxAge {
		return nil
	}
	out := make([]byte, len(m.lastFramePayload))
	copy(out, m.lastFramePayload)
	return out
}

func (m *channelMixer) sendLatestFrameToParticipant(sessionID uint32) {
	payload := m.latestFramePayload(2 * packetDur)
	if len(payload) == 0 {
		return
	}
	m.hub.sendToClient(sessionID, payload)
}

func (m *channelMixer) push(pkt *audioPacket) {
	if !m.canAcceptTxPacket(pkt.sessionID) {
		return
	}
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

func (m *channelMixer) markEOF(sessionID uint32) {
	m.txMu.Lock()
	delete(m.txStartedAt, sessionID)
	delete(m.txForceStopped, sessionID)
	delete(m.txLastStopNoticeAt, sessionID)
	m.txMu.Unlock()
	m.markBusyEOF(sessionID)
	select {
	case m.eof <- sessionID:
	default:
		select {
		case <-m.eof:
		default:
		}
		select {
		case m.eof <- sessionID:
		default:
		}
	}
}

func (m *channelMixer) run() {
	ticker := time.NewTicker(packetDur)
	defer ticker.Stop()

	states := make(map[uint32]*speakerStreamState)
	eofMarked := make(map[uint32]bool)

	for {
		select {
		case pkt := <-m.input:
			st := states[pkt.sessionID]
			if st == nil {
				st = newSpeakerStreamState(m.jitterMinPackets)
				states[pkt.sessionID] = st
			}
			prevLastPacketAt := st.lastPacketAt
			now := time.Now()
			// If a new burst starts after a gap and the previous short burst never reached
			// jitter start threshold (e.g. lost UDP EOF), drop stale buffered leftovers.
			// Otherwise those packets can be emitted at the start of the next long TX.
			if !prevLastPacketAt.IsZero() &&
				now.Sub(prevLastPacketAt) > (3*packetDur) &&
				!st.jitter.isStarted() &&
				st.jitter.pendingCount() > 0 {
				st.jitter.reset()
				st.lastPCM = nil
				st.missCount = 0
			}
			st.jitter.push(pkt)
			st.lastPacketAt = now
			st.lastSignal = float64(pkt.signalStrength)
			if eofMarked[pkt.sessionID] {
				// Do not immediately clear explicit EOF on trailing packets that are still
				// draining from network/jitter. Clear only when a new burst starts
				// after a noticeable gap.
				if !prevLastPacketAt.IsZero() && time.Since(prevLastPacketAt) > (3*packetDur) {
					eofMarked[pkt.sessionID] = false
				}
			} else {
				eofMarked[pkt.sessionID] = false
			}
		case sid := <-m.eof:
			eofMarked[sid] = true
			if st, ok := states[sid]; ok && st != nil {
				// Short bursts can end before reaching jitter_min_packets.
				// Force jitter start on EOF so buffered short TX drains now,
				// instead of sticking until the next long transmission.
				st.jitter.forceStartFromMinSeq()
			}
			m.markRepeaterEOF(sid)
			// Keep already buffered packets for a short natural drain, but disable
			// concealment via eofMarked in mix logic. This avoids truncating the tail
			// of TX when tx_eof arrives slightly before the last UDP frames.
		case <-ticker.C:
			m.mixAndBroadcast(states, eofMarked)
			m.cleanupStreamStates(states, eofMarked, time.Now())
		}
	}
}

func (m *channelMixer) cleanupStreamStates(states map[uint32]*speakerStreamState, eofMarked map[uint32]bool, now time.Time) {
	m.participantsMu.RLock()
	participants := make(map[uint32]struct{}, len(m.participants))
	for sid := range m.participants {
		participants[sid] = struct{}{}
	}
	m.participantsMu.RUnlock()

	m.txMu.Lock()
	defer m.txMu.Unlock()
	for sid, st := range states {
		_, present := participants[sid]
		if !present {
			delete(states, sid)
			delete(eofMarked, sid)
			delete(m.txStartedAt, sid)
			delete(m.txForceStopped, sid)
			delete(m.txLastStopNoticeAt, sid)
			continue
		}
		if now.Sub(st.lastPacketAt) > m.eofTimeoutDur && st.pending == nil {
			delete(states, sid)
			delete(eofMarked, sid)
			delete(m.txStartedAt, sid)
			delete(m.txForceStopped, sid)
			delete(m.txLastStopNoticeAt, sid)
		}
	}
}

func (m *channelMixer) mixAndBroadcast(states map[uint32]*speakerStreamState, eofMarked map[uint32]bool) {
	mixed := make([]float64, packetSamples)
	activeSpeakers := make([]uint32, 0, len(states))
	var signalSum float64
	now := time.Now()

	for sessionID, st := range states {
		if st == nil {
			continue
		}
		// If a short burst did not reach jitter start threshold and then paused,
		// force-start queued packets so they are drained now (even without explicit EOF).
		if st.jitter.pendingCount() > 0 && !st.jitter.isStarted() && now.Sub(st.lastPacketAt) > packetDur {
			st.jitter.forceStartFromMinSeq()
		}
		pkt := m.pullNextPacket(sessionID, st)
		hasPacket := pkt != nil
		speakerActive := false
		speakerSignal := st.lastSignal
		var pcmFrame []int16

		var (
			dec       *opus.Decoder
			createErr error
		)
		m.decodersMu.Lock()
		dec = m.decoders[sessionID]
		if dec == nil {
			dec, createErr = opus.NewDecoder(configuredSampleRate, audioChannels)
			if createErr == nil {
				m.decoders[sessionID] = dec
			}
		}
		m.decodersMu.Unlock()
		if createErr != nil {
			log.Printf("decoder create failed for %d: %v", sessionID, createErr)
			continue
		}

		if hasPacket {
			pcm := make([]int16, packetSamples)
			n, err := dec.Decode(pkt.opus, pcm)
			if err != nil {
				log.Printf("opus decode failed for %d: %v", sessionID, err)
				// Treat decode failures like temporary packet loss:
				// keep stream active via concealment instead of triggering tail/squelch.
				hasPacket = false
			} else if n > 0 {
				pcmFrame = make([]int16, packetSamples)
				maxN := n
				if maxN > packetSamples {
					maxN = packetSamples
				}
				for i := 0; i < maxN; i++ {
					pcmFrame[i] = pcm[i]
				}
				st.lastPCM = append(st.lastPCM[:0], pcmFrame...)
				st.missCount = 0
				st.lastPacketAt = now
				st.lastSignal = float64(pkt.signalStrength)
				speakerSignal = st.lastSignal
				speakerActive = true
			} else {
				// Zero-sample decode behaves like a gap in transport.
				hasPacket = false
			}
		}
		if !speakerActive && !hasPacket && !eofMarked[sessionID] && now.Sub(st.lastPacketAt) <= m.eofTimeoutDur && len(st.lastPCM) > 0 {
			decay := math.Pow(m.concealDecay, float64(st.missCount+1))
			if now.Sub(st.lastPacketAt) > m.hangoverDur {
				// After hangover window we keep stream alive, but fade faster
				// until implicit EOF timeout to avoid abrupt squelch spikes.
				decay *= math.Pow(0.80, float64(st.missCount+1))
			}
			if decay < 0.0 {
				decay = 0.0
			}
			if decay > 1.0 {
				decay = 1.0
			}
			pcmFrame = make([]int16, len(st.lastPCM))
			for i := range st.lastPCM {
				pcmFrame[i] = int16(float64(st.lastPCM[i]) * decay)
			}
			st.missCount++
			speakerActive = true
		} else if !speakerActive && now.Sub(st.lastPacketAt) > m.eofTimeoutDur {
			eofMarked[sessionID] = true
		}

		if !speakerActive || len(pcmFrame) == 0 {
			continue
		}
		client := m.hub.getClient(sessionID)
		if client != nil && client.isRepeaterMode() {
			m.processRepeaterCapture(sessionID, pcmFrame, speakerSignal, now)
			continue
		}
		activeSpeakers = append(activeSpeakers, sessionID)
		signalSum += speakerSignal
		for i := 0; i < packetSamples; i++ {
			mixed[i] += float64(pcmFrame[i])
		}
	}

	m.processRepeaterFinalization(now)

	if len(activeSpeakers) == 0 {
		signalSum = m.lastSignal
	} else {
		m.lastSignal = signalSum / float64(len(activeSpeakers))
	}
	if len(activeSpeakers) == 1 {
		m.lastSingleSpeaker = activeSpeakers[0]
		m.lastSingleSpeakerAt = now
	} else if len(activeSpeakers) > 1 {
		m.lastSingleSpeaker = 0
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

	outPCM := make([]int16, packetSamples)
	for i := 0; i < packetSamples; i++ {
		// Saturated character: hard clip after sum.
		outPCM[i] = hardClipFloat(mixed[i], 15000.0)
	}

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
	header := make([]byte, 9)
	binary.BigEndian.PutUint32(header[0:4], 0)
	binary.BigEndian.PutUint32(header[4:8], m.seq)
	header[8] = uint8(ctx.AvgSignalByte)
	payload := append(header, opusBuf[:n]...)
	m.rememberLatestFrame(payload)
	exclude := uint32(0)
	if len(activeSpeakers) == 1 {
		exclude = activeSpeakers[0]
	} else if len(activeSpeakers) == 0 && m.lastSingleSpeaker != 0 {
		// During post-TX tail frames, avoid feeding a sender its own ending burst.
		packetMs := int(packetDur / time.Millisecond)
		tailMaxMs := packetMs * 2
		if m.modulesCfg.Noise != nil && m.modulesCfg.Noise.Enabled {
			tailMaxMs = maxInt(m.modulesCfg.Noise.TailMaxMs, packetMs*2)
		}
		tailGrace := time.Duration(tailMaxMs) * time.Millisecond
		if now.Sub(m.lastSingleSpeakerAt) <= tailGrace {
			exclude = m.lastSingleSpeaker
		}
	}
	m.hub.broadcastMixed(m.name, exclude, opusBuf[:n], m.seq, uint8(ctx.AvgSignalByte))
}

func (m *channelMixer) processRepeaterCapture(sessionID uint32, pcm []int16, signalByte float64, now time.Time) {
	m.repeaterMu.Lock()
	defer m.repeaterMu.Unlock()
	st := m.repeaterState[sessionID]
	if st == nil {
		st = &repeaterSession{
			processor:  newAudioProcessor(m.modulesCfg),
			lastSignal: 255.0,
		}
		m.repeaterState[sessionID] = st
	}
	out, emit := st.processor.process(pcm, signalByte, true)
	if emit {
		st.bufferPCM = append(st.bufferPCM, out...)
	}
	st.eofMarked = false
	st.collecting = true
	st.lastPacket = now
	st.lastSignal = signalByte
}

func (m *channelMixer) markRepeaterEOF(sessionID uint32) {
	m.repeaterMu.Lock()
	defer m.repeaterMu.Unlock()
	st := m.repeaterState[sessionID]
	if st == nil {
		return
	}
	st.eofMarked = true
}

func (m *channelMixer) processRepeaterFinalization(now time.Time) {
	m.repeaterMu.Lock()
	defer m.repeaterMu.Unlock()
	for sessionID, st := range m.repeaterState {
		client := m.hub.getClient(sessionID)
		if client == nil || !client.isRepeaterMode() {
			delete(m.repeaterState, sessionID)
			continue
		}
		if !st.collecting {
			continue
		}
		if !st.eofMarked && now.Sub(st.lastPacket) < (2*packetDur) {
			continue
		}

		for i := 0; i < 16; i++ {
			out, emit := st.processor.process(nil, st.lastSignal, false)
			if !emit {
				break
			}
			st.bufferPCM = append(st.bufferPCM, out...)
		}

		bufferCopy := append([]int16(nil), st.bufferPCM...)
		st.bufferPCM = st.bufferPCM[:0]
		st.collecting = false
		st.eofMarked = false
		go m.playbackRepeaterAfterDelay(sessionID, bufferCopy, packetDur)
	}
}

func (m *channelMixer) playbackRepeaterAfterDelay(sessionID uint32, pcm []int16, delayDur time.Duration) {
	if len(pcm) == 0 {
		return
	}
	time.Sleep(delayDur)

	enc, err := opus.NewEncoder(configuredSampleRate, audioChannels, resolveOpusApplication(configuredOpus.Application))
	if err != nil {
		log.Printf("repeater encoder create failed: %v", err)
		return
	}
	applyOpusEncoderConfig(enc)

	var seq uint32
	for offset := 0; offset < len(pcm); offset += packetSamples {
		frame := make([]int16, packetSamples)
		end := offset + packetSamples
		if end > len(pcm) {
			end = len(pcm)
		}
		copy(frame, pcm[offset:end])

		opusBuf := make([]byte, opusMaxFrameLen)
		n, err := enc.Encode(frame, opusBuf)
		if err != nil || n <= 0 {
			continue
		}
		seq++
		header := make([]byte, 9)
		binary.BigEndian.PutUint32(header[0:4], 0)
		binary.BigEndian.PutUint32(header[4:8], seq)
		header[8] = 255
		payload := append(header, opusBuf[:n]...)
		m.hub.sendToClient(sessionID, payload)
		time.Sleep(packetDur)
	}
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
	shotNextAt         time.Time
	shotPhase          int
	shotPhaseRemain    time.Duration
	cfg                noiseConfig
}

type clickModule struct {
	clickAmplitude float64
	freqHz         float64
	burstSamples   int
	frameDuration  time.Duration
	glitchMaxMs    int
	glitchRemain   time.Duration
	glitchFreqMin  float64
	glitchFreqMax  float64
	glitchAmpMin   float64
	glitchAmpMax   float64
	lastActive     bool
	pendingEnd     bool
}

func newClickModule(frameDuration time.Duration, cfg clickConfig) *clickModule {
	amp := 32767.0 * dbToLinear(cfg.ClickDB)
	if amp < 0 {
		amp = 0
	}
	if amp > 32767.0 {
		amp = 32767.0
	}
	return &clickModule{
		clickAmplitude: amp,
		freqHz:         200.0,
		burstSamples:   configuredSampleRate / 80, // ~12.5 ms at current sample rate.
		frameDuration:  frameDuration,
		glitchMaxMs:    maxInt(cfg.GlitchIntervalMaxMs, 0),
		glitchFreqMin:  cfg.GlitchFreqMinHz,
		glitchFreqMax:  cfg.GlitchFreqMaxHz,
		glitchAmpMin:   32767.0 * dbToLinear(cfg.GlitchLevelMinDB),
		glitchAmpMax:   32767.0 * dbToLinear(cfg.GlitchLevelMaxDB),
	}
}

func (m *clickModule) Name() string {
	return "tx_click"
}

func (m *clickModule) Process(ctx *audioProcessContext) {
	active := ctx.ActiveSpeakers > 0
	if active && !m.lastActive {
		m.injectClick(ctx, 1.0)
		ctx.EmitFrame = true
		m.pendingEnd = false
		m.scheduleNextGlitch()
	} else if !active && m.lastActive {
		// Speaker just released PTT: wait until squelch/noise tail closes,
		// then emit exactly one terminal click.
		m.pendingEnd = true
		m.glitchRemain = 0
	}
	if active {
		m.processRandomGlitchClicks(ctx)
	}

	if !active && m.pendingEnd && !ctx.EmitFrame {
		m.injectClick(ctx, -1.0)
		ctx.EmitFrame = true
		m.pendingEnd = false
		m.lastActive = false
		return
	}
	m.lastActive = active
}

func (m *clickModule) processRandomGlitchClicks(ctx *audioProcessContext) {
	if m.glitchMaxMs <= 0 || len(ctx.Mixed) == 0 || m.glitchAmpMax <= 0 {
		return
	}
	if m.glitchRemain <= 0 {
		freq := randomFloat64(m.glitchFreqMin, m.glitchFreqMax)
		amp := randomFloat64(m.glitchAmpMin, m.glitchAmpMax)
		m.injectClickWithParams(ctx, 1.0, freq, amp)
		m.scheduleNextGlitch()
		return
	}
	m.glitchRemain -= m.frameDuration
}

func (m *clickModule) scheduleNextGlitch() {
	if m.glitchMaxMs <= 0 {
		m.glitchRemain = 0
		return
	}
	m.glitchRemain = randomDurationMs(1, m.glitchMaxMs)
}

func (m *clickModule) injectClick(ctx *audioProcessContext, sign float64) {
	m.injectClickWithParams(ctx, sign, m.freqHz, m.clickAmplitude)
}

func (m *clickModule) injectClickWithParams(ctx *audioProcessContext, sign float64, freqHz float64, amplitude float64) {
	if len(ctx.Mixed) == 0 || amplitude <= 0 || m.burstSamples <= 0 || freqHz <= 0 {
		return
	}
	limit := m.burstSamples
	if limit > len(ctx.Mixed) {
		limit = len(ctx.Mixed)
	}
	phaseOffset := 0.0
	if sign < 0 {
		phaseOffset = math.Pi
	}
	for i := 0; i < limit; i++ {
		t := float64(i) / float64(configuredSampleRate)
		env := math.Exp(-4.0 * float64(i) / float64(limit))
		wave := math.Sin(2.0*math.Pi*freqHz*t + phaseOffset)
		ctx.Mixed[i] += wave * amplitude * env
	}
}

func newWhiteNoiseSquelchModule(frameDuration time.Duration, cfg noiseConfig) *whiteNoiseSquelchModule {
	return &whiteNoiseSquelchModule{
		white:         newWhiteNoise(),
		frameDuration: frameDuration,
		cfg:           cfg,
	}
}

func (m *whiteNoiseSquelchModule) Name() string {
	return "white_noise_squelch"
}

const (
	shotPhaseNone = iota
	shotPhasePreSilence
	shotPhaseNoise
	shotPhasePostSilence
)

func (m *whiteNoiseSquelchModule) Process(ctx *audioProcessContext) {
	now := time.Now()
	if ctx.ActiveSpeakers <= 0 {
		// End-of-transmission tail burst: jump to 0 dB and emit white hiss briefly.
		if m.lastActive && m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(m.cfg.TailMinMs, m.cfg.TailMaxMs)
		}
		m.lastActive = false
		if m.squelchBurstRemain > 0 {
			noiseAmplitude := m.cfg.NoiseGain * dbToLinear(m.cfg.TailNoiseDB)
			for i := range ctx.Mixed {
				ctx.Mixed[i] += m.white.next() * noiseAmplitude
			}
			ctx.EmitFrame = true
			m.squelchBurstRemain -= m.frameDuration
			if m.squelchBurstRemain <= 0 {
				m.squelchBurstRemain = 0
				if m.cfg.SquelchShotsMaxS > 0 {
					m.shotNextAt = now.Add(randomDurationSec(m.cfg.SquelchShotsMinS, m.cfg.SquelchShotsMaxS))
				}
			}
			return
		}
		if m.cfg.SquelchShotsMaxS > 0 {
			m.squelchLatched = false
			m.processIdleShots(ctx, now)
			return
		}
		m.squelchLatched = false
		ctx.EmitFrame = false
		return
	}
	m.lastActive = true
	m.shotNextAt = time.Time{}
	m.shotPhase = shotPhaseNone
	m.shotPhaseRemain = 0

	noiseDB := m.cfg.MinNoiseDB
	if m.cfg.SignalDependent {
		noiseDB = mapSignalByteToNoiseDB(ctx.AvgSignalByte, m.cfg)
	}
	ctx.NoiseLevelDB = noiseDB
	noiseAmplitude := m.cfg.NoiseGain * dbToLinear(noiseDB)

	// Squelch behavior: when the line is too weak, emit a short burst of hiss then gate output.
	if noiseDB >= m.cfg.SquelchThresholdDB {
		if m.squelchLatched {
			ctx.DropFrame = true
			return
		}
		if m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(m.cfg.SquelchMinMs, m.cfg.SquelchMaxMs)
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

func (m *whiteNoiseSquelchModule) processIdleShots(ctx *audioProcessContext, now time.Time) {
	if m.shotPhase == shotPhaseNone {
		if m.shotNextAt.IsZero() {
			m.shotNextAt = now.Add(randomDurationSec(m.cfg.SquelchShotsMinS, m.cfg.SquelchShotsMaxS))
			ctx.EmitFrame = false
			return
		}
		if now.Before(m.shotNextAt) {
			ctx.EmitFrame = false
			return
		}
		m.shotPhase = shotPhasePreSilence
		m.shotPhaseRemain = time.Second
	}

	switch m.shotPhase {
	case shotPhasePreSilence:
		ctx.EmitFrame = true
		m.advanceShotPhase(now, randomDurationMs(m.cfg.TailMinMs, m.cfg.TailMaxMs))
	case shotPhaseNoise:
		// Use the same hiss profile as TX-end tail to keep squelch shots consistent.
		noiseAmplitude := m.cfg.NoiseGain * dbToLinear(m.cfg.TailNoiseDB)
		for i := range ctx.Mixed {
			ctx.Mixed[i] += m.white.next() * noiseAmplitude
		}
		ctx.EmitFrame = true
		m.advanceShotPhase(now, time.Second)
	case shotPhasePostSilence:
		ctx.EmitFrame = true
		m.advanceShotPhase(now, 0)
	default:
		ctx.EmitFrame = false
	}
}

func (m *whiteNoiseSquelchModule) advanceShotPhase(now time.Time, nextPhaseDuration time.Duration) {
	m.shotPhaseRemain -= m.frameDuration
	if m.shotPhaseRemain > 0 {
		return
	}
	switch m.shotPhase {
	case shotPhasePreSilence:
		m.shotPhase = shotPhaseNoise
		m.shotPhaseRemain = nextPhaseDuration
	case shotPhaseNoise:
		m.shotPhase = shotPhasePostSilence
		m.shotPhaseRemain = nextPhaseDuration
	case shotPhasePostSilence:
		m.shotPhase = shotPhaseNone
		m.shotPhaseRemain = 0
		m.shotNextAt = now.Add(randomDurationSec(m.cfg.SquelchShotsMinS, m.cfg.SquelchShotsMaxS))
	default:
		m.shotPhase = shotPhaseNone
		m.shotPhaseRemain = 0
	}
}

func mapSignalByteToNoiseDB(signalByte float64, cfg noiseConfig) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return cfg.MaxNoiseDB
	}
	if pct >= 100.0 {
		return cfg.MinNoiseDB
	}
	// Linear interpolation in dB domain:
	// 10% -> max noise dB, 100% -> min noise dB.
	return cfg.MaxNoiseDB - ((pct-10.0)/90.0)*(cfg.MaxNoiseDB-cfg.MinNoiseDB)
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

func randomDurationSec(minSec int, maxSec int) time.Duration {
	if maxSec <= minSec {
		return time.Duration(minSec) * time.Second
	}
	return time.Duration(minSec+rand.Intn(maxSec-minSec+1)) * time.Second
}

func randomFloat64(min float64, max float64) float64 {
	if max <= min {
		return min
	}
	return min + rand.Float64()*(max-min)
}

func maxInt(a int, b int) int {
	if a > b {
		return a
	}
	return b
}

func boolToInt(v bool) int {
	if v {
		return 1
	}
	return 0
}

func normalizePacketMs(ms int) int {
	if ms <= 0 {
		return defaultPacketMs
	}
	return ms
}

func normalizeSampleRate(rate int) int {
	switch rate {
	case 8000, 12000, 16000, 24000, 48000:
		return rate
	default:
		return defaultSampleRate
	}
}

func isSupportedSampleRate(rate int) bool {
	return rate == normalizeSampleRate(rate)
}

func isSupportedPacketMs(ms int) bool {
	switch ms {
	case 10, 20, 40, 60:
		return true
	default:
		return false
	}
}

func normalizeJitterMinPackets(count int) int {
	if count <= 0 {
		return 3
	}
	if count > 12 {
		return 12
	}
	return count
}

func normalizeOpusConfig(cfg opusConfig) opusConfig {
	out := cfg
	if out.Bitrate <= 0 {
		out.Bitrate = 12000
	}
	if out.Bitrate < 6000 {
		out.Bitrate = 6000
	}
	if out.Bitrate > 510000 {
		out.Bitrate = 510000
	}
	if out.Complexity < 0 || out.Complexity > 10 {
		out.Complexity = 5
	}
	switch strings.ToLower(strings.TrimSpace(out.Application)) {
	case "voip", "audio", "lowdelay":
		out.Application = strings.ToLower(strings.TrimSpace(out.Application))
	default:
		out.Application = "voip"
	}
	return out
}

func resolveOpusApplication(name string) opus.Application {
	switch strings.ToLower(strings.TrimSpace(name)) {
	case "audio":
		return opus.AppAudio
	case "lowdelay":
		return opus.AppRestrictedLowdelay
	default:
		return opus.AppVoIP
	}
}

func applyOpusEncoderConfig(enc *opus.Encoder) {
	if enc == nil {
		return
	}
	cfg := normalizeOpusConfig(configuredOpus)
	if err := enc.SetBitrate(cfg.Bitrate); err != nil {
		log.Printf("opus SetBitrate(%d) failed: %v", cfg.Bitrate, err)
	}
	if err := enc.SetComplexity(cfg.Complexity); err != nil {
		log.Printf("opus SetComplexity(%d) failed: %v", cfg.Complexity, err)
	}
	if err := enc.SetInBandFEC(cfg.FEC); err != nil {
		log.Printf("opus SetInBandFEC(%t) failed: %v", cfg.FEC, err)
	}
	if err := enc.SetDTX(cfg.DTX); err != nil {
		log.Printf("opus SetDTX(%t) failed: %v", cfg.DTX, err)
	}
}

func applyAudioTiming(sampleRate int, packetMs int) {
	configuredSampleRate = normalizeSampleRate(sampleRate)
	norm := normalizePacketMs(packetMs)
	packetDur = time.Duration(norm) * time.Millisecond
	packetSamples = configuredSampleRate * norm / 1000
}

type whiteNoise struct{}

func newWhiteNoise() *whiteNoise {
	return &whiteNoise{}
}

func (w *whiteNoise) next() float64 {
	return rand.Float64()*2 - 1
}

type compressorModule struct {
	thresholdDB  float64
	ratio        float64
	attackCoeff  float64
	releaseCoeff float64
	makeupGain   float64
	envelopeDB   float64
}

func newCompressorModule(sr int, cfg compressorConfig) *compressorModule {
	attackSec := cfg.AttackMs / 1000.0
	releaseSec := cfg.ReleaseMs / 1000.0
	if attackSec <= 0 {
		attackSec = 0.005
	}
	if releaseSec <= 0 {
		releaseSec = 0.08
	}
	return &compressorModule{
		thresholdDB:  cfg.ThresholdDB,
		ratio:        cfg.Ratio,
		attackCoeff:  math.Exp(-1.0 / (float64(sr) * attackSec)),
		releaseCoeff: math.Exp(-1.0 / (float64(sr) * releaseSec)),
		makeupGain:   dbToLinear(cfg.MakeupDB),
		envelopeDB:   -90.0,
	}
}

func (m *compressorModule) Name() string {
	return "compressor"
}

func (m *compressorModule) Process(ctx *audioProcessContext) {
	for i := range ctx.Mixed {
		x := ctx.Mixed[i]
		amp := math.Abs(x)
		inDB := -90.0
		if amp > 1e-9 {
			inDB = 20.0 * math.Log10(amp/32768.0)
		}

		// Attack when level rises, release when level falls.
		if inDB > m.envelopeDB {
			m.envelopeDB = m.attackCoeff*m.envelopeDB + (1.0-m.attackCoeff)*inDB
		} else {
			m.envelopeDB = m.releaseCoeff*m.envelopeDB + (1.0-m.releaseCoeff)*inDB
		}

		gainDB := 0.0
		if m.envelopeDB > m.thresholdDB {
			over := m.envelopeDB - m.thresholdDB
			compressedOver := over / m.ratio
			gainDB = compressedOver - over // negative attenuation
		}
		gain := dbToLinear(gainDB) * m.makeupGain
		ctx.Mixed[i] = x * gain
	}
}

type distortionModule struct {
	drive float64
	mix   float64
}

func newDistortionModule(cfg distortionConfig) *distortionModule {
	return &distortionModule{
		drive: cfg.Drive,
		mix:   cfg.Mix,
	}
}

func (m *distortionModule) Name() string {
	return "distortion"
}

func (m *distortionModule) Process(ctx *audioProcessContext) {
	for i := range ctx.Mixed {
		dry := ctx.Mixed[i] / 32768.0
		wet := math.Tanh(dry * m.drive)
		out := dry*(1.0-m.mix) + wet*m.mix
		ctx.Mixed[i] = out * 32768.0
	}
}

type bandPassModule struct {
	filter *bandPass
}

func newBandPassModule(sr int, cfg filterConfig) *bandPassModule {
	return &bandPassModule{
		filter: newBandPass(sr, cfg.LowCutHz, cfg.HighCutHz),
	}
}

func (m *bandPassModule) Name() string {
	return "band_pass"
}

func (m *bandPassModule) Process(ctx *audioProcessContext) {
	if m.filter == nil {
		return
	}
	for i := range ctx.Mixed {
		ctx.Mixed[i] = m.filter.processSample(ctx.Mixed[i])
	}
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
	hp []*onePoleHP
	lp []*onePoleLP
}

func newBandPass(sr int, lowCut float64, highCut float64) *bandPass {
	const poles = 4 // 4 * 6 dB/oct = 24 dB/oct per side
	if lowCut <= 0 && highCut <= 0 {
		return nil
	}
	hp := make([]*onePoleHP, 0, poles)
	lp := make([]*onePoleLP, 0, poles)
	if lowCut > 0 {
		for i := 0; i < poles; i++ {
			hp = append(hp, newOnePoleHP(sr, lowCut))
		}
	}
	if highCut > 0 {
		for i := 0; i < poles; i++ {
			lp = append(lp, newOnePoleLP(sr, highCut))
		}
	}
	return &bandPass{
		hp: hp,
		lp: lp,
	}
}

func (b *bandPass) process(frame []int16) {
	for i := range frame {
		x := float64(frame[i])
		z := b.processSample(x)
		frame[i] = hardClip(int32(z), 32767)
	}
}

func (b *bandPass) processSample(x float64) float64 {
	y := x
	for _, hp := range b.hp {
		y = hp.process(y)
	}
	for _, lp := range b.lp {
		y = lp.process(y)
	}
	return y
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

func defaultConfig() appConfig {
	return appConfig{
		Server: serverConfig{
			WSAddr:     ":5500",
			UDPAddr:    ":5505",
			SampleRate: defaultSampleRate,
			PacketMs:   defaultPacketMs,
			Opus: opusConfig{
				Bitrate:     12000,
				Complexity:  5,
				FEC:         true,
				DTX:         false,
				Application: "voip",
			},
			HangoverMs:         180,
			EOFTimeoutMs:       420,
			ConcealDecay:       0.90,
			JitterMinPkts:      3,
			BusyMode:           false,
			TransmitTimeoutSec: 0,
		},
		Modules: modulesConfig{
			Noise: &noiseConfig{
				Enabled:            true,
				SignalDependent:    true,
				MinNoiseDB:         -20.0,
				MaxNoiseDB:         -3.0,
				NoiseGain:          1200.0,
				SquelchThresholdDB: -3.0,
				SquelchMinMs:       50,
				SquelchMaxMs:       150,
				SquelchShotsMinS:   10,
				SquelchShotsMaxS:   0,
				TailNoiseDB:        0.0,
				TailMinMs:          15,
				TailMaxMs:          60,
			},
			Click: &clickConfig{
				Enabled:             true,
				ClickDB:             -8.0,
				GlitchIntervalMaxMs: 0,
				GlitchFreqMinHz:     120.0,
				GlitchFreqMaxHz:     360.0,
				GlitchLevelMinDB:    -14.0,
				GlitchLevelMaxDB:    -6.0,
			},
			Filter: &filterConfig{
				Enabled:   true,
				LowCutHz:  300.0,
				HighCutHz: 3000.0,
			},
			Compressor: &compressorConfig{
				Enabled:     true,
				ThresholdDB: -18.0,
				Ratio:       3.2,
				AttackMs:    5.0,
				ReleaseMs:   80.0,
				MakeupDB:    4.0,
			},
			Distortion: &distortionConfig{
				Enabled: true,
				Drive:   1.35,
				Mix:     0.28,
			},
		},
	}
}

func loadConfig(path string) (appConfig, error) {
	cfg := defaultConfig()
	data, err := os.ReadFile(path)
	if err != nil {
		return appConfig{}, fmt.Errorf("read config %s: %w", path, err)
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return appConfig{}, fmt.Errorf("parse config %s: %w", path, err)
	}
	cfg.Server.PacketMs = normalizePacketMs(cfg.Server.PacketMs)
	cfg.Server.JitterMinPkts = normalizeJitterMinPackets(cfg.Server.JitterMinPkts)
	cfg.Server.Opus = normalizeOpusConfig(cfg.Server.Opus)
	if err := validateConfig(cfg); err != nil {
		return appConfig{}, err
	}
	cfg.Server.SampleRate = normalizeSampleRate(cfg.Server.SampleRate)
	return cfg, nil
}

func validateConfig(cfg appConfig) error {
	if strings.TrimSpace(cfg.Server.WSAddr) == "" || strings.TrimSpace(cfg.Server.UDPAddr) == "" {
		return errors.New("server.ws_addr and server.udp_addr must be set")
	}
	if !isSupportedPacketMs(cfg.Server.PacketMs) {
		return errors.New("server.packet_ms must be one of: 10, 20, 40, 60")
	}
	if !isSupportedSampleRate(cfg.Server.SampleRate) {
		return errors.New("server.sample_rate must be one of: 8000, 12000, 16000, 24000, 48000")
	}
	if cfg.Server.Opus.Bitrate < 6000 || cfg.Server.Opus.Bitrate > 510000 {
		return errors.New("server.opus.bitrate must be in [6000..510000]")
	}
	if cfg.Server.Opus.Complexity < 0 || cfg.Server.Opus.Complexity > 10 {
		return errors.New("server.opus.complexity must be in [0..10]")
	}
	if cfg.Server.Opus.Application != normalizeOpusConfig(cfg.Server.Opus).Application {
		return errors.New("server.opus.application must be one of: voip, audio, lowdelay")
	}
	if cfg.Server.HangoverMs < cfg.Server.PacketMs {
		return errors.New("server.hangover_ms must be >= server.packet_ms")
	}
	if cfg.Server.EOFTimeoutMs < cfg.Server.HangoverMs {
		return errors.New("server.eof_timeout_ms must be >= server.hangover_ms")
	}
	if cfg.Server.ConcealDecay <= 0 || cfg.Server.ConcealDecay > 1 {
		return errors.New("server.conceal_decay must be in (0..1]")
	}
	if cfg.Server.JitterMinPkts < 1 || cfg.Server.JitterMinPkts > 12 {
		return errors.New("server.jitter_min_packets must be in [1..12]")
	}
	if cfg.Server.TransmitTimeoutSec < 0 {
		return errors.New("server.transmit_timeout must be >= 0")
	}
	if cfg.Modules.Compressor != nil && cfg.Modules.Compressor.Enabled {
		if cfg.Modules.Compressor.Ratio <= 0 {
			return errors.New("modules.compressor.ratio must be > 0")
		}
	}
	if cfg.Modules.Noise != nil && cfg.Modules.Noise.Enabled {
		if cfg.Modules.Noise.NoiseGain <= 0 {
			return errors.New("modules.noise.noise_gain must be > 0")
		}
		if cfg.Modules.Noise.SquelchShotsMinS < 0 {
			return errors.New("modules.noise.squelch_shots_min_s must be >= 0")
		}
		if cfg.Modules.Noise.SquelchShotsMaxS < 0 {
			return errors.New("modules.noise.squelch_shots_max_s must be >= 0")
		}
		if cfg.Modules.Noise.SquelchShotsMaxS > 0 {
			if cfg.Modules.Noise.SquelchShotsMinS <= 0 {
				return errors.New("modules.noise.squelch_shots_min_s must be > 0 when squelch shots are enabled")
			}
			if cfg.Modules.Noise.SquelchShotsMaxS < cfg.Modules.Noise.SquelchShotsMinS {
				return errors.New("modules.noise.squelch_shots_max_s must be >= squelch_shots_min_s")
			}
		}
		if cfg.Modules.Noise.SquelchMinMs <= 0 || cfg.Modules.Noise.SquelchMaxMs < cfg.Modules.Noise.SquelchMinMs {
			return errors.New("modules.noise squelch range is invalid")
		}
		if cfg.Modules.Noise.TailMinMs <= 0 || cfg.Modules.Noise.TailMaxMs < cfg.Modules.Noise.TailMinMs {
			return errors.New("modules.noise tail range is invalid")
		}
	}
	if cfg.Modules.Click != nil && cfg.Modules.Click.Enabled {
		if math.IsNaN(cfg.Modules.Click.ClickDB) || math.IsInf(cfg.Modules.Click.ClickDB, 0) {
			return errors.New("modules.click.click_db must be a finite number")
		}
		if cfg.Modules.Click.GlitchIntervalMaxMs < 0 {
			return errors.New("modules.click.glitch_interval_max_ms must be >= 0")
		}
		if math.IsNaN(cfg.Modules.Click.GlitchFreqMinHz) || math.IsInf(cfg.Modules.Click.GlitchFreqMinHz, 0) ||
			math.IsNaN(cfg.Modules.Click.GlitchFreqMaxHz) || math.IsInf(cfg.Modules.Click.GlitchFreqMaxHz, 0) {
			return errors.New("modules.click glitch_freq range must be finite")
		}
		if cfg.Modules.Click.GlitchFreqMinHz <= 0 || cfg.Modules.Click.GlitchFreqMaxHz < cfg.Modules.Click.GlitchFreqMinHz {
			return errors.New("modules.click glitch_freq range is invalid")
		}
		if math.IsNaN(cfg.Modules.Click.GlitchLevelMinDB) || math.IsInf(cfg.Modules.Click.GlitchLevelMinDB, 0) ||
			math.IsNaN(cfg.Modules.Click.GlitchLevelMaxDB) || math.IsInf(cfg.Modules.Click.GlitchLevelMaxDB, 0) {
			return errors.New("modules.click glitch_level range must be finite")
		}
		if cfg.Modules.Click.GlitchLevelMaxDB < cfg.Modules.Click.GlitchLevelMinDB {
			return errors.New("modules.click glitch_level range is invalid")
		}
	}
	if cfg.Modules.Distortion != nil && cfg.Modules.Distortion.Enabled {
		if cfg.Modules.Distortion.Mix < 0 || cfg.Modules.Distortion.Mix > 1 {
			return errors.New("modules.distortion.mix must be in [0..1]")
		}
	}
	if cfg.Modules.Filter != nil && cfg.Modules.Filter.Enabled {
		if math.IsNaN(cfg.Modules.Filter.LowCutHz) || math.IsInf(cfg.Modules.Filter.LowCutHz, 0) ||
			math.IsNaN(cfg.Modules.Filter.HighCutHz) || math.IsInf(cfg.Modules.Filter.HighCutHz, 0) {
			return errors.New("modules.filter cutoff range must be finite")
		}
		if cfg.Modules.Filter.LowCutHz < 0 || cfg.Modules.Filter.HighCutHz < 0 {
			return errors.New("modules.filter cutoff range is invalid")
		}
		if cfg.Modules.Filter.LowCutHz > 0 && cfg.Modules.Filter.HighCutHz > 0 &&
			cfg.Modules.Filter.HighCutHz <= cfg.Modules.Filter.LowCutHz {
			return errors.New("modules.filter cutoff range is invalid")
		}
	}
	return nil
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
	}
	s.hub.addClient(c)
	defer s.hub.removeClient(c.sessionID)

	_ = c.writeJSON(wsServerMessage{
		Type:       "welcome",
		SessionID:  c.sessionID,
		PacketMs:   normalizePacketMs(s.hub.cfg.Server.PacketMs),
		SampleRate: normalizeSampleRate(s.hub.cfg.Server.SampleRate),
		Opus: &wsOpusConfig{
			Bitrate:     normalizeOpusConfig(s.hub.cfg.Server.Opus).Bitrate,
			Complexity:  normalizeOpusConfig(s.hub.cfg.Server.Opus).Complexity,
			FEC:         normalizeOpusConfig(s.hub.cfg.Server.Opus).FEC,
			DTX:         normalizeOpusConfig(s.hub.cfg.Server.Opus).DTX,
			Application: normalizeOpusConfig(s.hub.cfg.Server.Opus).Application,
		},
		ProtocolVersion: protocolVersion,
		BusyMode:        boolPtr(s.hub.cfg.Server.BusyMode),
	})

	var initial wsMessage
	if err := c.conn.ReadJSON(&initial); err != nil {
		if !isExpectedDisconnectError(err) {
			log.Printf("ws initial bind read failed for %d: %v", c.sessionID, err)
		}
		return
	}
	initialType := strings.ToLower(strings.TrimSpace(initial.Type))
	if initialType == "has_activity" {
		initialChannel := normalizeChannelName(initial.Channel)
		active := s.hub.channelHasRecentActivity(initialChannel)
		_ = c.writeJSON(wsServerMessage{
			Type:    "has_activity",
			Channel: initialChannel,
			Active:  boolPtr(active),
		})
		return
	}
	if initialType != "" && initialType != "join" {
		_ = c.writeJSON(wsServerMessage{Type: "error", Info: "first message must bind channel"})
		_ = c.writeControl(
			websocket.CloseMessage,
			websocket.FormatCloseMessage(websocket.ClosePolicyViolation, "missing initial channel bind"),
			time.Now().Add(time.Second),
		)
		return
	}
	initialChannel := normalizeChannelName(initial.Channel)
	if initialChannel == "" {
		_ = c.writeJSON(wsServerMessage{Type: "error", Info: "channel is required in first message"})
		_ = c.writeControl(
			websocket.CloseMessage,
			websocket.FormatCloseMessage(websocket.ClosePolicyViolation, "missing initial channel bind"),
			time.Now().Add(time.Second),
		)
		return
	}
	s.hub.switchChannel(c, initialChannel)
	_ = c.writeJSON(wsServerMessage{Type: "joined", Channel: initialChannel})

	for {
		var msg wsMessage
		if err := c.conn.ReadJSON(&msg); err != nil {
			if !isExpectedDisconnectError(err) {
				log.Printf("ws read failed for %d: %v", c.sessionID, err)
			}
			return
		}
		switch strings.ToLower(msg.Type) {
		case "join", "switch_channel":
			_ = c.writeJSON(wsServerMessage{Type: "error", Info: "channel switch is disabled, reconnect with new channel"})
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
				chName := normalizeChannelName(c.getChannel())
				if chName != "" {
					s.hub.getOrCreateChannel(chName).sendLatestFrameToParticipant(c.sessionID)
				}
				_ = c.writeJSON(wsServerMessage{Type: "udp_registered", Info: host + ":" + strconv.Itoa(port)})
			}
		case "ping", "heartbeat":
			_ = c.writeJSON(wsServerMessage{Type: "pong"})
		case "repeater_mode":
			enabled := msg.RepeaterEnabled != nil && *msg.RepeaterEnabled
			c.setRepeaterMode(enabled)
			if !enabled {
				s.hub.getOrCreateChannel(c.getChannel()).clearRepeaterState(c.sessionID)
			}
			_ = c.writeJSON(wsServerMessage{Type: "repeater_mode", Info: strconv.FormatBool(enabled)})
		case "has_activity":
			ch := normalizeChannelName(msg.Channel)
			active := s.hub.channelHasRecentActivity(ch)
			_ = c.writeJSON(wsServerMessage{
				Type:    "has_activity",
				Channel: ch,
				Active:  boolPtr(active),
			})
		default:
			_ = c.writeJSON(wsServerMessage{Type: "error", Info: "unknown message type"})
		}
	}
}

func boolPtr(v bool) *bool {
	return &v
}

func isExpectedDisconnectError(err error) bool {
	if err == nil {
		return true
	}
	if errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed) {
		return true
	}
	if websocket.IsCloseError(
		err,
		websocket.CloseNormalClosure,
		websocket.CloseGoingAway,
		websocket.CloseNoStatusReceived,
		websocket.CloseAbnormalClosure,
	) {
		return true
	}
	msg := strings.ToLower(err.Error())
	return strings.Contains(msg, "unexpected eof")
}

func main() {
	if len(os.Args) > 2 {
		log.Fatalf("usage: %s [config-path]", os.Args[0])
	}
	cfgPath := configFilePath
	if len(os.Args) == 2 {
		cfgPath = strings.TrimSpace(os.Args[1])
		if cfgPath == "" {
			log.Fatalf("usage: %s [config-path]", os.Args[0])
		}
	}

	cfg, err := loadConfig(cfgPath)
	if err != nil {
		log.Fatalf("config error: %v", err)
	}
	applyAudioTiming(cfg.Server.SampleRate, cfg.Server.PacketMs)
	configuredOpus = normalizeOpusConfig(cfg.Server.Opus)

	rand.Seed(time.Now().UnixNano())

	udpConn, err := net.ListenUDP("udp", mustResolveUDP(cfg.Server.UDPAddr))
	if err != nil {
		log.Fatalf("udp listen failed: %v", err)
	}
	defer udpConn.Close()

	hub := newRelayHub(udpConn, cfg)
	s := &server{hub: hub}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go udpReader(ctx, udpConn, hub)

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", s.wsHandler)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ok"))
	})

	log.Printf(
		"relay started ws=%s udp=%s sample_rate=%d packet_ms=%d opus_bitrate=%d opus_complexity=%d opus_fec=%t opus_dtx=%t opus_application=%s protocol_version=%d jitter_min_packets=%d busy_mode=%t transmit_timeout=%ds",
		cfg.Server.WSAddr,
		cfg.Server.UDPAddr,
		normalizeSampleRate(cfg.Server.SampleRate),
		normalizePacketMs(cfg.Server.PacketMs),
		configuredOpus.Bitrate,
		configuredOpus.Complexity,
		configuredOpus.FEC,
		configuredOpus.DTX,
		configuredOpus.Application,
		protocolVersion,
		cfg.Server.JitterMinPkts,
		cfg.Server.BusyMode,
		cfg.Server.TransmitTimeoutSec,
	)
	if err := http.ListenAndServe(cfg.Server.WSAddr, mux); err != nil {
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
