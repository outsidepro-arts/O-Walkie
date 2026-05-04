package main

func buildChannelModuleSets(mcfg modulesConfig) (generators []audioModule, dsp []audioModule) {
	generators = make([]audioModule, 0, 1)
	dsp = make([]audioModule, 0, 7)
	if mcfg.Generators.SquelchShots != nil && mcfg.Generators.SquelchShots.Enabled {
		generators = append(generators, newSquelchShotsGenerator(*mcfg.Generators.SquelchShots))
	}
	if mcfg.DSP.Pops != nil && mcfg.DSP.Pops.Enabled {
		dsp = append(dsp, newPopsModule(packetDur, *mcfg.DSP.Pops))
	}
	if mcfg.DSP.Clicks != nil && mcfg.DSP.Clicks.Enabled {
		dsp = append(dsp, newClicksModule(*mcfg.DSP.Clicks))
	}
	if mcfg.DSP.Noise != nil && mcfg.DSP.Noise.Enabled {
		dsp = append(dsp, newNoiseDSPModule(*mcfg.DSP.Noise))
	}
	if mcfg.DSP.Squelch != nil && mcfg.DSP.Squelch.Enabled {
		dsp = append(dsp, newSquelchDSPModule(packetDur, *mcfg.DSP.Squelch))
	}
	if mcfg.DSP.Filter != nil && mcfg.DSP.Filter.Enabled {
		dsp = append(dsp, newBandPassModule(configuredSampleRate, *mcfg.DSP.Filter))
	}
	if mcfg.DSP.Compressor != nil && mcfg.DSP.Compressor.Enabled {
		dsp = append(dsp, newCompressorModule(configuredSampleRate, *mcfg.DSP.Compressor))
	}
	if mcfg.DSP.Distortion != nil && mcfg.DSP.Distortion.Enabled {
		dsp = append(dsp, newDistortionModule(*mcfg.DSP.Distortion))
	}
	return generators, dsp
}

func processModuleChain(mods []audioModule, ctx *audioProcessContext) bool {
	for _, mod := range mods {
		mod.Process(ctx)
		if ctx.DropFrame {
			return true
		}
	}
	return false
}
