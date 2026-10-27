#include "log.h"
#ifndef BIOSHOCK_RELEASE

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logLock;

void LogInit(const char* path) {
    InitializeCriticalSection(&g_logLock);
    // Лог прошлого запуска сохраняется: после вылета в нём видно, что случилось
    char prev[MAX_PATH];
    snprintf(prev, sizeof(prev), "%s", path);
    char* dot = strrchr(prev, '.');
    if (dot) strcpy(dot, ".prev.log");
    MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    g_log = fopen(path, "w");
    char launch[MAX_PATH];
    snprintf(launch, sizeof(launch), "%s", path);
    char* filename = strrchr(launch, '\\');
    if (filename) {
        strcpy(filename + 1, "BioshockVR.launch");
        FILE* token = fopen(launch, "rb");
        if (token) {
            char id[80] = {};
            if (fgets(id, sizeof(id), token)) {
                id[strcspn(id, "\r\n")] = 0;
                Log("Launch: %s", id);
            }
            fclose(token);
        }
    }
}

void Log(const char* format, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}
#endif
