#pragma once
// Win32 stand-in for the host probe of the frame-time diagnostic: the five
// entry points src/proxy/frame_timing.cpp uses, so that production translation
// unit can be compiled and executed unchanged on the host
// (verification/probe/frame_timing_host.cpp, -I this directory). The clock is
// scripted by the probe, one tick per microsecond, so the accounting is exact;
// `real_clock` switches it to the host monotonic clock for the optional
// per-call cost measurement, where the clock read is the cost being measured.
#include <ctime>
#include <cwchar>

using DWORD = unsigned long;
union LARGE_INTEGER {
    long long QuadPart;
};

namespace x3m_win32_standin {
inline long long clock_ticks = 0;         // scripted QueryPerformanceCounter
inline long long frequency = 1000000;     // 1 tick = 1 microsecond
inline bool real_clock = false;           // cost mode: read the host clock
inline DWORD last_error = 0;
inline const wchar_t* environment = nullptr; // X3M_FRAME_TIMING value, or none
inline unsigned environment_reads = 0;
inline unsigned counter_reads = 0;
inline void advance(long long ticks) { clock_ticks += ticks; }
}

inline DWORD GetLastError() { return x3m_win32_standin::last_error; }
inline void SetLastError(DWORD value) { x3m_win32_standin::last_error = value; }

inline int QueryPerformanceCounter(LARGE_INTEGER* value) {
    ++x3m_win32_standin::counter_reads;
    if (x3m_win32_standin::real_clock) {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        value->QuadPart = static_cast<long long>(now.tv_sec) * 1000000000ll + now.tv_nsec;
    } else {
        value->QuadPart = x3m_win32_standin::clock_ticks;
    }
    return 1;
}

inline int QueryPerformanceFrequency(LARGE_INTEGER* value) {
    value->QuadPart = x3m_win32_standin::frequency;
    return 1;
}

inline DWORD GetEnvironmentVariableW(const wchar_t*, wchar_t* buffer, DWORD size) {
    ++x3m_win32_standin::environment_reads;
    const wchar_t* value = x3m_win32_standin::environment;
    if (!value) return 0;
    DWORD length = 0;
    while (value[length]) ++length;
    if (!buffer || size <= length) return length + 1;
    for (DWORD i = 0; i <= length; ++i) buffer[i] = value[i];
    return length;
}
