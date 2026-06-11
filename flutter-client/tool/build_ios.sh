#!/usr/bin/env bash
# Builds the Flutter iOS app (macOS + Xcode required).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIMULATOR=false
CONFIG=Debug

while [[ $# -gt 0 ]]; do
  case "$1" in
    --simulator) SIMULATOR=true; shift ;;
    --release) CONFIG=Release; shift ;;
    --full-session)
      export OWALKIE_FLUTTER_FULL_SESSION=ON
      if [[ -z "${VCPKG_ROOT:-}" ]]; then
        echo "warning: --full-session requires VCPKG_ROOT (see ios/scripts/build-ios-deps.sh)" >&2
      fi
      shift
      ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

cd "${ROOT}"
flutter pub get

if [[ -d ios ]]; then
  (cd ios && pod install)
fi

if [[ "${CONFIG}" == "Release" ]]; then
  MODE_FLAG=--release
else
  MODE_FLAG=--debug
fi

if [[ "${SIMULATOR}" == true ]]; then
  flutter build ios ${MODE_FLAG} --simulator --no-codesign
else
  flutter build ios ${MODE_FLAG} --no-codesign
fi

echo ""
echo "Built iOS ${CONFIG} ($(if ${SIMULATOR}; then echo simulator; else echo device; fi))."
echo "Open ios/Runner.xcworkspace in Xcode for signing and run."
