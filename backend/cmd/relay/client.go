package main

import (
	"net"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

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
