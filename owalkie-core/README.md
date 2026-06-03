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

- **Utilities** (always available): protocol normalize, JSON parse/build, signal PCM, UDP pack/unpack
- **Session** (when `OWALKIE_CORE_HAS_SESSION` is defined): connect, TX/RX PCM, repeater mode, UDP reset

Callbacks:

- `on_rx_pcm` — decoded PCM frames from relay
- `on_session_event` — single stream for connection, welcome, RX broadcast, PTT lock/unlock, TX stop, UDP transport

### UDP keepalive profiles

Match Android `WalkieService` cadence:

| Profile | Idle | Recovery | RTX | Lost |
|---------|------|----------|-----|------|
| `OWALKIE_POWER_FOREGROUND` | 12 s | 6 s | 1 s | 8 s |
| `OWALKIE_POWER_BACKGROUND` | 50 s | 12 s | 2 s | 10 s |
| `OWALKIE_POWER_ACTIVE_TX` | same as fg, idle paused while local TX active | | | |

Hints (call from client lifecycle):

- `owalkie_session_set_power_profile` — fg/bg on resume/pause
- `owalkie_session_enter_udp_recovery` — after connect/reconnect (also auto on welcome)
- `owalkie_session_notify_network_changed` — recreate UDP + recovery window (deferred during TX)
- `owalkie_session_punch_udp_nat` — one-shot keepalive punch

### Uplink signal strength

UDP byte 8 on each outgoing audio frame. Default: **255** (unchanged legacy behavior).

```c
owalkie_session_set_tx_signal_strength(session, currentSignalByte);  // 0..255, except 254
owalkie_session_get_tx_signal_strength(session);
```

- **254** is rejected (`OWALKIE_ERR_INVALID_ARG`) — reserved for keepalive ack
- **255** is allowed on audio frames (max signal); keepalive uses the same byte value but on `seq=0` packets without opus
- Client updates on the same schedule as Android `currentSignalByte()` (WiFi/cellular poll); applies to PTT, Roger, and Call via `feed_tx_pcm`

Roger/Call tones are generated client-side via `owalkie_signal_generate_pcm`; the library does not send them automatically.

## Protocol

Current `protocolVersion` is **2** (`OWALKIE_PROTOCOL_VERSION`).

TLS (`use_tls`) is not implemented yet; `owalkie_session_connect` returns `OWALKIE_ERR_UNSUPPORTED`.

## Status

v0.1.0 — initial utilities + session port from `windows-client-cpp/src/RelayClient.cpp`. Android JNI and Windows client integration are planned next.
