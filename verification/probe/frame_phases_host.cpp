// Host checks of the frame-phase tracker and window reduction in
// src/proxy/frame_phases_core.h: a scripted frame with its ten stamps and the
// Present pair, the skip/repeat/backward order rules, the view accumulators,
// the dropped frame, the 300-frame reduction with its incomplete count and
// slowest ring, and no allocation while sampling. No Win32, no game.
#include "../../src/proxy/frame_phases_core.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) {
    if (refuse_allocation) {
        std::fprintf(stderr, "allocated\n");
        std::abort();
    }
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

using namespace x3m::frame_phases::detail;
static unsigned checks = 0;
static void check(bool value) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "check %u failed\n", checks);
        std::exit(1);
    }
}

static Tracker tracker; // static storage like the production object
static Window reduction;
static std::uint64_t clock_ticks = 1000; // one tick per microsecond
static void at(std::uint64_t advance) {
    clock_ticks += advance;
}
// One scripted frame: stamp k after `gap[k]` microseconds, `views` view
// triples of (setup 3, submit 7) inside the views phase, Present of `present`.
static bool scripted_frame(std::uint64_t frame, const std::uint64_t* gap, unsigned views, std::uint64_t present,
                           Sample& out) {
    for (unsigned k = 0; k < core_sites; ++k) {
        at(gap[k]);
        tracker.site(k, clock_ticks);
        if (k == 3)
            for (unsigned v = 0; v < views; ++v) {
                at(1);
                tracker.site(7, clock_ticks);
                at(3);
                tracker.site(8, clock_ticks);
                at(7);
                tracker.site(9, clock_ticks);
            }
    }
    at(gap[core_sites]);
    tracker.present_begin(clock_ticks);
    at(present);
    tracker.present_end(clock_ticks);
    return tracker.take(frame, 1000000, out);
}

int main() {
    Sample s;
    // Stamps before the first Present return are unmatched; nothing is live.
    tracker.site(0, clock_ticks);
    tracker.site(7, clock_ticks);
    check(tracker.unmatched == 2 && !tracker.live && !tracker.take(1, 1000000, s));
    tracker.present_begin(clock_ticks); // ignored while not live
    check(tracker.order_errors == 0);
    tracker.present_end(clock_ticks); // starts frame accounting, no sample yet
    check(tracker.live && !tracker.pending && !tracker.take(1, 1000000, s));

    const std::uint64_t gaps[core_sites + 1] = {100, 10, 20, 30, 40, 50, 60, 70};
    check(scripted_frame(1, gaps, 2, 500, s));
    check(s.frame == 1 && s.complete && s.views == 2);
    check(s.phase_us[pre_render] == 100 && s.phase_us[1] == 10 && s.phase_us[2] == 20 && s.phase_us[3] == 30);
    check(s.phase_us[4] == 40 + 2 * 11 && s.phase_us[5] == 50 && s.phase_us[6] == 60 && s.phase_us[7] == 70);
    check(s.phase_us[present_phase] == 500 && s.view_setup_us == 6 && s.view_submit_us == 14);
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < phase_count; ++i) sum += s.phase_us[i];
    check(s.dt_us == sum && s.dt_us == 100 + 10 + 20 + 30 + 62 + 50 + 60 + 70 + 500);
    check(!tracker.take(1, 1000000, s)); // taken once
    check(tracker.order_errors == 0 && tracker.clock_errors == 0 && tracker.dropped == 0);

    // A frame without views skips from begin_scene to text (site 5): allowed,
    // the skipped phases are zero and the frame is incomplete.
    at(5);
    tracker.site(0, clock_ticks);
    at(6);
    tracker.site(1, clock_ticks);
    at(7);
    tracker.site(2, clock_ticks);
    at(80);
    tracker.site(5, clock_ticks);
    at(9);
    tracker.site(6, clock_ticks);
    at(1);
    tracker.present_begin(clock_ticks);
    at(2);
    tracker.present_end(clock_ticks);
    check(tracker.take(2, 1000000, s) && !s.complete && s.views == 0);
    check(s.phase_us[3] == 80 && s.phase_us[4] == 0 && s.phase_us[5] == 0 && s.phase_us[6] == 9 && s.phase_us[7] == 1);
    check(tracker.order_errors == 0);

    // A repeated or backward core stamp drops the frame: no sample at Present,
    // the next Present return restarts, and the counters say why.
    at(5);
    tracker.site(0, clock_ticks);
    at(5);
    tracker.site(3, clock_ticks);
    at(5);
    tracker.site(2, clock_ticks);
    check(tracker.order_errors == 1 && tracker.dropped == 1 && !tracker.live);
    at(5);
    tracker.site(6, clock_ticks); // unmatched while dropped
    check(tracker.unmatched == 3);
    tracker.present_begin(clock_ticks);
    tracker.present_end(clock_ticks);
    check(!tracker.take(3, 1000000, s) && tracker.live);
    at(5);
    tracker.site(0, clock_ticks);
    at(5);
    tracker.site(0, clock_ticks);
    check(tracker.order_errors == 2 && tracker.dropped == 2);
    tracker.present_end(clock_ticks);
    check(tracker.order_errors == 2 && tracker.live); // a Present end while dropped only restarts
    // Two present_begin without an end: order error, frame dropped.
    tracker.present_begin(clock_ticks);
    tracker.present_begin(clock_ticks);
    check(tracker.order_errors == 3 && tracker.dropped == 3);
    tracker.present_end(clock_ticks);
    check(tracker.live);
    // A backward clock is a clock error that drops the frame.
    tracker.site(0, clock_ticks - 1);
    check(tracker.clock_errors == 1 && tracker.dropped == 4 && !tracker.live);
    tracker.present_end(clock_ticks);
    // A view stamp pair straddling nothing: submit_end without a begin adds nothing.
    at(1);
    tracker.site(9, clock_ticks);
    at(1);
    tracker.site(8, clock_ticks);
    check(scripted_frame(4, gaps, 0, 1, s) && s.view_submit_us == 0 && s.view_setup_us == 0 && s.views == 0);
    check(!tracker.take(4, 0, s)); // no frequency, no sample

    // Window: ascending dt with one incomplete frame in three, slowest four last.
    Summary summary;
    check(!reduction.close(summary));
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames; ++i) {
        Sample f{};
        f.frame = i;
        f.complete = i % 3 != 0;
        f.views = 3;
        for (unsigned p = 0; p < phase_count; ++p) {
            f.phase_us[p] = (i + 1) * (p + 1);
            f.dt_us += f.phase_us[p];
        }
        f.view_setup_us = i;
        f.view_submit_us = 2 * i;
        reduction.add(f);
    }
    check(reduction.full() && reduction.close(summary));
    refuse_allocation = false;
    check(summary.frames == 300 && summary.frame == 299 && summary.incomplete == 100);
    check(summary.phase_p50[0] == 151 && summary.phase_p95[0] == 286 && summary.phase_p50[8] == 151 * 9);
    check(summary.dt_p50 == 151 * 45 && summary.dt_p95 == 286 * 45);
    check(summary.view_setup_p50 == 150 && summary.view_submit_p95 == 570 && summary.views_p50 == 3);
    check(summary.slow_frames_count == 4 && summary.slow_frames[0].frame == 299 && summary.slow_frames[3].frame == 296);
    check(reduction.count() == 0 && !reduction.close(summary));
    // Partial window and over-long window.
    for (unsigned i = 0; i < 5; ++i) {
        Sample f{};
        f.frame = i;
        f.dt_us = (i * 7) % 5 + 1;
        f.complete = true;
        reduction.add(f);
    }
    check(reduction.close(summary) && summary.frames == 5 && summary.incomplete == 0 && summary.dt_p95 == 5);
    refuse_allocation = true;
    for (unsigned i = 0; i < window_frames + 40; ++i) {
        Sample f{};
        f.frame = i;
        f.dt_us = 9;
        reduction.add(f);
    }
    refuse_allocation = false;
    check(reduction.count() == window_frames && reduction.close(summary) && summary.frames == 300 &&
          summary.frame == 339);
    std::printf("frame_phases_host checks=%u failures=0 tracker_bytes=%zu window_bytes=%zu\n", checks, sizeof(Tracker),
                sizeof(Window));
    return 0;
}
