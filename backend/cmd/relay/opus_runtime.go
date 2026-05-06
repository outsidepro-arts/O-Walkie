package main

import (
	"log"
	"strings"

	"github.com/hraban/opus"
)

func normalizeOpusConfig(cfg opusConfig) opusConfig {
	out := cfg
	if out.Bitrate <= 0 {
		out.Bitrate = 12000
	}
	if out.Bitrate < 6000 {
		out.Bitrate = 6000
	}
	if out.Bitrate > 510000 {
		out.Bitrate = 510000
	}
	if out.Complexity < 0 || out.Complexity > 10 {
		out.Complexity = 5
	}
	switch strings.ToLower(strings.TrimSpace(out.Application)) {
	case "voip", "audio", "lowdelay":
		out.Application = strings.ToLower(strings.TrimSpace(out.Application))
	default:
		out.Application = "voip"
	}
	return out
}

func resolveOpusApplication(name string) opus.Application {
	switch strings.ToLower(strings.TrimSpace(name)) {
	case "audio":
		return opus.AppAudio
	case "lowdelay":
		return opus.AppRestrictedLowdelay
	default:
		return opus.AppVoIP
	}
}

func applyOpusEncoderConfig(enc *opus.Encoder) {
	if enc == nil {
		return
	}
	cfg := normalizeOpusConfig(configuredOpus)
	if err := enc.SetBitrate(cfg.Bitrate); err != nil {
		log.Printf("opus SetBitrate(%d) failed: %v", cfg.Bitrate, err)
	}
	if err := enc.SetComplexity(cfg.Complexity); err != nil {
		log.Printf("opus SetComplexity(%d) failed: %v", cfg.Complexity, err)
	}
	if err := enc.SetInBandFEC(cfg.FEC); err != nil {
		log.Printf("opus SetInBandFEC(%t) failed: %v", cfg.FEC, err)
	}
	if err := enc.SetDTX(cfg.DTX); err != nil {
		log.Printf("opus SetDTX(%t) failed: %v", cfg.DTX, err)
	}
}
