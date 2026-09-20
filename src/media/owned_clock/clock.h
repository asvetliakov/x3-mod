#pragma once
#include "exact_time.h"
#include <cstddef>

namespace media_owned {
struct Frame {
    std::uint64_t generation = 0, token = 0;
    std::int64_t start = 0, end = 0; // verified source-coordinate 100 ns ticks
};
enum class End { none, positive, eof, malformed, position_range };
enum class Admission { accepted, stale, inactive, full, malformed, after_eof };
struct Update {
    bool accepted = true, selected = false;
    Frame frame{};
    unsigned consumed = 0, late = 0, superseded = 0;
    End end = End::none;
};

// Single-owner metadata core, not a concurrent queue or a CPU-frame lease owner.
// Every counter-bearing call uses one supplied monotonic counter observation.
class Clock {
public:
    static constexpr unsigned capacity = 3;
    static constexpr std::uint64_t unit_rate = std::uint64_t(1) << 38;
    static_assert(std::uint64_t(INT32_MAX) * 2748779 < (std::uint64_t(1) << 53),
                  "every positive engine rate numerator is exactly binary64");
    explicit Clock(std::uint64_t frequency) : frequency_(frequency),
        denominator_(Wide(frequency).shifted(38)),
        seconds_denominator_(denominator_.times(10000000)) {}

    bool begin(std::uint64_t operation, std::int64_t start,
               std::int32_t end_ms, bool loop, std::uint64_t now) {
        if (!operation || !valid_epoch(start, now)) return false;
        operation_ = operation; start_ = start; end_ms_ = end_ms; loop_ = loop;
        intent_ = true; paused_ = false;
        reset_epoch(now);
        return true;
    }
    bool seek(std::int64_t start, std::int32_t end_ms, std::uint64_t now) {
        if (!operation_ || !valid_epoch(start, now)) return false;
        start_ = start; end_ms_ = end_ms;
        // A stopped seek stays stopped; a paused seek stays paused.
        reset_epoch(now);
        return true;
    }
    bool restart_loop(std::uint64_t now) {
        if (!intent_ || !loop_ || (end_ != End::positive && end_ != End::eof) ||
            !valid_epoch(start_, now)) return false;
        reset_epoch(now); // retain operation, selected rate and playing intent
        return true;
    }
    bool pause(std::uint64_t now) {
        if (!intent_ || end_ != End::none || !advance(now)) return false;
        paused_ = true;
        return true;
    }
    bool resume(std::uint64_t now) {
        if (!intent_ || end_ != End::none || !advance(now)) return false;
        paused_ = false;
        return true;
    }
    bool stop(std::uint64_t now) {
        if (!operation_ || generation_ == UINT64_MAX || !advance(now)) return false;
        intent_ = false; paused_ = false; ++generation_;
        size_ = 0; eof_ = false;
        return true;
    }
    bool set_rate(std::int32_t input, std::uint64_t now) {
        // Rejected nonpositive values do not even consume the supplied counter.
        if (input <= 0 || !frequency_ || !advance(now)) return false;
        rate_ = std::uint64_t(input) * 2748779;
        return true;
    }
    Admission submit(Frame frame) {
        if (frame.generation != generation_) return Admission::stale;
        if (!intent_ || end_ != End::none) return Admission::inactive;
        if (eof_) return Admission::after_eof;
        if (frame.start < 0 || frame.end <= frame.start ||
            (seen_ && frame.start < last_start_)) {
            end_ = End::malformed;
            return Admission::malformed;
        }
        if (size_ == capacity) return Admission::full;
        queue_[size_++] = frame;
        seen_ = true; last_start_ = frame.start;
        if (frame.end > final_end_) final_end_ = frame.end;
        return Admission::accepted;
    }
    bool provider_eof(std::uint64_t generation) {
        if (generation != generation_ || !intent_ || end_ != End::none) return false;
        eof_ = true; // producer contract: every frame published before EOF
        return true;
    }
    Update update(std::uint64_t now) {
        Update out;
        if (!advance(now)) { out.accepted = false; out.end = end_; return out; }
        if (!intent_ || paused_ || end_ != End::none) { out.end = end_; return out; }
        if (!armed_) {
            // Discard genuinely expired preroll. A future first frame arms at
            // requested start, not its PTS; its real gap is then respected.
            while (size_ && compare(queue_[0].end) >= 0) {
                pop(); ++out.consumed; ++out.late;
            }
            if (size_) armed_ = true;
        }
        if (armed_) {
            // Deliberately also on an empty queue, before any new copy decision.
            const auto position = milliseconds();
            if (!position.in_range) end_ = End::position_range;
            else if (end_ms_ > 0 && position.truncated > end_ms_) end_ = End::positive;
            if (end_ != End::none) { out.end = end_; return out; }
            while (size_ && out.consumed < capacity) {
                const Frame frame = queue_[0];
                if (compare(frame.start) < 0) break;
                pop(); ++out.consumed;
                if (compare(frame.end) >= 0) { ++out.late; continue; }
                if (out.selected) ++out.superseded;
                out.selected = true; out.frame = frame;
            }
        }
        if (eof_ && !size_ && (!seen_ || compare(final_end_) >= 0))
            end_ = End::eof;
        out.end = end_;
        return out;
    }
    Milliseconds milliseconds() const { return legacy_ms(position_, seconds_denominator_); }
    int compare(std::int64_t source_ticks) const {
        return position_.compare(denominator_.times(std::uint64_t(source_ticks)));
    }
    const Wide& numerator() const { return position_; }
    std::uint64_t generation() const { return generation_; }
    std::uint64_t operation() const { return operation_; }
    std::uint64_t rate_numerator() const { return rate_; }
    bool armed() const { return armed_; }
    bool intent() const { return intent_; }
    bool paused() const { return paused_; }
    unsigned queued() const { return size_; }
    End end() const { return end_; }

private:
    bool valid_epoch(std::int64_t start, std::uint64_t now) const {
        return frequency_ && start >= 0 && generation_ != UINT64_MAX && now >= last_counter_;
    }
    bool advance(std::uint64_t now) {
        if (!frequency_ || now < last_counter_) return false;
        if (armed_ && intent_ && !paused_ && end_ == End::none)
            position_.add(Wide(now - last_counter_).times(rate_).times(10000000));
        last_counter_ = now;
        return true;
    }
    void reset_epoch(std::uint64_t now) {
        ++generation_; last_counter_ = now; size_ = 0;
        armed_ = false; eof_ = false; seen_ = false;
        end_ = End::none; last_start_ = 0; final_end_ = 0;
        position_ = denominator_.times(std::uint64_t(start_));
    }
    void pop() {
        for (unsigned i = 1; i < size_; ++i) queue_[i - 1] = queue_[i];
        --size_;
    }
    std::uint64_t frequency_ = 0, last_counter_ = 0;
    Wide denominator_, seconds_denominator_, position_;
    std::uint64_t rate_ = unit_rate, generation_ = 0, operation_ = 0;
    std::int64_t start_ = 0, last_start_ = 0, final_end_ = 0;
    std::int32_t end_ms_ = 0;
    bool intent_ = false, paused_ = false, loop_ = false;
    bool armed_ = false, eof_ = false, seen_ = false;
    End end_ = End::none;
    unsigned size_ = 0;
    std::array<Frame, capacity> queue_{};
};
} // namespace media_owned
