// Saints Row - Game-specific kernel stub overrides
// These override default ReXGlue SDK implementations where the game
// needs special handling.

#include "saintsrow_config.h"
#include "saintsrow_init.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <cstdint>
#include <cstdio>

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

// Override sub_8278D148 - Bink decode worker thread
PPC_FUNC(sub_8278D148) {
    static int bink_stub_count = 0;
    if (++bink_stub_count <= 2) {
        fprintf(stderr, "[STUB] Bink worker thread started (PPC 0x8278D148) -- stubbed\n");
    }
    // The thread context is in r3 (ctx.r3.u32)
    // Field at offset 0: state flag (1 = running)
    // Just return immediately - the game will handle Bink not completing
    ctx.r3.u64 = 0;
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
