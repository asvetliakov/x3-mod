#pragma once
// Default-off frame-time diagnostic (X3M_FRAME_TIMING=1, launcher
// --frame-timing, requires --telemetry). frame_end logs only every 300 frames,
// so a slow window cannot be located inside it; this collects one sample per
// frame into fixed-size arrays and reduces them at the window boundary into a
// single `frame_timing` line plus up to four `frame_timing_slow` witnesses.
// Diagnostic timings, never game FPS: dt is the Present-to-Present interval
// seen by the wrapper and present_us is the wall time inside the forwarded
// native Present.
//
// The window reduction below is host-testable C++ with no Win32 and no
// allocation (verification/probe/frame_timing_host.cpp); frame_timing.cpp holds
// the option parse, the QueryPerformanceCounter stamps and the log lines.
// All state is touched from the render thread only, under the capture hook
// guard (Present and the draw snapshot), never concurrently.
#include <algorithm>
#include <cstdint>

namespace x3m::frame_timing {

inline constexpr unsigned window_frames = 300; // samples per reported window
inline constexpr unsigned slow_slots = 4;      // retained slowest frames per window

struct Frame {
    std::uint64_t frame = 0;      // wrapper frame index
    std::uint64_t dt_us = 0;      // Present-to-Present interval
    std::uint64_t present_us = 0; // wall time inside the native Present
    std::uint64_t draws = 0;      // draws submitted in that frame
    std::uint64_t prims = 0;      // primitives summed over the frame's draws
};

struct Summary {
    std::uint64_t frame = 0; // last frame of the window
    std::uint32_t frames = 0;
    std::uint64_t dt_p50 = 0, dt_p95 = 0, dt_max = 0;
    std::uint64_t draws_p50 = 0, draws_max = 0;
    std::uint64_t present_p50 = 0, present_p95 = 0, present_max = 0;
    std::uint32_t slow = 0; // frames with dt above twice the window p50
    Frame slow_frames[slow_slots]{};
    std::uint32_t slow_frames_count = 0; // slowest first
};

// Fixed 300-sample window; `add` is a store and at most four comparisons.
// `close` runs one copy plus one std::nth_element per requested statistic
// (eight passes over at most 300 values), once per window, off the per-frame
// path apart from the boundary frame itself.
class Window {
public:
    unsigned count() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= window_frames; }

    void add(const Frame& sample) noexcept {
        if (count_ < window_frames) {
            dt_[count_] = sample.dt_us;
            present_[count_] = sample.present_us;
            draws_[count_] = sample.draws;
            ++count_;
        }
        last_frame_ = sample.frame;
        // Slowest four by dt, descending, insertion into a fixed array.
        unsigned slot = slow_count_;
        while (slot > 0 && slow_[slot - 1].dt_us < sample.dt_us) --slot;
        if (slot >= slow_slots) return;
        for (unsigned i = slow_count_ < slow_slots ? slow_count_ : slow_slots - 1; i > slot; --i)
            slow_[i] = slow_[i - 1];
        slow_[slot] = sample;
        if (slow_count_ < slow_slots) ++slow_count_;
    }

    // Reduces the collected samples and restarts the window. False (and an
    // untouched summary) when no frame was collected.
    bool close(Summary& out) noexcept {
        if (!count_) { reset(); return false; }
        out = Summary{};
        out.frame = last_frame_;
        out.frames = count_;
        out.dt_p50 = percentile(dt_, 50);
        out.dt_p95 = percentile(dt_, 95);
        out.dt_max = percentile(dt_, 100);
        out.draws_p50 = percentile(draws_, 50);
        out.draws_max = percentile(draws_, 100);
        out.present_p50 = percentile(present_, 50);
        out.present_p95 = percentile(present_, 95);
        out.present_max = percentile(present_, 100);
        const std::uint64_t threshold = out.dt_p50 * 2;
        for (unsigned i = 0; i < count_; ++i)
            if (dt_[i] > threshold) ++out.slow;
        out.slow_frames_count = slow_count_;
        for (unsigned i = 0; i < slow_count_; ++i) out.slow_frames[i] = slow_[i];
        reset();
        return true;
    }

    void reset() noexcept { count_ = 0; slow_count_ = 0; last_frame_ = 0; }

private:
    // Nearest-rank on the collected samples: index min(count-1, count*p/100)
    // of the ascending order; p=100 is the maximum.
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) noexcept {
        std::copy(values, values + count_, scratch_);
        std::size_t index = (static_cast<std::size_t>(count_) * p) / 100;
        if (index >= count_) index = count_ - 1;
        std::nth_element(scratch_, scratch_ + index, scratch_ + count_);
        return scratch_[index];
    }

    std::uint64_t dt_[window_frames]{};
    std::uint64_t present_[window_frames]{};
    std::uint64_t draws_[window_frames]{};
    std::uint64_t scratch_[window_frames]{};
    Frame slow_[slow_slots]{};
    unsigned count_ = 0, slow_count_ = 0;
    std::uint64_t last_frame_ = 0;
};

// One read of X3M_FRAME_TIMING at attach; nothing else is allocated later.
void initialize() noexcept;

extern bool active; // false unless the option was requested

namespace detail {
void present_begin_impl() noexcept;
void present_end_impl() noexcept;
void draw_impl(unsigned primitives) noexcept;
void frame_impl(std::uint64_t frame, std::uint64_t draws) noexcept;
}

// Option off: one predictable branch on a process-global bool, no call.
inline void present_begin() noexcept { if (active) detail::present_begin_impl(); }
inline void present_end() noexcept { if (active) detail::present_end_impl(); }
inline void draw(unsigned primitives) noexcept { if (active) detail::draw_impl(primitives); }
inline void frame(std::uint64_t frame_index, std::uint64_t draws) noexcept {
    if (active) detail::frame_impl(frame_index, draws);
}

}
