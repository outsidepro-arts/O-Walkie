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
	"runtime/debug"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
	"github.com/hraban/opus"
)

// buildVersion is overridden by release builds: go build -ldflags "-X main.buildVersion=1.2.3"
var buildVersion = "dev"

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

	// Mixer emergency-restart observability (panic recovery path).
	mixerRestartAlertWindow    = time.Minute
	mixerRestartAlertThreshold = 5
	mixerRestartAlertThrottle  = 30 * time.Second
	// Unicast catch-up: last mixed frame may be older than one packet when a client
	// registers UDP (punch / udp_hello); too short a window skips resend during live TX.
	mixerCatchupFrameMaxAge = 500 * time.Millisecond
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
	// Port is used for both WebSocket HTTP listener and UDP listener (same port number).
	Port int `json:"port"`
	// Legacy keys (optional): if port is 0, port is inferred from the first non-empty address.
	LegacyWsAddr       string     `json:"ws_addr,omitempty"`
	LegacyUdpAddr      string     `json:"udp_addr,omitempty"`
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
	Generators generatorsModulesConfig `json:"generators"`
	DSP        dspModulesConfig        `json:"dsp"`
	// Legacy flat layout (auto-migrated into generators/dsp during load).
	Noise      *legacyNoiseConfig `json:"noise,omitempty"`
	Click      *legacyClickConfig `json:"click,omitempty"`
	Filter     *filterConfig      `json:"filter,omitempty"`
	Compressor *compressorConfig  `json:"compressor,omitempty"`
	Distortion *distortionConfig  `json:"distortion,omitempty"`
}

type generatorsModulesConfig struct {
	// Reserved for non-DSP frame initiators.
}

type dspModulesConfig struct {
	Clicks     *clicksConfig     `json:"clicks,omitempty"`
	Pops       *popsConfig       `json:"pops,omitempty"`
	Noise      *noiseDSPConfig   `json:"noise,omitempty"`
	Squelch    *squelchDSPConfig `json:"squelch,omitempty"`
	Filter     *filterConfig     `json:"filter,omitempty"`
	Compressor *compressorConfig `json:"compressor,omitempty"`
	Distortion *distortionConfig `json:"distortion,omitempty"`
}

type noiseDSPConfig struct {
	Enabled         bool    `json:"enabled"`
	SignalDependent bool    `json:"signal_dependent"`
	MinNoiseDB      float64 `json:"min_noise_db"`
	MaxNoiseDB      float64 `json:"max_noise_db"`
	NoiseGain       float64 `json:"noise_gain"`
	// NoiseDistribution: "gaussian" (default) or "uniform" (legacy per-sample [-1,1]).
	NoiseDistribution string `json:"noise_distribution,omitempty"`
	// ThermalLowpassHz: moving-average lowpass (~SR/window); 0 disables.
	ThermalLowpassHz float64 `json:"thermal_lowpass_hz,omitempty"`
}

type squelchDSPConfig struct {
	Enabled bool `json:"enabled"`
	// Signal threshold percentage (0..100) below which squelch burst is emitted then gated.
	ThresholdPercent float64 `json:"threshold_percent"`
	SquelchMinMs     int     `json:"squelch_min_ms"`
	SquelchMaxMs     int     `json:"squelch_max_ms"`
	NoiseGain        float64 `json:"noise_gain"`
	TailNoiseDB      float64 `json:"tail_noise_db"`
	TailMinMs        int     `json:"tail_min_ms"`
	TailMaxMs        int     `json:"tail_max_ms"`
	// SynthSilenceTailPackets: zero-PCM frames after each squelch-generated burst tail.
	// 0 = default jitter_min_packets+2 (min 4).
	SynthSilenceTailPackets int `json:"synth_silence_tail_packets,omitempty"`
}

type compressorConfig struct {
	Enabled     bool    `json:"enabled"`
	ThresholdDB float64 `json:"threshold_db"`
	Ratio       float64 `json:"ratio"`
	AttackMs    float64 `json:"attack_ms"`
	ReleaseMs   float64 `json:"release_ms"`
	MakeupDB    float64 `json:"makeup_db"`
}

// clickPopsConfig: sinusoidal PTT / in-TX pops (legacy flat click_* merged here when pops omitted).
type clickPopsConfig struct {
	ClickDB             float64 `json:"click_db"`
	ClickToneHz         float64 `json:"click_tone_hz,omitempty"`
	GlitchIntervalMaxMs int     `json:"glitch_interval_max_ms"`
	GlitchFreqMinHz     float64 `json:"glitch_freq_min_hz"`
	GlitchFreqMaxHz     float64 `json:"glitch_freq_max_hz"`
	GlitchLevelMinDB    float64 `json:"glitch_level_min_db"`
	GlitchLevelMaxDB    float64 `json:"glitch_level_max_db"`
}

type clickImpulsesConfig struct {
	Enabled            bool    `json:"enabled"`
	ProbAtWeakSignal   float64 `json:"prob_at_weak_signal"`
	ProbAtStrongSignal float64 `json:"prob_at_strong_signal"`
	GainDB             float64 `json:"gain_db"`
}

type clicksConfig struct {
	Enabled            bool                 `json:"enabled"`
	Impulses           *clickImpulsesConfig `json:"impulses,omitempty"`
	MultiClientRapidMs int                  `json:"multi_client_rapid_ms,omitempty"`
}

type popsConfig struct {
	Enabled bool             `json:"enabled"`
	Pops    *clickPopsConfig `json:"pops,omitempty"`
	// Legacy flat fields (used when pops is nil).
	ClickDB             float64 `json:"click_db,omitempty"`
	ClickToneHz         float64 `json:"click_tone_hz,omitempty"`
	GlitchIntervalMaxMs int     `json:"glitch_interval_max_ms,omitempty"`
	GlitchFreqMinHz     float64 `json:"glitch_freq_min_hz,omitempty"`
	GlitchFreqMaxHz     float64 `json:"glitch_freq_max_hz,omitempty"`
	GlitchLevelMinDB    float64 `json:"glitch_level_min_db,omitempty"`
	GlitchLevelMaxDB    float64 `json:"glitch_level_max_db,omitempty"`
}

type legacyNoiseConfig struct {
	Enabled                 bool    `json:"enabled"`
	SignalDependent         bool    `json:"signal_dependent"`
	MinNoiseDB              float64 `json:"min_noise_db"`
	MaxNoiseDB              float64 `json:"max_noise_db"`
	NoiseGain               float64 `json:"noise_gain"`
	SquelchThresholdDB      float64 `json:"squelch_threshold_db"`
	SquelchMinMs            int     `json:"squelch_min_ms"`
	SquelchMaxMs            int     `json:"squelch_max_ms"`
	TailNoiseDB             float64 `json:"tail_noise_db"`
	TailMinMs               int     `json:"tail_min_ms"`
	TailMaxMs               int     `json:"tail_max_ms"`
	NoiseDistribution       string  `json:"noise_distribution,omitempty"`
	ThermalLowpassHz        float64 `json:"thermal_lowpass_hz,omitempty"`
	SynthSilenceTailPackets int     `json:"synth_silence_tail_packets,omitempty"`
}
type legacyClickConfig struct {
	Enabled             bool                 `json:"enabled"`
	Pops                *clickPopsConfig     `json:"pops,omitempty"`
	ClickDB             float64              `json:"click_db,omitempty"`
	ClickToneHz         float64              `json:"click_tone_hz,omitempty"`
	GlitchIntervalMaxMs int                  `json:"glitch_interval_max_ms,omitempty"`
	GlitchFreqMinHz     float64              `json:"glitch_freq_min_hz,omitempty"`
	GlitchFreqMaxHz     float64              `json:"glitch_freq_max_hz,omitempty"`
	GlitchLevelMinDB    float64              `json:"glitch_level_min_db,omitempty"`
	GlitchLevelMaxDB    float64              `json:"glitch_level_max_db,omitempty"`
	Impulses            *clickImpulsesConfig `json:"impulses,omitempty"`
}

type filterConfig struct {
	Enabled   bool    `json:"enabled"`
	LowCutHz  float64 `json:"low_cut_hz"`
	HighCutHz float64 `json:"high_cut_hz"`
}

type distortionConfig struct {
	Enabled               bool    `json:"enabled"`
	Drive                 float64 `json:"drive"`
	Mix                   float64 `json:"mix"`
	MultiClientDriveBoost float64 `json:"multi_client_drive_boost,omitempty"`
	MultiClientMixBoost   float64 `json:"multi_client_mix_boost,omitempty"`
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
	txEOFPending bool

	// decoderFlushPending/eofAt: after explicit TX EOF, drop per-session Opus decoder once
	// the jitter queue has drained, so the next transmission does not inherit decoder tail.
	decoderFlushPending bool
	eofAt               time.Time
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
			// Punch establishes/refreshes c.srcAddr (NAT). Resend last mixed frame so
			// joiners mid-transmission hear immediately; udp_hello alone can race or use wrong path.
			if ch := normalizeChannelName(c.getChannel()); ch != "" {
				h.getOrCreateChannel(ch).sendLatestFrameToParticipant(pkt.sessionID)
			}
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
	generators   []audioModule
	dspMods      []audioModule
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

	mixerEmergencyRestarts atomic.Uint64
	mixerRestartMu         sync.Mutex
	mixerRestartWindow     []time.Time
	mixerRestartLastAlert  time.Time

	lastFrameMu              sync.RWMutex
	lastFramePayload         []byte
	lastFrameAt              time.Time
	lastFrameExcludedSession uint32 // broadcastMixed exclude for lastFramePayload; 0 = not excluded

	// Zero-PCM frames after squelch-generated burst tail, before idle.
	synthSilenceRemain int
	lastTxActive       bool
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
	generators   []audioModule
	dspMods      []audioModule
	noiseLevelDB float64
	lastSignal   float64
	lastTxActive bool
}

func newAudioProcessor(mcfg modulesConfig) *audioProcessor {
	noiseFloor := -30.0
	if mcfg.DSP.Noise != nil {
		noiseFloor = mcfg.DSP.Noise.MinNoiseDB
	}
	gen, dsp := buildChannelModuleSets(mcfg)
	return &audioProcessor{
		generators:   gen,
		dspMods:      dsp,
		noiseLevelDB: noiseFloor,
		lastSignal:   255.0,
	}
}

func (p *audioProcessor) process(input []int16, signalByte float64, active bool) ([]int16, bool) {
	mixed := make([]float64, packetSamples)
	txStart := active && !p.lastTxActive
	txEOF := !active && p.lastTxActive
	p.lastTxActive = active
	var signalForModules *float64
	if active {
		p.lastSignal = signalByte
		signalForModules = &p.lastSignal
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
		Control: audioModuleControl{
			TxActive:       active,
			TxStart:        txStart,
			TxEOF:          txEOF,
			MultiClientMix: false,
			SignalByte:     signalForModules,
		},
		EmitFrame: active,
	}
	if processModuleChain(p.generators, ctx) {
		p.noiseLevelDB = ctx.NoiseLevelDB
		return nil, false
	}
	if processModuleChain(p.dspMods, ctx) {
		p.noiseLevelDB = ctx.NoiseLevelDB
		return nil, false
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
	gen, dsp := buildChannelModuleSets(mcfg)
	noiseFloor := -30.0
	if mcfg.DSP.Noise != nil {
		noiseFloor = mcfg.DSP.Noise.MinNoiseDB
	}
	return &channelMixer{
		name:               name,
		hub:                hub,
		input:              make(chan *audioPacket, 256),
		eof:                make(chan uint32, 64),
		participants:       make(map[uint32]struct{}),
		decoders:           make(map[uint32]*opus.Decoder),
		encoder:            enc,
		noiseLevelDB:       noiseFloor,
		lastSignal:         255.0,
		modulesCfg:         mcfg,
		generators:         gen,
		dspMods:            dsp,
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

func (m *channelMixer) dropDecoder(sessionID uint32) {
	m.decodersMu.Lock()
	delete(m.decoders, sessionID)
	m.decodersMu.Unlock()
}

// maybeFlushDecoderAfterEOF drops the per-session Opus decoder after explicit EOF once
// the jitter buffer is empty and a short settle window has passed, so the next PTT
// does not inherit PLC/tail state from the previous transmission. Runs before pulling
// from jitter so we never clear the queue mid-drain.
func (m *channelMixer) maybeFlushDecoderAfterEOF(sessionID uint32, st *speakerStreamState, now time.Time) {
	if st == nil || !st.decoderFlushPending {
		return
	}
	if st.jitter.pendingCount() != 0 {
		return
	}
	minQuiet := 5 * packetDur
	if !st.eofAt.IsZero() && now.Sub(st.eofAt) < minQuiet {
		return
	}
	m.dropDecoder(sessionID)
	st.lastPCM = nil
	st.missCount = 0
	st.decoderFlushPending = false
	st.eofAt = time.Time{}
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

func (m *channelMixer) rememberLatestFrame(payload []byte, broadcastExcludeSession uint32) {
	if len(payload) == 0 {
		return
	}
	m.lastFrameMu.Lock()
	m.lastFramePayload = append(m.lastFramePayload[:0], payload...)
	m.lastFrameAt = time.Now()
	m.lastFrameExcludedSession = broadcastExcludeSession
	m.lastFrameMu.Unlock()
}

// latestFrameSnapshot returns the last mixed UDP payload and the broadcast exclude session
// used when it was produced (single lock — payload and metadata stay consistent).
func (m *channelMixer) latestFrameSnapshot(maxAge time.Duration) (payload []byte, excludedSession uint32) {
	m.lastFrameMu.RLock()
	defer m.lastFrameMu.RUnlock()
	if len(m.lastFramePayload) == 0 {
		return nil, 0
	}
	if maxAge > 0 && time.Since(m.lastFrameAt) > maxAge {
		return nil, 0
	}
	out := make([]byte, len(m.lastFramePayload))
	copy(out, m.lastFramePayload)
	return out, m.lastFrameExcludedSession
}

func (m *channelMixer) sendLatestFrameToParticipant(sessionID uint32) {
	payload, excluded := m.latestFrameSnapshot(mixerCatchupFrameMaxAge)
	if len(payload) == 0 {
		return
	}
	// Solo TX: broadcastMixed skips the active speaker to prevent self-echo.
	// Keepalive catch-up must not unicast that frame back — clients treat inbound UDP
	// during local TX as parallel-transmit (vibration / desktop buzz).
	if excluded != 0 && excluded == sessionID {
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
	restartDelay := 150 * time.Millisecond
	for {
		if !m.runLoop() {
			return
		}
		total := m.mixerEmergencyRestarts.Add(1)
		m.recordMixerRestartAlert(total)
		m.resetAfterLoopFailure()
		log.Printf("channel %s mixer loop emergency restart #%d in %s", m.name, total, restartDelay)
		time.Sleep(restartDelay)
		if restartDelay < 2*time.Second {
			restartDelay *= 2
		}
	}
}

func (m *channelMixer) recordMixerRestartAlert(lifetimeTotal uint64) {
	now := time.Now()
	cutoff := now.Add(-mixerRestartAlertWindow)

	m.mixerRestartMu.Lock()
	defer m.mixerRestartMu.Unlock()

	idx := 0
	for idx < len(m.mixerRestartWindow) && m.mixerRestartWindow[idx].Before(cutoff) {
		idx++
	}
	m.mixerRestartWindow = append(m.mixerRestartWindow[idx:], now)

	inWindow := len(m.mixerRestartWindow)
	if inWindow < mixerRestartAlertThreshold {
		return
	}
	if now.Sub(m.mixerRestartLastAlert) < mixerRestartAlertThrottle {
		return
	}
	m.mixerRestartLastAlert = now
	log.Printf("ALERT channel %q mixer: %d emergency restarts in last %v (lifetime total %d)",
		m.name, inWindow, mixerRestartAlertWindow, lifetimeTotal)
}

func (m *channelMixer) runLoop() (panicked bool) {
	defer func() {
		if r := recover(); r != nil {
			panicked = true
			log.Printf("channel %s mixer panic recovered: %v\n%s", m.name, r, string(debug.Stack()))
		}
	}()

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
				m.dropDecoder(pkt.sessionID)
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
				st.decoderFlushPending = true
				st.eofAt = time.Now()
				st.txEOFPending = true
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

func (m *channelMixer) resetAfterLoopFailure() {
	enc, err := opus.NewEncoder(configuredSampleRate, audioChannels, resolveOpusApplication(configuredOpus.Application))
	if err != nil {
		log.Printf("channel %s encoder re-init failed after panic: %v", m.name, err)
		return
	}
	applyOpusEncoderConfig(enc)
	m.encoder = enc

	m.decodersMu.Lock()
	m.decoders = make(map[uint32]*opus.Decoder)
	m.decodersMu.Unlock()

	m.repeaterMu.Lock()
	m.repeaterState = make(map[uint32]*repeaterSession)
	m.repeaterMu.Unlock()

	m.lastFrameMu.Lock()
	m.lastFramePayload = nil
	m.lastFrameAt = time.Time{}
	m.lastFrameExcludedSession = 0
	m.lastFrameMu.Unlock()

	m.synthSilenceRemain = 0
	m.lastTxActive = false
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
			m.dropDecoder(sid)
			continue
		}
		if now.Sub(st.lastPacketAt) > m.eofTimeoutDur && st.pending == nil {
			delete(states, sid)
			delete(eofMarked, sid)
			delete(m.txStartedAt, sid)
			delete(m.txForceStopped, sid)
			delete(m.txLastStopNoticeAt, sid)
			m.dropDecoder(sid)
		}
	}
}

// synthesizedBurstSilenceTicks returns how many mixer ticks of silence to send after squelch-generated audio.
func (m *channelMixer) synthesizedBurstSilenceTicks() int {
	n := int(m.jitterMinPackets) + 2
	if n < 4 {
		n = 4
	}
	if m.modulesCfg.DSP.Squelch != nil && m.modulesCfg.DSP.Squelch.Enabled && m.modulesCfg.DSP.Squelch.SynthSilenceTailPackets > 0 {
		n = m.modulesCfg.DSP.Squelch.SynthSilenceTailPackets
	}
	return n
}

func (m *channelMixer) emitEncodedSilenceFrame(now time.Time, activeSpeakers []uint32) {
	outPCM := make([]int16, packetSamples)
	opusBuf := make([]byte, opusMaxFrameLen)
	n, err := m.encoder.Encode(outPCM, opusBuf)
	if err != nil {
		log.Printf("opus encode failed in channel %s (silence tail): %v", m.name, err)
		return
	}
	if n <= 0 {
		return
	}
	m.seq++
	sig := m.lastSignal
	if sig < 0 {
		sig = 0
	}
	if sig > 255 {
		sig = 255
	}
	sigByte := uint8(sig)
	header := make([]byte, 9)
	binary.BigEndian.PutUint32(header[0:4], 0)
	binary.BigEndian.PutUint32(header[4:8], m.seq)
	header[8] = sigByte
	payload := append(header, opusBuf[:n]...)
	exclude := uint32(0)
	if len(activeSpeakers) == 1 {
		exclude = activeSpeakers[0]
	} else if len(activeSpeakers) == 0 && m.lastSingleSpeaker != 0 {
		packetMs := int(packetDur / time.Millisecond)
		tailMaxMs := packetMs * 2
		if m.modulesCfg.DSP.Squelch != nil && m.modulesCfg.DSP.Squelch.Enabled {
			tailMaxMs = maxInt(m.modulesCfg.DSP.Squelch.TailMaxMs, packetMs*2)
		}
		tailGrace := time.Duration(tailMaxMs) * time.Millisecond
		if now.Sub(m.lastSingleSpeakerAt) <= tailGrace {
			exclude = m.lastSingleSpeaker
		}
	}
	m.rememberLatestFrame(payload, exclude)
	m.hub.broadcastMixed(m.name, exclude, opusBuf[:n], m.seq, sigByte)
}

func (m *channelMixer) mixAndBroadcast(states map[uint32]*speakerStreamState, eofMarked map[uint32]bool) {
	mixed := make([]float64, packetSamples)
	activeSpeakers := make([]uint32, 0, len(states))
	var signalSum float64
	now := time.Now()
	txEOF := false

	for sessionID, st := range states {
		if st == nil {
			continue
		}
		client := m.hub.getClient(sessionID)
		isRepeater := client != nil && client.isRepeaterMode()
		if st.txEOFPending {
			if !isRepeater {
				txEOF = true
			}
			st.txEOFPending = false
		}
		m.maybeFlushDecoderAfterEOF(sessionID, st, now)
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
		m.decodersMu.Unlock()
		if dec == nil {
			dec, createErr = opus.NewDecoder(configuredSampleRate, audioChannels)
			if createErr == nil {
				m.decodersMu.Lock()
				if existing := m.decoders[sessionID]; existing != nil {
					dec = existing
				} else {
					m.decoders[sessionID] = dec
				}
				m.decodersMu.Unlock()
			}
		}
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
		if isRepeater {
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
	txActive := len(activeSpeakers) > 0
	multiClientMix := len(activeSpeakers) > 1
	txStart := txActive && !m.lastTxActive
	if !txEOF && !txActive && m.lastTxActive {
		txEOF = true
	}
	m.lastTxActive = txActive
	var signalForModules *float64
	avgSignal := signalSum / float64(maxInt(len(activeSpeakers), 1))
	if txActive {
		signalForModules = &avgSignal
	}
	if len(activeSpeakers) == 1 {
		m.lastSingleSpeaker = activeSpeakers[0]
		m.lastSingleSpeakerAt = now
	} else if len(activeSpeakers) > 1 {
		m.lastSingleSpeaker = 0
	}

	if m.synthSilenceRemain > 0 {
		m.emitEncodedSilenceFrame(now, activeSpeakers)
		m.synthSilenceRemain--
		return
	}

	ctx := &audioProcessContext{
		Mixed:          mixed,
		AvgSignalByte:  avgSignal,
		ActiveSpeakers: len(activeSpeakers),
		NoiseLevelDB:   m.noiseLevelDB,
		Control: audioModuleControl{
			TxActive:       txActive,
			TxStart:        txStart,
			TxEOF:          txEOF,
			MultiClientMix: multiClientMix,
			SignalByte:     signalForModules,
		},
		EmitFrame: txActive,
	}
	if processModuleChain(m.generators, ctx) {
		m.noiseLevelDB = ctx.NoiseLevelDB
		return
	}
	if processModuleChain(m.dspMods, ctx) {
		m.noiseLevelDB = ctx.NoiseLevelDB
		return
	}
	m.noiseLevelDB = ctx.NoiseLevelDB
	if ctx.QueueSynthSilenceTail {
		m.synthSilenceRemain += m.synthesizedBurstSilenceTicks()
	}
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
	exclude := uint32(0)
	if len(activeSpeakers) == 1 {
		exclude = activeSpeakers[0]
	} else if len(activeSpeakers) == 0 && m.lastSingleSpeaker != 0 {
		// During post-TX tail frames, avoid feeding a sender its own ending burst.
		packetMs := int(packetDur / time.Millisecond)
		tailMaxMs := packetMs * 2
		if m.modulesCfg.DSP.Squelch != nil && m.modulesCfg.DSP.Squelch.Enabled {
			tailMaxMs = maxInt(m.modulesCfg.DSP.Squelch.TailMaxMs, packetMs*2)
		}
		tailGrace := time.Duration(tailMaxMs) * time.Millisecond
		if now.Sub(m.lastSingleSpeakerAt) <= tailGrace {
			exclude = m.lastSingleSpeaker
		}
	}
	m.rememberLatestFrame(payload, exclude)
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
		nPad := m.synthesizedBurstSilenceTicks() * packetSamples
		if nPad > 0 {
			bufferCopy = append(bufferCopy, make([]int16, nPad)...)
		}
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
			Port:       5500,
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
			Generators: generatorsModulesConfig{},
			DSP: dspModulesConfig{
				Clicks: &clicksConfig{
					Enabled: true,
					Impulses: &clickImpulsesConfig{
						Enabled:            false,
						ProbAtWeakSignal:   0.10,
						ProbAtStrongSignal: 0.01,
						GainDB:             -8.0,
					},
					MultiClientRapidMs: 0, // 0 = every frame while multi-client TX is active.
				},
				Pops: &popsConfig{
					Enabled: true,
					Pops: &clickPopsConfig{
						ClickDB:             -8.0,
						ClickToneHz:         200.0,
						GlitchIntervalMaxMs: 0,
						GlitchFreqMinHz:     120.0,
						GlitchFreqMaxHz:     360.0,
						GlitchLevelMinDB:    -14.0,
						GlitchLevelMaxDB:    -6.0,
					},
				},
				Noise: &noiseDSPConfig{
					Enabled:           true,
					SignalDependent:   true,
					MinNoiseDB:        -20.0,
					MaxNoiseDB:        -3.0,
					NoiseGain:         1200.0,
					NoiseDistribution: "gaussian",
					ThermalLowpassHz:  8000,
				},
				Squelch: &squelchDSPConfig{
					Enabled:                 true,
					ThresholdPercent:        5.0,
					SquelchMinMs:            50,
					SquelchMaxMs:            150,
					NoiseGain:               1200.0,
					TailNoiseDB:             0.0,
					TailMinMs:               15,
					TailMaxMs:               60,
					SynthSilenceTailPackets: 0,
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
	normalizeServerListenPort(&cfg.Server)
	normalizeModulesConfig(&cfg.Modules)
	if err := validateConfig(cfg); err != nil {
		return appConfig{}, err
	}
	cfg.Server.SampleRate = normalizeSampleRate(cfg.Server.SampleRate)
	return cfg, nil
}

func parsePortFromListenAddr(s string) int {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0
	}
	if strings.HasPrefix(s, ":") {
		p, err := strconv.Atoi(s[1:])
		if err != nil || p <= 0 {
			return 0
		}
		return p
	}
	i := strings.LastIndex(s, ":")
	if i < 0 || i >= len(s)-1 {
		return 0
	}
	p, err := strconv.Atoi(s[i+1:])
	if err != nil || p <= 0 {
		return 0
	}
	return p
}

func normalizeServerListenPort(s *serverConfig) {
	if s.Port == 0 {
		if p := parsePortFromListenAddr(s.LegacyWsAddr); p > 0 {
			s.Port = p
		} else if p := parsePortFromListenAddr(s.LegacyUdpAddr); p > 0 {
			s.Port = p
		}
	}
	if s.Port == 0 {
		s.Port = 5500
	}
	s.LegacyWsAddr = ""
	s.LegacyUdpAddr = ""
}

func normalizeModulesConfig(m *modulesConfig) {
	if m == nil {
		return
	}
	// Migrate legacy flat layout into the new generators/dsp layout when missing.
	if m.Noise != nil {
		if m.DSP.Noise == nil {
			m.DSP.Noise = &noiseDSPConfig{
				Enabled:           m.Noise.Enabled,
				SignalDependent:   m.Noise.SignalDependent,
				MinNoiseDB:        m.Noise.MinNoiseDB,
				MaxNoiseDB:        m.Noise.MaxNoiseDB,
				NoiseGain:         m.Noise.NoiseGain,
				NoiseDistribution: m.Noise.NoiseDistribution,
				ThermalLowpassHz:  m.Noise.ThermalLowpassHz,
			}
		}
		if m.DSP.Squelch == nil {
			thresholdPct := 5.0
			if m.Noise.SignalDependent && m.Noise.MaxNoiseDB > m.Noise.MinNoiseDB {
				thresholdPct = 10.0 + 90.0*((m.Noise.MaxNoiseDB-m.Noise.SquelchThresholdDB)/(m.Noise.MaxNoiseDB-m.Noise.MinNoiseDB))
				if thresholdPct < 0 {
					thresholdPct = 0
				}
				if thresholdPct > 100 {
					thresholdPct = 100
				}
			}
			m.DSP.Squelch = &squelchDSPConfig{
				Enabled:                 m.Noise.Enabled,
				ThresholdPercent:        thresholdPct,
				SquelchMinMs:            m.Noise.SquelchMinMs,
				SquelchMaxMs:            m.Noise.SquelchMaxMs,
				NoiseGain:               m.Noise.NoiseGain,
				TailNoiseDB:             m.Noise.TailNoiseDB,
				TailMinMs:               m.Noise.TailMinMs,
				TailMaxMs:               m.Noise.TailMaxMs,
				SynthSilenceTailPackets: m.Noise.SynthSilenceTailPackets,
			}
		}
	}
	if m.Click != nil {
		if m.DSP.Pops == nil {
			m.DSP.Pops = &popsConfig{
				Enabled:             m.Click.Enabled,
				Pops:                m.Click.Pops,
				ClickDB:             m.Click.ClickDB,
				ClickToneHz:         m.Click.ClickToneHz,
				GlitchIntervalMaxMs: m.Click.GlitchIntervalMaxMs,
				GlitchFreqMinHz:     m.Click.GlitchFreqMinHz,
				GlitchFreqMaxHz:     m.Click.GlitchFreqMaxHz,
				GlitchLevelMinDB:    m.Click.GlitchLevelMinDB,
				GlitchLevelMaxDB:    m.Click.GlitchLevelMaxDB,
			}
		}
		if m.DSP.Clicks == nil {
			m.DSP.Clicks = &clicksConfig{
				Enabled:  m.Click.Enabled,
				Impulses: m.Click.Impulses,
			}
		}
	}
	if m.Filter != nil && m.DSP.Filter == nil {
		m.DSP.Filter = m.Filter
	}
	if m.Compressor != nil && m.DSP.Compressor == nil {
		m.DSP.Compressor = m.Compressor
	}
	if m.Distortion != nil && m.DSP.Distortion == nil {
		m.DSP.Distortion = m.Distortion
	}
}

func validateConfig(cfg appConfig) error {
	if cfg.Server.Port < 1 || cfg.Server.Port > 65535 {
		return errors.New("server.port must be between 1 and 65535")
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
	if cfg.Modules.DSP.Compressor != nil && cfg.Modules.DSP.Compressor.Enabled {
		if cfg.Modules.DSP.Compressor.Ratio <= 0 {
			return errors.New("modules.compressor.ratio must be > 0")
		}
	}
	if cfg.Modules.DSP.Noise != nil && cfg.Modules.DSP.Noise.Enabled {
		if cfg.Modules.DSP.Noise.NoiseGain <= 0 {
			return errors.New("modules.dsp.noise.noise_gain must be > 0")
		}
		if cfg.Modules.DSP.Noise.NoiseDistribution == "" {
			cfg.Modules.DSP.Noise.NoiseDistribution = "gaussian"
		}
		if cfg.Modules.DSP.Noise.NoiseDistribution != "gaussian" && cfg.Modules.DSP.Noise.NoiseDistribution != "uniform" {
			return errors.New("modules.dsp.noise.noise_distribution must be gaussian or uniform")
		}
		if cfg.Modules.DSP.Noise.ThermalLowpassHz < 0 {
			return errors.New("modules.dsp.noise.thermal_lowpass_hz must be >= 0")
		}
		if cfg.Modules.DSP.Noise.MaxNoiseDB < cfg.Modules.DSP.Noise.MinNoiseDB {
			return errors.New("modules.dsp.noise min/max noise range is invalid")
		}
	}
	if cfg.Modules.DSP.Squelch != nil && cfg.Modules.DSP.Squelch.Enabled {
		if cfg.Modules.DSP.Squelch.ThresholdPercent < 0 || cfg.Modules.DSP.Squelch.ThresholdPercent > 100 {
			return errors.New("modules.dsp.squelch.threshold_percent must be in [0..100]")
		}
		if cfg.Modules.DSP.Squelch.NoiseGain <= 0 {
			return errors.New("modules.dsp.squelch.noise_gain must be > 0")
		}
		if cfg.Modules.DSP.Squelch.SquelchMinMs <= 0 || cfg.Modules.DSP.Squelch.SquelchMaxMs < cfg.Modules.DSP.Squelch.SquelchMinMs {
			return errors.New("modules.dsp.squelch squelch range is invalid")
		}
		if cfg.Modules.DSP.Squelch.TailMinMs <= 0 || cfg.Modules.DSP.Squelch.TailMaxMs < cfg.Modules.DSP.Squelch.TailMinMs {
			return errors.New("modules.dsp.squelch tail range is invalid")
		}
		if cfg.Modules.DSP.Squelch.SynthSilenceTailPackets < 0 {
			return errors.New("modules.dsp.squelch.synth_silence_tail_packets must be >= 0")
		}
	}
	if cfg.Modules.DSP.Pops != nil && cfg.Modules.DSP.Pops.Enabled {
		pops := mergePopsConfig(*cfg.Modules.DSP.Pops)
		if math.IsNaN(pops.ClickDB) || math.IsInf(pops.ClickDB, 0) {
			return errors.New("modules.dsp.pops.click_db must be a finite number")
		}
		if pops.ClickToneHz != 0 && (math.IsNaN(pops.ClickToneHz) || math.IsInf(pops.ClickToneHz, 0) || pops.ClickToneHz <= 0) {
			return errors.New("modules.dsp.pops.click_tone_hz must be > 0 when set")
		}
		if pops.GlitchIntervalMaxMs < 0 {
			return errors.New("modules.dsp.pops.glitch_interval_max_ms must be >= 0")
		}
		if math.IsNaN(pops.GlitchFreqMinHz) || math.IsInf(pops.GlitchFreqMinHz, 0) ||
			math.IsNaN(pops.GlitchFreqMaxHz) || math.IsInf(pops.GlitchFreqMaxHz, 0) {
			return errors.New("modules.dsp.pops glitch_freq range must be finite")
		}
		if pops.GlitchFreqMinHz <= 0 || pops.GlitchFreqMaxHz < pops.GlitchFreqMinHz {
			return errors.New("modules.dsp.pops glitch_freq range is invalid")
		}
		if math.IsNaN(pops.GlitchLevelMinDB) || math.IsInf(pops.GlitchLevelMinDB, 0) ||
			math.IsNaN(pops.GlitchLevelMaxDB) || math.IsInf(pops.GlitchLevelMaxDB, 0) {
			return errors.New("modules.dsp.pops glitch_level range must be finite")
		}
		if pops.GlitchLevelMaxDB < pops.GlitchLevelMinDB {
			return errors.New("modules.dsp.pops glitch_level range is invalid")
		}
	}
	if cfg.Modules.DSP.Clicks != nil && cfg.Modules.DSP.Clicks.Enabled {
		if cfg.Modules.DSP.Clicks.MultiClientRapidMs < 0 {
			return errors.New("modules.dsp.clicks.multi_client_rapid_ms must be >= 0")
		}
		if cfg.Modules.DSP.Clicks.Impulses != nil && cfg.Modules.DSP.Clicks.Impulses.Enabled {
			im := cfg.Modules.DSP.Clicks.Impulses
			if im.ProbAtWeakSignal < 0 || im.ProbAtWeakSignal > 1 || im.ProbAtStrongSignal < 0 || im.ProbAtStrongSignal > 1 {
				return errors.New("modules.dsp.clicks.impulses prob_at_*_signal must be in [0..1]")
			}
			if math.IsNaN(im.GainDB) || math.IsInf(im.GainDB, 0) {
				return errors.New("modules.dsp.clicks.impulses.gain_db must be finite")
			}
		}
	}
	if cfg.Modules.DSP.Distortion != nil && cfg.Modules.DSP.Distortion.Enabled {
		if cfg.Modules.DSP.Distortion.Mix < 0 || cfg.Modules.DSP.Distortion.Mix > 1 {
			return errors.New("modules.dsp.distortion.mix must be in [0..1]")
		}
		if math.IsNaN(cfg.Modules.DSP.Distortion.MultiClientDriveBoost) || math.IsInf(cfg.Modules.DSP.Distortion.MultiClientDriveBoost, 0) ||
			math.IsNaN(cfg.Modules.DSP.Distortion.MultiClientMixBoost) || math.IsInf(cfg.Modules.DSP.Distortion.MultiClientMixBoost, 0) {
			return errors.New("modules.dsp.distortion multi_client_*_boost must be finite")
		}
		if cfg.Modules.DSP.Distortion.MultiClientDriveBoost < 0 || cfg.Modules.DSP.Distortion.MultiClientMixBoost < 0 {
			return errors.New("modules.dsp.distortion multi_client_*_boost must be >= 0")
		}
	}
	if cfg.Modules.DSP.Filter != nil && cfg.Modules.DSP.Filter.Enabled {
		if math.IsNaN(cfg.Modules.DSP.Filter.LowCutHz) || math.IsInf(cfg.Modules.DSP.Filter.LowCutHz, 0) ||
			math.IsNaN(cfg.Modules.DSP.Filter.HighCutHz) || math.IsInf(cfg.Modules.DSP.Filter.HighCutHz, 0) {
			return errors.New("modules.dsp.filter cutoff range must be finite")
		}
		if cfg.Modules.DSP.Filter.LowCutHz < 0 || cfg.Modules.DSP.Filter.HighCutHz < 0 {
			return errors.New("modules.dsp.filter cutoff range is invalid")
		}
		if cfg.Modules.DSP.Filter.LowCutHz > 0 && cfg.Modules.DSP.Filter.HighCutHz > 0 &&
			cfg.Modules.DSP.Filter.HighCutHz <= cfg.Modules.DSP.Filter.LowCutHz {
			return errors.New("modules.dsp.filter cutoff range is invalid")
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

	log.Printf("o-walkie relay version %s (config %s)", buildVersion, cfgPath)

	cfg, err := loadConfig(cfgPath)
	if err != nil {
		log.Fatalf("config error: %v", err)
	}
	applyAudioTiming(cfg.Server.SampleRate, cfg.Server.PacketMs)
	configuredOpus = normalizeOpusConfig(cfg.Server.Opus)

	rand.Seed(time.Now().UnixNano())

	listenAddr := fmt.Sprintf(":%d", cfg.Server.Port)
	udpConn, err := net.ListenUDP("udp", mustResolveUDP(listenAddr))
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
		"relay started port=%d (ws+udp) sample_rate=%d packet_ms=%d opus_bitrate=%d opus_complexity=%d opus_fec=%t opus_dtx=%t opus_application=%s protocol_version=%d jitter_min_packets=%d busy_mode=%t transmit_timeout=%ds",
		cfg.Server.Port,
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
	if err := http.ListenAndServe(listenAddr, mux); err != nil {
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
