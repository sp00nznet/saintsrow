# Saints Row (Xbox 360, 2006) - Static Recompilation

**Status: Playable to the main menu -- New Game reaches the level-load screen**

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
- [x] Trace the front-end-init abort to a missing IO format handler and unblock it
- [x] Register the vtable-only entry points codegen folds into neighbouring functions
- [x] **Front-end init completes** -- byte_8370DA94 sets, the 500ms fade runs, the main loop enters sub_82186C10
- [x] Game loop cycles (GameLoop2 / GL2_Init / GL2_World / GL2_Spawn) and the GPU receives PM4_DRAW_INDX
- [x] Survive unregistered indirect calls instead of aborting (SDK patch) -- runs 50s+ instead of ~15s
- [x] Restore presentation over the ring buffer (host-submitted PM4_XE_SWAP)
- [x] Stop clobbering vertex fetch 0 from the swap -- resolve failures 3037 -> 25 per run
- [x] Clear the tiled bit in the game's own swap packet (fixes the colour cast)
- [x] Drop the synthetic controller input; the SDK's real SDL input drives the game now
- [x] Legal -> FMV -> main menu -> New Game all work, with correct graphics and real input
- [x] Diagnose the church-screen stall: streaming is pumped only from the boot state machine
- [x] Follow the game's frontbuffer rotation in the forced present (fixes the 3-frame flicker)
- [x] Locate the in-game stall: IO workers park on a semaphore that is signalled but never consumed
- [x] Fix the guest/native object duplication: 201 native objects for one guest event, now 1
- [x] Map the streaming dispatch path end to end (driver, enqueue, workers)
- [ ] Find why the level loader allocates but never submits an IO request
- [ ] Load Game halts on a null deref: XamShowDeviceSelectorUI is always headless (see below)
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

SDK patches live in `docs/sdk-patches/rexglue-0.10.0-saintsrow.patch` (the SDK tree itself is
gitignored). Apply with `git apply` inside `tools/rexglue-sdk` at tag `v0.10.0`. Two of them are
required to boot at all:
`CommandProcessor::ExecuteIndirectBuffer`/`ExecutePacket` need a `count == 0` guard (a
zero-capacity `RingBuffer` divides by zero in `set_write_offset`), and `XexModule::ReadImage`
must skip `LookupHeap(base)->Reset()`, which wipes the function dispatcher's allocation.

**Where it gets to now.** Further than the 0.7.0 build: the boot state machine advances
1 -> 2 -> 3, the 151-item content queue drains completely, and front-end init starts loading
`.xtbl` tables (`death_faces`, `focused_audio`, `voice_distances`, `reverb`, ...). The old top
blocker -- the re-entrant IO-init crash loop -- no longer fires.

**Current blocker, traced end to end.** `sub_82105FE8` iterates a list at `[r26+8]` and makes a
virtual call on each entry. The entry is null, so the vtable read returns garbage and the call
target is `0x0000000F`. Walking it back:

```
sub_82105FE8   virtual-calls a null list entry                -> abort, target 0x0000000F
  <- slot filled only when sub_8260F990 returns >= 0 (the game never checks)
sub_8260F990   returns 0x80004001 (E_NOTIMPL)
sub_826243D8   returns 0x80004001 from its vtable call
sub_82610168   answers E_NOTIMPL when the handler lookup comes back null
sub_8260FF68   handler-registry lookup misses
```

`sub_8260FF68(registry, idx)` resolves an IO format handler: `idx <= 127` indexes the table at
`[registry+12]` bounded by `[registry+8]`, higher indices use `[registry+20]`/`[registry+16]`, and
an 8-byte entry counts as registered only when both dwords are non-zero. The registry hangs off
the current IO context (`[ctx+60] - 8`), and `sub_8260C4F8` installs a brand-new context at
`dword_837102B4` on **every** IO-context create -- so the "current" registry is replaced constantly.

Dumping the registry around IO init shows what that costs:

| context | slots filled by `sub_8260CC50` |
| --- | --- |
| first | 0, 1, 2, **3** |
| every later one | 0, 1, 2 |

Two separate defects fall out of that:

- **Slot 3 was being lost.** It is registered once, alongside the first IO init, and every
  subsequent context came up without it. Fixed: `sub_8260FF68` now remembers every registry it has
  seen and, on a miss, resolves against one that still has the slot. Entries are pointers to static
  handler descriptors, so serving one from an older table is safe. Confirmed working in the log
  (`[IOReg] idx=3 missed on ..., served from ...`).
- **Slot 4 is never registered at all** -- not on the first context, not on any later one. Its
  handler pair (`0x821071F8`/`0x82107208`) sits in the static descriptor list at `0x8203B59C`, in
  the same address range as the front-end code that needs it, which points at an ordering problem:
  the front-end looks the handler up before whatever installs it has run. That is the open thread.

**Unblocked (2026-09-04, later).** `sub_8260FF68` now answers a miss from the image's own
master handler list at `0x8203B56C`: five 12-byte `{a, b, n}` records whose `{a, b}` pairs are
exactly, in order, registry slots 0..4. Slots 0..3 match what IO init installs; record 4 is the
one nothing ever registers. Because a record's first two dwords have the same layout as a registry
entry, the record's own guest address can be handed back as the entry. That resolves both the lost
slot 3 and the never-registered slot 4, and front-end init runs to completion:
`byte_8370DA94` sets, the 500ms fade advances, and the main loop enters the real game loop.

What this does not do is explain why nothing registers record 4. Neither `sub_82604D10` /
`sub_8260D518` (hooking it shows the handler table unchanged across the call) nor anything
reachable by an immediate reference to `0x8203B56C` writes these slots. Find the real registrar and
the fallback goes away.

### The forced present is load-bearing, and must follow the buffer rotation (2026-09-05)

`GL2_Render` forces a present every frame. It is tempting to delete -- the game's own `VdSwap`
packet reaches the command processor and presents correctly, which is provable by editing the fetch
constant in *its* packet and watching the screen change. Deleting it is wrong anyway: the game's
logic paces off swap completion, and without the forced present the menu stops responding to input
and the intro loops in attract mode while the renderer keeps drawing. A `git diff` confirmed
removing that single call was the only functional change.

What *was* wrong is that it presented a hardcoded frontbuffer (`0x09258000`) while the game rotates
through its own buffers, so a stale frame interleaved with the live ones -- on screen, three frames
cycling forever (black, THQ logo, glyph artifact). It now publishes the frontbuffer address, width
and height that the game last handed `VdSwap` and presents that.

### Load Game wedges on save enumeration (2026-09-05)

Highlighting Load Game in the main menu freezes the game: the render loop and `SceneTick` keep
running, memory is stable, no heap errors -- but input stops reaching the game.

The kernel log at debug level shows why. Load Game calls `XamShowDeviceSelectorUI`, and in the SDK
that entry is dispatched headlessly **unconditionally** -- unlike `XamShowMessageBoxUI` and
`XamShowKeyboardUI`, it does not check the `headless` cvar. It returns a dummy device ID
(`0x00000001`) with `X_ERROR_SUCCESS`. The game then enumerates saves on that device, finds nothing
useful, and dereferences a null: `Call to invalid or unregistered function at guest address
0x0000000F (from lr 0x820FA6E8)`, followed by an unhandled guest access violation.

Avoid Load Game for now. New Game is unaffected and is the path that matters.

### Nothing is enqueuing IO at all (2026-09-06)

With the object duplication fixed, uncapped counters on every guest function that touches the IO
sync objects (found by scanning the image for references to `0x832AD66C` / `0x832AD680` /
`0x832AD690` / `0x832AD6A0`) give a clear picture on the church screen:

| function | role | calls |
| --- | --- | --- |
| `sub_8260CAD8` | IO worker body | 201 (all parked inside) |
| `sub_8260CBA0` | worker spawn | 0 |
| `sub_8260D180` | enqueue | 0 |
| `sub_8260D2D8` | enqueue | 0 |
| `sub_8260DE50` / `sub_8260DE98` | queue helpers | 0 |
| `sub_8260C5F8` | context ctor path | 0 |

`sub_8265F720` (StreamCallback) has **no direct callers** anywhere in the recompiled image -- it is
reached only through a pointer. The slot holding it (`0x827A1F24`) is referenced by exactly one
function, `sub_82604B00`, which is the real dispatch driver:

```
sub_82604B00(arg):
    lock
    ctx = [dword_837102B4]                  // global IO context
    if (ctx != 0)  ctx->vtable[1]()         // in-game path -> sub_82604ED0
    else           scan 0x827A1F24..0x827A1F28 for a callback and call it
                                            // boot path -> sub_8265F720
```

Boot takes the callback branch; once a context exists every call takes the context branch. But the
driver is only called **three times in a whole session** (once at boot, twice in-game), so it is a
"kick", not a pump.

So the shape of the stall is now precise: the workers are correctly parked on one shared event, no
enqueue function is ever called, and the driver barely runs -- yet the level loader is clearly busy,
allocating its way to 2.6GB during New Game. The loader is allocating but never submitting an IO
request. That submission path, upstream of all this plumbing, is the next thing to find.

### One guest event, 201 native objects (2026-09-05)

Instrumenting the SDK's `xeKeWaitForSingleObject` to log the *resolved* `XObject` for the IO sync
addresses gave the answer immediately: **201 distinct native objects for the single guest event at
`0x832AD680`**. Every IO worker was parked on its own private event.

`XObject::GetNativeObject` ties a guest dispatch header to its native object by writing
`kXObjSignature` (`'REX '`) into `wait_list_flink` and the handle into `wait_list_blink`. Its own
comment notes the XDK inlines some `KeInitialize` calls, and assumes "all other calls (Set/Reset/
Wait/etc) are used and we don't have to worry about PPC code poking the struct". Saints Row breaks
that assumption: it re-initialises the IO event and semaphore headers in place, wiping the
signature, so every subsequent wait fell into the "first use, create new" branch.

The fix (in `docs/sdk-patches/`) remembers the handle per guest address and re-attaches it when the
signature has been cleared, instead of minting another object. That takes it from 201 objects to 1.

Re-arming from the render loop was tried first and is not enough -- the game clobbers and waits many
times between frames, so the association has to be restored at the point of use, not on a timer.

**Still stalled, for a second and separate reason.** With one shared event, the probe shows *zero*
signals delivered to it: no `KeSetEvent` on `0x832AD680`, no `KeReleaseSemaphore` on `0x832AD66C`.
(The `sem_state=2` seen earlier is not a real signal either -- it is the `KeInitializeSemaphore`
override writing a count of 2 directly.) So the workers now correctly share one event that nothing
ever sets. The next question is which guest path is supposed to signal it, and why the in-game load
never reaches it.

### The IO workers are parked on a semaphore nobody wakes them from (2026-09-05)

Uncapped telemetry from the render loop, sampled while sitting on the church screen, reads:

```
[Stream] cb=10281(+0) find=10281(+0) comp=10281(+0) ctor=10281(+0) work=10281(+0)
         state=3 sem_state=2 evt_state=0
```

`sem_state=2` is the signal count of the IO semaphore at `0x832AD66C`. So the game **has** queued
work and **has** signalled the semaphore -- two units are available and nothing consumes them. The
watchdog thread sampler shows dozens of guest threads alive, all parked in `ntdll` waiting.

That is the stale-object mismatch the `KeInitializeSemaphore` override was written to prevent:
the game reinitialises the semaphore, a new native object is associated with the guest memory, and
the worker threads created earlier are still blocked on the *previous* object. The override
preserves the handle across re-init, but the evidence says a mismatch still happens somewhere -- a
signalled semaphore with a queue behind it and dozens of permanently parked workers is exactly that
shape.

So the level load never starts not because nothing is queued, and not because the dispatcher is not
called, but because the workers that would drain the queue are waiting on the wrong object. That is
the thing to fix next, and it sits squarely in the stubs' IO thread handling: `g_skip_thread_creation`,
the handle reuse after 200 real `sub_8260CC50` calls, and the `KeInitializeSemaphore` preservation.

### Why the level load behind the church screen does nothing (2026-09-05)

`StreamCallback` (`sub_8265F720`) dispatches queued IO, and it was only ever pumped from
`sub_822827B0` -- the **boot** state machine. That is fine while booting, but once the real game
loop owns the frame `sub_822827B0` stops being called, so after New Game the level load queues work
that nothing ever dispatches.

Proving that needed new instrumentation: every existing probe in `stubs.cpp` caps its logging
(`if (++c <= 20)`), so after a long session a *stopped* subsystem is indistinguishable from one
that merely stopped logging. The `[Stream]` line now dumps uncapped counters for the whole chain
every 600 frames, and behind the church screen all five stages sit frozen at an identical value
with zero delta while the render loop happily runs another 1.8M frames.

**Pumping from the render loop is not the fix.** It was tried and reverted:

- `sub_8265F720` always reports more work, so the pump never short-circuits.
- Every pass reaches `sub_8260C318`, which builds a *fresh* IO context with no already-created
  check, and nothing frees them.
- At the boot path's 8 pumps/frame that reaches `0x8007000E` (E_OUTOFMEMORY) part-way through the
  level load. At 1/frame it still leaks roughly 1.7MB/s of guest heap until `BaseHeap::Alloc`
  cannot find a contiguous range -- at which point the game's own allocations start failing and it
  freezes with the renderer still drawing the last frame. Observed live: moving the menu selection
  from New Game to Load Game was enough to tip it over.

So the fix has to come from one of two places: a once-guard so IO contexts are not manufactured per
call (the same missing check that drops the format-handler registry, see above), or getting the
game's own IO worker threads to service the queue. The stubs interfere with those threads heavily
-- handle reuse after 200 real `sub_8260CC50` calls, `KeInitializeSemaphore` preservation,
`g_skip_thread_creation` -- and that is the more likely real answer.

### What the boot sequence actually is (2026-09-05)

Worth recording, because two earlier sessions guessed wrong: the real order is
**legal screen -> FMV -> main menu -> New Game -> church/loading screen**. The church screen is the
*level* load, not part of the boot load. Earlier notes assumed legal -> church, which is why the
"cathedral loading screen" was treated as a boot-sequence step.

Two corrections were needed to get there, both found by running the game rather than reading logs:

- **Do not clear the tiled bit.** An earlier attempt cleared bit 31 of the swap fetch constant in
  the game's own packet, on the theory that the framebuffer is linear. It is not: with the bit
  cleared the texture cache reads tiled data as linear and the image is scrambled beyond
  recognition. The pre-rebase code cleared it correctly *for a different setup*, where we called
  IssueSwap ourselves with a hand-made fetch. Now that the game's own swap packet drives the
  present, its fetch is right as-is -- and with it left alone the graphical errors are gone.
- **`mnk_mode` defaults to off.** The SDK ships keyboard/mouse controller emulation but does not
  enable it, so after the synthetic input was removed there was no input path at all with no
  gamepad plugged in, and the game sat on the title screen. `SaintsRowApp::OnPreSetup` now defaults
  it on (an explicit `--no-mnk_mode` still wins). X or Enter = Start, Semicolon or Space = A,
  Z or Tab = Back, WASD = left stick, Shift+arrows = D-pad.

### Reaching the menu: swap format and real input (2026-09-04)

Two fixes came straight out of watching the game run rather than reading logs.

**The colour cast is the tiled bit, cleared in the right place.** `VdSwap` copies the frontbuffer
fetch constant out of the game's D3D9 texture header, and that header has the tiled bit set. Our
framebuffer is linear, so the texture cache detiles linear data and the colours come out wrong.
The fix is to clear bit 31 of the fetch's first dword *in the game's own swap packet*, right after
`VdSwap_entry` writes it into the guest command buffer -- not by injecting a competing fetch write
from an out-of-band present, which is what broke every resolve (see above).

**The synthetic input had to go.** The stubs used to override `XamInputGetState`,
`XamInputGetKeystrokeEx` and `XamInputSetState` to fake a repeating START/A button cycle so the
game would walk itself past attract mode. That explained three things at once: the cathedral
loading screen never appeared (START skipped it), the game went straight to the main menu (A
navigated it), and it then sat mashing buttons at the copyright screen. SDK 0.10 ships a working
input system -- SDL gamepad plus keyboard and mouse -- and `ReXApp` attaches the window to it, so
the overrides are gone and the game now responds to real input. The synthetic versions are in git
history if a scripted walkthrough is ever wanted for automated testing.

### Running stably, and the vertex-fetch clobber (2026-09-04)

Three changes took the game from aborting after ~15s to running indefinitely in the game loop.

**Unregistered indirect calls are no longer fatal.** `InvalidFunctionTrap` in the SDK's
`function_dispatcher.cpp` now logs each distinct `(target, lr)` once and returns instead of calling
`REX_FATAL`. A guest indirect call can land on an address codegen never emitted an entry point for,
and aborting turned every one of those into its own build/run cycle to discover the next. Returning
lets a single run report them all. This is a development aid -- revert it to `REX_FATAL` once the
manifest is complete.

**Presentation goes back through the ring buffer.** The forced `IssueSwap` died with the rebase
(the plugin does not export it). `RbPresent` now builds the same PM4 sequence `VdSwap` does -- a
Type3 `PM4_XE_SWAP` carrying the `'SWAP'` fourcc, frontbuffer physical address, width and height --
appends it to the primary ring buffer and advances `CP_RB_WPTR` through GPU MMIO. The CP picks it
up and calls `IssueSwap` on its own thread. The kick hook and `RbPresent` share one write position
under a mutex, since both append to the same buffer.

**The swap must not write fetch constant 0.** `VdSwap` sets registers `0x4800..0x4805` because it
owns the whole swap. We are injecting an extra present into a stream the game is also driving, and
those registers are *shared* between texture fetch 0 and vertex fetch 0. The resolve path reads
vertex fetch 0 -- `GetResolveInfo` in `graphics/util/draw.cpp` carries the comment "D3D9 HACK:
Vertices to use are always in vf0, and are written by the CPU" -- so writing the swap's texture
fetch there makes every subsequent resolve fail:

| swap writes fetch 0 | failed resolves per run | failed draws |
| --- | --- | --- |
| yes | 3037 | 3043 |
| no | 25 | 25 |

Dropping the write leaves the swap using whatever texture fetch 0 the game's own `VdSwap` last
published, which is what it should have been doing.

**Still black.** The framebuffer at `0x09258000` reads all zeros, so the resolves that now succeed
are not landing there, and the front-end fade `flt_8370D1A4` never advances off 0 (so
`byte_8370DA95`, and with it the transition out of the fade, never happens). Those are the next two
threads.

### Vtable-only entry points (2026-09-04)

Past front-end init the game started aborting on calls to *plausible* code addresses
(`0x82277408`, `0x82277418`, `0x824EAED8`). These are small functions that only virtual dispatch
reaches, so codegen never sees a call and folds them into whatever function precedes them. Two
scans now feed the manifest:

- **Tail-call trampolines** -- `lwz r11,0(rX); ...; mtctr r11; bctr` starting right after a
  blr/bctr and lying inside an already-emitted function. The XEX import thunk table
  (`{0x01<mod><ord>, 0x02<mod><ord>, mtctr, bctr}`, 230 of them) matches the same shape and must be
  skipped -- splitting those breaks the link with undefined `sub_827888xx` symbols.
- **Small vtable leaves** -- addresses appearing in a run of >= 4 consecutive code pointers (a
  vtable) that start after a terminator and reach their own blr/bctr within 16 instructions.

Both need a split-safety check: a split at S is only valid when nothing in `[host, S)` branches to
`>= S` and nothing in `[S, end)` branches to `< S`, or the two halves reference each other's labels
and the generated code will not compile. That check rejects 193 of 280 candidates, and one more
(`sub_826445E8`, reached through a jump table the check cannot see) had to be dropped from the
build error. Declaring *every* vtable slot instead -- all 3743 -- splits functions mid-flow and
breaks game init before the first frame.

Two approaches were tried and rejected on evidence: reusing the first IO context instead of letting
`sub_8260C4F8` build new ones does remove the abort, but the streaming queue then stops draining
(the contexts really are per-request); and replaying registrations through `sub_8260D518` records
nothing, because IO init populates the table directly rather than going through that registrar.

A `SIGABRT` handler dumping the aborting guest thread's `PPCContext` is what made this tractable --
`REX_FATAL` otherwise vanishes into `std::abort()` with no guest call site.

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

- New Game reaches the church screen and stops: the loading spinner animates and the render loop is alive, but the streaming pipeline issues no IO (current top blocker)
- Highlighting Load Game wedges the game on a null deref (see the save-enumeration note below)
- Screen capture of the D3D12 window from a script comes back black even while the game is visibly rendering; observations come from running it by hand
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
