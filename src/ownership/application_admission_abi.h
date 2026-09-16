#pragma once
#include "application_admission.h"
#include <atomic>
#include <new>
#include <type_traits>

namespace x3m::ownership {

// Win32 x86 bookkeeping boundary for the portable admission core. A null
// monitor disables observation: no core scope, lock, TLS entry or FP save occurs.
// Production must use one process-wide monitor and cover every required route;
// a null monitor is an option-off path, not an exemption for nested callbacks.
class ApplicationAdmissionAbi final {
public:
    explicit ApplicationAdmissionAbi(AdmissionMonitor* monitor) noexcept;
    ~ApplicationAdmissionAbi() noexcept;
    ApplicationAdmissionAbi(const ApplicationAdmissionAbi&)=delete;
    ApplicationAdmissionAbi& operator=(const ApplicationAdmissionAbi&)=delete;
    bool requested() const noexcept {return requested_;}
    bool admitted() const noexcept {return present_&&core()->admitted();}
    // Constructor decision only: it can remain Admitted after finish.
    // admitted()/boundary() report current activity.
    // Disabled returns InactiveBoundary, never an invented successful admission.
    // The ordinary option-off call may proceed; callers distinguish requested().
    AdmissionResult result() const noexcept {return result_;}
    // Finish AND core destruction happen within one independent state guard.
    // Supports child-to-parent final Release handoff; repeat finish is harmless.
    bool finish();
    // Borrow only while this same-thread scope is active. Do not retain across
    // finish/destruction or invoke raw core bookkeeping without its own guard.
    ApplicationAdmission* boundary() noexcept;
private:
    bool requested_=false,present_=false;
    std::thread::id thread_;
    AdmissionResult result_=AdmissionResult::InactiveBoundary;
    alignas(ApplicationAdmission) unsigned char storage_[sizeof(ApplicationAdmission)];
    ApplicationAdmission* core() noexcept {return std::launder(reinterpret_cast<ApplicationAdmission*>(storage_));}
    const ApplicationAdmission* core() const noexcept {return std::launder(reinterpret_cast<const ApplicationAdmission*>(storage_));}
};

class ReplayAdmissionAbi final {
public:
    explicit ReplayAdmissionAbi(ApplicationAdmissionAbi& application);
    // Allows a later Clear bridge to use a borrowed active application boundary.
    // The owning application adapter must remain alive until replay is finished.
    explicit ReplayAdmissionAbi(ApplicationAdmission* boundary);
    ~ReplayAdmissionAbi();
    ReplayAdmissionAbi(const ReplayAdmissionAbi&)=delete;
    ReplayAdmissionAbi& operator=(const ReplayAdmissionAbi&)=delete;
    bool admitted() const noexcept {return present_&&core()->admitted();}
    AdmissionResult result() const noexcept {return result_;}
    bool finish();
private:
    bool requested_=false,present_=false;
    std::thread::id thread_;
    AdmissionResult result_=AdmissionResult::InactiveBoundary;
    alignas(ReplayAdmission) unsigned char storage_[sizeof(ReplayAdmission)];
    ReplayAdmission* core() noexcept {return std::launder(reinterpret_cast<ReplayAdmission*>(storage_));}
    const ReplayAdmission* core() const noexcept {return std::launder(reinterpret_cast<const ReplayAdmission*>(storage_));}
};

void admission_veto(AdmissionMonitor* monitor,AdmissionVeto reason);
AdmissionSnapshot admission_snapshot(const AdmissionMonitor* monitor);

// Shared by loader, capture, ownership and loading hooks. Reads X3M_ADMISSION=1
// once before their first admitted operation; disabled returns null. Cold config
// preserves ordinary-return CPU state; the hot path only reads published state.
// Do not call from DllMain. Obtaining this pointer does not establish admission
// or certify startup/callback/window coverage for live replay.
AdmissionMonitor* process_admission_monitor() noexcept;
namespace detail {
// The configured selection, published once by process_admission_monitor() and
// never replaced. Read only through the accessor below.
extern std::atomic<bool> monitor_published;
extern AdmissionMonitor* published_monitor;
}
// Same answer as process_admission_monitor() with no cross-unit call once the
// configuration is published: one acquire load and one pointer load. Touches no
// x87/MXCSR/LastError state on that path, and falls back to the out-of-line
// configuration (which preserves CPU state) before the first publication.
// A null result is the option-off path: the adapters below then do nothing.
inline AdmissionMonitor* process_admission_monitor_published() noexcept {
    if (detail::monitor_published.load(std::memory_order_acquire)) return detail::published_monitor;
    return process_admission_monitor();
}

static_assert(std::is_trivially_destructible_v<std::thread::id>);

// On ordinary return, every enabled constructor, finish/destructor, veto and snapshot independently
// preserves complete x87 transport (108-byte FNSAVE/FRSTOR), MXCSR and LastError.
// Volatile XMM register values follow the ordinary C++ calling ABI. No saved
// state spans application/native work; a later boundary preserves its NEW state.
// Inline core lifetime is explicit, with trivial outer storage and no implicit
// optional/member teardown after restore. This translation unit requires
// -fno-exceptions (enforced at compilation), while the core remains exception-
// enabled. Exceptions crossing this shell have no supported recovery contract;
// CPU-state restoration is promised only for ordinary return. A future enclosing hook needs its own emitted-code
// audit: this standalone adapter does not certify that hook's EH prologue.
// The core's monitor lifetime, same-thread and LIFO scope restrictions apply.
// If finish() fails, keep the object alive and correct scope/thread order before
// retrying; destroying an active out-of-order scope violates the core contract.
// Refusal is an invariant result, never permission to invent an HRESULT, skip
// an application call or forward it through active replay. No TLS bypass exists.
}
