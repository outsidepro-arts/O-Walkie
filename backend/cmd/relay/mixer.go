package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"math"
	"runtime/debug"
	"sync"
	"sync/atomic"
	"time"

	"github.com/hraban/opus"
)

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
	jitterMaxPackets    uint16
	jitterAdaptEnabled  bool
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
	txCountdownNotified map[uint32]bool

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
	jMin := uint16(normalizeJitterMinPackets(cfg.Server.JitterMinPkts))
	jMax := jMin
	if cfg.Server.JitterAdaptEnabled {
		jMax = uint16(normalizeJitterMaxPackets(cfg.Server.JitterMinPkts, cfg.Server.JitterMaxPkts))
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
		jitterMinPackets:   jMin,
		jitterMaxPackets:   jMax,
		jitterAdaptEnabled: cfg.Server.JitterAdaptEnabled,
		busyMode:           cfg.Server.BusyMode,
		transmitTimeout:    time.Duration(maxInt(cfg.Server.TransmitTimeoutSec, 0)) * time.Second,
		txStartedAt:        make(map[uint32]time.Time),
		txForceStopped:     make(map[uint32]bool),
		txLastStopNoticeAt: make(map[uint32]time.Time),
		txCountdownNotified: make(map[uint32]bool),
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
	shouldStartCountdown := false
	accept := true
	if m.transmitTimeout > 0 {
		startedAt, started := m.txStartedAt[sessionID]
		if !started {
			m.txStartedAt[sessionID] = now
			m.txCountdownNotified[sessionID] = false
		} else if m.txForceStopped[sessionID] {
			shouldNotify = m.shouldNotifyTxStopLocked(sessionID, now)
			accept = false
		} else if now.Sub(startedAt) >= m.transmitTimeout {
			m.txForceStopped[sessionID] = true
			shouldNotify = m.shouldNotifyTxStopLocked(sessionID, now)
			accept = false
		} else {
			countdownLead := m.transmitTimeout - 5*time.Second
			if countdownLead < 0 {
				countdownLead = 0
			}
			if !m.txCountdownNotified[sessionID] && now.Sub(startedAt) >= countdownLead {
				m.txCountdownNotified[sessionID] = true
				shouldStartCountdown = true
			}
		}
	}
	m.txMu.Unlock()
	if shouldStartCountdown {
		m.notifyTxCountdownStart(sessionID)
	}
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

func (m *channelMixer) notifyTxCountdownStart(sessionID uint32) {
	if c := m.hub.getClient(sessionID); c != nil {
		_ = c.writeJSON(wsServerMessage{
			Type: "tx_countdown_start",
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

	m.txMu.Lock()
	delete(m.txStartedAt, sessionID)
	delete(m.txForceStopped, sessionID)
	delete(m.txLastStopNoticeAt, sessionID)
	delete(m.txCountdownNotified, sessionID)
	m.txMu.Unlock()
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
	delete(m.txCountdownNotified, sessionID)
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

		freshFromUDP := false
		usedConceal := false

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
				freshFromUDP = true
			} else {
				// Zero-sample decode behaves like a gap in transport.
				hasPacket = false
			}
		}
		if !speakerActive && !hasPacket && !eofMarked[sessionID] && now.Sub(st.lastPacketAt) <= m.eofTimeoutDur && len(st.lastPCM) > 0 {
			usedConceal = true
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

		if m.jitterAdaptEnabled && !eofMarked[sessionID] {
			st.adaptJitterOnTick(freshFromUDP, usedConceal, int(m.jitterMinPackets), int(m.jitterMaxPackets))
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
