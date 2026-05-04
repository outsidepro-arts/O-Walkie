package main

import (
	"math"
	"math/rand"
	"strings"
	"time"
)

type whiteNoise struct {
	gaussian   bool
	win        int
	buf        []float64
	sum        float64
	pos        int
	count      int
	hasSpare   bool
	spareGauss float64
}

func newWhiteNoise(distribution string, thermalLowpassHz float64) *whiteNoise {
	w := &whiteNoise{}
	switch strings.ToLower(strings.TrimSpace(distribution)) {
	case "", "gaussian":
		w.gaussian = true
	case "uniform":
		w.gaussian = false
	default:
		w.gaussian = true
	}
	if thermalLowpassHz > 0 && configuredSampleRate > 0 {
		win := int(float64(configuredSampleRate) / thermalLowpassHz)
		if win < 2 {
			win = 2
		}
		w.win = win
		w.buf = make([]float64, win)
	}
	return w
}

func (w *whiteNoise) nextRaw() float64 {
	if !w.gaussian {
		return rand.Float64()*2 - 1
	}
	if w.hasSpare {
		w.hasSpare = false
		return w.spareGauss * math.Sqrt(1.0/3.0)
	}
	u1 := rand.Float64()
	for u1 <= 1e-12 {
		u1 = rand.Float64()
	}
	u2 := rand.Float64()
	mag := math.Sqrt(-2.0 * math.Log(u1))
	z0 := mag * math.Cos(2*math.Pi*u2)
	z1 := mag * math.Sin(2*math.Pi*u2)
	w.spareGauss = z1
	w.hasSpare = true
	return z0 * math.Sqrt(1.0/3.0)
}

func (w *whiteNoise) next() float64 {
	x := w.nextRaw()
	if w.win <= 0 {
		return x
	}
	if w.count < w.win {
		w.buf[w.pos] = x
		w.sum += x
		w.pos = (w.pos + 1) % w.win
		w.count++
		return w.sum / float64(w.count)
	}
	old := w.buf[w.pos]
	w.buf[w.pos] = x
	w.sum += x - old
	w.pos = (w.pos + 1) % w.win
	return w.sum / float64(w.win)
}

type noiseDSPModule struct {
	white *whiteNoise
	cfg   noiseDSPConfig
}

func newNoiseDSPModule(cfg noiseDSPConfig) *noiseDSPModule {
	return &noiseDSPModule{
		white: newWhiteNoise(cfg.NoiseDistribution, cfg.ThermalLowpassHz),
		cfg:   cfg,
	}
}

func (m *noiseDSPModule) Name() string { return "noise" }

func (m *noiseDSPModule) Process(ctx *audioProcessContext) {
	if len(ctx.Mixed) == 0 {
		return
	}
	noiseDB := m.cfg.MinNoiseDB
	if m.cfg.SignalDependent {
		signalByte := ctx.AvgSignalByte
		if ctx.Control.SignalByte != nil {
			signalByte = *ctx.Control.SignalByte
		}
		noiseDB = mapSignalByteToNoiseDB(signalByte, m.cfg.MinNoiseDB, m.cfg.MaxNoiseDB)
	}
	ctx.NoiseLevelDB = noiseDB
	noiseAmplitude := m.cfg.NoiseGain * dbToLinear(noiseDB)
	for i := range ctx.Mixed {
		ctx.Mixed[i] += m.white.next() * noiseAmplitude
	}
	if ctx.Control.TxActive {
		ctx.EmitFrame = true
	}
}

type squelchDSPModule struct {
	white              *whiteNoise
	frameDuration      time.Duration
	squelchBurstRemain time.Duration
	squelchLatched     bool
	lastActive         bool
	cfg                squelchDSPConfig
}

func newSquelchDSPModule(frameDuration time.Duration, cfg squelchDSPConfig) *squelchDSPModule {
	return &squelchDSPModule{
		white:         newWhiteNoise("gaussian", 0),
		frameDuration: frameDuration,
		cfg:           cfg,
	}
}

func (m *squelchDSPModule) Name() string {
	return "squelch"
}

func (m *squelchDSPModule) Process(ctx *audioProcessContext) {
	active := ctx.Control.TxActive || ctx.ActiveSpeakers > 0
	txEOF := ctx.Control.TxEOF || (!active && m.lastActive)
	if !active {
		// End-of-transmission tail burst: jump to 0 dB and emit white hiss briefly.
		if txEOF && m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(m.cfg.TailMinMs, m.cfg.TailMaxMs)
		}
		m.lastActive = false
		if m.squelchBurstRemain > 0 {
			noiseAmplitude := m.cfg.NoiseGain * dbToLinear(m.cfg.TailNoiseDB)
			for i := range ctx.Mixed {
				ctx.Mixed[i] += m.white.next() * noiseAmplitude
			}
			ctx.EmitFrame = true
			m.squelchBurstRemain -= m.frameDuration
			if m.squelchBurstRemain <= 0 {
				m.squelchBurstRemain = 0
				ctx.SquelchHissJustEnded = true
				ctx.QueueSynthSilenceTail = true
			}
			return
		}
		m.squelchLatched = false
		ctx.EmitFrame = false
		return
	}
	m.lastActive = true

	signalByte := ctx.AvgSignalByte
	if ctx.Control.SignalByte != nil {
		signalByte = *ctx.Control.SignalByte
	}
	signalPct := (signalByte / 255.0) * 100.0
	// Squelch behavior: when line is weak, emit short burst then gate.
	if signalPct <= m.cfg.ThresholdPercent {
		if m.squelchLatched {
			ctx.DropFrame = true
			return
		}
		if m.squelchBurstRemain <= 0 {
			m.squelchBurstRemain = randomDurationMs(m.cfg.SquelchMinMs, m.cfg.SquelchMaxMs)
		}
		noiseAmplitude := m.cfg.NoiseGain * dbToLinear(m.cfg.TailNoiseDB)
		for i := range ctx.Mixed {
			ctx.Mixed[i] += m.white.next() * noiseAmplitude
		}
		m.squelchBurstRemain -= m.frameDuration
		if m.squelchBurstRemain <= 0 {
			m.squelchLatched = true
			ctx.DropFrame = true
		}
		return
	}
	m.squelchLatched = false
	m.squelchBurstRemain = 0
}

func mapSignalByteToNoiseDB(signalByte float64, minNoiseDB float64, maxNoiseDB float64) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return maxNoiseDB
	}
	if pct >= 100.0 {
		return minNoiseDB
	}
	// Linear interpolation in dB domain:
	// 10% -> max noise dB, 100% -> min noise dB.
	return maxNoiseDB - ((pct-10.0)/90.0)*(maxNoiseDB-minNoiseDB)
}
