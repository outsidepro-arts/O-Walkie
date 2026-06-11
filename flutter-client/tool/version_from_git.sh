#!/usr/bin/env bash
# Prints OWALKIE_VERSION_NAME and OWALKIE_VERSION_CODE from git tags (v*).
# Usage: eval "$(flutter-client/tool/version_from_git.sh)"  OR  source with exports.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

describe="$(git -C "${REPO_ROOT}" describe --tags --match 'v*' --long --dirty 2>/dev/null || true)"

strip_v() {
  local s="$1"
  if [[ "$s" == v* ]]; then
    echo "${s#v}"
  else
    echo "$s"
  fi
}

version_name="0.0.0-dev"
version_code="1"

if [[ -n "${OWALKIE_VERSION_NAME:-}" ]]; then
  version_name="${OWALKIE_VERSION_NAME}"
elif [[ -n "$describe" ]]; then
  version_name="$(strip_v "$describe")"
fi

if [[ -n "${OWALKIE_VERSION_CODE:-}" ]]; then
  version_code="${OWALKIE_VERSION_CODE}"
elif [[ -n "$describe" ]]; then
  if [[ "$describe" =~ ^v?([0-9]+)\.([0-9]+)\.([0-9]+)-([0-9]+)-g[0-9a-fA-F]+(-dirty)?$ ]]; then
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    patch="${BASH_REMATCH[3]}"
    offset="${BASH_REMATCH[4]}"
  else
    major=0; minor=0; patch=0; offset=1
  fi
  version_code=$(( major * 10000000 + minor * 100000 + patch * 1000 + offset ))
  if (( version_code < 1 )); then version_code=1; fi
  if (( version_code > 2100000000 )); then version_code=2100000000; fi
fi

floor_file="${REPO_ROOT}/android/owalkie-version.properties"
if [[ -f "$floor_file" && -z "${OWALKIE_VERSION_CODE:-}" ]]; then
  floor="$(grep -E '^versionCodeFloor=' "$floor_file" | cut -d= -f2 | tr -d '\r' || true)"
  if [[ "$floor" =~ ^[0-9]+$ ]] && (( floor > version_code )); then
    version_code="$floor"
  fi
fi

echo "export OWALKIE_VERSION_NAME=${version_name}"
echo "export OWALKIE_VERSION_CODE=${version_code}"
