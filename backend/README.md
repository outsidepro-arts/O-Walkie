# O-Walkie Relay Server

## Start

```powershell
go run ./cmd/relay -ws :8080 -udp :5000
```

Flags:

- `-ws` WebSocket control-plane address
- `-udp` UDP data-plane address
- `-loopback` send mixed audio back to source speaker too

## Control Plane (WebSocket `/ws`)

Server -> client:

- `{"type":"welcome","sessionId":123,"channel":"global"}`
- `{"type":"joined","channel":"newChannel"}`
- `{"type":"pong"}`

Client -> server:

- `{"type":"join","channel":"global"}`
- `{"type":"switch_channel","channel":"teamA"}`
- `{"type":"udp_hello","udpPort":7001}`
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
