package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strconv"
	"strings"
)

func defaultConfig() appConfig {
	return appConfig{
		Server: serverConfig{
			Port:       5500,
			SampleRate: defaultSampleRate,
			PacketMs:   defaultPacketMs,
			Opus: opusConfig{
				Bitrate:     12000,
				Complexity:  5,
				FEC:         true,
				DTX:         false,
				Application: "voip",
			},
			HangoverMs:         180,
			EOFTimeoutMs:       420,
			ConcealDecay:       0.90,
			JitterMinPkts:      3,
			BusyMode:           false,
			TransmitTimeoutSec: 0,
		},
		Modules: modulesConfig{
			Generators: generatorsModulesConfig{
				SquelchShots: &squelchShotsGeneratorConfig{
					Enabled:          false,
					IntervalMinMs:    60000,
					IntervalMaxMs:    180000,
					SilenceMinMs:     50,
					SilenceMaxMs:     200,
					SignalMinPercent: 4.0,
					SignalMaxPercent: 18.0,
				},
			},
			DSP: dspModulesConfig{
				Chain: []string{"pops", "clicks", "noise", "squelch", "filter", "compressor", "distortion"},
				Clicks: &clicksConfig{
					Enabled:             false,
					ImpulseProbAtWeak:   0.10,
					ImpulseProbAtStrong: 0.01,
					ImpulseGainDB:       -8.0,
					MultiClientRapidMs:  0, // 0 = every frame while multi-client TX is active.
				},
				Pops: &popsConfig{
					Enabled:             true,
					ClickDB:             -8.0,
					ClickToneHz:         200.0,
					GlitchIntervalMaxMs: 0,
					GlitchFreqMinHz:     120.0,
					GlitchFreqMaxHz:     360.0,
					GlitchLevelMinDB:    -14.0,
					GlitchLevelMaxDB:    -6.0,
				},
				Noise: &noiseDSPConfig{
					Enabled:           true,
					SignalDependent:   true,
					MinNoiseDB:        -20.0,
					MaxNoiseDB:        -3.0,
					NoiseGain:         1200.0,
					NoiseDistribution: "gaussian",
					ThermalLowpassHz:  8000,
				},
				Squelch: &squelchDSPConfig{
					Enabled:                 true,
					ThresholdPercent:        5.0,
					SquelchMinMs:            50,
					SquelchMaxMs:            150,
					NoiseDistribution:       "gaussian",
					ThermalLowpassHz:        8000,
					NoiseGain:               1200.0,
					TailNoiseDB:             0.0,
					TailMinMs:               15,
					TailMaxMs:               60,
					SynthSilenceTailPackets: 0,
				},
				Filter: &filterConfig{
					Enabled:   true,
					LowCutHz:  300.0,
					HighCutHz: 3000.0,
				},
				Dispersion: &dispersionConfig{
					Enabled:       false,
					Stages:        24,
					CenterHz:      800.0,
					Resonance:     0.55,
					SpreadOctaves: 0.12,
					SpreadStyle:   "octaves",
				},
				Compressor: &compressorConfig{
					Enabled:     true,
					ThresholdDB: -18.0,
					Ratio:       3.2,
					AttackMs:    5.0,
					ReleaseMs:   80.0,
					MakeupDB:    4.0,
				},
				Distortion: &distortionConfig{
					Enabled: true,
					Drive:   1.35,
					Mix:     0.28,
				},
			},
		},
	}
}

func loadConfig(path string) (appConfig, error) {
	cfg := defaultConfig()
	data, err := os.ReadFile(path)
	if err != nil {
		return appConfig{}, fmt.Errorf("read config %s: %w", path, err)
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return appConfig{}, fmt.Errorf("parse config %s: %w", path, err)
	}
	cfg.Server.PacketMs = normalizePacketMs(cfg.Server.PacketMs)
	cfg.Server.JitterMinPkts = normalizeJitterMinPackets(cfg.Server.JitterMinPkts)
	cfg.Server.Opus = normalizeOpusConfig(cfg.Server.Opus)
	normalizeServerListenPort(&cfg.Server)
	normalizeModulesConfig(&cfg.Modules)
	if err := validateConfig(cfg); err != nil {
		return appConfig{}, err
	}
	cfg.Server.SampleRate = normalizeSampleRate(cfg.Server.SampleRate)
	return cfg, nil
}

func parsePortFromListenAddr(s string) int {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0
	}
	if strings.HasPrefix(s, ":") {
		p, err := strconv.Atoi(s[1:])
		if err != nil || p <= 0 {
			return 0
		}
		return p
	}
	i := strings.LastIndex(s, ":")
	if i < 0 || i >= len(s)-1 {
		return 0
	}
	p, err := strconv.Atoi(s[i+1:])
	if err != nil || p <= 0 {
		return 0
	}
	return p
}

func normalizeServerListenPort(s *serverConfig) {
	if s.Port == 0 {
		if p := parsePortFromListenAddr(s.LegacyWsAddr); p > 0 {
			s.Port = p
		} else if p := parsePortFromListenAddr(s.LegacyUdpAddr); p > 0 {
			s.Port = p
		}
	}
	if s.Port == 0 {
		s.Port = 5500
	}
	s.LegacyWsAddr = ""
	s.LegacyUdpAddr = ""
}

func normalizeModulesConfig(m *modulesConfig) {
	if m == nil {
		return
	}
	// Migrate legacy flat layout into the new generators/dsp layout when missing.
	if m.Noise != nil {
		if m.DSP.Noise == nil {
			m.DSP.Noise = &noiseDSPConfig{
				Enabled:           m.Noise.Enabled,
				SignalDependent:   m.Noise.SignalDependent,
				MinNoiseDB:        m.Noise.MinNoiseDB,
				MaxNoiseDB:        m.Noise.MaxNoiseDB,
				NoiseGain:         m.Noise.NoiseGain,
				NoiseDistribution: m.Noise.NoiseDistribution,
				ThermalLowpassHz:  m.Noise.ThermalLowpassHz,
			}
		}
		if m.DSP.Squelch == nil {
			thresholdPct := 5.0
			if m.Noise.SignalDependent && m.Noise.MaxNoiseDB > m.Noise.MinNoiseDB {
				thresholdPct = 10.0 + 90.0*((m.Noise.MaxNoiseDB-m.Noise.SquelchThresholdDB)/(m.Noise.MaxNoiseDB-m.Noise.MinNoiseDB))
				if thresholdPct < 0 {
					thresholdPct = 0
				}
				if thresholdPct > 100 {
					thresholdPct = 100
				}
			}
			m.DSP.Squelch = &squelchDSPConfig{
				Enabled:                 m.Noise.Enabled,
				ThresholdPercent:        thresholdPct,
				SquelchMinMs:            m.Noise.SquelchMinMs,
				SquelchMaxMs:            m.Noise.SquelchMaxMs,
				NoiseDistribution:       m.Noise.NoiseDistribution,
				ThermalLowpassHz:        m.Noise.ThermalLowpassHz,
				NoiseGain:               m.Noise.NoiseGain,
				TailNoiseDB:             m.Noise.TailNoiseDB,
				TailMinMs:               m.Noise.TailMinMs,
				TailMaxMs:               m.Noise.TailMaxMs,
				SynthSilenceTailPackets: m.Noise.SynthSilenceTailPackets,
			}
		}
	}
	if m.Click != nil {
		if m.DSP.Pops == nil {
			m.DSP.Pops = &popsConfig{
				Enabled:             m.Click.Enabled,
				ClickDB:             m.Click.ClickDB,
				ClickToneHz:         m.Click.ClickToneHz,
				GlitchIntervalMaxMs: m.Click.GlitchIntervalMaxMs,
				GlitchFreqMinHz:     m.Click.GlitchFreqMinHz,
				GlitchFreqMaxHz:     m.Click.GlitchFreqMaxHz,
				GlitchLevelMinDB:    m.Click.GlitchLevelMinDB,
				GlitchLevelMaxDB:    m.Click.GlitchLevelMaxDB,
			}
		}
		if m.DSP.Clicks == nil {
			cc := clicksConfig{Enabled: m.Click.Enabled}
			if m.Click.Impulses != nil {
				cc.ImpulseProbAtWeak = m.Click.Impulses.ProbAtWeakSignal
				cc.ImpulseProbAtStrong = m.Click.Impulses.ProbAtStrongSignal
				cc.ImpulseGainDB = m.Click.Impulses.GainDB
			}
			m.DSP.Clicks = &cc
		}
	}
	if m.Filter != nil && m.DSP.Filter == nil {
		m.DSP.Filter = m.Filter
	}
	if m.Compressor != nil && m.DSP.Compressor == nil {
		m.DSP.Compressor = m.Compressor
	}
	if m.Distortion != nil && m.DSP.Distortion == nil {
		m.DSP.Distortion = m.Distortion
	}
}
