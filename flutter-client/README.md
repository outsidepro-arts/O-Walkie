# O-Walkie Flutter client (experiment)

Cross-platform shell for O-Walkie: **Flutter UI** + **`owalkie-core` FFI plugin**.

## Prerequisites

- Flutter SDK at `C:\dev\flutter` (stable)
- Android SDK at `C:\dev\android-sdk`
- Optional: vcpkg NDK deps for full session (`android/scripts/build-ndk-deps.ps1` + `OWALKIE_FLUTTER_FULL_SESSION=ON`)
- Windows: enable **Developer Mode** *or* run `tool/setup_plugin_junctions.ps1` before build (junction workaround)
- Visual Studio Build Tools with **Desktop development with C++** (Windows desktop)

## Quick start

```powershell
$env:Path = "C:\dev\flutter\bin;C:\dev\jdk\bin;" + $env:Path
$env:ANDROID_HOME = "C:\dev\android-sdk"
cd D:\progworkspace\Vibecoding\O-Walkie\flutter-client
flutter pub get

# All platforms (Windows dist + Android debug APK + ADB install):
$env:VCPKG_ROOT = "C:\dev\vcpkg"
powershell -File tool\build_all.ps1

# Windows only:
powershell -File tool\build_all.ps1 -SkipAndroid

# Android only (build + install):
powershell -File tool\build_all.ps1 -SkipWindows

# First-time Android native session (long vcpkg step):
powershell -File tool\build_all.ps1 -PrepareAndroidNdk
```

Android **applicationId**: `ru.outsidepro_arts.owalkie.flutter` (launcher: **O-Walkie Flutter**) — installs next to native `ru.outsidepro_arts.owalkie`.

**iOS** bundle ID: `ru.outsideproarts.owalkie.flutter` — see [ios/README.md](ios/README.md) (build on macOS; session scaffold utilities-only until vcpkg ios deps).

Android builds **auto-enable session transport** when vcpkg NDK deps are installed (`android/scripts/build-ndk-deps.ps1`). `build_all.ps1` sets `OWALKIE_FLUTTER_FULL_SESSION=ON` and clears native CMake cache before each Android build.

### Individual targets

```powershell
# Windows (requires VCPKG_ROOT=C:\dev\vcpkg for session transport):
powershell -File tool\build_windows.ps1 -Configuration Release
# Run from (includes opus.dll):
#   build\dist\windows-release\owalkie_app.exe

# After `flutter run -d windows` (Debug), if DLL load fails, stage opus.dll:
#   powershell -File tool\stage_runtime.ps1

flutter run -d windows
flutter run -d <device-id>
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for layering and roadmap.
