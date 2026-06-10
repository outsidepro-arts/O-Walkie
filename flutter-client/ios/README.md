# O-Walkie Flutter — iOS build (scaffold)

iOS support is scaffolded for Xcode/CI visibility. The UI and FFI plugin compile; **full relay session** (Boost/Opus via vcpkg) is not wired yet — the app runs in utilities-only mode (`sessionSupported` false) until deps are added.

## Prerequisites (macOS)

- Xcode 15+ with iOS SDK
- CocoaPods (`brew install cocoapods`)
- Flutter stable
- CMake 3.14+ (`brew install cmake`)

## Build (simulator, no codesign)

```bash
cd flutter-client
flutter pub get
cd ios && pod install && cd ..
flutter build ios --debug --simulator --no-codesign
```

Or:

```bash
./tool/build_ios.sh --simulator
```

## Build (device)

Requires Apple Developer signing configured in Xcode (`Runner` target → Signing & Capabilities).

```bash
./tool/build_ios.sh
flutter run -d <iphone-id>
```

## Native plugin

`packages/owalkie_core/ios/owalkie_core.podspec` runs `build_native.sh` before compile:

- CMake builds **static** `libowalkie_core.a` (utilities + miniaudio; session OFF)
- Linked with `-force_load` into the app; Dart uses `DynamicLibrary.process()`

## Enabling full session (future)

1. Install vcpkg iOS triplets (`arm64-ios`, `x64-ios` / simulator) with `boost-beast` and `opus`
2. Extend `packages/owalkie_core/src/CMakeLists.txt` `APPLE` branch (mirror Android vcpkg pin)
3. Build with `OWALKIE_FLUTTER_FULL_SESSION=ON` when invoking `build_native.sh`

Bundle ID: `ru.outsideproarts.owalkie.flutter` (installs alongside native Android/Kotlin app on other platforms; on iOS only this Flutter build exists).
