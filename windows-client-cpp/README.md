# O-Walkie Desktop

Release Windows desktop client (`windows-client-cpp`):

- `wxWidgets` UI
- `owalkie-core` managed session (`RelayClient`) for WebSocket + UDP + Opus
- **miniaudio** (vendored `third_party/miniaudio/miniaudio.h`) for PCM capture/playback — WASAPI on Windows

## Toolchain location

Tooling (example layout):
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
- `windows-client-cpp/build/owalkie-desktop.exe` — build tree
- `windows-client-cpp/build/dist/` — **same exe plus MinGW/UCRT64 DLL dependencies** copied next to it (via `scripts/bundle-ucrt64-deps.sh` + `objdump`). Run the app from `dist/` for distribution.

If MSYS2 is not under `C:/dev/msys64`, set CMake cache variable `OWALKIE_MSYS_ROOT` to your install (used only for the bundling step’s `bash.exe` path).

## Current stage

- **Server profiles** in `%AppData%/…/profiles.json` (name, host, ports, channel, repeater); **audio** in `audio.json`. Legacy `connection.json` is migrated once on startup if `profiles.json` is missing.
- **Transport** via `owalkie-core` managed session (`RelayClient`); PCM capture/playback in `AudioEngine` (miniaudio).
- **Auto-reconnect** with exponential backoff (about 1.5s → 30s cap) after WS/UDP drop while the session was connected; **Disconnect** cancels retries.
- Main screen: profile picker + Save/New/Delete, connection fields, repeater, PTT, level meter, miniaudio device list.
- **Busy mode:** PTT lock/unlock from server `ptt_lock` / `ptt_unlock`; decorative countdown on the PTT button; unlock when core reports not locked (no local RX heuristic).
- **System tray:** optional **Minimize to system tray when closing the window** (General settings, `minimize_to_tray_on_close` in `config.json`). Close (X / Alt+F4) hides to tray; tray menu **Show** / **Exit**; tooltip shows app name, connection status, and profile name when connected.
- Protocol: WS welcome, `join`, `udp_hello`, `repeater_mode`, UDP keepalive, UDP `tx_eof` burst.
- Welcome-driven Opus/sample rate reconfiguration; reopening playback on timing/device change.

Not yet (vs Android):

- Channel scan / `has_activity` profile switching
- Global hotkey PTT

Next:

1. Settings and workflow parity with Android where it still differs
2. Richer status and diagnostics

### Keyboard / accessibility notes

- Controls live on a **`wxPanel` with `wxTAB_TRAVERSAL`** so Tab/Shift+Tab follow one chain (recommended over placing focusables directly on `wxFrame`).
- **`wxWindow::DisableFocusFromKeyboard()`** on decorative widgets (`wxStaticText`, level gauge, read-only status line) so Tab skips them — matches common wx + AT guidance ([wxWidgets accessibility overview](https://wxwidgets.org/docs/tutorials/accessibility/), forum threads on tab traversal).
- **`MoveAfterInTabOrder`** locks traversal to top-to-bottom field order.
- After Tab to **Hold to Talk**, **Space** holds transmit (same as mouse hold); labels use **`SetName`** for screen readers where helpful.
