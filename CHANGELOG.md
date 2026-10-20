# Changelog

All notable changes to this project. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). Nothing has been released yet.

## [Unreleased]

### Added

- `SR_AUTOPLAY=1`: a scripted controller (A and Y, alternating every 4 s) layered on the SDK's
  real input, so test runs need nobody at the keyboard.
- `Setup.cmd` / `tools/setup.ps1`: one-shot setup that checks prerequisites, extracts the disc,
  builds the SDK and the game, and leaves `Play.cmd`.
- `saintsrow_crash.log`: the faulting stack of a fatal crash, as module+offset.
- `CHANGELOG.md`, `ROADMAP.md`, `CONTRIBUTING.md`, `docs/architecture.md`.
- Six vtable-only `li r4,N; b` stubs at `0x8221EC08..` to the manifest (Player Creation calls them).

### Changed

- Rebased onto ReXGlue `development` (`bd833a2`, 0.10.0.28-dev). The four SDK fixes live in a
  fork, [sp00nznet/rexglue-sdk](https://github.com/sp00nznet/rexglue-sdk) branch `saintsrow`,
  instead of a patch file. ReXGlue doesn't take AI-written PRs, so they aren't sent upstream.
- The 0.7-era game overrides in `stubs.cpp` and the null-page exception handler are off by
  default (`SAINTSROW_LEGACY_STUBS=OFF`). Without them the game renders correctly and reaches
  Player Creation; with them it stalled on the cathedral loading screen.
- README rewritten around Getting Started; the dated investigation notes moved to
  `docs/progress-log.md`.
- `LICENSE` is plain MIT again; its scope note moved to the README.

### Fixed

- 38 manifest entries that were not functions (jump-table case bodies and shared epilogue
  tails, picked up by a too-loose thunk scan). They split their host functions, and
  `sub_8222F7F8` died on `Unresolved branch from 0x8222FAB8 to 0x8222F8AC`.
- A literal NUL byte in the README.

### Removed

- Committed CMake build directories (`project/build-debug`, `build2`..`build4`) and their logs.
- The XenonRecomp-era `config/` files, superseded by the ReXGlue manifest.
- Old screenshots that showed since-fixed colour bugs.
- From the SDK tree: a draw trace from another project and a temporary IO probe.

## History before the changelog

Before 2026-10-20 the README carried a dated progress log instead; it is preserved in
[docs/progress-log.md](docs/progress-log.md). Milestones from it: first boot and D3D12 output
(2026-05), the ReXGlue 0.10.0 rebase and reaching the main menu (2026-09-04), and the
dispatch-header duplication fix (2026-09-05).
