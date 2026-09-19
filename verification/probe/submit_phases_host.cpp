// Host checks of the submit-phase accumulator and window reduction in
// src/proxy/submit_phases_core.h: the role table against the site order, open
// and close pairing on every interval, idle closes, a reopened interval, the
// geometry-guard skip closing the block at the End dispatch, the nested
// dispatch counts behind the net figures, walk sampling, miss and queue
// counters, the clock-failure and backward-clock counters, take() and its
// reset, the 300-frame reduction and no allocation while sampling. No Win32,
// no game.
#include "../../src/proxy/submit_phases_core.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) { std::fprintf(stderr, "allocated\n"); std::abort(); } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace x3m::submit_phases::detail;
static unsigned checks = 0;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "check %u failed\n", checks); std::exit(1); } }

static Accumulator accumulator; // static storage like the production objects
static Window reduction;
// Site indices in submit_phase_sites.h order.
enum : unsigned { SortEnter, SortA, SortB, SortC, WalkBegin, WalkMiss, WalkJoin, TechBegin, TechEnd, EndBegin, EndEnd,
                  BlockBegin, BlockEnd, IwBegin, IwEnd, IvBegin, IvEnd, MatEnter, MatReturn, WorldEnter, WorldA, WorldB };

int main() {
    // The role table: opens and closes in site order, the special sites where the handler expects them.
    unsigned opens = 0, per_interval[interval_count]{};
    for (unsigned i = 0; i < site_count; ++i) { opens += roles[i].action == Open; check(roles[i].interval < interval_count); ++per_interval[roles[i].interval]; }
    check(opens == interval_count && WorldB + 1 == site_count);
    check(per_interval[Sort] == 4 && per_interval[Walk] == 3 && per_interval[World] == 3 && per_interval[Material] == 2);
    check(sort_enter_site == SortEnter && walk_begin_site == WalkBegin && walk_miss_site == WalkMiss && walk_join_site == WalkJoin && end_begin_site == EndBegin);
    check(roles[MatEnter].interval == Material && roles[MatEnter].action == Open && roles[EndBegin].interval == End && roles[EndBegin].action == Open);

    refuse_allocation = true;
    std::uint64_t t = 1000; // one tick per microsecond at frequency 1e6
    auto& a = accumulator;
    // One draw: material { technique 5, block { inverse_world 3, inverse_view 2, +10 }, end 7 }.
    a.stamp(MatEnter, t);
    a.stamp(TechBegin, t += 1); a.stamp(TechEnd, t += 5);
    a.stamp(BlockBegin, t += 1);
    a.stamp(IwBegin, t += 1); a.stamp(IwEnd, t += 3);
    a.stamp(IvBegin, t += 1); a.stamp(IvEnd, t += 2);
    a.stamp(BlockEnd, t += 10);
    a.stamp(EndBegin, t += 20); a.stamp(EndEnd, t += 7);
    a.stamp(MatReturn, t += 1);
    check(a.ticks[Technique] == 5 && a.ticks[InverseWorld] == 3 && a.ticks[InverseView] == 2 && a.ticks[Block] == 17 && a.ticks[End] == 7 && a.ticks[Material] == 52);
    check(a.stamps == 12 && a.material_inner == 10 && a.block_inner == 4 && a.idle == 0 && a.reopened == 0 && a.block_skipped == 0);
    for (unsigned i = 0; i < interval_count; ++i) check(a.open_clock[i] == 0);
    // The geometry guard skips block_end: the End dispatch closes the block and is not counted inside it.
    a.stamp(BlockBegin, t += 1); a.stamp(EndBegin, t += 9); a.stamp(EndEnd, t += 2);
    check(a.ticks[Block] == 26 && a.calls[Block] == 2 && a.block_skipped == 1 && a.block_inner == 4 && a.calls[End] == 2);
    // An early-out edge onto the End return site, and the material caller's skip edge: idle.
    a.stamp(EndEnd, t += 1); a.stamp(MatReturn, t += 1);
    check(a.idle == 2 && a.calls[End] == 2 && a.calls[Material] == 1);
    // Sort: one entry, the first return closes, the queue length is recorded.
    a.sorted(40); a.stamp(SortEnter, t += 1); a.stamp(SortB, t += 30);
    a.sorted(7); a.stamp(SortEnter, t += 1); a.stamp(SortC, t += 4);
    check(a.calls[Sort] == 2 && a.ticks[Sort] == 34 && a.sort_nodes == 47 && a.sort_nodes_max == 40);
    // World matrix through both callers.
    a.stamp(WorldEnter, t += 1); a.stamp(WorldA, t += 2); a.stamp(WorldEnter, t += 1); a.stamp(WorldB, t += 3);
    check(a.calls[World] == 2 && a.ticks[World] == 5);
    // Walk: a hit closes at the join; a miss closes at the miss site and its join is idle.
    a.stamp(WalkBegin, t += 1); check(!a.walk_sampled()); a.stamp(WalkJoin, t += 6);
    a.stamp(WalkBegin, t += 1); a.stamp(WalkMiss, t += 8); a.stamp(WalkJoin, t += 50);
    check(a.calls[Walk] == 2 && a.ticks[Walk] == 14 && a.walk_misses == 1 && a.idle == 3 && a.walk_lookups == 2);
    // The sixteenth lookup is the sampled one; its count is scaled at take.
    for (unsigned i = 0; i < 13; ++i) { a.stamp(WalkBegin, t += 1); check(!a.walk_sampled()); a.stamp(WalkJoin, t += 1); }
    a.stamp(WalkBegin, t += 1); check(a.walk_sampled()); a.stamp(WalkJoin, t += 1); a.walked(450);
    check(a.walk_lookups == 16 && a.walk_sampled_iterations == 450);
    // Reopened, backward clock, clock failure, unmatched index.
    a.stamp(TechBegin, t += 1); a.stamp(TechBegin, t += 1); check(a.reopened == 1);
    a.stamp(TechEnd, t - 5); check(a.clock_errors == 1 && a.calls[Technique] == 1 && a.open_clock[Technique] == 0);
    a.stamp(WorldEnter, t += 1); a.stamp(WorldA, 0); check(a.clock_failures == 1 && a.open_clock[World] == 0 && a.calls[World] == 2);
    a.stamp(site_count, t); check(a.unmatched == 1);
    const std::uint32_t stamps = a.stamps;
    Sample s;
    a.take(9, 1000000, s);
    check(s.frame == 9 && s.stamps == stamps && s.self_us == std::uint64_t(stamps) * dispatch_cost_ns / 1000);
    check(s.interval_us[Material] == 52 && s.interval_us[Block] == 26 && s.interval_us[Sort] == 34 && s.calls[Walk] == 16 && s.calls[Sort] == 2);
    check(s.material_net_us == 52 - 10 * dispatch_cost_ns / 1000 && s.block_net_us == 26 - 4 * dispatch_cost_ns / 1000);
    check(s.sort_nodes == 47 && s.sort_nodes_max == 40 && s.walk_misses == 1 && s.walk_iterations == 450 * walk_sample_period);
    check(a.stamps == 0 && a.ticks[Material] == 0 && a.calls[Walk] == 0 && a.sort_nodes == 0 && a.walk_lookups == 16 && a.material_inner == 0);
    // An interval left open at the boundary is discarded, not carried.
    a.stamp(MatEnter, t += 1); a.discard(); a.stamp(MatReturn, t += 1);
    check(a.calls[Material] == 0 && a.ticks[Material] == 0);
    a.take(10, 0, s); check(s.interval_us[Material] == 0); // no frequency: zero, never a divide
    // Net figures clamp at zero.
    a.stamp(MatEnter, t += 1); for (unsigned i = 0; i < 40; ++i) { a.stamp(TechBegin, t); a.stamp(TechEnd, t); } a.stamp(MatReturn, t += 1);
    a.take(11, 1000000, s); check(s.interval_us[Material] == 1 && s.material_net_us == 0);

    // Window: 300 samples, nearest-rank percentiles, the longest queue of the window.
    check(!reduction.full());
    for (unsigned i = 1; i <= window_frames; ++i) {
        Sample w; w.frame = i; w.stamps = i; w.self_us = i / 10; w.sort_nodes = i; w.sort_nodes_max = i == 17 ? 900 : 5;
        for (unsigned k = 0; k < interval_count; ++k) { w.calls[k] = k + 1; w.interval_us[k] = std::uint64_t(i) * (k + 1); }
        w.material_net_us = i * 2; w.block_net_us = i; w.walk_misses = 3; w.walk_iterations = std::uint64_t(i) * 16;
        reduction.add(w);
    }
    check(reduction.full());
    Summary m;
    check(reduction.close(m) && m.frames == window_frames && m.frame == window_frames);
    check(m.stamps_p50 == 151 && m.stamps_p95 == 286 && m.self_p50 == 15);
    check(m.interval_p50[Sort] == 151 && m.interval_p95[Sort] == 286 && m.interval_p50[World] == 151 * 9 && m.calls_p50[World] == 9);
    check(m.material_net_p50 == 302 && m.block_net_p50 == 151 && m.sort_nodes_p50 == 151 && m.sort_nodes_max == 900);
    check(m.walk_misses_p50 == 3 && m.walk_iterations_p50 == 151 * 16 && m.walk_iterations_p95 == 286 * 16);
    check(reduction.count() == 0 && !reduction.close(m));
    Sample w; w.sort_nodes_max = 2; reduction.add(w); check(reduction.close(m) && m.sort_nodes_max == 2 && m.frames == 1);
    refuse_allocation = false;
    std::printf("submit_phases_host checks=%u failures=0 accumulator_bytes=%zu window_bytes=%zu dispatch_cost_ns=%llu\n",
                checks, sizeof(Accumulator), sizeof(Window), static_cast<unsigned long long>(dispatch_cost_ns));
    return 0;
}
