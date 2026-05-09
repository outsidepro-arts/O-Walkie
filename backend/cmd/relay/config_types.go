package main

type appConfig struct {
	Server  serverConfig  `json:"server"`
	Modules modulesConfig `json:"modules"`
}

type serverConfig struct {
	// Port is used for both WebSocket HTTP listener and UDP listener (same port number).
	Port int `json:"port"`
	// Legacy keys (optional): if port is 0, port is inferred from the first non-empty address.
	LegacyWsAddr  string     `json:"ws_addr,omitempty"`
	LegacyUdpAddr string     `json:"udp_addr,omitempty"`
	SampleRate    int        `json:"sample_rate"`
	PacketMs      int        `json:"packet_ms"`
	Opus          opusConfig `json:"opus"`
	HangoverMs    int        `json:"hangover_ms"`
	EOFTimeoutMs  int        `json:"eof_timeout_ms"`
	ConcealDecay  float64    `json:"conceal_decay"`
	JitterMinPkts int        `json:"jitter_min_packets"`
	// JitterAdaptEnabled: when true, each transmitter's jitter depth adapts between jitter_min_packets and jitter_max_packets.
	JitterAdaptEnabled bool `json:"jitter_adapt_enabled"`
	// JitterMaxPkts: ceiling for adaptive depth (ignored when jitter_adapt_enabled is false). If 0 when enabled, defaults to max(jitter_min_packets, 12).
	JitterMaxPkts int  `json:"jitter_max_packets,omitempty"`
	BusyMode      bool `json:"busy_mode"`
	// BusyTimeoutSec: when >0 and busy_mode enabled, parallel TX is unlocked after this many seconds.
	BusyTimeoutSec     int `json:"busy_timeout,omitempty"`
	TransmitTimeoutSec int `json:"transmit_timeout"`
}

type opusConfig struct {
	Bitrate     int    `json:"bitrate"`
	Complexity  int    `json:"complexity"`
	FEC         bool   `json:"fec"`
	DTX         bool   `json:"dtx"`
	Application string `json:"application"`
}

type modulesConfig struct {
	Generators generatorsModulesConfig `json:"generators"`
	DSP        dspModulesConfig        `json:"dsp"`
	// Legacy flat layout (auto-migrated into generators/dsp during load).
	Noise      *legacyNoiseConfig `json:"noise,omitempty"`
	Click      *legacyClickConfig `json:"click,omitempty"`
	Filter     *filterConfig      `json:"filter,omitempty"`
	Compressor *compressorConfig  `json:"compressor,omitempty"`
	Distortion *distortionConfig  `json:"distortion,omitempty"`
}

type generatorsModulesConfig struct {
	// Reserved for non-DSP frame initiators.
	SquelchShots *squelchShotsGeneratorConfig `json:"squelch_shots,omitempty"`
}

type dspModulesConfig struct {
	Chain      []string          `json:"chain,omitempty"`
	Clicks     *clicksConfig     `json:"clicks,omitempty"`
	Pops       *popsConfig       `json:"pops,omitempty"`
	Noise      *noiseDSPConfig   `json:"noise,omitempty"`
	Squelch    *squelchDSPConfig `json:"squelch,omitempty"`
	Filter     *filterConfig     `json:"filter,omitempty"`
	Dispersion *dispersionConfig `json:"dispersion,omitempty"`
	Compressor *compressorConfig `json:"compressor,omitempty"`
	Distortion *distortionConfig `json:"distortion,omitempty"`
}

type noiseDSPConfig struct {
	Enabled         bool    `json:"enabled"`
	SignalDependent bool    `json:"signal_dependent"`
	MinNoiseDB      float64 `json:"min_noise_db"`
	MaxNoiseDB      float64 `json:"max_noise_db"`
	NoiseGain       float64 `json:"noise_gain"`
	// NoiseDistribution: "gaussian" (default) or "uniform" (legacy per-sample [-1,1]).
	NoiseDistribution string `json:"noise_distribution,omitempty"`
	// ThermalLowpassHz: moving-average lowpass (~SR/window); 0 disables.
	ThermalLowpassHz float64 `json:"thermal_lowpass_hz,omitempty"`
}

type squelchDSPConfig struct {
	Enabled bool `json:"enabled"`
	// Signal threshold percentage (0..100) below which squelch burst is emitted then gated.
	ThresholdPercent float64 `json:"threshold_percent"`
	SquelchMinMs     int     `json:"squelch_min_ms"`
	SquelchMaxMs     int     `json:"squelch_max_ms"`
	// Squelch hiss uses the same white-noise generator shape as modules.dsp.noise (not its level mapping).
	// NoiseDistribution: "gaussian" (default) or "uniform" (legacy per-sample [-1,1]).
	NoiseDistribution string `json:"noise_distribution,omitempty"`
	// ThermalLowpassHz: moving-average lowpass (~SR/window); 0 disables.
	ThermalLowpassHz float64 `json:"thermal_lowpass_hz,omitempty"`
	NoiseGain        float64 `json:"noise_gain"`
	TailNoiseDB      float64 `json:"tail_noise_db"`
	TailMinMs        int     `json:"tail_min_ms"`
	TailMaxMs        int     `json:"tail_max_ms"`
	// EdgeImpulseDB: optional dB peak (scaled by noise_gain like tail hiss) for a single-sample
	// ± impulse at hiss burst start/end; nil = disabled (legacy smooth noise edges).
	EdgeImpulseDB *float64 `json:"edge_impulse_db,omitempty"`
	// SynthSilenceTailPackets: zero-PCM frames after each squelch-generated burst tail.
	// 0 = default jitter_min_packets+2 (min 4).
	SynthSilenceTailPackets int `json:"synth_silence_tail_packets,omitempty"`
}

type squelchShotsGeneratorConfig struct {
	Enabled bool `json:"enabled"`
	// Random timer range before a shot is started.
	IntervalMinMs int `json:"interval_min_ms"`
	IntervalMaxMs int `json:"interval_max_ms"`
	// Random duration of "silent transmission" after the start tick.
	SilenceMinMs int `json:"silence_min_ms"`
	SilenceMaxMs int `json:"silence_max_ms"`
	// Random signal level range used for generated transmission (0..100).
	SignalMinPercent float64 `json:"signal_min_percent"`
	SignalMaxPercent float64 `json:"signal_max_percent"`
}

type compressorConfig struct {
	Enabled     bool    `json:"enabled"`
	ThresholdDB float64 `json:"threshold_db"`
	Ratio       float64 `json:"ratio"`
	AttackMs    float64 `json:"attack_ms"`
	ReleaseMs   float64 `json:"release_ms"`
	MakeupDB    float64 `json:"makeup_db"`
}

// legacyClickImpulses: only for migrating legacy modules.click JSON (nested impulses block).
type legacyClickImpulses struct {
	Enabled            bool    `json:"enabled"`
	ProbAtWeakSignal   float64 `json:"prob_at_weak_signal"`
	ProbAtStrongSignal float64 `json:"prob_at_strong_signal"`
	GainDB             float64 `json:"gain_db"`
}

type clicksConfig struct {
	Enabled            bool `json:"enabled"`
	MultiClientRapidMs int  `json:"multi_client_rapid_ms,omitempty"`
	// RF-style sparse impulses (flat under modules.dsp.clicks); on/off is clicks.enabled only.
	ImpulseProbAtWeak   float64 `json:"impulse_prob_at_weak_signal,omitempty"`
	ImpulseProbAtStrong float64 `json:"impulse_prob_at_strong_signal,omitempty"`
	// ImpulseGainDB is the default for both weak/strong when the optional *DB pointers are omitted.
	ImpulseGainDB float64 `json:"impulse_gain_db,omitempty"`
	// Optional per-endpoint gain (dB); when nil, ImpulseGainDB is used for that endpoint.
	ImpulseGainAtWeakDB   *float64 `json:"impulse_gain_at_weak_signal_db,omitempty"`
	ImpulseGainAtStrongDB *float64 `json:"impulse_gain_at_strong_signal_db,omitempty"`
	// Optional band-pass on the impulse path only (same fields/semantics as modules.dsp.filter).
	Filter *filterConfig `json:"filter,omitempty"`
}

// popsConfig: sinusoidal PTT / in-TX glitch pops (flat under modules.dsp.pops).
type popsConfig struct {
	Enabled             bool    `json:"enabled"`
	ClickDB             float64 `json:"click_db,omitempty"`
	ClickToneHz         float64 `json:"click_tone_hz,omitempty"`
	GlitchIntervalMaxMs int     `json:"glitch_interval_max_ms,omitempty"`
	GlitchFreqMinHz     float64 `json:"glitch_freq_min_hz,omitempty"`
	GlitchFreqMaxHz     float64 `json:"glitch_freq_max_hz,omitempty"`
	GlitchLevelMinDB    float64 `json:"glitch_level_min_db,omitempty"`
	GlitchLevelMaxDB    float64 `json:"glitch_level_max_db,omitempty"`
}

type legacyNoiseConfig struct {
	Enabled                 bool    `json:"enabled"`
	SignalDependent         bool    `json:"signal_dependent"`
	MinNoiseDB              float64 `json:"min_noise_db"`
	MaxNoiseDB              float64 `json:"max_noise_db"`
	NoiseGain               float64 `json:"noise_gain"`
	SquelchThresholdDB      float64 `json:"squelch_threshold_db"`
	SquelchMinMs            int     `json:"squelch_min_ms"`
	SquelchMaxMs            int     `json:"squelch_max_ms"`
	TailNoiseDB             float64 `json:"tail_noise_db"`
	TailMinMs               int     `json:"tail_min_ms"`
	TailMaxMs               int     `json:"tail_max_ms"`
	NoiseDistribution       string  `json:"noise_distribution,omitempty"`
	ThermalLowpassHz        float64 `json:"thermal_lowpass_hz,omitempty"`
	SynthSilenceTailPackets int     `json:"synth_silence_tail_packets,omitempty"`
}

type legacyClickConfig struct {
	Enabled             bool                 `json:"enabled"`
	ClickDB             float64              `json:"click_db,omitempty"`
	ClickToneHz         float64              `json:"click_tone_hz,omitempty"`
	GlitchIntervalMaxMs int                  `json:"glitch_interval_max_ms,omitempty"`
	GlitchFreqMinHz     float64              `json:"glitch_freq_min_hz,omitempty"`
	GlitchFreqMaxHz     float64              `json:"glitch_freq_max_hz,omitempty"`
	GlitchLevelMinDB    float64              `json:"glitch_level_min_db,omitempty"`
	GlitchLevelMaxDB    float64              `json:"glitch_level_max_db,omitempty"`
	Impulses            *legacyClickImpulses `json:"impulses,omitempty"`
}

type filterConfig struct {
	Enabled   bool    `json:"enabled"`
	LowCutHz  float64 `json:"low_cut_hz"`
	HighCutHz float64 `json:"high_cut_hz"`
}

type distortionConfig struct {
	Enabled               bool    `json:"enabled"`
	Drive                 float64 `json:"drive"`
	Mix                   float64 `json:"mix"`
	MultiClientDriveBoost float64 `json:"multi_client_drive_boost,omitempty"`
	MultiClientMixBoost   float64 `json:"multi_client_mix_boost,omitempty"`
}
