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
    if (++c <= 5) {
        char fn[256] = {0};
        for (int i = 0; i < 255; i++) { fn[i] = (char)PPC_LOAD_U8(ctx.r3.u32 + i); if (!fn[i]) break; }
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkOpen #%d] '%s'\n", c, fn); fclose(f); }
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
// GameUpdate - trace the loading state machine (no more forcing)
PPC_FUNC(sub_822827B0) {
    static int _c = 0; _c++;
    __imp__sub_822827B0(ctx, base);
    if (_c <= 20) {
        uint32_t new_i = PPC_LOAD_U32(0x8371DD7C);
        uint32_t new_m = PPC_LOAD_U32(0x8370DD7C);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GameUpdate #%d] int=%u main=%u\n", _c, new_i, new_m); fclose(f); }
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
    uint32_t skip_flag = PPC_LOAD_U32(0x8372033C);
    uint32_t render_skip = PPC_LOAD_U32(0x83720340);
    static int _c = 0; _c++;
    if (_c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GL2_Render #%d] skip=%u render_skip=%u\n", _c, skip_flag, render_skip); fclose(f); }
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
// sub_82185498 (PostLoop5) - stub it + clear loading flag
// Running it for real crashes fatally. Stub and proceed to GameLoop2.
PPC_FUNC(sub_82185498) {
    // Clear load flag so wait loop runs → reaches GameLoop2
    // Need to also clear after return because the code between PostLoop5
    // and the flag check might re-set it
    PPC_STORE_U8(0x8370D6C9, 0);
    uint8_t flag_val = PPC_LOAD_U8(0x8370D6C9);
    static int c = 0;
    if (++c <= 3) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop5] STUBBED #%d flag=0x%02X r30=0x%08X\n", c, flag_val, ctx.r30.u32); fclose(f); } }
    ctx.r3.u64 = 0;
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

    // The game has TWO ring buffers:
    //   [13436] = 0xE98B7000 (primary, for setup/control)
    //   [13440] = 0xA95F0000 (secondary, actual rendering + VdSwap)
    // We need to init the command processor with the SECONDARY buffer
    // because that's where rendering commands and VdSwap packets go.
    if (rb_13440 != 0) {
        auto* ks = REX_KERNEL_STATE();
        auto* gs = static_cast<rex::graphics::GraphicsSystem*>(ks->emulator()->graphics_system());
        auto* cp = gs->command_processor();

        uint32_t rb_phys = ks->memory()->GetPhysicalAddress(rb_13440);
        if (rb_phys != UINT32_MAX) {
            // Ring buffer at 0xA95F0000 with size 0x2C0000 (2.75MB)
            // size_log2 = 18 (2^21 = 2MB ring buffer)
            int size_log2 = 18;
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

        // Before issuing swap, feed the current ring buffer write pointer
        // to the command processor. The game writes the write pointer to
        // [render_state+10768] (descriptor area) but this doesn't reach
        // the GPU command processor because it's not MMIO-mapped.
        // Read the write pointer and call UpdateWritePointer to process
        // all pending rendering commands in the ring buffer.
        {
            uint32_t render_state = 0x40001E00;
            uint32_t desc = PPC_LOAD_U32(render_state + 10768);
            if (desc) {
                // The game writes [desc+0] = writeback data, [desc+4] = write ptr value
                // Actually the descriptor format may differ. Let's check [render_state+40]
                // which is where the current ring buffer position is stored.
                uint32_t cur_pos = PPC_LOAD_U32(render_state + 40);
                uint32_t rb_base_virt = PPC_LOAD_U32(render_state + 13436);
                static int dbg_wp = 0;
                if (++dbg_wp <= 5) {
                    FILE* f2 = fopen("saintsrow_heartbeat.log", "a");
                    if (f2) { fprintf(f2, "[WP-Debug #%d] cur_pos=0x%08X rb_base=0x%08X desc=0x%08X desc[0]=0x%08X desc[4]=0x%08X\n",
                        dbg_wp, cur_pos, rb_base_virt, desc, PPC_LOAD_U32(desc), PPC_LOAD_U32(desc+4)); fclose(f2); }
                }
                // Use the SECONDARY ring buffer base (0xA95F0000 from [13440])
                uint32_t rb_secondary = PPC_LOAD_U32(render_state + 13440);
                if (rb_secondary && cur_pos >= rb_secondary) {
                    uint32_t write_idx = (cur_pos - rb_secondary) / 4;
                    cp->UpdateWritePointer(write_idx);

                    static int wp_c = 0;
                    if (++wp_c <= 10) {
                        FILE* f2 = fopen("saintsrow_heartbeat.log", "a");
                        if (f2) { fprintf(f2, "[VdSwap-WP #%d] pos=0x%08X base=0x%08X idx=%u\n",
                            wp_c, cur_pos, rb_base_virt, write_idx); fclose(f2); }
                    }
                }
            }
        }

        // Write fetch constants and issue swap on command processor thread
        // CallInThread signals the worker event, so the worker thread will
        // wake up even without InitializeRingBuffer being called.
        static int swap_call = 0;
        int this_call = ++swap_call;
        cp->CallInThread([cp, fb_phys, width, height, fetch, this_call]() {
            FILE* f2 = fopen("saintsrow_heartbeat.log", "a");
            if (f2 && this_call <= 10) { fprintf(f2, "[IssueSwap-Thread #%d] ENTER fb=0x%08X %ux%u\n", this_call, fb_phys, width, height); fclose(f2); }

            // Write fetch constant 0 (registers 0x4800-0x4805)
            cp->RestoreRegisters(0x4800, fetch, 6, true);

            f2 = fopen("saintsrow_heartbeat.log", "a");
            if (f2 && this_call <= 10) { fprintf(f2, "[IssueSwap-Thread #%d] fetch written, calling IssueSwap\n", this_call); fclose(f2); }

            cp->IssueSwap(fb_phys, width, height);

            f2 = fopen("saintsrow_heartbeat.log", "a");
            if (f2 && this_call <= 10) { fprintf(f2, "[IssueSwap-Thread #%d] IssueSwap returned\n", this_call); fclose(f2); }
        });

        static int kick_c = 0;
        if (++kick_c <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[VdSwap-Kick #%d] IssueSwap fb=0x%08X %ux%u\n",
                kick_c, fb_phys, width, height); fclose(f); }
        }
    }
}

// Trace GPU command buffer writer
extern "C" void __imp__sub_825CC640(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825CC640) {
    static int c = 0;
    if (++c <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GPU-Write #%d] r3=0x%08X r4=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_825CC640(ctx, base);
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
