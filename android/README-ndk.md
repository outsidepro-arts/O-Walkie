# Android NDK (owalkie-core)

Native relay links `owalkie_jni` → `owalkie-core` (Boost.Beast + libopus).

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
Opus encode/decode runs in that library (not the kopus AAR) when `owalkie.buildNativeRelay=true`.

Install NDK 27 if needed: Android Studio SDK Manager → NDK (Side by side) → 27.2.x, or let Gradle
download it on the first native build.

## Build APK

In `android/gradle.properties`:

```properties
owalkie.buildNativeRelay=true
```

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

## Fallback

`owalkie.buildNativeRelay=false` — legacy OkHttp/Datagram (no NDK deps).
