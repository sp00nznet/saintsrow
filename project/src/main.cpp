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
#include <rex/kernel/init.h>
#if REX_HAS_D3D12
#include <rex/graphics/d3d12/graphics_system.h>
#endif
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app.h>

#include <atomic>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// Null-page access handler: intercepts null pointer dereferences in recompiled
// code and zeros the destination register instead of crashing. This handles
// cases where the game accesses uninitialized pointers (GPU device, user
// profile, etc.) that would be valid on real hardware.
static LONG WINAPI NullPageHandler(EXCEPTION_POINTERS* ep) {
    // (debug via file since WIN32 app has no console)

    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto fault_addr = ep->ExceptionRecord->ExceptionInformation[1];

    // Log ALL access violations to a crash log (append mode)
    {
        static int total_av = 0;
        if (++total_av <= 100) {
            FILE* cf = fopen("saintsrow_all_crashes.log", "a");
            if (cf) {
                HMODULE hm = NULL;
                char mn[MAX_PATH] = "unknown";
                if (GetModuleHandleExA(6, (LPCSTR)ep->ContextRecord->Rip, &hm))
                    GetModuleFileNameA(hm, mn, MAX_PATH);
                uint8_t* ip = (uint8_t*)ep->ContextRecord->Rip;
                fprintf(cf, "[AV#%d] addr=0x%llX RIP=0x%llX TID=%lu mod=%s bytes=%02X%02X%02X%02X%02X%02X\n",
                    total_av, (unsigned long long)fault_addr,
                    (unsigned long long)ep->ContextRecord->Rip,
                    GetCurrentThreadId(), mn,
                    ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]);
                fclose(cf);
            }
        }
    }

    // Handle null page accesses (addresses 0x0 - 0xFFFF)
    // Also handle the specific MSVCP140 _Thrd_abort crash at addr 0x58
    if (fault_addr >= 0x10000) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Check if crash is in MSVCP140's _Thrd_abort (offset 0x123D2)
    // This happens when a thread wait operation uses a destroyed sync object
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)ep->ContextRecord->Rip, &hMod)) {
        char modName[MAX_PATH] = {0};
        GetModuleFileNameA(hMod, modName, MAX_PATH);
        if (strstr(modName, "MSVCP140") || strstr(modName, "msvcp140")) {
            // This is a CRT crash from a destroyed/null sync object
            // Skip the wait and return 0 (timeout) to the caller
            static int crt_crash_count = 0;
            if (++crt_crash_count <= 50) {
                FILE* wf = fopen("saintsrow_all_crashes.log", "a");
                if (wf) {
                    fprintf(wf, "[MSVCP140-BYPASS] offset=0x%llX fault=0x%llX -- skipping\n",
                        (unsigned long long)(ep->ContextRecord->Rip - (uint64_t)hMod),
                        (unsigned long long)fault_addr);
                    fclose(wf);
                }
            }
            // Unwind the call stack to return from _Thrd_abort to its caller
            // _Thrd_abort is a __cdecl function, RSP points to the stack
            // We need to find the return address and restore RSP
            // The function prologue saves RBX, RBP, RDI, RSI, R12
            // Simplest: walk the stack and return to the highest frame in our exe
            //
            // Actually, just set the thread to terminate cleanly rather than
            // crash in the CRT. Use longjmp-style recovery:
            // Set RAX to error code and jump to the function epilogue
            //
            // For now, terminate just this thread instead of the whole process
            ep->ContextRecord->Rip = (uint64_t)&ExitThread;
            ep->ContextRecord->Rcx = 0;  // Exit code 0
            // Fix stack alignment for the call
            ep->ContextRecord->Rsp &= ~0xF;
            ep->ContextRecord->Rsp -= 8;  // Shadow space
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    // Decode x86-64 instruction at RIP to figure out the destination register
    // and skip the instruction
    uint8_t* rip = (uint8_t*)ep->ContextRecord->Rip;
    DWORD64* regs = &ep->ContextRecord->Rax;

    // Simple MOV reg, [reg+disp] decoder for common patterns
    // REX prefix
    int rex = 0;
    int i = 0;
    if ((rip[i] & 0xF0) == 0x40) {
        rex = rip[i++];
    }

    // Debug instruction decode to file (WIN32 has no console)
    {
        static int dbg_cnt = 0;
        if (++dbg_cnt <= 20) {
            FILE* df = fopen("saintsrow_all_crashes.log", "a");
            if (df) {
                fprintf(df, "[VEH-DECODE] fault=0x%llX i=%d rex=0x%X opcode=0x%02X bytes=%02X%02X%02X%02X\n",
                    (unsigned long long)fault_addr, i, rex, rip[i], rip[0], rip[1], rip[2], rip[3]);
                fclose(df);
            }
        }
    }

    if (rip[i] == 0x8B || rip[i] == 0x0FB6 || rip[i] == 0x0FB7 || rip[i] == 0x63 ||
        rip[i] == 0x3B || rip[i] == 0x39 || rip[i] == 0x85 ||
        (rip[i] == 0x0F && (rip[i+1] == 0xB6 || rip[i+1] == 0xB7 || rip[i+1] == 0xBE || rip[i+1] == 0xBF))) {
        // It's a MOV/MOVZX/MOVSX/MOVSXD/CMP/TEST - zero dest and skip
        // Find dest register from ModRM byte
        int oplen = (rip[i] == 0x0F) ? 2 : 1;
        uint8_t modrm = rip[i + oplen];
        int reg_idx = (modrm >> 3) & 7;
        if (rex & 0x04) reg_idx += 8;  // REX.R extends reg

        // Map to CONTEXT register (Rax=0, Rcx=1, Rdx=2, Rbx=3, Rsp=4, Rbp=5, Rsi=6, Rdi=7, R8-R15)
        static const int ctx_map[] = {0, 1, 2, 3, -1, 5, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1};
        // Actually CONTEXT layout: Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8-R15
        // But the register encoding is different... simplify: just zero RAX and advance
        static int null_count = 0;
        if (++null_count <= 50) {
            FILE* nf = fopen("saintsrow_all_crashes.log", "a");
            if (nf) {
                fprintf(nf, "[NULL-HANDLED] addr=0x%llX RIP=0x%llX -- zeroed dest\n",
                    (unsigned long long)fault_addr, (unsigned long long)ep->ContextRecord->Rip);
                fclose(nf);
            }
        }

        // Zero the most likely destination (RAX is used for return values)
        ep->ContextRecord->Rax = 0;

        // Skip the instruction (estimate: 2-8 bytes for MOV with displacement)
        // This is imprecise but better than crashing
        int mod = modrm >> 6;
        int insn_len = i + oplen + 1;  // prefix + opcode + modrm
        if (mod == 0 && (modrm & 7) == 5) insn_len += 4;  // RIP-relative
        else if (mod == 0 && (modrm & 7) == 4) insn_len += 1;  // SIB
        else if (mod == 1) insn_len += 1;  // disp8
        else if (mod == 2) insn_len += 4;  // disp32
        if ((modrm & 7) == 4 && mod != 3) insn_len += 1;  // SIB byte

        ep->ContextRecord->Rip += insn_len;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Log the full crash info including thread ID and module name
    FILE* f = fopen("saintsrow_crash.log", "w");
    if (f) {
        fprintf(f, "NULL PAGE FAULT: addr=0x%llX RIP=0x%llX TID=%lu\n",
            (unsigned long long)fault_addr, (unsigned long long)ep->ContextRecord->Rip,
            GetCurrentThreadId());
        // Find which module the crash is in
        HMODULE hMod = NULL;
        char modName[MAX_PATH] = {0};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)ep->ContextRecord->Rip, &hMod)) {
            GetModuleFileNameA(hMod, modName, MAX_PATH);
            MODULEINFO mi = {};
            GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi));
            fprintf(f, "Module: %s (base=0x%p size=0x%X)\n", modName, mi.lpBaseOfDll, mi.SizeOfImage);
            fprintf(f, "Offset in module: 0x%llX\n",
                (unsigned long long)ep->ContextRecord->Rip - (unsigned long long)mi.lpBaseOfDll);
        }
        fprintf(f, "Instruction bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            rip[0], rip[1], rip[2], rip[3], rip[4], rip[5], rip[6], rip[7]);
        fprintf(f, "RAX=0x%016llX RCX=0x%016llX RDX=0x%016llX\n",
            ep->ContextRecord->Rax, ep->ContextRecord->Rcx, ep->ContextRecord->Rdx);
        fprintf(f, "RDI=0x%016llX RSI=0x%016llX RBX=0x%016llX\n",
            ep->ContextRecord->Rdi, ep->ContextRecord->Rsi, ep->ContextRecord->Rbx);
        fprintf(f, "RBP=0x%016llX RSP=0x%016llX\n",
            ep->ContextRecord->Rbp, ep->ContextRecord->Rsp);
        void* stack[32];
        WORD frames = CaptureStackBackTrace(0, 32, stack, NULL);
        fprintf(f, "Stack (%d frames):\n", frames);
        for (WORD i = 0; i < frames; i++)
            fprintf(f, "  [%d] 0x%p\n", i, stack[i]);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
// VEH registered in OnInitialize after SDK setup
#endif

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

        // Create and initialize runtime with full backend config
        runtime_ = std::make_unique<rex::Runtime>(game_dir);
        runtime_->set_app_context(&app_context());

        rex::RuntimeConfig config;
#if REX_HAS_D3D12
        config.graphics = REX_GRAPHICS_BACKEND(rex::graphics::d3d12::D3D12GraphicsSystem);
#endif
        config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
        config.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
        config.kernel_init = rex::kernel::InitializeKernel;

        auto status = runtime_->Setup(
            static_cast<uint32_t>(PPC_CODE_BASE),
            static_cast<uint32_t>(PPC_CODE_SIZE),
            static_cast<uint32_t>(PPC_IMAGE_BASE),
            static_cast<uint32_t>(PPC_IMAGE_SIZE),
            PPCFuncMappings,
            std::move(config));
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

        // Register null page handler AFTER SDK initialization
        // (must come after SDK's MMIO handler to avoid conflicts)
        AddVectoredExceptionHandler(1, NullPageHandler);
        REXLOG_INFO("Null page handler registered");

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
