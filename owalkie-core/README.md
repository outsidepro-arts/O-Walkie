# owalkie-core

Shared C/C++ library for O-Walkie relay protocol: JSON helpers, UDP framing, Roger/Call PCM synthesis, and (optional) WebSocket/UDP session transport with Opus.

## Build (MSYS2 UCRT64)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,boost,opus,nlohmann-json,pkgconf}
cd owalkie-core
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:

- `OWALKIE_CORE_BUILD_SESSION=ON` (default) — Boost.Asio + libopus session API
- `OWALKIE_CORE_BUILD_TESTS=ON` (default) — unit tests

## C API

Public header: `include/owalkie_core.h`

- **Utilities** (always available): protocol normalize, JSON parse (welcome/server messages), signal PCM, UDP pack/unpack. Outbound WS wire messages (`join`, `udp_hello`, …) are built inside the session only.
- **Managed session** (when `OWALKIE_CORE_HAS_SESSION` is defined): `owalkie_connect`, TX/RX PCM, `owalkie_get_session_info`, repeater mode, `punch_nat`

Callbacks:

- `on_rx_pcm` — decoded PCM frames from relay
- `on_session_event` — `Ready`, `Disconnected`, `ConnectFailed`, `ProtocolError`, RX broadcast start/end, PTT lock/unlock, TX countdown/stop (transport steps are internal)

### UDP keepalive

Keepalive, recovery, and NAT punch timing run **inside** the session implementation. Clients only set power profile and may call `owalkie_punch_nat(session_id)` for an optional one-shot punch.

| Profile | Idle | Recovery | RTX | Lost |
|---------|------|----------|-----|------|
| `OWALKIE_POWER_FOREGROUND` | 12 s | 6 s | 1 s | 8 s |
| `OWALKIE_POWER_BACKGROUND` | 50 s | 12 s | 2 s | 10 s |
| `OWALKIE_POWER_ACTIVE_TX` | same as fg, idle paused while local TX active | | | |

- `owalkie_set_power_profile(session_id, profile)` — foreground/background/active TX

### Uplink signal strength (global)

UDP byte 8 on outgoing audio frames comes from the global link-signal registry (all sessions share the same value).

```c
owalkie_report_signal(OWALKIE_SIGNAL_WIFI, rssi_dbm);   // e.g. -65
owalkie_report_signal(OWALKIE_SIGNAL_CELL, level_0_4);  // 0..4
owalkie_clear_signal(OWALKIE_SIGNAL_WIFI);              // channel unavailable
int byte = owalkie_get_uplink_signal_byte();            // max(wifi, cell), default 255
```

```c
owalkie_tx_start(session_id);
owalkie_push_tx_pcm(session_id, pcm_samples, sample_count);
owalkie_tx_end(session_id);  // UDP EOF burst + local TX end
```

Uplink signal byte comes from `owalkie_report_signal` / `owalkie_get_uplink_signal_byte()`.

Managed RX delivers decoded PCM via `on_rx_pcm` (not Opus).

After `OWALKIE_EV_READY`, read negotiated parameters with `owalkie_get_session_info` (no JSON round-trip).

Roger/Call tones are generated client-side via `owalkie_signal_generate_pcm`; the library does not send them automatically.

## Protocol

Current `protocolVersion` is **2** (`OWALKIE_PROTOCOL_VERSION`).

TLS (`use_tls`) is not implemented yet; `owalkie_session_connect` returns `OWALKIE_ERR_UNSUPPORTED`.

## Status

v0.1.0 — initial utilities + session port from `windows-client-cpp/src/RelayClient.cpp`. Android JNI and Windows client integration are planned next.
