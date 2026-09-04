# Saints Row (Xbox 360, 2006) - Static Recompilation

**Status: Rebased on ReXGlue SDK 0.10.0 -- boots past the loading queue into front-end init**

![Saints Row Logo Screen](logo.png)
![Cathedral Loading Screen](newscreen2.png)

Static recompilation of Saints Row for Xbox 360 to native x86-64 PC executable using [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

Saints Row (2006) was an Xbox 360 exclusive and has never been ported to PC, making it a compelling preservation target.

## Background

This project uses the same toolchain proven across multiple Xbox 360 titles:
- [360tools](https://github.com/sp00nznet/360tools) -- Extraction, analysis, and project templates
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) -- PowerPC to C++ static recompiler
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) -- Xbox 360 runtime (kernel, D3D12 GPU, audio, input)

## Project Complexity

Saints Row is a large open-world game -- significantly more complex than the arcade ports and smaller titles that have been successfully recompiled so far. Key challenges:

- **Scale**: Large PE image with tens of thousands of functions
- **Open world**: Streaming world data, LOD systems, AI systems
- **Physics**: Havok physics engine integration
- **Audio**: Complex multi-channel audio with environmental effects
- **Networking**: Xbox Live stubs needed for save/profile systems

## Pipeline

```
Saints Row.iso
    |
    v
[ extract_iso.py ]          -- Extract game files from XDVDFS disc image
    |
    v
[ extract_pe.py ]           -- Decrypt XEX2 + decompress PE image
    |
    v
[ find_abi_addrs.py ]       -- Locate PPC ABI helpers
[ extract_switch_tables.py ] -- Map jump tables
[ xex_info.py ]             -- Analyze XEX2 headers
[ parse_xex_imports.py ]    -- Identify kernel/XAM imports
    |
    v
[ XenonRecomp ]             -- PowerPC -> C++ static recompilation
    |
    v
[ ReXGlue SDK ]             -- Xbox 360 runtime environment
    |
    v
Native x86-64 .exe          -- Saints Row on PC
```

## Progress

- [x] Acquire game ISO
- [x] Extract ISO contents (375 files from XGD2 disc)
- [x] Extract and analyze XEX2 binary (33.4 MB PE, 12 sections, Bink middleware)
- [x] Find ABI helper addresses (8/10 found, no setjmp/longjmp)
- [x] Analyze kernel imports (266 kernel + 204 XAM)
- [x] Run XenonRecomp first pass (33,725 functions -> 122 MB C++)
- [x] Resolve unrecognized instructions (vandc, mulhdu, frsqrte, dcbst)
- [x] Create project scaffold (CMake + ReXGlue SDK v0.10.0)
- [x] First compilation (45 MB native x86-64 executable)
- [x] First boot -- game entry point executes, loads all packfiles
- [x] D3D12 GPU initialization (NVIDIA RTX 5070, ROV, tier 3)
- [x] Audio system (XMA decoder + SDL audio)
- [x] Game creates 14+ worker threads (physics, streaming, etc.)
- [x] Bink video decode -- both splash videos decode and audio plays
- [x] Game state machine -- transitions through all loading phases (state 1 -> 2 -> 3)
- [x] Content loading system -- all 151 items loaded (~30-60 seconds)
- [x] VdSwap/IssueSwap -- frames present to D3D12 swap chain
- [x] GPU ring buffer pipeline -- PM4 indirect buffers with circular wrap
- [x] GPU command processor -- processes 4500+ kicks, executes draw calls
- [x] Shader compilation -- 9 shaders, 5 graphics pipelines (including 3D game rendering)
- [x] Render targets created (1280x2048 color + depth at EDRAM base 0/720)
- [x] D3D12 presenter connected to window
- [x] **3D game content visible** -- Saints Row logo, cathedral, loading screen rendered
- [x] Game loop stable at 6500+ frames
- [x] Fix presentation flicker -- atomic swap guard, every-frame IssueSwap from GL2_Render
- [x] Fix tiling artifacts -- linear fetch constants for D3D12 framebuffer (sky now renders correctly)
- [x] Fix PM4 packet overflow -- virtual→physical memcpy for render IBs, zero-gap scanner fix
- [x] Fix ring buffer overflow -- zero physical memory on init and wrap-around
- [x] Streaming IO pump in all game states (world loading continues in state 3)
- [x] Headless static-analysis pipeline (IDA Pro + Ghidra) byte-aligned to the recomp image
- [x] Boot/splash sequence confirmed to complete naturally (state 1 -> 2 -> 3, 151-item queue drains)
- [x] Root-caused the post-boot crash to a re-entrant IO-init null deref (see below)
- [x] Rebase onto ReXGlue SDK 0.10.0 (manifest codegen, ReXApp, GPU plugin)
- [x] Boot past the 151-item content queue: state 1 -> 2 -> 3, front-end .xtbl loading starts
- [ ] Fix null virtual call in front-end init (sub_82105FE8, see below)
- [ ] Restore presentation on the ring-buffer path (PM4 Type0 overflow)
- [ ] Fix remaining color tint on highlights (red cast on bright areas, pink trees)
- [ ] Input system (controller navigation past menus)
- [ ] In-game rendering
- [ ] Gameplay

### Current Architecture

The game boots through Bink video playback (THQ/Volition logos), then loads 151 content items via a streaming IO pipeline: StreamCallback -> IOComp -> ResLookup -> BufAlloc -> IORead -> IOWork. Loading uses KeInitializeSemaphore preservation to maintain IO thread synchronization across re-initialization cycles, with multi-pump StreamCallback (8x per GameUpdate) for parallel IO dispatch.

After loading, PostLoop5 runs the main game loop calling GL2_Render at ~30fps. The GPU command processor receives PM4 commands via the primary ring buffer (physical memory writes with circular wrap-around), follows INDIRECT_BUFFER_PFD references to secondary buffers containing draw calls, and compiles 5 graphics pipelines including 3 game-specific 3D rendering shader pairs with lighting and texture sampling.

### Key Technical Fixes

- **KeInitializeSemaphore override**: Preserves native semaphore objects when game reinitializes IO semaphores, preventing orphaned IO threads
- **Audio deadlock fix**: Skip XAudioRegisterRenderDriverClient on re-init (sub_82600A68 acquires global_critical_region_ held by audio callback)
- **Ring buffer physical memory**: Write INDIRECT_BUFFER_PFD to physical memory (not virtual) so command processor can read them
- **Ring buffer wrap-around**: Circular write position prevents game loop kicks from being silently dropped
- **Virtual→physical memcpy**: Game writes PM4 to virtual memory but CP reads physical -- copy render IB data before creating indirect buffer references
- **PM4 zero-gap scanner**: Stop at first zero instead of skipping -- CP interprets zeros as Type0 packets causing misalignment and overflow errors
- **Linear fetch constants**: D3D12 framebuffer is linear, not Xenos-tiled -- clearing tiled bit fixes crosshatch artifacts and sky color
- **Atomic swap guard**: Prevent CallInThread queue buildup with atomic swap_pending flag -- eliminates presentation flashing
- **Ring buffer zero-fill**: Zero physical memory on init and wrap to prevent stale loading-phase PM4 data from causing overflow errors
- **Null page threshold**: Guest addresses < 256MB treated as null dereferences to prevent demand-page exhaustion
- **VEH ultra-fast path**: Handles bad vtable dispatches at fault_addr=0x8000001E without overhead

### SDK 0.10.0 Rebase (2026-09-04)

Moved from ReXGlue 0.7.0 to 0.10.0 (321 upstream commits). What changed on our side:

- **Manifest codegen.** `config/saintsrow_rexglue.toml` became `project/saintsrow_manifest.toml`
  with `[project]` / `[entrypoint]` sections. The ABI helper addresses
  (savegprlr/restgprlr/savefpr/restfpr/savevmx/restvmx) are auto-detected now, so they are gone
  from the config. Codegen is incremental and runs as a build step; a full regen is ~8s.
- **Adjustor thunks must be declared.** 0.10.0's scanner does not emit the packed
  8-byte `addi rX,rX,-N; b target` thunk tables, because only vtables reference them --
  an indirect call through one aborts with "Call to invalid or unregistered function".
  The manifest now declares the 40 missing thunks plus the 9 branch targets they jump to,
  found by scanning for runs of >= 3 consecutive thunk pairs.
- **ReXApp.** `main.cpp` no longer hand-rolls runtime setup, window creation and module
  launch; `SaintsRowApp` derives from `rex::ReXApp` and overrides only `OnPreSetup`
  (GPU plugin), `OnConfigurePaths`, `OnPostLoadXexImage` (null page + VEH) and
  `OnPreLaunchModule` (watchdog). Roughly 150 lines of bootstrap deleted.
- **Recomp macro rename.** `PPC_FUNC`/`PPC_FUNC_IMPL` -> `REX_EXTERN`, `PPC_LOAD_*`/`PPC_STORE_*`
  -> `REX_LOAD_*`/`REX_STORE_*`, `ppc_pvoid_t`/`ppc_pu32_t`/`ppc_u32_t` -> `mapped_void`/
  `mapped_u32`/`u32`, `rex::HostToGuestFunction` -> `rex::ppc::HostToGuestFunction`.
  Generated symbols are `extern "C"` now, so stub overrides must be too.
- **The GPU moved behind a plugin.** `rexgpu-xenos.dll` exports only `rex_gpu_create`, so
  `CommandProcessor::IssueSwap` / `CallInThread` / `InvalidateGpuMemory` /
  `WriteRegistersFromMem` are no longer reachable from the host. `InitializeRingBuffer` and
  `EnableReadPointerWriteBack` survive as `IGraphicsSystem` virtuals; everything else now
  goes through the GPU MMIO window at `0x7FC80000` (`0x01C5` = CP_RB_WPTR, `0x4800..` =
  fetch constants). The "force an IssueSwap from GL2_Render" presentation hack is gone.
- **Abort diagnostics.** `REX_FATAL` ends in `std::abort()`, which names no guest code.
  A `SIGABRT` handler now dumps the aborting guest thread's `PPCContext`, so an invalid
  indirect call reports its exact call site (`lr`) and target.

Two SDK patches are still required on top of v0.10.0 and are re-applied to the source tree:
`CommandProcessor::ExecuteIndirectBuffer`/`ExecutePacket` need a `count == 0` guard (a
zero-capacity `RingBuffer` divides by zero in `set_write_offset`), and `XexModule::ReadImage`
must skip `LookupHeap(base)->Reset()`, which wipes the function dispatcher's allocation.

**Where it gets to now.** Further than the 0.7.0 build: the boot state machine advances
1 -> 2 -> 3, the 151-item content queue drains completely, and front-end init starts loading
`.xtbl` tables (`death_faces`, `focused_audio`, `voice_distances`, `reverb`, ...). The old top
blocker -- the re-entrant IO-init crash loop -- no longer fires.

**Current blocker.** `sub_82105FE8` iterates a list at `[r26+8]` and makes a virtual call on
each entry. The entry is null (`r3 = 0`), so the vtable read returns garbage and the call
target is `0x0000000F`. The list slot is populated from `sub_8260F990`, which creates an object
through `sub_82623250(0x61820006, &out)` -- a type-ID class factory. Something in that creation
path fails and leaves the slot null.

**Presentation regression.** Removing the forced `IssueSwap` exposed a malformed PM4 stream:
the CP reports `ExecutePacketType0 overflow` on the indirect buffers our `sub_825D3580` hook
builds, so frames no longer reach the screen. The hand-built IB path predates 0.10.0's memory
changes -- note the new `REX_PHYS_HOST_OFFSET` (+0x1000 for guest physical >= 0xE0000000, which
covers the primary ring buffer at `0xE98B7000`) -- and needs revisiting.

### Latest Investigation (2026-05-31)

A deep static-analysis pass (IDA Pro headless, byte-aligned to the recomp image, cross-checked against the generated PPC) corrected two long-standing assumptions and pinpointed the real post-boot blocker:

- **Static-analysis tooling fixed.** IDA had been loading the extracted PE with stale section `PointerToRawData` values, shifting all of `.text` by `0xE00` and producing garbage decompilation. Patching section headers so raw offset == RVA gives a byte-exact, fully-decompiling database (18,842 functions) that matches the recomp 1:1.
- **`0x8370F248` is a red herring.** All seven writers set it to `nullsub_2` (a no-op stub) -- it is a hook slot that defaults to doing nothing, not a "physics/world init pointer." Whether it is null or `nullsub_2` is functionally identical.
- **The boot/splash load is NOT stuck.** A loading-diagnostic hook had been reading the wrong address (`0x840BAAE8`, which has zero code references and holds uninitialized data) instead of the real loading-slot array at `0x832BAAE8`. With the correct address, the boot state machine (`sub_822827B0`) advances `1 -> 2 -> 3` on its own and the 151-item content queue drains -- the splash sequence completes.
- **Real blocker: a re-entrant IO-init crash loop.** After boot, the game crash-loops on a deterministic null-object dereference (read at `obj+0x0`/`+0x8`) reached via `StreamCallback (sub_8265F720) -> IOComplete (sub_82604BE8) -> IOCtxCreate (sub_8260C318) -> IOInit (sub_8260CC50)`. `sub_8260CC50` initializes the IO semaphore (`0x832AD66C`), builds the IO queue list-heads, and spawns the IO worker threads (`sub_8260CBA0`). It is meant to run once but is re-entered every frame, re-initializing the semaphore and queues while the worker threads concurrently use them. The VEH masks each fault, but the corruption eventually turns fatal. Proposed fix: a true once-guard on `sub_8260CC50` so the IO subsystem initializes a single time.

### Known Issues

- Null virtual call in front-end init (`sub_82105FE8`) aborts the game (current top blocker)
- PM4 Type0 overflow on the hand-built indirect buffers -- nothing presents
- Red color tint on bright areas (highlights, trees) -- likely gamma ramp or render target format mismatch
- Scrolling white horizontal bar (screen tearing from unsynchronized IssueSwap)
- Second+ Bink videos blocked (garbage allocation crash)

## Related Projects

- [StilwaterReclaimed](https://github.com/THE-W0RLD/StilwaterReclaimed) -- Saints Row 2 static recompilation (early stage)
- [Halo 3 Recomp](https://github.com/twist84/halo3_cache_debug_recomp) -- Halo 3 cache debug recomp using ReXGlue SDK
- [OpenRow2](https://github.com/KairiFey/OpenRow2) -- Decompilation of the Saints Row 2 PC port
- [Xenia](https://github.com/xenia-project/xenia) -- Xbox 360 emulator with Saints Row compatibility info

## Prerequisites

- Python 3.8+ with `pycryptodome`
- CMake 3.25+, Ninja, Clang 18+ (built here with LLVM 21.1.8 + MSVC 2022 for the Windows SDK)
- MSVC 2022 (for Windows SDK headers)
- [360tools](https://github.com/sp00nznet/360tools)

## License

Tools and scripts in this repo are provided under the MIT License.
