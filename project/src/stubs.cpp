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

// Override sub_8278D148 - Bink decode worker thread
// The Bink worker receives a context struct in r3 with:
//   +0x00: state flag (0=idle, 1=running, checked by main thread)
//   +0x04: parameter 1
//   +0x08: semaphore/event to wait on
//   +0x14: event to signal when done
// We need to keep the thread alive and looping, not exit immediately,
// because the main thread expects to communicate with it via shared state.
PPC_FUNC(sub_8278D148) {
    static int bink_stub_count = 0;
    if (++bink_stub_count <= 2) {
        fprintf(stderr, "[STUB] Bink worker thread started (PPC 0x8278D148) -- idle loop\n");
    }
    // Set state to 0 (idle/done) so the main thread doesn't wait forever
    uint32_t context_ptr = ctx.r3.u32;
    if (context_ptr) {
        PPC_STORE_U32(context_ptr + 0, 0);  // state = idle
    }
    // Sleep indefinitely - the thread stays alive but does nothing
    // The main thread will check the state flag and see it's idle
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
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
