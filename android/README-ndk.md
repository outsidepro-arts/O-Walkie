# Android client

Relay transport is **always** native: `libowalkie_jni.so` → `owalkie-core` (Boost.Beast + libopus inside core).  
See [NDK build](#ndk-owalkie-core) below for vcpkg, 16 KB pages, and ABI triplets.

## Features

### Channel scan

- Main screen **Scan** toggle (off: «Scan» / on: «Scanning…»).
- Enabling opens a mode picker: **One-time** (stops after switching to the found profile) or **Continuous** (keeps scanning until turned off).
- Probes other saved profiles via `OwalkieNative.checkChannelActivity()` → `owalkie_check_channel_activity` (`has_activity` on the relay).
- Skips the **currently connected** profile while `wsConnected`.
- On activity elsewhere:
  - **Idle** (no RX and no TX on the current link) → select profile and connect.
  - **Active RX or TX** → toast with profile name + medium vibration; scan continues.

### Busy mode PTT

- PTT lock/unlock follows server `ptt_lock` / `ptt_unlock` only (no client-side RX heuristics).
- Decorative countdown on the PTT button while locked.
- **TalkBack:** countdown seconds read on focus (not auto-announced every tick); short «Locked» / «Unlocked» when accessibility focus is on PTT at the moment of change.

### Settings

- **Keep microphone ready** — optional warm `AudioRecord` pool while the main activity is foreground and connected (faster PTT start).
- Hardware PTT binding (`keyCode` + `scanCode`), toggle vs hold-to-talk, Bluetooth headset route, RX volume, screen orientation, Roger pattern editor — see in-app Settings.

### Reconnect

`WalkieService` gates reconnect on local `NET_CAPABILITY_VALIDATED` (no native reachability API):

1. `nativePrepareConnection` — allocate session id (no network)
2. `nativeConnect(sessionId, timeoutMs)` — first connect and every retry on the same id
3. On `EV_CONNECTION_LOST` — client loop calls `nativeConnect` again (backoff 1.5s → 8s)

`bindProcessToActiveNetwork()` runs before each connect attempt.

## NDK (owalkie-core)

### vcpkg (system install)

Default location: **`C:\dev\vcpkg`** (on `PATH`, `VCPKG_ROOT`).

Binary cache: **`C:\dev\vcpkg-bincache`** (`VCPKG_DEFAULT_BINARY_CACHE`).

One-time setup (new machine):

```powershell
# Optional: install vcpkg to C:\dev\vcpkg
git clone --depth 1 https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# User environment (new terminals pick this up)
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
[Environment]::SetEnvironmentVariable("VCPKG_DEFAULT_BINARY_CACHE", "C:\dev\vcpkg-bincache", "User")
# Add vcpkg to PATH if not already:
# ...\User Path += ;C:\dev\vcpkg

cd android
.\scripts\build-ndk-deps.ps1
```

### 16 KB page size (Pixel / Android 15+)

The app uses **NDK r27** (`ndkVersion` in `app/build.gradle.kts`) with
`-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` and 16 KB ELF link flags on `libowalkie_jni.so`.

Install NDK 27 if needed: Android Studio SDK Manager → NDK (Side by side) → 27.2.x, or let Gradle
download it on the first native build.

### Build APK

```bash
cd android
./gradlew :app:assembleDebug
```

| ABI | vcpkg triplet |
|-----|----------------|
| arm64-v8a | arm64-android |
| armeabi-v7a | arm-neon-android |
| x86_64 | x64-android |

Override vcpkg path: set **`VCPKG_ROOT`** before building.
