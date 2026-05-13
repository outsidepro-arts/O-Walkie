package main

import "time"

func (m *channelMixer) resetRxWsBroadcastState() {
	m.rxWsMu.Lock()
	defer m.rxWsMu.Unlock()
	m.rxOutSessionActive = false
	m.rxLastOutboundAt = time.Time{}
	m.pttLockServerActive = false
	m.lastPttLockDisplaySec = 0
}

// noteOutboundMixedUDP must run after each mixed UDP frame is sent to the channel.
// excludeSession must match broadcastMixed: solo TX / tail exclude — that client must not get
// rx_broadcast_start / ptt_lock for audio they do not receive (would self-abort TX on clients).
func (m *channelMixer) noteOutboundMixedUDP(now time.Time, excludeSession uint32) {
	var startMsg *wsServerMessage
	var lockMsg *wsServerMessage
	m.rxWsMu.Lock()
	if !m.rxOutSessionActive {
		m.rxOutSessionActive = true
		s := wsServerMessage{Type: "rx_broadcast_start", BusyMode: boolPtr(m.busyMode)}
		startMsg = &s
		if m.busyMode {
			ds := 0
			if m.busyTimeout > 0 {
				ds = int(m.busyTimeout / time.Second)
			}
			m.lastPttLockDisplaySec = ds
			m.pttLockServerActive = true
			l := wsServerMessage{Type: "ptt_lock", DisplaySec: intPtr(ds)}
			lockMsg = &l
		}
	}
	m.rxLastOutboundAt = now
	m.rxWsMu.Unlock()
	if startMsg != nil {
		m.hub.broadcastChannelWSJSONExcept(m.name, excludeSession, *startMsg)
	}
	if lockMsg != nil {
		m.hub.broadcastChannelWSJSONExcept(m.name, excludeSession, *lockMsg)
	}
}

func (m *channelMixer) probeRxBroadcastWsIdle(now time.Time) {
	m.rxWsMu.Lock()
	if !m.rxOutSessionActive || m.rxLastOutboundAt.IsZero() {
		m.rxWsMu.Unlock()
		return
	}
	if now.Sub(m.rxLastOutboundAt) < m.eofTimeoutDur {
		m.rxWsMu.Unlock()
		return
	}
	m.rxWsMu.Unlock()
	m.finishRxBroadcastIdle()
}

func (m *channelMixer) finishRxBroadcastIdle() {
	delayMs := int(m.eofTimeoutDur / time.Millisecond)
	if delayMs < 0 {
		delayMs = 0
	}
	var sendUnlock bool
	m.rxWsMu.Lock()
	if !m.rxOutSessionActive {
		m.rxWsMu.Unlock()
		return
	}
	m.rxOutSessionActive = false
	m.rxLastOutboundAt = time.Time{}
	// Clear WS ptt_lock when outbound mixed-audio session ends (paired with first-frame lock).
	if m.busyMode && m.pttLockServerActive {
		m.pttLockServerActive = false
		sendUnlock = true
	}
	m.rxWsMu.Unlock()

	m.hub.broadcastChannelWSJSON(m.name, wsServerMessage{
		Type:       "rx_broadcast_end",
		EndDelayMs: intPtr(delayMs),
	})
	if sendUnlock {
		m.hub.broadcastChannelWSJSON(m.name, wsServerMessage{Type: "ptt_unlock"})
	}
}

func (m *channelMixer) syncWsSessionStateToClient(c *client) {
	m.rxWsMu.Lock()
	active := m.rxOutSessionActive
	lock := m.pttLockServerActive
	ds := m.lastPttLockDisplaySec
	busy := m.busyMode
	m.rxWsMu.Unlock()
	if active {
		_ = c.writeJSON(wsServerMessage{Type: "rx_broadcast_start", BusyMode: boolPtr(busy)})
	}
	if lock {
		_ = c.writeJSON(wsServerMessage{Type: "ptt_lock", DisplaySec: intPtr(ds)})
	}
}
