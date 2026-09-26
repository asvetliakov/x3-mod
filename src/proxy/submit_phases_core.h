#pragma once
#include <cstdint>
#include "stamp_core.h"

// Value-only state of the submit-phase diagnostic (X3M_SUBMIT_PHASES=1): nine
// open/close stamp pairs on the view_submit candidates of
// docs/reverse-engineering/view-submit-hot-path.md, the owner gate and the
// 300-frame window reduction. No Win32, no allocation, no locking.
// Host-tested by verification/probe/submit_phases_host.cpp.
//
// Every interval is one open clock: none of the nine bracketed regions is
// re-entered on the owner thread while it is open (0x0047d9c0 recurses, but
// the walk pair closes before any call; the other routines are not recursive).
// An open stamp on an already-open interval abandons the older clock
// (`reopened`); a close with nothing open is `idle` (expected: the eleven
// early-out edges onto the End return site, the walk join after a miss
// already closed the pair, and the material caller's skip edge).
namespace x3m::submit_phases::detail {
inline constexpr unsigned site_count = 22, interval_count = 9;
inline constexpr unsigned window_frames = 300;
enum Interval : unsigned { Sort = 0, Walk, Technique, End, Block, InverseWorld, InverseView, Material, World };
inline constexpr const char* const interval_names[interval_count] = {
    "sort", "walk", "technique", "end", "block", "inverse_world", "inverse_view", "material", "world"};
// What each site of submit_phase_sites.h does, in table order.
enum Action : unsigned char { Open, Close };
struct Role {
    unsigned char interval;
    Action action;
};
// clang-format off
inline constexpr Role roles[site_count] = {
    {Sort, Open}, {Sort, Close}, {Sort, Close}, {Sort, Close},
    {Walk, Open}, {Walk, Close}, {Walk, Close},
    {Technique, Open}, {Technique, Close},
    {End, Open}, {End, Close},
    {Block, Open}, {Block, Close},
    {InverseWorld, Open}, {InverseWorld, Close}, {InverseView, Open}, {InverseView, Close},
    {Material, Open}, {Material, Close},
    {World, Open}, {World, Close}, {World, Close},
};
// clang-format on
inline constexpr unsigned sort_enter_site = 0, walk_begin_site = 4, walk_miss_site = 5, walk_join_site = 6,
                          end_begin_site = 9;
// One walk lookup in `walk_sample_period` has its list position counted by the
// handler (the count repeats the engine's pointer chase, so it is sampled);
// `walk_iterations` is the sampled sum scaled by the period.
inline constexpr unsigned walk_sample_period = 16;
// Cost of one context-stub dispatch (lean_stub.h emit_context: envelope, owner
// check, one QueryPerformanceCounter, the pairing) measured by the CPU fixture
// under the X3 bottle (`SUBMIT PHASE BENCH dispatch_ns`); the fixture refuses a
// constant more than 2x off. Keep in step with the ledger in
// docs/verification/sampling-profiler.md ("Submit phases").
inline constexpr std::uint64_t dispatch_cost_ns = 102;
using Gate = x3m::stamp::Gate;

struct Sample {
    std::uint64_t frame = 0;
    std::uint32_t stamps = 0;              // every dispatch of the frame
    std::uint32_t calls[interval_count]{}; // closed pairs
    std::uint64_t interval_us[interval_count]{};
    std::uint64_t material_net_us = 0, block_net_us = 0; // less the dispatches nested inside the pair
    std::uint32_t sort_nodes = 0, sort_nodes_max = 0;    // queue length summed over the frame's sorts / the longest
    std::uint32_t walk_misses = 0;
    std::uint64_t walk_iterations = 0; // sampled iterations x walk_sample_period
    std::uint64_t self_us = 0;         // stamps * dispatch_cost_ns / 1000
};

// Per-frame accumulator, owner thread only. stamp() is the whole per-dispatch
// work after the owner check; take() converts and resets once per frame.
struct Accumulator {
    std::uint64_t open_clock[interval_count]{}, ticks[interval_count]{};
    std::uint32_t calls[interval_count]{};
    std::uint32_t stamps = 0, material_inner = 0, block_inner = 0;
    std::uint32_t sort_nodes = 0, sort_nodes_max = 0, walk_lookups = 0, walk_misses = 0, walk_sampled_iterations = 0;
    // Window counters, reset by the reporter.
    std::uint64_t reopened = 0, idle = 0, clock_errors = 0, clock_failures = 0, unmatched = 0, block_skipped = 0;
    // Whether the walk lookup that just opened is the sampled one.
    bool walk_sampled() const noexcept { return walk_lookups % walk_sample_period == 0; }
    void close(unsigned interval, std::uint64_t now) noexcept {
        const std::uint64_t opened = open_clock[interval];
        if (!opened) {
            ++idle;
            return;
        }
        open_clock[interval] = 0;
        if (now >= opened) {
            ticks[interval] += now - opened;
            ++calls[interval];
        } else
            ++clock_errors;
    }
    void stamp(unsigned index, std::uint64_t now) noexcept {
        if (index >= site_count) {
            ++unmatched;
            return;
        }
        ++stamps;
        const Role role = roles[index];
        // Dispatches nested inside the two enclosing pairs (their own open and
        // close stamps are outside the measured interval's clock reads).
        if (open_clock[Material] && role.interval != Material) ++material_inner;
        if (open_clock[Block] && role.interval != Block && index != end_begin_site) ++block_inner;
        if (!now) {
            ++clock_failures;
            open_clock[role.interval] = 0;
            return;
        }
        if (index == end_begin_site && open_clock[Block]) {
            close(Block, now);
            ++block_skipped;
        } // the geometry guard skipped the pass-loop guard
        if (role.action == Open) {
            if (open_clock[role.interval]) ++reopened;
            open_clock[role.interval] = now;
            if (index == walk_begin_site) ++walk_lookups;
        } else {
            if (index == walk_miss_site && open_clock[Walk]) ++walk_misses;
            close(role.interval, now);
        }
    }
    void sorted(std::uint32_t nodes) noexcept {
        sort_nodes += nodes;
        if (nodes > sort_nodes_max) sort_nodes_max = nodes;
    }
    void walked(std::uint32_t iterations) noexcept { walk_sampled_iterations += iterations; }
    void discard() noexcept {
        for (unsigned i = 0; i < interval_count; ++i) {
            open_clock[i] = 0;
            ticks[i] = 0;
            calls[i] = 0;
        }
        stamps = material_inner = block_inner = 0;
        sort_nodes = sort_nodes_max = walk_misses = walk_sampled_iterations = 0;
        // walk_lookups keeps running: it is the sampling phase, not a frame count.
    }
    void take(std::uint64_t frame, std::uint64_t frequency, Sample& out) noexcept {
        out = Sample{};
        out.frame = frame;
        out.stamps = stamps;
        for (unsigned i = 0; i < interval_count; ++i) {
            out.calls[i] = calls[i];
            out.interval_us[i] = frequency ? ticks[i] * 1000000ull / frequency : 0;
        }
        const std::uint64_t material_self = std::uint64_t(material_inner) * dispatch_cost_ns / 1000;
        const std::uint64_t block_self = std::uint64_t(block_inner) * dispatch_cost_ns / 1000;
        out.material_net_us = out.interval_us[Material] > material_self ? out.interval_us[Material] - material_self : 0;
        out.block_net_us = out.interval_us[Block] > block_self ? out.interval_us[Block] - block_self : 0;
        out.sort_nodes = sort_nodes;
        out.sort_nodes_max = sort_nodes_max;
        out.walk_misses = walk_misses;
        out.walk_iterations = std::uint64_t(walk_sampled_iterations) * walk_sample_period;
        out.self_us = std::uint64_t(stamps) * dispatch_cost_ns / 1000;
        discard();
    }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0;
    std::uint64_t stamps_p50 = 0, stamps_p95 = 0, self_p50 = 0;
    std::uint64_t calls_p50[interval_count]{}, interval_p50[interval_count]{}, interval_p95[interval_count]{};
    std::uint64_t material_net_p50 = 0, block_net_p50 = 0;
    std::uint64_t sort_nodes_p50 = 0, sort_nodes_max = 0, walk_misses_p50 = 0, walk_iterations_p50 = 0,
                  walk_iterations_p95 = 0;
};

// Fixed 300-sample window: `add` is a few stores; `close` runs one
// std::nth_element per statistic, once per window.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }
    void add(const Sample& s) noexcept {
        if (count_ < window_frames) {
            for (unsigned i = 0; i < interval_count; ++i) {
                calls_[i][count_] = s.calls[i];
                interval_[i][count_] = s.interval_us[i];
            }
            stamps_[count_] = s.stamps;
            self_[count_] = s.self_us;
            net_[0][count_] = s.material_net_us;
            net_[1][count_] = s.block_net_us;
            sort_nodes_[count_] = s.sort_nodes;
            walk_misses_[count_] = s.walk_misses;
            walk_iterations_[count_] = s.walk_iterations;
            if (s.sort_nodes_max > sort_nodes_max_) sort_nodes_max_ = s.sort_nodes_max;
            ++count_;
        }
        last_frame_ = s.frame;
    }
    bool close(Summary& out) noexcept {
        if (!count_) {
            reset();
            return false;
        }
        out = Summary{};
        out.frame = last_frame_;
        out.frames = count_;
        out.stamps_p50 = percentile(stamps_, 50);
        out.stamps_p95 = percentile(stamps_, 95);
        out.self_p50 = percentile(self_, 50);
        for (unsigned i = 0; i < interval_count; ++i) {
            out.calls_p50[i] = percentile(calls_[i], 50);
            out.interval_p50[i] = percentile(interval_[i], 50);
            out.interval_p95[i] = percentile(interval_[i], 95);
        }
        out.material_net_p50 = percentile(net_[0], 50);
        out.block_net_p50 = percentile(net_[1], 50);
        out.sort_nodes_p50 = percentile(sort_nodes_, 50);
        out.sort_nodes_max = sort_nodes_max_;
        out.walk_misses_p50 = percentile(walk_misses_, 50);
        out.walk_iterations_p50 = percentile(walk_iterations_, 50);
        out.walk_iterations_p95 = percentile(walk_iterations_, 95);
        reset();
        return true;
    }
    void reset() noexcept {
        count_ = 0;
        last_frame_ = 0;
        sort_nodes_max_ = 0;
    }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        return x3m::stamp::percentile(values, count_, p, scratch_);
    }
    std::uint64_t calls_[interval_count][window_frames]{}, interval_[interval_count][window_frames]{};
    std::uint64_t stamps_[window_frames]{}, self_[window_frames]{}, net_[2][window_frames]{};
    std::uint64_t sort_nodes_[window_frames]{}, walk_misses_[window_frames]{}, walk_iterations_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    std::uint32_t sort_nodes_max_ = 0;
    unsigned count_ = 0;
    std::uint64_t last_frame_ = 0;
};
}
