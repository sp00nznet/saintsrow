// Saints Row - Game-specific kernel stub overrides
// These override default ReXGlue SDK implementations where the game
// needs special handling.

#include "saintsrow_pch.h"
#include "saintsrow_init.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/interfaces/graphics.h>

// SDK 0.10 moved the GPU behind the rexgpu-xenos plugin: the host reaches it
// through IGraphicsSystem's virtuals plus the GPU MMIO window at 0x7FC80000
// (see GraphicsSystem::WriteRegister -- 0x01C5 = CP_RB_WPTR).
#define REX_GPU_REG(index) (0x7FC80000u + (index) * 4u)
#include <rex/runtime.h>

#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <cstring>

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
REX_EXTERN(sub_82789600) { // BinkDoFrame
    static int c = 0;
    if (++c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkDoFrame #%d] handle=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
    __imp__sub_82789600(ctx, base);
    if (c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkDoFrame #%d] returned r3=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
}
REX_EXTERN(sub_82789658) { // BinkWait
    static int c = 0;
    __imp__sub_82789658(ctx, base);
    if (++c <= 5) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[BinkWait #%d] returned r3=%u\n", c, ctx.r3.u32); fclose(f); } }
}
REX_EXTERN(sub_82789EE8) { // BinkOpen
    static int c = 0;
    c++;
    char fn[256] = {0};
    if (ctx.r3.u32) {
        for (int i = 0; i < 255; i++) { fn[i] = (char)REX_LOAD_U8(ctx.r3.u32 + i); if (!fn[i]) break; }
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
REX_EXTERN(sub_825E5320) {
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
REX_EXTERN(sub_825E54A8) {
    // Force ALL rendering flags to get VdSwap called AND ring buffer kicked:
    uint32_t r31_val = ctx.r3.u32;
    if (r31_val) {
        REX_STORE_U32(r31_val + 19980, 0);    // skip flag = 0 (gates VdSwap call)
        uint8_t dirty = REX_LOAD_U8(r31_val + 20424);
        REX_STORE_U8(r31_val + 20424, dirty | 0x8);  // dirty flag (gates render sub calls)
        // Force frame counter at [r31+20080] to non-zero (gates buffer queue setup)
        uint32_t fc = REX_LOAD_U32(r31_val + 20080);
        if (fc == 0) {
            REX_STORE_U32(r31_val + 20080, 1);
        }
        // Set "direct write" bit at [r31+10809] |= 0x2
        // This gates the CP_RB_WPTR write in sub_825D3580 (ring buffer kick)
        // Without this, VdSwap writes packets but GPU never processes them
        uint8_t dw = REX_LOAD_U8(r31_val + 10809);
        REX_STORE_U8(r31_val + 10809, dw | 0x2);
        static int logc = 0;
        if (++logc <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[VdSwap-Fix #%d] skip=0 dirty=0x%02X fc=%u dw=0x%02X\n", logc, dirty|0x8, fc, dw|0x2); fclose(f); }
        }
    }
    // Log state JUST before calling real function
    if (r31_val) {
        uint32_t skip_val = REX_LOAD_U32(r31_val + 19980);
        uint32_t fc_val = REX_LOAD_U32(r31_val + 20080);
        uint8_t dirty_val = REX_LOAD_U8(r31_val + 20424);
        uint32_t q1 = REX_LOAD_U32(r31_val + 20072);
        uint32_t q2 = REX_LOAD_U32(r31_val + 20076);
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
        uint32_t rb_ptr = REX_LOAD_U32(r31_val + 40);
        uint32_t skip_after = REX_LOAD_U32(r31_val + 19980);
        static int pc = 0;
        if (++pc <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[Post-Call #%d] rb_ptr=0x%08X skip_after=%u\n", pc, rb_ptr, skip_after); fclose(f); }
        }
    }
}

// Render wait thread - run naturally
extern "C" void __imp__sub_825DF970(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825DF970) {
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
REX_EXTERN(sub_82653F98) {
    static int c = 0;
    c++;
    if (c <= 10 || c % 500 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[SceneTick #%d] ENTER state=%u\n", c, REX_LOAD_U32(0x8370DD7C)); fclose(f); }
    }
    __imp__sub_82653F98(ctx, base);
    if (c <= 10 || c % 500 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[SceneTick #%d] EXIT\n", c); fclose(f); }
    }
}

// Hook sub_827166C0 -- reads TLS r13+256 -> [+332] to get GPU/render context
REX_EXTERN(sub_827166C0) {
    __imp__sub_827166C0(ctx, base);
    static int tls_log_count = 0;
    if (++tls_log_count <= 10) {
        FILE* tf = fopen("saintsrow_render_debug.log", "a");
        if (tf) {
            fprintf(tf, "sub_827166C0: r13=0x%08X, TLS[256]=0x%08X, result r3=0x%08X\n",
                ctx.r13.u32,
                (ctx.r13.u32 ? REX_LOAD_U32(ctx.r13.u32 + 256) : 0),
                ctx.r3.u32);
            fclose(tf);
        }
    }
}

// Heartbeat hooks on suspected game loop functions
extern "C" void __imp__sub_82648ED8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82186F08(PPCContext& ctx, uint8_t* base);

REX_EXTERN(sub_82648ED8) {
    static int count = 0;
    count++;
    if (count <= 10) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[TICK-648ED8] #%d ENTER r3=0x%08X\n", count, ctx.r3.u32); fclose(hf); }
    }
    __imp__sub_82648ED8(ctx, base);

    if (count <= 10) {
        FILE* hf = fopen("saintsrow_heartbeat.log", "a");
        if (hf) { fprintf(hf, "[TICK-648ED8] #%d EXIT r3=0x%08X state=%u\n", count, ctx.r3.u32, REX_LOAD_U32(0x8370DD7C)); fclose(hf); }
    }
}

// Trace each sub-call in the main game loop (sub_82186F08)
#define TRACE_CALL(name, addr) \
    extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base); \
    REX_EXTERN(sub_##addr) { \
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
    REX_EXTERN(sub_##addr) { \
        uint32_t st_before = REX_LOAD_U32(0x8370DD7C); \
        __imp__sub_##addr(ctx, base); \
        uint32_t st_after = REX_LOAD_U32(0x8370DD7C); \
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
REX_EXTERN(sub_822827B0) {
    static int _c = 0; _c++;
    uint32_t state_before = REX_LOAD_U32(0x8370DD7C);
    __imp__sub_822827B0(ctx, base);
    uint32_t state_after = REX_LOAD_U32(0x8370DD7C);
    if (_c <= 30 || state_before != state_after || (_c % 100 == 0)) {
        // Trace the exact internal computation that should advance state
        uint32_t r31_addr = 0x8370DA78;
        uint32_t queue_ptr = REX_LOAD_U32(r31_addr - 196);
        uint32_t q8 = queue_ptr ? REX_LOAD_U32(queue_ptr + 8) : 0;
        uint32_t q12 = queue_ptr ? REX_LOAD_U32(queue_ptr + 12) : 0;
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
        uint32_t load_queue = REX_LOAD_U32(r31_addr - 196); // [r31-196] = loading queue ptr
        uint32_t internal_var = REX_LOAD_U32(r31_addr + 0);  // [r31+0] another state var
        uint32_t r31_776 = REX_LOAD_U32(r31_addr + 776);     // [r31+776]
        // sub_82282638 checks 4 entries at base 0x832BAAE8, stride 584.
        // (Verified in IDA against recomp: sub_82282638 computes
        //  lis -31956; addi -21784 -> 0x832BAAE8, then reads slot+24/+108/+360
        //  and ORs them; loading screen persists while any byte != 0.)
        // NOTE: the old base 0x840BAAE8 was a typo (0x84 vs 0x83) with ZERO
        // code xrefs - it read uninitialized .data, so prior [0,224,64]
        // readings were garbage. Real slot array is 0x832BAAE8.
        uint32_t base = 0x832BAAE8;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            uint32_t q8 = load_queue ? REX_LOAD_U32(load_queue + 8) : 0;
            uint32_t q12 = load_queue ? REX_LOAD_U32(load_queue + 12) : 0;
            fprintf(f, "[LoadCheck #%d] queue=0x%08X [+8]=0x%08X [+12]=0x%08X (empty=%d) var0=%u ",
                _c, load_queue, q8, q12, (q8 == q12), internal_var);
            for (int i = 0; i < 4; i++) {
                uint32_t entry = base + i * 584;
                uint8_t b24 = REX_LOAD_U8(entry + 24);
                uint8_t b108 = REX_LOAD_U8(entry + 108);
                uint8_t b360 = REX_LOAD_U8(entry + 360);
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
    // Pump the streaming callback in ALL states, not just state 1.
    // The game's world loading continues in state 3 and needs the
    // streaming system to keep processing file reads.
    {
        extern void sub_8265F720(PPCContext& ctx, uint8_t* base);
        for (int pump = 0; pump < 8; pump++) {
            PPCContext sc_ctx = ctx;
            sc_ctx.r3.u64 = 0x827A1F24;
            sc_ctx.r4.u64 = 0x827A1F28;
            sc_ctx.r5.u64 = 0;
            sc_ctx.r6.u64 = 0;
            sub_8265F720(sc_ctx, base);
            if (sc_ctx.r3.u32 == 0) break;
        }
        static int pump_log = 0;
        if (state_after >= 3 && ++pump_log <= 5) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[StreamPump] Pumping in state %u\n", state_after); fclose(f); }
        }
    }
    // Loading now completes naturally via KeInitializeSemaphore preservation.
    // No force-bypass needed. Keep a safety net for debugging:
    if (_c >= 10000 && state_after == 1) {
        REX_STORE_U32(0x8370DD7C, 3);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GameUpdate #%d] FORCED state 1 -> 3 (loading safety timeout)\n", _c); fclose(f); }
    }
}
// Defined below, next to the ring-buffer plumbing.
static std::mutex g_rb_mutex;
static uint32_t g_prim_write_pos;
static bool RbPresent(uint8_t* base, uint32_t rb_prim_virt, uint32_t fb_phys,
                      uint32_t width, uint32_t height);

TRACE_STATE("VideoMgr", 821FB9D8)
// VideoDriver - trace video queue state
extern "C" void __imp__sub_821FBD10(PPCContext& ctx, uint8_t* base);
// Read a NUL-terminated guest string into a host buffer (for progress logging).
static void gstr(uint8_t* base, uint32_t addr, char* out, int n) {
    int i = 0;
    if (addr >= 0x82000000 && addr < 0x84000000) {
        for (; i < n - 1; i++) { char ch = (char)REX_LOAD_U8(addr + i); if (!ch) break; out[i] = ch; }
    }
    out[i] = 0;
}

extern "C" void __imp__sub_825BFC10(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825BFC10) {
    uint32_t arg = ctx.r3.u32;
    // sub_825BFC10 is a generic table/resource loader: r3 is a filename string
    // when called from sub_82185498 (e.g. "multiplayer_levels.xtbl"). Log the
    // name so we can see how far the front-end init (sub_82185498) progresses.
    char name[64]; gstr(base, arg, name, sizeof(name));
    static int nc = 0;
    if (name[0] && ++nc <= 40) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[TblLoad #%d] sub_825BFC10(\"%s\") ENTER\n", nc, name); fclose(f); }
    }
    __imp__sub_825BFC10(ctx, base);
    if (name[0] && nc <= 40) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[TblLoad #%d] sub_825BFC10(\"%s\") RETURNED r3=0x%08X\n", nc, name, ctx.r3.u32); fclose(f); }
    }
}

// ---- sub_82185498 (front-end init) progress markers ----
// byte_8370DA94 is set ONLY at the very end of sub_82185498; runtime shows it
// never gets set, so the init hangs partway. These markers bracket where.
extern "C" void __imp__sub_822A8608(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_822A8608) {        // line 694: "unlockable_interface_script.xtbl"
    static bool once = false;
    char name[64]; gstr(base, ctx.r4.u32, name, sizeof(name));
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark A] sub_822A8608 r4=\"%s\" (line~694)\n", name); fclose(f); } }
    __imp__sub_822A8608(ctx, base);
}
extern "C" void __imp__sub_821162F0(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_821162F0) {        // line 781: "spawn_info_ranks.xtbl"
    static bool once = false;
    char name[64]; gstr(base, ctx.r3.u32, name, sizeof(name));
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark B] sub_821162F0 r3=\"%s\" (line~781)\n", name); fclose(f); } }
    __imp__sub_821162F0(ctx, base);
}
extern "C" void __imp__sub_821F4410(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_821F4410) {        // line 927: "cheats.xtbl"
    static bool once = false;
    char name[64]; gstr(base, ctx.r7.u32, name, sizeof(name));
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark C] sub_821F4410 r7=\"%s\" (line~927)\n", name); fclose(f); } }
    __imp__sub_821F4410(ctx, base);
}
// sub_82645660 = .peg asset loader. sub_82185498 loads "env_maps.peg" (line 465)
// then "always_loaded.peg" (line 466). A synchronous peg load that blocks on
// streaming IO is the prime suspect for the init hang. Log ENTER/RETURN per
// filename: an ENTER with no matching RETURN pinpoints the hanging load.
extern "C" void __imp__sub_82645660(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82645660) {
    char name[64]; gstr(base, ctx.r3.u32, name, sizeof(name));
    static int pc = 0;
    int myc = (name[0] ? ++pc : 0);
    if (myc && pc <= 60) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[Peg #%d] sub_82645660(\"%s\") ENTER\n", myc, name); fclose(f); }
    }
    __imp__sub_82645660(ctx, base);
    if (myc && pc <= 60) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[Peg #%d] sub_82645660(\"%s\") RETURNED r3=0x%08X\n", myc, name, ctx.r3.u32); fclose(f); }
    }
}

// ---- finer brackets for sub_82185498 lines 467..694 (peg loads OK; hang here) ----
extern "C" void __imp__sub_826F92F8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_826F92F8) {        // ".anim" loader (called many times during init)
    char name[64]; gstr(base, ctx.r3.u32, name, sizeof(name));
    static int dc = 0; int myc = ++dc;
    // The anim-preload routine ends by re-loading the watermelon anims; mark
    // "anims done" when the 2nd PML1_Watermelon_Long load returns, so the
    // post-anim markers below capture the next sub_82185498 call.
    extern std::atomic<int> g_after_anims;
    if (myc > 1000 && (name[0]=='P'||name[0]=='p') && strstr(name, "atermelon_Long")) {
        g_after_anims.store(1);
    }
    bool tail = (myc > 3680 && myc < 4100);   // verbose window around the plateau
    if (myc <= 3 || myc % 100 == 0 || tail) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[AnimLoad #%d] \"%s\" ENTER\n", myc, name); fclose(f); }
    }
    __imp__sub_826F92F8(ctx, base);
    if (tail) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[AnimLoad #%d] \"%s\" RETURNED r3=0x%08X\n", myc, name, ctx.r3.u32); fclose(f); }
    }
}
std::atomic<int> g_after_anims{0};
// Post-anim-preload markers: log the FIRST ENTER/RETURN of each once the anim
// phase is done (g_after_anims==1). The first marker showing ENTER with no
// RETURN is the hanging call. Covers sub_82185498 lines 522..527.
#define POSTANIM_MARK(fn, lineno)                                                      \
    extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base);                       \
    REX_EXTERN(fn) {                                                                     \
        static bool logged = false;                                                    \
        bool go = g_after_anims.load() && !logged;                                     \
        if (go) { logged = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");      \
            if (f) { fprintf(f, "[PostAnim] " #fn " ENTER (line~" #lineno ") r3=0x%08X\n", ctx.r3.u32); fclose(f); } } \
        __imp__##fn(ctx, base);                                                        \
        if (go) { FILE* f = fopen("saintsrow_heartbeat.log", "a");                     \
            if (f) { fprintf(f, "[PostAnim] " #fn " RETURNED (line~" #lineno ") r3=0x%08X\n", ctx.r3.u32); fclose(f); } } \
    }
POSTANIM_MARK(sub_82406E40, 522)
POSTANIM_MARK(sub_8222A750, 523)
POSTANIM_MARK(sub_822388A8, 524)
POSTANIM_MARK(sub_82519DC8, 525)
POSTANIM_MARK(sub_8224C510, 526)
POSTANIM_MARK(sub_826335C0, 527)
// sub_8224C510 (physics/Havok init) is the hang; bracket its early sub-calls
// (suffix = its own decompile line).
POSTANIM_MARK(sub_826C3430, phys_p38)
POSTANIM_MARK(sub_826DB590, phys_p59)
POSTANIM_MARK(sub_826C8868, phys_p70)
POSTANIM_MARK(sub_826992F8, phys_p73)
// sub_826C98D8 (Havok base/world init) is the hang. Bracket ITS sub-calls
// (suffix = sub_826C98D8's own decompile line). Last ENTER-without-RETURN wins.
POSTANIM_MARK(sub_826E3B30, hk_171)
POSTANIM_MARK(sub_826B34B0, hk_391)
POSTANIM_MARK(sub_82732A88, hk_453)
POSTANIM_MARK(sub_826E8E10, hk_505)
POSTANIM_MARK(sub_826DBD10, hk_521)
POSTANIM_MARK(sub_826C6B08, hk_527)
POSTANIM_MARK(sub_826C8028, hk_529)
POSTANIM_MARK(sub_826DB810, hk_541)
POSTANIM_MARK(sub_826E8F88, hk_544)
// Narrowed: hang is in lines 171..369 of sub_826C98D8 (sub_826E3B30 returns,
// sub_826E9A48 not reached). The only calls there are the Havok version-mismatch
// banner/error block (188..201, runs only if r6 != 30300/0x765C).
POSTANIM_MARK(sub_827590A0, hk_359)
POSTANIM_MARK(sub_826E9A48, hk_369)
POSTANIM_MARK(sub_826E7D98, hk_387)
POSTANIM_MARK(sub_826C1D78, hk_188_bannerStart)
POSTANIM_MARK(sub_826C1DF8, hk_201_bannerEnd)
// line 354 off_827A7230() -> sub_8267C3C8 -> sub_8267AAA0. Confirmed: returns fine.
POSTANIM_MARK(sub_8267C3C8, hk_354_offcall)
POSTANIM_MARK(sub_8267AAA0, hk_354_inner)
// Havok alloc path: sub_82277300 (vtable[0]) -> sub_82277618 (lookup) ->
// sub_826C19A8 (off_827A775C, allocate). Bracket all three; fopen is gated behind
// g_after_anims + a shared cap so the boot-time hot path stays cheap. The last
// unmatched ENTER (no RET) is the infinite loop.
static std::atomic<int> g_hkalloc_log{0};
#define ALLOCMARK(fn) \
    extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base); \
    REX_EXTERN(fn) { \
        bool go = g_after_anims.load(); int myc = 0; \
        if (go && (myc = g_hkalloc_log.fetch_add(1)) < 6000) { \
            FILE* f = fopen("saintsrow_heartbeat.log", "a"); \
            if (f) { fprintf(f, "[A] " #fn " ENTER #%d sz=%u\n", myc, ctx.r4.u32); fclose(f); } \
        } else myc = 1 << 30; \
        __imp__##fn(ctx, base); \
        if (go && myc < 6000) { \
            FILE* f = fopen("saintsrow_heartbeat.log", "a"); \
            if (f) { fprintf(f, "[A] " #fn " RET #%d\n", myc); fclose(f); } \
        } \
    }
ALLOCMARK(sub_82277300)
ALLOCMARK(sub_82277618)
ALLOCMARK(sub_826C19A8)

// sub_82702D20 = memcpy(dst, src, n). The watchdog caught the main thread spinning
// here with a huge CTR -> a memcpy with a garbage/huge size. Log only absurd sizes
// (the bug) plus the caller (LR) so we find the bad call site. Cheap: 1 compare/call.
extern "C" void __imp__sub_82702D20(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82702D20) {
    uint32_t n = ctx.r5.u32;
    if (n > 0x01000000) {  // >16MB memcpy is a size underflow (e.g. erase-from-empty
        // array: count-4 wraps to 0xFFFFFFFC). Largest real init copy is ~256KB.
        // Treat as a no-op: copying ~4GB from a null ptr would hang/crash, and the
        // degenerate case (empty array element removal) is correctly a no-op.
        static int c = 0;
        if (++c <= 30) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[BAD-MEMCPY #%d SKIPPED] dst=0x%08X src=0x%08X n=0x%08X caller_lr=0x%08X\n",
                c, ctx.r3.u32, ctx.r4.u32, n, (uint32_t)ctx.lr); fclose(f); }
        }
        ctx.r3.u64 = ctx.r3.u32;  // memcpy returns dst
        return;
    }
    __imp__sub_82702D20(ctx, base);
}

// NOTE: sub_82277300 = Havok allocator (vtable[0]); it does
// RtlEnterCriticalSection(this+112) -> alloc -> RtlLeaveCriticalSection.
// Hooking it directly is too hot (called from boot onward) and stalls the game,
// so confirmation of the CS deadlock must be done via the RtlEnterCriticalSection
// hook logging only slow/contended enters, not a per-alloc hook.
// Custom hook for sub_826C98D8: log entry args — r6 is the Havok build version;
// if r6 != 30300 (0x765C) the mismatch error path (line 194, hkWorld.cpp:2248) runs.
extern "C" void __imp__sub_826C98D8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_826C98D8) {
    static bool once = false;
    if (g_after_anims.load() && !once) {
        once = true;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        // Resolve the Havok allocator (dword_8283BE44) and its vtable methods so
        // we can decompile the one that hangs (vtable+16 used at lines 358/363/368).
        uint32_t alloc_obj = REX_LOAD_U32(0x8283BE44);
        uint32_t vt = alloc_obj ? REX_LOAD_U32(alloc_obj) : 0;
        uint32_t m16 = vt ? REX_LOAD_U32(vt + 16) : 0;
        uint32_t m24 = vt ? REX_LOAD_U32(vt + 24) : 0;
        if (f) { fprintf(f, "[HavokInit] sub_826C98D8 ENTER r3=0x%08X r5=0x%08X(ver) r6=0x%08X | alloc=0x%08X vt=0x%08X vt+16=0x%08X vt+24=0x%08X\n",
            ctx.r3.u32, ctx.r5.u32, ctx.r6.u32, alloc_obj, vt, m16, m24); fclose(f); }
    }
    __imp__sub_826C98D8(ctx, base);
    if (g_after_anims.load()) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[HavokInit] sub_826C98D8 RETURNED r3=0x%08X\n", ctx.r3.u32); fclose(f); }
    }
}
extern "C" void __imp__sub_8221CAC8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8221CAC8) {        // line 532: "preload.vpp" (only if byte_8370D671)
    static bool once = false;
    char name[64]; gstr(base, ctx.r3.u32, name, sizeof(name));
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark E1] sub_8221CAC8 vpp=\"%s\" D671=%u (line~532)\n", name, REX_LOAD_U8(0x8370D671)); fclose(f); } }
    __imp__sub_8221CAC8(ctx, base);
    { FILE* f = fopen("saintsrow_heartbeat.log", "a");
      if (f) { fprintf(f, "[InitMark E1-ret] sub_8221CAC8 vpp=\"%s\" RETURNED\n", name); fclose(f); } }
}
extern "C" void __imp__sub_8221C6E0(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8221C6E0) {        // line 536: else-branch package load
    static bool once = false;
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark E2] sub_8221C6E0 r3=0x%08X D671=%u (line~536)\n", ctx.r3.u32, REX_LOAD_U8(0x8370D671)); fclose(f); } }
    __imp__sub_8221C6E0(ctx, base);
    { static bool r=false; if(!r){r=true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
      if (f) { fprintf(f, "[InitMark E2-ret] sub_8221C6E0 RETURNED r3=0x%08X\n", ctx.r3.u32); fclose(f); } } }
}
extern "C" void __imp__sub_825CD1F8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825CD1F8) {        // line 644: 960x960 render target create
    static bool once = false;
    if (!once) { once = true; FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[InitMark F] sub_825CD1F8(%u,%u,..) (line~644)\n", ctx.r3.u32, ctx.r4.u32); fclose(f); } }
    __imp__sub_825CD1F8(ctx, base);
}
// Hook sub_826FE350 (comparison function) to see what's being compared
extern "C" void __imp__sub_826FE350(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_826FE350) {
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
REX_EXTERN(sub_82648648) {
    uint32_t in_r3 = ctx.r3.u32;
    uint32_t in_r4 = ctx.r4.u32;
    __imp__sub_82648648(ctx, base);
    static int c = 0; c++;
    if (ctx.r3.u32 == 0) {
        // Returned null - force first item from queue
        uint32_t first_item = in_r3 ? REX_LOAD_U32(in_r3 + 8) : 0;
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
REX_EXTERN(sub_821FBD10) {
    // Check the Bink data address used for queue lookup
    uint32_t bink_data = 0x8278CD08;
    uint32_t val0 = REX_LOAD_U32(bink_data);
    uint32_t val4 = REX_LOAD_U32(bink_data + 4);
    uint32_t val8 = REX_LOAD_U32(bink_data + 8);
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
REX_EXTERN(sub_8262FFE0) {
    // [0x8372033C] = skip_flag at [r28+32] where r28=0x8372031C
    // If non-zero, GL2_Render returns immediately (no rendering)
    uint32_t skip_flag = REX_LOAD_U32(0x8372033C);
    uint32_t render_skip = REX_LOAD_U32(0x83720340);
    uint32_t frame_count = REX_LOAD_U32(0x83720320 + 800); // frame counter at [r28+800]
    static int _c = 0; _c++;
    static int skip_count = 0;
    static int render_count = 0;
    if (skip_flag != 0) skip_count++;
    else render_count++;
    if (_c <= 10 || (_c >= 50 && _c <= 100) || (_c % 100 == 0)) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[GL2_Render #%d] skip=%u render_skip=%u frames=%u (rendered=%d skipped=%d)\n",
                _c, skip_flag, render_skip, frame_count, render_count, skip_count);
            fclose(f);
        }
    }
    // Only clear flags that gate indirect calls if function pointer is invalid.
    // When the pointer becomes valid, let the game call through to enable
    // world loading/physics system updates.
    {
        uint32_t fn = REX_LOAD_U32(0x8370F248);
        if (fn < 0x82000000 || fn >= 0x83000000) {
            REX_STORE_U8(0x8370E9DF, 0);
            REX_STORE_U8(0x8370E9F6, 0);
        }
    }

    // Check the render gate at [0x83710340] - if non-zero, sub_82633898 is skipped
    uint32_t render_gate = REX_LOAD_U32(0x83710340);
    static int rg_log = 0;
    if (++rg_log <= 5 || rg_log % 500 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[GL2_Render #%d] render_gate[0x83710340]=%u\n", _c, render_gate); fclose(f); }
    }

    __imp__sub_8262FFE0(ctx, base);

    // Force render-complete flags every frame so VdSwap fires
    {
        uint32_t r31_val = 0x40001E00;
        REX_STORE_U32(r31_val + 19980, 0);
        REX_STORE_U8(r31_val + 20424, REX_LOAD_U8(r31_val + 20424) | 0x8);
        REX_STORE_U8(r31_val + 10809, REX_LOAD_U8(r31_val + 10809) | 0x2);
        if (REX_LOAD_U32(r31_val + 20080) == 0)
            REX_STORE_U32(r31_val + 20080, 1);
    }

    // Present every frame with atomic guard.
    // Restore fetch constants before IssueSwap for correct format interpretation.
    if (REX_LOAD_U32(0x8370DD7C) >= 3) {
        static std::atomic<bool> swap_pending{false};
        if (!swap_pending.exchange(true)) {
            // Clear tiled bit (bit 31 of dword_0) - the D3D12 framebuffer
            // is linear, not Xenos-tiled. With tiled=1, the texture cache
            // applies detiling to linear data, causing crosshatch artifacts.
            // Linear (untiled) with ZYX1 swizzle for correct channel order
            static const uint32_t ft[6] = {0x0A000002, 0x09258006, 0x0059E4FF,
                                           0x00001414, 0x00000000, 0x00000200};
            (void)ft;
            // Submit a real PM4 swap through the ring buffer (see RbPresent).
            RbPresent(base, REX_LOAD_U32(0x40001E00 + 13436), 0x09258000, 1280, 720);
            // ponytail: presentation now rides the ring buffer. SDK 0.10 moved
            // the GPU into the rexgpu-xenos plugin, which exports only
            // rex_gpu_create; CommandProcessor::IssueSwap / CallInThread /
            // InvalidateGpuMemory are no longer reachable from the host, so the
            // old "force a swap every GL2_Render" hack is gone. The kick hook
            // (sub_825D3580) advances CP_RB_WPTR through GPU MMIO, which is the
            // hardware path VdSwap's swap packet travels. If frames stall again,
            // add a swap packet to the ring rather than reaching into the CP.
            swap_pending.store(false);
        }

        // Sample framebuffer pixels for color diagnosis
        static int px_log = 0;
        if (++px_log <= 5 || px_log == 50 || px_log == 200) {
            auto* ks3 = REX_KERNEL_STATE();
            uint32_t fb_phys = 0x09258000;
            uint8_t* fb_host = ks3->memory()->TranslatePhysical(fb_phys);
            if (fb_host) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) {
                    // Sample pixels at center (360*1280*4 + 640*4) and corners
                    uint32_t center = 360 * 1280 * 4 + 640 * 4;
                    uint32_t tl = 100 * 1280 * 4 + 100 * 4;
                    uint32_t tr = 100 * 1280 * 4 + 1100 * 4;
                    fprintf(f, "[Pixels #%d] center=[%02X %02X %02X %02X] tl=[%02X %02X %02X %02X] tr=[%02X %02X %02X %02X]\n",
                        px_log,
                        fb_host[center], fb_host[center+1], fb_host[center+2], fb_host[center+3],
                        fb_host[tl], fb_host[tl+1], fb_host[tl+2], fb_host[tl+3],
                        fb_host[tr], fb_host[tr+1], fb_host[tr+2], fb_host[tr+3]);
                    // Also dump first 16 bytes of framebuffer (first 4 pixels)
                    fprintf(f, "[FB Raw] %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
                        fb_host[0], fb_host[1], fb_host[2], fb_host[3],
                        fb_host[4], fb_host[5], fb_host[6], fb_host[7],
                        fb_host[8], fb_host[9], fb_host[10], fb_host[11],
                        fb_host[12], fb_host[13], fb_host[14], fb_host[15]);
                    fclose(f);
                }
            }
        }
    }
}
TRACE_STATE("RenderD", 82636688)
#undef TRACE_STATE
TRACE_CALL("BinkClean", 821FB070)
TRACE_CALL("LoadStart", 821FB318)
// ContentLoad with return value logging
extern "C" void __imp__sub_82107638(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82107638) {
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
REX_EXTERN(sub_8234C1C0) {
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
REX_EXTERN(sub_82604B00) {
    static int c = 0;
    c++;
    uint32_t arg = ctx.r3.u32;
    uint32_t cb_ptr = REX_LOAD_U32(0x837102B4);
    // Log the function table that RunCallbacks scans
    // lis(-32134) = 0x827A0000, +0x1F20 = 0x827A1F20
    // Scan from 0x827A1F24 to 0x827A1F28 for non-null function pointers
    uint32_t ft0 = REX_LOAD_U32(0x827A1F20);
    uint32_t ft1 = REX_LOAD_U32(0x827A1F24);
    uint32_t ft2 = REX_LOAD_U32(0x827A1F28);
    uint32_t cb_vt = cb_ptr ? REX_LOAD_U32(cb_ptr) : 0;
    uint32_t cb_fn = cb_vt ? REX_LOAD_U32(cb_vt + 4) : 0;
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
REX_EXTERN(sub_82604C10) {
    static int c = 0;
    if (++c <= 3) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop1] STUBBED #%d\n", c); fclose(f); }
    }
    ctx.r3.u64 = 0;
}
TRACE_CALL("CreateThr", 82716028)
TRACE_CALL("WaitThr", 82716038)
// sub_82185498 (PostLoop5) - main game loop containing GL2_Render.
// Runs for real now. Previously crashed from bad vtable dispatches.
extern "C" void __imp__sub_82185498(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82185498) {
    static int c = 0;
    c++;
    if (c <= 3) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop5] ENTER #%d r3=0x%08X r30=0x%08X\n", c, ctx.r3.u32, ctx.r30.u32); fclose(f); } }


    // The flags at 0x8370E9DF and 0x8370E9F6 gate a bctrl at PPC 0x82186EE0
    // that calls through [0x8370F248]. Only clear the flags if the function
    // pointer is null/invalid - let the call through if it points to valid code.
    {
        uint32_t fn_ptr = REX_LOAD_U32(0x8370F248);
        bool valid = (fn_ptr >= 0x82000000 && fn_ptr < 0x83000000);
        static int flog = 0;
        if (++flog <= 10 || flog % 500 == 0) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[FnGate #%d] [0x8370F248]=0x%08X flags=[%u,%u] valid=%d\n",
                flog, fn_ptr, REX_LOAD_U8(0x8370E9DF), REX_LOAD_U8(0x8370E9F6), valid); fclose(f); }
        }
        if (!valid) {
            REX_STORE_U8(0x8370E9DF, 0);
        }
    }

    __imp__sub_82185498(ctx, base);
    if (c <= 3) { FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[PostLoop5] EXIT #%d r3=0x%08X\n", c, ctx.r3.u32); fclose(f); } }
}
TRACE_CALL("FrameRender", 82648D80)
#undef TRACE_CALL

// Instead of hooking sub_82186F08, override it completely to control the loop
// This lets us instrument the loop condition check
REX_EXTERN(sub_82186F08) {
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

// CS deadlock detector: track which host thread currently holds each guest CS.
// On a contended Enter we log CS-WAIT *before* blocking and CS-GOT *after*; the
// deadlocking Enter shows as a CS-WAIT with no matching CS-GOT. same=1 means the
// SAME thread already holds it (recursion into a non-recursive CS); same=0 means a
// different thread holds it (cross-thread deadlock / crashed holder). Logging only
// fires on contention (holder != 0), so the hot uncontended path stays cheap.
static std::mutex g_cs_mtx;
static std::unordered_map<uint32_t, uint32_t> g_cs_holder;  // cs_addr -> host tid
static inline uint32_t host_tid() {
    return (uint32_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
}
REX_EXTERN(sub_82788704) {  // RtlEnterCriticalSection
    uint32_t cs_addr = ctx.r3.u32;
    uint32_t me = host_tid();
    uint32_t holder = 0;
    { std::lock_guard<std::mutex> g(g_cs_mtx); auto it = g_cs_holder.find(cs_addr); if (it != g_cs_holder.end()) holder = it->second; }
    bool contended = (holder != 0);
    if (contended) {
        uint32_t owner_guest = REX_LOAD_U32(cs_addr + 12);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[CS-WAIT] cs=0x%08X me=0x%08X holder=0x%08X same=%d ownerGuest=0x%08X\n",
            cs_addr, me, holder, holder == me, owner_guest); fclose(f); }
    }
    __imp__RtlEnterCriticalSection(ctx, base);
    { std::lock_guard<std::mutex> g(g_cs_mtx); g_cs_holder[cs_addr] = me; }
    if (contended) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[CS-GOT ] cs=0x%08X me=0x%08X\n", cs_addr, me); fclose(f); }
    }
}

REX_EXTERN(sub_82788714) {  // RtlLeaveCriticalSection
    uint32_t cs_addr = ctx.r3.u32;
    { std::lock_guard<std::mutex> g(g_cs_mtx); g_cs_holder.erase(cs_addr); }
    __imp__RtlLeaveCriticalSection(ctx, base);
}

// Force the "loading complete" flag after a few ticks
// The game waits for [0x8370D6C9] to become non-zero, but the loading
// pipeline doesn't complete due to stubbed subsystems. Force it after
// a short delay to let the game proceed.
// sub_82716020 - force loading complete flag after a few calls
extern "C" void __imp__sub_82716020(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82716020) {
    static int call_count = 0;
    call_count++;
    __imp__sub_82716020(ctx, base);

    // DIAGNOSTIC (force-flag disabled): observe the natural loading-completion
    // chain instead of short-circuiting it.
    //   sub_82185498 sets byte_8370DA94 when front-end content is ready
    //   -> sub_821FF480 runs a 500ms fade flt_8370D1A4 0->1
    //   -> byte_8370DA95=1 -> loader thread sub_82186AF0 sets byte_8370D6C9=1
    //   -> main spin loop exits into real game loop sub_82186C10.
    if ((call_count <= 20) || (call_count % 240 == 0)) {
        uint32_t fbits = REX_LOAD_U32(0x8370D1A4);
        float fade; memcpy(&fade, &fbits, 4);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[LoadChain #%d] r3=%d DA94(ready)=%u fade[D1A4]=%.4f DA95(done)=%u D6C9(complete)=%u DA95b=%u\n",
                call_count, ctx.r3.s32,
                REX_LOAD_U8(0x8370DA94), fade,
                REX_LOAD_U8(0x8370DA95), REX_LOAD_U8(0x8370D6C9),
                REX_LOAD_U8(0x8370DA95));
            fclose(f);
        }
    }
    // Force the "loading complete" flag so the main loop leaves the spin and
    // enters the real game loop sub_82186C10 (restores the cathedral render).
    // This is a known short-circuit; the real fix is making sub_82185498
    // (front-end init) complete so byte_8370DA94 -> fade -> byte_8370D6C9
    // happens naturally.
    if (call_count == 5) {
        REX_STORE_U8(0x8370D6C9, 1);
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[FORCE-FLAG] Set at 0x8370D6C9 after %d calls\n", call_count); fclose(f); }
    }
}

// Hook the ring buffer kick function.
// After the game writes its structure to the secondary buffer,
// extract the indirect buffer addresses and feed them to the CP.
extern "C" void __imp__sub_825D3580(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825D3580) {
    uint32_t r31 = ctx.r3.u32;
    uint32_t r4 = ctx.r4.u32;
    static int kick_total = 0;
    kick_total++;
    uint32_t game_state = REX_LOAD_U32(0x8370DD7C);
    if (kick_total <= 10 || (game_state >= 3 && kick_total <= 20) || kick_total % 500 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[Kick-Total #%d] state=%u r3=0x%08X r4=0x%08X\n",
            kick_total, game_state, r31, r4); fclose(f); }
    }

    __imp__sub_825D3580(ctx, base);

    // The kick function wrote 10 dwords of PM4 at r4 in the secondary buffer.
    // But the game also writes rendering PM4 BEFORE this position.
    // Create an INDIRECT_BUFFER_PFD covering ALL data from the last position
    // to the current position (including the 10 kick dwords).
    auto* ks = REX_KERNEL_STATE();

    uint32_t rb_prim = REX_LOAD_U32(r31 + 13436);
    uint32_t rb_sec = REX_LOAD_U32(r31 + 13440);

    // Track where we last created an indirect buffer reference
    static uint32_t last_ib_end = 0;
    // Shared with RbPresent: both append to the same primary ring buffer.
    uint32_t& prim_write_pos = g_prim_write_pos;
    std::lock_guard<std::mutex> rb_lock(g_rb_mutex);

    // The data between last_ib_end and r4+40 contains rendering PM4 + kick PM4
    uint32_t ib_start = last_ib_end ? last_ib_end : rb_sec;
    uint32_t ib_end = r4 + 40; // after kick's 10 dwords

    // Write to PHYSICAL memory, not virtual. The command processor reads from
    // TranslatePhysical(primary_buffer_ptr_) which maps to a DIFFERENT host
    // memory region than the virtual address. Writing to virtual rb_prim would
    // be invisible to the command processor.
    uint32_t rb_prim_phys = rb_prim ? ks->memory()->GetPhysicalAddress(rb_prim) : 0;
    uint8_t* rb_prim_host = rb_prim_phys ? ks->memory()->TranslatePhysical(rb_prim_phys) : nullptr;

    // Ring buffer is circular - 8192 dwords. Wrap write position.
    // Reserve positions 0-30 for ME_INIT, use 31-8191 for IBs.
    // On wrap, zero out the area ahead to prevent the CP from
    // interpreting stale data as PM4 packets (causes overflow errors).
    if (prim_write_pos >= 8180) {
        prim_write_pos = 31;
        if (rb_prim_host) {
            // Zero entire usable ring buffer (positions 31-8191) to prevent
            // the CP from hitting stale loading-phase data with huge packet
            // counts that cause overflow errors.
            memset(rb_prim_host + 31 * 4, 0, (8192 - 31) * 4);
        }
    }
    if (rb_prim_host) {
        uint32_t phys_kick = ks->memory()->GetPhysicalAddress(r4);
        if (phys_kick != UINT32_MAX) {
            // Write INDIRECT_BUFFER_PFD to PRIMARY ring buffer (physical memory)
            auto write_rb = [&](uint32_t offset, uint32_t value) {
                rex::memory::store_and_swap<uint32_t>(rb_prim_host + offset * 4, value);
            };

            // Debug: verify physical address matches what InitializeRingBuffer used
            static int rb_dbg = 0;
            if (++rb_dbg <= 3) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) {
                    // Read back the initial ring buffer content at physical memory
                    uint32_t rb0 = rex::memory::load_and_swap<uint32_t>(rb_prim_host);
                    uint32_t rb1 = rex::memory::load_and_swap<uint32_t>(rb_prim_host + 4);
                    fprintf(f, "[RB-Debug #%d] prim_virt=0x%08X prim_phys=0x%08X host=%p rb[0..1]=0x%08X 0x%08X wp=%u\n",
                        rb_dbg, rb_prim, rb_prim_phys, (void*)rb_prim_host, rb0, rb1, prim_write_pos);
                    fclose(f);
                }
            }

            write_rb(prim_write_pos, 0xC0013F00);
            write_rb(prim_write_pos + 1, phys_kick);
            write_rb(prim_write_pos + 2, 10);
            prim_write_pos += 3;

            // Create render IB for PM4 between last kick and this kick.
            if (last_ib_end > 0 && last_ib_end < r4) {
                uint32_t scan_start = last_ib_end;
                uint32_t scan_pos = 0;
                uint32_t scan_len = (r4 - scan_start) / 4;
                // The data starts with zero padding, then has valid PM4.
                // Skip leading zeros to find the PM4 start.
                uint32_t pm4_start = 0;
                while (pm4_start < scan_len && REX_LOAD_U32(scan_start + pm4_start * 4) == 0) {
                    pm4_start++;
                }
                // Scan for contiguous valid PM4 packets from pm4_start
                scan_pos = pm4_start;
                while (scan_pos < scan_len) {
                    uint32_t hdr = REX_LOAD_U32(scan_start + scan_pos * 4);
                    if (hdr == 0) {
                        // Stop at zero - the CP can't skip zeros and would
                        // misinterpret them as Type0 packets, causing
                        // misalignment and packet overflow errors.
                        break;
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
                        // Copy PM4 data from virtual to physical memory.
                        // The game writes PM4 to virtual memory but the CP reads
                        // from physical, which is a DIFFERENT host memory region.
                        uint8_t* render_virt = ks->memory()->TranslateVirtual(pm4_addr);
                        uint8_t* render_phys = ks->memory()->TranslatePhysical(phys_render);
                        if (render_virt && render_phys) {
                            memcpy(render_phys, render_virt, pm4_dwords * 4);
                        }

                        write_rb(prim_write_pos, 0xC0013F00);
                        write_rb(prim_write_pos + 1, phys_render);
                        write_rb(prim_write_pos + 2, pm4_dwords);
                        prim_write_pos += 3;

                        static int render_ib_c = 0;
                        render_ib_c++;
                        if (render_ib_c <= 5 || render_ib_c == 20 || render_ib_c == 100) {
                            FILE* rf = fopen("saintsrow_heartbeat.log", "a");
                            if (rf) {
                                fprintf(rf, "[Render-IB #%d] phys=0x%08X dwords=%u (skipped %u zeros)\n",
                                    render_ib_c, phys_render, pm4_dwords, pm4_start);
                                // Scan ALL packets looking for key registers
                                uint32_t dp = 0;
                                int pkt_logged = 0;
                                while (dp < pm4_dwords) {
                                    uint32_t h = REX_LOAD_U32(pm4_addr + dp * 4);
                                    if (h == 0) { dp++; continue; }
                                    uint32_t pt = (h >> 30) & 3;
                                    if (pt == 0) {
                                        uint32_t reg = h & 0x7FFF;
                                        uint32_t cnt = ((h >> 16) & 0x3FFF) + 1;
                                        // Log key registers: RB_COLOR_INFO=0x2001,
                                        // RB_SURFACE_INFO=0x2000, RB_COLOR_MASK=0x2023
                                        // Fetch constants start at 0x4800
                                        bool interesting = (reg >= 0x2000 && reg <= 0x2010)
                                            || (reg >= 0x4800 && reg <= 0x4806)
                                            || pkt_logged < 5;
                                        if (interesting) {
                                            fprintf(rf, "  [%u] Type0 reg=0x%04X cnt=%u", dp, reg, cnt);
                                            for (uint32_t j = 0; j < cnt && j < 8; j++)
                                                fprintf(rf, " %08X", REX_LOAD_U32(pm4_addr + (dp+1+j)*4));
                                            fprintf(rf, "\n");
                                            pkt_logged++;
                                        }
                                        dp += 1 + cnt;
                                    } else if (pt == 3) {
                                        uint32_t op = (h >> 8) & 0xFF;
                                        uint32_t cnt = ((h >> 16) & 0x3FFF) + 1;
                                        if (pkt_logged < 5 || op == 0x46 /*EVENT_WRITE*/)
                                            fprintf(rf, "  [%u] Type3 op=0x%02X cnt=%u\n", dp, op, cnt);
                                        pkt_logged++;
                                        dp += 1 + cnt;
                                    } else {
                                        dp++;
                                    }
                                }
                                fclose(rf);
                            }
                        }
                    }
                }
            }

            REX_MM_STORE_U32(REX_GPU_REG(0x01C5), prim_write_pos);  // CP_RB_WPTR


            static int ib_c = 0;
            if (++ib_c <= 5) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) {
                    fprintf(f, "[IB #%d] kick=0x%08X prim_wp=%u last_end=0x%08X r4=0x%08X\n",
                        ib_c, phys_kick, prim_write_pos, last_ib_end, r4);
                    // Dump first dwords of the rendering range
                    if (last_ib_end > 0 && last_ib_end < r4) {
                        fprintf(f, "  render[0..7]: ");
                        for (int i = 0; i < 8; i++) fprintf(f, "%08X ", REX_LOAD_U32(last_ib_end + i*4));
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
        for (int i = 0; i < 10; i++) d[i] = REX_LOAD_U32(r4 + i*4);
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
REX_EXTERN(sub_825D3DA8) {
    uint32_t r31 = ctx.r3.u32; // first arg = render state
    uint32_t r26_arg = ctx.r4.u32; // second arg = ring buffer config (r26 inside function)
    uint32_t rb_10780 = REX_LOAD_U32(r31 + 10780);
    uint32_t rb_13504 = REX_LOAD_U32(r31 + 13504);
    // Check what r26 (r4) points to - this controls ring buffer allocation
    uint32_t r26_v4 = r26_arg ? REX_LOAD_U32(r26_arg + 4) : 0;
    uint32_t r26_v8 = r26_arg ? REX_LOAD_U32(r26_arg + 8) : 0;
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RB-Init] ENTER r3=0x%08X r4=0x%08X [10780]=%u [13504]=0x%08X r4[4]=0x%08X r4[8]=0x%08X\n",
        r31, r26_arg, rb_10780, rb_13504, r26_v4, r26_v8); fclose(f); }
    __imp__sub_825D3DA8(ctx, base);
    uint32_t ret = ctx.r3.u32;
    uint32_t rb_10772 = REX_LOAD_U32(r31 + 10772);
    uint32_t rb_10768 = REX_LOAD_U32(r31 + 10768);
    uint32_t rb_13436 = REX_LOAD_U32(r31 + 13436);
    uint32_t rb_13440 = REX_LOAD_U32(r31 + 13440);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[RB-Init] EXIT ret=%u [10780]=%u [10772]=0x%08X [10768]=0x%08X [13436]=0x%08X [13440]=0x%08X\n",
        ret, REX_LOAD_U32(r31 + 10780), rb_10772, rb_10768, rb_13436, rb_13440); fclose(f); }

    // Use the PRIMARY ring buffer. It starts with 31 dwords of valid PM4
    // (ME_INIT + 4 INDIRECT_BUFFER_PFD). We'll append more INDIRECT_BUFFER
    // references as the kick function produces them.
    if (rb_13436 != 0) {
        auto* ks = REX_KERNEL_STATE();

        uint32_t rb_phys = ks->memory()->GetPhysicalAddress(rb_13436);
        if (rb_phys != UINT32_MAX) {
            // Primary ring buffer at 0xE98B7000, size 0x8000 (32KB)
            int size_log2 = 12;

            // Copy ME_INIT header from virtual to physical, then zero
            // the rest. Physical memory starts uninitialized and any
            // stale data will be misinterpreted as PM4 packets by the CP.
            uint8_t* rb_virt_host = ks->memory()->TranslateVirtual(rb_13436);
            uint8_t* rb_phys_host = ks->memory()->TranslatePhysical(rb_phys);
            if (rb_virt_host && rb_phys_host) {
                // Copy ME_INIT (first 31 dwords = 124 bytes)
                memcpy(rb_phys_host, rb_virt_host, 31 * 4);
                // Zero the rest to prevent stale data overflow errors
                memset(rb_phys_host + 31 * 4, 0, 32768 - 31 * 4);
            }

            ks->emulator()->graphics_system()->InitializeRingBuffer(rb_phys, size_log2);

            // Enable read pointer writeback
            if (rb_10768) {
                uint32_t rptr_wb = ks->memory()->GetPhysicalAddress(rb_10768);
                if (rptr_wb != UINT32_MAX) {
                    ks->emulator()->graphics_system()->EnableReadPointerWriteBack(rptr_wb, 6);
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
// Use REX_EXTERN to define the extern "C" symbol that the recompiled code calls
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
    extern void VdSwap_entry(mapped_void, mapped_void, mapped_void, mapped_void,
        mapped_void, mapped_u32, mapped_u32, mapped_u32, mapped_u32, mapped_u32);
}}}

REX_EXTERN(__imp__VdSwap) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[VdSwap INTERCEPTED #%d] r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X r10=0x%08X\n",
                c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32);
            // Dump fetch constant at r4 (6 dwords)
            if (ctx.r4.u32) {
                uint32_t f0 = REX_LOAD_U32(ctx.r4.u32), f1 = REX_LOAD_U32(ctx.r4.u32+4);
                uint32_t f2 = REX_LOAD_U32(ctx.r4.u32+8), f3 = REX_LOAD_U32(ctx.r4.u32+12);
                fprintf(f, "  fetch[0..3]=%08X %08X %08X %08X\n", f0, f1, f2, f3);
                // base_address is in fetch dword 0 bits [31:12] (or similar)
                fprintf(f, "  fetch.base_addr_raw = 0x%08X (<<12 = 0x%08X)\n", f0 >> 12, (f0 >> 12) << 12);
            }
            // Dump frontbuffer ptr at r8
            if (ctx.r8.u32) {
                fprintf(f, "  *frontbuffer_ptr = 0x%08X\n", REX_LOAD_U32(ctx.r8.u32));
            }
            fclose(f);
        }
    }
    // Save buffer_ptr before call to check what VdSwap writes
    uint32_t buf_addr = ctx.r3.u32;

    // Call the real SDK implementation
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);

    // VdSwap has just written its packet into the guest command buffer: a Type0
    // write of the frontbuffer fetch constant (6 dwords at buf+4) followed by
    // PM4_XE_SWAP. The fetch it copies from the game's D3D9 texture header has
    // the tiled bit set, but our framebuffer is linear, so the texture cache
    // detiles linear data and the colours come out wrong. Clear the bit here,
    // in the game's own packet, rather than injecting a competing fetch write:
    // registers 0x4800..0x4805 double as vertex fetch 0, and writing them from
    // an out-of-band present breaks every later resolve (3037 failures per run).
    {
        uint32_t d0 = REX_LOAD_U32(buf_addr + 4);
        if (d0 & 0x80000000u) {
            REX_STORE_U32(buf_addr + 4, d0 & ~0x80000000u);
            static int c = 0;
            if (++c <= 3) {
                FILE* f = fopen("saintsrow_heartbeat.log", "a");
                if (f) { fprintf(f, "[SwapFetch #%d] cleared tiled bit: 0x%08X -> 0x%08X\n",
                                 c, d0, d0 & ~0x80000000u); fclose(f); }
            }
        }
    }

    // After VdSwap writes packets to ring buffer, we need to kick the
    // command processor. The game's ring buffer init (sub_825DAB58) is never
    // called, so VdInitializeRingBuffer never fires. We must:
    // 1. Initialize the ring buffer on first call
    // 2. Update the write pointer after each VdSwap
    {
        auto* ks = REX_KERNEL_STATE();

        // Bypass the ring buffer entirely and call IssueSwap directly.
        // VdSwap wrote the swap packet with:
        //   frontbuffer physical address = 0x09258000 (from buf[9])
        //   width = 1280, height = 720 (from buf[10], buf[11])
        // Also write the fetch constant to GPU register file first.
        uint32_t fb_phys = REX_LOAD_U32(buf_addr + 36); // buf[9] = frontbuffer phys
        uint32_t width = REX_LOAD_U32(buf_addr + 40);   // buf[10]
        uint32_t height = REX_LOAD_U32(buf_addr + 44);  // buf[11]

        // Read the fetch constant data (6 dwords after the Type0 header)
        uint32_t fetch[6];
        for (int i = 0; i < 6; i++) {
            fetch[i] = REX_LOAD_U32(buf_addr + 4 + i*4);
        }

        // Trace flush function gate conditions to understand what's blocking
        {
            uint32_t rs = 0x40001E00;
            uint8_t v10808 = REX_LOAD_U8(rs + 10808);
            uint8_t v10809 = REX_LOAD_U8(rs + 10809);
            uint32_t v12960 = REX_LOAD_U32(rs + 12960);
            uint32_t v13160 = REX_LOAD_U32(rs + 13160);
            uint32_t v13528 = REX_LOAD_U32(rs + 13528);
            uint32_t v40 = REX_LOAD_U32(rs + 40);
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
                        uint32_t v13160_152 = REX_LOAD_U32(v13160 + 152);
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

        // Test pattern removed - let the GPU's actual rendering show through.

        // Log ALL fetch constants to detect format changes over time
        static int fetch_log = 0;
        static uint32_t prev_fetch[6] = {};
        bool changed = (fetch_log == 0);
        for (int i = 0; i < 6 && !changed; i++)
            if (fetch[i] != prev_fetch[i]) changed = true;
        fetch_log++;
        if (changed || fetch_log <= 10 || fetch_log % 50 == 0) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) {
                fprintf(f, "[Fetch #%d%s] %08X %08X %08X %08X %08X %08X  fb=0x%08X %ux%u\n",
                    fetch_log, changed ? " CHANGED" : "",
                    fetch[0], fetch[1], fetch[2], fetch[3], fetch[4], fetch[5],
                    fb_phys, width, height);
                fclose(f);
            }
            for (int i = 0; i < 6; i++) prev_fetch[i] = fetch[i];
        }

        // No IssueSwap here - GL2_Render handles frequent presentation.
        // VdSwap fires too infrequently to be the sole presenter.
        static int kick_c = 0;
        if (++kick_c <= 10) {
            FILE* f = fopen("saintsrow_heartbeat.log", "a");
            if (f) { fprintf(f, "[VdSwap #%d] fb=0x%08X %ux%u (GL2_Render presents)\n",
                kick_c, fb_phys, width, height); fclose(f); }
        }
    }
}

// Trace GPU command buffer writer - this is the core function that writes PM4 to the command buffer
extern "C" void __imp__sub_825CC640(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825CC640) {
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
REX_EXTERN(sub_825CCAB0) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[DrawSetup #%d] r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_825CCAB0(ctx, base);
}

// Track sub_825CCB78 - complex draw state machine dispatching different draw modes
extern "C" void __imp__sub_825CCB78(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825CCB78) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[DrawStateMachine #%d] r3=0x%08X r4=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_825CCB78(ctx, base);
}

// Track shader/state setup
extern "C" void __imp__sub_825CC5B0(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_825CC5B0) {
    static int c = 0;
    if (++c <= 20) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[ShaderSetup #%d] r3=0x%08X r4=0x%08X r5=0x%08X\n", c, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); fclose(f); }
    }
    __imp__sub_825CC5B0(ctx, base);
}

static std::atomic<int> g_input_frame{0};

// Hook NtAllocateVirtualMemory to log failures (BaseHeap::Alloc page count too big)
extern "C" void __imp__NtAllocateVirtualMemory(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_827893D4) { // NtAllocateVirtualMemory thunk
    uint32_t base_addr_ptr = ctx.r3.u32;
    uint32_t region_size_ptr = ctx.r4.u32;
    uint32_t alloc_type = ctx.r5.u32;
    uint32_t protect = ctx.r6.u32;
    uint32_t input_base = base_addr_ptr ? REX_LOAD_U32(base_addr_ptr) : 0;
    uint32_t input_size = region_size_ptr ? REX_LOAD_U32(region_size_ptr) : 0;

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
        uint32_t out_addr = base_addr_ptr ? REX_LOAD_U32(base_addr_ptr) : 0;
        uint32_t out_size = region_size_ptr ? REX_LOAD_U32(region_size_ptr) : 0;
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
    extern void KeInitializeSemaphore_entry(ppc_ptr_t<rex::system::X_KSEMAPHORE>, u32, u32);
}}}

// Track semaphore addresses that have been initialized
static constexpr int MAX_TRACKED_SEMS = 32;
static uint32_t g_tracked_sem_addrs[MAX_TRACKED_SEMS] = {};
static uint32_t g_tracked_sem_flink[MAX_TRACKED_SEMS] = {};
static uint32_t g_tracked_sem_blink[MAX_TRACKED_SEMS] = {};
static int g_tracked_sem_count = 0;

REX_EXTERN(__imp__KeInitializeSemaphore) {
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
            REX_STORE_U32(sem_addr + 8, g_tracked_sem_flink[i]);   // wait_list_flink
            REX_STORE_U32(sem_addr + 12, g_tracked_sem_blink[i]);  // wait_list_blink

            // Set signal_state. The game passes count=0 to block threads until
            // work is queued. But with handle reuse, old threads are already
            // waiting on this semaphore. Setting count=0 keeps them blocked.
            // Set to 2 (matching the 2 IO threads) so they wake up and check
            // for new work items.
            uint32_t effective_count = (count == 0) ? 2 : count;
            REX_STORE_U32(sem_addr + 4, effective_count);  // signal_state

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
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::KeInitializeSemaphore_entry>(ctx, base);

    // Track the handle that was stashed
    if (g_tracked_sem_count < MAX_TRACKED_SEMS) {
        uint32_t flink = REX_LOAD_U32(sem_addr + 8);
        uint32_t blink = REX_LOAD_U32(sem_addr + 12);
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
REX_EXTERN(sub_826368E0) {
    uint32_t context = ctx.r3.u32;
    uint32_t sem_handle = context ? REX_LOAD_U32(context + 64) : 0;
    uint32_t work_func = context ? REX_LOAD_U32(context + 52) : 0;
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

// ---- Host-driven present over the ring buffer -------------------------------
//
// SDK 0.10 moved the GPU into the rexgpu-xenos plugin, which exports only
// rex_gpu_create, so CommandProcessor::IssueSwap is no longer reachable from the
// host and the old "force a swap from GL2_Render" hack died with the rebase.
// The supported equivalent is what VdSwap itself does: put a PM4 swap packet in
// the ring buffer and advance CP_RB_WPTR through GPU MMIO. The CP then runs
// ExecutePacketType3_XE_SWAP and calls IssueSwap on its own thread.
//
// ponytail: this presents the frontbuffer every game frame rather than waiting
// for the guest's own swap to survive our hand-built indirect buffers. Once the
// IB stream parses cleanly the game's own VdSwap packet will do this, and this
// helper goes away.
static uint32_t g_prim_write_pos_init_ = (g_prim_write_pos = 31);   // 0..30 reserved for the game's ME_INIT
static constexpr uint32_t RB_DWORDS = 8192;

// Append dwords to the primary ring buffer (physical memory, byte-swapped) and
// publish the new write pointer. Returns false when the buffer is not set up.
static bool RbSubmit(uint8_t* base, uint32_t rb_prim_virt, const uint32_t* dw, uint32_t n) {
    if (!rb_prim_virt || !n) return false;
    auto* ks = REX_KERNEL_STATE();
    uint32_t phys = ks->memory()->GetPhysicalAddress(rb_prim_virt);
    if (phys == UINT32_MAX) return false;
    uint8_t* host = ks->memory()->TranslatePhysical(phys);
    if (!host) return false;

    std::lock_guard<std::mutex> lock(g_rb_mutex);
    if (g_prim_write_pos + n >= RB_DWORDS - 16) {
        g_prim_write_pos = 31;
        std::memset(host + 31 * 4, 0, (RB_DWORDS - 31) * 4);
    }
    for (uint32_t i = 0; i < n; i++)
        rex::memory::store_and_swap<uint32_t>(host + (g_prim_write_pos + i) * 4, dw[i]);
    g_prim_write_pos += n;
    REX_MM_STORE_U32(REX_GPU_REG(0x01C5), g_prim_write_pos);   // CP_RB_WPTR
    return true;
}

// The frontbuffer fetch constant the game hands VdSwap, with the tiled bit
// cleared: the D3D12 framebuffer is linear, and leaving tiled set makes the
// texture cache detile linear data (crosshatch artifacts, wrong sky colour).
// Reference value only (see RbPresent for why it is not written): the
// frontbuffer fetch constant with the tiled bit cleared, since the D3D12
// framebuffer is linear and leaving tiled set makes the texture cache detile
// linear data.
[[maybe_unused]] static const uint32_t kSwapFetch[6] = {0x0A000002, 0x09258006, 0x0059E4FF,
                                                        0x00001414, 0x00000000, 0x00000200};

static bool RbPresent(uint8_t* base, uint32_t rb_prim_virt, uint32_t fb_phys,
                      uint32_t width, uint32_t height) {
    uint32_t pkt[12];
    uint32_t n = 0;
    // Deliberately NOT writing fetch constant 0 here. VdSwap sets it because it
    // owns the whole swap; we are injecting an extra present into a stream the
    // game is also driving, and registers 0x4800..0x4805 are *shared* between
    // texture fetch 0 and vertex fetch 0. The resolve path reads vertex fetch 0
    // (GetResolveInfo: "D3D9 HACK: Vertices to use are always in vf0"), so
    // writing the swap's texture fetch there makes every subsequent resolve fail
    // with "Unsupported resolve vertex buffer format". Measured: 3037 failed
    // resolves per run with the write, 25 without. The swap uses whatever
    // texture fetch 0 the game's own VdSwap last published, which is correct.
    pkt[n++] = (3u << 30) | ((4u - 1u) << 16) | (0x64u << 8);     // Type3 PM4_XE_SWAP, 4 dwords
    pkt[n++] = 0x53574150u;                                        // 'SWAP'
    pkt[n++] = fb_phys;
    pkt[n++] = width;
    pkt[n++] = height;
    bool ok = RbSubmit(base, rb_prim_virt, pkt, n);
    static int c = 0;
    if (++c <= 5 || c % 300 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[RbPresent #%d] rb=0x%08X fb=0x%08X %ux%u wp=%u ok=%d\n",
                         c, rb_prim_virt, fb_phys, width, height, g_prim_write_pos, ok); fclose(f); }
    }
    return ok;
}

// ---- Input ------------------------------------------------------------------
//
// There used to be XamInputGetState / XamInputGetKeystrokeEx / XamInputSetState
// overrides here that synthesised a START/A button cycle to walk the game past
// attract mode on its own. They worked -- too well: the fake presses skipped the
// cathedral loading screen, drove straight through the main menu, and then sat
// mashing buttons at the copyright screen.
//
// SDK 0.10 ships a real input system (SDL gamepad plus keyboard/mouse) and
// ReXApp attaches the window to it, so the SDK's own implementations are used
// now and the game responds to actual input. See git history for the synthetic
// versions if a scripted walkthrough is ever wanted for automated testing.

// Hook streaming manager callback (sub_8265F720) and work finder (sub_8265F4F0)
extern "C" void __imp__sub_8265F4F0(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8265F4F0) {
    uint32_t filter = ctx.r3.u32;
    // Check the pending request count at 0x827A6BC4
    uint32_t req_count = REX_LOAD_U8(0x827A6BC4);
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
REX_EXTERN(sub_8265F720) {
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
REX_EXTERN(sub_82636BB0) {
    uint32_t context = ctx.r3.u32;
    uint32_t item_before = context ? REX_LOAD_U32(context + 44) : 0;
    __imp__sub_82636BB0(ctx, base);
    uint32_t item_after = context ? REX_LOAD_U32(context + 44) : 0;
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
REX_EXTERN(sub_8260C318) {
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
REX_EXTERN(sub_8260CC50) {
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
            io_sync_flink[i] = REX_LOAD_U32(IO_SYNC_ADDRS[i] + 8);
            io_sync_blink[i] = REX_LOAD_U32(IO_SYNC_ADDRS[i] + 12);
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
REX_EXTERN(sub_8260FFC8) {
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
REX_EXTERN(sub_82604F38) {
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
REX_EXTERN(sub_82622CB8) {
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
        uint32_t obj = r4_arg ? REX_LOAD_U32(r4_arg) : 0;
        uint32_t sub_obj = obj ? REX_LOAD_U32(obj + 68) : 0;
        uint32_t vtable = sub_obj ? REX_LOAD_U32(sub_obj) : 0;
        uint32_t vt28 = vtable ? REX_LOAD_U32(vtable + 28) : 0;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOSetup #%d] returned r3=0x%08X | vtable chain: [r4]=0x%08X [+68]=0x%08X vt=0x%08X vt[28]=0x%08X\n",
            c, ctx.r3.u32, obj, sub_obj, vtable, vt28); fclose(f); }
    }
}

// Hook sub_8260D068 - thread config generator inside IOWork
extern "C" void __imp__sub_8260D068(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8260D068) {
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
                    fprintf(f, "%02X ", REX_LOAD_U8(cfg_addr + i));
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
REX_EXTERN(sub_82600A68) {
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
REX_EXTERN(sub_8260D180) {
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
REX_EXTERN(sub_82604FB8) {
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

// ---- IO format-handler registry: carry registrations across context swaps ---
//
// sub_8260C4F8 constructs an IO context (vtable, refcount, intrusive list heads)
// and unconditionally installs it at dword_837102B4. It runs once per IO-context
// create, so the "current" context is replaced constantly, and with it the
// format-handler registry that hangs off [ctx+60].
//
// IO init re-registers handlers 0..2 on every new context, but handler 3 is
// registered once, by game startup, through sub_82604D10 -> sub_8260D518. After
// the first swap a lookup for 3 returns null (sub_8260FF68), sub_82610168 answers
// E_NOTIMPL, sub_8260F990 propagates it, and sub_82105FE8 is left virtual-calling
// a null list entry -- the "target 0x0000000F" abort.
//
// Reusing the first context instead does fix the abort but stalls the streaming
// pipeline (the queue stops draining), so the contexts really are per-request.
// Record every registration and replay it onto each newly built context instead.
static constexpr int IO_HANDLER_SLOTS = 128;
struct IoHandlerReg { bool set; uint32_t a, b, c; };
static IoHandlerReg g_io_handlers[IO_HANDLER_SLOTS] = {};

extern "C" void __imp__sub_8260D518(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8260D518) {
    uint32_t idx = ctx.r4.u32;
    if (idx < IO_HANDLER_SLOTS) {
        g_io_handlers[idx] = {true, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32};
    }
    __imp__sub_8260D518(ctx, base);
}

extern "C" void __imp__sub_8260C4F8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8260C4F8) {
    __imp__sub_8260C4F8(ctx, base);
    uint32_t new_ctx = ctx.r3.u32;
    if (!new_ctx) return;

    int replayed = 0;
    for (int i = 0; i < IO_HANDLER_SLOTS; i++) {
        if (!g_io_handlers[i].set) continue;
        PPCContext rc = ctx;
        rc.r3.u64 = new_ctx;
        rc.r4.u64 = (uint32_t)i;
        rc.r5.u64 = g_io_handlers[i].a;
        rc.r6.u64 = g_io_handlers[i].b;
        rc.r7.u64 = g_io_handlers[i].c;
        __imp__sub_8260D518(rc, base);
        replayed++;
    }
    static int c = 0;
    if (++c <= 5 || c % 200 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOCtx #%d] new=0x%08X replayed %d handler(s)\n", c, new_ctx, replayed); fclose(f); }
    }
}

// Hook sub_82623408 (buffer allocator, vtable[20]) to find what vtable[16] call blocks
extern "C" void __imp__sub_82623408(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82623408) {
    uint32_t obj = ctx.r3.u32;
    uint32_t size = ctx.r4.u32;
    uint32_t vtable = obj ? REX_LOAD_U32(obj) : 0;
    uint32_t vt16 = vtable ? REX_LOAD_U32(vtable + 16) : 0;
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
REX_EXTERN(sub_82623160) {
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
REX_EXTERN(sub_82623250) {
    uint32_t hash = ctx.r3.u32;
    uint32_t data = ctx.r4.u32;
    uint32_t ctx_ptr = ctx.r5.u32;
    uint32_t obj = ctx_ptr ? REX_LOAD_U32(ctx_ptr) : 0;
    uint32_t vtable = obj ? REX_LOAD_U32(obj) : 0;
    uint32_t vt0 = vtable ? REX_LOAD_U32(vtable + 0) : 0;
    uint32_t vt8 = vtable ? REX_LOAD_U32(vtable + 8) : 0;
    uint32_t vt12 = vtable ? REX_LOAD_U32(vtable + 12) : 0;
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
    uint32_t obj_after = ctx_ptr ? REX_LOAD_U32(ctx_ptr) : 0;
    uint32_t vt_after = obj_after ? REX_LOAD_U32(obj_after) : 0;
    uint32_t vt5_after = vt_after ? REX_LOAD_U32(vt_after + 20) : 0;
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
    extern u32 ExCreateThread_entry(mapped_u32, u32,
        mapped_u32, u32, mapped_void, mapped_void, u32);
}}}

// Track handles from real ExCreateThread calls for reuse
static uint32_t g_io_thread_handles[8] = {};
static int g_io_thread_handle_count = 0;

REX_EXTERN(__imp__ExCreateThread) {
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
            REX_STORE_U32(handle_out_addr, reuse_handle);
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
    rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::ExCreateThread_entry>(ctx, base);
    if (ctx.r3.u32 == 0 && handle_out_addr && g_io_thread_handle_count < 8) {
        uint32_t new_handle = REX_LOAD_U32(handle_out_addr);
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
REX_EXTERN(sub_82789084) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[TERMINATE] XamLoaderTerminateTitle called! Blocking exit.\n"); fclose(f); }
    // Don't actually terminate - just sleep forever
    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
}

// Hook game init to trace progress
extern "C" void __imp__sub_827176E0(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_827176E0) {
    FILE* f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[GAME-INIT] entering sub_827176E0\n"); fclose(f); }
    __imp__sub_827176E0(ctx, base);
    f = fopen("saintsrow_heartbeat.log", "a");
    if (f) { fprintf(f, "[GAME-INIT] sub_827176E0 returned r3=0x%08X\n", ctx.r3.u32); fclose(f); }
}

// Hook xstart to trace the main game flow
extern "C" void __imp__xstart(PPCContext& ctx, uint8_t* base);
REX_EXTERN(xstart) {
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

// ---- Front-end init object-creation chain (current top blocker) -------------
// sub_82105FE8 virtual-calls every entry of the list at [r26+8]. The slot is
// filled only when sub_8260F990 returns >= 0; the game never checks, so a
// failure leaves a null there and the vtable load returns garbage (target 0x0F).
// Trace the chain to find which link fails:
//   sub_8260F990 -> sub_826243D8 -> sub_826235B8   (allocation)
//                -> sub_82623250(0x61820006, &out) (type-ID factory)
#define RETTRACE(fn, cap)                                                              \
    extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base);                       \
    REX_EXTERN(fn) {                                                                   \
        static int c = 0;                                                              \
        int n = ++c;                                                                   \
        uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32;                    \
        __imp__##fn(ctx, base);                                                        \
        if (n <= (cap)) {                                                              \
            FILE* f = fopen("saintsrow_heartbeat.log", "a");                           \
            if (f) {                                                                   \
                fprintf(f, "[CHAIN] " #fn " #%d(r3=0x%08X r4=0x%08X r5=0x%08X) -> %d\n", \
                        n, a3, a4, a5, ctx.r3.s32);                                    \
                fclose(f);                                                             \
            }                                                                          \
        }                                                                              \
    }
RETTRACE(sub_8260F990, 20)
// sub_826243D8 returns E_NOTIMPL (0x80004001) during front-end init. Two ways
// out: sub_826235B8 failing, or the vtable call on [[dword_837102B4]+60]-8
// (slot 20). Dump that chain on every failure so we can see which link is null.
extern "C" void __imp__sub_826243D8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_826243D8) {
    uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32;
    __imp__sub_826243D8(ctx, base);
    int32_t ret = ctx.r3.s32;
    static int c = 0, failed = 0;
    bool log = (++c <= 20) || (ret < 0 && ++failed <= 10);
    if (log) {
        uint32_t g1 = REX_LOAD_U32(0x837102B4);
        uint32_t g2 = g1 ? REX_LOAD_U32(g1 + 60) : 0;
        uint32_t obj = g2 ? g2 - 8 : 0;
        uint32_t vt = obj ? REX_LOAD_U32(obj) : 0;
        uint32_t fn = vt ? REX_LOAD_U32(vt + 20) : 0;
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) {
            fprintf(f, "[CHAIN] sub_826243D8 #%d(r3=0x%08X r4=0x%08X) -> 0x%08X | [837102B4]=0x%08X +60=0x%08X obj=0x%08X vt=0x%08X vt[20]=0x%08X\n",
                    c, a3, a4, (uint32_t)ret, g1, g2, obj, vt, fn);
            fclose(f);
        }
    }
}
RETTRACE(sub_826235B8, 400)

// The victim: log the list slot right before the virtual call would use it.
extern "C" void __imp__sub_82105FE8(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82105FE8) {
    static int c = 0;
    int n = ++c;
    if (n <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[CHAIN] sub_82105FE8 #%d ENTER r3=0x%08X r4=0x%08X\n",
                         n, ctx.r3.u32, ctx.r4.u32); fclose(f); }
    }
    __imp__sub_82105FE8(ctx, base);
    if (n <= 10) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[CHAIN] sub_82105FE8 #%d EXIT\n", n); fclose(f); }
    }
}

// ---- IO format-handler lookup: fall back to the image's master list --------
//
// sub_8260FF68(registry, idx) resolves an IO format handler: idx <= 127 indexes
// the table at [registry+12] bounded by [registry+8], higher indices use
// [registry+20]/[registry+16]. An 8-byte {a, b} entry counts as registered only
// when both dwords are non-zero.
//
// The registry hangs off the current IO context ([ctx+60]-8), and sub_8260C4F8
// installs a brand-new context at dword_837102B4 on every IO-context create, so
// the live registry is replaced constantly. Dumping it around IO init shows
// sub_8260CC50 filling slots 0..2 on every context but slot 3 only on the first,
// and slot 4 on none of them. A miss then makes sub_82610168 answer E_NOTIMPL,
// sub_8260F990 propagates it, and sub_82105FE8 is left virtual-calling the null
// it was handed -- the "target 0x0000000F" abort.
//
// The image carries the master list at 0x8203B56C: five 12-byte {a, b, n}
// records whose {a, b} pairs are exactly, in order, the registry slots 0..4.
// Slots 0..3 match what IO init installs; record 4 is the one nothing ever
// registers. Because the record's first two dwords have the same layout as a
// registry entry, a miss can be answered with the record's own guest address.
//
// ponytail: this serves the handler the image already describes; it does not
// explain why nothing registers record 4. Neither sub_82604D10/sub_8260D518 nor
// anything reachable by an immediate reference to 0x8203B56C writes these slots,
// so the registrar is still unidentified. Find it and this fallback goes away.
static constexpr uint32_t IO_HANDLER_LIST = 0x8203B56Cu;  // 12-byte {a, b, n}
static constexpr uint32_t IO_HANDLER_LIST_COUNT = 5u;

extern "C" void __imp__sub_8260FF68(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_8260FF68) {
    uint32_t self = ctx.r3.u32, idx = ctx.r4.u32 & 0xFF;
    __imp__sub_8260FF68(ctx, base);
    if (ctx.r3.u32 != 0 || idx >= IO_HANDLER_LIST_COUNT) return;

    uint32_t rec = IO_HANDLER_LIST + idx * 12;
    if (REX_LOAD_U32(rec) == 0 || REX_LOAD_U32(rec + 4) == 0) return;
    ctx.r3.u64 = rec;

    static int c = 0;
    if (++c <= 10 || c % 500 == 0) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOReg #%d] idx=%u missed on 0x%08X, served record 0x%08X (%08X/%08X)\n",
                         c, idx, self, rec, REX_LOAD_U32(rec), REX_LOAD_U32(rec + 4)); fclose(f); }
    }
}

// sub_82604D10(idx, fn, ?, ctx) registers a handler on the current IO context
// (dword_837102B4) via sub_8260D518, or returns 0x80004005 when no context is
// installed. Log every call: a registration that bails here never reaches any
// registry, which is why an index can be missing from all of them.
extern "C" void __imp__sub_82604D10(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82604D10) {
    uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a6 = ctx.r6.u32;
    uint32_t g = REX_LOAD_U32(0x837102B4);
    uint32_t reg = g ? REX_LOAD_U32(g + 60) : 0; if (reg) reg -= 8;
    uint32_t tab = reg ? REX_LOAD_U32(reg + 12) : 0;
    uint32_t pre[6] = {};
    for (int k = 0; k < 6 && tab; k++) pre[k] = REX_LOAD_U32(tab + k * 8);
    __imp__sub_82604D10(ctx, base);
    if (tab) {
        FILE* g2 = fopen("saintsrow_heartbeat.log", "a");
        if (g2) {
            fprintf(g2, "[IOReg-tab] reg=0x%08X tab=0x%08X", reg, tab);
            for (int k = 0; k < 6; k++)
                fprintf(g2, " %08X->%08X", pre[k], REX_LOAD_U32(tab + k * 8));
            fprintf(g2, "\n");
            fclose(g2);
        }
    }
    static int c = 0;
    if (++c <= 30) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[IOReg-add #%d] idx=%u fn=0x%08X ctx=0x%08X global=0x%08X -> 0x%08X\n",
                         c, a3, a4, a6, g, ctx.r3.u32); fclose(f); }
    }
}


// sub_82610168(self, desc, out): the index it resolves is the first byte of the
// descriptor at r4. Log it next to the result so we can tell whether the
// E_NOTIMPL that reaches sub_826243D8 really comes from this lookup.
extern "C" void __imp__sub_82610168(PPCContext& ctx, uint8_t* base);
REX_EXTERN(sub_82610168) {
    uint32_t self = ctx.r3.u32, desc = ctx.r4.u32;
    uint32_t idx = desc ? REX_LOAD_U8(desc) : 0xFFFF;
    __imp__sub_82610168(ctx, base);
    static int c = 0;
    if (++c <= 25) {
        FILE* f = fopen("saintsrow_heartbeat.log", "a");
        if (f) { fprintf(f, "[Fmt #%d] sub_82610168(self=0x%08X desc=0x%08X idx=%u) -> 0x%08X\n",
                         c, self, desc, idx, ctx.r3.u32); fclose(f); }
    }
}

