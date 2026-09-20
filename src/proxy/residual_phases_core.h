#pragma once
#include <cstdint>
#include "stamp_core.h"

// Value-only state of the residual-phase diagnostic (X3M_RESIDUAL_PHASES=1):
// two accumulate-only stamps that split what the pass and frame groups leave
// unattributed, the owner gate and the 300-frame window reduction. No Win32,
// no allocation, no locking. Host-tested by
// verification/probe/residual_phases_host.cpp.
//
// The original pass_end -> material and material -> first pass_begin spans
// can cross view boundaries. Retained cumulative submission clocks partition
// each span into its intersection with main-view submission (prepare/setup)
// and its complement (between_prepare/outside_setup). These complements can
// include scene composite, particles and other engine/proxy work; they are not
// additional disjoint frame phases. No new QPC is read for this partition.
// Particles and other retain their original frame-view definitions. The first
// material without a fresh pass end, and setup without a following pass, remain
// explicitly skipped. Failed QPC and each frame discard invalidate pairings.
namespace x3m::residual_phases::detail {
inline constexpr unsigned site_count = 2, interval_count = 6, stamped_intervals = 6;
inline constexpr unsigned window_frames = 300;
inline constexpr const char* const interval_names[interval_count] = {"prepare", "setup", "particles", "other", "between_prepare", "outside_setup"};
// Cost of one lean-stub dispatch (the same stub as pass phases: envelope,
// owner check, one QueryPerformanceCounter, the pairing) measured by the CPU
// fixture under the X3 bottle (`RESIDUAL PHASE BENCH dispatch_ns`); the
// fixture refuses a constant more than 2x off. `self_us` = (materials +
// particle_views) * this. Keep in step with the ledger in
// docs/verification/sampling-profiler.md ("Residual phases").
// Active-view successful pairing measured 108.9 ns on 2026-09-20 (best
// of 7 x 20,000 loops), rounded up to 109 ns. This is a two-site average,
// not the material-only dispatch cost or exact flight self-cost.
inline constexpr std::uint64_t dispatch_cost_ns = 109;
using Gate = x3m::stamp::Gate;

struct Sample {
    std::uint64_t frame = 0;
    std::uint32_t materials = 0;        // material_setup stamps of the frame
    std::uint32_t particle_views = 0;   // view_particles stamps of the frame
    std::uint32_t passes = 0;           // the same frame's pass_phases pass count
    std::uint32_t views = 0;            // the same frame's frame_phases view count
    std::uint64_t interval_us[interval_count]{};
    std::uint64_t prepare_per_pass_ns = 0, setup_per_pass_ns = 0; // interval * 1000 / passes
    std::uint64_t views_us = 0, view_setup_us = 0, view_submit_us = 0; // the frame_phases inputs of `other`
    std::uint64_t self_us = 0;          // (materials + particle_views) * dispatch_cost_ns / 1000
};

// Per-frame accumulator, owner thread only. material()/view() are the whole
// per-dispatch work after the owner check; take() converts and resets once
// per frame.
struct Accumulator {
    std::uint64_t ticks[stamped_intervals]{}; // slot 3 (other) is computed at take
    std::uint32_t materials = 0, particle_views = 0;
    std::uint64_t p_clock = 0;          // the last material_setup clock, 0 = none this frame
    std::uint64_t submit_end_seen = 0;  // the frame submit_end clock already paired
    std::uint64_t p_submission = 0;
    std::uint64_t scope_errors = 0, outside_materials = 0;
    bool broken = false;                // a clock failure at material_setup: the next pairing is skipped
    // Window counters, reset by the reporter.
    std::uint64_t prepare_skipped = 0;  // no pass_end since the previous material (the frame's first, or a skipped pass loop)
    std::uint64_t setup_skipped = 0;    // no pass_begin followed the material (a skipped pass loop)
    std::uint64_t view_skipped = 0;     // no fresh view_submit_end to pair with
    std::uint64_t other_underflow = 0;  // views < view_setup + view_submit + particles: other reported as 0
    std::uint64_t clock_errors = 0;     // a backward clock: the interval is not accumulated
    std::uint64_t clock_failures = 0;   // QueryPerformanceCounter failed: counting only
    std::uint64_t unmatched = 0;        // an index outside the site table
    void close_setup(std::uint64_t begin_clock, bool& begin_armed, std::uint64_t begin_submission) noexcept {
        if (!p_clock) return;
        if (!begin_armed && begin_clock >= p_clock) partition(1, 5, begin_clock - p_clock, p_submission, begin_submission);
        else ++setup_skipped;
    }
    void partition(unsigned inside, unsigned outside, std::uint64_t elapsed, std::uint64_t from, std::uint64_t to) noexcept {
        if (to < from || to - from > elapsed) { ++scope_errors; return; }
        ticks[inside] += to - from; ticks[outside] += elapsed - (to - from);
    }
    void material(std::uint64_t now, std::uint64_t end_clock, std::uint64_t begin_clock, bool& begin_armed,
                  std::uint64_t submission, std::uint64_t end_submission, std::uint64_t begin_submission, bool in_view) noexcept {
        ++materials; if (!in_view) ++outside_materials;
        if (!now) { ++clock_failures; broken = true; begin_armed = false; p_clock = 0; return; }
        if (broken) { ++prepare_skipped; ++setup_skipped; broken = false; }
        else {
            close_setup(begin_clock, begin_armed, begin_submission);
            if (end_clock && end_clock > p_clock) {
                if (now >= end_clock) partition(0, 4, now - end_clock, end_submission, submission); else ++clock_errors;
            } else ++prepare_skipped;
        }
        p_clock = now; p_submission = submission; begin_armed = true;
    }
    void view(std::uint64_t now, std::uint64_t submit_end) noexcept {
        ++particle_views;
        if (!now) { ++clock_failures; return; }
        if (submit_end && submit_end != submit_end_seen) {
            submit_end_seen = submit_end;
            if (now >= submit_end) ticks[2] += now - submit_end; else ++clock_errors;
        } else ++view_skipped;
    }
    void discard(bool& begin_armed) noexcept {
        for (auto& t : ticks) t = 0;
        materials = particle_views = 0; p_clock = p_submission = 0; submit_end_seen = 0; broken = false; begin_armed = false;
    }
    // Closes the frame's accumulation as a sample in microseconds; the pending
    // setup of the frame's last material is closed first.
    void take(std::uint64_t frame, std::uint64_t frequency, std::uint64_t views_us, std::uint64_t view_setup_us,
              std::uint64_t view_submit_us, std::uint32_t views, std::uint32_t passes,
              std::uint64_t begin_clock, bool& begin_armed, Sample& out, std::uint64_t begin_submission) noexcept {
        if (!broken) close_setup(begin_clock, begin_armed, begin_submission);
        out = Sample{};
        out.frame = frame; out.materials = materials; out.particle_views = particle_views;
        out.passes = passes; out.views = views;
        out.views_us = views_us; out.view_setup_us = view_setup_us; out.view_submit_us = view_submit_us;
        for (unsigned i = 0; i < stamped_intervals; ++i) out.interval_us[i] = frequency ? ticks[i] * 1000000ull / frequency : 0;
        const std::uint64_t attributed = view_setup_us + view_submit_us + out.interval_us[2];
        if (views_us >= attributed) out.interval_us[3] = views_us - attributed; else ++other_underflow;
        if (passes) {
            out.prepare_per_pass_ns = out.interval_us[0] * 1000 / passes;
            out.setup_per_pass_ns = out.interval_us[1] * 1000 / passes;
        }
        out.self_us = (std::uint64_t(materials) + particle_views) * dispatch_cost_ns / 1000;
        discard(begin_armed);
    }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0;
    std::uint64_t materials_p50 = 0, particle_views_p50 = 0, passes_p50 = 0, views_p50 = 0;
    std::uint64_t interval_p50[interval_count]{}, interval_p95[interval_count]{};
    std::uint64_t prepare_per_pass_p50 = 0, setup_per_pass_p50 = 0, self_p50 = 0;
};

// Fixed 300-sample window: `add` is a few stores; `close` runs one
// std::nth_element per statistic, once per window.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }
    void add(const Sample& s) noexcept {
        if (count_ < window_frames) {
            materials_[count_] = s.materials; particle_views_[count_] = s.particle_views;
            passes_[count_] = s.passes; views_[count_] = s.views;
            for (unsigned i = 0; i < interval_count; ++i) interval_[i][count_] = s.interval_us[i];
            per_pass_[0][count_] = s.prepare_per_pass_ns; per_pass_[1][count_] = s.setup_per_pass_ns;
            self_[count_] = s.self_us;
            ++count_;
        }
        last_frame_ = s.frame;
    }
    bool close(Summary& out) noexcept {
        if (!count_) { reset(); return false; }
        out = Summary{};
        out.frame = last_frame_; out.frames = count_;
        out.materials_p50 = percentile(materials_, 50); out.particle_views_p50 = percentile(particle_views_, 50);
        out.passes_p50 = percentile(passes_, 50); out.views_p50 = percentile(views_, 50);
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_p50[i] = percentile(interval_[i], 50);
            out.interval_p95[i] = percentile(interval_[i], 95);
        }
        out.prepare_per_pass_p50 = percentile(per_pass_[0], 50); out.setup_per_pass_p50 = percentile(per_pass_[1], 50);
        out.self_p50 = percentile(self_, 50);
        reset();
        return true;
    }
    void reset() noexcept { count_ = 0; last_frame_ = 0; }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        return x3m::stamp::percentile(values, count_, p, scratch_);
    }
    std::uint64_t materials_[window_frames]{}, particle_views_[window_frames]{}, passes_[window_frames]{}, views_[window_frames]{};
    std::uint64_t interval_[interval_count][window_frames]{}, per_pass_[2][window_frames]{}, self_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    unsigned count_ = 0;
    std::uint64_t last_frame_ = 0;
};
}
