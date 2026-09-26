#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace x3m::ownership {

// Process-wide application-call admission. Production must use one monitor and
// enter BEFORE capture/registry/native locks. This core does not establish that
// all entrypoints or callback routes are covered, and does not preserve Win32
// LastError/FP state: the outer ABI adapter must preserve both around bookkeeping.
class ApplicationAdmission;
class ReplayAdmission;
enum class AdmissionVeto : std::uint32_t {
    None = 0,
    PrivateUnknown = 1,
    UnobservedRoute = 2,
    ExternalResource = 4,
    ForeignRouting = 8,
    SameThreadReentry = 16,
    ScopeOrder = 32,
    CounterExhaustion = 64
};
enum class AdmissionResult {
    Admitted,
    InactiveBoundary,
    NestedBoundary,
    OtherApplications,
    WaitingApplications,
    Vetoed,
    ReplayAlreadyActive,
    SameThreadReentry,
    DifferentMonitor
};
struct AdmissionSnapshot {
    std::uint64_t active_roots = 0, waiting_roots = 0, admitted_roots = 0;
    std::uint64_t promotions = 0;
    std::uint32_t vetoes = 0;
    AdmissionVeto first_veto = AdmissionVeto::None;
    bool replay_active = false;
};
class AdmissionMonitor final {
public:
    AdmissionMonitor() = default;
    AdmissionMonitor(const AdmissionMonitor&) = delete;
    AdmissionMonitor& operator=(const AdmissionMonitor&) = delete;
    // Must precede native registration/escape under application admission.
    // Permanent for the monitor lifetime, including failed registration attempts.
    // A veto never prevents ordinary application work or clears existing scopes.
    void veto(AdmissionVeto);
    AdmissionSnapshot snapshot() const;

private:
    friend class ApplicationAdmission;
    friend class ReplayAdmission;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    AdmissionSnapshot state_;
};

// Same-thread, LIFO scopes; explicit finish supports the child-to-parent final
// Release handoff. Concurrent independent roots are allowed during ordinary
// execution. A new outer root waits only while replay owns exclusivity.
class ApplicationAdmission final {
public:
    explicit ApplicationAdmission(AdmissionMonitor&);
    ~ApplicationAdmission();
    ApplicationAdmission(const ApplicationAdmission&) = delete;
    ApplicationAdmission& operator=(const ApplicationAdmission&) = delete;
    AdmissionResult result() const noexcept { return result_; }
    bool admitted() const noexcept { return active_; }
    bool finish();

private:
    friend class ReplayAdmission;
    AdmissionMonitor& monitor_;
    ApplicationAdmission* previous_ = nullptr;
    std::thread::id thread_;
    AdmissionResult result_ = AdmissionResult::Admitted;
    bool active_ = false;
};

// Nonblocking promotion of the sole outer application boundary. Declare AFTER
// that boundary so replay ends first. No monitor mutex spans native work.
// A same-thread application entry during replay is refused and permanently
// vetoes future promotion. That is an invariant diagnostic, NOT permission to
// synthesize an application HRESULT or forward through the active replay.
// Integration must prove the selected native segment cannot make such an entry.
class ReplayAdmission final {
public:
    explicit ReplayAdmission(ApplicationAdmission&);
    ~ReplayAdmission();
    ReplayAdmission(const ReplayAdmission&) = delete;
    ReplayAdmission& operator=(const ReplayAdmission&) = delete;
    AdmissionResult result() const noexcept { return result_; }
    bool admitted() const noexcept { return active_; }
    bool finish();

private:
    ApplicationAdmission& boundary_;
    std::thread::id thread_;
    AdmissionResult result_ = AdmissionResult::InactiveBoundary;
    bool active_ = false;
};
}
