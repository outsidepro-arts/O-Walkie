package main

import (
	"math"
	"math/rand"
	"time"
)

type clicksModule struct {
	probWeak      float64
	probStrong    float64
	gainWeakDB    float64
	gainStrongDB  float64
	peakLinearAmp float64
	clickBP       *bandPass
	tailBuf       []float64
	rapidInterval time.Duration
	rapidRemain   time.Duration
}

func newClicksModule(sr int, cfg clicksConfig) *clicksModule {
	m := &clicksModule{
		probWeak:   cfg.ImpulseProbAtWeak,
		probStrong: cfg.ImpulseProbAtStrong,
	}
	if cfg.MultiClientRapidMs > 0 {
		m.rapidInterval = time.Duration(cfg.MultiClientRapidMs) * time.Millisecond
	}
	weakDB := cfg.ImpulseGainDB
	strongDB := cfg.ImpulseGainDB
	if cfg.ImpulseGainAtWeakDB != nil {
		weakDB = *cfg.ImpulseGainAtWeakDB
	}
	if cfg.ImpulseGainAtStrongDB != nil {
		strongDB = *cfg.ImpulseGainAtStrongDB
	}
	m.gainWeakDB = weakDB
	m.gainStrongDB = strongDB
	peakLin := 32767.0 * math.Max(dbToLinear(weakDB), dbToLinear(strongDB))
	if peakLin < 0 || math.IsNaN(peakLin) {
		peakLin = 0
	}
	if peakLin > 32767.0 {
		peakLin = 32767.0
	}
	m.peakLinearAmp = peakLin
	if cfg.Filter != nil && cfg.Filter.Enabled {
		m.clickBP = newBandPass(sr, cfg.Filter.LowCutHz, cfg.Filter.HighCutHz)
	}
	return m
}

func (m *clicksModule) Name() string { return "clicks" }

func (m *clicksModule) Process(ctx *audioProcessContext) {
	if m.peakLinearAmp <= 0 || len(ctx.Mixed) == 0 {
		return
	}
	// Like pops: only texture incoming carrier — idle channel must stay silent (no EmitFrame from clicks alone).
	if !ctx.Control.TxActive && ctx.ActiveSpeakers <= 0 {
		m.tailBuf = m.tailBuf[:0]
		m.rapidRemain = 0
		return
	}
	m.flushClickTail(ctx)
	signalByte := ctx.AvgSignalByte
	if ctx.Control.SignalByte != nil {
		signalByte = *ctx.Control.SignalByte
	}
	// With multiple active transmitters we can accelerate click generation
	// to emulate stronger RF overlap texture.
	if ctx.Control.MultiClientMix {
		if m.rapidInterval <= 0 {
			m.injectImpulse(ctx, signalByte)
			return
		}
		if m.rapidRemain <= 0 {
			m.injectImpulse(ctx, signalByte)
			m.rapidRemain = m.rapidInterval
		}
		m.rapidRemain -= packetDur
		return
	}
	m.rapidRemain = 0
	p := mapSignalByteToImpulseProbability(signalByte, m.probWeak, m.probStrong)
	if p <= 0 || rand.Float64() >= p {
		return
	}
	m.injectImpulse(ctx, signalByte)
}

func (m *clicksModule) flushClickTail(ctx *audioProcessContext) {
	if len(m.tailBuf) == 0 || len(ctx.Mixed) == 0 {
		return
	}
	n := min(len(m.tailBuf), len(ctx.Mixed))
	for i := 0; i < n; i++ {
		ctx.Mixed[i] += m.tailBuf[i]
	}
	if n > 0 {
		ctx.EmitFrame = true
	}
	m.tailBuf = append(m.tailBuf[:0], m.tailBuf[n:]...)
}

func (m *clicksModule) injectImpulse(ctx *audioProcessContext, signalByte float64) {
	if len(ctx.Mixed) == 0 {
		return
	}
	gainDB := mapSignalByteToImpulseGainDB(signalByte, m.gainWeakDB, m.gainStrongDB)
	amp := 32767.0 * dbToLinear(gainDB)
	if amp <= 0 || math.IsNaN(amp) {
		return
	}
	if amp > 32767.0 {
		amp = 32767.0
	}
	i := rand.Intn(len(ctx.Mixed))
	sign := 1.0
	if rand.Intn(2) == 0 {
		sign = -1.0
	}
	if m.clickBP == nil {
		ctx.Mixed[i] += sign * amp
		ctx.EmitFrame = true
		return
	}
	m.clickBP.reset()
	const (
		clickTailEpsilon    = 1e-6
		clickTailMaxSamples = 6000
		clickTailMinSamples = 24
	)
	inVal := sign * amp
	for n := 0; n < clickTailMaxSamples; n++ {
		y := m.clickBP.processSample(inVal)
		inVal = 0
		if n >= clickTailMinSamples && math.Abs(y) < clickTailEpsilon {
			break
		}
		pos := i + n
		if pos < len(ctx.Mixed) {
			ctx.Mixed[pos] += y
		} else {
			m.tailBuf = append(m.tailBuf, y)
		}
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
	clickToneHz := cfg.ClickToneHz
	if clickToneHz <= 0 {
		clickToneHz = 200.0
	}
	amp := 32767.0 * dbToLinear(cfg.ClickDB)
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
		glitchMaxMs:    maxInt(cfg.GlitchIntervalMaxMs, 0),
		glitchFreqMin:  cfg.GlitchFreqMinHz,
		glitchFreqMax:  cfg.GlitchFreqMaxHz,
		glitchAmpMin:   32767.0 * dbToLinear(cfg.GlitchLevelMinDB),
		glitchAmpMax:   32767.0 * dbToLinear(cfg.GlitchLevelMaxDB),
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

func mapSignalByteToImpulseProbability(signalByte, probWeak, probStrong float64) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return probWeak
	}
	if pct >= 100.0 {
		return probStrong
	}
	return probWeak + ((pct-10.0)/90.0)*(probStrong-probWeak)
}

func mapSignalByteToImpulseGainDB(signalByte, dbWeak, dbStrong float64) float64 {
	pct := (signalByte / 255.0) * 100.0
	if pct <= 10.0 {
		return dbWeak
	}
	if pct >= 100.0 {
		return dbStrong
	}
	return dbWeak + ((pct-10.0)/90.0)*(dbStrong-dbWeak)
}
