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

// Null object page: a 64K block of zeroed guest memory that acts as a "safe"
// target for null pointer dereferences. When game code reads [null+offset],
// instead of crashing we return a pointer to this zeroed page, so subsequent
// field reads get 0 values instead of cascading crashes.
//
// We also make the null page self-referencing: every 4-byte aligned slot
// points back to itself, so pointer chains like obj->field->subfield all
// resolve to the null page.
static uint64_t g_null_object_host_addr = 0;  // Host address for x86 code
static uint32_t g_null_object_guest_addr = 0;  // Guest address for PPC loads

// Watchdog: sample all threads to find which PPC function is spinning
#include <tlhelp32.h>

static void WatchdogThread() {
    Sleep(5000);
    DWORD pid = GetCurrentProcessId();
    DWORD my_tid = GetCurrentThreadId();
    HMODULE exe = GetModuleHandleA(NULL);
    MODULEINFO mi = {};
    GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof(mi));
    uint64_t exe_base = (uint64_t)mi.lpBaseOfDll;
    uint64_t exe_end = exe_base + mi.SizeOfImage;

    FILE* wf = fopen("saintsrow_watchdog.log", "w");
    if (!wf) return;
    for (int s = 0; s < 10; s++) {
        fprintf(wf, "--- Sample %d ---\n", s);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(snap, &te)) do {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == my_tid) continue;
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                if (!ht) continue;
                SuspendThread(ht);
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(ht, &ctx)) {
                    uint64_t rip = ctx.Rip;
                    if (rip >= exe_base && rip < exe_end) {
                        uint32_t best_ppc = 0; uint64_t best_dist = UINT64_MAX;
                        for (int j = 0; PPCFuncMappings[j].guest != 0; j++) {
                            uint64_t fn = (uint64_t)PPCFuncMappings[j].host;
                            if (fn <= rip && (rip - fn) < best_dist) {
                                best_dist = rip - fn; best_ppc = (uint32_t)PPCFuncMappings[j].guest;
                            }
                        }
                        fprintf(wf, "  TID=%lu exe+0x%llX", te.th32ThreadID, (unsigned long long)(rip - exe_base));
                        if (best_ppc && best_dist < 0x1000000)
                            fprintf(wf, " -> PPC 0x%08X (+0x%llX)", best_ppc, (unsigned long long)best_dist);
                        fprintf(wf, "\n");
                    } else {
                        // Print non-exe threads too, with module info
                        HMODULE hm = NULL;
                        char mn[64] = "??";
                        if (GetModuleHandleExA(6, (LPCSTR)rip, &hm))
                            GetModuleBaseNameA(GetCurrentProcess(), hm, mn, sizeof(mn));
                        fprintf(wf, "  TID=%lu %s+0x%llX\n", te.th32ThreadID, mn, (unsigned long long)(rip - (uint64_t)hm));
                    }
                }
                ResumeThread(ht);
                CloseHandle(ht);
            } while (Thread32Next(snap, &te));
            CloseHandle(snap);
        }
        fflush(wf);
        Sleep(1000);
    }
    fclose(wf);
}

static void InitNullObjectPage(uint8_t* membase) {
    // Allocate a page in guest address space for the null object
    // Use address 0x0F000000 (in the v00000000 heap, unlikely to conflict)
    g_null_object_guest_addr = 0x0F000000;
    uint8_t* host = membase + g_null_object_guest_addr;
    g_null_object_host_addr = (uint64_t)host;

    // Commit the page
    VirtualAlloc(host, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    memset(host, 0, 0x10000);

    // Make every 4-byte slot self-referencing with the HOST address
    // The generated x86 code works with host addresses directly
    // For 64-bit pointers, fill with the host address
    // For 32-bit PPC loads (PPC_LOAD_U32), fill with guest address
    // Since the code might use either, use 0 (safest) -- the demand pager
    // will handle subsequent faults
    // Actually: the generated code uses PPC_LOAD_U32 which reads from
    // (base + guest_addr), so the values stored should be guest addresses
    // that will be translated. Let's fill with the guest addr.
    uint32_t* slots = (uint32_t*)host;
    for (int i = 0; i < 0x10000 / 4; i++) {
        slots[i] = 0;  // Zero is safest -- prevents infinite self-reference loops
    }
}

// Null-page access handler
static LONG WINAPI NullPageHandler(EXCEPTION_POINTERS* ep) {
    // For null page faults, try to identify the PPC function
    // by checking return addresses in the stack against the function mapping table
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->ExceptionInformation[1] < 0x10000) {
        FILE* pf = fopen("saintsrow_ppc_crash.log", "a");
        if (pf) {
            // Walk the stack and try to find which generated sub_ function we're in
            void* frames[32];
            WORD nframes = CaptureStackBackTrace(0, 32, frames, NULL);
            HMODULE exe = GetModuleHandleA(NULL);
            MODULEINFO mi = {};
            GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof(mi));
            uint64_t exe_base = (uint64_t)mi.lpBaseOfDll;
            uint64_t exe_end = exe_base + mi.SizeOfImage;

            fprintf(pf, "=== NULL at 0x%llX, RIP=0x%llX ===\n",
                (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1],
                (unsigned long long)ep->ContextRecord->Rip);
            fprintf(pf, "EXE base=0x%llX size=0x%X\n", exe_base, mi.SizeOfImage);
            fprintf(pf, "RIP offset in exe: 0x%llX\n",
                (unsigned long long)(ep->ContextRecord->Rip - exe_base));

            // Try to resolve PPC addresses by scanning PPCFuncMappings
            // Each entry has {ppc_addr, host_func_ptr}
            // Find the entry whose host pointer is closest to (but below) each frame
            for (WORD fi = 0; fi < nframes && fi < 20; fi++) {
                uint64_t addr = (uint64_t)frames[fi];
                if (addr >= exe_base && addr < exe_end) {
                    // Search function table for the best match
                    uint32_t best_ppc = 0;
                    uint64_t best_dist = UINT64_MAX;
                    for (int j = 0; PPCFuncMappings[j].guest != 0; j++) {
                        uint64_t fn = (uint64_t)PPCFuncMappings[j].host;
                        if (fn <= addr && (addr - fn) < best_dist) {
                            best_dist = addr - fn;
                            best_ppc = (uint32_t)PPCFuncMappings[j].guest;
                        }
                    }
                    if (best_ppc && best_dist < 0x100000) {
                        fprintf(pf, "  frame[%d] exe+0x%llX -> PPC 0x%08X (+0x%llX)\n", fi,
                            (unsigned long long)(addr - exe_base), best_ppc,
                            (unsigned long long)best_dist);
                    } else {
                        fprintf(pf, "  frame[%d] exe+0x%llX (SDK/system)\n", fi,
                            (unsigned long long)(addr - exe_base));
                    }
                }
            }
            fprintf(pf, "\n");
            fclose(pf);
        }
    }

    // Handle BREAKPOINT (int 3 / assert_always in SDK)
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        static int bp_count = 0;
        if (++bp_count <= 20) {
            FILE* bf = fopen("saintsrow_all_crashes.log", "a");
            if (bf) {
                uint8_t* ip = (uint8_t*)ep->ContextRecord->Rip;
                fprintf(bf, "[BREAKPOINT #%d] RIP=0x%llX bytes=%02X%02X%02X%02X TID=%lu\n",
                    bp_count, (unsigned long long)ep->ContextRecord->Rip,
                    ip[0], ip[1], ip[2], ip[3], GetCurrentThreadId());
                fclose(bf);
            }
        }
        // Skip the int 3 (1 byte) and continue
        ep->ContextRecord->Rip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Handle ILLEGAL_INSTRUCTION (ud2 from __builtin_trap in SDK code)
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) {
        uint8_t* ip = (uint8_t*)ep->ContextRecord->Rip;
        if (ip[0] == 0x0F && ip[1] == 0x0B) {
            // ud2 instruction - skip it (2 bytes)
            static int ud2_count = 0;
            if (++ud2_count <= 20) {
                FILE* uf = fopen("saintsrow_all_crashes.log", "a");
                if (uf) {
                    fprintf(uf, "[UD2] __builtin_trap at RIP=0x%llX -- skipped\n",
                        (unsigned long long)ep->ContextRecord->Rip);
                    fclose(uf);
                }
            }
            ep->ContextRecord->Rip += 2;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Handle null function pointer calls (RIP is 0 or in null object page)
    // This happens when code does `call [ptr]` where ptr was null/zeroed
    auto fault_rip = ep->ContextRecord->Rip;
    if (fault_rip < 0x10000 || (g_null_object_host_addr && fault_rip >= g_null_object_host_addr && fault_rip < g_null_object_host_addr + 0x10000)) {
        // Simulate a `ret` - pop return address from stack, return 0
        uint64_t* rsp = (uint64_t*)ep->ContextRecord->Rsp;
        ep->ContextRecord->Rip = *rsp;  // Pop return address
        ep->ContextRecord->Rsp += 8;     // Adjust stack
        ep->ContextRecord->Rax = 0;      // Return 0
        static int null_call_count = 0;
        if (++null_call_count <= 20) {
            FILE* nf = fopen("saintsrow_all_crashes.log", "a");
            if (nf) {
                fprintf(nf, "[NULL-CALL] call to 0x%llX, returning to 0x%llX with RAX=0\n",
                    (unsigned long long)fault_rip, (unsigned long long)ep->ContextRecord->Rip);
                fclose(nf);
            }
        }
        return EXCEPTION_CONTINUE_EXECUTION;
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
    if (fault_addr < 0x10000) {
        // Fall through to null page handler below
    }
    // Handle writes to uncommitted guest physical memory (GPU command buffer)
    // Guest range 0xA0000000-0xBFFFFFFF maps to host 0x1A0000000-0x1BFFFFFFF
    // Also handle 0x80000000-0x8FFFFFFF (XEX heap) and other guest ranges
    else if (fault_addr >= 0x100000000ull && fault_addr < 0x200000000ull) {
        // Don't demand-page guest address 0 (null page should remain invalid)
        uint32_t guest_addr = (uint32_t)(fault_addr - 0x100000000ull);
        if (guest_addr < 0x10000) {
            // This is a null page access via membase translation - treat as null deref
            // Fall through to the instruction decoder below
            goto null_page_handler;
        }
        // Commit the page on demand (4KB aligned)
        void* page_addr = (void*)(fault_addr & ~0xFFFull);
        void* result = VirtualAlloc(page_addr, 0x10000, MEM_COMMIT, PAGE_READWRITE);
        if (result) {
            static int demand_page = 0;
            if (++demand_page <= 20) {
                FILE* gf = fopen("saintsrow_all_crashes.log", "a");
                if (gf) {
                    fprintf(gf, "[DEMAND-PAGE] committed 64K at host 0x%llX (guest 0x%08X)\n",
                        (unsigned long long)(uintptr_t)page_addr,
                        (uint32_t)(fault_addr - 0x100000000ull));
                    fclose(gf);
                }
            }
            return EXCEPTION_CONTINUE_EXECUTION;  // Retry the faulting instruction
        }
        // VirtualAlloc failed - skip the instruction instead
        return EXCEPTION_CONTINUE_SEARCH;
    }
    else {
        // Check if the faulting instruction is a CALL (FF /2) - if so, the
        // fault is from jumping to an invalid function pointer. Simulate ret.
        uint8_t* call_ip = (uint8_t*)ep->ContextRecord->Rip;
        int ci = 0;
        if ((call_ip[ci] & 0xF0) == 0x40) ci++;  // REX prefix
        if (call_ip[ci] == 0xFF) {
            uint8_t modrm = call_ip[ci + 1];
            int reg_field = (modrm >> 3) & 7;
            if (reg_field == 2 || reg_field == 3) {  // CALL or CALLF
                // The fault is from reading the call target from memory
                // (e.g. call [rsi+rax*2]). The call hasn't pushed a return
                // address yet - skip the entire call instruction and set RAX=0.
                int oplen = ci + 2;  // prefix + FF + modrm
                int mod = modrm >> 6;
                int crm = modrm & 7;
                if (crm == 4 && mod != 3) oplen += 1;  // SIB
                if (mod == 0 && crm == 5) oplen += 4;  // RIP-relative
                else if (mod == 1) oplen += 1;  // disp8
                else if (mod == 2) oplen += 4;  // disp32
                ep->ContextRecord->Rip += oplen;
                ep->ContextRecord->Rax = 0;
                static int bad_call_count = 0;
                if (++bad_call_count <= 20) {
                    FILE* bf = fopen("saintsrow_all_crashes.log", "a");
                    if (bf) {
                        fprintf(bf, "[BAD-CALL] call [mem] at RIP+0x%llX fault_addr=0x%llX -- skipped (%d bytes)\n",
                            (unsigned long long)(ep->ContextRecord->Rip - oplen - (uint64_t)call_ip + (uint64_t)call_ip),
                            (unsigned long long)fault_addr, oplen);
                        fclose(bf);
                    }
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        // For non-call AVs at unknown addresses, try to skip the instruction
        // by zeroing the dest register (same as null page handler)
        // Fall through to the instruction decoder below
    }

null_page_handler:
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

    // x86-64 instruction prefix + opcode decoder
    int rex = 0;
    int i = 0;
    // Skip legacy prefixes (66, 67, F2, F3, 2E, 3E, 26, 36, 64, 65)
    while (rip[i] == 0x66 || rip[i] == 0x67 || rip[i] == 0xF0 || rip[i] == 0xF2 || rip[i] == 0xF3 ||
           rip[i] == 0x2E || rip[i] == 0x3E || rip[i] == 0x26 || rip[i] == 0x36 ||
           rip[i] == 0x64 || rip[i] == 0x65) {
        i++;
        if (i > 4) break;  // Safety limit
    }
    // REX prefix (0x40-0x4F)
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

    // Handle any instruction with a ModRM byte that accesses memory
    // This covers MOV, MOVZX, MOVSX, CMP, TEST, ADD, SUB, AND, OR, XOR, etc.
    bool has_modrm = false;
    int oplen = 1;
    uint8_t op = rip[i];
    if (op == 0x0F && (rip[i+1] == 0x38 || rip[i+1] == 0x3A)) {
        has_modrm = true; oplen = 3;  // 3-byte opcode (0F 38 xx / 0F 3A xx)
        if (rip[i+1] == 0x3A) oplen = 3;  // 0F3A has imm8 but we handle that separately
    } else if (op == 0x0F && (rip[i+1] == 0xB6 || rip[i+1] == 0xB7 || rip[i+1] == 0xBE || rip[i+1] == 0xBF ||
                       rip[i+1] == 0xB0 || rip[i+1] == 0xB1 ||  // CMPXCHG
                       rip[i+1] == 0xC1 || rip[i+1] == 0xC0 ||  // XADD
                       rip[i+1] == 0xAF ||  // IMUL
                       rip[i+1] == 0xA3 || rip[i+1] == 0xAB || rip[i+1] == 0xB3 || rip[i+1] == 0xBB)) {  // BT/BTS/BTR/BTC
        has_modrm = true; oplen = 2;  // 2-byte opcode
    } else if (op == 0x8B || op == 0x89 || op == 0x8A || op == 0x88 ||  // MOV variants
               op == 0x3B || op == 0x39 || op == 0x3A || op == 0x38 ||  // CMP variants
               op == 0x85 || op == 0x84 ||  // TEST
               op == 0x63 ||  // MOVSXD
               op == 0x03 || op == 0x01 || op == 0x2B || op == 0x29 ||  // ADD/SUB
               op == 0x23 || op == 0x21 || op == 0x0B || op == 0x09 ||  // AND/OR
               op == 0x33 || op == 0x31 ||  // XOR
               op == 0x80 || op == 0x81 || op == 0x83 ||  // Immediate ops
               op == 0x86 || op == 0x87) {  // XCHG
        has_modrm = true; oplen = 1;
    }
    if (has_modrm) {
        // It's an instruction with ModRM - zero dest register and skip
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

        // Zero the destination register properly based on ModRM encoding
        // Register order in x86-64: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8-R15
        DWORD64* ctx_regs[] = {
            &ep->ContextRecord->Rax, &ep->ContextRecord->Rcx,
            &ep->ContextRecord->Rdx, &ep->ContextRecord->Rbx,
            &ep->ContextRecord->Rsp, &ep->ContextRecord->Rbp,
            &ep->ContextRecord->Rsi, &ep->ContextRecord->Rdi,
            &ep->ContextRecord->R8,  &ep->ContextRecord->R9,
            &ep->ContextRecord->R10, &ep->ContextRecord->R11,
            &ep->ContextRecord->R12, &ep->ContextRecord->R13,
            &ep->ContextRecord->R14, &ep->ContextRecord->R15,
        };
        static const char* reg_names[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
            "r8","r9","r10","r11","r12","r13","r14","r15"};
        if (reg_idx < 16 && reg_idx != 4) {  // Don't zero RSP!
            // Instead of zeroing, point to the null object page (HOST address)
            // This prevents cascading null derefs down pointer chains
            // The x86 code uses host addresses, so we give it the host addr
            if (g_null_object_host_addr != 0) {
                *ctx_regs[reg_idx] = g_null_object_host_addr;
            } else {
                *ctx_regs[reg_idx] = 0;
            }
            static int null_detail_count = 0;
            if (++null_detail_count <= 30) {
                int rm = modrm & 7;
                if (rex & 0x01) rm += 8;
                FILE* rf = fopen("saintsrow_all_crashes.log", "a");
                if (rf) {
                    fprintf(rf, "[NULL-OBJ] dest=%s -> host 0x%llX (was reading [%s+0x%llX])\n",
                        reg_names[reg_idx], (unsigned long long)g_null_object_host_addr,
                        rm < 16 ? reg_names[rm] : "?",
                        (unsigned long long)fault_addr);
                    fclose(rf);
                }
            }
        } else {
            ep->ContextRecord->Rax = g_null_object_host_addr ? g_null_object_host_addr : 0;
        }

        // Skip the instruction (estimate: 2-8 bytes for MOV with displacement)
        // This is imprecise but better than crashing
        int mod = modrm >> 6;
        int rm = modrm & 7;
        int insn_len = i + oplen + 1;  // prefix + opcode + modrm
        // SIB byte present when rm==4 and mod!=3
        bool has_sib = (rm == 4 && mod != 3);
        if (has_sib) insn_len += 1;
        // Displacement
        if (mod == 0 && rm == 5) insn_len += 4;  // RIP-relative (no SIB special case)
        else if (mod == 0 && has_sib) {
            uint8_t sib = rip[i + oplen + 1];
            if ((sib & 7) == 5) insn_len += 4;  // SIB base=5 with mod=0 means disp32
        }
        if (mod == 1) insn_len += 1;  // disp8
        else if (mod == 2) insn_len += 4;  // disp32

        // Add immediate operand size for certain opcodes
        if (op == 0x80 || op == 0x83) insn_len += 1;  // imm8
        else if (op == 0x81) insn_len += 4;  // imm32

        ep->ContextRecord->Rip += insn_len;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Fallback: log unhandled instruction and let the crash handler produce a log
    {
        static int fallback_count = 0;
        if (++fallback_count <= 20) {
            FILE* ff = fopen("saintsrow_all_crashes.log", "a");
            if (ff) {
                fprintf(ff, "[UNHANDLED-AV] addr=0x%llX RIP=0x%llX bytes=%02X%02X%02X%02X\n",
                    (unsigned long long)fault_addr, (unsigned long long)ep->ContextRecord->Rip,
                    rip[0], rip[1], rip[2], rip[3]);
                fclose(ff);
            }
        }
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

        // Initialize null object page in guest memory
        InitNullObjectPage(runtime_->memory()->virtual_membase());
        REXLOG_INFO("Null object page at guest 0x{:08X}", g_null_object_guest_addr);

        // Register null page handler AFTER SDK initialization
        AddVectoredExceptionHandler(1, NullPageHandler);
        REXLOG_INFO("Null page handler registered");

        // Start watchdog thread to sample PPC execution
        std::thread(WatchdogThread).detach();

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
