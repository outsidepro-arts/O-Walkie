package main

import (
	"encoding/binary"
	"errors"
	"net"
)

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
