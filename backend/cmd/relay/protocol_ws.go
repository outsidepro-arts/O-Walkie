package main

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
	TransmitTimeout *int          `json:"transmitTimeoutSec,omitempty"`
	Active          *bool         `json:"active,omitempty"`
	// rx_broadcast_end: server idle guard (ms) after last outbound mix frame (same order as EOF timeout).
	EndDelayMs *int `json:"endDelayMs,omitempty"`
	// ptt_lock: UI-only countdown hint; clients must not unlock on timer — only on ptt_unlock.
	DisplaySec *int `json:"displaySec,omitempty"`
}

type wsOpusConfig struct {
	Bitrate     int    `json:"bitrate"`
	Complexity  int    `json:"complexity"`
	FEC         bool   `json:"fec"`
	DTX         bool   `json:"dtx"`
	Application string `json:"application"`
}
