# Flutter client — migration roadmap (Android → Flutter)

Experimental Flutter client targeting parity with the native Kotlin app (`android/`) and Windows client (`windows-client-cpp/`). Go relay and `owalkie-core` protocol stay unchanged.

**Principle:** relay transport, Opus, PTT uplink, RX PCM, and reconnect live in `packages/owalkie_core` → `owalkie-core`. Do **not** replace that stack with Dart audio/network plugins. Use pub.dev plugins for UI, persistence, navigation, and platform shell only. Custom plugins only when no suitable package exists or reviews show poor quality.

See also: [ARCHITECTURE.md](ARCHITECTURE.md).

## PTT input policy (fixed — no “PTT toggle mode” setting)

| Input | Behavior |
|-------|----------|
| **On-screen PTT button** | **Hold** = push-to-talk; **slide up while holding** = latch until tap; **tap when latched** = stop |
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
- [ ] Hold PTT → TX while finger down; release stops unless slid up to latch
- [ ] Slide up while holding PTT → latched TX (red button) until tap
- [ ] Tap latched PTT → TX stops
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
| i18n (ru/en) | `flutter gen-l10n` (`lib/l10n/app_*.arb`, system locale) | done |

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

## Phase 4 — Android: background, network, audio policy *(in progress — 4d media keys)*

### 4a. Foreground service + notification *(done)*

| Task | Solution |
|------|----------|
| FGS type `microphone` (API 34+) | Thin Kotlin `WalkieForegroundService` + method channel |
| POST_NOTIFICATIONS | `MainActivity` permission on connect |
| Notification actions | Disconnect/connect toggle + battery settings intent |
| Battery optimization hint | Notification action → system battery screen |

**Implementation:** `WalkieForegroundService.kt`, `PlatformEvents` EventChannel, `NativePlatform.startSessionForeground` wired from `HomeScreenController` on connect/disconnect.

### 4b. Network and reconnect *(done)*

| Task | Solution |
|------|----------|
| Network validated / lost | Kotlin `SessionNetworkController` + `ConnectivityManager.NetworkCallback` |
| Process network bind | `android_setprocnetwork` preConnect hook + `bindProcessToNetwork` |
| Uplink signal byte | Wifi RSSI + cell level → FFI `owalkie_report_signal` → UI chip `%` |
| NAT punch / UDP recover | FFI `punch_nat` + `recover_udp_transport` on real network handoff |

**Implementation:** persistent client reconnect loop, handle-based handoff debouncing, `SessionNetworkController` logging (`OwalkieFlutterNet`).

### 4c. Audio routing and interruptions *(done)*

| Task | Solution |
|------|----------|
| `MODE_IN_COMMUNICATION` / BT route | `AudioRouteHelper.kt` + `prepareAudioSession(bluetoothHeadset:)` |
| Pause relay on phone call | [`audio_session`](https://pub.dev/packages/audio_session) `interruptionEventStream` + `SessionPauseRelayCommand` |
| Settings toggles | Pause during call (default on), Bluetooth headset |
| Mic source, warm mic, disable NS/AGC | **Deferred** — custom native audio layer (Kotlin parity) |

### 4d. Media keys (PTT toggle)

| Task | Plugin | Note |
|------|--------|------|
| Headset play/pause → toggle PTT | [`audio_service`](https://pub.dev/packages/audio_service) | Long-press not available on OS |
| Foreground hardware keys | Extend `MainActivity.kt` | Hold-PTT from keyboard |

### Acceptance tests (Phase 4)

- [x] Connect → home button → mic stays alive 2+ min; ongoing notification visible
- [x] Notification disconnect action works
- [x] Battery optimization prompt opens system screen
- [x] Switch Wi‑Fi ↔ mobile → reconnect without app restart
- [x] Incoming phone call pauses TX/RX; resume after call
- [x] BT headset audio route (media PTT latch — 4d)
- [x] BT headset play/pause toggles latch; hardware PTT hold works (4d)

---

## Phase 5 — Deep links, share, scan

| Task | Plugin |
|------|--------|
| `owalkie://connect/<base64>` | [`app_links`](https://pub.dev/packages/app_links) |
| Share/import connection | [`share_plus`](https://pub.dev/packages/share_plus) |
| Channel scan | FFI `owalkie_check_channel_activity` + Dart orchestrator |
| Scan vibration | [`vibration`](https://pub.dev/packages/vibration) |

### Acceptance tests (Phase 5)

- [x] `owalkie://connect/…` link fills profile (`app_links` + Android intent filter)
- [x] Share / import connection via clipboard
- [x] Scan finds activity on idle channel; continuous scan every ~10 s (FFI probe loop)
- [x] Auto-connect on scan hit (one-shot and continuous modes)
- [ ] Device verification: deep link from browser, scan on LAN relay

---

## Phase 6 — Settings screen *(mostly done)*

Parity with `SettingsActivity.kt`: pause on call, BT route, media/hardware PTT, pattern pickers, orientation, About, Tasker API.

**Deferred:** mic source, warm mic, disable NS/AGC — custom native audio layer (same as Phase 4c).

Note: no “screen PTT toggle mode” checkbox — see [PTT policy](#ptt-input-policy-fixed--no-ptt-toggle-mode-setting).

### Acceptance tests (Phase 6)

- [ ] Mic source / warm mic settings affect capture *(deferred)*
- [x] BT route setting affects audio session
- [x] Tasker intents: PTT, connect, disconnect, next/prev profile (Android `ExternalControlReceiver`)
- [x] Roger/Call pattern pickers open editor
- [x] Pause-on-call toggle (Phase 4c; device-tested)
- [x] Tasker device verification on Pixel

---

## Phase 7 — iOS *(in progress)*

| Task | Approach |
|------|----------|
| Full session (Boost/Opus) | vcpkg iOS + CMake (`ios/scripts/build-ios-deps.sh`) |
| Background audio | `UIBackgroundModes: audio` |
| Deep links | `app_links` + `CFBundleURLTypes` |
| Media keys | OS-limited; deferred (`audio_service` optional) |

### Acceptance tests (Phase 7)

- [ ] iPhone: full connect + PTT + RX (after `build-ios-deps.sh` on macOS)
- [ ] Background audio: screen off, TX/RX still work with relay
- [x] Deep link scheme registered (`owalkie://` in Info.plist; Dart handler from Phase 5)
- [ ] Device verification on iPhone

---

## Phase 8 — Windows desktop

| Task | Plugin | Status |
|------|--------|--------|
| Global PTT hotkey (hold/release via `WH_KEYBOARD_LL`) | native `windows/runner/global_ptt_hook.*` | done |
| Tray / window close | [`tray_manager`](https://pub.dev/packages/tray_manager) / [`window_manager`](https://pub.dev/packages/window_manager) | done |
| UI tones | `audioplayers` / `just_audio` | deferred |

`lib/platform/windows/desktop_shell.dart`, `windows_settings_store.dart`, Settings → Windows section.

Global hotkey uses native low-level keyboard hook: **hold = TX on, release = TX off** (same model as `windows-client-cpp`).

### Acceptance tests (Phase 8)

- [ ] Global hotkey PTT works when app in background
- [ ] Tray icon: show/hide, exit cleanly
- [ ] In-app Space + system hotkey do not conflict

---

## Phase 9 — Polish and release

| Task | Status |
|------|--------|
| Git-tag versioning (`tool/version_from_git.*`, `--build-name` / `--build-number`) | done |
| Android release signing (shared `android/keystore/`, debug fallback) | done |
| CI: `flutter analyze` + `flutter test` | done |
| Release on tag: experimental Flutter debug APK asset | done |
| Expanded a11y widget tests | done |
| README experimental feature flag (root + `flutter-client/`) | done |

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
| Windows global PTT | — | `WH_KEYBOARD_LL` in `windows/runner/global_ptt_hook.*` |

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
