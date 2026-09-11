#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>

namespace x3m::ownership {
// Observed application execution only. Internal state access is synchronized;
// snapshots are not replay exclusion leases. Native access is a closed-world
// boundary: unseen native application calls cannot be discovered by this core.
enum class ExecutionReason : std::uint8_t {
    None, Disabled, TransitionFailure, ContradictoryTransition, UnsupportedQuery,
    UnknownQuery, InvalidIssue, ActiveQueryDestroyed, DeviceLost, Resetting,
    ResetFailed, NativeBypass, CounterOverflow, ActiveQueryReset, NativeCallInFlight, OverlappingNativeCalls
};
struct ExecutionView {
    bool requested = false, known = false, scene_open = false;
    bool stateblock_recording = false, queries_idle = false;
    std::uint64_t generation = 0, active_queries = 0, in_flight = 0;
    std::uint64_t observed_calls = 0, refusals = 0;
    ExecutionReason reason = ExecutionReason::Disabled;
};
class ObservedExecutionState;
class ExecutionObservation;
// One token per canonical wrapper query; no allocation or native resource held.
class ExecutionQuery {
public:
    ExecutionQuery() = default;
    ExecutionQuery(const ExecutionQuery&) = delete;
    ExecutionQuery& operator=(const ExecutionQuery&) = delete;
private:
    friend class ObservedExecutionState;
    std::atomic<const ObservedExecutionState*> owner_{nullptr};
    std::uint64_t generation_ = 0;
    bool registered_ = false, occlusion_ = false, active_ = false;
};
// Stack-scoped bookkeeping ticket. Neither construction nor destruction holds
// a mutex across native code. Keep it alive until native result processing and
// COM output cleanup finish. Concurrent tickets permanently taint observation.
class ExecutionObservation {
public:
    ~ExecutionObservation();
    // Call only after native result and all output/cleanup bookkeeping completed.
    // An unacknowledged destructor permanently invalidates the observation.
    void complete() noexcept;
    ExecutionObservation(const ExecutionObservation&) = delete;
    ExecutionObservation& operator=(const ExecutionObservation&) = delete;
private:
    friend class ObservedExecutionState;
    explicit ExecutionObservation(ObservedExecutionState&) noexcept;
    ObservedExecutionState* const owner_;
    bool active_ = false, completed_ = false;
};
class ObservedExecutionState {
public:
    ObservedExecutionState() = default;
    ObservedExecutionState(const ObservedExecutionState&) = delete;
    ObservedExecutionState& operator=(const ObservedExecutionState&) = delete;
    // Exactly once at a pristine native device's successful creation. Never use
    // to adopt an existing device or clear a taint on a living device.
    void initialize(bool enabled) noexcept;
    ExecutionView view() const noexcept;
    ExecutionObservation begin_native() noexcept;
    void begin_scene(std::int32_t result) noexcept;
    void end_scene(std::int32_t result) noexcept;
    void begin_stateblock(std::int32_t result) noexcept;
    void end_stateblock(std::int32_t result) noexcept;
    // Call only after a real, successfully created query is wrapped, never for
    // null-output support probes. Numeric types are D3DQUERYTYPE; only 9
    // (occlusion) has qualified interval semantics in this initial policy.
    void query_created(ExecutionQuery&, std::uint32_t type) noexcept;
    void query_issue(ExecutionQuery&, std::uint32_t flags, std::int32_t result) noexcept;
    void query_destroyed(ExecutionQuery&) noexcept;
    void before_reset() noexcept;
    void after_reset(std::int32_t result) noexcept;
    void observe_result(std::int32_t result) noexcept;
    // Permanent for this device lifetime; successful Reset cannot erase an
    // unseen native query scope or restore closed-world interception coverage.
    void unknown_native_execution() noexcept;
private:
    friend class ExecutionObservation;
    void start_native(ExecutionObservation&) noexcept;
    void finish_native(ExecutionObservation&) noexcept;
    void complete_native(ExecutionObservation&) noexcept;
    void observe_result_locked(std::int32_t result) noexcept;
    mutable std::mutex mutex_;
    std::uint64_t in_flight_ = 0, loss_epoch_ = 0, reset_loss_epoch_ = 0;
    void transition(bool& state, bool desired, std::int32_t result) noexcept;
    void poison(ExecutionReason, bool query_permanent = false) noexcept;
    void count_call() noexcept;
    std::atomic<bool> requested_{false}; // set once at pristine initialization
    bool initialized_ = false, healthy_ = false;
    bool scene_ = false, recording_ = false, resetting_ = false;
    bool query_tainted_ = false;
    std::uint64_t generation_ = 0, active_ = 0, calls_ = 0, refusals_ = 0;
    ExecutionReason reason_ = ExecutionReason::Disabled;
    ExecutionReason query_reason_ = ExecutionReason::None;
};
} // namespace x3m::ownership
