# O-Walkie Desktop C++ (wxWidgets)

Fresh Windows desktop client implementation in C++ with:
- `wxWidgets` UI
- `Boost.Beast` WebSocket + UDP transport
- `Opus` codec
- `PortAudio` capture/playback

## Toolchain location

All required C++ tooling is installed under:
- `C:\dev\msys64`

Installed UCRT64 packages:
- `mingw-w64-ucrt-x86_64-cmake`
- `mingw-w64-ucrt-x86_64-ninja`
- `mingw-w64-ucrt-x86_64-wxwidgets3.2-msw`
- `mingw-w64-ucrt-x86_64-portaudio`
- `mingw-w64-ucrt-x86_64-opus`
- `mingw-w64-ucrt-x86_64-boost`
- `mingw-w64-ucrt-x86_64-nlohmann-json`

## Build

From PowerShell:

```powershell
& "C:/dev/msys64/usr/bin/bash.exe" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; cd /d/progworkspace/Vibecoding/O-Walkie/windows-client-cpp; cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build"
```

Output:
- `windows-client-cpp/build/owalkie-desktop-cpp.exe`

## Current stage

This is stage-1 migration:
- main screen and transport/audio pipeline are wired
- WS welcome handling, `join`, `udp_hello`, `repeater_mode`, UDP keepalive, UDP `tx_eof` burst are implemented
- baseline push-to-talk is implemented for desktop evaluation

Next stages:
1. profile persistence + settings page parity
2. robust reconnect state machine + diagnostics
3. full accessibility pass and keyboard-first UX
