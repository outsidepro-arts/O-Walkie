package main

import "time"

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

	// Adaptive jitter (relay uplink): updated from mix loop when jitter_adapt_enabled.
	jitterAdaptConcealStreak int
	jitterAdaptStableTicks   int
}

type speakerJitterBuffer struct {
	minStart int
	maxKeep  int
	packets  map[uint32]*audioPacket
	head     uint32
	started  bool
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

// setMinStartClamped updates buffering depth (floor..ceil) and trims excess packets.
func (b *speakerJitterBuffer) setMinStartClamped(n, floor, ceil int) {
	if b == nil {
		return
	}
	if n < floor {
		n = floor
	}
	if n > ceil {
		n = ceil
	}
	if n == b.minStart {
		return
	}
	b.minStart = n
	b.maxKeep = n * 8
	if b.maxKeep < 32 {
		b.maxKeep = 32
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
