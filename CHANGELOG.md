# Changelog

All notable changes to this project will be documented in this file.

This project follows a lightweight Keep a Changelog style and Semantic Versioning for public releases.

## [Unreleased]

### Added
- `owalkie_get_session_info` — thread-safe session snapshot (welcome/transport/runtime flags) without JSON parsing on clients.

### Changed
- Removed public `owalkie_json_build_*` from C API (join/udp_hello/repeater/heartbeat); wire JSON stays internal to session.
- Removed legacy Kotlin WebSocket/UDP relay, JNI Opus codec, and `BUILD_NATIVE_RELAY` flag — Android always uses owalkie-core JNI.
- Removed public `owalkie_session_*` pointer API from `owalkie_core.h`.
- Windows `AudioEngine`: PCM-only path (no local Opus); relay Opus stays in owalkie-core.
- owalkie-core: client-visible session events reduced to 10 types (`Ready` replaces Welcome/SessionReady/UDP-ready; connecting/connected/local-TX/UDP-lost are internal only).

### Fixed
- Android native relay: disconnect no longer blocks the service main thread (async session teardown in owalkie-core; disconnect on a background thread).
- Android: connection error tone no longer plays during intentional server profile switch (next/prev server while staying connected).

### Changed
- owalkie-core: global link signal API (`owalkie_report_signal` / `clear` / `get_uplink_signal_byte`); TX Opus no longer takes per-frame signal byte.
- owalkie-core: keepalive/recovery internalized; public `owalkie_punch_nat(session)` only; removed `notify_network_changed` and `reset_udp_transport` from managed API.
- owalkie-core: managed audio TX/RX uses PCM (`owalkie_tx_start` / `push_tx_pcm` / `tx_end`, `on_rx_pcm`); Opus encode/decode inside core.
- Android/Windows clients updated for simplified core API.

## [v0.1.5] - 2026-04-29

### Changed
- Android network recovery now reacts only to actual network switches and avoids unnecessary UDP socket rotation.

### Fixed
- Outgoing generated signal pacing (call/roger stream path) was stabilized with monotonic frame scheduling to prevent audible stream breakup on receiving clients.
- UDP recovery no longer interrupts active TX/call/roger signal streaming.

## [v0.1.4] - 2026-04-28

### Changed
- Relay server switched from external jitter library to an in-process jitter/reorder buffer implementation tuned for unstable networks.
- Backend dependencies were cleaned up to remove no-longer-used `pion/interceptor` jitter components.

### Fixed
- Critical relay freeze issue where the server process stayed alive but audio flow stalled under packet loss/reordering.
- End-of-transmission tail truncation reduced by allowing buffered packets to drain after explicit `tx_eof`.
- Expected WebSocket disconnects (`unexpected EOF`/close `1006`) are no longer logged as server errors.

## [v0.1.3] - 2026-04-27

### Added
- Server-side busy mode (`server.busy_mode`) with protocol announcement in `welcome.busyMode`.
- Server-side transmit timeout (`server.transmit_timeout`) with `tx_stop` signaling and hard UDP drop fallback.
- Android RX volume control slider above PTT with persistence and accessibility announcements.
- Android Bluetooth headset mode toggle (`Use Bluetooth headset`) with service-side route policy controls.

### Changed
- Default relay config tuned for stability:
  - `jitter_min_packets` increased to `4`
  - `transmit_timeout` default set to `60` seconds
  - noise squelch burst window adjusted (`10..20 ms`).
- Android hardware PTT binding now stores both `keyCode` and `scanCode` for better HID/custom key compatibility.
- Android communication audio profile now releases when TX-related activity is idle, except when headset policy intentionally holds route.

### Fixed
- Busy headset mode now auto-falls back to normal behavior when no active Bluetooth communication device is available.

## [v0.1.2] - 2026-04-26

### Changed
- Channel model switched to one-time initial bind on connect:
  - clients send channel immediately after `welcome`
  - runtime channel switching in active session is disabled (reconnect required)
- Relay startup log now includes `protocol_version`.

### Added
- Protocol compatibility handshake via `welcome.protocolVersion` (current version: `1`).
- Android and Windows clients now treat missing/mismatched protocol version as incompatible protocol and stop connection.
- Protocol compatibility notes added to backend README.

## [v0.1.1] - 2026-04-26

### Added
- Release process automation baseline:
  - release notes configuration (`.github/release.yml`)
  - tag-triggered release workflow (`.github/workflows/release-on-tag.yml`)
- Changelog template for future public releases.

## [v0.1.0] - 2026-04-26

### Added
- First public release published to GitHub.
- MIT license, bilingual root documentation and contribution templates.
- CI workflow for backend, Android and Windows prototype build validation.

### Changed
- Client profile logic no longer relies on hardcoded local relay targets.
- Android and Windows clients now support empty saved profile lists safely.
