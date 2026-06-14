# O-Walkie — Project Memory

Last updated: 2026-06-14

## Branches

| Branch | Status | Description |
|--------|--------|-------------|
| `master` | stable | Production clients (Android Kotlin, Windows C++). Latest tag: `v0.1.6`. |
| `feature/flutter-client` | active | Experimental Flutter client (phases 0–9 done). Current working branch. |
| `beka_build_fixes` | stale | Build fixes branch. |

**Current work is on `feature/flutter-client`.** Master has 35 commits ahead of `v0.1.6` (owalkie-core, SessionManager, TX pipeline, channel scan, system tray, warm mic).

## Current Stage

- **Master**: owalkie-core extracted as shared C/C++ relay client library (v0.1.0); Android and Windows migrated to SessionManager + TX submit pipeline (`OPEN`/`PCM`/`CLOSE`); channel scan via `owalkie_check_channel_activity`; Windows system tray; Android warm mic recorder + scan toggle modes.
- **Flutter branch**: Experimental cross-platform client through phases 0–9 (protocol UX, profiles, Roger/Call, FGS, deep links, scan, iOS scaffold, Windows tray/hotkey, release polish). Active uncommitted changes in `flutter-client/` (a11y chips, profile reorder, vibration imitation, warm mic recorder store).
- Late stabilization before tester rollout on master.
- Accessibility-based background PTT removed from `master` (Play Protect friction).

## Architecture

### Components

| Directory | Language / Stack | Description |
|-----------|-----------------|-------------|
| `backend/` | Go (`cmd/relay/`) | Relay server: WebSocket control + UDP audio plane |
| `owalkie-core/` | C/C++ | Shared relay client library: SessionManager, Opus, activity probe, Roger/Call PCM, TX submit pipeline |
| `android/` | Kotlin | Android client (release): foreground `WalkieService`, JNI → owalkie-core |
| `windows-client-cpp/` | C++ (wxWidgets + miniaudio) | Windows desktop client (release): owalkie-core managed session |
| `flutter-client/` | Dart/Flutter | Experimental cross-platform client (FFI → owalkie-core). Android, Windows, iOS scaffold |

### owalkie-core (v0.1.0)

Shared C/C++ library used by all three clients. Public header: `include/owalkie_core.h`.

- **SessionManager**: multi-session lifecycle, async disconnect, `owalkie_disconnect_all_and_wait`.
- **TX submit pipeline**: `owalkie_tx_submit(session_id, type, pcm, count, ...)` — types: `OPEN`, `PCM`, `CLOSE` (flush + UDP EOF burst). Ordered submit queue in core.
- **Activity probe**: `owalkie_check_channel_activity` — one-shot WebSocket `has_activity` query (no managed session).
- **Power profiles**: `FOREGROUND` / `BACKGROUND` / `ACTIVE_TX` — controls keepalive intervals.
- **Link signal**: global registry (`owalkie_report_signal`, `owalkie_get_uplink_signal_byte`).
- **Events**: `CONNECTED`, `DISCONNECTED`, `PROTOCOL_ERROR`, `CONNECTION_FAILED`, `CONNECTION_LOST`, `RX_BROADCAST_START/END`, `PTT_LOCKED/UNLOCKED`, `TX_COUNTDOWN_START`, `TX_STOP`.
- **Reconnect**: client-owned; core emits `CONNECTION_LOST`, client calls `owalkie_connect(session_id, timeout_ms)` on timer with backoff.
- Build: MSYS2 UCRT64, CMake + Ninja. Options: `OWALKIE_CORE_BUILD_SESSION=ON`, `OWALKIE_CORE_BUILD_TESTS=ON`.

## Modules Ready

### Relay (Backend)
- WebSocket control + UDP audio routing/mixing.
- Opus negotiation: sample rate + encoder profile in welcome handshake.
- Server DSP chain: noise/squelch, clicks, pops, filter, compressor, distortion, dispersion.
- Configurable DSP chain order (`modules.dsp.chain`); ERROR + full disable if missing.
- Duplicate module names allowed in chain.
- Signal-dependent click gain + optional click band-pass filter.
- Squelch hiss edge impulse (`edge_impulse_db`).
- `server.busy_timeout` (seconds, `0` = strict single-TX); client-side PTT unlock via `ptt_unlock`.
- `tx_countdown_start`: 5s warning before server `tx_stop`.
- Adaptive jitter depth on uplink (`jitter_adapt_enabled`).
- UDP keepalive ACK (`signal=254, seq=0`); unicast resend on keepalive punch.
- Mixer crash recovery: panic restart, lifetime counter, ALERT on ≥5 restarts/min.
- Server-side Opus params configurable (bitrate/complexity/FEC/DTX/application).
- Single port for WS+UDP (`server.port`, default 5500).

### Android (Kotlin, master)
- SessionManager + async `disconnect()` — fixes UI freeze.
- TX submit pipeline (`OPEN`/`PCM`/`CLOSE`) via JNI.
- Channel scan via `owalkie_check_channel_activity` (JNI).
- Scan toggle modes (one-shot, continuous) with smarter channel switching.
- Warm mic recorder: optional infinite recorder keep when main activity is foreground (`WarmMicRecorderStore`).
- PTT TalkBack improvements: busy lock and countdown announcements.
- `RxPcmJitterBuffer` — fixes choppy RX playback in background sessions.
- Media button PTT (toggle mode only, `PttMediaSessionController`).
- External Control API (`ExternalControlReceiver`, `ExternalControlStore`).
- Deep-link import triggers immediate reconnect when session active.
- Phone call relay pause (`PhoneCallRelayPauseStore`).
- Connection tones: connect start sequence, disconnect reverse, connected `{1400Hz, 1700Hz}`, error tone.
- Background battery: softer UDP keepalive, longer RX timeout, `Network` binding after handoffs.

### Windows (`windows-client-cpp`, master)
- System tray background working (minimize to tray, tray menu).
- TX submit pipeline via owalkie-core SessionManager.
- WS transport stabilization and reconnect behavior.
- GUI subsystem (`WIN32`), no console window.
- Inno Setup installer (RU/EN, deep-link registration).
- Portable fallback: `config/` folder next to exe.
- `config.json` primary (legacy `audio.json` fallback read).
- Auto-reconnect: detached worker thread, ticketed sleeper, exponential backoff.
- `wxStatusBar` + `HumanizeStatus` mapper.
- Global PTT: side-aware modifiers, `WH_KEYBOARD_LL` capture.
- TX collision vibration imitation (100Hz pulses).
- Settings dialog with audio device selection, signal pattern selection.
- Deep-link: `--connect-uri` or direct `owalkie://...` argument.

### Flutter Client (`flutter-client/`, feature branch)
- **Phase 0** (done): Protocol events, FFI gaps, reconnect + backoff.
- **Phase 1** (done): Profiles persistence (`shared_preferences`), Settings shell, i18n (ru/en via `gen-l10n`), `go_router`, About/version.
- **Phase 2** (done): Repeater mode, PTT lock + countdown, RX busy chip, parallel TX vibration, keep screen on, PTT spam guard.
- **Phase 3** (done): Roger/Call signal TX via FFI, pattern editor, clipboard copy/paste, local UI WAV sounds.
- **Phase 4a** (done): Android foreground service (`WalkieForegroundService.kt`), notification actions, battery optimization.
- **Phase 4b** (done): Network validated/lost, process network bind, uplink signal byte, NAT punch, UDP recovery.
- **Phase 4c** (done): Audio routing (`AudioRouteHelper.kt`), BT headset, phone call pause (`audio_session`).
- **Phase 4d** (done): Media keys PTT toggle (`audio_service`), hardware PTT hold.
- **Phase 5** (done): Deep links (`app_links`), share/import (`share_plus`), channel scan (FFI probe).
- **Phase 6** (done): Settings parity — Tasker external control API, pattern pickers, pause-on-call.
- **Phase 7** (done): iOS scaffold (CocoaPods, background audio, deep links). Full session deferred (vcpkg iOS triplets).
- **Phase 8** (done): Windows tray (`tray_manager`), global PTT hotkey (`WH_KEYBOARD_LL`), desktop settings.
- **Phase 9** (done): Release polish, git-tag versioning, CI (`flutter analyze` + `flutter test`), a11y tests.
- **Post-phase work** (uncommitted): Home a11y chips, Kotlin-style action confirmations, profile reorder buttons, vibration imitation settings, desktop vibration imitation, audio device selection improvements, Android TX stutter fix, Roger/Call signal preview, messenger-style PTT latch (swipe up), hot-swap server profile while connected, UI sound effects parity.
- **PTT policy**: Hold = push-to-talk; slide up while holding = latch; tap when latched = stop. No user "toggle-only" setting.
- **Stack**: `flutter_riverpod`, `go_router`, `shared_preferences`, `vibration`, `wakelock_plus`, `audio_session`, `app_links`, `share_plus`, `tray_manager`, `window_manager`.
- **Plugin**: `packages/owalkie_core` — FFI to owalkie-core C API; Dart background isolate (`session_worker.dart`) owns all FFI calls; UI talks via `SessionService` + `SendPort`.

## Planned

- Windows desktop toolchain: considering MSVC + vcpkg migration (on owner request only).
- Validate tester release behavior without Accessibility service.
- Tune relay noise/tail defaults based on field feedback.
- Remove legacy `audio.json` read fallback (TODO, after a few versions).
- iOS full session (vcpkg iOS triplets for Flutter client).
- Flutter client: mic source, warm mic, disable NS/AGC (custom native audio layer).
- Flutter client merge to master (when ready).

## Recent Key Changes (master, since v0.1.6)

### owalkie-core
- Created as shared relay client library v0.1.0 (`0d5b088`).
- SessionManager: multi-session lifecycle, async disconnect (`115afa3`).
- Simplified relay client API, removed legacy transport paths (`eca1f8d`).
- TX submit pipeline: `OPEN`/`PCM`/`CLOSE` ordered queue (`4ddadc2`).
- Activity probe: `owalkie_check_channel_activity` one-shot WebSocket query (`428fe30`).
- Async WebSocket I/O in session, fixed Android relay crashes (`ce41ec8`).
- Reconnect control moved to clients, connect stability fixes (`cf53e1b`).
- Link signal global registry (`link_signal.cpp`).

### Android (master)
- Migrated to owalkie-core SessionManager + JNI (`115afa3`, `eca1f8d`).
- TX submit pipeline replaces start/end calls (`4ddadc2`).
- `RxPcmJitterBuffer` — fixes choppy RX in background (`fe41467`).
- Reconnect stabilization when server goes down (`9a2a7ef`).
- Long session stability improvements (`31f2c32`).
- `ensureVoiceAudioProfile` moved to capture job (`975c618`).
- Warm mic recorder: optional infinite keep when foreground (`fa4b7a8`).
- Channel scan via `owalkie_check_channel_activity` JNI (`4e8b7db`).
- Scan toggle modes + smarter channel switching (`b7ccbaa`).
- PTT TalkBack: busy lock and countdown (`b82e012`).

### Windows (master)
- Migrated to owalkie-core SessionManager (`115afa3`, `eca1f8d`).
- TX submit pipeline (`4ddadc2`).
- WS transport stabilization (`0c35aaa`).
- AudioEngine improvements (`9a2a7ef`).
- System tray background working (`84c6bf3`).
- Translation fixes for new statuses (`f7faf61`).

### Backend (master)
- Config tunings for optimal sound, compressor, busy mode.
- Squelch hiss edge impulse (`edge_impulse_db`) (`5c3f009`).
- Clicks DSP only during active uplink (`1021d44`).
- Signal-dependent click gain + optional click band-pass (`df21c5c`).
- Duplicate module names allowed in DSP chain (`3bdc80c`).
- WS PTT/RX busy parity, drop RX holdoff (`e30dfd0`).
- `busy_timeout` relay unlock + client PTT countdown (`acc9bba`).

## Testers Release 0.1.7

- Artifacts in `dist/testers-0.1.7/`.
- Android: `owalkie-android-0.1.7-release.apk` (versionCode 107001).
- Windows: `owalkie-desktop-0.1.7-portable-win64.zip`.
- Relay: `owalkie-relay-0.1.7-windows-amd64.exe`.

## Release Process

- Annotated semver tag: `vMAJOR.MINOR.PATCH`.
- **Android**: `versionCode` strictly greater than last published. Floor in `android/owalkie-version.properties`.
- **Windows**: `-DOWALKIE_VERSION_STRING=...` or env `OWALKIE_VERSION_STRING`.
- **Relay**: `-ldflags "-X main.buildVersion=MAJOR.MINOR.PATCH"`.
- **Flutter**: git-tag versioning (`tool/version_from_git.*`, `--build-name` / `--build-number`).
- GitHub Release artifacts: `owalkie-relay-*-linux-amd64`, `owalkie-android-*-debug.apk`. Windows exe is separate.

## Next Steps

1. Continue Flutter client development on `feature/flutter-client` (uncommitted a11y/UX improvements).
2. Validate tester release without Accessibility service (installability + Play Protect).
3. Tune relay noise/tail defaults from field feedback.
4. Maintain protocol compatibility as new relay audio settings are added.
5. Plan Flutter client merge to master when stable.
