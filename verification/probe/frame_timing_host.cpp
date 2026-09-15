// Host checks of the frame-time window reduction in src/proxy/frame_timing.h:
// percentiles, the slow count, the four-slot slowest ring and the absence of
// any allocation while samples are collected. No Win32, no device, no game.
#include "../../src/proxy/frame_timing.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) { std::fprintf(stderr, "allocated\n"); std::abort(); } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace x3m::frame_timing;
static unsigned checks = 0;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "check %u failed\n", checks); std::exit(1); } }

static Window window; // static storage: the production window is a global too

int main() {
    Summary summary;
    check(!window.close(summary)); // an empty window reports nothing
    check(window.count() == 0 && !window.full());

    // Ascending series: dt 10..3000 us, draws 0..299, present 0..598.
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames; ++i)
        window.add({i, (i + 1) * 10ull, i * 2ull, i, i * 7ull});
    check(window.full() && window.count() == window_frames);
    check(window.close(summary));
    refuse_allocation = false;
    check(summary.frames == 300 && summary.frame == 299);
    check(summary.dt_p50 == 1510 && summary.dt_p95 == 2860 && summary.dt_max == 3000);
    check(summary.draws_p50 == 150 && summary.draws_max == 299);
    check(summary.present_p50 == 300 && summary.present_p95 == 570 && summary.present_max == 598);
    check(summary.slow == 0); // no frame exceeds twice the median here
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 299 && summary.slow_frames[0].dt_us == 3000);
    check(summary.slow_frames[0].draws == 299 && summary.slow_frames[0].present_us == 598 && summary.slow_frames[0].prims == 299 * 7);
    check(summary.slow_frames[1].frame == 298 && summary.slow_frames[2].frame == 297 && summary.slow_frames[3].frame == 296);
    check(window.count() == 0); // close restarts the window
    check(!window.close(summary));

    // Flat 1000 us window with five 5000 us spikes: slow counts every frame
    // above twice the median, the ring retains the four earliest of the tie.
    for (unsigned i = 0; i < window_frames; ++i) {
        const bool spike = i == 10 || i == 20 || i == 30 || i == 40 || i == 50;
        window.add({i, spike ? 5000ull : 1000ull, spike ? 900ull : 100ull, spike ? 4000ull : 500ull, 0});
    }
    check(window.close(summary));
    check(summary.dt_p50 == 1000 && summary.dt_max == 5000 && summary.dt_p95 == 1000);
    check(summary.slow == 5);
    check(summary.draws_p50 == 500 && summary.draws_max == 4000);
    check(summary.present_p50 == 100 && summary.present_max == 900);
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 10 && summary.slow_frames[1].frame == 20);
    check(summary.slow_frames[2].frame == 30 && summary.slow_frames[3].frame == 40);
    for (unsigned i = 0; i < 4; ++i) check(summary.slow_frames[i].dt_us == 5000);

    // A partial window reduces over the samples it has; the ring is ordered by
    // dt, slowest first, with a late spike displacing an earlier smaller one.
    const std::uint64_t dts[] = {300, 100, 500, 200, 400};
    for (unsigned i = 0; i < 5; ++i) window.add({i, dts[i], dts[i] / 10, i, i});
    check(!window.full() && window.count() == 5);
    check(window.close(summary));
    check(summary.frames == 5 && summary.frame == 4);
    check(summary.dt_p50 == 300 && summary.dt_p95 == 500 && summary.dt_max == 500);
    check(summary.present_p50 == 30 && summary.present_max == 50);
    check(summary.slow == 0);
    check(summary.slow_frames_count == 4);
    check(summary.slow_frames[0].frame == 2 && summary.slow_frames[1].frame == 4);
    check(summary.slow_frames[2].frame == 0 && summary.slow_frames[3].frame == 3);

    // One sample: every percentile is that sample and nothing is slow.
    window.add({77, 9000, 8000, 12, 34});
    check(window.close(summary));
    check(summary.frames == 1 && summary.frame == 77 && summary.dt_p50 == 9000 && summary.dt_p95 == 9000 && summary.dt_max == 9000);
    check(summary.draws_p50 == 12 && summary.present_p95 == 8000 && summary.slow == 0);
    check(summary.slow_frames_count == 1 && summary.slow_frames[0].prims == 34);

    // More than 300 samples without a close: the extra samples are dropped
    // rather than written past the fixed arrays, and the ring still tracks dt.
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames + 50; ++i) window.add({i, 1000, 100, 1, 1});
    refuse_allocation = false;
    check(window.count() == window_frames);
    check(window.close(summary));
    check(summary.frames == 300 && summary.frame == 349 && summary.dt_max == 1000);

    std::printf("frame_timing_host checks=%u failures=0\n", checks);
    return 0;
}
