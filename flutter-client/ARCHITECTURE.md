# O-Walkie Flutter client — architecture (experiment)

Feature phases and plugin choices: [ROADMAP.md](ROADMAP.md).

## PTT input policy

| Input | Behavior |
|-------|----------|
| On-screen button | Hold = push-to-talk; slide up while holding = latch; tap when latched = stop (`ptt_gesture_button.dart`) |
| Hardware PTT | Hold-to-talk only |
| Media play/pause | Toggle latch (planned) |
| Space / a11y actions | Toggle latch |

No user setting for “screen PTT toggle-only mode” (unlike legacy Kotlin checkbox).

## Text fields and TalkBack

Connection fields keep text in `TextEditingController` until Save/Connect (like Kotlin
inputs). Do **not** sync every `onChanged` into Riverpod — that caused controller
rewrites and fragmented duplication with TalkBack braille input.

Use `labelText` + `helperText` only (no `hintText` in semantics) to reduce risk of
label/hint being merged into committed text ([flutter#113457](https://github.com/flutter/flutter/issues/113457)).

## Layout

```
flutter-client/
  lib/                    # Dart UI + orchestration (single codebase)
  packages/owalkie_core/  # FFI plugin → ../../../owalkie-core (C/C++)
  ARCHITECTURE.md
```

Go relay and `owalkie-core` protocol stay unchanged.

## Layers (target)

| Layer | Package / location | Responsibility |
|-------|-------------------|----------------|
| **UI** | `lib/features/*` | Screens, widgets, accessibility |
| **App state** | `flutter_riverpod` | View models, connection/PTT state |
| **Domain** | `lib/domain/` *(next)* | Server profiles, scan policy, PTT rules |
| **Relay session** | `packages/owalkie_core` | FFI to `owalkie_prepare_connection`, `connect`, `tx_submit`, callbacks |
| **Platform** | Future plugins | Audio I/O, FGS, hotkeys, BT route |

## Plugin: `owalkie_core`

- **Now:** session transport + miniaudio RX/TX in native code; Dart **background isolate** (`session_worker.dart`) owns all FFI calls; UI talks via `SessionService` + `SendPort`.
- **Android (default):** utilities-only unless vcpkg NDK deps installed; full session: `android/scripts/build-ndk-deps.ps1` + `OWALKIE_FLUTTER_FULL_SESSION=ON`.
- **Windows (default):** session ON when `VCPKG_ROOT` set and `boost-beast`/`opus` installed for `x64-windows`.
- **iOS (scaffold):** Xcode project + CocoaPods CMake static lib; **utilities-only** until vcpkg iOS triplets wired (`flutter-client/ios/README.md`).
- **Next:** extend `ffigen` to `include/owalkie_core.h` managed-session API; register native callbacks → Dart `SendPort` (dedicated isolate, mirror `NativeRelayBridge`).

Suggested future plugins (pub.dev):

| Need | Plugin direction |
|------|------------------|
| Profiles / settings | `shared_preferences` |
| Deep links `owalkie://` | `app_links` |
| Share/import connection | `share_plus`, `clipboard` |
| Android FGS + wake | `flutter_foreground_task` or thin Kotlin service + channel |
| Low-latency audio | custom `owalkie_audio` plugin (AudioRecord/Track, WASAPI) |
| Windows global PTT | custom `owalkie_hotkey` (LL hook) |
| i18n | `flutter gen-l10n` (replace `app_strings.dart`) |
| Settings / about | `package_info_plus`, `url_launcher` |

## Session flow (planned)

```mermaid
sequenceDiagram
  participant UI as Flutter UI
  participant VM as Riverpod controller
  participant FFI as owalkie_core FFI
  participant Core as owalkie-core
  participant Srv as Go relay

  UI->>VM: Connect / PTT / volume
  VM->>FFI: prepare + connect + tx_submit
  FFI->>Core: C API
  Core->>Srv: WS + UDP
  Core-->>FFI: on_session_event / on_rx_pcm
  FFI-->>VM: Stream (isolate port)
  VM-->>UI: rebuild
```

Audio path: **do not** decode/play PCM on UI isolate — native callback → audio plugin queue (same as `WalkieService` + `AudioTrack`).

## Parity map (Kotlin → Flutter)

| Kotlin | Flutter (planned) |
|--------|-------------------|
| `WalkieService` | `SessionOrchestrator` (Dart) + `owalkie_platform` plugin |
| `NativeRelayBridge` | `packages/owalkie_core` Dart facade |
| `MainActivity` | `HomeScreen` |
| `SettingsActivity` | `SettingsScreen` + `go_router` |
| `ServerStore` | `shared_preferences` + `domain/server_profile.dart` |

## Build

```powershell
$env:Path = "C:\dev\flutter\bin;C:\dev\jdk\bin;" + $env:Path
$env:ANDROID_HOME = "C:\dev\android-sdk"
cd D:\progworkspace\Vibecoding\O-Walkie\flutter-client
flutter pub get
flutter build apk --debug
flutter build windows
# iOS (macOS): tool/build_ios.sh --simulator
```

Android NDK deps: same as Kotlin client (`android/scripts/build-ndk-deps.ps1` if vcpkg triplets missing).
