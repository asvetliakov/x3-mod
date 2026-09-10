#include "execution_state.h"
#include <limits>
namespace x3m::ownership {
namespace {
constexpr std::uint32_t occlusion = 9, issue_end = 1, issue_begin = 2;
bool device_lost(std::int32_t hr) noexcept {
    const auto bits = static_cast<std::uint32_t>(hr);
    return bits == 0x88760868u || bits == 0x88760869u;
}
void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
}
}
void ObservedExecutionState::initialize(bool enabled) noexcept {
    if (initialized_) { unknown_native_execution(); return; }
    initialized_ = true; requested_ = enabled; healthy_ = enabled;
    generation_ = enabled ? 1 : 0;
    reason_ = enabled ? ExecutionReason::None : ExecutionReason::Disabled;
}
ExecutionView ObservedExecutionState::view() const noexcept {
    ExecutionView v;
    v.requested = requested_; v.known = requested_ && healthy_ && !resetting_ && !query_tainted_;
    v.scene_open = scene_; v.stateblock_recording = recording_;
    v.queries_idle = v.known && active_ == 0;
    v.generation = generation_; v.active_queries = active_;
    v.observed_calls = calls_; v.refusals = refusals_;
    v.reason = query_tainted_ ? query_reason_ : reason_;
    return v;
}
void ObservedExecutionState::count_call() noexcept { if (requested_) increment(calls_); }
void ObservedExecutionState::poison(ExecutionReason reason, bool query_permanent) noexcept {
    if (!requested_) return;
    healthy_ = false; increment(refusals_);
    if (reason_ == ExecutionReason::None || reason_ == ExecutionReason::Resetting) reason_ = reason;
    if (query_permanent && !query_tainted_) { query_tainted_ = true; query_reason_ = reason; }
}
void ObservedExecutionState::observe_result(std::int32_t result) noexcept {
    if (device_lost(result)) poison(ExecutionReason::DeviceLost, active_ != 0);
}
void ObservedExecutionState::transition(bool& state, bool desired, std::int32_t result) noexcept {
    count_call(); if (!requested_) return;
    observe_result(result);
    if (result != 0) { poison(ExecutionReason::TransitionFailure); return; }
    if (resetting_ || state == desired) { poison(ExecutionReason::ContradictoryTransition); return; }
    state = desired;
}
void ObservedExecutionState::begin_scene(std::int32_t result) noexcept { transition(scene_, true, result); }
void ObservedExecutionState::end_scene(std::int32_t result) noexcept { transition(scene_, false, result); }
void ObservedExecutionState::begin_stateblock(std::int32_t result) noexcept { transition(recording_, true, result); }
void ObservedExecutionState::end_stateblock(std::int32_t result) noexcept { transition(recording_, false, result); }
void ObservedExecutionState::query_created(ExecutionQuery& q, std::uint32_t type) noexcept {
    count_call(); if (!requested_) return;
    if (q.registered_) { poison(ExecutionReason::UnknownQuery, true); return; }
    q.owner_ = this; q.generation_ = generation_; q.registered_ = true;
    q.occlusion_ = type == occlusion; q.active_ = false;
    if (!q.occlusion_) poison(ExecutionReason::UnsupportedQuery, true);
}
void ObservedExecutionState::query_issue(ExecutionQuery& q, std::uint32_t flags, std::int32_t result) noexcept {
    count_call(); if (!requested_) return;
    observe_result(result);
    // Lost devices can report successful commands without executing them. Once
    // observation is unhealthy, no Issue result can heal an interval proof.
    if (!healthy_) { poison(ExecutionReason::InvalidIssue, true); return; }
    if (!q.registered_ || q.owner_ != this) { poison(ExecutionReason::UnknownQuery, true); return; }
    if (!q.occlusion_) { poison(ExecutionReason::UnsupportedQuery, true); return; }
    if (q.generation_ != generation_) {
        // Reset is allowed to preserve only previously idle query objects.
        if (q.active_) { poison(ExecutionReason::ActiveQueryReset, true); return; }
        q.generation_ = generation_;
    }
    if (resetting_ || result != 0 || (flags != issue_begin && flags != issue_end) ||
        (flags == issue_begin ? q.active_ : !q.active_)) {
        poison(ExecutionReason::InvalidIssue, true); return;
    }
    if (flags == issue_begin) {
        if (active_ == std::numeric_limits<std::uint64_t>::max()) {
            poison(ExecutionReason::CounterOverflow, true); return;
        }
        q.active_ = true; ++active_;
    } else {
        if (!active_) { poison(ExecutionReason::UnknownQuery, true); return; }
        q.active_ = false; --active_;
    }
}
void ObservedExecutionState::query_destroyed(ExecutionQuery& q) noexcept {
    count_call(); if (!requested_) return;
    if (!q.registered_ || q.owner_ != this) { poison(ExecutionReason::UnknownQuery, true); return; }
    if (q.active_) poison(ExecutionReason::ActiveQueryDestroyed, true);
    q.owner_ = nullptr; q.generation_ = 0;
    q.registered_ = q.occlusion_ = q.active_ = false;
}
void ObservedExecutionState::before_reset() noexcept {
    count_call(); if (!requested_) return;
    if (active_) poison(ExecutionReason::ActiveQueryReset, true);
    if (resetting_) poison(ExecutionReason::NativeBypass, true);
    resetting_ = true; healthy_ = false;
    if (!query_tainted_) reason_ = ExecutionReason::Resetting;
}
void ObservedExecutionState::after_reset(std::int32_t result) noexcept {
    count_call(); if (!requested_) return;
    if (!resetting_) { poison(ExecutionReason::NativeBypass, true); return; }
    resetting_ = false;
    if (result != 0) { reason_ = ExecutionReason::ResetFailed; poison(reason_); return; }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        poison(ExecutionReason::CounterOverflow, true); return;
    }
    ++generation_; scene_ = false; recording_ = false;
    // Active query state never becomes trustworthy merely because Reset passed.
    healthy_ = !query_tainted_; reason_ = ExecutionReason::None;
}
void ObservedExecutionState::unknown_native_execution() noexcept { poison(ExecutionReason::NativeBypass, true); }
} // namespace x3m::ownership
