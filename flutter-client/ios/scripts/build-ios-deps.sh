#!/usr/bin/env bash
# Builds Boost.Beast + Opus for iOS device and simulator via vcpkg (macOS only).
set -euo pipefail

VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/vcpkg}"
if [[ ! -d "${VCPKG_ROOT}" ]]; then
  echo "Cloning vcpkg into ${VCPKG_ROOT}..."
  git clone --depth 1 https://github.com/microsoft/vcpkg "${VCPKG_ROOT}"
fi

if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
  "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi

VCPKG="${VCPKG_ROOT}/vcpkg"
TRIPLETS=(arm64-ios arm64-ios-simulator x64-ios-simulator)

for triplet in "${TRIPLETS[@]}"; do
  echo "=== vcpkg install boost-beast opus (${triplet}) ==="
  "${VCPKG}" install boost-beast opus --triplet "${triplet}"
done

echo ""
echo "Done. Export before building:"
echo "  export VCPKG_ROOT=${VCPKG_ROOT}"
echo "  export OWALKIE_FLUTTER_FULL_SESSION=ON"
