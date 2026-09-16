#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

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
// per-stamp work is one 64-bit subtract and add; the window arithmetic runs
// once per frame at the frame boundary under the frame-phase guard.
namespace x3m::pass_phases::detail {
inline constexpr unsigned site_count = 4, interval_count = 3;
inline constexpr unsigned window_frames = 300;
inline constexpr const char* const interval_names[interval_count] = {"apply", "draw", "end"};
// Cost of one lean-stub dispatch (stub envelope, owner check, one
// QueryPerformanceCounter, the accumulate) measured by the CPU fixture under
// the X3 bottle (verification/probe/run_game_phase_cpu.py, `PASS PHASE
// BENCH dispatch_ns`). Each stamp's cost lands in the interval that follows
// it; `self_us` = passes * 4 * this lets the reader subtract it. Measured
// 86.8 and 90.5 ns in two runs (2026-09-16, best of 7 x 20,000 loops; the
// fixture refuses a constant more than 2x off); keep in step with the ledger in
// docs/verification/sampling-profiler.md ("Pass phases").
inline constexpr std::uint64_t dispatch_cost_ns = 87;

struct Sample {
    std::uint64_t frame = 0;
    std::uint32_t passes = 0;
    std::uint64_t interval_us[interval_count]{};
    std::uint64_t sum_us = 0;         // apply + draw + end
    std::uint64_t view_submit_us = 0; // the same frame's frame_phases view_submit sum
    std::uint64_t self_us = 0;        // passes * site_count * dispatch_cost_ns / 1000
};

// Per-frame accumulator, owner thread only. stamp() is the whole per-dispatch
// work after the owner check; take() converts and resets once per frame.
struct Accumulator {
    std::uint64_t ticks[interval_count]{};
    std::uint32_t passes = 0;
    std::uint64_t last = 0; // the previous stamp's clock, 0 = no open interval
    // Window counters, reset by the reporter.
    std::uint64_t orphans = 0;        // a closing stamp with no open interval (chain entered mid-pass)
    std::uint64_t clock_errors = 0;   // a backward clock: the interval is not accumulated
    std::uint64_t clock_failures = 0; // QueryPerformanceCounter failed: counting only, chain reset
    std::uint64_t unmatched = 0;      // an index outside the site table
    void stamp(unsigned index, std::uint64_t now) noexcept {
        if (index >= site_count) { ++unmatched; return; }
        if (!now) { ++clock_failures; last = 0; if (index == site_count - 1) ++passes; return; }
        if (index == 0) { last = now; return; }
        if (!last) ++orphans;
        else if (now < last) ++clock_errors;
        else ticks[index - 1] += now - last;
        if (index == site_count - 1) { ++passes; last = 0; }
        else last = now;
    }
    void discard() noexcept { for (auto& t : ticks) t = 0; passes = 0; last = 0; }
    // Closes the frame's accumulation as a sample in microseconds.
    void take(std::uint64_t frame, std::uint64_t frequency, std::uint64_t view_submit_us, Sample& out) noexcept {
        out = Sample{};
        out.frame = frame; out.passes = passes; out.view_submit_us = view_submit_us;
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_us[i] = frequency ? ticks[i] * 1000000ull / frequency : 0;
            out.sum_us += out.interval_us[i];
        }
        out.self_us = std::uint64_t(passes) * site_count * dispatch_cost_ns / 1000;
        discard();
    }
};

// Owner admission: the frame boundary (the frame-phase guard, Present thread)
// admits its thread once; a stamp from any other thread is `foreign`, a stamp
// before the first frame boundary is `early`; both are counted and ignored.
// Per stamp this is one relaxed load and a compare.
struct Gate {
    std::atomic<std::uint32_t> owner{0};
    std::atomic<std::uint32_t> early{0}, foreign{0};
    bool admit(std::uint32_t thread) noexcept {
        std::uint32_t expected = 0;
        owner.compare_exchange_strong(expected, thread, std::memory_order_acq_rel);
        return owner.load(std::memory_order_acquire) == thread;
    }
    bool owned(std::uint32_t thread) noexcept {
        const std::uint32_t current = owner.load(std::memory_order_relaxed);
        if (current == thread) return true;
        (current ? foreign : early).fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    void reset() noexcept { owner.store(0); early.store(0); foreign.store(0); }
};

struct Summary {
    std::uint64_t frame = 0;
    std::uint32_t frames = 0;
    std::uint64_t passes_p50 = 0;
    std::uint64_t interval_p50[interval_count]{}, interval_p95[interval_count]{};
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
            ++count_;
        }
        last_frame_ = s.frame;
    }
    bool close(Summary& out) noexcept {
        if (!count_) { reset(); return false; }
        out = Summary{};
        out.frame = last_frame_; out.frames = count_;
        out.passes_p50 = percentile(passes_, 50);
        for (unsigned i = 0; i < interval_count; ++i) {
            out.interval_p50[i] = percentile(interval_[i], 50);
            out.interval_p95[i] = percentile(interval_[i], 95);
        }
        out.sum_p50 = percentile(sum_, 50); out.view_submit_p50 = percentile(submit_, 50);
        out.self_p50 = percentile(self_, 50);
        reset();
        return true;
    }
    void reset() noexcept { count_ = 0; last_frame_ = 0; }

private:
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        std::copy(values, values + count_, scratch_);
        std::size_t index = (static_cast<std::size_t>(count_) * p) / 100;
        if (index >= count_) index = count_ - 1;
        std::nth_element(scratch_, scratch_ + index, scratch_ + count_);
        return scratch_[index];
    }
    std::uint64_t passes_[window_frames]{}, interval_[interval_count][window_frames]{};
    std::uint64_t sum_[window_frames]{}, submit_[window_frames]{}, self_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    unsigned count_ = 0;
    std::uint64_t last_frame_ = 0;
};
}
