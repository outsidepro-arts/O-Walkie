# iOS build handoff (Phase 7)

Branch: `feature/flutter-client` (commits through `a24e89f` and later).

Target: **full relay session** on device/simulator, not utilities-only.

## Quick checklist

1. macOS with Xcode 15+, CocoaPods, Flutter stable, CMake (`brew install cmake cocoapods`)
2. Clone repo, checkout `feature/flutter-client`
3. `cd flutter-client && flutter pub get`
4. **Optional but required for connect/PTT:** install vcpkg iOS deps (one-time, long):
   ```bash
   cd flutter-client/ios/scripts
   chmod +x build-ios-deps.sh
   ./build-ios-deps.sh
   export VCPKG_ROOT=~/vcpkg
   export OWALKIE_FLUTTER_FULL_SESSION=ON
   ```
5. `cd flutter-client/ios && pod install && cd ..`
6. Build:
   ```bash
   ./tool/build_ios.sh --simulator --full-session
   # or device (signing in Xcode):
   ./tool/build_ios.sh --full-session
   flutter run -d <device-id>
   ```

## Verify session is enabled

- Home screen footer shows core version and **protocol version > 0**
- Connection chip is not **Session unavailable**
- Connect to LAN relay → PTT works

If `sessionSupported` is false, native build ran **without** vcpkg (`VCPKG_ROOT` unset or deps missing). Re-run `build-ios-deps.sh` and clean:

```bash
rm -rf packages/owalkie_core/ios/build
rm -rf ios/Pods ios/Podfile.lock
cd ios && pod install && cd ..
```

## Known minimal fixes (first successful build)

- **Pod / CMake cache:** delete `packages/owalkie_core/ios/build` after changing `VCPKG_ROOT` or `OWALKIE_FLUTTER_FULL_SESSION`
- **Signing:** open `ios/Runner.xcworkspace` → Runner → Signing & Capabilities → your Team
- **Bundle ID:** `ru.outsideproarts.owalkie.flutter` (differs from Kotlin Android package)
- **Local network:** first connect may prompt for local network access (Info.plist already has `NSLocalNetworkUsageDescription`)

## Deep links

Registered in `Info.plist` (`owalkie://connect/…`). Test from Notes/Safari:

```
owalkie://connect/<base64-payload>
```

Dart handler is shared with Android (Phase 5).

## Background audio

`UIBackgroundModes` → `audio` is set. Validate TX/RX with screen locked while connected.

## Pull requests

Please target `feature/flutter-client`. Small focused PRs (build fixes, iOS-specific platform code) are welcome. Note triplet/arch (`arm64-ios` vs `arm64-ios-simulator`) in PR description if touching CMake.

## Contact / context

- Roadmap: `flutter-client/ROADMAP.md` Phase 7
- Native CMake: `packages/owalkie_core/cmake/owalkie_ios_deps.cmake`
- Detailed build notes: [README.md](README.md)
