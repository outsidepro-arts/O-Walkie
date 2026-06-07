# Android NDK (owalkie-core)

Relay transport is **always** native: `libowalkie_jni.so` → `owalkie-core` (Boost.Beast + libopus inside core).

## vcpkg (system install)

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

## 16 KB page size (Pixel / Android 15+)

The app uses **NDK r27** (`ndkVersion` in `app/build.gradle.kts`) with
`-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` and 16 KB ELF link flags on `libowalkie_jni.so`.

Install NDK 27 if needed: Android Studio SDK Manager → NDK (Side by side) → 27.2.x, or let Gradle
download it on the first native build.

## Build APK

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

## Client-owned reconnect (Android)

`WalkieService` gates reconnect attempts on local `NET_CAPABILITY_VALIDATED` (no native reachability API):

1. `nativePrepareConnection` — allocate session id (no network)
2. `nativeConnect(sessionId, timeoutMs)` — first connect and every retry on the same id
3. On `EV_CONNECTION_LOST` — client loop calls `nativeConnect` again (backoff 1.5s → 8s)

`bindProcessToActiveNetwork()` runs before each connect attempt.
