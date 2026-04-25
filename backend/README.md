# O-Walkie Relay Server

## Start

```powershell
go run ./cmd/relay
```

Server startup flags are disabled. Configure all runtime values in `config.json`.

### Config (`backend/config.json`)

- `server.ws_addr` WebSocket control-plane address
- `server.udp_addr` UDP data-plane address
- `server.packet_ms` Opus frame duration in milliseconds (`10`, `20`, `40`, `60`; default `20`)
- `server.hangover_ms` keep TX alive briefly on packet gaps before considering stream ended
- `server.eof_timeout_ms` hard timeout for implicit EOF if no packets arrive
- `server.conceal_decay` frame-to-frame attenuation for concealment replay during hangover (`0..1`)
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

- `{"type":"welcome","sessionId":123,"channel":"global"}`
- `{"type":"joined","channel":"newChannel"}`
- `{"type":"pong"}`

Client -> server:

- `{"type":"join","channel":"global"}`
- `{"type":"switch_channel","channel":"teamA"}`
- `{"type":"udp_hello","udpPort":7001}`
- `{"type":"repeater_mode","enabled":true}`
- `{"type":"tx_eof"}` marks end-of-transmission explicitly (after PTT/roger tail)
- `{"type":"heartbeat"}`

## UDP Packet Format

Inbound client packet:

- `4 bytes` SessionID (`uint32`, big-endian)
- `4 bytes` Sequence (`uint32`, big-endian)
- `1 byte` SignalStrength (`0..255`)
- `N bytes` Opus frame payload

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
