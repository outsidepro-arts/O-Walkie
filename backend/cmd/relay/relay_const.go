package main

import "time"

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

	// Per-speaker adaptive jitter (uplink): concealment streak → deeper buffer; stable delivery → shallower.
	jitterAdaptConcealThreshold    = 4
	jitterAdaptStableDecreaseTicks = 80
	jitterMaxPacketsLimit          = 24 // max for jitter_max_packets when jitter_adapt_enabled
)
