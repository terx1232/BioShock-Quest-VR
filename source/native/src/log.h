#pragma once
#ifdef BIOSHOCK_RELEASE
inline void LogInit(const char*){}
inline void Log(const char*,...){}
#else

// Appends a line to BioshockVR.log next to the game exe (flushed, so it survives a crash).
void Log(const char* format, ...);
void LogInit(const char* path);
#endif
