package main

import (
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
)

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
