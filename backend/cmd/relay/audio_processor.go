package main

type audioProcessor struct {
	generators   []audioModule
	dspMods      []audioModule
	noiseLevelDB float64
	lastSignal   float64
	lastTxActive bool
}

func newAudioProcessor(mcfg modulesConfig) *audioProcessor {
	noiseFloor := -30.0
	if mcfg.DSP.Noise != nil {
		noiseFloor = mcfg.DSP.Noise.MinNoiseDB
	}
	gen, dsp := buildChannelModuleSets(mcfg)
	return &audioProcessor{
		generators:   gen,
		dspMods:      dsp,
		noiseLevelDB: noiseFloor,
		lastSignal:   255.0,
	}
}

func (p *audioProcessor) process(input []int16, signalByte float64, active bool) ([]int16, bool) {
	mixed := make([]float64, packetSamples)
	txStart := active && !p.lastTxActive
	txEOF := !active && p.lastTxActive
	p.lastTxActive = active
	var signalForModules *float64
	if active {
		p.lastSignal = signalByte
		signalForModules = &p.lastSignal
		limit := len(input)
		if limit > packetSamples {
			limit = packetSamples
		}
		for i := 0; i < limit; i++ {
			mixed[i] = float64(input[i])
		}
	}
	ctx := &audioProcessContext{
		Mixed:          mixed,
		AvgSignalByte:  p.lastSignal,
		ActiveSpeakers: boolToInt(active),
		NoiseLevelDB:   p.noiseLevelDB,
		Control: audioModuleControl{
			TxActive:       active,
			TxStart:        txStart,
			TxEOF:          txEOF,
			MultiClientMix: false,
			SignalByte:     signalForModules,
		},
		EmitFrame: active,
	}
	if processModuleChain(p.generators, ctx) {
		p.noiseLevelDB = ctx.NoiseLevelDB
		return nil, false
	}
	if processModuleChain(p.dspMods, ctx) {
		p.noiseLevelDB = ctx.NoiseLevelDB
		return nil, false
	}
	p.noiseLevelDB = ctx.NoiseLevelDB
	if !ctx.EmitFrame {
		return nil, false
	}

	out := make([]int16, packetSamples)
	for i := 0; i < packetSamples; i++ {
		out[i] = hardClipFloat(ctx.Mixed[i], 15000.0)
	}
	return out, true
}
