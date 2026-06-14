# O-Walkie — Agent Instructions

## Project Overview

O-Walkie is a decentralized lo-fi walkie-talkie platform. Focus: low latency, practical PTT workflow, atmospheric radio artifacts (noise, squelch, TX clicks, distortion) instead of hi-fi quality. Built in full collaboration with LLMs (vibe-coding project).

## Components

| Directory | Language / Stack | Description |
|-----------|-----------------|-------------|
| `backend/` | Go (`cmd/relay`, `main.go`) | Relay server: WebSocket control + UDP audio plane |
| `owalkie-core/` | C/C++ | Shared relay library: managed sessions, Opus, activity probe, Roger/Call PCM |
| `android/` | Kotlin | Android client (release): foreground `WalkieService`, JNI -> `owalkie-core` |
| `windows-client-cpp/` | C++ (wxWidgets + miniaudio) | Windows desktop client (release): `owalkie-core` managed session |
| `flutter-client/` | Dart/Flutter | Experimental cross-platform client (FFI `owalkie_core`). Not production |

## Toolchain Paths (Host Machine)

All dev tools are installed under `C:\dev\`:

| Tool | Path |
|------|------|
| Go | `C:\dev\go` |
| MSYS2 UCRT64 | `C:\dev\msys64` (wx-config, msgfmt, GCC/Clang) |
| Android SDK | `C:\dev\android-sdk` |
| Gradle | `C:\dev\gradle` |
| JDK | `C:\dev\jdk` |
| Inno Setup | `C:\dev\InnoSetup` |
| vcpkg | `C:\dev\vcpkg` |
| VS 2022 Build Tools | `C:\dev\vs2022buildtools` |
| Flutter | `C:\dev\flutter` |

## Feature Parity Policy

All new user-facing or protocol-impacting functionality **must be implemented in both Android (Kotlin) and Windows (C++) clients** with parity unless explicitly scoped otherwise. Flutter client (`flutter-client/`) is experimental and not subject to mandatory parity until merged to master. Legacy WPF `windows-client` is removed.

## Tech Stack Details

- **Backend**: Go, single `package main` in `cmd/relay/`. Files: `config_*.go`, `hub.go`, `mixer.go`, `mixer_repeater.go`, `jitter.go`, `ws_server.go`, `udp_reader.go`, `audio_context.go`, `module_pipeline.go`, `gen_*.go`, `dsp_*.go`, `audio_helpers.go`.
- **owalkie-core**: C/C++ shared relay client library (v0.1.0). SessionManager, TX submit pipeline, activity probe, Opus, miniaudio. Public header: `include/owalkie_core.h`. Used by Android (JNI), Windows (direct), Flutter (FFI).
- **Android**: Kotlin, Gradle Kotlin DSL, Opus via owalkie-core (JNI), foreground service.
- **Windows**: C++ wxWidgets, Boost.Beast, miniaudio (vendored), gettext i18n (`wxLocale`, source lang English, translation: Russian).
- **Flutter**: Dart/Flutter, `flutter_riverpod`, `go_router`, FFI to owalkie-core via `packages/owalkie_core` plugin. Background isolate for session. Android, Windows, iOS scaffold.
- **Audio protocol**: Opus mono, server handshake with protocol/version/audio params. Single port for WS+UDP (`server.port`, default 5500).
- **Protocol version**: `welcome.protocolVersion` = **2** until next public GitHub release. New WS messages are additive.

## Debug Build Workflow (MANDATORY after code changes)

After **any code change** that touches a target, build and verify Debug before considering the task done.

### Windows (`windows-client-cpp`)

```powershell
cmake -G Ninja -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

Output: `windows-client-cpp/build-debug/`

### Android (`android/`)

```powershell
./gradlew :app:assembleDebug
# If device connected:
./gradlew :app:installDebug
adb shell monkey -p ru.outsidepro_arts.owalkie -c android.intent.category.LAUNCHER 1
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`

### owalkie-core

When core changes affect JNI/desktop, rebuild consuming targets. Optionally run tests:
```powershell
cmake --build build --target owalkie_core_tests
```

### Flutter (`flutter-client/`)

```powershell
$env:Path = "C:\dev\flutter\bin;C:\dev\jdk\bin;" + $env:Path
$env:ANDROID_HOME = "C:\dev\android-sdk"
cd flutter-client
flutter pub get
flutter build apk --debug
flutter build windows
```

Android NDK deps: `android/scripts/build-ndk-deps.ps1` if vcpkg triplets missing.

## Release Process

- Annotated semver tag: `vMAJOR.MINOR.PATCH`.
- **Android**: `versionCode` must be strictly greater than last published. Floor in `android/owalkie-version.properties`.
- **Windows**: `-DOWALKIE_VERSION_STRING=...` or env `OWALKIE_VERSION_STRING`.
- **Relay**: `-ldflags "-X main.buildVersion=MAJOR.MINOR.PATCH"`.
- **Flutter**: git-tag versioning (`tool/version_from_git.*`, `--build-name` / `--build-number`).
- GitHub Release artifacts: `owalkie-relay-*-linux-amd64`, `owalkie-android-*-debug.apk`. Windows exe is separate.

## Windows Client i18n Rules

- Source language: **English** (`_("English msgid")`).
- Translation: **Russian** (`windows-client-cpp/po/ru.po`).
- After adding strings: add English `msgid` + Russian `msgstr` in `ru.po`, then rebuild:
  ```
  msgfmt -o locale/ru/LC_MESSAGES/owalkie.mo po/ru.po
  ```
- `locale/ru/LC_MESSAGES/owalkie.mo` is copied next to exe in POST_BUILD.
- Language setting: `ui_language` (`en`|`ru`) in `config.json` (restart required).

## Configuration

- **Backend**: `backend/config.json`, optional path override via CLI argument.
- **Windows client**: `config.json` (legacy fallback `audio.json` — to be removed in future versions).
- **Android**: SharedPreferences via `*Store` classes.

## Key Conventions

- Follow existing style per component/language.
- Prefer readability over cleverness.
- Avoid unrelated refactoring in functional PRs.
- Commit messages: concise imperative style (`Fix Android profile navigation with empty lists.`).
- Never commit secrets, keys, tokens.
- Keep backward compatibility for relay protocol changes.
- Config: `config.json` is current; `audio.json` is legacy (auto-migrated, fallback read only).
- TODO (future): remove `audio.json` read fallback.

## Current Stage

- Late stabilization/integration before tester rollout.
- Core relay and client protocol implemented; focus on reliability, deployment safety, UX edge cases.
- Accessibility-based background PTT removed from `master` (Play Protect friction).
- Both Android and Windows in active feature + reliability iteration with parity checks.
- Experimental Flutter client on `feature/flutter-client` branch (phases 0–9 complete, not yet merged).

## Project Memory

Detailed project status, recent changes, and next steps are tracked in `.opencode/project_memory.md`. Read it when you need full context on what has been done and what's planned.

After completing each major subtask, **update `.opencode/project_memory.md`** with the current project status, recent changes, and next steps.
