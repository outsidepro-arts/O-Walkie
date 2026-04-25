# O-Walkie Windows Client (Stage 2)

This folder contains a WPF desktop client scaffold for Windows.

## Current capabilities

- Basic UI for relay connection profile (`host`, `wsPort`, `udpPort`, `channel`)
- WebSocket control plane bootstrap (`join`, `udp_hello`, `repeater_mode`)
- UDP data plane receive/send with existing relay packet format
- Opus encode/decode using Concentus at `8 kHz mono`
- Capture pipeline using `WASAPI` (with `WaveInEvent` fallback)
- Playback pipeline using `WASAPI` output (with `WaveOutEvent` fallback)
- App settings persisted to `%APPDATA%/OWalkie/WindowsClient/settings.json`
- Server profile list in main window with save/delete
- Settings dialog for repeater mode, microphone backend, Roger/Calling preset IDs, and hardware PTT key mapping

## Build

```powershell
dotnet build windows-client/OWalkie.Windows.sln -c Debug
```

## Next stage focus

- Replace ID-based Roger/Calling selectors with full preset editor parity
- Implement local action sounds and Roger/Calling generator parity with Android client
- Add accessibility polish for screen readers (automation names, live region notifications)
- Add reconnect strategy + richer network diagnostics in desktop UI
