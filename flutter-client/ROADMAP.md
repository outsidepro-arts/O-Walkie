# Flutter client — migration roadmap (Android → Flutter)

Experimental Flutter client targeting parity with the native Kotlin app (`android/`) and Windows client (`windows-client-cpp/`). Go relay and `owalkie-core` protocol stay unchanged.

**Principle:** relay transport, Opus, PTT uplink, RX PCM, and reconnect live in `packages/owalkie_core` → `owalkie-core`. Do **not** replace that stack with Dart audio/network plugins. Use pub.dev plugins for UI, persistence, navigation, and platform shell only. Custom plugins only when no suitable package exists or reviews show poor quality.

See also: [ARCHITECTURE.md](ARCHITECTURE.md).

## PTT input policy (fixed — no “PTT toggle mode” setting)

| Input | Behavior |
|-------|----------|
| **On-screen PTT button** | Short **tap** = TX latch until next tap; **hold** = classic push-to-talk |
| **Hardware PTT key** | Always hold-to-talk (Phase 4 / 6) |
| **Media / headset play-pause** | Toggle TX latch (Phase 4d) |
| **Keyboard Space** (focused) | Toggle TX latch |
| **Screen reader** | Explicit Start / Stop talking actions (toggle) |

Implementation: `lib/features/home/ptt_gesture_button.dart`, `ptt_touch_policy.dart`.

There is **no** user setting to switch the screen button to “toggle-only” (removed vs Kotlin).

---

## Current baseline

| Done | Missing / stubbed |
|------|-------------------|
| Connect / disconnect, hybrid PTT, RX volume | Roger/Call, scan logic |
| PTT lock, TX countdown, repeater, parallel TX, burst guard | FGS, deep links |
| Multi-profile persistence + Settings About | Full settings (mic, Tasker) |
| `SessionService` + isolate worker | |
| Android/iOS mic permission + audio session | FGS, notifications |
| Windows session (vcpkg) | iOS full session |
| | Media keys, Tasker, phone-call pause |

---

## Phase 0 — Core stabilization *(done — iOS vcpkg session deferred to Phase 7)*

**Goal:** solidify existing stack before new screens.

| Task | Status |
|------|--------|
| Full session events in UI (`session_event_mapper.dart`) | done |
| `pttLocked`, `txCountdown`, `protocolError`, `connectionFailed` | done |
| Human-readable reconnect + backoff | done (`Reconnecting…`, 1.5s→8s) |
| FFI `set_repeater_mode`, `check_channel_activity` | done |
| Signal TX (Roger/Call uplink) | Phase 3 |
| iOS full session (vcpkg triplets) | Phase 7 (`ios/README.md`) |
| Regression tests | done (`test/session_event_mapper_test.dart`) |

**Plugins:** none — `owalkie_core` + Dart only.

### Acceptance tests (Phase 0)

Prerequisites: relay in LAN, Android APK or Windows dist with session enabled, second client optional.

- [ ] Connect → chip **Connected**; disconnect → **Disconnected**
- [ ] Hold PTT under ~300 ms release → one TX burst; hold longer → TX while finger down
- [ ] Short tap PTT → TX stays on until second tap (latch)
- [ ] Space with PTT focused → toggle latch
- [ ] RX volume slider affects incoming audio
- [ ] Kill Wi‑Fi briefly → **Reconnecting…**, then **Connected** when network returns
- [ ] Server PTT lock → button shows countdown, TX blocked
- [ ] `flutter test` passes on dev machine

---

## Phase 1 — App shell and data *(done)*


**Goal:** structure like Kotlin without heavy platform work.

| Task | Plugin |
|------|--------|
| Navigation Home ↔ Settings ↔ Roger editor | [`go_router`](https://pub.dev/packages/go_router) |
| Server profiles (CRUD, order) | [`shared_preferences`](https://pub.dev/packages/shared_preferences) |
| Screen orientation | `SystemChrome.setPreferredOrientations` |
| About / version | [`package_info_plus`](https://pub.dev/packages/package_info_plus) |
| GitHub link | [`url_launcher`](https://pub.dev/packages/url_launcher) |
| i18n (ru/en) | `flutter gen-l10n` |

**Custom:** `lib/domain/` + `ServerStore` (mirror `android/.../model/ServerStore.kt`).

### Acceptance tests (Phase 1)

- [ ] Add 2+ profiles: **new name → Save adds**; **same name → Save updates** (Kotlin parity) → force-stop → restored
- [ ] Dropdown / Prev / Next switch profile (blocked while connected, or disconnect first)
- [ ] **Delete** removes profile (cannot delete last one)
- [ ] **More → Settings** opens About; app version visible; GitHub link opens browser
- [ ] Orientation setting persists (if implemented)
- [ ] Connect using saved profile still works after restart

---

## Phase 2 — Protocol UX *(done)*

**Goal:** MainActivity + WalkieService logic at UI level (no background service yet).

| Task | Solution |
|------|----------|
| Repeater mode (overflow menu) | FFI `owalkie_set_repeater_mode` + Riverpod |
| PTT lock + countdown on button | `OWALKIE_EV_PTT_*`, `TX_COUNTDOWN` |
| RX busy / signal chip | `rx_broadcast_start/end` |
| Parallel TX vibration | [`vibration`](https://pub.dev/packages/vibration) |
| Keep screen on during TX | [`wakelock_plus`](https://pub.dev/packages/wakelock_plus) |
| PTT spam guard | Dart (from `WalkieService.kt`) |
| Hardware PTT hold / media toggle | per [PTT policy](#ptt-input-policy-fixed--no-ptt-toggle-mode-setting) |

### Acceptance tests (Phase 2)

- [x] Repeater menu toggles mode while connected (second client sees effect)
- [x] Parallel TX → vibration (Android device)
- [x] TX keeps screen on (`wakelock_plus`)
- [x] PTT spam guard after rapid releases
- [x] Latch + hold PTT still behave per policy during lock countdown / RX busy chip

**Implementation:** repeater in More menu, `PttBurstGuard`, `wakelock_plus` on TX, `vibration` for parallel TX + TX countdown pulse, `connectionDisplayChip` for parallel transmission.

---

## Phase 3 — Roger / Call / UI sounds *(done)*

| Task | Solution |
|------|----------|
| Roger on release, Call uplink | FFI `owalkie_signal_generate_pcm` + `owalkie_tx_submit` |
| Pattern editor screen | New route + `shared_preferences` |
| Clipboard copy/paste sequences | `Clipboard` / [`clipboard`](https://pub.dev/packages/clipboard) |
| Local WAV (connect, PTT, error) | [`audioplayers`](https://pub.dev/packages/audioplayers) or [`just_audio`](https://pub.dev/packages/just_audio) — **UI only**, not relay PCM |

Relay RX/TX stays in miniaudio (`owalkie_flutter_audio.cpp`).

### Acceptance tests (Phase 3)

- [ ] Roger on PTT release audible locally and on second client
- [ ] **Call** button sends call pattern on air
- [ ] Connect / error UI WAV audible (not relay path)
- [ ] Custom pattern editor save/load survives app restart

---

## Phase 4 — Android: background, network, audio policy *(in progress — 4a FGS)*

### 4a. Foreground service + notification *(implemented — device test pending)*

| Task | Solution |
|------|----------|
| FGS type `microphone` (API 34+) | Thin Kotlin `WalkieForegroundService` + method channel |
| POST_NOTIFICATIONS | `MainActivity` permission on connect |
| Notification actions | Disconnect/connect toggle + battery settings intent |
| Battery optimization hint | Notification action → system battery screen |

**Implementation:** `WalkieForegroundService.kt`, `PlatformEvents` EventChannel, `NativePlatform.startSessionForeground` wired from `HomeScreenController` on connect/disconnect.

### 4b. Network and reconnect

| Task | Plugin / custom |
|------|-----------------|
| Network validated / lost | [`connectivity_plus`](https://pub.dev/packages/connectivity_plus) |
| `android_setprocnetwork` | **Custom** method channel (mirror JNI hook) |
| Uplink signal byte | [`network_info_plus`](https://pub.dev/packages/network_info_plus) → FFI `owalkie_report_signal` |
| NAT punch on handoff | FFI `owalkie_punch_nat` |

### 4c. Audio routing and interruptions

| Task | Solution |
|------|----------|
| `MODE_IN_COMMUNICATION` | Existing `MainActivity.kt` channel |
| BT headset route | [`audio_session`](https://pub.dev/packages/audio_session) + optional Android channel |
| Pause relay on phone call | [`audio_session`](https://pub.dev/packages/audio_session) `interruptionEventStream` (prefer over `READ_PHONE_STATE`) |
| Mic source, warm mic, disable NS/AGC | **Custom** native audio layer |

### 4d. Media keys (PTT toggle)

| Task | Plugin | Note |
|------|--------|------|
| Headset play/pause → toggle PTT | [`audio_service`](https://pub.dev/packages/audio_service) | Long-press not available on OS |
| Foreground hardware keys | Extend `MainActivity.kt` | Hold-PTT from keyboard |

### Acceptance tests (Phase 4)

- [ ] Connect → home button → mic stays alive 2+ min; ongoing notification visible
- [ ] Notification disconnect action works
- [ ] Switch Wi‑Fi ↔ mobile → reconnect without app restart
- [ ] Incoming phone call pauses TX/RX; resume after call
- [ ] BT headset: route + media play/pause toggles latch; hardware PTT hold works
- [ ] Battery optimization prompt opens system screen

---

## Phase 5 — Deep links, share, scan

| Task | Plugin |
|------|--------|
| `owalkie://connect/<base64>` | [`app_links`](https://pub.dev/packages/app_links) |
| Share/import connection | [`share_plus`](https://pub.dev/packages/share_plus) |
| Channel scan | FFI `owalkie_check_channel_activity` + Dart orchestrator |
| Scan vibration | [`vibration`](https://pub.dev/packages/vibration) |

### Acceptance tests (Phase 5)

- [ ] `owalkie://connect/…` link fills profile (from browser / QR)
- [ ] Share / import connection via clipboard
- [ ] Scan finds activity on idle channel; continuous scan every ~10 s
- [ ] Auto-connect on scan hit (if enabled in UI)

---

## Phase 6 — Settings screen

Parity with `SettingsActivity.kt`: PTT modes, mic/BT/warm mic, pause on call, pattern pickers, orientation, About.

**External control (Tasker):** **custom** `BroadcastReceiver` — no suitable pub.dev package; mirror `ExternalControlReceiver.kt`.

Note: no “screen PTT toggle mode” checkbox — see [PTT policy](#ptt-input-policy-fixed--no-ptt-toggle-mode-setting).

### Acceptance tests (Phase 6)

- [ ] Mic source / warm mic / BT route settings affect capture
- [ ] Tasker intents: PTT, connect, next profile (Android)
- [ ] Roger/Call pattern pickers open editor
- [ ] Pause-on-call toggle works with real cellular call

---

## Phase 7 — iOS

| Task | Approach |
|------|----------|
| Full session (Boost/Opus) | vcpkg iOS + CMake |
| Background audio | `UIBackgroundModes: audio` |
| Deep links | `app_links` |
| Media keys | `audio_service` (same OS limits) |

### Acceptance tests (Phase 7)

- [ ] iPhone: full connect + PTT + RX (not utilities-only)
- [ ] Background audio: screen off, TX/RX still work with relay
- [ ] Deep link opens app with profile

---

## Phase 8 — Windows desktop

| Task | Plugin |
|------|--------|
| Global PTT hotkey | [`hotkey_manager`](https://pub.dev/packages/hotkey_manager) |
| Tray / window | [`tray_manager`](https://pub.dev/packages/tray_manager) / [`window_manager`](https://pub.dev/packages/window_manager) |
| UI tones | `audioplayers` / `just_audio` |

### Acceptance tests (Phase 8)

- [ ] Global hotkey PTT works when app in background
- [ ] Tray icon: show/hide, exit cleanly
- [ ] In-app Space + system hotkey do not conflict

---

## Phase 9 — Polish and release

Release signing, CI, git-tag versioning, expanded a11y tests, README feature flag.

### Acceptance tests (Phase 9)

- [ ] Release APK / exe install on clean device
- [ ] Version matches git tag in About
- [ ] CI green; full manual smoke on Android + Windows

---

## Plugin vs custom code

| Area | Plugin | Custom |
|------|--------|--------|
| Relay WS/UDP/Opus/PTT | — | `owalkie_core` |
| Profiles / settings | `shared_preferences` | domain layer |
| Android FGS | `flutter_foreground_task` | Kotlin service fallback |
| Network handoff | `connectivity_plus` | `setprocnetwork` channel |
| Phone call pause | `audio_session` | — |
| Media key toggle PTT | `audio_service` | foreground key dispatch |
| UI WAV | `audioplayers` | — |
| Roger/Call uplink | — | FFI to core |
| Tasker API | — | BroadcastReceiver |
| Windows global PTT | `hotkey_manager` | — |

---

## Recommended order

1. **Phase 0** — protocol events + FFI gaps  
2. **Phase 1** — profiles + Settings shell  
3. **Phase 2** — PTT UX  
4. **Phase 3** — Roger/Call + UI sounds  
5. **Phase 4** — Android FGS (required for background mic)  
6. **Phase 5** — deep links + scan  
7. **Phases 7–8** — iOS session + Windows hotkey in parallel  

After Phase 4 the Flutter client is viable for daily Android use; Phases 5–6 complete power-user parity.
