# Roadmap

## Next

- **Begin Game → the city.** Player Creation renders and takes input. The next step is pressing
  Begin Game and following the load into Stilwater.
- **The intermittent startup crash.** One of the first three bare-build runs segfaulted about 3 s after the
  guest started, before the logos. `saintsrow_crash.log` now captures the stack; it needs a repro.
- **Retire `stubs.cpp`'s legacy overrides.** They are compiled out by default
  (`SAINTSROW_LEGACY_STUBS=OFF`). Once nothing needs them for comparison, delete them, along with
  the null-page VEH in `main.cpp`.
- **Finish the manifest.** Keep adding vtable-only functions the scanner misses, as the
  "Call to invalid or unregistered function" log reports them, then revert the SDK patch that
  makes that error non-fatal.
- **Keep the SDK fork small.** Rebase `sp00nznet/rexglue-sdk` onto new upstream `development` and
  drop any fix upstream makes unnecessary. The fixes aren't sent upstream, because ReXGlue doesn't
  accept AI-written PRs.

## House-style gaps (repo rules, section 10)

- **Headless mode**, `--headless --record out.mp4`, so runs work over RDP. Test runs currently
  need the console session and a second monitor (`--monitor=2`, recorded with ffmpeg's `ddagrab`).
  This belongs in ReXGlue's presenter rather than here.
- **Conformance harness.** The obvious ground truth is a fixed `SR_AUTOPLAY` script with checkpoint
  frames (menu, loading screen, Player Creation) compared against reference captures, reporting
  how many checkpoints were reached. Reference frames can't be redistributed, so it must skip
  cleanly without them.
- **netlab builds.** recomp-netlab's builders cross-compile with clang-cl and an xwin splat; ReXGlue
  builds with the GNU-style clang driver and pulls in SDL3, FFmpeg and DXC. Until a builder can
  build ReXGlue, this project builds on the workstation.
- **CI.** Needs the same toolchain on a hosted runner. Without game data it can only check that
  the SDK and the runtime glue compile.

## Deferred

- Load Game, multiplayer (System Link / Xbox Live) and achievements: after the single-player path
  works.
- A `Setup.cmd` run from a truly clean machine (no LLVM, no VS Build Tools) has not been done yet;
  the resume path has.

## Out of scope

- Distributing the game, its generated C++, or any of its data.
- Saints Row 2 (see StilwaterReclaimed).
