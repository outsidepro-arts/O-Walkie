# O-Walkie Relay Server

## Start

```powershell
go run ./cmd/relay
```

Server startup flags are disabled. Configure all runtime values in `config.json`.

### Config (`backend/config.json`)

- `server.ws_addr` WebSocket control-plane address
- `server.udp_addr` UDP data-plane address
- `server.sample_rate` Opus sample rate (`8000`, `12000`, `16000`, `24000`, `48000`; default `8000`)
- `server.packet_ms` Opus frame duration in milliseconds (`10`, `20`, `40`, `60`; default `20`)
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
- `modules.noise.*` white-noise + squelch behavior
- `modules.click.click_db` click level at transmission start/end
- `modules.click.glitch_interval_max_ms` random in-TX click interval ceiling (`0` disables)
- `modules.click.glitch_freq_min_hz` / `modules.click.glitch_freq_max_hz` random in-TX click frequency range
- `modules.click.glitch_level_min_db` / `modules.click.glitch_level_max_db` random in-TX click loudness range
- `modules.filter.low_cut_hz` / `modules.filter.high_cut_hz` band-pass cutoff range for full channel stream
- `modules.compressor.*` compressor settings
- `modules.distortion.*` distortion settings

## Control Plane (WebSocket `/ws`)

Server -> client:

- `{"type":"welcome","sessionId":123,"packetMs":20,"sampleRate":8000,"protocolVersion":1}`
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
