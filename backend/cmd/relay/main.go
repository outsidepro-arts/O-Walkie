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
	sampleRate      = 8000
	audioChannels   = 1
	maxUDPDatagram  = 1500
	opusMaxFrameLen = 512
	configFilePath  = "config.json"
	defaultPacketMs = 20
)

var (
	packetDur     = time.Duration(defaultPacketMs) * time.Millisecond
	packetSamples = sampleRate * defaultPacketMs / 1000
)

type appConfig struct {
	Server  serverConfig  `json:"server"`
	Modules modulesConfig `json:"modules"`
}

type serverConfig struct {
	WSAddr        string `json:"ws_addr"`
	UDPAddr       string `json:"udp_addr"`
	PacketMs      int    `json:"packet_ms"`
	HangoverMs    int    `json:"hangover_ms"`
	EOFTimeoutMs  int    `json:"eof_timeout_ms"`
	ConcealDecay  float64 `json:"conceal_decay"`
}

type modulesConfig struct {
	Noise      noiseConfig      `json:"noise"`
	Click      clickConfig      `json:"click"`
	Filter     filterConfig     `json:"filter"`
	Compressor compressorConfig `json:"compressor"`
	Distortion distortionConfig `json:"distortion"`
}

type noiseConfig struct {
	MinNoiseDB         float64 `json:"min_noise_db"`
	MaxNoiseDB         float64 `json:"max_noise_db"`
	NoiseGain          float64 `json:"noise_gain"`
	SquelchThresholdDB float64 `json:"squelch_threshold_db"`
	SquelchMinMs       int     `json:"squelch_min_ms"`
	SquelchMaxMs       int     `json:"squelch_max_ms"`
	TailNoiseDB        float64 `json:"tail_noise_db"`
	TailMinMs          int     `json:"tail_min_ms"`
	TailMaxMs          int     `json:"tail_max_ms"`
}

type compressorConfig struct {
	ThresholdDB float64 `json:"threshold_db"`
	Ratio       float64 `json:"ratio"`
	AttackMs    float64 `json:"attack_ms"`
	ReleaseMs   float64 `json:"release_ms"`
	MakeupDB    float64 `json:"makeup_db"`
}

type clickConfig struct {
	ClickDB             float64 `json:"click_db"`
	GlitchIntervalMaxMs int     `json:"glitch_interval_max_ms"`
	GlitchFreqMinHz     float64 `json:"glitch_freq_min_hz"`
	GlitchFreqMaxHz     float64 `json:"glitch_freq_max_hz"`
	GlitchLevelMinDB    float64 `json:"glitch_level_min_db"`
	GlitchLevelMaxDB    float64 `json:"glitch_level_max_db"`
}

type filterConfig struct {
	LowCutHz  float64 `json:"low_cut_hz"`
	HighCutHz float64 `json:"high_cut_hz"`
}

type distortionConfig struct {
	Drive float64 `json:"drive"`
	Mix   float64 `json:"mix"`
}

type wsMessage struct {
	Type            string `json:"type"`
	Channel         string `json:"channel,omitempty"`
	UDPPort         int    `json:"udpPort,omitempty"`
	RepeaterEnabled *bool  `json:"enabled,omitempty"`
}

type wsServerMessage struct {
	Type      string `json:"type"`
	SessionID uint32 `json:"sessionId,omitempty"`
	Channel   string `json:"channel,omitempty"`
	Info      string `json:"info,omitempty"`
	PacketMs  int    `json:"packetMs,omitempty"`
}

type client struct {
	sessionID uint32
	conn      *websocket.Conn

	mu       sync.RWMutex
	channel  string
	udpAddr  *net.UDPAddr
	repeater bool
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

	udpConn *net.UDPConn
	cfg     appConfig
}

type speakerStreamState struct {
	pending      *audioPacket
	lastPacketAt time.Time
	lastSignal   float64
	lastPCM      []int16
	missCount    int
}

func newRelayHub(udpConn *net.UDPConn, cfg appConfig) *relayHub {
	return &relayHub{
		clients:  make(map[uint32]*client),
		channels: make(map[string]*channelMixer),
		udpConn:  udpConn,
		cfg:      cfg,
	}
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
	if len(pkt.opus) == 0 {
		// Empty-opus datagrams are treated as UDP keepalive punches.
		return
	}
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
}

type repeaterSession struct {
	processor  *audioProcessor
	bufferPCM  []int16
	lastPacket time.Time
	collecting bool
	lastSignal float64
}

type audioProcessor struct {
	modules      []audioModule
	noiseLevelDB float64
	lastSignal   float64
}

func newAudioProcessor(mcfg modulesConfig) *audioProcessor {
	return &audioProcessor{
		modules: []audioModule{
			newWhiteNoiseSquelchModule(packetDur, mcfg.Noise),
			newClickModule(packetDur, mcfg.Click),
			newBandPassModule(sampleRate, mcfg.Filter),
			newCompressorModule(sampleRate, mcfg.Compressor),
			newDistortionModule(mcfg.Distortion),
		},
		noiseLevelDB: mcfg.Noise.MinNoiseDB,
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
	enc, err := opus.NewEncoder(sampleRate, audioChannels, opus.AppVoIP)
	if err != nil {
		panic(fmt.Sprintf("create encoder for channel %s: %v", name, err))
	}
	hangoverMs := maxInt(cfg.Server.HangoverMs, normalizePacketMs(cfg.Server.PacketMs)*2)
	eofTimeoutMs := maxInt(cfg.Server.EOFTimeoutMs, hangoverMs+normalizePacketMs(cfg.Server.PacketMs))
	return &channelMixer{
		name:         name,
		hub:          hub,
		input:        make(chan *audioPacket, 256),
		eof:          make(chan uint32, 64),
		participants: make(map[uint32]struct{}),
		decoders:     make(map[uint32]*opus.Decoder),
		encoder:      enc,
		noiseLevelDB: -30.0,
		lastSignal:   255.0,
		modulesCfg:   mcfg,
		modules: []audioModule{
			newWhiteNoiseSquelchModule(packetDur, mcfg.Noise),
			newClickModule(packetDur, mcfg.Click),
			newBandPassModule(sampleRate, mcfg.Filter),
			newCompressorModule(sampleRate, mcfg.Compressor),
			newDistortionModule(mcfg.Distortion),
		},
		repeaterState: make(map[uint32]*repeaterSession),
		hangoverDur:   time.Duration(hangoverMs) * time.Millisecond,
		eofTimeoutDur: time.Duration(eofTimeoutMs) * time.Millisecond,
		concealDecay:  cfg.Server.ConcealDecay,
	}
}

func (m *channelMixer) addParticipant(sessionID uint32) {
	m.participantsMu.Lock()
	defer m.participantsMu.Unlock()
	m.participants[sessionID] = struct{}{}
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

func (m *channelMixer) markEOF(sessionID uint32) {
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
				st = &speakerStreamState{}
				states[pkt.sessionID] = st
			}
			st.pending = pkt
			st.lastPacketAt = time.Now()
			st.lastSignal = float64(pkt.signalStrength)
			eofMarked[pkt.sessionID] = false
		case sid := <-m.eof:
			eofMarked[sid] = true
			if st := states[sid]; st != nil {
				// Explicit EOF should cut ongoing concealment immediately.
				st.pending = nil
			}
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

	for sid, st := range states {
		_, present := participants[sid]
		if !present {
			delete(states, sid)
			delete(eofMarked, sid)
			continue
		}
		if now.Sub(st.lastPacketAt) > m.eofTimeoutDur && st.pending == nil {
			delete(states, sid)
			delete(eofMarked, sid)
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
		pkt := st.pending
		hasPacket := pkt != nil
		speakerActive := false
		speakerSignal := st.lastSignal
		var pcmFrame []int16

		if hasPacket {
			st.pending = nil
		}

		var (
			dec       *opus.Decoder
			createErr error
		)
		m.decodersMu.Lock()
		dec = m.decoders[sessionID]
		if dec == nil {
			dec, createErr = opus.NewDecoder(sampleRate, audioChannels)
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
	exclude := uint32(0)
	if len(activeSpeakers) == 1 {
		exclude = activeSpeakers[0]
	} else if len(activeSpeakers) == 0 && m.lastSingleSpeaker != 0 {
		// During post-TX tail frames, avoid feeding a sender its own ending burst.
		packetMs := int(packetDur / time.Millisecond)
		tailGrace := time.Duration(maxInt(m.modulesCfg.Noise.TailMaxMs, packetMs*2)) * time.Millisecond
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
	st.collecting = true
	st.lastPacket = now
	st.lastSignal = signalByte
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
		if now.Sub(st.lastPacket) < (2 * packetDur) {
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
		go m.playbackRepeaterAfterDelay(sessionID, bufferCopy, 500*time.Millisecond)
	}
}

func (m *channelMixer) playbackRepeaterAfterDelay(sessionID uint32, pcm []int16, delayDur time.Duration) {
	if len(pcm) == 0 {
		return
	}
	time.Sleep(delayDur)

	enc, err := opus.NewEncoder(sampleRate, audioChannels, opus.AppVoIP)
	if err != nil {
		log.Printf("repeater encoder create failed: %v", err)
		return
	}

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
		burstSamples:   sampleRate / 80, // ~12.5 ms burst at 8 kHz.
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
		t := float64(i) / float64(sampleRate)
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

func (m *whiteNoiseSquelchModule) Process(ctx *audioProcessContext) {
	if ctx.ActiveSpeakers <= 0 {
		// End-of-transmission tail burst: jump to 0 dB and emit white hiss briefly.
		if m.lastActive && m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(m.cfg.TailMinMs, m.cfg.TailMaxMs)
		}
		m.lastActive = false
		if m.squelchBurstRemain <= 0 {
			m.squelchLatched = false
			ctx.EmitFrame = false
			return
		}
		noiseAmplitude := m.cfg.NoiseGain * dbToLinear(m.cfg.TailNoiseDB)
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

	noiseDB := mapSignalByteToNoiseDB(ctx.AvgSignalByte, m.cfg)
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

func isSupportedPacketMs(ms int) bool {
	switch ms {
	case 10, 20, 40, 60:
		return true
	default:
		return false
	}
}

func applyPacketTiming(packetMs int) {
	norm := normalizePacketMs(packetMs)
	packetDur = time.Duration(norm) * time.Millisecond
	packetSamples = sampleRate * norm / 1000
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
		z := b.processSample(x)
		frame[i] = hardClip(int32(z), 32767)
	}
}

func (b *bandPass) processSample(x float64) float64 {
	y := b.hp.process(x)
	return b.lp.process(y)
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
			WSAddr:       ":5500",
			UDPAddr:      ":5505",
			PacketMs:     defaultPacketMs,
			HangoverMs:   180,
			EOFTimeoutMs: 420,
			ConcealDecay: 0.90,
		},
		Modules: modulesConfig{
			Noise: noiseConfig{
				MinNoiseDB:         -20.0,
				MaxNoiseDB:         -3.0,
				NoiseGain:          1200.0,
				SquelchThresholdDB: -3.0,
				SquelchMinMs:       50,
				SquelchMaxMs:       150,
				TailNoiseDB:        0.0,
				TailMinMs:          15,
				TailMaxMs:          60,
			},
			Click: clickConfig{
				ClickDB:             -8.0,
				GlitchIntervalMaxMs: 0,
				GlitchFreqMinHz:     120.0,
				GlitchFreqMaxHz:     360.0,
				GlitchLevelMinDB:    -14.0,
				GlitchLevelMaxDB:    -6.0,
			},
			Filter: filterConfig{
				LowCutHz:  300.0,
				HighCutHz: 3000.0,
			},
			Compressor: compressorConfig{
				ThresholdDB: -18.0,
				Ratio:       3.2,
				AttackMs:    5.0,
				ReleaseMs:   80.0,
				MakeupDB:    4.0,
			},
			Distortion: distortionConfig{
				Drive: 1.35,
				Mix:   0.28,
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
	if err := validateConfig(cfg); err != nil {
		return appConfig{}, err
	}
	return cfg, nil
}

func validateConfig(cfg appConfig) error {
	if strings.TrimSpace(cfg.Server.WSAddr) == "" || strings.TrimSpace(cfg.Server.UDPAddr) == "" {
		return errors.New("server.ws_addr and server.udp_addr must be set")
	}
	if !isSupportedPacketMs(cfg.Server.PacketMs) {
		return errors.New("server.packet_ms must be one of: 10, 20, 40, 60")
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
	if cfg.Modules.Compressor.Ratio <= 0 {
		return errors.New("modules.compressor.ratio must be > 0")
	}
	if cfg.Modules.Noise.NoiseGain <= 0 {
		return errors.New("modules.noise.noise_gain must be > 0")
	}
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
	if cfg.Modules.Distortion.Mix < 0 || cfg.Modules.Distortion.Mix > 1 {
		return errors.New("modules.distortion.mix must be in [0..1]")
	}
	if math.IsNaN(cfg.Modules.Filter.LowCutHz) || math.IsInf(cfg.Modules.Filter.LowCutHz, 0) ||
		math.IsNaN(cfg.Modules.Filter.HighCutHz) || math.IsInf(cfg.Modules.Filter.HighCutHz, 0) {
		return errors.New("modules.filter cutoff range must be finite")
	}
	nyquist := float64(sampleRate) / 2.0
	if cfg.Modules.Filter.LowCutHz <= 0 || cfg.Modules.Filter.HighCutHz <= cfg.Modules.Filter.LowCutHz || cfg.Modules.Filter.HighCutHz >= nyquist {
		return errors.New("modules.filter cutoff range is invalid")
	}
	if cfg.Modules.Noise.SquelchMinMs <= 0 || cfg.Modules.Noise.SquelchMaxMs < cfg.Modules.Noise.SquelchMinMs {
		return errors.New("modules.noise squelch range is invalid")
	}
	if cfg.Modules.Noise.TailMinMs <= 0 || cfg.Modules.Noise.TailMaxMs < cfg.Modules.Noise.TailMinMs {
		return errors.New("modules.noise tail range is invalid")
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
		channel:   "global",
	}
	s.hub.addClient(c)
	s.hub.switchChannel(c, "global")
	defer s.hub.removeClient(c.sessionID)

	_ = conn.WriteJSON(wsServerMessage{
		Type:      "welcome",
		SessionID: c.sessionID,
		Channel:   "global",
		PacketMs:  normalizePacketMs(s.hub.cfg.Server.PacketMs),
	})

	for {
		var msg wsMessage
		if err := conn.ReadJSON(&msg); err != nil {
			if isExpectedDisconnectError(err) {
				log.Printf("ws disconnected for %d: %v", c.sessionID, err)
			} else {
				log.Printf("ws read failed for %d: %v", c.sessionID, err)
			}
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
		case "repeater_mode":
			enabled := msg.RepeaterEnabled != nil && *msg.RepeaterEnabled
			c.setRepeaterMode(enabled)
			if !enabled {
				s.hub.getOrCreateChannel(c.getChannel()).clearRepeaterState(c.sessionID)
			}
			_ = conn.WriteJSON(wsServerMessage{Type: "repeater_mode", Info: strconv.FormatBool(enabled)})
		case "tx_eof":
			s.hub.markTxEOF(c.sessionID)
		default:
			_ = conn.WriteJSON(wsServerMessage{Type: "error", Info: "unknown message type"})
		}
	}
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
	if len(os.Args) > 1 {
		log.Fatalf("startup flags are disabled: configure server via %s only", configFilePath)
	}

	cfg, err := loadConfig(configFilePath)
	if err != nil {
		log.Fatalf("config error: %v", err)
	}
	applyPacketTiming(cfg.Server.PacketMs)

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

	log.Printf("relay started ws=%s udp=%s packet_ms=%d", cfg.Server.WSAddr, cfg.Server.UDPAddr, normalizePacketMs(cfg.Server.PacketMs))
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
