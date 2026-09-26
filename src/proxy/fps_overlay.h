#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace x3m {
// Frame-rate accumulator behind the FPS overlay (comparison-hotkeys.md, "FPS
// overlay"). Pure arithmetic on QPC ticks the caller reads: no OS calls, D3D
// objects or allocations. Four 250 ms buckets form a one-second sliding
// window; the text is refreshed when a bucket closes, so a frame costs three
// integer adds and one compare, and an unrequested overlay costs one branch.
// On for the whole session when requested (no key since 2026-09-26).
class FpsOverlay {
public:
    static constexpr unsigned bucket_count = 4, line_capacity = 40;
    void configure(bool requested, std::uint64_t frequency) noexcept {
        requested_ = requested;
        frequency_ = frequency ? frequency : 1;
        reset();
    }
    bool requested() const noexcept { return requested_; }
    bool visible() const noexcept { return requested_; }
    // One call per presented frame with the QPC stamp and the frame's draw
    // count. True when line() changed (each bucket close, about 250 ms).
    bool frame(std::uint64_t now, std::uint64_t draws) noexcept {
        if (!visible()) return false; // the caller gates on visible() too; unrequested is inert either way
        if (!primed_) {
            primed_ = true;
            last_ = bucket_start_ = now;
            return false;
        }
        const std::uint64_t dt = now >= last_ ? now - last_ : 0;
        last_ = now;
        Bucket& current = buckets_[head_];
        ++current.frames;
        current.draws += draws;
        current.ticks += dt;
        if (now < bucket_start_ || now - bucket_start_ < frequency_ / 4) return false;
        std::uint64_t frames = 0, sum_draws = 0, ticks = 0;
        for (const Bucket& bucket : buckets_) {
            frames += bucket.frames;
            sum_draws += bucket.draws;
            ticks += bucket.ticks;
        }
        head_ = (head_ + 1) % bucket_count;
        buckets_[head_] = Bucket{};
        bucket_start_ = now;
        if (!frames || !ticks) return false;
        const double seconds = double(ticks) / double(frequency_);
        format(line_, sizeof line_, double(frames) / seconds, seconds * 1000.0 / double(frames),
               (sum_draws + frames / 2) / frames);
        return true;
    }
    const char* line() const noexcept { return line_; }
    // The second line's volumetric fog state (MotionOutput::volumetric_fog_overlay_state; -1 none), compared each
    // shown frame so a change rewrites the text the same frame: true when it differs from the last written state.
    bool fog(int state) noexcept {
        if (state == fog_) return false;
        fog_ = state;
        return true;
    }
    // The draw's outcome each shown frame. A failure keeps the mode on (the
    // next frame retries); true only at the start of a failure episode, so
    // the caller logs once until a draw succeeds again.
    bool draw_outcome(bool failed) noexcept {
        const bool first = failed && !draw_failed_;
        draw_failed_ = failed;
        return first;
    }
    bool draw_failed() const noexcept { return draw_failed_; }
    // Device Reset and configure: the window and the text go, the overlay stays on.
    void reset() noexcept {
        for (Bucket& bucket : buckets_) bucket = Bucket{};
        head_ = 0;
        last_ = bucket_start_ = 0;
        primed_ = false;
        line_[0] = '\0';
        fog_ = -2;
        draw_failed_ = false;
    }
    // "FPS 61.3  16.3 MS  DRAWS 638": the ms figure is the Present-to-Present
    // interval, not GPU time. Uppercase only (the notice glyph set); values
    // clamp so the line always fits the notice's 36 columns.
    static void format(char* out, std::size_t capacity, double fps, double ms, std::uint64_t draws) noexcept {
        const double clamp = 9999.9;
        if (!(fps >= 0.0)) fps = 0.0;
        if (fps > clamp) fps = clamp;
        if (!(ms >= 0.0)) ms = 0.0;
        if (ms > clamp) ms = clamp;
        if (draws > 999999u) draws = 999999u;
        std::snprintf(out, capacity, "FPS %.1f  %.1f MS  DRAWS %llu", fps, ms, static_cast<unsigned long long>(draws));
    }

private:
    struct Bucket {
        std::uint64_t frames = 0, draws = 0, ticks = 0;
    };
    Bucket buckets_[bucket_count]{};
    unsigned head_ = 0;
    int fog_ = -2; // last written fog state of the second line; -2 = nothing written since reset
    std::uint64_t frequency_ = 1, last_ = 0, bucket_start_ = 0;
    bool requested_ = false, primed_ = false, draw_failed_ = false;
    char line_[line_capacity]{};
};
} // namespace x3m
