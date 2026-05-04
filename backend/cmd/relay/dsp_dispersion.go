package main

import (
	"math"
	"strings"
)

const (
	dispersionMinHz      = 5.0
	dispersionMaxStages  = 512
	dispersionNyquistDiv = 2.05
)

// dispersionConfig: cascaded second-order allpass filters (phase dispersion, flat magnitude).
// Does not use audioProcessContext.Control — pure sample processing only.
type dispersionConfig struct {
	Enabled bool `json:"enabled"`
	// Stages: number of allpass biquads in series (1..dispersionMaxStages).
	Stages int `json:"stages"`
	// CenterHz: pivot frequency; stages are spread around it.
	CenterHz float64 `json:"center_hz"`
	// Resonance: Q of each stage (Diopser-style; must be > 0, typical 0.3..3).
	Resonance float64 `json:"resonance"`
	// SpreadOctaves: per-stage frequency spread in octaves (negative allowed). 0 = same frequency for all stages.
	SpreadOctaves float64 `json:"spread_octaves"`
	// SpreadStyle: "octaves" (default) or "linear" (Hz offset derived from octave spread range).
	SpreadStyle string `json:"spread_style,omitempty"`
}

type dispersionBiquad struct {
	b0, b1, b2, a1, a2 float64
	s1, s2             float64
}

type dispersionDSPModule struct {
	enabled bool
	filters []dispersionBiquad
}

func newDispersionDSPModule(sampleRate int, cfg dispersionConfig) *dispersionDSPModule {
	if !cfg.Enabled || cfg.Stages <= 0 || sampleRate <= 0 {
		return &dispersionDSPModule{enabled: false}
	}
	stages := cfg.Stages
	if stages > dispersionMaxStages {
		stages = dispersionMaxStages
	}
	sr := float64(sampleRate)
	nyquist := sr / dispersionNyquistDiv

	style := strings.ToLower(strings.TrimSpace(cfg.SpreadStyle))
	if style != "linear" {
		style = "octaves"
	}
	freq := cfg.CenterHz
	if freq < dispersionMinHz {
		freq = dispersionMinHz
	}
	if freq > nyquist {
		freq = nyquist
	}
	q := cfg.Resonance
	if q < 0.01 {
		q = 0.01
	}
	spread := cfg.SpreadOctaves

	var maxLinSpread float64
	if spread >= 0 {
		maxLinSpread = freq - freq*math.Pow(2, -spread)
	} else {
		maxLinSpread = freq*math.Pow(2, spread) - freq
	}

	filters := make([]dispersionBiquad, stages)
	inv := 1.0 / float64(stages)
	for i := 0; i < stages; i++ {
		prop := float64(i)*inv*2.0 - 1.0
		var fStage float64
		if style == "linear" {
			fStage = freq + maxLinSpread*prop
		} else {
			fStage = freq * math.Pow(2, spread*prop)
		}
		if fStage < dispersionMinHz {
			fStage = dispersionMinHz
		}
		if fStage > nyquist {
			fStage = nyquist
		}
		b0, b1, b2, a1, a2 := allpassBiquadCoefficients(sr, fStage, q)
		filters[i] = dispersionBiquad{b0: b0, b1: b1, b2: b2, a1: a1, a2: a2}
	}
	return &dispersionDSPModule{enabled: true, filters: filters}
}

func (m *dispersionDSPModule) Name() string {
	return "dispersion"
}

func (m *dispersionDSPModule) Process(ctx *audioProcessContext) {
	if !m.enabled || len(m.filters) == 0 {
		return
	}
	for i := range ctx.Mixed {
		x := ctx.Mixed[i]
		for j := range m.filters {
			x = m.filters[j].processSample(x)
		}
		ctx.Mixed[i] = x
	}
}

func (b *dispersionBiquad) processSample(x float64) float64 {
	result := b.b0*x + b.s1
	b.s1 = b.b1*x - b.a1*result + b.s2
	b.s2 = b.b2*x - b.a2*result
	return result
}

// allpassBiquadCoefficients: normalized RBJ-style allpass (a0=1), same structure as Diopser filter.rs.
func allpassBiquadCoefficients(sampleRate, frequency, q float64) (b0, b1, b2, a1, a2 float64) {
	omega0 := 2.0 * math.Pi * frequency / sampleRate
	cosW := math.Cos(omega0)
	sinW := math.Sin(omega0)
	alpha := sinW / (2.0 * q)
	a0 := 1.0 + alpha
	b0 = (1.0 - alpha) / a0
	b1 = (-2.0 * cosW) / a0
	b2 = (1.0 + alpha) / a0
	a1 = (-2.0 * cosW) / a0
	a2 = (1.0 - alpha) / a0
	return b0, b1, b2, a1, a2
}
