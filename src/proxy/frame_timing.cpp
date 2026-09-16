#include "frame_timing.h"
#include <windows.h>

// The session log (src/proxy/capture.h), declared here rather than included:
// this translation unit uses nothing else from the proxy, so the host probe
// compiles it as it is against a Win32 stand-in and its own log sink
// (verification/probe/frame_timing_host.cpp).
namespace x3m { void log(const char* format, ...); }

namespace x3m::frame_timing {

bool active = false;

namespace {
constexpr unsigned draw_bucket = static_cast<unsigned>(Bucket::Draw);
constexpr unsigned scene_bucket = static_cast<unsigned>(Bucket::Scene);
constexpr unsigned state_bucket = static_cast<unsigned>(Bucket::State);
std::uint64_t frequency = 1; // QueryPerformanceFrequency, never zero
Window window;
std::uint64_t previous_qpc = 0;  // previous frame boundary
std::uint64_t present_stamp = 0; // stamp taken ahead of the forwarded Present
std::uint64_t present_us = 0;    // native Present time of the frame in progress
std::uint64_t prims = 0;         // primitives submitted in the frame in progress
// Per-frame hooked-call accounting, in ticks; converted once at the boundary.
std::uint64_t bucket_ticks[bucket_count]{};
std::uint64_t bucket_calls[bucket_count]{};
std::uint64_t draw_native_ticks = 0;
std::uint64_t draw_native_stamp = 0;
std::uint64_t slow_call_ticks = 0;
const char* slow_call_entry = "";
// Native Present ticks since attach. A Scope subtracts what accumulated inside
// it, so the buckets stay proxy-side; it is never reset at a frame boundary
// because the Present hook's own Scope spans that boundary.
std::uint64_t native_excluded = 0;
unsigned depth = 0; // hooked-call nesting: only the outermost entry is timed

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
        draw_native_ticks = draw_native_stamp = slow_call_ticks = native_excluded = 0;
        slow_call_entry = "";
        depth = 0;
        for (unsigned b = 0; b < bucket_count; ++b) bucket_ticks[b] = bucket_calls[b] = 0;
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
        if (now > present_stamp) {
            present_us += microseconds(now - present_stamp);
            native_excluded += now - present_stamp;
        }
        present_stamp = 0;
    }
    SetLastError(saved);
}

void draw_native_begin_impl() noexcept {
    const DWORD saved = GetLastError();
    draw_native_stamp = stamp();
    SetLastError(saved);
}

void draw_native_end_impl() noexcept {
    const DWORD saved = GetLastError();
    if (draw_native_stamp) {
        const std::uint64_t now = stamp();
        if (now > draw_native_stamp) draw_native_ticks += now - draw_native_stamp;
        draw_native_stamp = 0;
    }
    SetLastError(saved);
}

// One QueryPerformanceCounter pair per outermost hooked call: no division, no
// allocation and no logging on this path. A reentrant hooked call (the proxy's
// own device calls inside a pass or inside Present) only moves the nesting
// depth, so its time stays attributed to the entry the game made.
void scope_begin_impl(ScopeState& state, unsigned bucket, const char* entry) noexcept {
    state.entered = true;
    state.outermost = depth == 0;
    ++depth;
    if (!state.outermost) return;
    const DWORD saved = GetLastError();
    state.bucket = bucket;
    state.entry = entry ? entry : "";
    state.exclude = native_excluded;
    state.start = stamp();
    SetLastError(saved);
}

void scope_end_impl(ScopeState& state) noexcept {
    state.entered = false;
    if (depth) --depth;
    if (!state.outermost) return;
    const DWORD saved = GetLastError();
    const std::uint64_t now = stamp();
    std::uint64_t ticks = now > state.start ? now - state.start : 0;
    const std::uint64_t excluded = native_excluded - state.exclude;
    ticks = ticks > excluded ? ticks - excluded : 0;
    if (state.bucket < bucket_count) {
        bucket_ticks[state.bucket] += ticks;
        ++bucket_calls[state.bucket];
    }
    if (ticks > slow_call_ticks) { slow_call_ticks = ticks; slow_call_entry = state.entry; }
    SetLastError(saved);
}

void draw_impl(unsigned primitives) noexcept { prims += primitives; }

void frame_impl(std::uint64_t frame, std::uint64_t draws) noexcept {
    const DWORD saved = GetLastError();
    const std::uint64_t now = stamp();
    // The first observed frame only starts the interval: its dt is unknown and
    // would otherwise carry the whole load time into the first window.
    if (previous_qpc && now > previous_qpc) {
        Frame sample{frame, microseconds(now - previous_qpc), present_us, draws, prims, {}, 0, {}, "", 0};
        for (unsigned b = 0; b < bucket_count; ++b) {
            sample.bucket_us[b] = microseconds(bucket_ticks[b]);
            sample.bucket_calls[b] = bucket_calls[b];
        }
        sample.draw_native_us = microseconds(draw_native_ticks);
        sample.slow_call = slow_call_entry;
        sample.slow_call_us = microseconds(slow_call_ticks);
        window.add(sample);
    }
    previous_qpc = now;
    present_us = 0;
    prims = 0;
    draw_native_ticks = 0;
    slow_call_ticks = 0;
    slow_call_entry = "";
    for (unsigned b = 0; b < bucket_count; ++b) bucket_ticks[b] = bucket_calls[b] = 0;
    if (window.full()) {
        Summary s;
        if (window.close(s)) {
            log("frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu present_max_us=%llu"
                " draw_p50_us=%llu draw_p95_us=%llu draw_max_us=%llu draw_native_p50_us=%llu draw_native_max_us=%llu"
                " scene_p50_us=%llu scene_p95_us=%llu scene_max_us=%llu state_p50_us=%llu state_p95_us=%llu state_max_us=%llu"
                " draw_calls_p50=%llu scene_calls_p50=%llu state_calls_p50=%llu slow=%u",
                s.frame, s.frames, s.dt_p50, s.dt_p95, s.dt_max, s.draws_p50, s.draws_max,
                s.present_p50, s.present_p95, s.present_max,
                s.bucket_p50[draw_bucket], s.bucket_p95[draw_bucket], s.bucket_max[draw_bucket],
                s.draw_native_p50, s.draw_native_max,
                s.bucket_p50[scene_bucket], s.bucket_p95[scene_bucket], s.bucket_max[scene_bucket],
                s.bucket_p50[state_bucket], s.bucket_p95[state_bucket], s.bucket_max[state_bucket],
                s.bucket_calls_p50[draw_bucket], s.bucket_calls_p50[scene_bucket], s.bucket_calls_p50[state_bucket],
                s.slow);
            for (unsigned i = 0; i < s.slow_frames_count; ++i) {
                const Frame& f = s.slow_frames[i];
                log("frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu"
                    " draw_us=%llu draw_native_us=%llu scene_us=%llu state_us=%llu"
                    " draw_calls=%llu scene_calls=%llu state_calls=%llu slow_call=%s slow_call_us=%llu",
                    f.frame, f.dt_us, f.draws, f.present_us, f.prims,
                    f.bucket_us[draw_bucket], f.draw_native_us, f.bucket_us[scene_bucket], f.bucket_us[state_bucket],
                    f.bucket_calls[draw_bucket], f.bucket_calls[scene_bucket], f.bucket_calls[state_bucket],
                    f.slow_call && f.slow_call[0] ? f.slow_call : "none", f.slow_call_us);
            }
        }
    }
    SetLastError(saved);
}

}
}
