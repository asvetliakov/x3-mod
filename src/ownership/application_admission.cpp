#include "application_admission.h"
#include <limits>

namespace x3m::ownership {
namespace {
struct LocalAdmission {
    AdmissionMonitor* monitor = nullptr;
    ApplicationAdmission* top = nullptr;
    ReplayAdmission* replay = nullptr;
};
thread_local LocalAdmission local;
}
void AdmissionMonitor::veto(AdmissionVeto reason) {
    if (reason == AdmissionVeto::None) return;
    std::lock_guard<std::mutex> lock(mutex_);
    state_.vetoes |= static_cast<std::uint32_t>(reason);
    if (state_.first_veto == AdmissionVeto::None) state_.first_veto = reason;
}
AdmissionSnapshot AdmissionMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
ApplicationAdmission::ApplicationAdmission(AdmissionMonitor& monitor)
    : monitor_(monitor)
    , thread_(std::this_thread::get_id()) {
    if (local.monitor && local.monitor != &monitor_) {
        result_ = AdmissionResult::DifferentMonitor;
        monitor_.veto(AdmissionVeto::UnobservedRoute);
        local.monitor->veto(AdmissionVeto::UnobservedRoute);
        return;
    }
    if (local.replay) {
        result_ = AdmissionResult::SameThreadReentry;
        monitor_.veto(AdmissionVeto::SameThreadReentry);
        return;
    }
    previous_ = local.top;
    if (!previous_) {
        std::unique_lock<std::mutex> lock(monitor_.mutex_);
        auto& state = monitor_.state_;
        if (state.replay_active) {
            ++state.waiting_roots;
            monitor_.changed_.wait(lock, [&] { return !state.replay_active; });
            --state.waiting_roots;
        }
        // Saturation is unreachable for live OS thread counts. Never wrap the
        // historical counter into a misleading value or re-enable promotion.
        if (state.admitted_roots == std::numeric_limits<std::uint64_t>::max()) {
            state.vetoes |= static_cast<std::uint32_t>(AdmissionVeto::CounterExhaustion);
            if (state.first_veto == AdmissionVeto::None) state.first_veto = AdmissionVeto::CounterExhaustion;
        } else
            ++state.admitted_roots;
        ++state.active_roots;
    }
    local.monitor = &monitor_;
    local.top = this;
    active_ = true;
}
ApplicationAdmission::~ApplicationAdmission() {
    finish();
}
bool ApplicationAdmission::finish() {
    if (thread_ != std::this_thread::get_id()) return false;
    if (!active_) return true;
    if (local.top != this || local.replay) {
        monitor_.veto(AdmissionVeto::ScopeOrder);
        return false;
    }
    local.top = previous_;
    active_ = false;
    if (!previous_) {
        local.monitor = nullptr;
        std::lock_guard<std::mutex> lock(monitor_.mutex_);
        --monitor_.state_.active_roots;
    }
    return true;
}
ReplayAdmission::ReplayAdmission(ApplicationAdmission& boundary)
    : boundary_(boundary)
    , thread_(std::this_thread::get_id()) {
    if (boundary.thread_ != thread_ || !boundary.active_) return;
    if (local.replay) {
        result_ = AdmissionResult::ReplayAlreadyActive;
        return;
    }
    if (local.top != &boundary || boundary.previous_) {
        result_ = AdmissionResult::NestedBoundary;
        return;
    }
    std::lock_guard<std::mutex> lock(boundary.monitor_.mutex_);
    auto& state = boundary.monitor_.state_;
    if (state.vetoes) {
        result_ = AdmissionResult::Vetoed;
        return;
    }
    if (state.active_roots != 1) {
        result_ = AdmissionResult::OtherApplications;
        return;
    }
    // An owner finishing one replay must not repeatedly beat awakened callers
    // to another promotion. Ordinary application work gets the next interval.
    if (state.waiting_roots) {
        result_ = AdmissionResult::WaitingApplications;
        return;
    }
    if (state.promotions == std::numeric_limits<std::uint64_t>::max()) {
        state.vetoes |= static_cast<std::uint32_t>(AdmissionVeto::CounterExhaustion);
        if (state.first_veto == AdmissionVeto::None) state.first_veto = AdmissionVeto::CounterExhaustion;
        result_ = AdmissionResult::Vetoed;
        return;
    }
    state.replay_active = true;
    ++state.promotions;
    local.replay = this;
    active_ = true;
    result_ = AdmissionResult::Admitted;
}
ReplayAdmission::~ReplayAdmission() {
    finish();
}
bool ReplayAdmission::finish() {
    if (thread_ != std::this_thread::get_id()) return false;
    if (!active_) return true;
    if (local.replay != this) return false;
    {
        std::lock_guard<std::mutex> lock(boundary_.monitor_.mutex_);
        boundary_.monitor_.state_.replay_active = false;
        local.replay = nullptr;
        active_ = false;
    }
    boundary_.monitor_.changed_.notify_all();
    return true;
}
}
