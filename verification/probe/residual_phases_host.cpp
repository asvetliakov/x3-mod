// Host checks of the residual-phase accumulator, owner gate and window
// reduction in src/proxy/residual_phases_core.h: scripted materials against
// retained pass clocks (prepare and setup pairing, the frame's first material,
// a skipped pass loop, the pending setup closed at take), scripted views
// against a retained view_submit_end (pairing, dedupe), the clock-failure and
// backward-clock counters, take() with `other` and its underflow, the
// per-pass figures and the self-cost field, the gate, the 300-frame reduction
// and no allocation while sampling. No Win32, no game.
#include "../../src/proxy/residual_phases_core.h"
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

using namespace x3m::residual_phases::detail;
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
// The pass group's retained clocks, as the pass accumulator would keep them.
static std::uint64_t end_clock = 0, begin_clock = 0;
static bool begin_armed = false;
static std::uint64_t clock_ticks = 1000; // one tick per microsecond
static void at(std::uint64_t advance) {
    clock_ticks += advance;
}
// One scripted material: the Begin dispatch stamp, then (unless skipped) the
// first pass_begin after `setup` and the last pass_end after `passes` more.
static void material(std::uint64_t setup, std::uint64_t passes, bool skip = false) {
    accumulator.material(clock_ticks, end_clock, begin_clock, begin_armed, clock_ticks, end_clock, begin_clock, true);
    if (skip) return;
    at(setup);
    if (begin_armed) {
        begin_armed = false;
        begin_clock = clock_ticks;
    }
    at(passes);
    end_clock = clock_ticks;
}

int main() {
    Sample s;
    refuse_allocation = true;
    // Three materials: the first has no pass_end to pair with; each later one
    // closes the previous setup and pairs prepare with the last pass_end.
    material(10, 100);
    at(5);
    material(20, 100);
    at(7);
    material(30, 100);
    check(accumulator.materials == 3 && accumulator.prepare_skipped == 1 && accumulator.setup_skipped == 0);
    check(accumulator.ticks[0] == 12 && accumulator.ticks[1] == 30 && begin_armed == false);
    // A skipped pass loop: its setup never closes, the next prepare has no fresh pass_end.
    at(3);
    material(0, 0, true);
    at(4);
    material(40, 100);
    check(accumulator.materials == 5 && accumulator.setup_skipped == 1 && accumulator.prepare_skipped == 2);
    check(accumulator.ticks[0] == 12 + 3 && accumulator.ticks[1] == 60);
    // Views: pairs once with a retained view_submit_end, a repeat is skipped, a
    // new submit_end pairs again; a backward clock is counted, not accumulated.
    accumulator.view(2000, 1990);
    accumulator.view(2005, 1990);
    accumulator.view(2100, 2050);
    accumulator.view(2110, 2120);
    check(accumulator.particle_views == 4 && accumulator.ticks[2] == 60 && accumulator.view_skipped == 1 &&
          accumulator.clock_errors == 1);
    accumulator.view(2200, 0);
    check(accumulator.view_skipped == 2);
    // Clock failures: counting only; the material after a failure pairs nothing.
    accumulator.view(0, 2300);
    accumulator.material(0, end_clock, begin_clock, begin_armed, 0, end_clock, begin_clock, true);
    check(accumulator.clock_failures == 2 && accumulator.broken && !begin_armed);
    at(9);
    material(50, 100);
    check(!accumulator.broken && accumulator.prepare_skipped == 3 && accumulator.setup_skipped == 2 &&
          accumulator.ticks[0] == 15 && accumulator.ticks[1] == 60);
    // take: the pending setup closes, `other` is views minus setup, submit and particles.
    accumulator.take(7, 1000000, 1000, 100, 500, 3, 4, begin_clock, begin_armed, s, begin_clock);
    check(s.frame == 7 && s.materials == 7 && s.particle_views == 6 && s.passes == 4 && s.views == 3);
    check(s.interval_us[0] == 15 && s.interval_us[1] == 110 && s.interval_us[2] == 60 && s.interval_us[3] == 340);
    check(s.prepare_per_pass_ns == 3750 && s.setup_per_pass_ns == 27500);
    check(s.views_us == 1000 && s.view_setup_us == 100 && s.view_submit_us == 500);
    check(s.self_us == 13 * dispatch_cost_ns / 1000 && accumulator.other_underflow == 0);
    check(accumulator.materials == 0 && accumulator.particle_views == 0 && accumulator.p_clock == 0 &&
          accumulator.submit_end_seen == 0 && !begin_armed);
    // Underflow: views smaller than the attributed sum reports other as 0 and counts it.
    accumulator.view(3000, 2900);
    accumulator.take(8, 1000000, 50, 10, 20, 1, 0, begin_clock, begin_armed, s, begin_clock);
    check(s.interval_us[2] == 100 && s.interval_us[3] == 0 && accumulator.other_underflow == 1 &&
          s.prepare_per_pass_ns == 0);
    // Zero frequency: intervals are zero, the counts survive.
    material(1, 1);
    accumulator.take(9, 0, 5, 1, 1, 1, 1, begin_clock, begin_armed, s, begin_clock);
    check(s.materials == 1 && s.interval_us[0] == 0 && s.interval_us[3] == 3);
    // Unmatched is the caller's (the handler counts an index outside the table).
    ++accumulator.unmatched;
    check(accumulator.unmatched == 1);

    // Gate: before admission every stamp is early; a second thread is foreign.
    check(!gate.owned(11) && gate.early.load() == 1 && gate.admit(11) && gate.owned(11));
    check(!gate.owned(22) && gate.foreign.load() == 1 && gate.owner.load() == 11);
    gate.reset();
    check(gate.owner.load() == 0 && gate.early.load() == 0 && gate.foreign.load() == 0);

    // Window: ascending values, p50 and p95 by nearest rank.
    Summary summary;
    check(!reduction.close(summary));
    for (unsigned i = 0; i < window_frames; ++i) {
        Sample f{};
        f.frame = i;
        f.materials = 1000 + i;
        f.particle_views = 3;
        f.passes = 900 + i;
        f.views = 9;
        for (unsigned p = 0; p < interval_count; ++p) f.interval_us[p] = (i + 1) * (p + 1);
        f.prepare_per_pass_ns = 2 * i;
        f.setup_per_pass_ns = 3 * i;
        f.self_us = i;
        reduction.add(f);
    }
    check(reduction.full() && reduction.close(summary));
    check(summary.frames == 300 && summary.frame == 299 && summary.materials_p50 == 1150 &&
          summary.passes_p50 == 1050 && summary.particle_views_p50 == 3 && summary.views_p50 == 9);
    check(summary.interval_p50[0] == 151 && summary.interval_p95[0] == 286 && summary.interval_p50[3] == 604 &&
          summary.interval_p95[3] == 1144);
    check(summary.prepare_per_pass_p50 == 300 && summary.setup_per_pass_p50 == 450 && summary.self_p50 == 150);
    check(reduction.count() == 0 && !reduction.close(summary));
    // Partial window and over-long window.
    for (unsigned i = 0; i < 5; ++i) {
        Sample f{};
        f.frame = i;
        f.interval_us[2] = (i * 7) % 5 + 1;
        reduction.add(f);
    }
    check(reduction.close(summary) && summary.frames == 5 && summary.interval_p50[2] == 3 && summary.frame == 4);
    for (unsigned i = 0; i < window_frames + 40; ++i) {
        Sample f{};
        f.frame = i;
        f.interval_us[1] = 9;
        reduction.add(f);
    }
    check(reduction.count() == window_frames && reduction.close(summary) && summary.frames == 300 &&
          summary.frame == 339 && summary.interval_p50[1] == 9);
    refuse_allocation = false;
    std::printf(
        "residual_phases_host checks=%u failures=0 accumulator_bytes=%zu window_bytes=%zu dispatch_cost_ns=%llu\n",
        checks, sizeof(Accumulator), sizeof(Window), static_cast<unsigned long long>(dispatch_cost_ns));
    return 0;
}
