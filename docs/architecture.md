# Architecture

Three parts, two of them built from source on your machine:

```
  your disc ──extract──> extracted/            game files + default.xex (never committed)
                              │
  project/saintsrow_manifest.toml ──┐
                              ▼     ▼
                     ReXGlue codegen (build step)
                              │
                              ▼
                     generated/default/*.cpp   PowerPC → C++, ~125 files (never committed)
                              │
  project/src/main.cpp ───────┤
  project/src/stubs.cpp ──────┤
                              ▼
                     saintsrow.exe ──links──> rexruntime.dll + rexgpu-xenos.dll
                                              (ReXGlue: kernel, XAM, D3D12 GPU, audio, input)
```

## What each part owns

**ReXGlue SDK** (`tools/rexglue-sdk`, cloned and patched by setup). The recompiler (`rexglue
codegen`) and the runtime: an Xbox 360 kernel and XAM implementation, the Xenos GPU emulated on
D3D12 (a plugin, `rexgpu-xenos.dll`), XMA audio, and SDL input and windowing. Its `ReXApp` class
does the whole startup sequence; a game overrides a few hooks. This project's changes to it are
in `docs/sdk-patches/` (see the README there for why each exists).

**The manifest** (`project/saintsrow_manifest.toml`). Where the XEX is and where generated code
goes, plus functions codegen's scanner can't find on its own. Those are almost all reached only
through vtables, so no direct call points at them: C++ adjustor thunks, tail-call trampolines,
small leaf functions and packed `li r4,N; b` stubs. A missing one shows up at run time as
`Call to invalid or unregistered function at guest address 0x...`. A wrong one, an address that
isn't really a function, splits its host and can produce `Unresolved branch` at compile or run
time. Each group's comment says how it was found.

**`main.cpp`.** `SaintsRowApp` (a `ReXApp`): the default game-data path and, in the default build,
a last-chance crash filter that writes the faulting stack to `saintsrow_crash.log`.

**`stubs.cpp`.** Overrides of guest functions (`REX_EXTERN(sub_XXXXXXXX)`) and kernel imports
(`REX_EXTERN(__imp__Name)`). The linker's `/force:multiple` lets these replace the generated or SDK
versions. In the default build it holds just the `SR_AUTOPLAY` input wrapper.

## The legacy overrides, and why they are off

Most of `stubs.cpp`, about 2,600 lines, was written against the 0.7-era runtime, when the game
couldn't get through boot by itself. It forces the loading state machine forward, calls the
streaming callback eight times per frame from the main thread, builds indirect buffers by hand,
swaps in its own presents, and registers a vectored exception handler that skips faulting
instructions and zeroes the destination register.

On the current runtime those interventions are the problem. The streaming pump re-enters the IO
subsystem's init (`sub_8260CC50`) every frame, and the exception handler hides the faults that
follows. With everything compiled out (`SR_BARE`, the default) and the false manifest entries
removed, the game boots, plays attract mode and reaches Player Creation with correct graphics. The
old path stalled on the cathedral loading screen with a colour cast. The code stays behind
`SAINTSROW_LEGACY_STUBS=ON` only for comparison; see ROADMAP.md.

## Data flow at run time

`saintsrow.exe` starts `ReXApp`: SDL video, the D3D12 device, the guest memory arena, then it
loads `default.xex` from `extracted/` into guest memory and jumps to the recompiled entry point
`xstart`. Guest code calls kernel imports, which the SDK implements. It writes PM4 command packets
to a ring buffer in guest physical memory and advances the write pointer through the GPU MMIO
window at `0x7FC80000`. The GPU plugin's command processor reads those packets on its own thread and
draws with D3D12, and a `VdSwap` packet presents the frame.
