#!/usr/bin/env bash
# Builds libowalkie_core.a for the active Xcode SDK/arch (device or simulator).
# Invoked from owalkie_core.podspec script_phase during `pod install` / Xcode build.
set -euo pipefail

IOS_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${IOS_DIR}/.." && pwd)"
BUILD_DIR="${IOS_DIR}/build/${PLATFORM_NAME:-iphoneos}-${CONFIGURATION:-Debug}"

if [[ "${PLATFORM_NAME:-iphoneos}" == "iphonesimulator" ]]; then
  SYSROOT=iphonesimulator
else
  SYSROOT=iphoneos
fi

ARCHS="${ARCHS:-arm64}"
if [[ "${PLATFORM_NAME:-}" == "iphonesimulator" && "${ARCHS}" == *"x86_64"* ]]; then
  ARCHS="x86_64"
fi

mkdir -p "${BUILD_DIR}"

cmake -S "${ROOT_DIR}/src" -B "${BUILD_DIR}" \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="${SYSROOT}" \
  -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_BUILD_TYPE="${CONFIGURATION:-Debug}"

cmake --build "${BUILD_DIR}" --target owalkie_core -- -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

LIB="${BUILD_DIR}/libowalkie_core.a"
if [[ ! -f "${LIB}" ]]; then
  echo "error: expected static library at ${LIB}" >&2
  exit 1
fi

mkdir -p "${IOS_DIR}/build"
cp "${LIB}" "${IOS_DIR}/build/libowalkie_core.a"
