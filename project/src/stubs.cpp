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

// Stub ALL Bink video section functions to return 0.
// The entire BINK section (0x82789600-0x82799724) is RAD Game Tools
// Bink video middleware. We disable it completely to avoid GPU thread
// crashes from Bink's Xbox 360-specific hardware video decode.
// The game skips video playback when Bink functions return null/0.
#define BINK_STUB(addr) PPC_FUNC(sub_##addr) { ctx.r3.u64 = 0; }
BINK_STUB(82789600)
// sub_82789658 = BinkWait: return 1 = "video done / ready for next frame"
// Returning 0 causes infinite spin in the video playback loop
PPC_FUNC(sub_82789658) { ctx.r3.u64 = 1; }
BINK_STUB(827896F0)
BINK_STUB(8278AF80)
BINK_STUB(8278AFE0)
BINK_STUB(8278B210)
BINK_STUB(8278B380)
BINK_STUB(8278B410)
BINK_STUB(8278B4A0)
BINK_STUB(8278B660)
BINK_STUB(8278B680)
BINK_STUB(8278BC20)
BINK_STUB(8278BDE0)
BINK_STUB(8278BF58)
BINK_STUB(8278C240)

// sub_821FBD10 is the video playback driver function.
// With Bink stubbed, this function enters an infinite loop traversing
// an uninitialized linked list. Skip it entirely.
PPC_FUNC(sub_821FBD10) { ctx.r3.u64 = 0; }

// Bink worker thread - keep alive but idle
PPC_FUNC(sub_8278D148) {
    uint32_t context_ptr = ctx.r3.u32;
    if (context_ptr) {
        PPC_STORE_U32(context_ptr + 0, 0);  // state = idle
    }
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    ctx.r3.u64 = 0;
}
#undef BINK_STUB

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
    if (count <= 5 || (count % 100 == 0)) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[TICK-648ED8] #%d r3=0x%08X\n", count, ctx.r3.u32); fclose(hf); }
    }
    __imp__sub_82648ED8(ctx, base);
}

PPC_FUNC(sub_82186F08) {
    static int count = 0;
    count++;
    if (count <= 10 || (count % 100 == 0)) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[LOOP-186F08] #%d r3=0x%08X r4=0x%08X\n", count, ctx.r3.u32, ctx.r4.u32); fclose(hf); }
    }
    __imp__sub_82186F08(ctx, base);
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
// Original waits on a VSync event. Replace with simple idle loop so the
// game thread isn't blocked waiting for this thread to process frames.
// ============================================================================
PPC_FUNC(sub_825DF970) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RENDER-WAIT] stub: idling instead of event wait\n"); fclose(f); }
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
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
