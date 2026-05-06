package main

import (
	"context"
	"fmt"
	"log"
	"math/rand"
	"net"
	"net/http"
	"os"
	"strings"
	"time"
)

// buildVersion is overridden by release builds: go build -ldflags "-X main.buildVersion=1.2.3"
var buildVersion = "dev"

func main() {
	if len(os.Args) > 2 {
		log.Fatalf("usage: %s [config-path]", os.Args[0])
	}
	cfgPath := configFilePath
	if len(os.Args) == 2 {
		cfgPath = strings.TrimSpace(os.Args[1])
		if cfgPath == "" {
			log.Fatalf("usage: %s [config-path]", os.Args[0])
		}
	}

	log.Printf("o-walkie relay version %s (config %s)", buildVersion, cfgPath)

	cfg, err := loadConfig(cfgPath)
	if err != nil {
		log.Fatalf("config error: %v", err)
	}
	applyAudioTiming(cfg.Server.SampleRate, cfg.Server.PacketMs)
	configuredOpus = normalizeOpusConfig(cfg.Server.Opus)

	rand.Seed(time.Now().UnixNano())

	listenAddr := fmt.Sprintf(":%d", cfg.Server.Port)
	udpConn, err := net.ListenUDP("udp", mustResolveUDP(listenAddr))
	if err != nil {
		log.Fatalf("udp listen failed: %v", err)
	}
	defer udpConn.Close()

	hub := newRelayHub(udpConn, cfg)
	s := &server{hub: hub}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go udpReader(ctx, udpConn, hub)

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", s.wsHandler)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ok"))
	})

	log.Printf(
		"relay started port=%d (ws+udp) sample_rate=%d packet_ms=%d opus_bitrate=%d opus_complexity=%d opus_fec=%t opus_dtx=%t opus_application=%s protocol_version=%d jitter_min_packets=%d jitter_adapt=%t jitter_max_packets=%d busy_mode=%t transmit_timeout=%ds",
		cfg.Server.Port,
		normalizeSampleRate(cfg.Server.SampleRate),
		normalizePacketMs(cfg.Server.PacketMs),
		configuredOpus.Bitrate,
		configuredOpus.Complexity,
		configuredOpus.FEC,
		configuredOpus.DTX,
		configuredOpus.Application,
		protocolVersion,
		cfg.Server.JitterMinPkts,
		cfg.Server.JitterAdaptEnabled,
		cfg.Server.JitterMaxPkts,
		cfg.Server.BusyMode,
		cfg.Server.TransmitTimeoutSec,
	)
	if err := http.ListenAndServe(listenAddr, mux); err != nil {
		log.Fatalf("ws server error: %v", err)
	}
}

func mustResolveUDP(addr string) *net.UDPAddr {
	ua, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		panic(err)
	}
	return ua
}
