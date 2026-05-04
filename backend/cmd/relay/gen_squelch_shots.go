package main

import (
	"math/rand"
	"time"
)

type squelchShotsPhase int

const (
	squelchShotsPhaseIdle squelchShotsPhase = iota
	squelchShotsPhaseStartTick
	squelchShotsPhaseSilentTx
	squelchShotsPhaseEOFTick
)

type squelchShotsGenerator struct {
	cfg         squelchShotsGeneratorConfig
	nextAt      time.Time
	phase       squelchShotsPhase
	silenceLeft time.Duration
	signalByte  float64
}

func newSquelchShotsGenerator(cfg squelchShotsGeneratorConfig) *squelchShotsGenerator {
	return &squelchShotsGenerator{
		cfg:   cfg,
		phase: squelchShotsPhaseIdle,
	}
}

func (m *squelchShotsGenerator) Name() string {
	return "squelch_shots"
}

func (m *squelchShotsGenerator) Process(ctx *audioProcessContext) {
	// Do not inject synthetic shots while real TX is active.
	if ctx.Control.TxActive {
		m.phase = squelchShotsPhaseIdle
		m.nextAt = time.Time{}
		return
	}

	now := time.Now()
	if m.nextAt.IsZero() {
		m.nextAt = now.Add(randomDurationMs(m.cfg.IntervalMinMs, m.cfg.IntervalMaxMs))
	}

	switch m.phase {
	case squelchShotsPhaseIdle:
		if now.Before(m.nextAt) {
			return
		}
		m.signalByte = randomSignalByteFromPercentRange(m.cfg.SignalMinPercent, m.cfg.SignalMaxPercent)
		m.phase = squelchShotsPhaseStartTick
		m.silenceLeft = randomDurationMs(m.cfg.SilenceMinMs, m.cfg.SilenceMaxMs)
		m.applySyntheticTx(ctx, true, false)
	case squelchShotsPhaseStartTick:
		m.phase = squelchShotsPhaseSilentTx
		m.applySyntheticTx(ctx, false, false)
	case squelchShotsPhaseSilentTx:
		m.applySyntheticTx(ctx, false, false)
		m.silenceLeft -= packetDur
		if m.silenceLeft <= 0 {
			m.phase = squelchShotsPhaseEOFTick
		}
	case squelchShotsPhaseEOFTick:
		// Explicit EOF to let DSP chain perform post-TX behavior.
		ctx.Control.TxEOF = true
		ctx.Control.TxActive = false
		ctx.EmitFrame = false
		ctx.AvgSignalByte = m.signalByte
		ctx.Control.SignalByte = &ctx.AvgSignalByte
		m.phase = squelchShotsPhaseIdle
		m.nextAt = now.Add(randomDurationMs(m.cfg.IntervalMinMs, m.cfg.IntervalMaxMs))
	}
}

func (m *squelchShotsGenerator) applySyntheticTx(ctx *audioProcessContext, txStart bool, txEOF bool) {
	ctx.Control.TxActive = true
	ctx.Control.TxStart = txStart
	ctx.Control.TxEOF = txEOF
	ctx.EmitFrame = true
	ctx.AvgSignalByte = m.signalByte
	ctx.Control.SignalByte = &ctx.AvgSignalByte
}

func randomSignalByteFromPercentRange(minPercent float64, maxPercent float64) float64 {
	p := minPercent
	if maxPercent > minPercent {
		p = minPercent + rand.Float64()*(maxPercent-minPercent)
	}
	if p < 0 {
		p = 0
	}
	if p > 100 {
		p = 100
	}
	return (p / 100.0) * 255.0
}
