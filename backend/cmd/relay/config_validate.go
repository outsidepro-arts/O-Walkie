package main

import (
	"errors"
	"fmt"
	"math"
	"strings"
)

func validateConfig(cfg appConfig) error {
	if cfg.Server.Port < 1 || cfg.Server.Port > 65535 {
		return errors.New("server.port must be between 1 and 65535")
	}
	if !isSupportedPacketMs(cfg.Server.PacketMs) {
		return errors.New("server.packet_ms must be one of: 10, 20, 40, 60")
	}
	if !isSupportedSampleRate(cfg.Server.SampleRate) {
		return errors.New("server.sample_rate must be one of: 8000, 12000, 16000, 24000, 48000")
	}
	if cfg.Server.Opus.Bitrate < 6000 || cfg.Server.Opus.Bitrate > 510000 {
		return errors.New("server.opus.bitrate must be in [6000..510000]")
	}
	if cfg.Server.Opus.Complexity < 0 || cfg.Server.Opus.Complexity > 10 {
		return errors.New("server.opus.complexity must be in [0..10]")
	}
	if cfg.Server.Opus.Application != normalizeOpusConfig(cfg.Server.Opus).Application {
		return errors.New("server.opus.application must be one of: voip, audio, lowdelay")
	}
	if cfg.Server.HangoverMs < cfg.Server.PacketMs {
		return errors.New("server.hangover_ms must be >= server.packet_ms")
	}
	if cfg.Server.EOFTimeoutMs < cfg.Server.HangoverMs {
		return errors.New("server.eof_timeout_ms must be >= server.hangover_ms")
	}
	if cfg.Server.ConcealDecay <= 0 || cfg.Server.ConcealDecay > 1 {
		return errors.New("server.conceal_decay must be in (0..1]")
	}
	if cfg.Server.JitterMinPkts < 1 || cfg.Server.JitterMinPkts > 12 {
		return errors.New("server.jitter_min_packets must be in [1..12]")
	}
	if cfg.Server.JitterAdaptEnabled {
		if cfg.Server.JitterMaxPkts < cfg.Server.JitterMinPkts {
			return errors.New("server.jitter_max_packets must be >= server.jitter_min_packets when jitter_adapt_enabled")
		}
		if cfg.Server.JitterMaxPkts > jitterMaxPacketsLimit {
			return fmt.Errorf("server.jitter_max_packets must be <= %d", jitterMaxPacketsLimit)
		}
	}
	if cfg.Server.TransmitTimeoutSec < 0 {
		return errors.New("server.transmit_timeout must be >= 0")
	}
	if cfg.Modules.DSP.Compressor != nil && cfg.Modules.DSP.Compressor.Enabled {
		if cfg.Modules.DSP.Compressor.Ratio <= 0 {
			return errors.New("modules.compressor.ratio must be > 0")
		}
	}
	if cfg.Modules.DSP.Noise != nil && cfg.Modules.DSP.Noise.Enabled {
		if cfg.Modules.DSP.Noise.NoiseGain <= 0 {
			return errors.New("modules.dsp.noise.noise_gain must be > 0")
		}
		if cfg.Modules.DSP.Noise.NoiseDistribution == "" {
			cfg.Modules.DSP.Noise.NoiseDistribution = "gaussian"
		}
		if cfg.Modules.DSP.Noise.NoiseDistribution != "gaussian" && cfg.Modules.DSP.Noise.NoiseDistribution != "uniform" {
			return errors.New("modules.dsp.noise.noise_distribution must be gaussian or uniform")
		}
		if cfg.Modules.DSP.Noise.ThermalLowpassHz < 0 {
			return errors.New("modules.dsp.noise.thermal_lowpass_hz must be >= 0")
		}
		if cfg.Modules.DSP.Noise.MaxNoiseDB < cfg.Modules.DSP.Noise.MinNoiseDB {
			return errors.New("modules.dsp.noise min/max noise range is invalid")
		}
	}
	if cfg.Modules.Generators.SquelchShots != nil && cfg.Modules.Generators.SquelchShots.Enabled {
		shots := cfg.Modules.Generators.SquelchShots
		if shots.IntervalMinMs <= 0 || shots.IntervalMaxMs < shots.IntervalMinMs {
			return errors.New("modules.generators.squelch_shots interval range is invalid")
		}
		if shots.SilenceMinMs <= 0 || shots.SilenceMaxMs < shots.SilenceMinMs {
			return errors.New("modules.generators.squelch_shots silence range is invalid")
		}
		if math.IsNaN(shots.SignalMinPercent) || math.IsInf(shots.SignalMinPercent, 0) ||
			math.IsNaN(shots.SignalMaxPercent) || math.IsInf(shots.SignalMaxPercent, 0) {
			return errors.New("modules.generators.squelch_shots signal percent range must be finite")
		}
		if shots.SignalMinPercent < 0 || shots.SignalMaxPercent > 100 || shots.SignalMaxPercent < shots.SignalMinPercent {
			return errors.New("modules.generators.squelch_shots signal percent range is invalid")
		}
	}
	if len(cfg.Modules.DSP.Chain) > 0 {
		allowed := map[string]struct{}{
			"pops": {}, "clicks": {}, "noise": {}, "squelch": {}, "filter": {}, "dispersion": {}, "compressor": {}, "distortion": {},
		}
		for _, name := range cfg.Modules.DSP.Chain {
			if _, ok := allowed[name]; !ok {
				return fmt.Errorf("modules.dsp.chain contains unknown module %q", name)
			}
		}
	}
	if cfg.Modules.DSP.Squelch != nil && cfg.Modules.DSP.Squelch.Enabled {
		if cfg.Modules.DSP.Squelch.ThresholdPercent < 0 || cfg.Modules.DSP.Squelch.ThresholdPercent > 100 {
			return errors.New("modules.dsp.squelch.threshold_percent must be in [0..100]")
		}
		if cfg.Modules.DSP.Squelch.NoiseGain <= 0 {
			return errors.New("modules.dsp.squelch.noise_gain must be > 0")
		}
		if math.IsNaN(cfg.Modules.DSP.Squelch.TailNoiseDB) || math.IsInf(cfg.Modules.DSP.Squelch.TailNoiseDB, 0) {
			return errors.New("modules.dsp.squelch.tail_noise_db must be finite")
		}
		if cfg.Modules.DSP.Squelch.NoiseDistribution == "" {
			cfg.Modules.DSP.Squelch.NoiseDistribution = "gaussian"
		}
		if cfg.Modules.DSP.Squelch.NoiseDistribution != "gaussian" && cfg.Modules.DSP.Squelch.NoiseDistribution != "uniform" {
			return errors.New("modules.dsp.squelch.noise_distribution must be gaussian or uniform")
		}
		if cfg.Modules.DSP.Squelch.ThermalLowpassHz < 0 {
			return errors.New("modules.dsp.squelch.thermal_lowpass_hz must be >= 0")
		}
		if cfg.Modules.DSP.Squelch.SquelchMinMs <= 0 || cfg.Modules.DSP.Squelch.SquelchMaxMs < cfg.Modules.DSP.Squelch.SquelchMinMs {
			return errors.New("modules.dsp.squelch squelch range is invalid")
		}
		if cfg.Modules.DSP.Squelch.TailMinMs <= 0 || cfg.Modules.DSP.Squelch.TailMaxMs < cfg.Modules.DSP.Squelch.TailMinMs {
			return errors.New("modules.dsp.squelch tail range is invalid")
		}
		if cfg.Modules.DSP.Squelch.SynthSilenceTailPackets < 0 {
			return errors.New("modules.dsp.squelch.synth_silence_tail_packets must be >= 0")
		}
		if cfg.Modules.DSP.Squelch.EdgeImpulseDB != nil &&
			(math.IsNaN(*cfg.Modules.DSP.Squelch.EdgeImpulseDB) || math.IsInf(*cfg.Modules.DSP.Squelch.EdgeImpulseDB, 0)) {
			return errors.New("modules.dsp.squelch.edge_impulse_db must be finite")
		}
	}
	if cfg.Modules.DSP.Pops != nil && cfg.Modules.DSP.Pops.Enabled {
		p := cfg.Modules.DSP.Pops
		if math.IsNaN(p.ClickDB) || math.IsInf(p.ClickDB, 0) {
			return errors.New("modules.dsp.pops.click_db must be a finite number")
		}
		if p.ClickToneHz != 0 && (math.IsNaN(p.ClickToneHz) || math.IsInf(p.ClickToneHz, 0) || p.ClickToneHz <= 0) {
			return errors.New("modules.dsp.pops.click_tone_hz must be > 0 when set")
		}
		if p.GlitchIntervalMaxMs < 0 {
			return errors.New("modules.dsp.pops.glitch_interval_max_ms must be >= 0")
		}
		if math.IsNaN(p.GlitchFreqMinHz) || math.IsInf(p.GlitchFreqMinHz, 0) ||
			math.IsNaN(p.GlitchFreqMaxHz) || math.IsInf(p.GlitchFreqMaxHz, 0) {
			return errors.New("modules.dsp.pops glitch_freq range must be finite")
		}
		if p.GlitchFreqMinHz <= 0 || p.GlitchFreqMaxHz < p.GlitchFreqMinHz {
			return errors.New("modules.dsp.pops glitch_freq range is invalid")
		}
		if math.IsNaN(p.GlitchLevelMinDB) || math.IsInf(p.GlitchLevelMinDB, 0) ||
			math.IsNaN(p.GlitchLevelMaxDB) || math.IsInf(p.GlitchLevelMaxDB, 0) {
			return errors.New("modules.dsp.pops glitch_level range must be finite")
		}
		if p.GlitchLevelMaxDB < p.GlitchLevelMinDB {
			return errors.New("modules.dsp.pops glitch_level range is invalid")
		}
	}
	if cfg.Modules.DSP.Clicks != nil && cfg.Modules.DSP.Clicks.Enabled {
		if cfg.Modules.DSP.Clicks.MultiClientRapidMs < 0 {
			return errors.New("modules.dsp.clicks.multi_client_rapid_ms must be >= 0")
		}
		c := cfg.Modules.DSP.Clicks
		if c.ImpulseProbAtWeak < 0 || c.ImpulseProbAtWeak > 1 || c.ImpulseProbAtStrong < 0 || c.ImpulseProbAtStrong > 1 {
			return errors.New("modules.dsp.clicks impulse_prob_at_*_signal must be in [0..1]")
		}
		if math.IsNaN(c.ImpulseGainDB) || math.IsInf(c.ImpulseGainDB, 0) {
			return errors.New("modules.dsp.clicks.impulse_gain_db must be finite")
		}
		if c.ImpulseGainAtWeakDB != nil && (math.IsNaN(*c.ImpulseGainAtWeakDB) || math.IsInf(*c.ImpulseGainAtWeakDB, 0)) {
			return errors.New("modules.dsp.clicks.impulse_gain_at_weak_signal_db must be finite")
		}
		if c.ImpulseGainAtStrongDB != nil && (math.IsNaN(*c.ImpulseGainAtStrongDB) || math.IsInf(*c.ImpulseGainAtStrongDB, 0)) {
			return errors.New("modules.dsp.clicks.impulse_gain_at_strong_signal_db must be finite")
		}
		if c.Filter != nil && c.Filter.Enabled {
			if err := validateBandPassCutoffs(c.Filter.LowCutHz, c.Filter.HighCutHz, "modules.dsp.clicks.filter"); err != nil {
				return err
			}
		}
	}
	if cfg.Modules.DSP.Distortion != nil && cfg.Modules.DSP.Distortion.Enabled {
		if cfg.Modules.DSP.Distortion.Mix < 0 || cfg.Modules.DSP.Distortion.Mix > 1 {
			return errors.New("modules.dsp.distortion.mix must be in [0..1]")
		}
		if math.IsNaN(cfg.Modules.DSP.Distortion.MultiClientDriveBoost) || math.IsInf(cfg.Modules.DSP.Distortion.MultiClientDriveBoost, 0) ||
			math.IsNaN(cfg.Modules.DSP.Distortion.MultiClientMixBoost) || math.IsInf(cfg.Modules.DSP.Distortion.MultiClientMixBoost, 0) {
			return errors.New("modules.dsp.distortion multi_client_*_boost must be finite")
		}
		if cfg.Modules.DSP.Distortion.MultiClientDriveBoost < 0 || cfg.Modules.DSP.Distortion.MultiClientMixBoost < 0 {
			return errors.New("modules.dsp.distortion multi_client_*_boost must be >= 0")
		}
	}
	if cfg.Modules.DSP.Filter != nil && cfg.Modules.DSP.Filter.Enabled {
		if err := validateBandPassCutoffs(cfg.Modules.DSP.Filter.LowCutHz, cfg.Modules.DSP.Filter.HighCutHz, "modules.dsp.filter"); err != nil {
			return err
		}
	}
	if cfg.Modules.DSP.Dispersion != nil && cfg.Modules.DSP.Dispersion.Enabled {
		d := cfg.Modules.DSP.Dispersion
		sr := float64(cfg.Server.SampleRate)
		if sr <= 0 {
			return errors.New("server.sample_rate must be > 0 for modules.dsp.dispersion")
		}
		nyq := sr / dispersionNyquistDiv
		if d.Stages < 1 || d.Stages > dispersionMaxStages {
			return fmt.Errorf("modules.dsp.dispersion.stages must be in [1..%d]", dispersionMaxStages)
		}
		if math.IsNaN(d.CenterHz) || math.IsInf(d.CenterHz, 0) {
			return errors.New("modules.dsp.dispersion.center_hz must be finite")
		}
		if d.CenterHz < dispersionMinHz || d.CenterHz > nyq {
			return fmt.Errorf("modules.dsp.dispersion.center_hz must be in [%.g..%.g] Hz", dispersionMinHz, nyq)
		}
		if math.IsNaN(d.Resonance) || math.IsInf(d.Resonance, 0) || d.Resonance <= 0 {
			return errors.New("modules.dsp.dispersion.resonance must be > 0")
		}
		if math.IsNaN(d.SpreadOctaves) || math.IsInf(d.SpreadOctaves, 0) {
			return errors.New("modules.dsp.dispersion.spread_octaves must be finite")
		}
		if d.SpreadOctaves < -10 || d.SpreadOctaves > 10 {
			return errors.New("modules.dsp.dispersion.spread_octaves must be in [-10..10]")
		}
		style := strings.ToLower(strings.TrimSpace(d.SpreadStyle))
		if style != "" && style != "octaves" && style != "linear" {
			return errors.New("modules.dsp.dispersion.spread_style must be octaves or linear")
		}
	}
	return nil
}

func validateBandPassCutoffs(lowCutHz, highCutHz float64, path string) error {
	if math.IsNaN(lowCutHz) || math.IsInf(lowCutHz, 0) ||
		math.IsNaN(highCutHz) || math.IsInf(highCutHz, 0) {
		return fmt.Errorf("%s cutoff range must be finite", path)
	}
	if lowCutHz < 0 || highCutHz < 0 {
		return fmt.Errorf("%s cutoff range is invalid", path)
	}
	if lowCutHz > 0 && highCutHz > 0 &&
		highCutHz <= lowCutHz {
		return fmt.Errorf("%s cutoff range is invalid", path)
	}
	return nil
}
