# Changelog

All notable changes to this project will be documented in this file.

This project follows a lightweight Keep a Changelog style and Semantic Versioning for public releases.

## [Unreleased]

### Added
- _(add new features here)_

### Changed
- _(add behavior/config changes here)_

### Fixed
- _(add bug fixes here)_

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
