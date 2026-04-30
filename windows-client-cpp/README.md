# O-Walkie Desktop C++ (wxWidgets)

Windows desktop client in C++ with:
- `wxWidgets` UI
- `Boost.Beast` WebSocket + UDP transport
- `Opus` codec
- **miniaudio** (vendored `third_party/miniaudio/miniaudio.h`) for capture/playback — WASAPI on Windows

## Toolchain location

C++ tooling (example layout):
- `C:\dev\msys64`

Installed UCRT64 packages (typical):
- `mingw-w64-ucrt-x86_64-cmake`
- `mingw-w64-ucrt-x86_64-ninja`
- `mingw-w64-ucrt-x86_64-wxwidgets3.2-msw`
- `mingw-w64-ucrt-x86_64-opus`
- `mingw-w64-ucrt-x86_64-boost`
- `mingw-w64-ucrt-x86_64-nlohmann-json`

PortAudio is **not** required.

## Build

From PowerShell:

```powershell
& "C:/dev/msys64/usr/bin/bash.exe" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; cd /d/progworkspace/Vibecoding/O-Walkie/windows-client-cpp; cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build"
```

Outputs:
- `windows-client-cpp/build/owalkie-desktop-cpp.exe` — build tree
- `windows-client-cpp/build/dist/` — **same exe plus MinGW/UCRT64 DLL dependencies** copied next to it (via `scripts/bundle-ucrt64-deps.sh` + `objdump`). Run the app from `dist/` for distribution.

If MSYS2 is not under `C:/dev/msys64`, set CMake cache variable `OWALKIE_MSYS_ROOT` to your install (used only for the bundling step’s `bash.exe` path).

## Current stage

- Main screen: connection fields, repeater, PTT, level meter, miniaudio input/output device list + persistence in `connection.json`
- Protocol: WS welcome, `join`, `udp_hello`, `repeater_mode`, UDP keepalive, UDP `tx_eof` burst
- Welcome-driven Opus/sample rate reconfiguration; reopening playback on timing/device change

Next:
1. Multi-profile / settings parity with Android
2. Reconnect + diagnostics
3. Keyboard-accessible PTT and richer status (Tx/Rx)
