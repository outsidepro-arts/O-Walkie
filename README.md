# O-Walkie

[![CI](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml/badge.svg)](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**This project is built in full collaboration with LLM models and is a vibe-coding project.**

O-Walkie is a decentralized lo-fi walkie-talkie platform.  
The project focuses on low latency, practical PTT workflow and atmospheric radio artifacts instead of hi-fi voice quality.

Информация на русском языке: [README.ru.md](README.ru.md)

## First release status

This is the first public release of the project.

- External pull requests are welcome.
- Issues and discussions are welcome.
- Protocol and UX are still evolving.

## Why O-Walkie?

O-Walkie intentionally simulates imperfect radio behavior:

- noise floor and signal degradation
- squelch behavior and tails
- TX clicks and distortion
- configurable DSP chain on relay side

This design gives a more "alive" radio feeling for communication.

## Project components

- `backend/` - Go relay server (`WebSocket` control plane + `UDP` audio plane).
- `android/` - Android client (release target).
- `windows-client/` - Windows WPF client (working prototype for testing, not release target yet).

## What we publish

Public release artifacts currently include:

- relay server build (`backend`)
- Android client build (`android`)

Windows client is available in repository as a prototype and test client.

## Quick start

### Relay server

See full instructions in `backend/README.md`.

### Android client

Run from `android/`:

- `./gradlew :app:assembleDebug`
- `./gradlew :app:assembleRelease`

## Contribution

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## Contacts

- Email: `denis.outsidepro@gmail.com`
- Telegram: [@outsidepro](https://t.me/outsidepro)

## License

Project license: [MIT](LICENSE).

## Third-party licenses

This project depends on third-party libraries with their own licenses.  
When distributing binaries, ensure compliance with dependency license terms.
