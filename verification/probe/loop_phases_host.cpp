// Host checks of the loop-phase accumulator, owner gate and window reduction
// in src/proxy/loop_phases_core.h: scripted stamps for multi-sector frames
// with skipped containers (gate edges onto the pass-end sites with nothing
// open), the sector/container/dispatch counts, the collide/simulate/post/passb
// accumulation, the largest interval and its owner, the orphan (mid-chain
// entry and an open interval overwritten), clock-error, clock-failure and
// unmatched counters, take() conversion with the joined dt and input, the gate
// (early/foreign), the 300-frame reduction (p50/p95, window maximum, slow
// frames over 50 ms rate-limited to the first 64) and no allocation while
// sampling. No Win32, no game.
#include "../../src/proxy/loop_phases_core.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) { std::fprintf(stderr, "allocated\n"); std::abort(); } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace x3m::loop_phases::detail;
static unsigned checks = 0;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "check %u failed\n", checks); std::exit(1); } }

static Accumulator accumulator; // static storage like the production objects
static Gate gate;
static Window reduction;
static std::uint64_t clock_ticks = 1000; // one tick per microsecond
static void at(std::uint64_t advance) { clock_ticks += advance; }
static void stamp(unsigned index) { accumulator.stamp(index, clock_ticks); }
// One active container of pass A: collide, simulate, post, then the pass-A end.
static void sector_a(std::uint64_t collide, std::uint64_t simulate, std::uint64_t post) {
    stamp(0); at(collide); stamp(1); at(simulate); stamp(2); at(post); stamp(3);
}
// One active container of pass B: economy, then the pass-B end.
static void sector_b(std::uint64_t passb) { stamp(4); at(passb); stamp(5); }
// A container the gates skip: only the pass-end stamp fires (the gate edge lands on it).
static void skipped_a() { at(3); stamp(3); }
static void skipped_b() { at(3); stamp(5); }

int main() {
    Sample s;
    refuse_allocation = true;
    // Frame: node0 active, node1 skipped, node2 skipped, node3 active, in both passes.
    sector_a(10, 20, 30); skipped_a(); skipped_a(); sector_a(1, 2, 3);
    sector_b(40); skipped_b(); skipped_b(); sector_b(4);
    check(accumulator.sectors == 2 && accumulator.containers == 4 && accumulator.dispatches == 16 && accumulator.open == none && accumulator.last == 0);
    check(accumulator.ticks[0] == 11 && accumulator.ticks[1] == 22 && accumulator.ticks[2] == 33 && accumulator.ticks[3] == 44);
    check(accumulator.max_ticks == 40 && accumulator.max_owner == 3);
    check(accumulator.orphans == 0 && accumulator.clock_errors == 0 && accumulator.clock_failures == 0 && accumulator.unmatched == 0);
    accumulator.take(7, 1000000, 9000, 4321, s);
    check(s.frame == 7 && s.sectors == 2 && s.containers == 4 && s.dispatches == 16 && s.dt_us == 9000 && s.input_us == 4321);
    check(s.interval_us[0] == 11 && s.interval_us[1] == 22 && s.interval_us[2] == 33 && s.interval_us[3] == 44 && s.sum_us == 110);
    check(s.max_interval_us == 40 && s.max_owner == 3);
    check(s.self_us == 16 * dispatch_cost_ns / 1000);
    check(accumulator.sectors == 0 && accumulator.containers == 0 && accumulator.dispatches == 0 && accumulator.ticks[3] == 0 && accumulator.max_ticks == 0 && accumulator.max_owner == none);
    // Frequency scaling: 10 MHz ticks (100 ns) convert to microseconds; one pathological sector.
    sector_a(100, 5000, 300); sector_a(100, 200, 300); sector_b(400); sector_b(400);
    accumulator.take(8, 10000000, 0, 0, s);
    check(s.interval_us[0] == 20 && s.interval_us[1] == 520 && s.interval_us[2] == 60 && s.interval_us[3] == 80 && s.sum_us == 680);
    check(s.max_interval_us == 500 && s.max_owner == 1 && s.sectors == 2 && s.containers == 2);
    // Pass A gated off entirely: only pass B runs, sectors and containers still count.
    sector_b(5); skipped_b(); accumulator.take(9, 1000000, 0, 0, s);
    check(s.sectors == 1 && s.containers == 2 && s.dispatches == 3 && s.sum_us == 5 && s.interval_us[0] == 0 && s.max_owner == 3);
    // Entering mid-chain at simulate: collide's close has no open interval (one
    // orphan); simulate and post still accumulate, the pass-A end closes post.
    stamp(1); at(7); stamp(2); at(9); stamp(3);
    check(accumulator.orphans == 1 && accumulator.ticks[0] == 0 && accumulator.ticks[1] == 7 && accumulator.ticks[2] == 9 && accumulator.open == none);
    // An opening stamp over an interval still open is an orphan too (a lost end).
    stamp(0); at(5); stamp(0); at(2); stamp(1); at(1); stamp(2); at(1); stamp(3);
    check(accumulator.orphans == 2 && accumulator.ticks[0] == 2);
    // A pass-B end without economy open, after economy left open by a lost end.
    stamp(4); at(1); stamp(0);
    check(accumulator.orphans == 3 && accumulator.open == 0);
    at(1); stamp(1); at(1); stamp(2); at(1); stamp(3);
    check(accumulator.orphans == 3);
    // A backward clock is a clock error: the interval is skipped, the chain continues.
    stamp(0); accumulator.stamp(1, clock_ticks - 1); at(4); stamp(2); at(1); stamp(3);
    check(accumulator.clock_errors == 1 && accumulator.orphans == 3);
    // A failed clock (0) degrades to counting: the chain resets, the counts survive.
    stamp(4); accumulator.stamp(5, 0); accumulator.stamp(4, 0); at(3); stamp(5);
    check(accumulator.clock_failures == 2 && accumulator.sectors == 3 && accumulator.containers == 2 && accumulator.orphans == 3);
    // An index outside the table is unmatched and changes nothing.
    stamp(6); stamp(99);
    check(accumulator.unmatched == 2 && accumulator.dispatches == 21);
    accumulator.discard();
    check(accumulator.dispatches == 0 && accumulator.last == 0 && accumulator.open == none && accumulator.orphans == 3);
    // Zero frequency: intervals are zero, the counts and the self estimate survive.
    sector_a(1, 1, 1); sector_b(1); accumulator.take(10, 0, 5, 6, s);
    check(s.sectors == 1 && s.sum_us == 0 && s.max_interval_us == 0 && s.dt_us == 5 && s.input_us == 6 && s.self_us == 6 * dispatch_cost_ns / 1000);

    // Gate: before admission every stamp is early; a second thread is foreign;
    // admission is first come, and re-admission by the owner is idempotent.
    check(!gate.owned(11) && !gate.owned(11) && gate.early.load() == 2 && gate.foreign.load() == 0);
    check(gate.admit(11) && gate.owned(11) && gate.early.load() == 2);
    check(!gate.owned(22) && gate.foreign.load() == 1 && !gate.admit(22) && gate.owner.load() == 11);
    check(gate.admit(11) && gate.foreign.load() == 1);
    gate.reset(); check(gate.owner.load() == 0 && gate.early.load() == 0 && gate.foreign.load() == 0);

    // Window: ascending values, p50 and p95 by nearest rank, the window maximum
    // and its owner, slow frames (sum over 50 ms) counted and the first 64 kept.
    static Summary summary; // 64 witnesses: static like the production object
    check(!reduction.close(summary));
    for (unsigned i = 0; i < window_frames; ++i) {
        Sample f{};
        f.frame = i; f.sectors = 1 + i % 3; f.containers = 10 + i; f.dispatches = 6 * f.sectors;
        for (unsigned p = 0; p < interval_count; ++p) { f.interval_us[p] = (i + 1) * (p + 1); f.sum_us += f.interval_us[p]; }
        f.input_us = 10 * i; f.self_us = f.dispatches * dispatch_cost_ns / 1000; f.dt_us = f.sum_us + 100;
        f.max_interval_us = i == 150 ? 777 : 1; f.max_owner = i == 150 ? 2 : 0;
        reduction.add(f);
    }
    check(reduction.full() && reduction.close(summary));
    check(summary.frames == 300 && summary.frame == 299 && summary.sectors_p50 == 2 && summary.containers_p50 == 160);
    check(summary.interval_p50[0] == 151 && summary.interval_p95[0] == 286 && summary.interval_p50[3] == 604 && summary.interval_p95[3] == 1144);
    check(summary.sum_p50 == 151 * 10 && summary.input_p50 == 1500 && summary.self_p50 == 12 * dispatch_cost_ns / 1000);
    check(summary.max_interval_us == 777 && summary.max_owner == 2);
    check(summary.slow == 0 && summary.slow_count == 0);
    check(reduction.count() == 0 && !reduction.close(summary));
    // Slow frames: 100 of 300 frames exceed 50 ms; all are counted, the first 64 kept in order.
    for (unsigned i = 0; i < window_frames; ++i) {
        Sample f{}; f.frame = 1000 + i; f.sum_us = i % 3 == 0 ? slow_threshold_us + 1 + i : 9; f.interval_us[1] = f.sum_us; f.max_owner = 1;
        reduction.add(f);
    }
    check(reduction.close(summary) && summary.frames == 300 && summary.slow == 100 && summary.slow_count == slow_limit);
    check(summary.slow_frames[0].frame == 1000 && summary.slow_frames[0].sum_us == slow_threshold_us + 1 && summary.slow_frames[63].frame == 1000 + 63 * 3);
    check(summary.slow_frames[63].interval_us[1] == slow_threshold_us + 1 + 63 * 3 && summary.slow_frames[63].max_owner == 1);
    // Exactly the threshold is not slow; one over is.
    { Sample f{}; f.frame = 1; f.sum_us = slow_threshold_us; reduction.add(f); f.frame = 2; f.sum_us = slow_threshold_us + 1; reduction.add(f); }
    check(reduction.close(summary) && summary.frames == 2 && summary.slow == 1 && summary.slow_count == 1 && summary.slow_frames[0].frame == 2);
    // Partial window and over-long window.
    for (unsigned i = 0; i < 5; ++i) { Sample f{}; f.frame = i; f.sum_us = (i * 7) % 5 + 1; reduction.add(f); }
    check(reduction.close(summary) && summary.frames == 5 && summary.sum_p50 == 3 && summary.frame == 4 && summary.slow == 0);
    for (unsigned i = 0; i < window_frames + 40; ++i) { Sample f{}; f.frame = i; f.sum_us = 9; reduction.add(f); }
    check(reduction.count() == window_frames && reduction.close(summary) && summary.frames == 300 && summary.frame == 339 && summary.sum_p50 == 9);
    refuse_allocation = false;
    std::printf("loop_phases_host checks=%u failures=0 accumulator_bytes=%zu window_bytes=%zu summary_bytes=%zu dispatch_cost_ns=%llu slow_limit=%u\n",
                checks, sizeof(Accumulator), sizeof(Window), sizeof(Summary), static_cast<unsigned long long>(dispatch_cost_ns), slow_limit);
    return 0;
}
