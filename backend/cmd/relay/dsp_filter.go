package main

import "math"

type bandPassModule struct {
	filter *bandPass
}

func newBandPassModule(sr int, cfg filterConfig) *bandPassModule {
	return &bandPassModule{
		filter: newBandPass(sr, cfg.LowCutHz, cfg.HighCutHz),
	}
}

func (m *bandPassModule) Name() string {
	return "band_pass"
}

func (m *bandPassModule) Process(ctx *audioProcessContext) {
	if m.filter == nil {
		return
	}
	for i := range ctx.Mixed {
		ctx.Mixed[i] = m.filter.processSample(ctx.Mixed[i])
	}
}

type onePoleHP struct {
	alpha float64
	prevX float64
	prevY float64
}

func newOnePoleHP(sr int, cutoff float64) *onePoleHP {
	dt := 1.0 / float64(sr)
	rc := 1.0 / (2.0 * math.Pi * cutoff)
	alpha := rc / (rc + dt)
	return &onePoleHP{alpha: alpha}
}

func (f *onePoleHP) process(x float64) float64 {
	y := f.alpha * (f.prevY + x - f.prevX)
	f.prevX = x
	f.prevY = y
	return y
}

type onePoleLP struct {
	alpha float64
	prevY float64
}

func newOnePoleLP(sr int, cutoff float64) *onePoleLP {
	dt := 1.0 / float64(sr)
	rc := 1.0 / (2.0 * math.Pi * cutoff)
	alpha := dt / (rc + dt)
	return &onePoleLP{alpha: alpha}
}

func (f *onePoleLP) process(x float64) float64 {
	f.prevY = f.prevY + f.alpha*(x-f.prevY)
	return f.prevY
}

type bandPass struct {
	hp []*onePoleHP
	lp []*onePoleLP
}

func newBandPass(sr int, lowCut float64, highCut float64) *bandPass {
	const poles = 4 // 4 * 6 dB/oct = 24 dB/oct per side
	if lowCut <= 0 && highCut <= 0 {
		return nil
	}
	hp := make([]*onePoleHP, 0, poles)
	lp := make([]*onePoleLP, 0, poles)
	if lowCut > 0 {
		for i := 0; i < poles; i++ {
			hp = append(hp, newOnePoleHP(sr, lowCut))
		}
	}
	if highCut > 0 {
		for i := 0; i < poles; i++ {
			lp = append(lp, newOnePoleLP(sr, highCut))
		}
	}
	return &bandPass{
		hp: hp,
		lp: lp,
	}
}

func (b *bandPass) process(frame []int16) {
	for i := range frame {
		x := float64(frame[i])
		z := b.processSample(x)
		frame[i] = hardClip(int32(z), 32767)
	}
}

func (b *bandPass) processSample(x float64) float64 {
	y := x
	for _, hp := range b.hp {
		y = hp.process(y)
	}
	for _, lp := range b.lp {
		y = lp.process(y)
	}
	return y
}
