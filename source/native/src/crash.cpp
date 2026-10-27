// Ловушка вылетов: при серьёзном исключении (обращение к памяти, деление на ноль, переполнение
// стека) пишет в лог место (модуль+смещение) и адреса возврата со стека. Игра обрабатывает
// исключения сама (окно "Critical error"), поэтому смотрим их раньше всех - векторным обработчиком.
#include "crash.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#include "log.h"

namespace crash {
namespace {

volatile LONG g_reported = 0;

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

bool Serious(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW ||
           code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR;
}

volatile LONG g_watchLeft = 0;
DWORD g_watchAddr = 0;

LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP && (info->ContextRecord->Dr6 & 1) && g_watchAddr) {
        CONTEXT* c = info->ContextRecord;
        LONG left = InterlockedDecrement(&g_watchLeft);
        if (left >= 0) {
            char where[96], line[1024];
            Describe(c->Eip, where, sizeof(where));
            int n = snprintf(line, sizeof(line), "Watch: write to %08lx (now %08lx) by %s thread %lu |", g_watchAddr,
                             *(DWORD*)(UINT_PTR)g_watchAddr, where, GetCurrentThreadId());
            const DWORD* stack = (const DWORD*)(UINT_PTR)c->Esp;
            int found = 0;
            for (int k = 0; k < 128 && found < 8 && n < (int)sizeof(line) - 100; k++) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(stack + k, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
                DWORD v = stack[k];
                if (v >= 0x10901000 && v < 0x10f00000) {
                    Describe(v, where, sizeof(where));
                    n += snprintf(line + n, sizeof(line) - n, " %s", where);
                    found++;
                }
            }
            Log("%s", line);
        }
        c->Dr6 = 0;
        if (left <= 0) c->Dr7 = 0;  // хватит - снимаем точку с этого потока
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (!Serious(code) || InterlockedIncrement(&g_reported) > 8) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    char where[96], line[1024];
    Describe(c->Eip, where, sizeof(where));
    int n = snprintf(line, sizeof(line), "EXCEPTION 0x%08lx at %s", code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        n += snprintf(line + n, sizeof(line) - n, " (%s 0x%08lx)",
                      info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                      (DWORD)info->ExceptionRecord->ExceptionInformation[1]);
    n += snprintf(line + n, sizeof(line) - n, " eax=%08lx ecx=%08lx edx=%08lx esp=%08lx |",
                  c->Eax, c->Ecx, c->Edx, c->Esp);
    const DWORD* stack = (const DWORD*)(UINT_PTR)c->Esp;
    int found = 0;
    for (int k = 0; k < 256 && found < 10 && n < (int)sizeof(line) - 100; k++) {
        MEMORY_BASIC_INFORMATION mbi;
        DWORD v;
        if (!VirtualQuery(stack + k, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
        v = stack[k];
        if (v > 0x10000 && VirtualQuery((LPCVOID)(UINT_PTR)v, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            Describe(v, where, sizeof(where));
            n += snprintf(line + n, sizeof(line) - n, " %s", where);
            found++;
        }
    }
    Log("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI ArmWatch(LPVOID) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te = {sizeof(te)};
    int armed = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != (DWORD)-1) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                c.Dr0 = g_watchAddr;
                c.Dr7 = (c.Dr7 & ~0xF0003u) | 1u | (1u << 16) | (3u << 18);  // L0, запись, 4 байта
                if (SetThreadContext(t, &c)) armed++;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    Log("Watch: %08lx armed on %d threads", g_watchAddr, armed);
    return 0;
}

}  // namespace

void Install() { AddVectoredExceptionHandler(1, Handler); }

void WatchWrites(void* address, int hits) {
    g_watchAddr = (DWORD)(UINT_PTR)address;
    InterlockedExchange(&g_watchLeft, hits);
    CloseHandle(CreateThread(nullptr, 0, ArmWatch, nullptr, 0, nullptr));
}

}  // namespace crash
