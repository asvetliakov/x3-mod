#pragma once
// Default-off frame-time diagnostic (X3M_FRAME_TIMING=1, launcher
// --frame-timing, requires --telemetry). frame_end logs only every 300 frames,
// so a slow window cannot be located inside it; this collects one sample per
// frame into fixed-size arrays and reduces them at the window boundary into a
// single `frame_timing` line plus up to four `frame_timing_slow` witnesses.
// Diagnostic timings, never game FPS: dt is the Present-to-Present interval
// seen by the wrapper and present_us is the wall time inside the forwarded
// native Present. The per-frame sample also carries the wall time spent inside
// the proxy's own hooked entry points, split into draw, scene and state
// buckets (Scope below), which is the split the sampling profiler cannot
// produce under FEX. State-bucket calls are counted, not stamped, unless
// X3M_FRAME_TIMING_STATE_STAMPS=N asks for every Nth one; the remainder of the
// frame (game code between hooked calls) is split into three gaps by position
// from the stamps already taken.
//
// The window reduction below is host-testable C++ with no Win32 and no
// allocation (verification/probe/frame_timing_host.cpp); frame_timing.cpp holds
// the option parse, the QueryPerformanceCounter stamps and the log lines.
// All state is touched under the capture hook mutex (the guards take the
// frame-timing stamps with the lock already held), never concurrently. An
// abnormal unwind (SEH or longjmp) past a live Scope would leave the nesting
// depth above zero and silence the buckets until the next initialize(); this
// is a diagnostic, so it neither guards against that nor affects the hooks.
#include <algorithm>
#include <cstdint>

namespace x3m::frame_timing {

inline constexpr unsigned window_frames = 300; // samples per reported window
inline constexpr unsigned slow_slots = 4;      // retained slowest frames per window

// Where a hooked entry point's wall time is accounted. The sampling profiler
// cannot attribute leaves under FEX (run84), so the split between the proxy's
// own code and the game/driver is measured here instead: one bucket for the
// four draw hooks, one for the proxy's own scene-end passes and one for every
// other hooked device call.
enum class Bucket : unsigned { Draw = 0, Scene = 1, State = 2 };
inline constexpr unsigned bucket_count = 3;

// Where the frame's unhooked time (dt minus every hooked call) fell, by
// position relative to the frame's draws: before the first draw, between the
// first and the last draw (the game's per-object scene traversal between
// hooked calls) and after the last draw (UI, script, physics, AI). This is
// game time, not proxy time, and it is derived from stamps already taken.
enum class Gap : unsigned { Pre = 0, Draw = 1, Post = 2 };
inline constexpr unsigned gap_count = 3;

// State calls are counted, not stamped, unless X3M_FRAME_TIMING_STATE_STAMPS=N
// asks for every Nth call: two QueryPerformanceCounter reads per hooked state
// call cost more than the call itself under FEX (67.8 ns per read,
// docs/verification/sampling-profiler.md). With no stamps and no calibration
// constant the state bucket has no time to report and carries this sentinel,
// which the log prints as -1.
inline constexpr std::uint64_t unknown_us = ~std::uint64_t{0};
// Nanoseconds per hooked state call, measured on this machine; zero means no
// calibration exists and state_us is reported as -1 when nothing is stamped.
inline constexpr std::uint64_t state_calibration_ns = 0;

// Per-entry call counts for the state bucket, so the setter mix of a frame is
// measured rather than one total (docs/architecture/state-call-fast-path.md).
// A fixed open-addressed table keyed by the entry's static name pointer: one
// hash, one pointer compare and one increment per hooked state call, no QPC.
// A name that finds no slot within the probe limit is counted as "other".
inline constexpr unsigned state_entry_slots = 32; // power of two
inline constexpr unsigned state_top_count = 6;    // entries reported per window
struct TopEntry {
    unsigned slot = 0;         // index into the process-wide name table
    std::uint64_t calls = 0;   // window p50 of that entry's per-frame calls
};

struct Frame {
    std::uint64_t frame = 0;      // wrapper frame index
    std::uint64_t dt_us = 0;      // Present-to-Present interval
    std::uint64_t present_us = 0; // wall time inside the native Present
    std::uint64_t draws = 0;      // draws submitted in that frame
    std::uint64_t prims = 0;      // primitives summed over the frame's draws
    // Wall time inside the proxy's hooked entry points, by bucket, counted on
    // the outermost entry only so a reentrant hooked call is never counted
    // twice. draw_us and scene_us include the forwarded native call;
    // draw_native_us is that native call alone across the draw hooks, so
    // draw_us - draw_native_us is the proxy's own per-draw work.
    std::uint64_t bucket_us[bucket_count]{};
    std::uint64_t draw_native_us = 0;
    std::uint64_t bucket_calls[bucket_count]{};
    const char* slow_call = "";      // slowest single hooked call of the frame
    std::uint64_t slow_call_us = 0;
    // The frame's unhooked time split by position (Gap above). The three sum
    // with the buckets and present_us to dt_us unless a gap clamped at zero.
    std::uint64_t gap_us[gap_count]{};
    // State-bucket calls of the frame by entry-name slot, and the calls whose
    // name found no slot. The slot-to-name mapping lives in frame_timing.cpp.
    std::uint64_t state_entry_calls[state_entry_slots]{};
    std::uint64_t state_other_calls = 0;
};

struct Summary {
    std::uint64_t frame = 0; // last frame of the window
    std::uint32_t frames = 0;
    std::uint64_t dt_p50 = 0, dt_p95 = 0, dt_max = 0;
    std::uint64_t draws_p50 = 0, draws_max = 0;
    std::uint64_t present_p50 = 0, present_p95 = 0, present_max = 0;
    std::uint64_t bucket_p50[bucket_count]{}, bucket_p95[bucket_count]{}, bucket_max[bucket_count]{};
    std::uint64_t bucket_calls_p50[bucket_count]{};
    std::uint64_t draw_native_p50 = 0, draw_native_max = 0;
    std::uint64_t gap_p50[gap_count]{}, gap_p95[gap_count]{}, gap_max[gap_count]{};
    std::uint64_t gap_draw_per_draw_ns = 0; // window p50 gap_draw divided by the p50 draw count
    TopEntry state_top[state_top_count]{}; // most-called state entries, descending
    unsigned state_top_used = 0;
    std::uint64_t state_other_p50 = 0;
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
            for (unsigned b = 0; b < bucket_count; ++b) {
                bucket_[b][count_] = sample.bucket_us[b];
                bucket_calls_[b][count_] = sample.bucket_calls[b];
            }
            draw_native_[count_] = sample.draw_native_us;
            for (unsigned g = 0; g < gap_count; ++g) gap_[g][count_] = sample.gap_us[g];
            for (unsigned e = 0; e < state_entry_slots; ++e)
                state_entry_[e][count_] = sample.state_entry_calls[e];
            state_other_[count_] = sample.state_other_calls;
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
        for (unsigned b = 0; b < bucket_count; ++b) {
            out.bucket_p50[b] = percentile(bucket_[b], 50);
            out.bucket_p95[b] = percentile(bucket_[b], 95);
            out.bucket_max[b] = percentile(bucket_[b], 100);
            out.bucket_calls_p50[b] = percentile(bucket_calls_[b], 50);
        }
        for (unsigned g = 0; g < gap_count; ++g) {
            out.gap_p50[g] = percentile(gap_[g], 50);
            out.gap_p95[g] = percentile(gap_[g], 95);
            out.gap_max[g] = percentile(gap_[g], 100);
        }
        out.gap_draw_per_draw_ns = out.draws_p50
            ? out.gap_p50[static_cast<unsigned>(Gap::Draw)] * 1000ull / out.draws_p50 : 0;
        out.draw_native_p50 = percentile(draw_native_, 50);
        out.draw_native_max = percentile(draw_native_, 100);
        // Per-entry medians, then the six most-called, descending; ties keep
        // the lower slot, which is the order the names were first seen.
        out.state_other_p50 = percentile(state_other_, 50);
        for (unsigned e = 0; e < state_entry_slots; ++e) {
            const std::uint64_t calls = percentile(state_entry_[e], 50);
            if (!calls) continue;
            unsigned at = out.state_top_used;
            while (at > 0 && out.state_top[at - 1].calls < calls) --at;
            if (at >= state_top_count) continue;
            for (unsigned i = out.state_top_used < state_top_count ? out.state_top_used : state_top_count - 1; i > at; --i)
                out.state_top[i] = out.state_top[i - 1];
            out.state_top[at] = TopEntry{e, calls};
            if (out.state_top_used < state_top_count) ++out.state_top_used;
        }
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
    std::uint64_t bucket_[bucket_count][window_frames]{};
    std::uint64_t bucket_calls_[bucket_count][window_frames]{};
    std::uint64_t draw_native_[window_frames]{};
    std::uint64_t gap_[gap_count][window_frames]{};
    std::uint64_t state_entry_[state_entry_slots][window_frames]{};
    std::uint64_t state_other_[window_frames]{};
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
void draw_native_begin_impl() noexcept;
void draw_native_end_impl() noexcept;
void draw_impl(unsigned primitives) noexcept;
void frame_impl(std::uint64_t frame, std::uint64_t draws) noexcept;

// Stack-local state of one timed hooked call. Only `entered` is initialized
// when the option is off: constructing a Scope then costs one store and the
// branch on `active`, and nothing is read afterwards.
struct ScopeState {
    std::uint64_t start;
    std::uint64_t exclude; // native Present ticks already accounted at entry
    const char* entry;
    unsigned bucket;
    bool outermost;
    bool timed;    // stamped: false for a counted-only state call
    bool entered = false;
};
void scope_begin_impl(ScopeState& state, unsigned bucket, const char* entry) noexcept;
void scope_end_impl(ScopeState& state) noexcept;
}

// One timed hooked entry point. `entry` must have static storage (a literal or
// __builtin_FUNCTION()); it is retained as the slow-call witness. The stamps
// sit outside the CPU-state envelope by construction: the object is declared
// after the boundary, so its destructor runs after after_original() and before
// the boundary restores the outgoing state (src/proxy/cpu_state.h).
class Scope {
public:
    Scope(Bucket bucket, const char* entry) noexcept {
        if (active) detail::scope_begin_impl(state_, static_cast<unsigned>(bucket), entry);
    }
    ~Scope() { if (state_.entered) detail::scope_end_impl(state_); }
    // Ends the measurement early, before the rest of the hook's scope; a second
    // call and the destructor then do nothing.
    void close() noexcept { if (state_.entered) detail::scope_end_impl(state_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    detail::ScopeState state_;
};

// Option off: one predictable branch on a process-global bool, no call.
inline void present_begin() noexcept { if (active) detail::present_begin_impl(); }
inline void present_end() noexcept { if (active) detail::present_end_impl(); }
inline void draw(unsigned primitives) noexcept { if (active) detail::draw_impl(primitives); }
// Around the forwarded native draw call only, in the same position as
// present_begin/present_end relative to before_original/after_original.
inline void draw_native_begin() noexcept { if (active) detail::draw_native_begin_impl(); }
inline void draw_native_end() noexcept { if (active) detail::draw_native_end_impl(); }
inline void frame(std::uint64_t frame_index, std::uint64_t draws) noexcept {
    if (active) detail::frame_impl(frame_index, draws);
}

}
