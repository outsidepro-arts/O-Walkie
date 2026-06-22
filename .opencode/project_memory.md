# O-Walkie — Project Memory

Last updated: 2026-06-22

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
- **Post-phase work** (uncommitted): Home a11y chips, Kotlin-style action confirmations, profile reorder buttons, vibration imitation settings, desktop vibration imitation, audio device selection improvements, Android TX stutter fix, Roger/Call signal preview, messenger-style PTT latch (swipe up), hot-swap server profile while connected, UI sound effects parity, tray context menu keyboard focus fix (`bringAppToFront: true`), main-thread selective widget rebuilds (Choreographer fix), **phone call detection tri-mode** (off/telephony/audioFocus via `PhoneCallObserver.kt` + `SegmentedButton` settings UI).
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
6. **Flutter client main-thread optimization**: remaining optimizations 2–6 from the plan below (UI sound synthesis, mic permission cache, async audio profile, WAV decode).

## Flutter Client: Non-blocking Connect + State Machine (2026-06-17, with Settle Window)

**Проблема**: `owalkie_connect()` блокировал воркер-изолят на 3500ms, замораживая event loop. 8 булевых флагов для состояния создавали потенциально невалидные комбинации. Гонки обрабатывались флагами-костылями.

**Решение**: Два ключевых изменения:

### 1. Non-blocking connectAsync (C++ FFI)

**`owalkie_flutter_relay.cpp`**:
- `owalkie_flutter_connect_async(session_id, timeout_ms)` — спавнит `std::thread`, который вызывает `owalkie_connect()`, возвращает управление немедленно
- `owalkie_flutter_connect_cancel(session_id)` — вызывает `owalkie_connect_cancel()` → `cancelOngoingConnect()` (стоп-флаг + закрытие сокетов)

**`owalkie-core`**:
- `SessionManager::cancelConnect(id)` — новая публичная функция, вызывает `session->cancelOngoingConnect()`
- `owalkie_connect_cancel(session_id)` — новая C API

### 2. State machine в `session_worker.dart`

**Вместо 8 bool**:
```dart
enum SessionState { idle, connecting, connected, paused }
```

**Явные переходы**:
```
ConnectCommand:        (any) → connecting
SwitchServerCommand:  (any) → connecting (disconnect old + prepare new)
DisconnectCommand:    (any) → idle
CONNECTED:            connecting → connected (cancel reconnect timer)
CONNECTION_LOST:      connected → connecting (start reconnect timer)
PROTOCOL_ERROR:       (any) → idle
PauseRelayCommand:    connected → paused
ResumeRelayCommand:   paused → connecting
```

**Timer-based reconnect** (вместо `while(true) + await`):
- `Timer.periodic(300ms)` тикает, пока `state == connecting`
- На каждом тике: cancel предыдущего connect → `connectAsync()` → проверка `sessionReady()`
- Burst: 4 попытки, затем backoff (3s → 4.5s → ... → 10s)

**Убраны**: `_desiredConnected`, `_hadConnected`, `_clientReconnectRunning`, `_reconnectCancelled`, `_relayPausedExternally`, `_publishedConnected`, `_publishedConnecting`, `_publishedReconnecting`, `_pendingNetworkRecover`

### Изменённые файлы (полный список за сегодня)

| Файл | Изменения |
|------|-----------|
| `owalkie-core/include/owalkie_core.h` | `owalkie_connect_cancel()` + `OWALKIE_ERR_BUSY` |
| `owalkie-core/include/owalkie/session_manager.hpp` | `cancelConnect()` |
| `owalkie-core/src/session_manager.cpp` | `cancelConnect()` impl |
| `owalkie-core/src/c_api.cpp` | `owalkie_connect_cancel()` |
| `flutter-client/.../owalkie_flutter_relay.cpp` | `connectAsync`, `connectCancel`, `play_local_pcm_async` FFI exports |
| `flutter-client/.../owalkie_flutter_audio.cpp` | RxJitterBuffer class, `play_local_pcm_async()` |
| `flutter-client/.../owalkie_flutter_audio.h` | `play_local_pcm_async()` declaration |
| `flutter-client/.../owalkie_flutter_bridge.h` | `owalkie_flutter_play_local_pcm_async` declaration |
| `flutter-client/.../session_relay_bindings.dart` | FFI bindings for connectAsync/cancel + playLocalPcmAsync |
| `flutter-client/.../session_worker.dart` | State machine + timer reconnect + settle window + pttResult before tone |
| `flutter-client/.../home_screen_controller.dart` | Removed `_phoneCallPauseInProgress`, warm mic sync in `_ensureSessionImpl` |

### Reconnect Settle Window

При `CONNECTION_LOST` в течение 2500ms после `CONNECTED` — не стартуем reconnect loop немедленно, а запускаем settle timer (poll `sessionReady()` каждые 100ms). Если сессия самовосстановилась — остаёмся в `connected`. Если settle окно истекло — переход в `connecting`.

### Что не менялось (будет в следующей итерации)

- `_pttDownInProgress` в `home_screen_controller.dart` — сохранён из-за async permission check на mobile, можно будет убрать после рефакторинга permission flow
- `_disconnect()` всё ещё блокирует воркер на ~200ms (ожидание connect thread) — можно сделать асинхронным в будущем
- `home_screen_controller.dart` обратно совместим с новым `transportState`
- **Убраны**: `_phoneCallPauseInProgress` — полностью избыточен (state.relayPausedForPhoneCall + worker command queue)

### PTT Latency Fix: Non-blocking pttResult + play_local_pcm_async (2026-06-17)

**Проблема**: `_playLocalUi()` вызывалась ДО `_mainPort.send(pttResult)`, и `play_local_pcm_blocking()` блокировала воркер на 100-300ms, задерживая отправку `pttResult` → UI не показывал TX.

**Решение**:
1. **`session_worker.dart`**: `_mainPort.send(pttResult)` перемещён до `_playLocalUi()`. UI видит TX через ~3-5ms после PTT вместо 100-300ms.
2. **`play_local_pcm_async()`**: новая C++ функция в `owalkie_flutter_audio.cpp`, которая копирует сэмплы в `shared_ptr` и запускает `play_local_pcm_blocking` в `std::thread`. FFI возвращается немедленно, воркер не блокируется.
3. **`_playLocalUi()`**: использует `playLocalPcmAsync()` вместо `playLocalPcm()`.

**Изменённые файлы**:
- `owalkie_flutter_audio.h` — `play_local_pcm_async()` declaration
- `owalkie_flutter_audio.cpp` — `play_local_pcm_async()` implementation
- `owalkie_flutter_bridge.h` — FFI export declaration
- `owalkie_flutter_relay.cpp` — FFI export `owalkie_flutter_play_local_pcm_async`
- `session_relay_bindings.dart` — FFI binding + wrapper `playLocalPcmAsync()`
- `session_worker.dart` — `pttResult` before tone + use async playback

### Warm Mic Recorder (2026-06-17)

Реализован warm mic recorder — аналог Android `WarmMicRecorderStore`. Когда включён в настройках и приложение в foreground + connected, capture device держится открытым в idle, устраняя задержку на `ma_device_init` при PTT.

**Уже было (C++)**:
- `warm_capture()` — открывает capture device без старта TX pump thread
- `release_capture_if_idle()` — закрывает capture device если TX не активен

**Добавлено (Dart)**:
- Синхронизация настроек воркеру через `SessionSyncWarmCaptureCommand` при: создании сессии, изменении lifecycle, получении transport state, окончании TX
- `_applyWarmCaptureFromFlags()` в воркере — проверяет `_warmMicRecorderEnabled && _appInForeground && connected && !_localTxActive`
- Вызывается из `_publishState()`, `_pttUp()`, `_onCommand()`
- Настройка в `settings_screen.dart` с переключателем "Keep microphone ready (faster PTT start)"

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

---

### Flutter Client: Reconnect RX Audio Fix (2026-06-17)

**Проблема**: При серии реконнектов звук не воспроизводился, хотя чип статуса показывал "приём".

**Корень**: `OWALKIE_EV_CONNECTION_LOST` не обрабатывался в `on_session_event()` (`owalkie_flutter_relay.cpp`). В отличие от `DISCONNECTED`/`CONNECTION_FAILED`/`PROTOCOL_ERROR`, не вызывались `stop_capture()` и `release_session_audio()`. При reconnect `configure()` не пересоздавал playback device (те же параметры → `reopen = false`), и звук оставался в стейте от предыдущей сессии.

**Фикс**: Добавлен блок `if (ev->type == OWALKIE_EV_CONNECTION_LOST)` с очисткой аудио-стэйта (без сброса `g_active_session` — нужен для reconnect loop). После reconnect первый `on_rx_pcm` инициализирует playback с нуля.

**Статус**: ✅ Подтверждено пользователем, проблема исправлена.

---

## Flutter Client: RxJitterBuffer Implementation (2026-06-17, with Non-blocking Connect)

**Проблема**: После реконнекта RX-аудио начиналось с задержками и рывками из-за:
1. Отсутствия preroll — playback device стартовал немедленно, первый фрейм часто был silent
2. Отсутствия jitter буфера — сетевые bursts не сглаживались, underrun → тишина
3. Отсутствия ресинхронизации при sustained underrun

**Решение**: Заменён сырой `ma_pcm_rb` + `ma_device` на `RxJitterBuffer` class в `owalkie_flutter_audio.cpp`:

### RxJitterBuffer API
- `open(sample_rate, frame_samples)` — выделяет ring buffer (48 фреймов = 960ms), НЕ стартует device
- `push(samples, count)` — запись в SPSC ring buffer
- `read(output, frame_count)` — чтение из callback'а miniaudio
- `close()` — uninit device + очистка
- `isOpen()` — проверка состояния

### Preroll (6 фреймов = 120ms)
- Playback device НЕ стартует, пока не накоплено 6 фреймов
- Устраняет начальную тишину (первый callback всегда находит данные)
- После старта — все последующие push() пишут в ring buffer, callback читает

### Resync при sustained underrun (>2 подряд)
- Когда callback не находит данных >2 раз подряд → `_resync = true`
- Ring buffer очищается (write=0, read=0, fill=0)
- Push() сбрасывает `_resync` после накопления preroll заново
- Аналог Android `RxPcmJitterBuffer` resync при >2 frame drift

### Thread safety
- `push()` — вызывается из `on_rx_pcm()` (C++ WS/UDP thread) под `g_mu`
- `read()` — вызывается из `deviceCallback` (miniaudio internal thread), lock-free SPSC
- Single producer, single consumer через `_fill` atomic (acquire/release)
- При полном буфере — новые сэмплы дропаются (no overflow race)

### Изменённые файлы
- `flutter-client/packages/owalkie_core/src/owalkie_flutter_audio.cpp` — RxJitterBuffer class (154 строки), заменены глобалы, open/close/on_rx_pcm

---

## Flutter Client: UI Responsiveness Fix (2026-06-21)

**Проблема**: Интерфейс подтормаживал, данные не всегда обновлялись. Четыре корневые причины:

1. **Блокировка UI-потока при воспроизведении звуков**: `UiSoundLibrary._playSamples()` вызывала `LocalPcmPlayer.playBlocking()` → C++ `play_local_pcm_blocking()`, которая захватывала глобальный мьютекс `g_mu` и вызывала `std::this_thread::sleep_for(duration+80ms)` на главном потоке. Каждый UI-звук замораживал интерфейс на 200-400ms, а при смене состояния соединения проигрывалось 2-3 звука подряд (0.5-1s полной неотзывчивости).

2. **Конкуренция за `g_mu`**: Пока главный изолят держал мьютекс для UI-звука, worker-изолят блокировался на захвате/воспроизведении RX-аудио.

3. **Полная перестройка дерева виджетов**: `HomeScreen.build()` использовал `ref.watch(homeScreenControllerProvider)` — любое изменение любого поля (включая `signalChip`, который обновлялся каждые несколько секунд) вызывало полную перестройку всего дерева.

4. **`Isolate.run()` для вибро-имитации**: `VibrationImitationPlayer` спавнил новый изолят для каждого вибро-события (100-300ms overhead).

**Решение**: Четыре изменения:

### 1. Асинхронное воспроизведение UI-звуков
- Добавлен `LocalPcmPlayer.playAsync()` — использует существующую C++ функцию `play_local_pcm_async()`, которая копирует PCM в `shared_ptr` и запускает воспроизведение в `std::thread::detach()`. FFI возвращается немедленно.
- `UiSoundLibrary._playSamples()` переведён с `playBlocking()` на `playAsync()`.
- Все UI-звуки (connect/disconnect/switch/volume/error tones) больше не блокируют UI-поток и не конкурируют с worker-изолятом за `g_mu`.

### 2. Пропуск избыточных обновлений состояния
- В `_onSessionMessage()` добавлена проверка: если `SessionTransportStateMessage` не меняет `connected`/`connecting`/`reconnecting`/`error` — обработчик завершается досрочно (no-op state update).
- В `SessionUplinkSignalMessage` — пропуск если `percent == state.uplinkSignalPercent`.

### 3. Селективные перестройки виджетов
- `_StatusChips` преобразован из `StatelessWidget` в `ConsumerWidget` с селективными `ref.watch(provider.select(...))` на `connectionDisplayChip` и `signalChip`. Теперь только чипы перестраиваются при изменении сигнала.
- Создан `_PttAreaConsumer` — ConsumerWidget, отслеживающий только `txActive`, `pttServerLocked`, `pttLockSec`, `txCountdownSec`, `isConnected`. PTT-область не перестраивается при изменении сигнала или громкости.
- Удалена неиспользуемая переменная `pttUiEnabled` из главного `build()`.

### 4. Прямой `playAsync()` вместо `Isolate.run()` для вибро-имитации
- `VibrationImitationPlayer.playDuration()` и `playPreview()` теперь вызывают `LocalPcmPlayer.playAsync()` напрямую вместо `Isolate.run()`.
- Удалены `_playPcmBlockingIsolate` и класс `_PcmPlayArgs`.
- Удалены импорты `dart:isolate` и `dart:typed_data`.

### Изменённые файлы
| Файл | Изменения |
|------|-----------|
| `packages/owalkie_core/lib/src/local_pcm_player.dart` | Добавлен `playAsync()` |
| `packages/owalkie_core/lib/src/ui_sound_library.dart` | `playBlocking` → `playAsync` |
| `lib/features/home/home_screen_controller.dart` | Guard от no-op transport state + uplink signal |
| `lib/features/home/home_screen.dart` | `_StatusChips` ConsumerWidget, `_PttAreaConsumer`, удалён неиспользуемый `pttUiEnabled` |
| `lib/platform/vibration_imitation_player.dart` | `Isolate.run()` → `LocalPcmPlayer.playAsync()`, удалены isolate-хелперы |

### Эффект
- **UI-звуки**: 0ms блокировки (было 200-400ms)
- **Worker изолят**: больше не блокируется UI-звуками (конкуренция за `g_mu` устранена)
- **Перестройки виджетов**: сигнал-чипы не триггерят перестройку PTT-области и формы
- **Вибро-имитация**: 0ms overhead спавна изолята (было 100-300ms)

*Last updated: 2026-06-22*

## Flutter Client: Main Thread Optimization Plan

**Проблема**: `Choreographer: Skipped 298 frames!` при запуске на Android. Главный поток блокировался полными перестройками виджетов при каждом `state.copyWith()` и PCM-синтезом UI-звуков.

### Оптимизация 1: Селективные виджеты — ✅ DONE (2026-06-22)

Монолитный `ref.watch(homeScreenControllerProvider)` без `.select()` в `home_screen.dart:176` заменён на 6 селективных `ConsumerWidget`-подписчиков:

| Виджет | Что подписывает | Что перестраивает |
|---|---|---|
| `HomeScreen.build()` | `connectionDetailsExpanded` | Только layout switch |
| `_StatusChips` | `connectionDisplayChip`, `signalChip` | Только чипы статуса |
| `_ErrorStatusArea` | `statusInfo`, `lastError` | Только ошибки (shrink если null) |
| `_ServerProfileArea` | 9 полей (profiles, selectedIndex, expanded, scanActive, isConnected, isConnecting, sessionSupported, canNavigate, hasPrevious, hasNext) | Dropdown + кнопки навигации |
| `_ExpandedFormActions` | 6 полей (profiles, canMoveUp/Down, isConnected, isConnecting, sessionSupported) | Expanded form кнопки |
| `_FooterVersion` | `coreVersion`, `protocolVersion` | Только версия |

**Результат**: Choreographer предупреждения исчезли, интерфейс стал заметно отзывчивее. a11y-семантика полностью сохранена (8/8 assertions проходят).

**Изменённые файлы**:
- `flutter-client/lib/features/home/home_screen.dart` — рефакторинг виджетов, удаление неиспользуемых imports, deprecated `DropdownButtonFormField.value` → `initialValue`

### Оптимизация 2: Асинхронный синтез UI-звуков — PENDING

**Проблема**: `ui_sound_library.dart:213-242` — `_synthesize()` делает per-sample `math.sin()` + envelope на главном потоке. Для тона 200мс при 44100Hz = ~8800 итераций. `_playMixed()` тоже считает на главном потоке.

**Решение**:
- Предсинтез статических тонов (connected, error, connectStart, disconnect) при старте в `_loadAll()` — ~5-10мс один раз
- Динамические звуки (connect/disconnect action = mixing wav+tones, signal pattern preview) через `compute()` — background isolate

**a11y-влияние**: Нет (звуки не в семантическом дереве).

### Оптимизация 3: Кэширование разрешений микрофона — PENDING

**Проблема**: `home_screen_controller.dart:1405` — `NativePlatform.ensureMicrophonePermission()` вызывается на каждый PTT press через platform channel.

**Решение**: Кэш `bool? _micPermissionCache` в контроллере. Сброс при `AppLifecycleState.resumed`.

**a11y-влияние**: Нет (platform channel, не UI).

### Оптимизация 4: Async audio profile — PENDING

**Проблема**: `_applyVoiceAudioRoute()` вызывает цепочку синхронных FFI + platform channel на главном потоке: `listCaptureDevices()`, `listPlaybackDevices()`, `applySelection()`, 4× `setAaudio*()`.

**Решение**: Перенести FFI вызовы в `Isolate.run()` для desktop path (Android/iOS path уже async через platform channel).

**a11y-влияние**: Нет (FFI, не UI).

### Оптимизация 5: WAV decode via compute — PENDING

**Проблема**: `ui_sound_library.dart:40-51` — `_loadResampled()` делает decode + linear resample на главном потоке при `ensureLoaded()`.

**Решение**: `compute(_decodeAndResample, bytes)` для каждой WAV-загрузки. 4 файла загружаются параллельно через `Future.wait()`.

**a11y-влияние**: Нет (asset loading, не UI).

### Оптимизация 6: TX countdown — DONE (автоматически после Оптимизации 1)

`_PttAreaConsumer` уже использует `.select()` на `txCountdownSec`. Timer-тик перестраивает только PTT-область, а не всё дерево.
