package main

import (
	"encoding/binary"
	"log"
	"time"

	"github.com/hraban/opus"
)

type repeaterSession struct {
	processor  *audioProcessor
	bufferPCM  []int16
	lastPacket time.Time
	collecting bool
	lastSignal float64
	eofMarked  bool
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
