#pragma once
#include <cstdint>
#include "stamp_core.h"

// Value-only state of the loop-phase diagnostic (X3M_LOOP_PHASES=1): the
// per-frame accumulate-only stamp logic for the per-sector update driver
// 0x0043a360, the owner gate and the 300-frame window reduction with its
// slow-frame witnesses. No Win32, no allocation, no locking. Host-tested by
// verification/probe/loop_phases_host.cpp.
//
// Ten stamps (loop_phase_sites.h) in two independent chains. The sector chain
// (sites 0-5): per active container of pass A, collide (site 0 -> 1),
// simulate (1 -> 2) and post (2 -> 3) accumulate; per active container of
// pass B, passb (4 -> 5) accumulates. Sites 3 and 5 also fire for every
// container the gates skip (the gate edges land on them), so with no interval
// open they are the container walk, not an error. The region chain (sites
// 6-9) brackets the three calls of the main loop's `input_part=0` region once
// per frame: cutevent (6 -> 7), containers (7 -> 8, the whole driver
// 0x0043a360, so it contains the sector chain) and sweep (8 -> 9); site 9 also
// fires on the pause edge with nothing open, which is not an error. Within a
// chain, a closing stamp whose expected interval is not open, or an opening
// stamp over an interval still open, is an orphan. The largest single
// interval of the frame and its owner are kept per chain, so one pathological
// sector and the region call that owns the frame are both visible.
namespace x3m::loop_phases::detail {
inline constexpr unsigned site_count = 10, interval_count = 7, none = interval_count;
// Intervals [0, sector_count) belong to the sector chain (sum_us), the rest to
// the region chain (region_us); sites from region_site on stamp the region chain.
inline constexpr unsigned sector_count = 4, region_site = 6;
inline constexpr unsigned window_frames = 300, slow_limit = 64;
inline constexpr std::uint64_t slow_threshold_us = 50000;
inline constexpr const char* const interval_names[interval_count + 1] = {"collide",  "simulate",   "post",  "passb",
                                                                         "cutevent", "containers", "sweep", "none"};
// Interval opened / closed by each site (none = no open / close).
inline constexpr unsigned opens[site_count] = {0, 1, 2, none, 3, none, 4, 5, 6, none};
inline constexpr unsigned closes[site_count] = {none, 0, 1, 2, none, 3, none, 4, 5, 6};
// The two pass ends also fire for every container the gates skip; the region
// end also fires on the pause edge (0x00403b10 -> 0x00403b3a).
inline constexpr bool walk[site_count] = {false, false, false, true, false, true, false, false, false, true};
// Cost of one lean-stub dispatch (the same stub as pass phases: stub envelope,
// owner check, one QueryPerformanceCounter, the accumulate) measured by the
// CPU fixture under the X3 bottle (verification/probe/run_game_phase_cpu.py,
// `LOOP PHASE BENCH dispatch_ns`); the fixture refuses a constant more than
// 2x off. `self_us` = dispatches * this. Keep in step with the ledger in
// docs/verification/sampling-profiler.md ("Loop phases").
inline constexpr std::uint64_t dispatch_cost_ns = 91;
using Gate = x3m::stamp::Gate;

struct Sample {
    std::uint64_t frame = 0;
    std::uint64_t dt_us = 0;      // the same frame's frame_phases dt (Present return to Present return)
    std::uint32_t sectors = 0;    // containers that passed the pass-B gate (site 4 hits)
    std::uint32_t containers = 0; // containers pass B walked (site 5 hits)
    std::uint32_t dispatches = 0; // every accepted stamp of the frame
    std::uint64_t interval_us[interval_count]{};
    std::uint64_t sum_us = 0;    // collide + simulate + post + passb
    std::uint64_t region_us = 0; // cutevent + containers + sweep: the whole input_part=0 region
    std::uint64_t input_us = 0;  // the same frame's pre_render, or the game-phase input phase when present
    std::uint64_t self_us = 0;   // dispatches * dispatch_cost_ns / 1000
    std::uint64_t max_interval_us = 0;
    unsigned max_owner = none; // interval index of the largest single sector-chain interval
    std::uint64_t region_max_us = 0;
    unsigned region_max_owner = none; // interval index of the largest region-chain interval
};
// Whether a frame is a slow-frame witness: either chain over the threshold.
inline bool slow_frame(const Sample& s) noexcept {
    return (s.sum_us > s.region_us ? s.sum_us : s.region_us) > slow_threshold_us;
}

// Per-frame accumulator, owner thread only. stamp() is the whole per-dispatch
// work after the owner check; take() converts and resets once per frame.
struct Accumulator {
    std::uint64_t ticks[interval_count]{};
    std::uint32_t sectors = 0, containers = 0, dispatches = 0;
    std::uint64_t last = 0; // the sector chain's opening stamp's clock
    unsigned open = none;   // the interval it opened
    std::uint64_t max_ticks = 0;
    unsigned max_owner = none;
    std::uint64_t region_last = 0; // the region chain's opening stamp's clock
    unsigned region_open = none;   // the interval it opened
    std::uint64_t region_max_ticks = 0;
    unsigned region_max_owner = none;
    // Window counters, reset by the reporter.
    std::uint64_t orphans = 0;        // chain broken: a close without its open, or an open over an open interval
    std::uint64_t clock_errors = 0;   // a backward clock: the interval is not accumulated
    std::uint64_t clock_failures = 0; // QueryPerformanceCounter failed: counting only, chain reset
    std::uint64_t unmatched = 0;      // an index outside the site table
    void stamp(unsigned index, std::uint64_t now) noexcept {
        if (index >= site_count) {
            ++unmatched;
            return;
        }
        ++dispatches;
        if (index == 4)
            ++sectors;
        else if (index == 5)
            ++containers;
        if (index >= region_site)
            chain(index, now, region_open, region_last, region_max_ticks, region_max_owner);
        else
            chain(index, now, open, last, max_ticks, max_owner);
    }
    void discard() noexcept {
        for (auto& t : ticks) t = 0;
        sectors = containers = dispatches = 0;
        last = region_last = 0;
        open = region_open = none;
        max_ticks = region_max_ticks = 0;
        max_owner = region_max_owner = none;
    }
    // Closes the frame's accumulation as a sample in microseconds.
    void take(std::uint64_t frame, std::uint64_t frequency, std::uint64_t dt_us, std::uint64_t input_us,
              Sample& out) noexcept {
        out = Sample{};
        out.frame = frame;
        out.dt_us = dt_us;
        out.sectors = sectors;
        out.containers = containers;
        out.dispatches = dispatches;
        out.input_us = input_us;
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_us[i] = frequency ? ticks[i] * 1000000ull / frequency : 0;
            (i < sector_count ? out.sum_us : out.region_us) += out.interval_us[i];
        }
        out.self_us = std::uint64_t(dispatches) * dispatch_cost_ns / 1000;
        out.max_interval_us = frequency ? max_ticks * 1000000ull / frequency : 0;
        out.max_owner = max_owner;
        out.region_max_us = frequency ? region_max_ticks * 1000000ull / frequency : 0;
        out.region_max_owner = region_max_owner;
        discard();
    }

private:
    // One chain's step: a failed clock resets that chain only; a close with
    // nothing open on a walk site is not an orphan.
    void chain(unsigned index, std::uint64_t now, unsigned& chain_open, std::uint64_t& chain_last,
               std::uint64_t& chain_max, unsigned& chain_owner) noexcept {
        if (!now) {
            ++clock_failures;
            chain_open = none;
            chain_last = 0;
            return;
        }
        const unsigned close = closes[index];
        if (close != none) {
            // A pass end with nothing open is a skipped container (the gate edge
            // landed on it); any other mismatch is a broken chain.
            if (chain_open != close) {
                if (chain_open != none || !walk[index]) ++orphans;
            } else if (now < chain_last)
                ++clock_errors;
            else {
                const std::uint64_t delta = now - chain_last;
                ticks[close] += delta;
                if (delta > chain_max) {
                    chain_max = delta;
                    chain_owner = close;
                }
            }
        } else if (chain_open != none)
            ++orphans;
        chain_open = opens[index];
        chain_last = chain_open == none ? 0 : now;
    }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0;
    std::uint64_t sectors_p50 = 0, containers_p50 = 0;
    std::uint64_t interval_p50[interval_count]{}, interval_p95[interval_count]{};
    std::uint64_t sum_p50 = 0, region_p50 = 0, input_p50 = 0, self_p50 = 0;
    std::uint64_t max_interval_us = 0; // the largest single sector-chain interval of the window
    unsigned max_owner = none;
    std::uint64_t region_max_us = 0; // the largest region-chain interval of the window
    unsigned region_max_owner = none;
    std::uint32_t slow = 0;  // frames over slow_threshold_us in either chain (all of them)
    unsigned slow_count = 0; // witnesses kept, the first slow_limit
    Sample slow_frames[slow_limit]{};
};

// Fixed 300-sample window: `add` is a few stores (plus one Sample copy for a
// slow frame within the limit); `close` runs one std::nth_element per
// statistic, once per window.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }
    void add(const Sample& s) noexcept {
        if (count_ < window_frames) {
            sectors_[count_] = s.sectors;
            containers_[count_] = s.containers;
            for (unsigned i = 0; i < interval_count; ++i) interval_[i][count_] = s.interval_us[i];
            sum_[count_] = s.sum_us;
            region_[count_] = s.region_us;
            input_[count_] = s.input_us;
            self_[count_] = s.self_us;
            ++count_;
        }
        if (s.max_interval_us > max_interval_us_) {
            max_interval_us_ = s.max_interval_us;
            max_owner_ = s.max_owner;
        }
        if (s.region_max_us > region_max_us_) {
            region_max_us_ = s.region_max_us;
            region_max_owner_ = s.region_max_owner;
        }
        if (slow_frame(s)) {
            ++slow_;
            if (slow_count_ < slow_limit) slow_frames_[slow_count_++] = s;
        }
        last_frame_ = s.frame;
    }
    bool close(Summary& out) noexcept {
        if (!count_) {
            reset();
            return false;
        }
        out.frame = last_frame_;
        out.frames = count_;
        out.sectors_p50 = percentile(sectors_, 50);
        out.containers_p50 = percentile(containers_, 50);
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_p50[i] = percentile(interval_[i], 50);
            out.interval_p95[i] = percentile(interval_[i], 95);
        }
        out.sum_p50 = percentile(sum_, 50);
        out.region_p50 = percentile(region_, 50);
        out.input_p50 = percentile(input_, 50);
        out.self_p50 = percentile(self_, 50);
        out.max_interval_us = max_interval_us_;
        out.max_owner = max_owner_;
        out.region_max_us = region_max_us_;
        out.region_max_owner = region_max_owner_;
        out.slow = slow_;
        out.slow_count = slow_count_;
        for (unsigned i = 0; i < slow_count_; ++i) out.slow_frames[i] = slow_frames_[i];
        reset();
        return true;
    }
    void reset() noexcept {
        count_ = 0;
        last_frame_ = 0;
        max_interval_us_ = 0;
        max_owner_ = none;
        region_max_us_ = 0;
        region_max_owner_ = none;
        slow_ = 0;
        slow_count_ = 0;
    }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        return x3m::stamp::percentile(values, count_, p, scratch_);
    }
    std::uint64_t sectors_[window_frames]{}, containers_[window_frames]{}, interval_[interval_count][window_frames]{};
    std::uint64_t sum_[window_frames]{}, region_[window_frames]{}, input_[window_frames]{}, self_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    Sample slow_frames_[slow_limit]{};
    unsigned count_ = 0, slow_count_ = 0;
    std::uint32_t slow_ = 0;
    std::uint64_t last_frame_ = 0, max_interval_us_ = 0, region_max_us_ = 0;
    unsigned max_owner_ = none, region_max_owner_ = none;
};
}
