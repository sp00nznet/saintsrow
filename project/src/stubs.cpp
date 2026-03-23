// Saints Row - Game-specific kernel stub overrides
// These override default ReXGlue SDK implementations where the game
// needs special handling.

#include "saintsrow_config.h"
#include "saintsrow_init.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

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
// VideoDriver - trace every call unconditionally
extern "C" void __imp__sub_821FBD10(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_821FBD10) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[VideoDriver] ENTER r19=0x%08X\n", ctx.r19.u32); fclose(f); }
    __imp__sub_821FBD10(ctx, base);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[VideoDriver] EXIT\n"); fclose(f); }
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

// XamInputGetState - let real input system handle it
// (previously faked connected, but render path uses disconnected state)

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

// ============================================================================
// Render wait thread - sub_825DF970
// Let it run normally - the blocking might be needed for synchronization
// ============================================================================
extern "C" void __imp__sub_825DF970(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_825DF970) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RENDER-WAIT] running normally\n"); fclose(f); }
    __imp__sub_825DF970(ctx, base);
}

// ============================================================================
// Content / License Stubs
// ============================================================================

// XamContentGetLicenseMask - return full license (all content unlocked)
extern "C" uint32_t XamContentGetLicenseMask_entry(
    uint32_t* mask_ptr, void* overlapped) {
    if (mask_ptr) *mask_ptr = 0xFFFFFFFF;
    return 0;  // X_ERROR_SUCCESS
}
