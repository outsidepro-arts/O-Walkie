package main

import (
	"log"
	"sync"
)

var dspChainMissingLogOnce sync.Once

func buildChannelModuleSets(mcfg modulesConfig) (generators []audioModule, dsp []audioModule) {
	generators = make([]audioModule, 0, 1)
	dsp = make([]audioModule, 0, 8)
	if mcfg.Generators.SquelchShots != nil && mcfg.Generators.SquelchShots.Enabled {
		generators = append(generators, newSquelchShotsGenerator(*mcfg.Generators.SquelchShots))
	}
	if len(mcfg.DSP.Chain) == 0 {
		dspChainMissingLogOnce.Do(func() {
			log.Printf("ERROR modules.dsp.chain is not configured: DSP plugins are disabled")
		})
		return generators, dsp
	}
	available := make(map[string]audioModule, 7)
	if mcfg.DSP.Pops != nil && mcfg.DSP.Pops.Enabled {
		available["pops"] = newPopsModule(packetDur, *mcfg.DSP.Pops)
	}
	if mcfg.DSP.Clicks != nil && mcfg.DSP.Clicks.Enabled {
		available["clicks"] = newClicksModule(configuredSampleRate, *mcfg.DSP.Clicks)
	}
	if mcfg.DSP.Noise != nil && mcfg.DSP.Noise.Enabled {
		available["noise"] = newNoiseDSPModule(*mcfg.DSP.Noise)
	}
	if mcfg.DSP.Squelch != nil && mcfg.DSP.Squelch.Enabled {
		available["squelch"] = newSquelchDSPModule(packetDur, *mcfg.DSP.Squelch)
	}
	if mcfg.DSP.Filter != nil && mcfg.DSP.Filter.Enabled {
		available["filter"] = newBandPassModule(configuredSampleRate, *mcfg.DSP.Filter)
	}
	if mcfg.DSP.Dispersion != nil && mcfg.DSP.Dispersion.Enabled {
		available["dispersion"] = newDispersionDSPModule(configuredSampleRate, *mcfg.DSP.Dispersion)
	}
	if mcfg.DSP.Compressor != nil && mcfg.DSP.Compressor.Enabled {
		available["compressor"] = newCompressorModule(configuredSampleRate, *mcfg.DSP.Compressor)
	}
	if mcfg.DSP.Distortion != nil && mcfg.DSP.Distortion.Enabled {
		available["distortion"] = newDistortionModule(*mcfg.DSP.Distortion)
	}
	for _, name := range mcfg.DSP.Chain {
		if mod, ok := available[name]; ok && mod != nil {
			dsp = append(dsp, mod)
		}
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
