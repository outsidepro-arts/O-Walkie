package main

import "math"

type compressorModule struct {
	thresholdDB  float64
	ratio        float64
	attackCoeff  float64
	releaseCoeff float64
	makeupGain   float64
	envelopeDB   float64
}

func newCompressorModule(sr int, cfg compressorConfig) *compressorModule {
	attackSec := cfg.AttackMs / 1000.0
	releaseSec := cfg.ReleaseMs / 1000.0
	if attackSec <= 0 {
		attackSec = 0.005
	}
	if releaseSec <= 0 {
		releaseSec = 0.08
	}
	return &compressorModule{
		thresholdDB:  cfg.ThresholdDB,
		ratio:        cfg.Ratio,
		attackCoeff:  math.Exp(-1.0 / (float64(sr) * attackSec)),
		releaseCoeff: math.Exp(-1.0 / (float64(sr) * releaseSec)),
		makeupGain:   dbToLinear(cfg.MakeupDB),
		envelopeDB:   -90.0,
	}
}

func (m *compressorModule) Name() string {
	return "compressor"
}

func (m *compressorModule) Process(ctx *audioProcessContext) {
	for i := range ctx.Mixed {
		x := ctx.Mixed[i]
		amp := math.Abs(x)
		inDB := -90.0
		if amp > 1e-9 {
			inDB = 20.0 * math.Log10(amp/32768.0)
		}

		// Attack when level rises, release when level falls.
		if inDB > m.envelopeDB {
			m.envelopeDB = m.attackCoeff*m.envelopeDB + (1.0-m.attackCoeff)*inDB
		} else {
			m.envelopeDB = m.releaseCoeff*m.envelopeDB + (1.0-m.releaseCoeff)*inDB
		}

		gainDB := 0.0
		if m.envelopeDB > m.thresholdDB {
			over := m.envelopeDB - m.thresholdDB
			compressedOver := over / m.ratio
			gainDB = compressedOver - over // negative attenuation
		}
		gain := dbToLinear(gainDB) * m.makeupGain
		ctx.Mixed[i] = x * gain
	}
}
