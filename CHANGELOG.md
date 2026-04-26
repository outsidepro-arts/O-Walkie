# Changelog

All notable changes to this project will be documented in this file.

This project follows a lightweight Keep a Changelog style and Semantic Versioning for public releases.

## [Unreleased]

### Added
- _(add new features here)_

### Changed
- _(add behavior/config changes here)_

### Fixed
- _(add bug fixes here)_

## [v0.1.1] - 2026-04-26

### Added
- Release process automation baseline:
  - release notes configuration (`.github/release.yml`)
  - tag-triggered release workflow (`.github/workflows/release-on-tag.yml`)
- Changelog template for future public releases.

## [v0.1.0] - 2026-04-26

### Added
- First public release published to GitHub.
- MIT license, bilingual root documentation and contribution templates.
- CI workflow for backend, Android and Windows prototype build validation.

### Changed
- Client profile logic no longer relies on hardcoded local relay targets.
- Android and Windows clients now support empty saved profile lists safely.
