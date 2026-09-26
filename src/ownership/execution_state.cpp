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
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        poison(ExecutionReason::NativeBypass, true);
        return;
    }
    initialized_ = true;
    requested_ = enabled;
    healthy_ = enabled;
    generation_ = enabled ? 1 : 0;
    reason_ = enabled ? ExecutionReason::None : ExecutionReason::Disabled;
}
ExecutionView ObservedExecutionState::view() const noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    ExecutionView v;
    v.requested = requested_;
    v.known = requested_ && healthy_ && !resetting_ && !query_tainted_ && in_flight_ == 0;
    v.scene_open = scene_;
    v.stateblock_recording = recording_;
    v.queries_idle = v.known && active_ == 0;
    v.generation = generation_;
    v.active_queries = active_;
    v.in_flight = in_flight_;
    v.observed_calls = calls_;
    v.refusals = refusals_;
    v.reason = query_tainted_ ? query_reason_ : in_flight_ ? ExecutionReason::NativeCallInFlight : reason_;
    return v;
}
void ObservedExecutionState::count_call() noexcept {
    if (requested_) increment(calls_);
}
void ObservedExecutionState::poison(ExecutionReason reason, bool query_permanent) noexcept {
    if (!requested_) return;
    healthy_ = false;
    increment(refusals_);
    if (reason_ == ExecutionReason::None || reason_ == ExecutionReason::Resetting) reason_ = reason;
    if (query_permanent && !query_tainted_) {
        query_tainted_ = true;
        query_reason_ = reason;
    }
}
void ObservedExecutionState::observe_result_locked(std::int32_t result) noexcept {
    if (device_lost(result)) {
        if (loss_epoch_ == std::numeric_limits<std::uint64_t>::max())
            poison(ExecutionReason::CounterOverflow, true);
        else
            ++loss_epoch_;
        poison(ExecutionReason::DeviceLost, active_ != 0);
    }
}
void ObservedExecutionState::observe_result(std::int32_t result) noexcept {
    // The ordinary setter/draw forwarding path must not acquire a mutex.
    if (!device_lost(result) || !requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    observe_result_locked(result);
}
void ObservedExecutionState::transition(bool& state, bool desired, std::int32_t result) noexcept {
    count_call();
    if (!requested_) return;
    observe_result_locked(result);
    if (result != 0) {
        poison(ExecutionReason::TransitionFailure);
        return;
    }
    if (resetting_ || state == desired) {
        poison(ExecutionReason::ContradictoryTransition);
        return;
    }
    state = desired;
}
void ObservedExecutionState::begin_scene(std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    transition(scene_, true, result);
}
void ObservedExecutionState::end_scene(std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    transition(scene_, false, result);
}
void ObservedExecutionState::begin_stateblock(std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    transition(recording_, true, result);
}
void ObservedExecutionState::end_stateblock(std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    transition(recording_, false, result);
}
void ObservedExecutionState::query_created(ExecutionQuery& q, std::uint32_t type) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    count_call();
    if (!requested_) return;
    const ObservedExecutionState* expected = nullptr;
    q.owner_.compare_exchange_strong(expected, this);
    // Owner is published atomically and never changes during token lifetime.
    // Never inspect non-atomic query fields under another observer's mutex.
    if (q.owner_.load() != this || q.registered_) {
        poison(ExecutionReason::UnknownQuery, true);
        return;
    }
    q.generation_ = generation_;
    q.registered_ = true;
    q.occlusion_ = type == occlusion;
    q.active_ = false;
    if (!q.occlusion_) poison(ExecutionReason::UnsupportedQuery, true);
}
void ObservedExecutionState::query_issue(ExecutionQuery& q, std::uint32_t flags, std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    count_call();
    if (!requested_) return;
    observe_result_locked(result);
    // Lost devices can report successful commands without executing them. Once
    // observation is unhealthy, no Issue result can heal an interval proof.
    if (!healthy_) {
        poison(ExecutionReason::InvalidIssue, true);
        return;
    }
    if (q.owner_.load() != this || !q.registered_) {
        poison(ExecutionReason::UnknownQuery, true);
        return;
    }
    if (!q.occlusion_) {
        poison(ExecutionReason::UnsupportedQuery, true);
        return;
    }
    if (q.generation_ != generation_) {
        // Reset is allowed to preserve only previously idle query objects.
        if (q.active_) {
            poison(ExecutionReason::ActiveQueryReset, true);
            return;
        }
        q.generation_ = generation_;
    }
    if (resetting_ || result != 0 || (flags != issue_begin && flags != issue_end) ||
        (flags == issue_begin ? q.active_ : !q.active_)) {
        poison(ExecutionReason::InvalidIssue, true);
        return;
    }
    if (flags == issue_begin) {
        if (active_ == std::numeric_limits<std::uint64_t>::max()) {
            poison(ExecutionReason::CounterOverflow, true);
            return;
        }
        q.active_ = true;
        ++active_;
    } else {
        if (!active_) {
            poison(ExecutionReason::UnknownQuery, true);
            return;
        }
        q.active_ = false;
        --active_;
    }
}
void ObservedExecutionState::query_destroyed(ExecutionQuery& q) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    count_call();
    if (!requested_) return;
    if (q.owner_.load() != this || !q.registered_) {
        poison(ExecutionReason::UnknownQuery, true);
        return;
    }
    if (q.active_) poison(ExecutionReason::ActiveQueryDestroyed, true);
    q.generation_ = 0;
    q.registered_ = q.occlusion_ = q.active_ = false;
}
void ObservedExecutionState::before_reset() noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    count_call();
    if (!requested_) return;
    if (active_) poison(ExecutionReason::ActiveQueryReset, true);
    if (resetting_) poison(ExecutionReason::NativeBypass, true);
    resetting_ = true;
    healthy_ = false;
    reset_loss_epoch_ = loss_epoch_;
    if (!query_tainted_) reason_ = ExecutionReason::Resetting;
}
void ObservedExecutionState::after_reset(std::int32_t result) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    count_call();
    if (!requested_) return;
    if (!resetting_) {
        poison(ExecutionReason::NativeBypass, true);
        return;
    }
    resetting_ = false;
    if (result != 0) {
        reason_ = ExecutionReason::ResetFailed;
        poison(reason_);
        return;
    }
    if (loss_epoch_ != reset_loss_epoch_) {
        poison(ExecutionReason::DeviceLost, true);
        return;
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        poison(ExecutionReason::CounterOverflow, true);
        return;
    }
    ++generation_;
    scene_ = false;
    recording_ = false;
    // Active query state never becomes trustworthy merely because Reset passed.
    healthy_ = !query_tainted_;
    reason_ = ExecutionReason::None;
}
void ObservedExecutionState::unknown_native_execution() noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    poison(ExecutionReason::NativeBypass, true);
}
ExecutionObservation::ExecutionObservation(ObservedExecutionState& owner) noexcept
    : owner_(&owner) {
    owner_->start_native(*this);
}
ExecutionObservation::~ExecutionObservation() {
    owner_->finish_native(*this);
}
void ExecutionObservation::complete() noexcept {
    owner_->complete_native(*this);
}
ExecutionObservation ObservedExecutionState::begin_native() noexcept {
    return ExecutionObservation(*this);
}
void ObservedExecutionState::start_native(ExecutionObservation& call) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requested_) return;
    if (in_flight_) poison(ExecutionReason::OverlappingNativeCalls, true);
    if (in_flight_ == std::numeric_limits<std::uint64_t>::max()) {
        poison(ExecutionReason::CounterOverflow, true);
        return;
    }
    ++in_flight_;
    call.active_ = true;
}
void ObservedExecutionState::finish_native(ExecutionObservation& call) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!call.active_) return;
    call.active_ = false;
    if (!call.completed_) poison(ExecutionReason::NativeBypass, true);
    if (!in_flight_) {
        poison(ExecutionReason::OverlappingNativeCalls, true);
        return;
    }
    --in_flight_;
}
void ObservedExecutionState::complete_native(ExecutionObservation& call) noexcept {
    if (!requested_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requested_) return;
    if (!call.active_ || call.completed_) {
        poison(ExecutionReason::NativeBypass, true);
        return;
    }
    call.completed_ = true;
}
} // namespace x3m::ownership
