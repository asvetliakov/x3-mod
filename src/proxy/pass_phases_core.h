#pragma once
#include <cstdint>
#include "stamp_core.h"

// Value-only state of the pass-phase diagnostic (X3M_PASS_PHASES=1): the
// per-frame accumulate-only stamp logic, the owner gate and the 300-frame
// window reduction. No Win32, no allocation, no locking. Host-tested by
// verification/probe/pass_phases_host.cpp.
//
// The four stamps sit in the D3DX effect pass loop of 0x004c0150
// (docs/reverse-engineering/effect-pass-loop.md): pass_begin opens the
// pass-apply interval (BeginPass), pass_applied closes it and opens the draw
// interval (two geometry calls plus DrawIndexedPrimitive), pass_drawn closes
// that and opens the EndPass interval, pass_end closes it and counts one pass.
// They fire once per material draw, ~4,024 dispatches per busy frame, so the
// per-stamp work also partitions elapsed ticks by cumulative view submission;
// the window arithmetic runs
// once per frame at the frame boundary under the frame-phase guard.
namespace x3m::pass_phases::detail {
inline constexpr unsigned site_count = 4, interval_count = 3;
inline constexpr unsigned window_frames = 300;
inline constexpr const char* const interval_names[interval_count] = {"apply", "draw", "end"};
// Cost of one lean-stub dispatch (stub envelope, owner check, one
// QueryPerformanceCounter, the accumulate) measured by the CPU fixture under
// the X3 bottle (verification/probe/run_game_phase_cpu.py, `PASS PHASE
// BENCH dispatch_ns`). Each stamp's cost lands in the interval that follows
// it; `self_us` = passes * 4 * this estimates the dispatch contribution.
// Active-view attribution measured 101.4 ns on 2026-09-20 (best of 7 x
// 20,000 loops), rounded up to 102 ns. The fixture refuses a constant more
// than 2x off; keep in step with docs/verification/sampling-profiler.md
// ("Pass phases"). This CPU-fixture estimate is not exact flight self-cost.
inline constexpr std::uint64_t dispatch_cost_ns = 102;
using Gate = x3m::stamp::Gate;

struct Sample {
    std::uint64_t frame = 0;
    std::uint32_t passes = 0;
    std::uint64_t interval_us[interval_count]{};
    std::uint64_t sum_us = 0;         // apply + draw + end
    std::uint64_t view_submit_us = 0; // the same frame's frame_phases view_submit sum
    std::uint64_t scoped_us = 0, outside_us = 0, complement_us = 0;
    std::uint32_t outside_passes = 0, crossing_passes = 0;
    std::uint64_t self_us = 0;        // passes * site_count * dispatch_cost_ns / 1000
};

// Per-frame accumulator, owner thread only. stamp() is the whole per-dispatch
// work after the owner check; take() converts and resets once per frame.
struct Accumulator {
    std::uint64_t ticks[interval_count]{};
    std::uint32_t passes = 0;
    std::uint64_t last = 0; // the previous stamp's clock, 0 = no open interval
    // Clocks retained for the residual group (residual_phases_core.h), written
    // here and read on the same owner thread: the last pass_end, and the first
    // pass_begin after the residual group armed the capture. One store per
    // pass_end and the matching cumulative submission clocks.
    std::uint64_t end_clock = 0, begin_clock = 0;
    bool begin_armed = false;
    std::uint64_t end_submission = 0, begin_submission = 0;
    std::uint64_t last_submission = 0, scoped_ticks = 0, outside_ticks = 0, pass_view = 0, pass_submission = 0;
    std::uint32_t outside_passes = 0, crossing_passes = 0;
    std::uint64_t scope_errors = 0, complement_underflow = 0;
    // Window counters, reset by the reporter.
    std::uint64_t orphans = 0;        // a closing stamp with no open interval, or an opening stamp over one still open
    std::uint64_t clock_errors = 0;   // a backward clock: the interval is not accumulated
    std::uint64_t clock_failures = 0; // QueryPerformanceCounter failed: counting only, chain reset
    std::uint64_t unmatched = 0;      // an index outside the site table
    void stamp(unsigned index, std::uint64_t now, std::uint64_t submission = 0, std::uint64_t view = 0) noexcept {
        if (index >= site_count) { ++unmatched; return; }
        if (index == 0 && begin_armed) { begin_armed = false; begin_clock = now; begin_submission = submission; }
        if (!now) { ++clock_failures; last = 0; if (index == site_count - 1) { ++passes; end_clock = end_submission = 0; } return; }
        if (index == 0) { if (last) ++orphans; last = now; last_submission = submission; pass_view = view; pass_submission = submission; return; } // a pass whose end never arrived
        if (!last) ++orphans;
        else if (now < last) ++clock_errors;
        else {
            const auto elapsed = now - last;
            ticks[index - 1] += elapsed;
            if (submission >= last_submission && submission - last_submission <= elapsed) {
                scoped_ticks += submission - last_submission;
                outside_ticks += elapsed - (submission - last_submission);
            } else ++scope_errors;
        }
        last_submission = submission;
        if (index == site_count - 1) { ++passes; last = 0; end_clock = now; end_submission = submission;
            // Whole-pass categories: outside is wholly outside, crossing is
            // any view boundary, including outside -> full view -> outside.
            if (!pass_view && !view && submission == pass_submission) ++outside_passes;
            else if (pass_view != view || !pass_view) ++crossing_passes; }
        else last = now;
    }
    void discard() noexcept { for (auto& t : ticks) t = 0; passes = 0; last = 0; end_clock = begin_clock = 0; begin_armed = false; end_submission = begin_submission = last_submission = scoped_ticks = outside_ticks = pass_view = pass_submission = 0; outside_passes = crossing_passes = 0; }
    // Closes the frame's accumulation as a sample in microseconds.
    void take(std::uint64_t frame, std::uint64_t frequency, std::uint64_t view_submit_us, Sample& out) noexcept {
        out = Sample{};
        out.frame = frame; out.passes = passes; out.view_submit_us = view_submit_us;
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_us[i] = frequency ? ticks[i] * 1000000ull / frequency : 0;
            out.sum_us += out.interval_us[i];
        }
        out.scoped_us = frequency ? scoped_ticks * 1000000ull / frequency : 0;
        out.outside_us = frequency ? outside_ticks * 1000000ull / frequency : 0;
        out.outside_passes = outside_passes; out.crossing_passes = crossing_passes;
        if (view_submit_us >= out.scoped_us) out.complement_us = view_submit_us - out.scoped_us;
        else ++complement_underflow;
        out.self_us = std::uint64_t(passes) * site_count * dispatch_cost_ns / 1000;
        discard();
    }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0;
    std::uint64_t passes_p50 = 0;
    std::uint64_t interval_p50[interval_count]{}, interval_p95[interval_count]{};
    std::uint64_t attribution_p50[5]{}, attribution_p95[5]{};
    std::uint64_t outside_passes_total = 0, crossing_passes_total = 0;
    std::uint64_t sum_p50 = 0, view_submit_p50 = 0, self_p50 = 0;
};

// Fixed 300-sample window: `add` is six stores; `close` runs one
// std::nth_element per statistic, once per window.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }
    void add(const Sample& s) noexcept {
        if (count_ < window_frames) {
            passes_[count_] = s.passes;
            for (unsigned i = 0; i < interval_count; ++i) interval_[i][count_] = s.interval_us[i];
            sum_[count_] = s.sum_us; submit_[count_] = s.view_submit_us; self_[count_] = s.self_us;
            const std::uint64_t a[] = {s.scoped_us, s.outside_us, s.complement_us, s.outside_passes, s.crossing_passes};
            for (unsigned i = 0; i < 5; ++i) attribution_[i][count_] = a[i];
            outside_passes_total_ += s.outside_passes; crossing_passes_total_ += s.crossing_passes;
            ++count_;
        }
        last_frame_ = s.frame;
    }
    bool close(Summary& out) noexcept {
        if (!count_) { reset(); return false; }
        out = Summary{};
        out.frame = last_frame_; out.frames = count_;
        out.passes_p50 = percentile(passes_, 50);
        out.outside_passes_total = outside_passes_total_; out.crossing_passes_total = crossing_passes_total_;
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_p50[i] = percentile(interval_[i], 50);
            out.interval_p95[i] = percentile(interval_[i], 95);
        }
        out.sum_p50 = percentile(sum_, 50); out.view_submit_p50 = percentile(submit_, 50);
        for (unsigned i = 0; i < 5; ++i) { out.attribution_p50[i] = percentile(attribution_[i], 50); out.attribution_p95[i] = percentile(attribution_[i], 95); }
        out.self_p50 = percentile(self_, 50);
        reset();
        return true;
    }
    void reset() noexcept { count_ = 0; last_frame_ = outside_passes_total_ = crossing_passes_total_ = 0; }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        return x3m::stamp::percentile(values, count_, p, scratch_);
    }
    std::uint64_t passes_[window_frames]{}, interval_[interval_count][window_frames]{};
    std::uint64_t sum_[window_frames]{}, submit_[window_frames]{}, self_[window_frames]{};
    std::uint64_t attribution_[5][window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    unsigned count_ = 0;
    std::uint64_t last_frame_ = 0, outside_passes_total_ = 0, crossing_passes_total_ = 0;
};
}
