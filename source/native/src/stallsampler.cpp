// Startup diagnosis: until the game has created its D3D device, samples where its main thread
// is every few seconds (instruction pointer plus return addresses found on the stack) and logs
// them with module names, so a stall on the headset can be traced to a function.
#include "stallsampler.h"

#include <windows.h>
#include <psapi.h>
#include <stdio.h>

#include "log.h"

namespace stallsampler {
namespace {

HANDLE g_thread = nullptr;
volatile LONG g_done = 0;

void Describe(DWORD address, char* out, size_t size) {
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(UINT_PTR)address, &module) && module) {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = strrchr(path, '\\');
        snprintf(out, size, "%s+0x%lx", name ? name + 1 : path, address - (DWORD)(UINT_PTR)module);
    } else {
        snprintf(out, size, "0x%08lx", address);
    }
}

bool IsCode(DWORD address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(UINT_PTR)address, &mbi, sizeof(mbi))) return false;
    return mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

DWORD WINAPI Sample(LPVOID) {
    for (int i = 0; i < 200 && !g_done; i++) {
        Sleep(i < 5 ? 1000 : 3000);
        if (g_done) break;
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (SuspendThread(g_thread) == (DWORD)-1) continue;
        BOOL ok = GetThreadContext(g_thread, &ctx);
        DWORD stack[384] = {};
        SIZE_T got = 0;
        if (ok) ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(UINT_PTR)ctx.Esp, stack, sizeof(stack), &got);
        ResumeThread(g_thread);
        if (!ok) {
            Log("Stall: no context (error %lu)", GetLastError());
            continue;
        }
        char line[1024], where[96];
        Describe(ctx.Eip, where, sizeof(where));
        int n = snprintf(line, sizeof(line), "Stall %d: eip=%s |", i, where);
        int found = 0;
        for (SIZE_T k = 0; k < got / 4 && found < 10 && n < (int)sizeof(line) - 100; k++) {
            if (stack[k] > 0x10000 && IsCode(stack[k])) {
                Describe(stack[k], where, sizeof(where));
                n += snprintf(line + n, sizeof(line) - n, " %s", where);
                found++;
            }
        }
        Log("%s", line);
    }
    return 0;
}

}  // namespace

void Start() {
    if (g_thread) return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_thread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    CreateThread(nullptr, 0, Sample, nullptr, 0, nullptr);
}

void Stop() { InterlockedExchange(&g_done, 1); }

}  // namespace stallsampler
