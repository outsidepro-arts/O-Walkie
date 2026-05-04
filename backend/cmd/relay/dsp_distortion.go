package main

import "math"

type distortionModule struct {
	drive                 float64
	mix                   float64
	multiClientDriveBoost float64
	multiClientMixBoost   float64
}

func newDistortionModule(cfg distortionConfig) *distortionModule {
	return &distortionModule{
		drive:                 cfg.Drive,
		mix:                   cfg.Mix,
		multiClientDriveBoost: cfg.MultiClientDriveBoost,
		multiClientMixBoost:   cfg.MultiClientMixBoost,
	}
}

func (m *distortionModule) Name() string {
	return "distortion"
}

func (m *distortionModule) Process(ctx *audioProcessContext) {
	drive := m.drive
	mix := m.mix
	if ctx.Control.MultiClientMix {
		drive += m.multiClientDriveBoost
		mix += m.multiClientMixBoost
		if mix > 1.0 {
			mix = 1.0
		}
	}
	for i := range ctx.Mixed {
		dry := ctx.Mixed[i] / 32768.0
		wet := math.Tanh(dry * drive)
		out := dry*(1.0-mix) + wet*mix
		ctx.Mixed[i] = out * 32768.0
	}
}
