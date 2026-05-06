package main

import "time"

var (
	configuredSampleRate = defaultSampleRate
	packetDur            = time.Duration(defaultPacketMs) * time.Millisecond
	packetSamples        = configuredSampleRate * defaultPacketMs / 1000
	configuredOpus       = opusConfig{
		Bitrate:     12000,
		Complexity:  5,
		FEC:         true,
		DTX:         false,
		Application: "voip",
	}
)

func normalizePacketMs(ms int) int {
	if ms <= 0 {
		return defaultPacketMs
	}
	return ms
}

func normalizeSampleRate(rate int) int {
	switch rate {
	case 8000, 12000, 16000, 24000, 48000:
		return rate
	default:
		return defaultSampleRate
	}
}

func isSupportedSampleRate(rate int) bool {
	return rate == normalizeSampleRate(rate)
}

func isSupportedPacketMs(ms int) bool {
	switch ms {
	case 10, 20, 40, 60:
		return true
	default:
		return false
	}
}

func normalizeJitterMinPackets(count int) int {
	if count <= 0 {
		return 3
	}
	if count > 12 {
		return 12
	}
	return count
}

// normalizeJitterMaxPackets clamps adaptive ceiling: at least minPkts, at most jitterMaxPacketsLimit.
func normalizeJitterMaxPackets(minPkts, maxPkts int) int {
	if maxPkts < minPkts {
		maxPkts = minPkts
	}
	if maxPkts > jitterMaxPacketsLimit {
		maxPkts = jitterMaxPacketsLimit
	}
	return maxPkts
}

func applyAudioTiming(sampleRate int, packetMs int) {
	configuredSampleRate = normalizeSampleRate(sampleRate)
	norm := normalizePacketMs(packetMs)
	packetDur = time.Duration(norm) * time.Millisecond
	packetSamples = configuredSampleRate * norm / 1000
}
