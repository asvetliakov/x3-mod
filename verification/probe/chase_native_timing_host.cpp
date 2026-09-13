// Host-only state-machine checks for consolidated native chase timings.
#include "chase_native_timing_core.h"
#include <algorithm>
#include <cstdio>

using namespace x3m::chase_lead::native_timing;

static unsigned scenarios = 0, checks = 0, failures = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
static Token token(std::uint32_t thread = 1, std::uint64_t generation = 7,
                   std::uint64_t update = 11, std::uint32_t cockpit = 0x1000) {
    return {generation, update, thread, cockpit, 258, 0x2000};
}
static unsigned live_starts(const State& state) {
    unsigned count = 0;
    for (const auto& slot : state.slots)
        for (const auto& start : slot.starts) count += start.stamp != 0;
    return count;
}

static void four_phases_and_solver_false() {
    ++scenarios; State state;
    for (unsigned phase = 0; phase < phase_count; ++phase) {
        auto begin_token = token();
        auto end_token = begin_token; end_token.mode = 300 + phase; end_token.target = 0x3000 + phase;
        const auto begin = 100ull + phase * 20;
        state.begin(begin_token, phase, 40 + phase, begin);
        state.end(end_token, phase, 40 + phase, begin + 10, phase == 1 ? 0 : 1, 5);
    }
    for (unsigned phase = 0; phase < phase_count; ++phase) {
        const auto& metric = state.window.metrics[phase];
        check(metric.calls == 1 && metric.ticks == 10 && metric.maximum == 10,
              "each native phase records one exact duration");
        check(metric.failures == (phase == 1 ? 1u : 0u),
              "only false solver result increments failure count");
    }
    check(state.window.slow == 4 && state.window.first_used == 4 && state.window.last_used == 0,
          "four slow phases occupy the first-sample set");
    check(state.window.first[1].result == 0 && state.window.first[2].end_mode == 302
              && state.window.first[2].end_target == 0x3002,
          "slow sample retains result and end-state diagnostics");
    check(live_starts(state) == 0, "normal phase pairs release all thread slots");
}

static void unmatched_common_end() {
    ++scenarios; State state;
    state.end(token(), 0, 1, 20, 1, 0);
    check(state.window.unmatched == 1 && state.window.metrics[0].calls == 0,
          "common end without start is unmatched");
}

enum class Mismatch { Generation, Update, Thread, Frame };
static void identity_mismatch(Mismatch mismatch) {
    ++scenarios; State state;
    auto begin_token = token(); auto end_token = begin_token;
    state.begin(begin_token, 0, 9, 10);
    std::uint32_t frame = 9;
    if (mismatch == Mismatch::Generation) ++end_token.generation;
    if (mismatch == Mismatch::Update) ++end_token.update;
    if (mismatch == Mismatch::Thread) ++end_token.thread;
    if (mismatch == Mismatch::Frame) ++frame;
    state.end(end_token, 0, frame, 20, 1, 0);
    if (mismatch == Mismatch::Thread) {
        check(state.window.unmatched == 1 && state.window.abandoned == 0 && live_starts(state) == 1,
              "wrong thread cannot consume another thread's start");
    } else {
        check(state.window.abandoned == 1 && state.window.unmatched == 0 && live_starts(state) == 0,
              "generation/update/frame mismatch consumes as abandoned");
    }
    check(state.window.metrics[0].calls == 0, "identity mismatch records no duration");
}

static void nested_begin_abandons() {
    ++scenarios; State state;
    auto first = token();
    state.begin(first, 0, 1, 10);
    state.begin(first, 0, 1, 20);
    check(state.window.abandoned == 1 && live_starts(state) == 1,
          "nested begin of same phase abandons prior start");
    auto next = first; ++next.update;
    state.begin(next, 1, 2, 30);
    check(state.window.abandoned == 2 && live_starts(state) == 1,
          "new update on same thread abandons remaining old-update span");
}

static void explicit_revoke_paths() {
    ++scenarios; State state;
    const auto destructor = token(1, 7, 11, 0x1000);
    const auto update_exit = token(2, 8, 12, 0x2000);
    state.begin(destructor, 0, 1, 10);
    state.begin(update_exit, 2, 2, 20);
    state.revoke(destructor.cockpit, 0);
    check(state.window.abandoned == 1 && live_starts(state) == 1,
          "cockpit destructor revokes only its matching span");
    state.revoke(0, update_exit.thread);
    check(state.window.abandoned == 2 && live_starts(state) == 0,
          "update-exit thread revoke clears remaining span and slot");
}

static void invalid_and_backwards_clock() {
    ++scenarios; State state;
    auto valid = token(); Token invalid{};
    state.begin(valid, phase_count, 1, 1);
    state.begin(valid, 0, 0, 1);
    state.begin(valid, 0, 1, 0);
    state.begin(invalid, 0, 1, 1);
    state.end(invalid, 0, 1, 2, 1, 0);
    check(live_starts(state) == 0 && state.window.unmatched == 0,
          "invalid phase/token/frame/QPC inputs are ignored");
    state.begin(valid, 0, 1, 20);
    state.end(valid, 0, 1, 19, 1, 0);
    check(state.window.abandoned == 1 && state.window.metrics[0].calls == 0,
          "backwards QPC abandons span without unsigned duration");
}

static void bounded_thread_capacity() {
    ++scenarios; State state;
    for (std::uint32_t thread = 1; thread <= thread_count; ++thread)
        state.begin(token(thread), 0, thread, 10 + thread);
    check(live_starts(state) == thread_count, "all eight timing thread slots admit one span");
    state.begin(token(thread_count + 1), 0, 20, 30);
    check(state.window.overflow == 1 && live_starts(state) == thread_count,
          "ninth timing thread is refused without disturbing live slots");
}

static void slow_sample_ring() {
    ++scenarios; State state;
    for (std::uint32_t index = 0; index < 10; ++index) {
        const auto begin = 1ull + index * 10;
        state.begin(token(), 0, index + 1, begin);
        state.end(token(), 0, index + 1, begin + 5, 1, 5);
    }
    check(state.window.slow == 10 && state.window.first_used == 4
              && state.window.last_used == 4 && state.window.slow - 8 == 2,
          "first-four/last-four slow ring reports two omitted samples");
    check(state.window.first[0].begin == 1 && state.window.first[3].begin == 31,
          "first slow sample set remains stable");
    const std::uint64_t expected[4] = {61, 71, 81, 91};
    bool ordered = true;
    for (unsigned index = 0; index < 4; ++index)
        ordered &= state.window.last[(state.window.next + index) % 4].begin == expected[index];
    check(ordered, "last-four ring is recoverable in chronological order");
}

static void take_preserves_open_spans() {
    ++scenarios; State state;
    state.begin(token(), 0, 1, 10);
    state.begin(token(), 1, 1, 20);
    state.end(token(), 1, 1, 25, 1, 0);
    const auto first = state.take();
    check(first.metrics[1].calls == 1 && state.window.metrics[1].calls == 0,
          "window take returns and clears completed metrics");
    check(live_starts(state) == 1, "window take preserves open spans");
    state.end(token(), 0, 1, 30, 1, 0);
    check(state.window.metrics[0].calls == 1 && state.window.metrics[0].ticks == 20,
          "preserved open span completes into the next window");
}

int main() {
    four_phases_and_solver_false();
    unmatched_common_end();
    for (auto mismatch : {Mismatch::Generation, Mismatch::Update, Mismatch::Thread, Mismatch::Frame})
        identity_mismatch(mismatch);
    nested_begin_abandons();
    explicit_revoke_paths();
    invalid_and_backwards_clock();
    bounded_thread_capacity();
    slow_sample_ring();
    take_preserves_open_spans();
    std::printf("chase_native_timing_host scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
