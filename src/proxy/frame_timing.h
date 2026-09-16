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
#include "linear_cutout.h" // the two qualified cutout pairs, counted independently of the table
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

// ---- count-only window diagnostics ----------------------------------------
// Three per-window counters that decide the next features (run 31): which
// program pairs the frame's draws used, how many state writes rewrite the
// value already on the device, and how many consecutive draws differ only in
// their constants. All three are counted, never stamped: no clock read, no
// allocation, no lock of their own (the hook mutex is already held), and one
// predictable branch on `active` when the option is off. They accumulate over
// the whole window and are reset with it, unlike the per-frame Frame sample.

// Draws per bound (vertex, pixel) program pair, keyed by the proxy's program
// hashes (zero means no program: fixed function or an unregistered program).
// A fixed open-addressed table: one mix, at most eight probes and one
// increment per draw; a pair that finds no slot is counted in overflow().
inline constexpr unsigned draw_pair_slots = 64; // power of two
inline constexpr unsigned draw_pair_top_count = 8;

class DrawPairs {
public:
    struct Pair { std::uint64_t vs = 0, ps = 0, draws = 0; };

    // The two qualified cutout pairs, in report order: hull then station.
    static constexpr unsigned cutout_pair_count = 2;

    void record(std::uint64_t vs, std::uint64_t ps) noexcept {
        ++draws_;
        // Counted before and independently of the table: a late-arriving pair
        // can land in overflow_, so a zero drawn from the table would not
        // prove the pair never drew. These two counters always do.
        if (vs == cutout::pair_hashes[2] && ps == cutout::pair_hashes[3]) ++cutout_[0];
        else if (vs == cutout::pair_hashes[0] && ps == cutout::pair_hashes[1]) ++cutout_[1];
        const unsigned mix = static_cast<unsigned>(vs ^ (vs >> 32) ^ ps ^ (ps >> 17));
        for (unsigned probe = 0; probe < 8; ++probe) {
            Pair& slot = slots_[(mix + probe) & (draw_pair_slots - 1)];
            if (slot.draws && slot.vs == vs && slot.ps == ps) { ++slot.draws; return; }
            if (!slot.draws) { slot.vs = vs; slot.ps = ps; slot.draws = 1; return; }
        }
        ++overflow_;
    }
    std::uint64_t draws() const noexcept { return draws_; }
    std::uint64_t overflow() const noexcept { return overflow_; }
    // Exact, whatever the table did: 0 is the hull pair, 1 the station pair.
    std::uint64_t cutout_draws(unsigned pair) const noexcept {
        return pair < cutout_pair_count ? cutout_[pair] : 0;
    }
    // Draws counted for one exact pair; zero when the pair never drew.
    std::uint64_t draws_of(std::uint64_t vs, std::uint64_t ps) const noexcept {
        for (const Pair& slot : slots_)
            if (slot.draws && slot.vs == vs && slot.ps == ps) return slot.draws;
        return 0;
    }
    // The most-drawn pairs, descending; ties keep the lower slot. One pass
    // over the fixed table at the window boundary, off the per-draw path.
    unsigned top(Pair* out, unsigned capacity) const noexcept {
        unsigned used = 0;
        for (const Pair& slot : slots_) {
            if (!slot.draws) continue;
            unsigned at = used;
            while (at > 0 && out[at - 1].draws < slot.draws) --at;
            if (at >= capacity) continue;
            for (unsigned i = used < capacity ? used : capacity - 1; i > at; --i) out[i] = out[i - 1];
            out[at] = slot;
            if (used < capacity) ++used;
        }
        return used;
    }
    void reset() noexcept {
        for (Pair& slot : slots_) slot = Pair{};
        for (std::uint64_t& pair : cutout_) pair = 0;
        draws_ = overflow_ = 0;
    }

private:
    Pair slots_[draw_pair_slots]{};
    std::uint64_t cutout_[cutout_pair_count]{};
    std::uint64_t draws_ = 0, overflow_ = 0;
};

// Which shadowed setter a redundancy belongs to.
enum class StateSet : unsigned { RenderState = 0, SamplerState = 1, Texture = 2 };
inline constexpr unsigned state_set_count = 3;
inline constexpr unsigned redundant_entry_slots = 32; // power of two, render states only
inline constexpr unsigned redundant_top_count = 4;

// State writes whose incoming value equals the value the proxy's shadow
// already holds for that entry. Nothing is elided: the native call still
// happens (docs/architecture/state-call-fast-path.md, section (d)); this only
// measures how much of the game's setter traffic is rewriting.
class RedundantStates {
public:
    // `set` selects the hook; `entry` is the D3DRS index for RenderState and
    // is not attributed for the other two. Called only for a write that had a
    // shadowed value to compare against, so shadowed() is the denominator.
    void record(unsigned set, unsigned entry, bool equal) noexcept {
        if (set >= state_set_count) return;
        ++shadowed_[set];
        if (!equal) return;
        ++redundant_[set];
        if (set != static_cast<unsigned>(StateSet::RenderState)) return;
        for (unsigned probe = 0; probe < 8; ++probe) {
            Entry& slot = entries_[(entry + probe) & (redundant_entry_slots - 1)];
            if (slot.count && slot.entry == entry) { ++slot.count; return; }
            if (!slot.count) { slot.entry = entry; slot.count = 1; return; }
        }
        // No slot: the aggregate above still counts it, the attribution does not.
    }
    std::uint64_t redundant(unsigned set) const noexcept { return set < state_set_count ? redundant_[set] : 0; }
    std::uint64_t shadowed(unsigned set) const noexcept { return set < state_set_count ? shadowed_[set] : 0; }
    struct Entry { unsigned entry = 0; std::uint64_t count = 0; };
    unsigned top(Entry* out, unsigned capacity) const noexcept {
        unsigned used = 0;
        for (const Entry& slot : entries_) {
            if (!slot.count) continue;
            unsigned at = used;
            while (at > 0 && out[at - 1].count < slot.count) --at;
            if (at >= capacity) continue;
            for (unsigned i = used < capacity ? used : capacity - 1; i > at; --i) out[i] = out[i - 1];
            out[at] = slot;
            if (used < capacity) ++used;
        }
        return used;
    }
    void reset() noexcept {
        for (unsigned s = 0; s < state_set_count; ++s) redundant_[s] = shadowed_[s] = 0;
        for (Entry& slot : entries_) slot = Entry{};
    }

private:
    std::uint64_t redundant_[state_set_count]{}, shadowed_[state_set_count]{};
    Entry entries_[redundant_entry_slots]{};
};

// What identifies one draw for the batchability counters: the bindings the
// proxy's shadow already holds plus the primitive range. `valid` is false when
// the shadow is not live (motion output off, or a state block recording); such
// a draw is classified as nothing and is not counted.
struct DrawKey {
    std::uint64_t vs = 0, ps = 0;
    std::uint64_t stream0 = 0, indices = 0, declaration = 0; // declaration covers FVF
    std::uint64_t textures[4]{};                             // stage 0..3
    std::uint32_t primitive_type = 0, primitives = 0, start_index = 0;
    std::int32_t base_vertex = 0;
    bool valid = false;
    // A DrawPrimitiveUP/DrawIndexedPrimitiveUP draw. D3D9 clears stream 0 on
    // such a call and the proxy's shadow is not invalidated, so the shadowed
    // buffers say nothing about it: it is counted apart and never batched.
    bool user_memory = false;
};

// Consecutive draws of one frame, compared against the draw before them. The
// three classes are disjoint: same_mesh is an instancing candidate (only the
// constants differ), same_mesh_any_range is the same buffers and material with
// another range, same_material shares shaders and textures across meshes.
class DrawBatch {
public:
    void record(const DrawKey& key) noexcept {
        ++draws_;
        // A user-memory draw is its own count and breaks the chain: neither it
        // nor the draw after it may be compared against a stale stream shadow.
        if (key.user_memory) { ++user_memory_; previous_ = DrawKey{}; return; }
        classify(key);          // compares in place, before the key replaces it
        previous_ = key;
    }
    // A draw is only compared with a draw of the same frame.
    void end_frame() noexcept { previous_ = DrawKey{}; }
    std::uint64_t same_mesh() const noexcept { return same_mesh_; }
    std::uint64_t same_mesh_any_range() const noexcept { return same_mesh_any_range_; }
    std::uint64_t same_material() const noexcept { return same_material_; }
    std::uint64_t user_memory() const noexcept { return user_memory_; }
    std::uint64_t draws() const noexcept { return draws_; }
    void reset() noexcept {
        same_mesh_ = same_mesh_any_range_ = same_material_ = user_memory_ = draws_ = 0;
        previous_ = DrawKey{};
    }

private:
    // No copy of the previous key: the fields are compared where they are.
    void classify(const DrawKey& key) noexcept {
        if (!key.valid || !previous_.valid) return;
        if (key.vs != previous_.vs || key.ps != previous_.ps) return;
        for (unsigned i = 0; i < 4; ++i) if (key.textures[i] != previous_.textures[i]) return;
        if (key.stream0 != previous_.stream0 || key.indices != previous_.indices
            || key.declaration != previous_.declaration) { ++same_material_; return; }
        if (key.primitive_type == previous_.primitive_type && key.primitives == previous_.primitives
            && key.base_vertex == previous_.base_vertex && key.start_index == previous_.start_index) ++same_mesh_;
        else ++same_mesh_any_range_;
    }

    DrawKey previous_{};
    std::uint64_t same_mesh_ = 0, same_mesh_any_range_ = 0, same_material_ = 0, user_memory_ = 0, draws_ = 0;
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
void draw_state_impl(const DrawKey& key) noexcept;
void state_write_impl(unsigned set, unsigned entry, bool equal) noexcept;
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
// One call per hooked draw, from the draw path, with the bindings the proxy
// already shadows: counts the program pair and classifies the draw against the
// previous draw of the frame. The caller builds the key only when `active`.
inline void draw_state(const DrawKey& key) noexcept { if (active) detail::draw_state_impl(key); }
// One call per shadowed state write, from inside the shadow update. `shadowed`
// is false when the shadow holds no current value for the entry; the write is
// then neither a redundancy nor a denominator. Nothing is elided either way.
inline void state_write(StateSet set, unsigned entry, bool shadowed, bool equal) noexcept {
    if (active && shadowed) detail::state_write_impl(static_cast<unsigned>(set), entry, equal);
}

}
