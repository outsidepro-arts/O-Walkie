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
- `server.transmit_timeout` max continuous TX duration in seconds (`0` disables timeout)
- each module block in `modules.*` is optional:
  - if the block is missing, module is disabled
  - if the block exists, `enabled: true|false` controls activation
- `modules.noise.signal_dependent` controls whether noise level follows signal strength (`true`) or stays fixed at `min_noise_db` (`false`)
- `modules.noise.noise_distribution` `gaussian` (default) or `uniform` (legacy per-sample [-1,1])
- `modules.noise.thermal_lowpass_hz` moving-average lowpass (~sample_rate/window); `0` disables; default `8000` matches coarse thermal-noise proxy (see `examples/radio_noise/B_thermal_lp_8kHz.wav` at 48 kHz)
- `modules.noise.*` squelch / tail / idle-shot behavior (unchanged)
- `modules.noise.squelch_shots_min_s` idle-shot timer minimum in seconds (`> 0` when enabled)
- `modules.noise.squelch_shots_max_s` idle-shot timer maximum in seconds (`0` disables shots; when enabled must be `>= squelch_shots_min_s`)
- when no TX is active, next shot is scheduled in random `squelch_shots_min_s..squelch_shots_max_s` seconds; shot duration is random `tail_min_ms..tail_max_ms`
- each idle shot includes `1s` silence before and `1s` silence after (for reliable short-shot playback on clients)
- `modules.click.pops.*` sinusoidal PTT start/end and in-TX glitch pops (`click_db`, `glitch_*`; legacy flat `click_db` / `glitch_*` still work if `pops` is omitted)
- `modules.click.impulses` optional sparse RF-style impulses: `enabled`, `prob_at_weak_signal` / `prob_at_strong_signal` (per-frame probability, same signal-strength mapping as noise: weak line → higher rate), `gain_db`
- `modules.filter.low_cut_hz` / `modules.filter.high_cut_hz` band-pass cutoff range for full channel stream
- `modules.compressor.*` compressor settings
- `modules.distortion.*` distortion settings

## Control Plane (WebSocket `/ws`)

Server -> client:

- `{"type":"welcome","sessionId":123,"packetMs":20,"sampleRate":8000,"opus":{"bitrate":12000,"complexity":5,"fec":true,"dtx":false,"application":"voip"},"protocolVersion":2}`
  - includes `busyMode: true|false` so clients can lock PTT while channel is receiving
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
