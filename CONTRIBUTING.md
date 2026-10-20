# Contributing

Issues and pull requests are welcome: crash reports, missing manifest entries, runtime fixes,
documentation.

## Before you open a PR

- Build with the default options (no `SAINTSROW_LEGACY_STUBS`) and check that the game still
  reaches Player Creation with `SR_AUTOPLAY=1`. Say in the PR how far it got before and after.
- Keep game-specific code in this repo. Runtime fixes go to our fork,
  [sp00nznet/rexglue-sdk](https://github.com/sp00nznet/rexglue-sdk) (branch `saintsrow`). Please
  don't open AI-assisted PRs against upstream ReXGlue; its maintainers have asked not to get them.
- A manifest entry needs a reason in the comment above its group: how it was found, and why it is
  a function (who calls it).

## Never commit

- Anything from the game: the disc image, `extracted/`, `default.xex`, the generated C++ in
  `generated/`, memory dumps, or tables lifted from the binary.
- Build output, logs and crash dumps.

## Where your code comes from

Everything here is MIT, so contributions must be your own work or MIT-compatible,
with any third-party licence notice kept.

Don't port code from copyleft or unlicensed projects, even rewritten. The ones most likely to be
open next to a 360 recomp are **xemu, Cxbx-Reloaded, RPCS3, Dolphin and QEMU** (GPL/LGPL), and
Saints Row decompilation or modding projects that have no licence at all. Running an emulator as a
black box and comparing its output is fine; copying its code is not. Xenia (BSD-3-Clause) is
permissive and ReXGlue already derives from it.

If something you submit is unusually polished or complete, expect to be asked where it came from.

## A note on AI-assisted contributions

AI-assisted PRs are welcome, provided a person has read the change, understood it, and run it.
Say in the PR that a tool was used.
