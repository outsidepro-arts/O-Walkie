# O-Walkie Flutter — iOS

iOS builds on macOS with Xcode. Without vcpkg iOS deps the app runs **utilities-only** (`sessionSupported` false). With deps installed, the relay session (Boost/Opus) links statically into the app.

## Prerequisites (macOS)

- Xcode 15+ with iOS SDK
- CocoaPods (`brew install cocoapods`)
- Flutter stable
- CMake 3.14+ (`brew install cmake`)

## Full session (vcpkg)

```bash
cd flutter-client/ios/scripts
chmod +x build-ios-deps.sh
./build-ios-deps.sh

export VCPKG_ROOT=~/vcpkg   # or your clone path
export OWALKIE_FLUTTER_FULL_SESSION=ON
```

Triplets installed: `arm64-ios`, `arm64-ios-simulator`, `x64-ios-simulator`.

## Build (simulator, no codesign)

```bash
cd flutter-client
flutter pub get
cd ios && pod install && cd ..
export VCPKG_ROOT=~/vcpkg   # optional; omit for utilities-only
flutter build ios --debug --simulator --no-codesign
```

Or:

```bash
./tool/build_ios.sh --simulator
```

## Build (device)

Requires Apple Developer signing in Xcode (`Runner` → Signing & Capabilities).

```bash
./tool/build_ios.sh
flutter run -d <iphone-id>
```

## Native plugin

`packages/owalkie_core/ios/owalkie_core.podspec` runs `build_native.sh` before compile:

- CMake builds static `libowalkie_core.a`
- Linked with `-force_load`; Dart uses `DynamicLibrary.process()`
- When `VCPKG_ROOT` is set, CMake uses the matching iOS vcpkg triplet for the active SDK/arch

## App capabilities (Phase 7)

- **Background audio:** `UIBackgroundModes` → `audio` in `Info.plist`
- **Deep links:** `owalkie://connect/…` via `CFBundleURLTypes` + Dart `app_links`
- **Mic permission:** `NSMicrophoneUsageDescription`
- **LAN relay:** `NSLocalNetworkUsageDescription`, `NSAllowsLocalNetworking`

## Media keys

Headset remote play/pause is limited on iOS (no Android-style `MediaSession`). On-screen PTT and keyboard Space still work.

Bundle ID: `ru.outsideproarts.owalkie.flutter`
