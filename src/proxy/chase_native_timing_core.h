#pragma once
#include <cstdint>

// Fixed diagnostic state only. No owning pointers, native calls or allocations.
namespace x3m::chase_lead::native_timing {
constexpr unsigned phase_count = 4, thread_count = 8;
struct Token {
    std::uint64_t generation = 0, update = 0;
    std::uint32_t thread = 0, cockpit = 0, mode = 0, target = 0;
    bool valid() const { return generation && update && thread && cockpit; }
};
inline bool same(const Token &a, const Token &b) {
    return a.valid() && b.valid() && a.generation == b.generation && a.update == b.update &&
           a.thread == b.thread && a.cockpit == b.cockpit;
}
struct Start { std::uint64_t stamp = 0; std::uint32_t frame = 0; Token token{}; };
struct Slot { std::uint32_t thread = 0; Start starts[phase_count]{}; };
struct Metric { std::uint64_t calls = 0, ticks = 0, maximum = 0, failures = 0; };
struct Sample {
    Token token{};
    std::uint64_t begin = 0, end = 0;
    std::uint32_t phase = 0, result = 0, end_mode = 0, end_target = 0;
};
struct Window {
    Metric metrics[phase_count]{};
    std::uint64_t abandoned = 0, unmatched = 0, overflow = 0, slow = 0;
    Sample first[4]{}, last[4]{};
    unsigned first_used = 0, last_used = 0, next = 0;
};
struct State {
    Slot slots[thread_count]{};
    Window window{};
    void revoke(std::uint32_t cockpit, std::uint32_t thread) {
        for (auto &slot : slots) {
            for (auto &start : slot.starts)
                if (start.stamp && (cockpit ? start.token.cockpit == cockpit : slot.thread == thread)) {
                    ++window.abandoned;
                    start = {};
                }
            bool live = false;
            for (const auto &start : slot.starts) live |= start.stamp != 0;
            if (!live) slot.thread = 0;
        }
    }
    void begin(const Token &token, unsigned phase, std::uint32_t frame, std::uint64_t stamp) {
        if (!token.valid() || phase >= phase_count || !frame || !stamp) return;
        Slot *found = nullptr;
        for (auto &slot : slots) if (slot.thread == token.thread) { found = &slot; break; }
        if (!found) for (auto &slot : slots) if (!slot.thread) { found = &slot; break; }
        if (!found) { ++window.overflow; return; }
        for (auto &start : found->starts)
            if (start.stamp && !same(start.token, token)) { ++window.abandoned; start = {}; }
        auto &start = found->starts[phase];
        if (start.stamp) ++window.abandoned;
        found->thread = token.thread;
        start = {stamp, frame, token};
    }
    void end(const Token &token, unsigned phase, std::uint32_t frame, std::uint64_t stamp,
             std::uint32_t result, std::uint64_t slow_threshold) {
        if (!token.valid() || phase >= phase_count) return;
        Slot *found = nullptr;
        for (auto &slot : slots) if (slot.thread == token.thread) { found = &slot; break; }
        if (!found || !found->starts[phase].stamp) { ++window.unmatched; return; }
        auto start = found->starts[phase];
        found->starts[phase] = {};
        bool live = false;
        for (const auto &remaining : found->starts) live |= remaining.stamp != 0;
        if (!live) found->thread = 0;
        if (!same(start.token, token) || start.frame != frame || stamp < start.stamp) {
            ++window.abandoned;
            return;
        }
        const auto ticks = stamp - start.stamp;
        auto &metric = window.metrics[phase];
        ++metric.calls;
        metric.ticks += ticks;
        if (ticks > metric.maximum) metric.maximum = ticks;
        if (!result) ++metric.failures;
        if (!slow_threshold || ticks < slow_threshold) return;
        const Sample sample{start.token, start.stamp, stamp, phase, result, token.mode, token.target};
        ++window.slow;
        if (window.first_used < 4) window.first[window.first_used++] = sample;
        else {
            window.last[window.next] = sample;
            window.next = (window.next + 1) % 4;
            if (window.last_used < 4) ++window.last_used;
        }
    }
    Window take() { const auto out = window; window = {}; return out; }
};
} // namespace x3m::chase_lead::native_timing
