#include "frame_timing.h"
#include "capture.h"
#include <windows.h>

namespace x3m::frame_timing {

bool active = false;

namespace {
std::uint64_t frequency = 1; // QueryPerformanceFrequency, never zero
Window window;
std::uint64_t previous_qpc = 0;  // previous frame boundary
std::uint64_t present_stamp = 0; // stamp taken ahead of the forwarded Present
std::uint64_t present_us = 0;    // native Present time of the frame in progress
std::uint64_t prims = 0;         // primitives submitted in the frame in progress

std::uint64_t stamp() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}
std::uint64_t microseconds(std::uint64_t ticks) noexcept { return ticks * 1000000ull / frequency; }
}

void initialize() noexcept {
    const DWORD saved = GetLastError();
    wchar_t setting[8]{};
    active = GetEnvironmentVariableW(L"X3M_FRAME_TIMING", setting, 8) == 1 && setting[0] == L'1';
    if (active) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        frequency = f.QuadPart > 0 ? static_cast<std::uint64_t>(f.QuadPart) : 1;
        window.reset();
        previous_qpc = present_stamp = present_us = prims = 0;
    }
    SetLastError(saved);
}

namespace detail {

void present_begin_impl() noexcept {
    const DWORD saved = GetLastError();
    present_stamp = stamp();
    SetLastError(saved);
}

void present_end_impl() noexcept {
    const DWORD saved = GetLastError();
    if (present_stamp) {
        const std::uint64_t now = stamp();
        if (now > present_stamp) present_us += microseconds(now - present_stamp);
        present_stamp = 0;
    }
    SetLastError(saved);
}

void draw_impl(unsigned primitives) noexcept { prims += primitives; }

void frame_impl(std::uint64_t frame, std::uint64_t draws) noexcept {
    const DWORD saved = GetLastError();
    const std::uint64_t now = stamp();
    // The first observed frame only starts the interval: its dt is unknown and
    // would otherwise carry the whole load time into the first window.
    if (previous_qpc && now > previous_qpc)
        window.add({frame, microseconds(now - previous_qpc), present_us, draws, prims});
    previous_qpc = now;
    present_us = 0;
    prims = 0;
    if (window.full()) {
        Summary s;
        if (window.close(s)) {
            log("frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu present_max_us=%llu slow=%u",
                s.frame, s.frames, s.dt_p50, s.dt_p95, s.dt_max, s.draws_p50, s.draws_max,
                s.present_p50, s.present_p95, s.present_max, s.slow);
            for (unsigned i = 0; i < s.slow_frames_count; ++i) {
                const Frame& f = s.slow_frames[i];
                log("frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu",
                    f.frame, f.dt_us, f.draws, f.present_us, f.prims);
            }
        }
    }
    SetLastError(saved);
}

}
}
