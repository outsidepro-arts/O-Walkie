# O-Walkie Relay Server

## Start

```powershell
go run ./cmd/relay
```

By default server loads `./config.json`. You can pass another config path as the first argument:

```powershell
go run ./cmd/relay ./config.json
```

### Config (`backend/config.json`)

- `server.ws_addr` WebSocket control-plane address
- `server.udp_addr` UDP data-plane address
- `server.sample_rate` Opus sample rate (`8000`, `12000`, `16000`, `24000`, `48000`; default `8000`)
- `server.packet_ms` Opus frame duration in milliseconds (`10`, `20`, `40`, `60`; default `20`)
- `server.opus.bitrate` Opus target bitrate in bits/s (`6000..510000`; default `12000`)
- `server.opus.complexity` Opus encoder complexity (`0..10`; default `5`)
- `server.opus.fec` Opus in-band FEC (`true|false`; default `true`)
- `server.opus.dtx` Opus DTX (`true|false`; default `false`)
- `server.opus.application` Opus application mode (`voip`, `audio`, `lowdelay`; default `voip`)
- `server.hangover_ms` keep TX alive briefly on packet gaps before considering stream ended
- `server.eof_timeout_ms` hard timeout for implicit EOF if no packets arrive
- `server.conceal_decay` frame-to-frame attenuation for concealment replay during hangover (`0..1`)
- `server.jitter_min_packets` minimum packets to accumulate before server-side jitter playout (`1..12`, recommended `2..4`)
- `server.busy_mode` allow only one active transmitter per channel at a time (others are blocked until TX ends)
- `server.busy_timeout` busy-mode parallel unlock delay in seconds (`0` disables unlock and keeps strict single-TX behavior)
- `server.transmit_timeout` max continuous TX duration in seconds (`0` disables timeout)
- each module block in `modules.*` is optional:
  - if the block is missing, module is disabled
  - if the block exists, `enabled: true|false` controls activation
- `modules.dsp.noise.signal_dependent` controls whether noise level follows signal strength (`true`) or stays fixed at `min_noise_db` (`false`)
- `modules.dsp.noise.noise_distribution` `gaussian` (default) or `uniform` (legacy per-sample [-1,1])
- `modules.dsp.noise.thermal_lowpass_hz` moving-average lowpass (~sample_rate/window); `0` disables; default `8000` matches coarse thermal-noise proxy (see `examples/radio_noise/B_thermal_lp_8kHz.wav` at 48 kHz)
- `modules.dsp.squelch.*` handles weak-signal gating and TX-end tail (`threshold_percent`, `squelch_min_ms`, `squelch_max_ms`, `tail_noise_db`, `tail_*` ms, `noise_gain`)
- `modules.dsp.squelch.noise_distribution` / `thermal_lowpass_hz` match the **noise generator** used in `modules.dsp.noise` (weak-signal burst and EOF tail both use `tail_noise_db` for level)
- `modules.dsp.squelch.edge_impulse_db` optional: peak dB for a single-sample ± impulse at hiss **start** and **end** (scaled by `noise_gain` like tail hiss); omit to keep smooth noise edges; on weak-signal bursts the closing impulse is placed on the last **emitted** frame (the final frame is still dropped by the gate)
- `modules.dsp.pops.*` sinusoidal PTT start/end and in-TX glitch pops (`click_db`, `click_tone_hz`, `glitch_*`)
- `modules.dsp.clicks` sparse RF-style impulses (when `enabled`): `impulse_prob_at_weak_signal` / `impulse_prob_at_strong_signal`; `impulse_gain_db` is the default loudness at both signal endpoints, with optional `impulse_gain_at_weak_signal_db` / `impulse_gain_at_strong_signal_db` for the same 10%–100% signal curve as probability; optional nested `filter` (`enabled`, `low_cut_hz`, `high_cut_hz`) applies the same band-pass as `modules.dsp.filter` only to the impulse path; `multi_client_rapid_ms` accelerates clicks while multiple TX are active
- `modules.dsp.filter.low_cut_hz` / `modules.dsp.filter.high_cut_hz` band-pass cutoff range for full channel stream
- `modules.dsp.chain` ordered list of DSP plugin names (`pops`, `clicks`, `noise`, `squelch`, `filter`, `dispersion`, `compressor`, `distortion`); add `"dispersion"` where you want phase-dispersion in the chain
- `modules.dsp.dispersion.*` cascaded allpass (flat magnitude, frequency-dependent phase): `stages` (1..512), `center_hz`, `resonance` (Q, >0), `spread_octaves`, `spread_style` `octaves`|`linear`; pure signal path, ignores TX/control events
- `modules.dsp.compressor.*` compressor settings
- `modules.dsp.distortion.*` distortion settings

## Control Plane (WebSocket `/ws`)

Server -> client:

- `{"type":"welcome","sessionId":123,"packetMs":20,"sampleRate":8000,"opus":{"bitrate":12000,"complexity":5,"fec":true,"dtx":false,"application":"voip"},"protocolVersion":2}`
  - includes `busyMode: true|false` and `busyTimeoutSec` so clients can lock PTT and display unlock countdown
- `{"type":"busy_timeout_elapsed"}` busy-mode timeout elapsed; parallel TX may start now
- `{"type":"tx_stop","info":"transmit_timeout_reached"}` server-enforced stop for overlong TX
- `{"type":"joined","channel":"teamA"}`
- `{"type":"pong"}`

Client -> server:

- first client message after `welcome` must bind channel:
  - `{"type":"join","channel":"teamA"}`
- runtime channel switch is disabled; reconnect with new channel instead
- `{"type":"udp_hello","udpPort":7001}`
- `{"type":"repeater_mode","enabled":true}`
- `{"type":"heartbeat"}`

If the first client message does not contain a valid channel bind, server replies with `error` and closes the WebSocket session.

Protocol compatibility:
- current protocol version is `2`
- clients must validate `welcome.protocolVersion`
- missing or mismatched protocol version must be treated as incompatible protocol

## UDP Packet Format

Inbound client packet:

- `4 bytes` SessionID (`uint32`, big-endian)
- `4 bytes` Sequence (`uint32`, big-endian)
- `1 byte` SignalStrength (`0..255`)
- `N bytes` Opus frame payload

UDP EOF marker (preferred over WS for low-latency sync):

- same 9-byte header, with **empty Opus payload**
- `SignalStrength = 0`
- `Sequence > 0`
- client sends marker burst (`~3 packets`) to improve delivery probability on lossy links

Outbound mixed packet:

- Same header format
- `SessionID` is `0` (mixed stream)
- `SignalStrength` represents mixed output clarity estimate

## Build Notes (Windows)

`github.com/hraban/opus` requires CGO and `gcc` toolchain in `PATH`.

Example (PowerShell session):

```powershell
$env:CGO_ENABLED = "1"
go build ./...
```
