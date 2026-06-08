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
- **Activity probe** (when `OWALKIE_CORE_HAS_SESSION`): `owalkie_check_channel_activity` — short-lived WebSocket `has_activity` query using `owalkie_connect_params` (no managed session).
- **Managed session** (when `OWALKIE_CORE_HAS_SESSION` is defined): `owalkie_prepare_connection`, `owalkie_connect(session_id)`, TX/RX PCM, `owalkie_get_session_info`, `owalkie_set_repeater_mode`, `owalkie_set_power_profile`, `owalkie_punch_nat`

`owalkie_prepare_connection` allocates a session id and registers callbacks (no network I/O). The client calls `owalkie_connect(session_id, timeout_ms)` for the first connect and every retry.

```c
owalkie_connect_params params = {
    .host = "relay.example.com",
    .port = 5500,
    .channel = "team-a",
    .use_tls = 0,
    .repeater_mode = 0,
};
int active = 0;
if (owalkie_check_channel_activity(&params, 4000, &active) == OWALKIE_OK && active) {
    /* recent traffic on channel */
}
```

Callbacks:

- `on_rx_pcm` — decoded PCM from relay (Opus decode is inside core)
- `on_session_event` — public `owalkie_event_type` values only (connecting, welcome, UDP hello/ready/lost, local TX, etc. stay internal):
  - `OWALKIE_EV_CONNECTED`, `DISCONNECTED`, `PROTOCOL_ERROR`, `CONNECTION_FAILED`, `CONNECTION_LOST`
  - `RX_BROADCAST_START` / `RX_BROADCAST_END`, `PTT_LOCKED` / `PTT_UNLOCKED`
  - `TX_COUNTDOWN_START`, `TX_STOP`

Lifecycle: `owalkie_disconnect`, `owalkie_disconnect_all`, `owalkie_disconnect_all_and_wait` (app exit), `owalkie_session_id_valid`, `owalkie_session_id_ready`, `owalkie_connect` (single teardown+connect attempt per call).

### UDP keepalive

Keepalive, recovery, WS heartbeat, and NAT punch run inside the session until `owalkie_disconnect()`. **Transport reconnect is client-owned**: while the user intent remains connected, the app calls `owalkie_connect(session_id, timeout_ms)` on a timer (with backoff). Core emits `OWALKIE_EV_CONNECTION_LOST` when the link drops; `OWALKIE_EV_DISCONNECTED` is reserved for explicit user teardown (or fatal config/protocol errors). Optional `owalkie_punch_nat(session_id)` remains for manual NAT punch.

On Android, gate reconnect attempts locally with `NetworkCallback` / `NET_CAPABILITY_VALIDATED` (do not call into core for reachability).

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
owalkie_tx_submit(session_id, OWALKIE_TX_OPEN, NULL, 0, NULL, 0);
owalkie_tx_submit(session_id, OWALKIE_TX_PCM, pcm_samples, sample_count, NULL, 0);
owalkie_tx_submit(session_id, OWALKIE_TX_CLOSE, NULL, 0, NULL, 0);  // flush + UDP EOF burst
```

After `OWALKIE_EV_CONNECTED`, prefer `owalkie_get_session_info` for welcome/transport flags (Android JNI uses this; no JSON round-trip). The connected event may still carry `u.welcome.config`, but string pointers are only valid for the callback.

Roger/Call tones are generated client-side via `owalkie_signal_generate_pcm`; the library does not send them automatically.

## Protocol

Current `protocolVersion` is **2** (`OWALKIE_PROTOCOL_VERSION`).

TLS is not implemented: `owalkie_connect` with `use_tls != 0` or a `wss://` host fails during connect (`Result::Unsupported` / invalid session id).

## Integrations

- **Android** — always native relay via `libowalkie_jni.so` → owalkie-core; see [`android/README-ndk.md`](../android/README-ndk.md).
- **Windows** — `windows-client-cpp` uses managed sessions + PCM `AudioEngine` / `RelayClient`.

## Status

Library version string: **0.1.0** (`owalkie_version_string`). Repo changelog may be ahead of the embedded version until the next core release tag.
