// Saints Row (Xbox 360, 2006) - ReXGlue Recompiled Project
// Minimal bootstrap - gets the runtime up and launches the XEX module

#include "saintsrow_config.h"
#include "saintsrow_init.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app.h>

#include <atomic>
#include <filesystem>
#include <thread>

// VEH crash handler removed - was conflicting with SDK's MMIO exception handler

class SaintsRowApp : public rex::ui::WindowedApp, public rex::ui::WindowListener {
public:
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
        return std::make_unique<SaintsRowApp>(ctx);
    }

    SaintsRowApp(rex::ui::WindowedAppContext& ctx)
        : WindowedApp(ctx, "saintsrow", "[game_directory]") {
        AddPositionalOption("game_directory");
    }

    bool OnInitialize() override {
        auto exe_dir = rex::filesystem::GetExecutableFolder();

        std::filesystem::path game_dir;
        if (auto arg = GetArgument("game_directory")) {
            game_dir = *arg;
        } else {
            game_dir = exe_dir / ".." / "extracted";
        }

        std::string log_file_cvar = REXCVAR_GET(log_file);
        std::string log_level_str = REXCVAR_GET(log_level);
        if (REXCVAR_GET(log_verbose) && log_level_str == "info") {
            log_level_str = "trace";
        }
        auto log_config = rex::BuildLogConfig(
            log_file_cvar.empty() ? nullptr : log_file_cvar.c_str(),
            log_level_str, {});
        rex::InitLogging(log_config);
        rex::RegisterLogLevelCallback();
        REXLOG_INFO("Saints Row starting");
        REXLOG_INFO("  Game directory: {}", game_dir.string());

        // Create and initialize runtime
        runtime_ = std::make_unique<rex::Runtime>(game_dir);
        runtime_->set_app_context(&app_context());

        auto status = runtime_->Setup(
            static_cast<uint32_t>(PPC_CODE_BASE),
            static_cast<uint32_t>(PPC_CODE_SIZE),
            static_cast<uint32_t>(PPC_IMAGE_BASE),
            static_cast<uint32_t>(PPC_IMAGE_SIZE),
            PPCFuncMappings);
        if (XFAILED(status)) {
            REXLOG_ERROR("Runtime setup failed: {:08X}", status);
            return false;
        }

        // Test guest memory access before loading XEX
        {
            auto* mem = runtime_->memory();
            if (mem) {
                auto* heap = mem->LookupHeap(0x82000000);
                REXLOG_INFO("Memory heap for 0x82000000: {}", heap ? "VALID" : "NULL");
                if (heap) {
                    REXLOG_INFO("  Heap page_size={}, host_offset=0x{:X}",
                        heap->page_size(), heap->host_address_offset());
                    // Try translating the address
                    uint8_t* host = mem->TranslateVirtual(0x82000000u);
                    REXLOG_INFO("  TranslateVirtual(0x82000000) = {}", (void*)host);
                    // Try a small allocation
                    bool ok = heap->AllocFixed(0x82000000, 4096, 4096,
                        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite);
                    REXLOG_INFO("  Test AllocFixed: {}", ok ? "OK" : "FAILED");
                    if (ok) {
                        // Try writing to it
                        *host = 0x42;
                        REXLOG_INFO("  Test write: OK (read back: 0x{:02X})", *host);
                        heap->Decommit(0x82000000, 4096);
                    }
                }
            }
        }

        // Load XEX image
        status = runtime_->LoadXexImage("game:\\default.xex");
        if (XFAILED(status)) {
            REXLOG_ERROR("Failed to load XEX: {:08X}", status);
            return false;
        }
        REXLOG_INFO("XEX image loaded successfully!");
        spdlog::default_logger()->flush();

        REXLOG_INFO("XEX fully loaded. Attempting window creation...");
        spdlog::default_logger()->flush();

        // Create window
        window_ = rex::ui::Window::Create(app_context(), "Saints Row - Recomp", 1280, 720);
        if (!window_) {
            REXLOG_ERROR("Failed to create window");
            return false;
        }

        window_->AddListener(this);
        window_->Open();
        runtime_->set_display_window(window_.get());

        // Launch module
        app_context().CallInUIThreadDeferred([this]() {
            auto main_thread = runtime_->LaunchModule();
            if (!main_thread) {
                REXLOG_ERROR("Failed to launch module");
                app_context().QuitFromUIThread();
                return;
            }

            module_thread_ = std::thread([this, main_thread = std::move(main_thread)]() mutable {
                main_thread->Wait(0, 0, 0, nullptr);
                REXLOG_INFO("Execution complete");
                if (!shutting_down_.load(std::memory_order_acquire)) {
                    app_context().CallInUIThread([this]() {
                        app_context().QuitFromUIThread();
                    });
                }
            });
        });

        return true;
    }

    void OnClosing(rex::ui::UIEvent& e) override {
        (void)e;
        REXLOG_INFO("Window closing, shutting down...");
        shutting_down_.store(true, std::memory_order_release);
        if (runtime_ && runtime_->kernel_state()) {
            runtime_->kernel_state()->TerminateTitle();
        }
        app_context().QuitFromUIThread();
    }

    void OnDestroy() override {
        if (window_) {
            window_->SetPresenter(nullptr);
        }
        if (module_thread_.joinable()) {
            module_thread_.join();
        }
        if (window_) {
            window_->RemoveListener(this);
        }
        window_.reset();
        runtime_.reset();
    }

private:
    std::unique_ptr<rex::Runtime> runtime_;
    std::unique_ptr<rex::ui::Window> window_;
    std::thread module_thread_;
    std::atomic<bool> shutting_down_{false};
};

XE_DEFINE_WINDOWED_APP(saintsrow, SaintsRowApp::Create)
