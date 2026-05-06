package main

import (
	"encoding/binary"
	"log"
	"net"
	"sync"
	"time"
)

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
