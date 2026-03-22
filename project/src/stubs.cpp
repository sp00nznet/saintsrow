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
BINK_STUB(82789658)
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
// Content / License Stubs
// ============================================================================

// XamContentGetLicenseMask - return full license (all content unlocked)
extern "C" uint32_t XamContentGetLicenseMask_entry(
    uint32_t* mask_ptr, void* overlapped) {
    if (mask_ptr) *mask_ptr = 0xFFFFFFFF;
    return 0;  // X_ERROR_SUCCESS
}
