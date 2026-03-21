// Saints Row - Console boot test (no GPU/window)
// Tests whether the recompiled game code initializes correctly

#include "saintsrow_config.h"
#include "saintsrow_init.h"

#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/kernel/init.h>

#include <cstdio>
#include <filesystem>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    std::filesystem::path game_dir;
    if (argc > 1) {
        game_dir = argv[1];
    } else {
        game_dir = std::filesystem::current_path() / ".." / "extracted";
    }

    // Init logging to console
    auto log_config = rex::BuildLogConfig(nullptr, "trace", {});
    rex::InitLogging(log_config);

    printf("=== Saints Row Console Boot Test ===\n");
    printf("Game dir: %s\n", game_dir.string().c_str());

    if (!std::filesystem::exists(game_dir / "default.xex")) {
        printf("ERROR: default.xex not found in %s\n", game_dir.string().c_str());
        return 1;
    }

    // Create runtime with NO graphics, NO audio, NO input (headless)
    rex::RuntimeConfig config;
    config.tool_mode = true;  // Skip GPU initialization
    config.kernel_init = rex::kernel::InitializeKernel;

    auto runtime = std::make_unique<rex::Runtime>(game_dir);

    printf("[1] Setting up runtime (tool_mode, no GPU)...\n");
    auto status = runtime->Setup(
        static_cast<uint32_t>(PPC_CODE_BASE),
        static_cast<uint32_t>(PPC_CODE_SIZE),
        static_cast<uint32_t>(PPC_IMAGE_BASE),
        static_cast<uint32_t>(PPC_IMAGE_SIZE),
        PPCFuncMappings,
        std::move(config));

    if (XFAILED(status)) {
        printf("ERROR: Runtime setup failed: 0x%08X\n", status);
        return 1;
    }
    printf("[1] Runtime setup OK\n");

    printf("[2] Loading XEX image...\n");
    status = runtime->LoadXexImage("game:\\default.xex");
    if (XFAILED(status)) {
        printf("ERROR: XEX load failed: 0x%08X\n", status);
        return 1;
    }
    printf("[2] XEX loaded OK\n");

    printf("[3] Launching module...\n");
    auto main_thread = runtime->LaunchModule();
    if (!main_thread) {
        printf("ERROR: Failed to launch module\n");
        return 1;
    }
    printf("[3] Module launched! Entry point executing...\n");

    // Let it run for a few seconds
    printf("[4] Waiting 5 seconds for game init...\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    printf("[5] Terminating...\n");
    if (runtime->kernel_state()) {
        runtime->kernel_state()->TerminateTitle();
    }

    // Wait for thread to finish
    main_thread->Wait(0, 0, 0, nullptr);
    printf("[6] Done!\n");

    return 0;
}
