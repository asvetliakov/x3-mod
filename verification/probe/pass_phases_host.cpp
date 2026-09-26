// Host checks of the pass-phase accumulator, owner gate and window reduction
// in src/proxy/pass_phases_core.h: scripted stamps for apply/draw/end
// accumulation and the pass count, the orphan/clock/failure/unmatched
// counters, take() conversion with the joined view_submit and the self-cost
// field, early/foreign stamps counted and ignored by the gate, the 300-frame
// reduction (p50/p95, partial and over-long windows) and no allocation while
// sampling. No Win32, no game.
#include "../../src/proxy/pass_phases_core.h"
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

using namespace x3m::pass_phases::detail;
static unsigned checks = 0;
static void check(bool value) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "check %u failed\n", checks);
        std::exit(1);
    }
}

static Accumulator accumulator; // static storage like the production objects
static Gate gate;
static Window reduction;
static std::uint64_t clock_ticks = 1000; // one tick per microsecond
static void at(std::uint64_t advance) {
    clock_ticks += advance;
}
// One scripted pass: begin, then applied after `apply`, drawn after `draw`, end after `end` microseconds.
static void pass(std::uint64_t apply, std::uint64_t draw, std::uint64_t end) {
    accumulator.stamp(0, clock_ticks);
    at(apply);
    accumulator.stamp(1, clock_ticks);
    at(draw);
    accumulator.stamp(2, clock_ticks);
    at(end);
    accumulator.stamp(3, clock_ticks);
}

int main() {
    Sample s;
    refuse_allocation = true;
    // Three passes: the intervals accumulate, the loop tail between passes does not.
    pass(10, 20, 30);
    at(100);
    pass(1, 2, 3);
    at(100);
    pass(5, 5, 5);
    check(accumulator.passes == 3 && accumulator.last == 0);
    check(accumulator.ticks[0] == 16 && accumulator.ticks[1] == 27 && accumulator.ticks[2] == 38);
    check(accumulator.orphans == 0 && accumulator.clock_errors == 0 && accumulator.clock_failures == 0 &&
          accumulator.unmatched == 0);
    accumulator.take(7, 1000000, 4321, s);
    check(s.frame == 7 && s.passes == 3 && s.view_submit_us == 4321);
    check(s.interval_us[0] == 16 && s.interval_us[1] == 27 && s.interval_us[2] == 38 && s.sum_us == 81);
    check(s.self_us == 3 * site_count * dispatch_cost_ns / 1000);
    check(accumulator.passes == 0 && accumulator.ticks[0] == 0 && accumulator.ticks[1] == 0 &&
          accumulator.ticks[2] == 0 && accumulator.last == 0);
    // Frequency scaling: 10 MHz ticks (100 ns) convert to microseconds.
    pass(100, 200, 300);
    accumulator.take(8, 10000000, 0, s);
    check(s.interval_us[0] == 10 && s.interval_us[1] == 20 && s.interval_us[2] == 30 && s.sum_us == 60);
    // Entering mid-pass: the closing stamp is an orphan, later intervals still count.
    at(5);
    accumulator.stamp(1, clock_ticks);
    at(7);
    accumulator.stamp(2, clock_ticks);
    at(9);
    accumulator.stamp(3, clock_ticks);
    check(accumulator.orphans == 1 && accumulator.passes == 1 && accumulator.ticks[0] == 0 &&
          accumulator.ticks[1] == 7 && accumulator.ticks[2] == 9);
    // A backward clock is a clock error: the interval is skipped, the chain continues from the new stamp.
    accumulator.stamp(0, clock_ticks);
    accumulator.stamp(1, clock_ticks - 1);
    at(4);
    accumulator.stamp(2, clock_ticks);
    at(1);
    accumulator.stamp(3, clock_ticks);
    check(accumulator.clock_errors == 1 && accumulator.passes == 2 && accumulator.ticks[0] == 0 &&
          accumulator.ticks[1] == 7 + 5 && accumulator.ticks[2] == 10);
    // A failed clock (0) degrades to counting: the chain resets, the pass still counts on pass_end.
    accumulator.stamp(0, clock_ticks);
    accumulator.stamp(1, 0);
    at(3);
    accumulator.stamp(2, clock_ticks);
    at(3);
    accumulator.stamp(3, 0);
    check(accumulator.clock_failures == 2 && accumulator.orphans == 2 && accumulator.passes == 3 &&
          accumulator.ticks[2] == 10);
    // An index outside the table is unmatched and changes nothing.
    accumulator.stamp(4, clock_ticks);
    accumulator.stamp(99, clock_ticks);
    check(accumulator.unmatched == 2 && accumulator.passes == 3);
    // A pass_begin over a pass whose end never arrived is an orphan and moves the open interval.
    accumulator.stamp(0, clock_ticks);
    at(50);
    accumulator.stamp(0, clock_ticks);
    at(2);
    accumulator.stamp(1, clock_ticks);
    check(accumulator.ticks[0] == 2 && accumulator.orphans == 3);
    accumulator.discard();
    check(accumulator.passes == 0 && accumulator.last == 0 && accumulator.ticks[0] == 0);
    // Zero frequency: intervals are zero, the count and the self estimate survive.
    pass(1, 1, 1);
    accumulator.take(9, 0, 5, s);
    check(s.passes == 1 && s.sum_us == 0 && s.view_submit_us == 5 && s.self_us == site_count * dispatch_cost_ns / 1000);

    // Gate: before admission every stamp is early; a second thread is foreign;
    // admission is first come, and re-admission by the owner is idempotent.
    check(!gate.owned(11) && !gate.owned(11) && gate.early.load() == 2 && gate.foreign.load() == 0);
    check(gate.admit(11) && gate.owned(11) && gate.early.load() == 2);
    check(!gate.owned(22) && gate.foreign.load() == 1 && !gate.admit(22) && gate.owner.load() == 11);
    check(gate.admit(11) && gate.foreign.load() == 1);
    gate.reset();
    check(gate.owner.load() == 0 && gate.early.load() == 0 && gate.foreign.load() == 0);

    // Window: ascending values, p50 and p95 by nearest rank.
    Summary summary;
    check(!reduction.close(summary));
    for (unsigned i = 0; i < window_frames; ++i) {
        Sample f{};
        f.frame = i;
        f.passes = 1000 + i;
        for (unsigned p = 0; p < interval_count; ++p) {
            f.interval_us[p] = (i + 1) * (p + 1);
            f.sum_us += f.interval_us[p];
        }
        f.view_submit_us = 10 * i;
        f.self_us = f.passes * site_count * dispatch_cost_ns / 1000;
        reduction.add(f);
    }
    check(reduction.full() && reduction.close(summary));
    check(summary.frames == 300 && summary.frame == 299 && summary.passes_p50 == 1150);
    check(summary.interval_p50[0] == 151 && summary.interval_p95[0] == 286 && summary.interval_p50[2] == 453 &&
          summary.interval_p95[2] == 858);
    check(summary.sum_p50 == 151 * 6 && summary.view_submit_p50 == 1500 &&
          summary.self_p50 == 1150 * site_count * dispatch_cost_ns / 1000);
    check(reduction.count() == 0 && !reduction.close(summary));
    // Partial window and over-long window.
    for (unsigned i = 0; i < 5; ++i) {
        Sample f{};
        f.frame = i;
        f.sum_us = (i * 7) % 5 + 1;
        reduction.add(f);
    }
    check(reduction.close(summary) && summary.frames == 5 && summary.sum_p50 == 3 && summary.frame == 4);
    for (unsigned i = 0; i < window_frames + 40; ++i) {
        Sample f{};
        f.frame = i;
        f.sum_us = 9;
        reduction.add(f);
    }
    check(reduction.count() == window_frames && reduction.close(summary) && summary.frames == 300 &&
          summary.frame == 339 && summary.sum_p50 == 9);
    refuse_allocation = false;
    std::printf("pass_phases_host checks=%u failures=0 accumulator_bytes=%zu window_bytes=%zu dispatch_cost_ns=%llu\n",
                checks, sizeof(Accumulator), sizeof(Window), static_cast<unsigned long long>(dispatch_cost_ns));
    return 0;
}
