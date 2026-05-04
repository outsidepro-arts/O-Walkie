package main

// audioProcessContext is the shared frame context passed through generator and DSP stages.
type audioProcessContext struct {
	Mixed          []float64
	AvgSignalByte  float64
	ActiveSpeakers int
	NoiseLevelDB   float64
	Control        audioModuleControl
	EmitFrame      bool
	DropFrame      bool
	// SquelchHissJustEnded: synthetic hiss (TX tail or idle-shot noise) ended this tick — terminal click.
	SquelchHissJustEnded bool
	// QueueSynthSilenceTail: after this mixed frame, emit zero-PCM tail before going fully idle.
	QueueSynthSilenceTail bool
}

// audioModuleControl carries transport/lifecycle events to modules.
type audioModuleControl struct {
	TxActive       bool
	TxStart        bool
	TxEOF          bool
	MultiClientMix bool
	SignalByte     *float64
}

// audioModule is implemented by every pipeline stage.
type audioModule interface {
	Name() string
	Process(ctx *audioProcessContext)
}

// GeneratorModule may inject energy or gate output; runs before DSP modules.
type GeneratorModule interface {
	audioModule
}

// DSPModule shapes the mix after generators (filter, dynamics, saturation).
type DSPModule interface {
	audioModule
}
