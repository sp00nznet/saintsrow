// Saints Row - Game-specific kernel stub overrides
// These override default ReXGlue SDK implementations where the game
// needs special handling.

#include "saintsrow_config.h"
#include "saintsrow_init.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/command_processor.h>
#include <rex/runtime.h>

#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>

// Worker thread semaphore handles captured from sub_826368E0
static uint32_t g_worker_sem_handles[16] = {};
static int g_worker_sem_count = 0;

// Global flag to skip thread creation during IO re-initialization
extern std::atomic<bool> g_skip_thread_creation;

// Track handles from real ExCreateThread calls for reuse
extern uint32_t g_io_thread_handles[8];
extern int g_io_thread_handle_count;

// ============================================================================
// XAM User / Profile Stubs
// ============================================================================

// XamUserGetSigninState - return signed in for player 0
// The game checks if a user is signed in before proceeding
extern "C" uint32_t XamUserGetSigninState_entry(uint32_t user_index) {
    if (user_index == 0) return 1;  // eSignedInLocally
    return 0;  // eNotSignedIn
}

// XamUserGetSigninInfo - provide basic user info
// Prevents null pointer crashes when game tries to read user profile
extern "C" void XamUserGetSigninInfo_entry(
    uint32_t user_index, uint32_t flags, void* info_ptr) {
    // Zero the info struct - game will handle defaults
    if (info_ptr) {
        memset(info_ptr, 0, 0x100);  // Conservative size
    }
}

// ============================================================================
// Bink Video Worker Thread Override
// ============================================================================
// The Bink video worker thread (at PPC addr 0x8278D148) crashes in the
// D3D12 driver when trying to decode video. On Xbox 360, Bink uses the GPU
// for hardware-accelerated decode, but the host D3D12 context isn't set up
// for multi-threaded Bink decode.
//
// Override the Bink worker to be a no-op that just waits for shutdown.
// This skips intro videos but prevents the crash.

// Bink video decode is disabled - these functions all live in the BINK code
// section (0x82789600-0x82799724). We stub the key entry points that the
// game calls to initialize and decode Bink videos.

// ============================================================================
// Bink Video - let recompiled Bink library run for real
// The splash video rendering initializes the GPU render pipeline.
// Only stub the worker thread (needs special handling).
// ============================================================================

// Trace key Bink functions to understand video render flow
extern "C" void __imp__sub_82789600(PPCContext& ctx, uint8_t* base); // BinkDoFrame
extern "C" void __imp__sub_82789658(PPCContext& ctx, uint8_t* base); // BinkWait
extern "C" void __imp__sub_82789EE8(PPCContext& ctx, uint8_t* base); // BinkOpen
PPC_FUNC(sub_82789600) { // BinkDoFrame
    static int c = 0;
    if (++c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkDoFrame #%d] handle=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
    __imp__sub_82789600(ctx, base);
    if (c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkDoFrame #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
}
PPC_FUNC(sub_82789658) { // BinkWait
    static int c = 0;
    __imp__sub_82789658(ctx, base);
    if (++c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkWait #%d] returned r3=%u\n", c, ctx.r3.u32); fclose(f); } }
}
PPC_FUNC(sub_82789EE8) { // BinkOpen
    static int c = 0;
    c++;
    char fn[256] = {0};
    if (ctx.r3.u32) {
        for (int i = 0; i < 255; i++) { fn[i] = (char)PPC_LOAD_U8(ctx.r3.u32 + i); if (!fn[i]) break; }
    }
    if (c <= 5) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkOpen #%d] '%s'\n", c, fn); fclose(f); }
    }
    // Only allow the first BinkOpen (THQ logo). The second one (sr_nite_01.bik)
    // causes a garbage 0xC271A7B0 allocation that corrupts memory and crashes.
    if (c >= 2) {
        ctx.r3.u64 = 0; // return null handle (video won't play)
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkOpen #%d] BLOCKED '%s' (prevents garbage alloc crash)\n", c, fn); fclose(f); }
        return;
    }
    __imp__sub_82789EE8(ctx, base);
    if (c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkOpen #%d] returned handle=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
}

// Write a test pattern to the framebuffer to verify display works

// Force render flag so VdSwap gets called
// The render wait thread checks [r31+10810] & 0x4
// We need to find r31's value and set the flag
// r31 comes from a global pointer chain in the render wait thread
// Let's hook sub_825E5320 (render) and sub_825E54A8 (swap) directly instead
extern "C" void __imp__sub_825E5320(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825E5320) {
    static int c = 0;
    if (++c <= 5) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RenderFrame #%d] ENTER\n", c); fclose(f); }
    }
    __imp__sub_825E5320(ctx, base);
    if (c <= 5) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RenderFrame #%d] EXIT\n", c); fclose(f); }
    }
}

// VdSwap wrapper - clear skip flag so VdSwap actually gets called
// The skip flag is at [r31 + 19980] inside the function
// r31 comes from [[0x82800658]] (same as render wait thread)
extern "C" void __imp__sub_825E54A8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825E54A8) {
    // Force ALL rendering flags to get VdSwap called AND ring buffer kicked:
    uint32_t r31_val = ctx.r3.u32;
    if (r31_val) {
        PPC_STORE_U32(r31_val + 19980, 0);    // skip flag = 0 (gates VdSwap call)
        uint8_t dirty = PPC_LOAD_U8(r31_val + 20424);
        PPC_STORE_U8(r31_val + 20424, dirty | 0x8);  // dirty flag (gates render sub calls)
        // Force frame counter at [r31+20080] to non-zero (gates buffer queue setup)
        uint32_t fc = PPC_LOAD_U32(r31_val + 20080);
        if (fc == 0) {
            PPC_STORE_U32(r31_val + 20080, 1);
        }
        // Set "direct write" bit at [r31+10809] |= 0x2
        // This gates the CP_RB_WPTR write in sub_825D3580 (ring buffer kick)
        // Without this, VdSwap writes packets but GPU never processes them
        uint8_t dw = PPC_LOAD_U8(r31_val + 10809);
        PPC_STORE_U8(r31_val + 10809, dw | 0x2);
        static int logc = 0;
        if (++logc <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[VdSwap-Fix #%d] skip=0 dirty=0x%02X fc=%u dw=0x%02X\n", logc, dirty|0x8, fc, dw|0x2); fclose(f); }
        }
    }
    // Log state JUST before calling real function
    if (r31_val) {
        uint32_t skip_val = PPC_LOAD_U32(r31_val + 19980);
        uint32_t fc_val = PPC_LOAD_U32(r31_val + 20080);
        uint8_t dirty_val = PPC_LOAD_U8(r31_val + 20424);
        uint32_t q1 = PPC_LOAD_U32(r31_val + 20072);
        uint32_t q2 = PPC_LOAD_U32(r31_val + 20076);
        static int pre = 0;
        if (++pre <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[Pre-Call #%d] skip=%u fc=%u dirty=0x%02X q1=%u q2=%u r3=0x%08X\n",
                pre, skip_val, fc_val, dirty_val, q1, q2, r31_val); fclose(f); }
        }
    }

    __imp__sub_825E54A8(ctx, base);

    // After: check if VdSwap was reached
    if (r31_val) {
        uint32_t rb_ptr = PPC_LOAD_U32(r31_val + 40);
        uint32_t skip_after = PPC_LOAD_U32(r31_val + 19980);
        static int pc = 0;
        if (++pc <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[Post-Call #%d] rb_ptr=0x%08X skip_after=%u\n", pc, rb_ptr, skip_after); fclose(f); }
        }
    }
}

// Render wait thread - run naturally
extern "C" void __imp__sub_825DF970(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825DF970) {
    __imp__sub_825DF970(ctx, base);
}

// Bink worker thread - let it run for real now
// (previously stubbed to idle, but Bink needs it for video transitions)

// ============================================================================
// Render subsystem init debug hook
// ============================================================================
extern "C" void __imp__sub_82653F98(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_827166C0(PPCContext& ctx, uint8_t* base);

// Hook sub_82653F98 (scene/camera tick) - this was crashing through XamInputGetKeystrokeEx
PPC_FUNC(sub_82653F98) {
    static int c = 0;
    c++;
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[SceneTick #%d] ENTER\n", c); fclose(f); }
    }
    __imp__sub_82653F98(ctx, base);
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[SceneTick #%d] EXIT\n", c); fclose(f); }
    }
}

// Hook sub_827166C0 -- reads TLS r13+256 -> [+332] to get GPU/render context
PPC_FUNC(sub_827166C0) {
    __imp__sub_827166C0(ctx, base);
    static int tls_log_count = 0;
    if (++tls_log_count <= 10) {
        FILE* tf = fopen("saintsrow_render_debug.log", "a");
        if (tf) {
            fprintf(tf, "sub_827166C0: r13=0x%08X, TLS[256]=0x%08X, result r3=0x%08X\n",
                ctx.r13.u32,
                (ctx.r13.u32 ? PPC_LOAD_U32(ctx.r13.u32 + 256) : 0),
                ctx.r3.u32);
            fclose(tf);
        }
    }
}

// Heartbeat hooks on suspected game loop functions
extern "C" void __imp__sub_82648ED8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82186F08(PPCContext& ctx, uint8_t* base);

PPC_FUNC(sub_82648ED8) {
    static int count = 0;
    count++;
    if (count <= 10) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[TICK-648ED8] #%d ENTER r3=0x%08X\n", count, ctx.r3.u32); fclose(hf); }
    }
    __imp__sub_82648ED8(ctx, base);

    if (count <= 10) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[TICK-648ED8] #%d EXIT r3=0x%08X state=%u\n", count, ctx.r3.u32, PPC_LOAD_U32(0x8370DD7C)); fclose(hf); }
    }
}

// Trace each sub-call in the main game loop (sub_82186F08)
#define TRACE_CALL(name, addr) \
    extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base); \
    PPC_FUNC(sub_##addr) { \
        static int _c = 0; _c++; \
        if (_c <= 10) { FILE* f = fopen("saintsrow_heartbeat.log", "a"); \
            if (f) { fprintf(f, "[" name "] ENTER #%d\n", _c); fclose(f); } } \
        __imp__sub_##addr(ctx, base); \
        if (_c <= 10) { FILE* f = fopen("saintsrow_heartbeat.log", "a"); \
            if (f) { fprintf(f, "[" name "] EXIT #%d\n", _c); fclose(f); } } \
    }

// Trace with state monitoring
#define TRACE_STATE(name, addr) \
    extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base); \
    PPC_FUNC(sub_##addr) { \
        uint32_t st_before = PPC_LOAD_U32(0x8370DD7C); \
        __imp__sub_##addr(ctx, base); \
        uint32_t st_after = PPC_LOAD_U32(0x8370DD7C); \
        static int _c = 0; _c++; \
        if (st_before != st_after || _c <= 5) { \
            FILE* f = fopen("saintsrow_heartbeat.log", "a"); \
            if (f) { fprintf(f, "[" name " #%d] state %u -> %u\n", _c, st_before, st_after); fclose(f); } \
        } \
    }

// GameUpdate - the loading state machine
// Internal state at 0x8371DD7C controls loading flow:
//   0 = uninitialized (default: skips to done, sets main=3)
//   1 = loading phase 1
//   2 = loading phase 2 (checking completion)
//   3 = loading complete
// When internal_state=0, the function immediately exits to state 3 (done).
// Force internal_state to 2 on first call to enter the loading flow.
extern "C" void __imp__sub_822827B0(PPCContext& ctx, uint8_t* base);
// GameUpdate - trace the loading state machine
// The state at [0x8370DD7C] controls the main loop:
//   0 = default (immediately sets to 3)
//   1 = loading phase 1 (stuck here because content loading can't complete)
//   2 = loading phase 2
//   3 = done → exits loading loop → proceeds to game loop with GL2_Render
// Force state to 3 after a few frames to bypass the stuck loading.
PPC_FUNC(sub_822827B0) {
    static int _c = 0; _c++;
    uint32_t state_before = PPC_LOAD_U32(0x8370DD7C);
    __imp__sub_822827B0(ctx, base);
    uint32_t state_after = PPC_LOAD_U32(0x8370DD7C);
    if (_c <= 30 || state_before != state_after || (_c % 100 == 0)) {
        // Trace the exact internal computation that should advance state
        uint32_t r31_addr = 0x8370DA78;
        uint32_t queue_ptr = PPC_LOAD_U32(r31_addr - 196);
        uint32_t q8 = queue_ptr ? PPC_LOAD_U32(queue_ptr + 8) : 0;
        uint32_t q12 = queue_ptr ? PPC_LOAD_U32(queue_ptr + 12) : 0;
        int32_t diff = (int32_t)(q8 - q12);
        uint32_t clz = (diff == 0) ? 32 : __builtin_clz((uint32_t)diff);
        // PPC rlwinm r11,r11,27,31,31: rotate 32-bit left by 27, mask bit 31 (LSB)
        uint32_t rotl = ((clz << 27) | (clz >> 5)); // 32-bit rotate left 27
        uint32_t should_advance = rotl & 1;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GameUpdate #%d] state %u -> %u (queue=0x%08X q8=%u q12=%u diff=%d clz=%u rotl=0x%X advance=%u)\n",
            _c, state_before, state_after, queue_ptr, q8, q12, diff, clz, rotl, should_advance); fclose(f); }
    }
    // Log the loading check flags and state machine internals
    if (_c <= 50 || _c % 100 == 0) {
        uint32_t r31_addr = 0x8370DA78;
        uint32_t load_queue = PPC_LOAD_U32(r31_addr - 196); // [r31-196] = loading queue ptr
        uint32_t internal_var = PPC_LOAD_U32(r31_addr + 0);  // [r31+0] another state var
        uint32_t r31_776 = PPC_LOAD_U32(r31_addr + 776);     // [r31+776]
        // sub_82282638 checks 4 entries at base 0x840BAAE8, stride 584
        uint32_t base = 0x840BAAE8;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            uint32_t q8 = load_queue ? PPC_LOAD_U32(load_queue + 8) : 0;
            uint32_t q12 = load_queue ? PPC_LOAD_U32(load_queue + 12) : 0;
            fprintf(f, "[LoadCheck #%d] queue=0x%08X [+8]=0x%08X [+12]=0x%08X (empty=%d) var0=%u ",
                _c, load_queue, q8, q12, (q8 == q12), internal_var);
            for (int i = 0; i < 4; i++) {
                uint32_t entry = base + i * 584;
                uint8_t b24 = PPC_LOAD_U8(entry + 24);
                uint8_t b108 = PPC_LOAD_U8(entry + 108);
                uint8_t b360 = PPC_LOAD_U8(entry + 360);
                fprintf(f, "e%d[%u,%u,%u] ", i, b24, b108, b360);
            }
            fprintf(f, "\n");
            fclose(f);
        }
    }
    // Pump the streaming callback from the MAIN thread.
    // NOTE: Do NOT clear [0x837102B4] - it's the IO context pointer read by
    // IO threads at [0x83710000+692]. Clearing it causes threads to exit,
    // killing loading progress.
    if (state_after == 1) {
        extern void sub_8265F720(PPCContext& ctx, uint8_t* base);
        // Pump streaming callback multiple times per frame to dispatch
        // IO requests in parallel rather than one-at-a-time.
        for (int pump = 0; pump < 8; pump++) {
            PPCContext sc_ctx = ctx;
            sc_ctx.r3.u64 = 0x827A1F24;
            sc_ctx.r4.u64 = 0x827A1F28;
            sc_ctx.r5.u64 = 0;
            sc_ctx.r6.u64 = 0;
            sub_8265F720(sc_ctx, base);
            // Stop if callback returned null (no more pending requests)
            if (sc_ctx.r3.u32 == 0) break;
        }
    }
    // Loading now completes naturally via KeInitializeSemaphore preservation.
    // No force-bypass needed. Keep a safety net for debugging:
    if (_c >= 10000 && state_after == 1) {
        PPC_STORE_U32(0x8370DD7C, 3);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GameUpdate #%d] FORCED state 1 -> 3 (loading safety timeout)\n", _c); fclose(f); }
    }
}
TRACE_STATE("VideoMgr", 821FB9D8)
// VideoDriver - trace video queue state
extern "C" void __imp__sub_821FBD10(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_825BFC10(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825BFC10) {
    __imp__sub_825BFC10(ctx, base);
    static int c = 0;
    if (++c <= 5) {
        uint32_t queue = ctx.r3.u32;
        uint32_t item = queue ? PPC_LOAD_U32(queue + 8) : 0;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[VideoQueue #%d] queue=0x%08X [queue+8]=0x%08X\n", c, queue, item); fclose(f); }
    }
}
// Hook sub_826FE350 (comparison function) to see what's being compared
extern "C" void __imp__sub_826FE350(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_826FE350) {
    uint32_t data = ctx.r3.u32;
    uint32_t filter = ctx.r4.u32;
    __imp__sub_826FE350(ctx, base);
    static int c = 0;
    // Only log when filter matches VideoDriver's value (0x8204EB54-ish)
    if (++c <= 30 && (filter > 0x82000000 && filter < 0x83000000)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[Compare #%d] data=0x%08X filter=0x%08X -> match=%d\n",
            c, data, filter, ctx.r3.s32); fclose(f); }
    }
}

// Hook sub_82648648 - log ALL null returns and force first item
extern "C" void __imp__sub_82648648(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82648648) {
    uint32_t in_r3 = ctx.r3.u32;
    uint32_t in_r4 = ctx.r4.u32;
    __imp__sub_82648648(ctx, base);
    static int c = 0; c++;
    if (ctx.r3.u32 == 0) {
        // Returned null - force first item from queue
        uint32_t first_item = in_r3 ? PPC_LOAD_U32(in_r3 + 8) : 0;
        static int null_c = 0;
        if (++null_c <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[GetPlayItem NULL#%d] q=0x%08X f=0x%08X first=0x%08X\n",
                null_c, in_r3, in_r4, first_item); fclose(f); }
        }
        if (first_item) {
            ctx.r3.u64 = first_item;
        }
    }
}
PPC_FUNC(sub_821FBD10) {
    // Check the Bink data address used for queue lookup
    uint32_t bink_data = 0x8278CD08;
    uint32_t val0 = PPC_LOAD_U32(bink_data);
    uint32_t val4 = PPC_LOAD_U32(bink_data + 4);
    uint32_t val8 = PPC_LOAD_U32(bink_data + 8);
    static int c = 0;
    if (++c <= 3) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[VideoDriver #%d] bink_data[0x8278CD08]=%08X %08X %08X\n",
            c, val0, val4, val8); fclose(f); }
    }
    __imp__sub_821FBD10(ctx, base);
    if (c <= 3) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[VideoDriver #%d] EXIT r19=0x%08X r3=0x%08X\n", c, ctx.r19.u32, ctx.r3.u32); fclose(f); }
    }
}
TRACE_STATE("RenderA", 826365E0)
TRACE_STATE("RenderB", 8263DE08)
TRACE_STATE("RenderC", 8263DD80)
// GL2_Render with detailed state check
extern "C" void __imp__sub_8262FFE0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8262FFE0) {
    // [0x8372033C] = skip_flag at [r28+32] where r28=0x8372031C
    // If non-zero, GL2_Render returns immediately (no rendering)
    uint32_t skip_flag = PPC_LOAD_U32(0x8372033C);
    uint32_t render_skip = PPC_LOAD_U32(0x83720340);
    uint32_t frame_count = PPC_LOAD_U32(0x83720320 + 800); // frame counter at [r28+800]
    static int _c = 0; _c++;
    static int skip_count = 0;
    static int render_count = 0;
    if (skip_flag != 0) skip_count++;
    else render_count++;
    if (_c <= 30 || (_c % 300 == 0)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[GL2_Render #%d] skip=%u render_skip=%u frames=%u (rendered=%d skipped=%d)\n",
                _c, skip_flag, render_skip, frame_count, render_count, skip_count);
            fclose(f);
        }
    }
    __imp__sub_8262FFE0(ctx, base);
}
TRACE_STATE("RenderD", 82636688)
#undef TRACE_STATE
TRACE_CALL("BinkClean", 821FB070)
TRACE_CALL("LoadStart", 821FB318)
// ContentLoad with return value logging
extern "C" void __imp__sub_82107638(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82107638) {
    __imp__sub_82107638(ctx, base);
    static int c = 0; c++;
    if (c <= 5) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ContentLoad #%d] returned r3=%u\n", c, ctx.r3.u32); fclose(f); }
    }
}
TRACE_CALL("PreInit1", 82184260)
TRACE_CALL("PreInit2", 82636250)
TRACE_CALL("PreInit3", 822826B0)
TRACE_CALL("RenderSetup", 8220F3C0)
TRACE_CALL("InitWorld", 82189260)
TRACE_CALL("GameLoop2", 82186C10)
TRACE_CALL("Shutdown", 8220D3F0)
TRACE_CALL("GL2_Init1", 8236FAA0)
TRACE_CALL("GL2_Init2", 8220E5E0)
TRACE_CALL("GL2_Func1", 8216E338)
TRACE_CALL("GL2_Func2", 821700F0)
TRACE_CALL("GL2_Func3", 8220FD60)
// GL2_Render hooked above with skip_flag check
TRACE_CALL("GL2_Timer", 82717EC8)
// sub_8234C1C0 = physics update - crashes without proper world data
PPC_FUNC(sub_8234C1C0) {
    static int c = 0;
    if (++c <= 3) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GL2_Physics] STUBBED #%d\n", c); fclose(f); }
    }
    ctx.r3.u64 = 0;
}
TRACE_CALL("GL2_World", 82355E88)
TRACE_CALL("GL2_Spawn", 8265AF50)
TRACE_CALL("ThreadWrap", 82716078)
// sub_82716020 already hooked above for force-flag
// sub_82604B00 is the callback/event dispatcher used by the content loading system.
// Previously stubbed because it blocked on a critical section. Now let it run
// to allow loading callbacks to fire and update viewport loading flags.
extern "C" void __imp__sub_82604B00(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82604B00) {
    static int c = 0;
    c++;
    uint32_t arg = ctx.r3.u32;
    uint32_t cb_ptr = PPC_LOAD_U32(0x837102B4);
    // Log the function table that RunCallbacks scans
    // lis(-32134) = 0x827A0000, +0x1F20 = 0x827A1F20
    // Scan from 0x827A1F24 to 0x827A1F28 for non-null function pointers
    uint32_t ft0 = PPC_LOAD_U32(0x827A1F20);
    uint32_t ft1 = PPC_LOAD_U32(0x827A1F24);
    uint32_t ft2 = PPC_LOAD_U32(0x827A1F28);
    uint32_t cb_vt = cb_ptr ? PPC_LOAD_U32(cb_ptr) : 0;
    uint32_t cb_fn = cb_vt ? PPC_LOAD_U32(cb_vt + 4) : 0;
    if (c <= 10 || (c % 200 == 0)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[RunCallbacks #%d] r3=0x%08X cb=0x%08X cb_fn=0x%08X\n",
                c, arg, cb_ptr, cb_fn);
            fclose(f);
        }
    }
    __imp__sub_82604B00(ctx, base);
}

// sub_82604C10 blocks on a critical section after main loop exits.
// Stub it to return 0 to unblock the game thread.
PPC_FUNC(sub_82604C10) {
    static int c = 0;
    if (++c <= 3) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop1] STUBBED #%d\n", c); fclose(f); }
    }
    ctx.r3.u64 = 0;
}
TRACE_CALL("CreateThr", 82716028)
TRACE_CALL("WaitThr", 82716038)
// sub_82185498 (PostLoop5) - Previously crashed. Try running it now with
// all IO/threading/audio fixes in place.
extern "C" void __imp__sub_82185498(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82185498) {
    static int c = 0;
    c++;
    if (c <= 3) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop5] ENTER #%d r3=0x%08X r30=0x%08X\n", c, ctx.r3.u32, ctx.r30.u32); fclose(f); } }
    __imp__sub_82185498(ctx, base);
    if (c <= 3) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop5] EXIT #%d r3=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
}
TRACE_CALL("FrameRender", 82648D80)
#undef TRACE_CALL

// Instead of hooking sub_82186F08, override it completely to control the loop
// This lets us instrument the loop condition check
PPC_FUNC(sub_82186F08) {
    // Call the real function but intercept the loop
    __imp__sub_82186F08(ctx, base);
    FILE* hf = fopen("saintsrow_heartbeat.log", "a");
    if (hf) { fprintf(hf, "[LOOP-186F08] EXITED\n"); fclose(hf); }
}


// ============================================================================
// Critical Section tracing
// ============================================================================
extern "C" void __imp__RtlEnterCriticalSection(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__RtlLeaveCriticalSection(PPCContext& ctx, uint8_t* base);

PPC_FUNC(sub_82788704) {  // RtlEnterCriticalSection
    static int ec = 0;
    uint32_t cs_addr = ctx.r3.u32;
    // Check lock_count before entering - if contended, log it
    int32_t lock_count = (int32_t)PPC_LOAD_U32(cs_addr + 4);  // lock_count offset
    uint32_t owner = PPC_LOAD_U32(cs_addr + 12);  // owning_thread offset
    if (++ec <= 50 || lock_count >= 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[CS-ENTER #%d] cs=0x%08X lock=%d owner=0x%08X\n",
                ec, cs_addr, lock_count, owner);
            fclose(f);
        }
    }
    __imp__RtlEnterCriticalSection(ctx, base);
}

PPC_FUNC(sub_82788714) {  // RtlLeaveCriticalSection
    static int lc = 0;
    if (++lc <= 50) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[CS-LEAVE #%d] cs=0x%08X\n", lc, ctx.r3.u32);
            fclose(f);
        }
    }
    __imp__RtlLeaveCriticalSection(ctx, base);
}

// Force the "loading complete" flag after a few ticks
// The game waits for [0x8370D6C9] to become non-zero, but the loading
// pipeline doesn't complete due to stubbed subsystems. Force it after
// a short delay to let the game proceed.
// sub_82716020 - force loading complete flag after a few calls
extern "C" void __imp__sub_82716020(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82716020) {
    static int call_count = 0;
    call_count++;
    __imp__sub_82716020(ctx, base);
    if (call_count == 5) {
        PPC_STORE_U8(0x8370D6C9, 1);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[FORCE-FLAG] Set at 0x8370D6C9 after %d calls\n", call_count); fclose(f); }
    }
}

// Hook the ring buffer kick function.
// After the game writes its structure to the secondary buffer,
// extract the indirect buffer addresses and feed them to the CP.
extern "C" void __imp__sub_825D3580(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825D3580) {
    uint32_t r31 = ctx.r3.u32;
    uint32_t r4 = ctx.r4.u32; // where the structure will be written

    __imp__sub_825D3580(ctx, base);

    // The kick function wrote 10 dwords of PM4 at r4 in the secondary buffer.
    // But the game also writes rendering PM4 BEFORE this position.
    // Create an INDIRECT_BUFFER_PFD covering ALL data from the last position
    // to the current position (including the 10 kick dwords).
    auto* ks = REX_KERNEL_STATE();
    auto* gs = static_cast<rex::graphics::GraphicsSystem*>(ks->emulator()->graphics_system());
    auto* cp = gs->command_processor();

    uint32_t rb_prim = PPC_LOAD_U32(r31 + 13436);
    uint32_t rb_sec = PPC_LOAD_U32(r31 + 13440);

    // Track where we last created an indirect buffer reference
    static uint32_t last_ib_end = 0;
    static uint32_t prim_write_pos = 31;

    // The data between last_ib_end and r4+40 contains rendering PM4 + kick PM4
    uint32_t ib_start = last_ib_end ? last_ib_end : rb_sec;
    uint32_t ib_end = r4 + 40; // after kick's 10 dwords

    if (rb_prim && prim_write_pos < 8000) {
        // Only create an indirect buffer for the KICK's own 10 dwords of PM4.
        // These are known-valid: WAIT_FOR_IDLE + 2x EVENT_WRITE_SHD
        // The rendering PM4 between kicks needs separate handling.
        uint32_t phys_kick = ks->memory()->GetPhysicalAddress(r4);
        if (phys_kick != UINT32_MAX) {
            PPC_STORE_U32(rb_prim + prim_write_pos * 4, 0xC0013F00);
            PPC_STORE_U32(rb_prim + (prim_write_pos + 1) * 4, phys_kick);
            PPC_STORE_U32(rb_prim + (prim_write_pos + 2) * 4, 10);
            prim_write_pos += 3;

            // Also create an IB for rendering PM4 between last kick end and this kick.
            // Scan the data to find valid PM4 extent.
            if (last_ib_end > 0 && last_ib_end < r4) {
                uint32_t scan_start = last_ib_end;
                uint32_t scan_pos = 0;
                uint32_t scan_len = (r4 - scan_start) / 4;
                // The data starts with zero padding, then has valid PM4.
                // Skip leading zeros to find the PM4 start.
                uint32_t pm4_start = 0;
                while (pm4_start < scan_len && PPC_LOAD_U32(scan_start + pm4_start * 4) == 0) {
                    pm4_start++;
                }
                // Scan for contiguous valid PM4 packets from pm4_start
                scan_pos = pm4_start;
                while (scan_pos < scan_len) {
                    uint32_t hdr = PPC_LOAD_U32(scan_start + scan_pos * 4);
                    if (hdr == 0) {
                        // Skip zero padding (possible alignment)
                        scan_pos++;
                        continue;
                    }
                    uint32_t pkt_type = (hdr >> 30) & 3;
                    uint32_t count;
                    if (pkt_type == 0) {
                        count = ((hdr >> 16) & 0x3FFF) + 1;
                        if (count > 0x2000 || scan_pos + 1 + count > scan_len) break;
                        scan_pos += 1 + count;
                    } else if (pkt_type == 2) {
                        scan_pos += 1;
                    } else if (pkt_type == 3) {
                        count = ((hdr >> 16) & 0x3FFF) + 1;
                        if (count > 0x2000 || scan_pos + 1 + count > scan_len) break;
                        scan_pos += 1 + count;
                    } else {
                        break; // Type1 or invalid
                    }
                }
                if (scan_pos > pm4_start) {
                    // Create IB starting from where PM4 begins (after zero padding)
                    uint32_t pm4_addr = scan_start + pm4_start * 4;
                    uint32_t pm4_dwords = scan_pos - pm4_start;
                    uint32_t phys_render = ks->memory()->GetPhysicalAddress(pm4_addr);
                    if (phys_render != UINT32_MAX && prim_write_pos < 7990) {
                        PPC_STORE_U32(rb_prim + prim_write_pos * 4, 0xC0013F00);
                        PPC_STORE_U32(rb_prim + (prim_write_pos + 1) * 4, phys_render);
                        PPC_STORE_U32(rb_prim + (prim_write_pos + 2) * 4, pm4_dwords);
                        prim_write_pos += 3;

                        static int render_ib_c = 0;
                        if (++render_ib_c <= 3) {
                            FILE* rf = fopen("saintsrow_heartbeat.log", "a");
                            if (rf) {
                                fprintf(rf, "[Render-IB #%d] phys=0x%08X dwords=%u (skipped %u zeros)\n",
                                    render_ib_c, phys_render, pm4_dwords, pm4_start);
                                // Decode first few PM4 opcodes
                                uint32_t dp = 0;
                                for (int pkt = 0; pkt < 10 && dp < pm4_dwords; pkt++) {
                                    uint32_t h = PPC_LOAD_U32(pm4_addr + dp * 4);
                                    uint32_t pt = (h >> 30) & 3;
                                    if (pt == 3) {
                                        uint32_t op = (h >> 8) & 0xFF;
                                        uint32_t cnt = ((h >> 16) & 0x3FFF) + 1;
                                        fprintf(rf, "  [%u] Type3 op=0x%02X cnt=%u\n", dp, op, cnt);
                                        dp += 1 + cnt;
                                    } else if (pt == 0) {
                                        uint32_t reg = h & 0x7FFF;
                                        uint32_t cnt = ((h >> 16) & 0x3FFF) + 1;
                                        fprintf(rf, "  [%u] Type0 reg=0x%04X cnt=%u\n", dp, reg, cnt);
                                        dp += 1 + cnt;
                                    } else if (h == 0) {
                                        dp++; // skip zero
                                    } else {
                                        fprintf(rf, "  [%u] Type%u hdr=0x%08X\n", dp, pt, h);
                                        dp++;
                                    }
                                }
                                fclose(rf);
                            }
                        }
                    }
                }
            }

            cp->UpdateWritePointer(prim_write_pos);

            static int ib_c = 0;
            if (++ib_c <= 5) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) {
                    fprintf(f, "[IB #%d] kick=0x%08X prim_wp=%u last_end=0x%08X r4=0x%08X\n",
                        ib_c, phys_kick, prim_write_pos, last_ib_end, r4);
                    // Dump first dwords of the rendering range
                    if (last_ib_end > 0 && last_ib_end < r4) {
                        fprintf(f, "  render[0..7]: ");
                        for (int i = 0; i < 8; i++) fprintf(f, "%08X ", PPC_LOAD_U32(last_ib_end + i*4));
                        fprintf(f, "\n");
                    }
                    fclose(f);
                }
            }
        }

        last_ib_end = r4 + 40;
    }

    static int kick_trace = 0;
    if (++kick_trace <= 5) {
        uint32_t d[10];
        for (int i = 0; i < 10; i++) d[i] = PPC_LOAD_U32(r4 + i*4);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[Kick #%d] @0x%08X: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
                kick_trace, r4, d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8],d[9]);
            fclose(f);
        }
    }
}

// Hook GPU ring buffer init function to trace why VdInitializeRingBuffer is never called
extern "C" void __imp__sub_825D3DA8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825D3DA8) {
    uint32_t r31 = ctx.r3.u32; // first arg = render state
    uint32_t r26_arg = ctx.r4.u32; // second arg = ring buffer config (r26 inside function)
    uint32_t rb_10780 = PPC_LOAD_U32(r31 + 10780);
    uint32_t rb_13504 = PPC_LOAD_U32(r31 + 13504);
    // Check what r26 (r4) points to - this controls ring buffer allocation
    uint32_t r26_v4 = r26_arg ? PPC_LOAD_U32(r26_arg + 4) : 0;
    uint32_t r26_v8 = r26_arg ? PPC_LOAD_U32(r26_arg + 8) : 0;
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RB-Init] ENTER r3=0x%08X r4=0x%08X [10780]=%u [13504]=0x%08X r4[4]=0x%08X r4[8]=0x%08X\n",
        r31, r26_arg, rb_10780, rb_13504, r26_v4, r26_v8); fclose(f); }
    __imp__sub_825D3DA8(ctx, base);
    uint32_t ret = ctx.r3.u32;
    uint32_t rb_10772 = PPC_LOAD_U32(r31 + 10772);
    uint32_t rb_10768 = PPC_LOAD_U32(r31 + 10768);
    uint32_t rb_13436 = PPC_LOAD_U32(r31 + 13436);
    uint32_t rb_13440 = PPC_LOAD_U32(r31 + 13440);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RB-Init] EXIT ret=%u [10780]=%u [10772]=0x%08X [10768]=0x%08X [13436]=0x%08X [13440]=0x%08X\n",
        ret, PPC_LOAD_U32(r31 + 10780), rb_10772, rb_10768, rb_13436, rb_13440); fclose(f); }

    // Use the PRIMARY ring buffer. It starts with 31 dwords of valid PM4
    // (ME_INIT + 4 INDIRECT_BUFFER_PFD). We'll append more INDIRECT_BUFFER
    // references as the kick function produces them.
    if (rb_13436 != 0) {
        auto* ks = REX_KERNEL_STATE();
        auto* gs = static_cast<rex::graphics::GraphicsSystem*>(ks->emulator()->graphics_system());
        auto* cp = gs->command_processor();

        uint32_t rb_phys = ks->memory()->GetPhysicalAddress(rb_13436);
        if (rb_phys != UINT32_MAX) {
            // Primary ring buffer at 0xE98B7000, size 0x8000 (32KB)
            int size_log2 = 12;
            cp->InitializeRingBuffer(rb_phys, size_log2);

            // Enable read pointer writeback
            if (rb_10768) {
                uint32_t rptr_wb = ks->memory()->GetPhysicalAddress(rb_10768);
                if (rptr_wb != UINT32_MAX) {
                    cp->EnableReadPointerWriteBack(rptr_wb, 6);
                }
            }

            f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[RB-Init] MANUAL InitRB secondary rb_virt=0x%08X rb_phys=0x%08X size_log2=%d\n",
                rb_13440, rb_phys, size_log2); fclose(f); }
        }
    }
}

// VdSwap override - intercept the actual SDK import symbol
// The recompiled code calls __imp__VdSwap directly, NOT through sub_827889E4
// Use PPC_FUNC_IMPL to define the extern "C" symbol that the recompiled code calls
// The linker's /force:multiple flag lets us override the SDK's definition
//
// VdSwap_entry takes 10 args via HostToGuestFunction:
//   r3=buffer_ptr, r4=fetch_ptr, r5=unk2, r6=unk3, r7=unk4,
//   r8=frontbuffer_ptr, r9=texture_format_ptr, r10=color_space_ptr,
//   stack[0]=width, stack[1]=height
//
// We need to call the SDK's actual VdSwap_entry. Since we override __imp__VdSwap,
// we'll call VdSwap_entry directly via HostToGuestFunction.
#include <rex/ppc/function.h>
namespace rex { namespace kernel { namespace xboxkrnl {
    extern void VdSwap_entry(ppc_pvoid_t, ppc_pvoid_t, ppc_pvoid_t, ppc_pvoid_t,
        ppc_pvoid_t, ppc_pu32_t, ppc_pu32_t, ppc_pu32_t, ppc_pu32_t, ppc_pu32_t);
}}}

PPC_FUNC_IMPL(__imp__VdSwap) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[VdSwap INTERCEPTED #%d] r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X r10=0x%08X\n",
                c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32);
            // Dump fetch constant at r4 (6 dwords)
            if (ctx.r4.u32) {
                uint32_t f0 = PPC_LOAD_U32(ctx.r4.u32), f1 = PPC_LOAD_U32(ctx.r4.u32+4);
                uint32_t f2 = PPC_LOAD_U32(ctx.r4.u32+8), f3 = PPC_LOAD_U32(ctx.r4.u32+12);
                fprintf(f, "  fetch[0..3]=%08X %08X %08X %08X\n", f0, f1, f2, f3);
                // base_address is in fetch dword 0 bits [31:12] (or similar)
                fprintf(f, "  fetch.base_addr_raw = 0x%08X (<<12 = 0x%08X)\n", f0 >> 12, (f0 >> 12) << 12);
            }
            // Dump frontbuffer ptr at r8
            if (ctx.r8.u32) {
                fprintf(f, "  *frontbuffer_ptr = 0x%08X\n", PPC_LOAD_U32(ctx.r8.u32));
            }
            fclose(f);
        }
    }
    // Save buffer_ptr before call to check what VdSwap writes
    uint32_t buf_addr = ctx.r3.u32;

    // Call the real SDK implementation
    rex::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);

    // After VdSwap writes packets to ring buffer, we need to kick the
    // command processor. The game's ring buffer init (sub_825DAB58) is never
    // called, so VdInitializeRingBuffer never fires. We must:
    // 1. Initialize the ring buffer on first call
    // 2. Update the write pointer after each VdSwap
    {
        auto* ks = REX_KERNEL_STATE();
        auto* gs = static_cast<rex::graphics::GraphicsSystem*>(ks->emulator()->graphics_system());
        auto* cp = gs->command_processor();

        // Bypass the ring buffer entirely and call IssueSwap directly.
        // VdSwap wrote the swap packet with:
        //   frontbuffer physical address = 0x09258000 (from buf[9])
        //   width = 1280, height = 720 (from buf[10], buf[11])
        // Also write the fetch constant to GPU register file first.
        uint32_t fb_phys = PPC_LOAD_U32(buf_addr + 36); // buf[9] = frontbuffer phys
        uint32_t width = PPC_LOAD_U32(buf_addr + 40);   // buf[10]
        uint32_t height = PPC_LOAD_U32(buf_addr + 44);  // buf[11]

        // Read the fetch constant data (6 dwords after the Type0 header)
        uint32_t fetch[6];
        for (int i = 0; i < 6; i++) {
            fetch[i] = PPC_LOAD_U32(buf_addr + 4 + i*4);
        }

        // Trace flush function gate conditions to understand what's blocking
        {
            uint32_t rs = 0x40001E00;
            uint8_t v10808 = PPC_LOAD_U8(rs + 10808);
            uint8_t v10809 = PPC_LOAD_U8(rs + 10809);
            uint32_t v12960 = PPC_LOAD_U32(rs + 12960);
            uint32_t v13160 = PPC_LOAD_U32(rs + 13160);
            uint32_t v13528 = PPC_LOAD_U32(rs + 13528);
            uint32_t v40 = PPC_LOAD_U32(rs + 40);
            static int gate_c = 0;
            if (++gate_c <= 5) {
                FILE* gf = fopen("saintsrow_heartbeat.log", "a");
                if (gf) {
                    fprintf(gf, "[Flush-Gates #%d] [10808]=0x%02X [10809]=0x%02X [12960]=0x%08X [13160]=0x%08X [13528]=0x%08X [40]=0x%08X\n",
                        gate_c, v10808, v10809, v12960, v13160, v13528, v40);
                    // sub_825D3660 gate analysis:
                    // Gate 1: [10809] bit 0x40 -> if SET, skip to end (loc_825D379C)
                    fprintf(gf, "  Gate1 [10809]&0x40=%d (must be 0)\n", (v10809 & 0x40) != 0);
                    // Gate 2: [10808] & 0xFFFFFF80 -> if non-zero, go check [13160]
                    fprintf(gf, "  Gate2 [10808]&0x80=%d (0=direct, 1=check 13160)\n", (v10808 & 0x80) != 0);
                    // Gate 3: [13160] -> if 0, skip to loc_825D375C (direct flush path)
                    fprintf(gf, "  Gate3 [13160]==0: %d (0=direct flush, 1=check further)\n", v13160 == 0);
                    if (v13160) {
                        uint32_t v13160_152 = PPC_LOAD_U32(v13160 + 152);
                        fprintf(gf, "  Gate4 [[13160]+152]=0x%08X (must be 0 for flush)\n", v13160_152);
                    }
                    // Gate 5: [12960] -> controls the flush path at loc_825D36F8
                    fprintf(gf, "  Gate5 [12960]=0x%08X (non-zero enables indirect flush)\n", v12960);
                    fclose(gf);
                }
            }
        }

        // The kick function (sub_825D3580) handles UpdateWritePointer
        // by appending INDIRECT_BUFFER_PFD to the primary ring buffer.

        // Write a test pattern to the framebuffer to verify display pipeline.
        // The framebuffer at 0x09258000 is a 1280x720 RGBA8 tiled texture.
        // Xbox 360 uses a specific tiling pattern, but for testing, just write
        // a gradient pattern to the PHYSICAL memory backing the texture.
        // The texture cache will pick it up on the next IssueSwap.
        {
            auto* mem = ks->memory();
            // Write directly to PHYSICAL memory (what the GPU texture cache reads)
            uint8_t* fb_host = mem->TranslatePhysical(fb_phys);
            static int frame_num = 0;
            frame_num++;

            // Write a simple color gradient - each frame slightly different
            // Xbox 360 textures are tiled, so this won't look right geometrically,
            // but ANY non-black color proves the display pipeline works.
            uint32_t color;
            int phase = (frame_num / 30) % 6;
            switch (phase) {
                case 0: color = 0xFF0000FF; break; // Red
                case 1: color = 0x00FF00FF; break; // Green
                case 2: color = 0x0000FFFF; break; // Blue
                case 3: color = 0xFFFF00FF; break; // Yellow
                case 4: color = 0xFF00FFFF; break; // Magenta
                case 5: color = 0x00FFFFFF; break; // Cyan
            }
            // Fill a portion of the framebuffer with the color
            // Framebuffer is 1280*720*4 = 3,686,400 bytes
            uint32_t* fb32 = (uint32_t*)fb_host;
            for (uint32_t i = 0; i < 1280 * 720; i++) {
                fb32[i] = color;
            }
            // Invalidate texture cache so GPU picks up our changes
            cp->InvalidateGpuMemory();
        }

        // Write fetch constants and issue swap on command processor thread
        static int swap_call = 0;
        int this_call = ++swap_call;
        cp->CallInThread([cp, fb_phys, width, height, fetch, this_call]() {
            // Write fetch constant 0 (registers 0x4800-0x4805)
            cp->RestoreRegisters(0x4800, fetch, 6, true);
            cp->IssueSwap(fb_phys, width, height);
        });

        static int kick_c = 0;
        if (++kick_c <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[VdSwap-Kick #%d] IssueSwap fb=0x%08X %ux%u\n",
                kick_c, fb_phys, width, height); fclose(f); }
        }
    }
}

// Trace GPU command buffer writer - this is the core function that writes PM4 to the command buffer
extern "C" void __imp__sub_825CC640(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825CC640) {
    static int c = 0;
    c++;
    if (c <= 20 || (c % 1000 == 0)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GPU-Write #%d] r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_825CC640(ctx, base);
}

// Track draw dispatch functions - these set up draw calls that should generate PM4_DRAW_INDX
extern "C" void __imp__sub_825CCAB0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825CCAB0) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[DrawSetup #%d] r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_825CCAB0(ctx, base);
}

// Track sub_825CCB78 - complex draw state machine dispatching different draw modes
extern "C" void __imp__sub_825CCB78(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825CCB78) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[DrawStateMachine #%d] r3=0x%08X r4=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_825CCB78(ctx, base);
}

// Track shader/state setup
extern "C" void __imp__sub_825CC5B0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825CC5B0) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ShaderSetup #%d] r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_825CC5B0(ctx, base);
}

// Stub XamInputGetKeystrokeEx - the SDK's input_system() returns null,
// causing a crash at NULL+8 in GetKeystroke(). Override the SDK import directly.
// The func_mapping maps 0x82788EE4 -> __imp__XamInputGetKeystrokeEx,
// so PPC_FUNC(sub_82788EE4) doesn't intercept calls.
// Use PPC_FUNC_IMPL to override the SDK's __imp__XamInputGetKeystrokeEx.
PPC_FUNC_IMPL(__imp__XamInputGetKeystrokeEx) {
    // r3 = user_index_ptr, r4 = flags, r5 = keystroke_ptr
    // X_INPUT_KEYSTROKE: [+0] u16 virtual_key, [+2] u16 unicode, [+4] u16 flags, [+6] u8 user, [+7] u8 hid
    uint32_t keystroke_ptr = ctx.r5.u32;
    if (keystroke_ptr) {
        for (int i = 0; i < 2; i++) PPC_STORE_U32(keystroke_ptr + i*4, 0);
    }
    static int call_total = 0;
    call_total++;
    uint32_t game_state = PPC_LOAD_U32(0x8370DD7C);
    if (call_total <= 5 || (game_state >= 3 && call_total <= 50)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GetKeystrokeEx #%d] state=%u r3=0x%08X r4=0x%08X r5=0x%08X\n",
            call_total, game_state, ctx.r3.u32, ctx.r4.u32, keystroke_ptr); fclose(f); }
    }

    // After loading (state >= 3), inject button presses to navigate past attract mode
    if (game_state >= 3 && keystroke_ptr) {
        static int ks_frame = 0;
        ks_frame++;
        // Simulate Start press at frame 100, A press at frame 300
        // Each press needs KEYDOWN then KEYUP
        uint16_t vk = 0;
        uint16_t flags = 0;
        if (ks_frame == 100 || ks_frame == 500 || ks_frame == 900) {
            vk = 0x5814; // VK_PAD_START
            flags = 0x0001; // KEYDOWN
        } else if (ks_frame == 105 || ks_frame == 505 || ks_frame == 905) {
            vk = 0x5814; // VK_PAD_START
            flags = 0x0002; // KEYUP
        } else if (ks_frame == 300 || ks_frame == 700 || ks_frame == 1100) {
            vk = 0x5800; // VK_PAD_A
            flags = 0x0001; // KEYDOWN
        } else if (ks_frame == 305 || ks_frame == 705 || ks_frame == 1105) {
            vk = 0x5800; // VK_PAD_A
            flags = 0x0002; // KEYUP
        }
        if (vk) {
            PPC_STORE_U16(keystroke_ptr + 0, vk);
            PPC_STORE_U16(keystroke_ptr + 2, 0);  // unicode
            PPC_STORE_U16(keystroke_ptr + 4, flags);
            PPC_STORE_U8(keystroke_ptr + 6, 0);   // user_index
            PPC_STORE_U8(keystroke_ptr + 7, 0);   // hid_code
            static int key_c = 0;
            if (++key_c <= 20) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[Keystroke] frame=%d vk=0x%04X flags=0x%04X\n",
                    ks_frame, vk, flags); fclose(f); }
            }
            ctx.r3.u64 = 0; // X_ERROR_SUCCESS
            return;
        }
    }

    ctx.r3.u64 = 0x80070002; // X_ERROR_EMPTY (no keystroke, device connected)
}

// Stub XamInputSetState - also crashes on null input_system()
PPC_FUNC_IMPL(__imp__XamInputSetState) {
    ctx.r3.u64 = 0x80070481; // X_ERROR_DEVICE_NOT_CONNECTED
}

// Stub XamInputGetState - return connected controller for player 0.
// After loading completes, simulate button presses to skip attract mode.
// XINPUT_STATE layout (big-endian):
//   [+0] uint32 dwPacketNumber
//   [+4] uint16 wButtons   (XINPUT_GAMEPAD_START=0x0010, GAMEPAD_A=0x1000)
//   [+6] uint8  bLeftTrigger
//   [+7] uint8  bRightTrigger
//   [+8..15] thumbsticks (int16 x4)
static std::atomic<int> g_input_frame{0};
PPC_FUNC_IMPL(__imp__XamInputGetState) {
    uint32_t user = ctx.r3.u32;
    uint32_t state_ptr = ctx.r5.u32;
    if (user != 0) {
        ctx.r3.u64 = 0x80070481; // NOT_CONNECTED for users 1-3
        return;
    }
    // Zero the struct first
    if (state_ptr) {
        for (int i = 0; i < 8; i++) PPC_STORE_U32(state_ptr + i*4, 0);
    }
    // Check game state - press buttons after loading
    uint32_t game_state = PPC_LOAD_U32(0x8370DD7C);
    if (game_state >= 3 && state_ptr) {
        int frame = g_input_frame.fetch_add(1);
        uint16_t buttons = 0;
        // Press Start at frame 30-35, then A at frame 60-65
        // (brief pulses to simulate button presses, then release)
        if ((frame >= 30 && frame < 35) || (frame >= 120 && frame < 125)) {
            buttons = 0x0010; // XINPUT_GAMEPAD_START
        } else if ((frame >= 60 && frame < 65) || (frame >= 150 && frame < 155)) {
            buttons = 0x1000; // XINPUT_GAMEPAD_A
        }
        if (buttons) {
            // Write buttons as big-endian uint16 at offset 4
            PPC_STORE_U16(state_ptr + 4, buttons);
            static int btn_c = 0;
            if (++btn_c <= 10) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[Input] frame=%d buttons=0x%04X\n", frame, buttons); fclose(f); }
            }
        }
        // Increment packet number so game detects change
        static uint32_t pkt = 1;
        PPC_STORE_U32(state_ptr, pkt++);
    }
    ctx.r3.u64 = 0; // X_ERROR_SUCCESS (connected)
}

// Hook NtAllocateVirtualMemory to log failures (BaseHeap::Alloc page count too big)
extern "C" void __imp__NtAllocateVirtualMemory(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_827893D4) { // NtAllocateVirtualMemory thunk
    uint32_t base_addr_ptr = ctx.r3.u32;
    uint32_t region_size_ptr = ctx.r4.u32;
    uint32_t alloc_type = ctx.r5.u32;
    uint32_t protect = ctx.r6.u32;
    uint32_t input_base = base_addr_ptr ? PPC_LOAD_U32(base_addr_ptr) : 0;
    uint32_t input_size = region_size_ptr ? PPC_LOAD_U32(region_size_ptr) : 0;

    __imp__NtAllocateVirtualMemory(ctx, base);

    uint32_t result = ctx.r3.u32;
    if (result != 0) { // failed
        static int fail_c = 0;
        if (++fail_c <= 30) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) {
                fprintf(f, "[NtAllocVM FAIL #%d] base=0x%08X size=0x%08X (%u KB) type=0x%X prot=0x%X -> 0x%08X\n",
                    fail_c, input_base, input_size, input_size / 1024, alloc_type, protect, result);
                fclose(f);
            }
        }
    } else {
        uint32_t out_addr = base_addr_ptr ? PPC_LOAD_U32(base_addr_ptr) : 0;
        uint32_t out_size = region_size_ptr ? PPC_LOAD_U32(region_size_ptr) : 0;
        static int ok_c = 0;
        if (++ok_c <= 30 || input_size >= 0x100000) { // log first 30 + any large allocs
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) {
                fprintf(f, "[NtAllocVM OK #%d] base=0x%08X size=0x%08X -> addr=0x%08X size=0x%08X (%u KB)\n",
                    ok_c, input_base, input_size, out_addr, out_size, out_size / 1024);
                fclose(f);
            }
        }
    }
}

// Capture worker thread semaphore handles and pump them from the main thread.
// The IO completion callbacks never fire (sub_826E7190 is never called), so
// the worker thread semaphores are never signaled after the initial count.
// We signal them periodically from the GameUpdate hook to keep loading progressing.
#include <rex/system/xsemaphore.h>

// ============================================================================
// KeInitializeSemaphore Override
// ============================================================================
// The game reinitializes IO semaphores on every IOWork call. Before calling
// KeInitializeSemaphore, the game code manually resets the dispatch header's
// wait_list_flink/blink (linked list init). This destroys the SDK's
// kXObjSignature magic value that associates guest memory with native objects.
// Result: a NEW semaphore object is created each time, while old IO threads
// still wait on the OLD semaphore. Loading never completes.
//
// Fix: detect re-initialization and preserve the existing native object.
// On re-init, just reset signal_state instead of creating a new semaphore.
namespace rex { namespace kernel { namespace xboxkrnl {
    extern void KeInitializeSemaphore_entry(ppc_ptr_t<rex::system::X_KSEMAPHORE>, ppc_u32_t, ppc_u32_t);
}}}

// Track semaphore addresses that have been initialized
static constexpr int MAX_TRACKED_SEMS = 32;
static uint32_t g_tracked_sem_addrs[MAX_TRACKED_SEMS] = {};
static uint32_t g_tracked_sem_flink[MAX_TRACKED_SEMS] = {};
static uint32_t g_tracked_sem_blink[MAX_TRACKED_SEMS] = {};
static int g_tracked_sem_count = 0;

PPC_FUNC_IMPL(__imp__KeInitializeSemaphore) {
    uint32_t sem_addr = ctx.r3.u32;
    uint32_t count = ctx.r4.u32;
    uint32_t limit = ctx.r5.u32;

    // Check if this semaphore was previously initialized
    for (int i = 0; i < g_tracked_sem_count; i++) {
        if (g_tracked_sem_addrs[i] == sem_addr) {
            // Re-initialization! Restore the SDK's magic values first,
            // then let the SDK reinitialize properly.
            // The game code clobbered wait_list_flink/blink with self-pointers.
            // Restore kXObjSignature so GetNativeObject finds the existing object.
            PPC_STORE_U32(sem_addr + 8, g_tracked_sem_flink[i]);   // wait_list_flink
            PPC_STORE_U32(sem_addr + 12, g_tracked_sem_blink[i]);  // wait_list_blink

            // Set signal_state. The game passes count=0 to block threads until
            // work is queued. But with handle reuse, old threads are already
            // waiting on this semaphore. Setting count=0 keeps them blocked.
            // Set to 2 (matching the 2 IO threads) so they wake up and check
            // for new work items.
            uint32_t effective_count = (count == 0) ? 2 : count;
            PPC_STORE_U32(sem_addr + 4, effective_count);  // signal_state

            static int reinit_c = 0;
            if (++reinit_c <= 10) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[KeInitSem] REINIT @0x%08X count=%u (preserved handle=0x%08X)\n",
                    sem_addr, count, g_tracked_sem_blink[i]); fclose(f); }
            }
            return;
        }
    }

    // First-time initialization - call real SDK implementation
    rex::HostToGuestFunction<rex::kernel::xboxkrnl::KeInitializeSemaphore_entry>(ctx, base);

    // Track the handle that was stashed
    if (g_tracked_sem_count < MAX_TRACKED_SEMS) {
        uint32_t flink = PPC_LOAD_U32(sem_addr + 8);
        uint32_t blink = PPC_LOAD_U32(sem_addr + 12);
        g_tracked_sem_addrs[g_tracked_sem_count] = sem_addr;
        g_tracked_sem_flink[g_tracked_sem_count] = flink;
        g_tracked_sem_blink[g_tracked_sem_count] = blink;
        g_tracked_sem_count++;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[KeInitSem] NEW @0x%08X count=%u limit=%u flink=0x%08X blink=0x%08X (total=%d)\n",
            sem_addr, count, limit, flink, blink, g_tracked_sem_count); fclose(f); }
    }
}

// Hook worker thread (sub_826368E0) to capture semaphore handles and work function
extern "C" void __imp__sub_826368E0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_826368E0) {
    uint32_t context = ctx.r3.u32;
    uint32_t sem_handle = context ? PPC_LOAD_U32(context + 64) : 0;
    uint32_t work_func = context ? PPC_LOAD_U32(context + 52) : 0;
    if (sem_handle && g_worker_sem_count < 16) {
        bool found = false;
        for (int i = 0; i < g_worker_sem_count; i++) {
            if (g_worker_sem_handles[i] == sem_handle) { found = true; break; }
        }
        if (!found) {
            g_worker_sem_handles[g_worker_sem_count++] = sem_handle;
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[WorkerThread] ctx=0x%08X sem=0x%08X work=0x%08X (total=%d)\n",
                context, sem_handle, work_func, g_worker_sem_count); fclose(f); }
        }
    }
    __imp__sub_826368E0(ctx, base);
}

// Hook streaming manager callback (sub_8265F720) and work finder (sub_8265F4F0)
extern "C" void __imp__sub_8265F4F0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8265F4F0) {
    uint32_t filter = ctx.r3.u32;
    // Check the pending request count at 0x827A6BC4
    uint32_t req_count = PPC_LOAD_U8(0x827A6BC4);
    __imp__sub_8265F4F0(ctx, base);
    static int c = 0;
    if (++c <= 20 || (c % 200 == 0)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[StreamFind #%d] filter=0x%08X req_count=%u -> result=0x%08X\n",
            c, filter, req_count, ctx.r3.u32); fclose(f); }
    }
}

extern "C" void __imp__sub_8265F720(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8265F4F0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8265F720) {
    static int c = 0;
    c++;
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[StreamCallback #%d] ENTER\n", c); fclose(f); }
    }

    // The real callback deadlocks on 2nd+ call because sub_82604BE8
    // (the IO completion processor at [0x827A1F28]) enters a blocking IO chain.
    // Hook sub_82604BE8 to make it non-blocking by stubbing its call to sub_8260C318.
    __imp__sub_8265F720(ctx, base);

    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[StreamCallback #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook worker work function (sub_82636BB0) to also pump the streaming callback.
// The workers have valid PPC contexts and TLS. When they find no work items,
// they normally just return. We use that opportunity to call the streaming
// callback which would otherwise deadlock on the main thread.
extern "C" void __imp__sub_82636BB0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82636BB0) {
    uint32_t context = ctx.r3.u32;
    uint32_t item_before = context ? PPC_LOAD_U32(context + 44) : 0;
    __imp__sub_82636BB0(ctx, base);
    uint32_t item_after = context ? PPC_LOAD_U32(context + 44) : 0;
    static int c = 0;
    if (++c <= 10 || item_after != 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[WorkerRun #%d] item: 0x%08X -> 0x%08X\n", c, item_before, item_after); fclose(f); }
    }
}

// Hook sub_82604BE8 (IO completion processor) to prevent deadlock.
// This function calls sub_8260C318 which blocks on synchronous IO.
// Instead of blocking, call sub_8260C318 on a background thread and
// return immediately. When the thread completes, it will signal completion.
// Hook sub_8260C318 - the IO completion function that deadlocks on synchronous IO.
// It calls sub_8260C420 (init), sub_82623250 (resource lookup), and vtable dispatch.
// The deadlock is in the resource lookup or vtable dispatch.
// Stub it to return "not ready" (-1) so the caller retries later.
extern "C" void __imp__sub_8260C318(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260C318) {
    static int c = 0;
    c++;
    // Let the first call through (initial setup)
    if (true) { // passthrough ALL calls (test on main thread)
        if (c <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOComp #%d] passthrough r3=0x%08X r4=0x%08X r5=0x%08X\n",
                c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
        }
        __imp__sub_8260C318(ctx, base);
        if (c <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOComp #%d] passthrough returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
        }
    } else {
        // Call sub_8260C318 but set r4 (callback context) to a safe non-zero value.
        // The function stores r4 at stack[156] and later loads it as r29 for a vtable
        // dispatch. When r4=0, it reads from the null pool and deadlocks.
        // Set r4 to a known-safe dummy that will cause the vtable check to fail gracefully.
        // If r4 is non-zero but points to an object with vtable[5]=0, the call is skipped.
        // Use a null-pool address that returns 0 for all reads.
        PPCContext io_ctx = ctx;
        io_ctx.r4.u64 = 0x0F000100; // null pool area - reads return 0
        __imp__sub_8260C318(io_ctx, base);
        ctx.r3.u64 = io_ctx.r3.u64;
        if (c <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOComp #%d] returned r3=%d\n", c, io_ctx.r3.s32); fclose(f); }
        }
        static int stub_c = 0;
        if (++stub_c <= 5 || stub_c % 100 == 0) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOComp #%d] STUB (skip deadlocking IO) r3=0x%08X r4=0x%08X\n",
                c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
        }
    }
}

// IO sync objects at fixed global addresses (computed from lis -31957):
//   0x832AD65C = dispatch header A
//   0x832AD66C = SEMAPHORE (KeInitializeSemaphore called - handled by override)
//   0x832AD680 = EVENT B (IO threads KeWaitForSingleObject on THIS)
//   0x832AD690 = sync object C
//   0x832AD6A0 = sync object D
// The game code manually writes type/signal_state/flink/blink before
// KeInitializeSemaphore, which destroys the SDK's kXObjSignature association.
// We capture these after first init and restore them on re-init.
static constexpr uint32_t IO_SYNC_ADDRS[] = {0x832AD65Cu, 0x832AD680u, 0x832AD690u, 0x832AD6A0u};
static constexpr int IO_SYNC_COUNT = 4;
static uint32_t io_sync_flink[IO_SYNC_COUNT] = {};
static uint32_t io_sync_blink[IO_SYNC_COUNT] = {};
static bool io_sync_captured = false;

// Hook sub_8260CC50 - called after IORead, does the actual work
extern "C" void __imp__sub_8260CC50(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260CC50) {
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOWork #%d] r3=0x%08X r4=0x%08X ENTER\n", c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }

    static bool io_initialized = false;
    if (!io_initialized) {
        io_initialized = true;
        __imp__sub_8260CC50(ctx, base);

        // Capture SDK handles for all IO sync objects after first init.
        // The SDK's GetNativeObject sets wait_list_flink = kXObjSignature
        // and wait_list_blink = handle. Save these for restoration.
        for (int i = 0; i < IO_SYNC_COUNT; i++) {
            io_sync_flink[i] = PPC_LOAD_U32(IO_SYNC_ADDRS[i] + 8);
            io_sync_blink[i] = PPC_LOAD_U32(IO_SYNC_ADDRS[i] + 12);
        }
        io_sync_captured = true;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[IOWork] Captured sync objects:");
            for (int i = 0; i < IO_SYNC_COUNT; i++) {
                fprintf(f, " @0x%08X[fl=0x%08X bl=0x%08X]", IO_SYNC_ADDRS[i],
                    io_sync_flink[i], io_sync_blink[i]);
            }
            fprintf(f, "\n");
            fclose(f);
        }
    } else {
        // Re-init: use handle reuse after a limit to prevent thread exhaustion.
        // IO threads exit after their batch, so real creation is needed, but
        // too many causes STATUS_NO_MEMORY. Cap at 200 real IOWork calls
        // (200 * 2 threads = 400 threads max, well within limits).
        static int real_count = 0;
        bool use_reuse = (real_count >= 200);
        if (use_reuse) {
            g_skip_thread_creation.store(true);
        } else {
            real_count++;
        }
        if (c <= 20) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOWork #%d] %s r3=0x%08X (real=%d)\n",
                c, use_reuse ? "REUSE" : "REAL", ctx.r3.u32, real_count); fclose(f); }
        }
        __imp__sub_8260CC50(ctx, base);
        if (use_reuse) {
            g_skip_thread_creation.store(false);
        }

        if (c <= 20) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[IOWork #%d] returned r3=%d\n", c, ctx.r3.s32); fclose(f); }
        }
    }
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOWork #%d] returned r3=%d\n", c, ctx.r3.s32); fclose(f); }
    }
}

// Hook sub_8260FFC8 - the actual IO work function called from sub_8260CC50.
// After the first successful call, sub_8260CC50 tries to create 6 IO threads
// which deadlocks. Return null on 2nd+ calls to skip thread creation.
extern "C" void __imp__sub_8260FFC8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260FFC8) {
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IODoWork #%d] r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
    __imp__sub_8260FFC8(ctx, base);
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IODoWork #%d] returned 0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82604F38 (registration loop) - called from IOWork to register IO items
extern "C" void __imp__sub_82604F38(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82604F38) {
    static int c = 0;
    c++;
    if (c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RegLoop #%d] ENTER r3=0x%08X r4=0x%08X r5=0x%08X\n",
            c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_82604F38(ctx, base);
    if (c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RegLoop #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82622CB8 - called from IOWork after registration, sets up IO completion
extern "C" void __imp__sub_82622CB8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82622CB8) {
    static int c = 0;
    c++;
    uint32_t r4_arg = ctx.r4.u32; // output pointer (r30+64)
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOSetup #%d] ENTER r3=0x%08X r4=0x%08X\n",
            c, ctx.r3.u32, r4_arg); fclose(f); }
    }
    __imp__sub_82622CB8(ctx, base);
    if (c <= 10) {
        // Trace the vtable chain that IOWork will follow after we return:
        // [r4] -> obj -> [obj+68] -> sub_obj -> [sub_obj+0] -> vtable -> [vtable+28] -> target
        uint32_t obj = r4_arg ? PPC_LOAD_U32(r4_arg) : 0;
        uint32_t sub_obj = obj ? PPC_LOAD_U32(obj + 68) : 0;
        uint32_t vtable = sub_obj ? PPC_LOAD_U32(sub_obj) : 0;
        uint32_t vt28 = vtable ? PPC_LOAD_U32(vtable + 28) : 0;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOSetup #%d] returned r3=0x%08X | vtable chain: [r4]=0x%08X [+68]=0x%08X vt=0x%08X vt[28]=0x%08X\n",
            c, ctx.r3.u32, obj, sub_obj, vtable, vt28); fclose(f); }
    }
}

// Hook sub_8260D068 - thread config generator inside IOWork
extern "C" void __imp__sub_8260D068(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260D068) {
    static int c = 0;
    c++;
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ThreadCfg #%d] ENTER r3=0x%08X r4=0x%08X\n",
            c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_8260D068(ctx, base);
    if (c <= 10) {
        // Log the thread config buffer that was filled at r4
        uint32_t cfg_addr = ctx.r4.u32;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[ThreadCfg #%d] returned cfg@0x%08X: ", c, cfg_addr);
            if (cfg_addr) {
                for (int i = 0; i < 20; i++) {
                    fprintf(f, "%02X ", PPC_LOAD_U8(cfg_addr + i));
                }
            }
            fprintf(f, "\n");
            fclose(f);
        }
    }
}

// Hook sub_82600A68 - audio driver registration called from IOWork vtable[28].
// First call registers the audio render driver client (needed for XMA/audio).
// Second+ calls deadlock: XAudioRegisterRenderDriverClient acquires
// global_critical_region_ which the audio callback thread already holds.
extern "C" void __imp__sub_82600A68(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82600A68) {
    static int c = 0;
    c++;
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (c == 1) {
        if (f) { fprintf(f, "[AudioReg #%d] ENTER r3=0x%08X r4=0x%08X (first call - passthrough)\n",
            c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
        __imp__sub_82600A68(ctx, base);
    } else {
        if (f) { fprintf(f, "[AudioReg #%d] SKIPPED r3=0x%08X r4=0x%08X (prevents audio deadlock)\n",
            c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
}

// Hook sub_8260D180 - signals the IO event (0x832AD680) to wake IO threads
// Called via sub_826230C8 vtable dispatch when work is submitted
extern "C" void __imp__sub_8260D180(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260D180) {
    static int c = 0;
    c++;
    if (c <= 20 || c % 100 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOSignal #%d] ENTER r3=0x%08X (signals event@0x832AD680)\n", c, ctx.r3.u32); fclose(f); }
    }
    __imp__sub_8260D180(ctx, base);
    if (c <= 20 || c % 100 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOSignal #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82604FB8 - the actual registration function called by sub_82604F38
extern "C" void __imp__sub_82604FB8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82604FB8) {
    static int c = 0;
    c++;
    if (c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RegInit #%d] ENTER r3=0x%08X r4=0x%08X\n",
            c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_82604FB8(ctx, base);
    if (c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RegInit #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_8260C4F8 - called after BufAlloc succeeds, probably the IO read/completion
extern "C" void __imp__sub_8260C4F8(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_8260C4F8) {
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IORead #%d] r3=0x%08X r4=0x%08X ENTER\n", c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_8260C4F8(ctx, base);
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IORead #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82623408 (buffer allocator, vtable[20]) to find what vtable[16] call blocks
extern "C" void __imp__sub_82623408(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82623408) {
    uint32_t obj = ctx.r3.u32;
    uint32_t size = ctx.r4.u32;
    uint32_t vtable = obj ? PPC_LOAD_U32(obj) : 0;
    uint32_t vt16 = vtable ? PPC_LOAD_U32(vtable + 16) : 0;
    static int c = 0;
    if (++c <= 30) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BufAlloc #%d] obj=0x%08X size=%u vt[16]=0x%08X\n", c, obj, size, vt16); fclose(f); }
    }
    __imp__sub_82623408(ctx, base);
    if (c <= 30) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BufAlloc #%d] returned 0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82623160 (called when obj is null in sub_82623250) — might block
extern "C" void __imp__sub_82623160(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82623160) {
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ResNull #%d] ENTER r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_82623160(ctx, base);
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ResNull #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); }
    }
}

// Hook sub_82623250 to trace vtable calls and find the blocking one
extern "C" void __imp__sub_82623250(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82623250) {
    uint32_t hash = ctx.r3.u32;
    uint32_t data = ctx.r4.u32;
    uint32_t ctx_ptr = ctx.r5.u32;
    uint32_t obj = ctx_ptr ? PPC_LOAD_U32(ctx_ptr) : 0;
    uint32_t vtable = obj ? PPC_LOAD_U32(obj) : 0;
    uint32_t vt0 = vtable ? PPC_LOAD_U32(vtable + 0) : 0;
    uint32_t vt8 = vtable ? PPC_LOAD_U32(vtable + 8) : 0;
    uint32_t vt12 = vtable ? PPC_LOAD_U32(vtable + 12) : 0;
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[ResLookup #%d] hash=0x%08X data=0x%08X obj=0x%08X vt=0x%08X fn[0]=0x%08X fn[8]=0x%08X fn[12]=0x%08X\n",
                c, hash, data, obj, vtable, vt0, vt8, vt12);
            fclose(f);
        }
    }
    __imp__sub_82623250(ctx, base);
    // After sub_82623250 returns, [r5] may now point to a newly created object
    // (sub_82623160 creates it when obj was null). Log the vtable function.
    uint32_t obj_after = ctx_ptr ? PPC_LOAD_U32(ctx_ptr) : 0;
    uint32_t vt_after = obj_after ? PPC_LOAD_U32(obj_after) : 0;
    uint32_t vt5_after = vt_after ? PPC_LOAD_U32(vt_after + 20) : 0;
    if (c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ResLookup #%d] returned r3=0x%08X obj_now=0x%08X vt=0x%08X vt[20]=0x%08X\n",
            c, ctx.r3.u32, obj_after, vt_after, vt5_after); fclose(f); }
    }
}

// Global flag to skip thread creation during IO re-initialization
std::atomic<bool> g_skip_thread_creation{false};

// Override ExCreateThread to skip during IO re-init.
// KeInitializeSemaphore can't be overridden (SDK uses templated ppc_ptr_t<X_KSEMAPHORE>).
namespace rex { namespace kernel { namespace xboxkrnl {
    extern ppc_u32_result_t ExCreateThread_entry(ppc_pu32_t, ppc_u32_t,
        ppc_pu32_t, ppc_u32_t, ppc_pvoid_t, ppc_pvoid_t, ppc_u32_t);
}}}

// Track handles from real ExCreateThread calls for reuse
static uint32_t g_io_thread_handles[8] = {};
static int g_io_thread_handle_count = 0;

PPC_FUNC_IMPL(__imp__ExCreateThread) {
    uint32_t handle_out_addr = ctx.r3.u32;

    if (g_skip_thread_creation.load()) {
        // Reuse an existing IO thread handle. The caller does:
        // ObReferenceObjectByHandle → KeSetBasePriorityThread →
        // KeResumeThread → ObDereferenceObject
        // All of these are harmless on a valid, already-running thread.
        if (g_io_thread_handle_count > 0 && handle_out_addr) {
            // Cycle through captured handles
            static int reuse_idx = 0;
            uint32_t reuse_handle = g_io_thread_handles[reuse_idx % g_io_thread_handle_count];
            reuse_idx++;
            PPC_STORE_U32(handle_out_addr, reuse_handle);
            ctx.r3.u64 = 0; // success
            static int skip_c = 0;
            if (++skip_c <= 10) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[ExCreateThread] REUSE #%d handle=0x%08X (of %d)\n",
                    skip_c, reuse_handle, g_io_thread_handle_count); fclose(f); }
            }
        } else {
            ctx.r3.s64 = -1; // no handles available
            static int err_c = 0;
            if (++err_c <= 3) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[ExCreateThread] NO HANDLES (count=%d)\n", g_io_thread_handle_count); fclose(f); }
            }
        }
        return;
    }

    // Real thread creation — capture handle for reuse
    rex::HostToGuestFunction<rex::kernel::xboxkrnl::ExCreateThread_entry>(ctx, base);
    if (ctx.r3.u32 == 0 && handle_out_addr && g_io_thread_handle_count < 8) {
        uint32_t new_handle = PPC_LOAD_U32(handle_out_addr);
        if (new_handle) {
            g_io_thread_handles[g_io_thread_handle_count++] = new_handle;
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[ExCreateThread] CAPTURED handle=0x%08X (total=%d)\n",
                new_handle, g_io_thread_handle_count); fclose(f); }
        }
    }
}

// Hook XamLoaderTerminateTitle to catch game exits
extern "C" void __imp__XamLoaderTerminateTitle(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82789084) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[TERMINATE] XamLoaderTerminateTitle called! Blocking exit.\n"); fclose(f); }
    // Don't actually terminate - just sleep forever
    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
}

// Hook game init to trace progress
extern "C" void __imp__sub_827176E0(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_827176E0) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[GAME-INIT] entering sub_827176E0\n"); fclose(f); }
    __imp__sub_827176E0(ctx, base);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[GAME-INIT] sub_827176E0 returned r3=0x%08X\n", ctx.r3.u32); fclose(f); }
}

// Hook xstart to trace the main game flow
extern "C" void __imp__xstart(PPCContext& ctx, uint8_t* base);
PPC_FUNC_IMPL(xstart) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[XSTART] entering\n"); fclose(f); }
    __imp__xstart(ctx, base);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[XSTART] returned (game exited)\n"); fclose(f); }
}

// Render wait thread (sub_825DF970) - hooked above with render flag forcing

// ============================================================================
// Content / License Stubs
// ============================================================================

// XamContentGetLicenseMask - return full license (all content unlocked)
extern "C" uint32_t XamContentGetLicenseMask_entry(
    uint32_t* mask_ptr, void* overlapped) {
    if (mask_ptr) *mask_ptr = 0xFFFFFFFF;
    return 0;  // X_ERROR_SUCCESS
}
