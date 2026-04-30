#!/usr/bin/env bash
# Copy MinGW/UCRT64 DLL dependencies next to the executable (recursive).
set -euo pipefail
EXE="${1:?path to .exe}"
DEST="${2:?dist directory}"
UCRT_BIN="${UCRT64_BIN:-/ucrt64/bin}"
mkdir -p "$DEST"
cp -f "$EXE" "$DEST/"
NAME="$(basename "$EXE")"

declare -A seen
queue=()

add_dll() {
    local d="$1"
    [[ -z "$d" || "$d" == *:* ]] && return
    [[ -n "${seen[$d]+x}" ]] && return
    seen[$d]=1
    if [[ -f "$UCRT_BIN/$d" ]]; then
        cp -f "$UCRT_BIN/$d" "$DEST/"
        queue+=("$d")
    fi
}

while IFS= read -r line; do
    dll="${line##* }"
    dll="${dll//$'\r'/}"
    [[ "$dll" == *.dll ]] && add_dll "$dll"
done < <(objdump -p "$EXE" 2>/dev/null | grep "DLL Name" || true)

idx=0
while (( idx < ${#queue[@]} )); do
    dll="${queue[idx]}"
    ((idx++)) || true
    f="$DEST/$dll"
    [[ -f "$f" ]] || continue
    while IFS= read -r line; do
        dep="${line##* }"
        dep="${dep//$'\r'/}"
        [[ "$dep" == *.dll ]] && add_dll "$dep"
    done < <(objdump -p "$f" 2>/dev/null | grep "DLL Name" || true)
done

echo "Bundled to $DEST : $NAME and dependencies from $UCRT_BIN"
