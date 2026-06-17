# O-Walkie — Project Memory

Last updated: 2026-06-17

## Branches

| Branch | Status | Description |
|--------|--------|-------------|
| `master` | stable | Production clients (Android Kotlin, Windows C++). Latest tag: `v0.1.6`. |
| `feature/flutter-client` | active | Experimental Flutter client (phases 0–9 done). Current working branch. |
| `beka_build_fixes` | stale | Build fixes branch. |

**Current work is on `feature/flutter-client`.** Master has 35 commits ahead of `v0.1.6` (owalkie-core, SessionManager, TX pipeline, channel scan, system tray, warm mic).

## Current Stage

- **Master**: owalkie-core extracted as shared C/C++ relay client library (v0.1.0); Android and Windows migrated to SessionManager + TX submit pipeline (`OPEN`/`PCM`/`CLOSE`); channel scan via `owalkie_check_channel_activity`; Windows system tray; Android warm mic recorder + scan toggle modes.
- **Flutter branch**: Experimental cross-platform client through phases 0–9 (protocol UX, profiles, Roger/Call, FGS, deep links, scan, iOS scaffold, Windows tray/hotkey, release polish). Active uncommitted changes in `flutter-client/` (a11y chips, profile reorder, vibration imitation, warm mic recorder store, tray menu keyboard focus fix).
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
- **Post-phase work** (uncommitted): Home a11y chips, Kotlin-style action confirmations, profile reorder buttons, vibration imitation settings, desktop vibration imitation, audio device selection improvements, Android TX stutter fix, Roger/Call signal preview, messenger-style PTT latch (swipe up), hot-swap server profile while connected, UI sound effects parity, tray context menu keyboard focus fix (`bringAppToFront: true`).
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

## Flutter Client Stability Improvements (2026-06-14)

### Priority 1: Critical Fixes (Completed)

**1.1. Lifecycle guards в SessionService**
- Добавлен `_disposed` flag для предотвращения операций после dispose
- Улучшен `stop()` с proper cleanup и задержкой для graceful shutdown
- Все публичные методы проверяют `_disposed` перед отправкой команд в worker isolate

**1.2. Cancel reconnect loop в SessionWorker**
- Добавлен `_reconnectCancelled` flag для немедленной остановки reconnect loop
- Флаг устанавливается в `_disconnect()`, `_pauseRelayForExternalReason()`, `_shutdown()`
- Reconnect loop проверяет флаг на каждой итерации

**1.3. Guard _onSessionMessage после dispose**
- Добавлен `_disposed` flag в `HomeScreenController`
- `_onSessionMessage()`, `_onPlatformEvent()`, `_onWindowsGlobalPttEvent()` проверяют флаг
- `_dispose()` устанавливает флаг и предотвращает повторный вызов

**1.4. Fix pollTimer cancellation**
- `_shutdown()` теперь устанавливает `_pollTimer = null` после cancel
- Предотвращает использование timer после shutdown

### Priority 2: Stability Improvements (Completed)

**2.3. Error handling для platform channels**
- Все `MethodChannel.invokeMethod()` вызовы обёрнуты в try-catch
- Platform errors silently ignored для предотвращения crashes
- Методы возвращают safe defaults при ошибках (false, empty list, unassigned binding)

### Priority 3: Long-term Improvements (Completed)

**3.1. Refactor scan loop с proper cancellation**
- Добавлен `_scanCancellation` Completer для немедленной остановки scan loop
- Создан helper метод `_cancellableScanDelay()` для cancellable delays
- Scan loop проверяет cancellation на каждой итерации и внутри profile loop
- `stopScanning()` завершает completer для немедленной остановки

**3.2. Session telemetry для диагностики**
- Создан `SessionTelemetry` класс (`lib/platform/session_telemetry.dart`)
- Ring buffer на 100 событий с timestamps
- Логирует ключевые события: CONNECT, DISCONNECT, CONNECTED, CONNECTION_LOST, PTT_DOWN/UP, SCAN_START/STOP, SCAN_FOUND, ERROR
- Интегрирован в `HomeScreenController` с вызовами в ключевых точках lifecycle
- Доступен через `controller.telemetry.dump()` для диагностики

### Priority 4: Additional Stability Fixes (2026-06-14)

**4.1. _ensureSession() race condition fix**
- Добавлен `_ensureSessionFuture` для сериализации concurrent вызовов
- Если `_ensureSession()` уже выполняется, последующие вызовы ждут завершения
- Предотвращает создание нескольких `SessionService` одновременно
- Метод разделён на `_ensureSession()` (wrapper) и `_ensureSessionImpl()` (implementation)

**4.2. _scheduleMobileSessionRelease() race condition fix**
- Добавлен `_mobileReleaseScheduled` flag для предотвращения overlapping calls
- Метод вызывается из `toggleConnection()` и `_onSessionMessage` при transport state change
- Flag устанавливается в начале и очищается в `finally` блоке
- Предотвращает race при teardown audio session и worker isolate

**4.3. PTT state race condition fix**
- Добавлен `_pttDownInProgress` flag для предотвращения concurrent PTT down calls
- PTT команды приходят из 6 источников: touch, hardware, media button, external API, Windows global PTT, keyboard
- Flag устанавливается в `pttDown()` перед отправкой команды
- Очищается в `SessionPttResultMessage` handler или при transport state change (disconnect)
- Также очищается в `_pttDownAsync()` при mic permission failure и в `_dispose()`
- Предотвращает отправку нескольких pttDown команд worker'у одновременно

**4.4. Phone call pause/resume race condition fix**
- Добавлен `_phoneCallPauseInProgress` flag для предотвращения overlapping pause/resume операций
- Методы `_pauseRelayForPhoneCall()` и `_resumeRelayAfterPhoneCall()` вызываются из `AudioInterruptionManager`
- При быстром toggling (begin/end/begin) может возникнуть race condition
- Flag устанавливается в начале каждого метода и очищается в конце
- Предотвращает interleaving операций pause и resume

### Impact

Эти изменения устраняют основные причины падений и race conditions:
- Предотвращает использование disposed объектов
- Немедленная остановка async operations при disconnect
- Защита от platform channel errors
- Proper cleanup при shutdown

Все изменения прошли `flutter analyze` без новых ошибок.

### UI Sounds: Main Thread Playback (2026-06-14)

**Проблема**: UI звуки подключения/отключения (connected, manual connect/disconnect) отправлялись в worker isolate через `session.playLocalSamples()`, что добавляло задержку из-за message passing между isolates.

**Решение**: Изменён `UiSoundLibrary._playSamples()` в `packages/owalkie_core/lib/src/ui_sound_library.dart`:
- Все UI звуки теперь ВСЕГДА проигрываются через `LocalPcmPlayer.playBlocking()` в основном потоке
- Убрана проверка `session.isRunning` и отправка в worker
- Звуки проигрываются моментально вместе с `switch_nav.wav`

**Затронутые звуки**:
- `playConnected()` — тоны {1400Hz, 1700Hz}
- `playManualConnectStart()` — восходящая последовательность
- `playManualDisconnect()` — нисходящая последовательность
- `playConnectionError()` — error tone
- `playSwitch()` — switch_nav.wav
- `playPttPress()`, `playPttRelease()` — PTT звуки
- `playVolumePreview()` — preview громкости
- `playSignalPatternPreview()` — preview Roger/Call паттернов

**Результат**: UI звуки теперь проигрываются без задержки, независимо от состояния session.

### Burst Reconnect Strategy (2026-06-14)

**Проблема**: Старая стратегия reconnect использовала экспоненциальный backoff с начальной задержкой 1500ms, что было слишком медленно для мобильных сетей с кратковременными потерями связи.

**Решение**: Новая "burst reconnect" стратегия в `packages/owalkie_core/lib/src/session_worker.dart`:
- **Серии быстрых попыток**: 4 попытки с интервалом 300ms каждая
- **Паузы между сериями**: начинаются с 3 секунд, увеличиваются с множителем 1.5x
- **Максимальная пауза**: 10 секунд между сериями
- **Бесконечные серии**: продолжаются до успешного подключения или отмены пользователем

**Константы**:
```dart
static const _attemptsPerSeries = 4;        // Попыток в серии
static const _intervalInSeriesMs = 300;      // Интервал между попытками (ms)
static const _initialSeriesPauseMs = 3000;   // Начальная пауза между сериями (ms)
static const _maxSeriesPauseMs = 10000;      // Максимальная пауза (ms)
static const _seriesPauseMultiplier = 1.5;   // Множитель увеличения паузы
```

**Поведение**:
- Серия 1: 4×300ms → пауза 3s
- Серия 2: 4×300ms → пауза 4.5s
- Серия 3: 4×300ms → пауза 6.75s
- Серия 4+: 4×300ms → пауза 10s (максимум)

**Улучшения**:
- Первая попытка reconnect: 300ms вместо 1500ms (5× быстрее)
- Время до первой паузы: 1.2s вместо 1.5s
- Количество попыток за 10 секунд: 8-12 вместо 2-3 (4× больше)
- Успешное подключение в серии сбрасывает паузу к начальному значению

**Результат**: Значительно улучшена отзывчивость при кратковременных потерях сети, типичных для мобильных устройств.

## Disconnect Acknowledgement Protocol (2026-06-17)

**Проблема**: Двухфазный disconnect с fire-and-forget + таймер 400ms создавал race condition. `session.disconnect()` ничего не возвращал, и UI-изолят не знал, когда воркер закончил native cleanup. `_scheduleMobileSessionRelease()` убивал изолят через 400+100ms независимо от того, завершился ли native `_relay.disconnect()`.

**Решение**: Однофазный disconnect с подтверждением от воркера через ack-сообщения:

### Изменённые файлы

**`packages/owalkie_core/lib/src/session_messages.dart`**:
- Добавлены `SessionDisconnectCompleteMessage` и `SessionShutdownCompleteMessage`

**`packages/owalkie_core/lib/src/session_worker.dart`**:
- `_disconnect()`: отправляет `SessionDisconnectCompleteMessage` после `_publishState()`
- `_shutdown()`: отправляет `SessionShutdownCompleteMessage` перед `Isolate.exit()`

**`packages/owalkie_core/lib/session_service.dart`**:
- `disconnect()` → `Future<void>`, использует `Completer<void>` с подпиской на `SessionDisconnectCompleteMessage`
- Таймаут 5s как safety net
- `_disconnectInProgress` guard для предотвращения concurrent вызовов
- `stop()`: ждёт `SessionShutdownCompleteMessage` (таймаут 3s)
- `dispose()`: завершает pending disconnect completer, закрывает events stream после stop

**`lib/features/home/home_screen_controller.dart`**:
- Удалён `_mobileReleaseScheduled` field
- Удалён `_scheduleMobileSessionRelease()` — заменён на `_releaseMobileAudioAndTeardown()`
- `toggleConnection()` disconnect path: `await session.disconnect()`, затем `if (!sessionKeepAlive)` — cleanup
- Все места вызова `session.disconnect()` теперь await: `_reconnectToProfile()`, `shutdownForAppExit()`, `_connectToProfileFromScan()`
- Добавлены no-op case для `SessionDisconnectCompleteMessage` и `SessionShutdownCompleteMessage`

### Новая последовательность disconnect

```
User taps Disconnect
  ↓
toggleConnection() — optimistic UI update
  ↓
session.disconnect() → SessionDisconnectCommand
  ↓ (serialized via ReceivePort)
Worker _disconnect():
  1. native cleanup (pttUp, disconnect, releaseSessionAudio)
  2. _publishState(connected:false, connecting:false)
  3. send SessionDisconnectCompleteMessage
  ↓
SessionService получает ack → Future resolved
  ↓
await session.disconnect() завершён
  ↓
if (!sessionKeepAlive):
  _releaseMobileAudioAndTeardown()
    → NativePlatform.releaseAudioSession()
    → _session.setAndroidBtVoiceRoute(false)
    → _teardownIdleSession() → _stopSessionWorker()
      → _sessionSub?.cancel()
      → session.stop()
        → SessionShutdownCommand
        → ждёт SessionShutdownCompleteMessage (3s timeout)
        → Isolate.kill (safety net)
```

**Результат**: Полная детерминированность, ни одной гонки, никаких магических таймеров. Если пользователь переподключается во время disconnect, `sessionKeepAlive` возвращает true и teardown не происходит.

### Signal Sequence Clipboard Unification (2026-06-17)

**Проблема**: Flutter-клиент использовал собственный упрощённый JSON-формат для copy/paste сигнатур сигналов (`{"name":..., "points":[...], "repeatCount":N}`), несовместимый с Android и Windows.

**Решение**: Формат унифицирован — Flutter теперь использует тот же формат, что Android (`SignalSequenceClipboard.kt`) и Windows:

```json
{"oWalkieSignalSequence":{"version":1,"signal":{"name":"..."},"points":[{"durationMs":20,"freqHz":890},...],"repetitions":4}}
```

**Изменённые файлы** (flutter-client):
- `lib/domain/signal_sequence_clipboard.dart` — новый файл: `signalSequenceToJson()` (сериализация) и `signalSequenceParseFromText()` (десериализация с поддержкой nested envelope + legacy flat fallback + извлечение `{...}` из текста)
- `lib/features/signals/pattern_editor_screen.dart` — `_copyToClipboard()` и `_pasteFromClipboard()` переведены на новый формат; добавлена валидация длительности и пустых points
- `test/signal_sequence_clipboard_test.dart` — 17 тестов (round-trip, Android-совместимость, legacy, garbage extraction, corner cases)

**Результат**: clipboard-формат сигнатур полностью совместим между Android, Windows и Flutter-клиентами.
