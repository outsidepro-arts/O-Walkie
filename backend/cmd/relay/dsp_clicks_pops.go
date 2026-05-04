package main

import (
	"math"
	"math/rand"
	"time"
)

type clicksModule struct {
	impulses      *clickImpulsesConfig
	impulseAmp    float64
	rapidInterval time.Duration
	rapidRemain   time.Duration
}

func newClicksModule(cfg clicksConfig) *clicksModule {
	m := &clicksModule{
		impulses: cfg.Impulses,
	}
	if cfg.MultiClientRapidMs > 0 {
		m.rapidInterval = time.Duration(cfg.MultiClientRapidMs) * time.Millisecond
	}
	if cfg.Impulses != nil && cfg.Impulses.Enabled {
		m.impulseAmp = 32767.0 * dbToLinear(cfg.Impulses.GainDB)
		if m.impulseAmp < 0 {
			m.impulseAmp = 0
		}
		if m.impulseAmp > 32767.0 {
			m.impulseAmp = 32767.0
		}
	}
	return m
}

func (m *clicksModule) Name() string { return "clicks" }

func (m *clicksModule) Process(ctx *audioProcessContext) {
	if m.impulses == nil || !m.impulses.Enabled || m.impulseAmp <= 0 || len(ctx.Mixed) == 0 {
		return
	}
	// With multiple active transmitters we can accelerate click generation
	// to emulate stronger RF overlap texture.
	if ctx.Control.MultiClientMix {
		if m.rapidInterval <= 0 {
			m.injectImpulse(ctx)
			return
		}
		if m.rapidRemain <= 0 {
			m.injectImpulse(ctx)
			m.rapidRemain = m.rapidInterval
		}
		m.rapidRemain -= packetDur
		return
	}
	m.rapidRemain = 0
	signalByte := ctx.AvgSignalByte
	if ctx.Control.SignalByte != nil {
		signalByte = *ctx.Control.SignalByte
	}
	p := mapSignalByteToImpulseProbability(signalByte, *m.impulses)
	if p <= 0 || rand.Float64() >= p {
		return
	}
	m.injectImpulse(ctx)
}

func (m *clicksModule) injectImpulse(ctx *audioProcessContext) {
	i := rand.Intn(len(ctx.Mixed))
	if rand.Intn(2) == 0 {
		ctx.Mixed[i] -= m.impulseAmp
	} else {
		ctx.Mixed[i] += m.impulseAmp
	}
	ctx.EmitFrame = true
}

type popsModule struct {
	clickAmplitude float64
	freqHz         float64
	burstSamples   int
	frameDuration  time.Duration
	glitchMaxMs    int
	glitchRemain   time.Duration
	glitchFreqMin  float64
	glitchFreqMax  float64
	glitchAmpMin   float64
	glitchAmpMax   float64
	lastActive     bool
}

func newPopsModule(frameDuration time.Duration, cfg popsConfig) *popsModule {
	pops := mergePopsConfig(cfg)
	clickToneHz := pops.ClickToneHz
	if clickToneHz <= 0 {
		clickToneHz = 200.0
	}
	amp := 32767.0 * dbToLinear(pops.ClickDB)
	if amp < 0 {
		amp = 0
	}
	if amp > 32767.0 {
		amp = 32767.0
	}
	return &popsModule{
		clickAmplitude: amp,
		freqHz:         clickToneHz,
		burstSamples:   configuredSampleRate / 80, // ~12.5 ms
		frameDuration:  frameDuration,
		glitchMaxMs:    maxInt(pops.GlitchIntervalMaxMs, 0),
		glitchFreqMin:  pops.GlitchFreqMinHz,
		glitchFreqMax:  pops.GlitchFreqMaxHz,
		glitchAmpMin:   32767.0 * dbToLinear(pops.GlitchLevelMinDB),
		glitchAmpMax:   32767.0 * dbToLinear(pops.GlitchLevelMaxDB),
	}
}

func (m *popsModule) Name() string { return "pops" }

func (m *popsModule) Process(ctx *audioProcessContext) {
	active := ctx.Control.TxActive || ctx.ActiveSpeakers > 0
	txStart := ctx.Control.TxStart || (active && !m.lastActive)
	txEOF := ctx.Control.TxEOF || (!active && m.lastActive)

	if txStart {
		m.injectClick(ctx, 1.0)
		ctx.EmitFrame = true
		m.scheduleNextGlitch()
	}
	if active {
		m.processRandomGlitchClicks(ctx)
	}
	if txEOF {
		// End click is same profile as start click.
		m.injectClick(ctx, 1.0)
		ctx.EmitFrame = true
	}
	m.lastActive = active
}

func (m *popsModule) processRandomGlitchClicks(ctx *audioProcessContext) {
	if m.glitchMaxMs <= 0 || len(ctx.Mixed) == 0 || m.glitchAmpMax <= 0 {
		return
	}
	if m.glitchRemain <= 0 {
		freq := randomFloat64(m.glitchFreqMin, m.glitchFreqMax)
		amp := randomFloat64(m.glitchAmpMin, m.glitchAmpMax)
		m.injectClickWithParams(ctx, 1.0, freq, amp)
		m.scheduleNextGlitch()
		return
	}
	m.glitchRemain -= m.frameDuration
}

func (m *popsModule) scheduleNextGlitch() {
	if m.glitchMaxMs <= 0 {
		m.glitchRemain = 0
		return
	}
	m.glitchRemain = randomDurationMs(1, m.glitchMaxMs)
}

func (m *popsModule) injectClick(ctx *audioProcessContext, sign float64) {
	m.injectClickWithParams(ctx, sign, m.freqHz, m.clickAmplitude)
}

func (m *popsModule) injectClickWithParams(ctx *audioProcessContext, sign float64, freqHz float64, amplitude float64) {
	if len(ctx.Mixed) == 0 || amplitude <= 0 || m.burstSamples <= 0 || freqHz <= 0 {
		return
	}
	limit := m.burstSamples
	if limit > len(ctx.Mixed) {
		limit = len(ctx.Mixed)
	}
	phaseOffset := 0.0
	if sign < 0 {
		phaseOffset = math.Pi
	}
	for i := 0; i < limit; i++ {
		t := float64(i) / float64(configuredSampleRate)
		env := math.Exp(-4.0 * float64(i) / float64(limit))
		wave := math.Sin(2.0*math.Pi*freqHz*t + phaseOffset)
		ctx.Mixed[i] += wave * amplitude * env
	}
}

func mergePopsConfig(cfg popsConfig) clickPopsConfig {
	if cfg.Pops != nil {
		return *cfg.Pops
	}
	return clickPopsConfig{
		ClickDB:             cfg.ClickDB,
		ClickToneHz:         cfg.ClickToneHz,
		GlitchIntervalMaxMs: cfg.GlitchIntervalMaxMs,
		GlitchFreqMinHz:     cfg.GlitchFreqMinHz,
		GlitchFreqMaxHz:     cfg.GlitchFreqMaxHz,
		GlitchLevelMinDB:    cfg.GlitchLevelMinDB,
		GlitchLevelMaxDB:    cfg.GlitchLevelMaxDB,
	}
}

func mapSignalByteToImpulseProbability(signalByte float64, c clickImpulsesConfig) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return c.ProbAtWeakSignal
	}
	if pct >= 100.0 {
		return c.ProbAtStrongSignal
	}
	return c.ProbAtWeakSignal + ((pct-10.0)/90.0)*(c.ProbAtStrongSignal-c.ProbAtWeakSignal)
}
