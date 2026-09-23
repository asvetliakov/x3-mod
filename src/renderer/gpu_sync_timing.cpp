#include "gpu_sync_timing.h"
#include "../proxy/cpu_state.h"

namespace x3m::renderer {
namespace {
// Device vtable slots (d3d9.h order): AddRef 1, Release 2, TestCooperativeLevel 3, CreateQuery 118.
enum : unsigned { AddRef = 1, Release = 2, TestCooperativeLevel = 3, CreateQuery = 118 };
using D = IDirect3DDevice9*;
using CountFn = ULONG(WINAPI*)(D);
using CooperativeFn = HRESULT(WINAPI*)(D);
using CreateQueryFn = HRESULT(WINAPI*)(D, D3DQUERYTYPE, IDirect3DQuery9**);
std::uint64_t qpc() noexcept { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return std::uint64_t(t.QuadPart); }
}

HRESULT GpuSyncTiming::attach(IDirect3DDevice9* device, void* const* native, unsigned window) noexcept {
    detach();
    tracker_.configure(window);
    if (!device || !native) { reason_ = "no_device"; return create_result_ = E_INVALIDARG; }
    LARGE_INTEGER f{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) { reason_ = "no_performance_counter"; return create_result_ = E_FAIL; }
    frequency_ = std::uint64_t(f.QuadPart);
    device_ = device; native_ = native;
    return create();
}
HRESULT GpuSyncTiming::create() noexcept {
    if (available_) return S_OK;
    if (tripped_) { reason_ = "sync_timeouts"; return create_result_ = D3DERR_NOTAVAILABLE; } // sticky for the session
    PreserveCpuState guard;
    const auto create_query = reinterpret_cast<CreateQueryFn>(native_[CreateQuery]);
    // Support check (documented: CreateQuery with a null out-pointer returns
    // S_OK or D3DERR_NOTAVAILABLE without creating anything).
    HRESULT hr = create_query(device_, D3DQUERYTYPE_EVENT, nullptr);
    if (hr != S_OK) { reason_ = "event_unsupported"; return create_result_ = FAILED(hr) ? hr : D3DERR_NOTAVAILABLE; }
    const auto add_ref = reinterpret_cast<CountFn>(native_[AddRef]);
    const auto release_fn = reinterpret_cast<CountFn>(native_[Release]);
    add_ref(device_); const ULONG before = release_fn(device_);
    for (unsigned i = 0; i < gpu_sync_timing::boundary_count && SUCCEEDED(hr); ++i) {
        hr = create_query(device_, D3DQUERYTYPE_EVENT, &queries_[i]);
        if (SUCCEEDED(hr) && !queries_[i]) hr = E_POINTER;
    }
    if (FAILED(hr)) { release(); reason_ = "create_failed"; return create_result_ = hr; } // partial creation rolled back
    // The optional census query: unsupported or refused leaves the timing available without it.
    if (create_query(device_, D3DQUERYTYPE_OCCLUSION, nullptr) == S_OK && (FAILED(create_query(device_, D3DQUERYTYPE_OCCLUSION, &census_)) || !census_)) census_ = nullptr;
    add_ref(device_); const ULONG after = release_fn(device_);
    references_ = after > before ? unsigned(after - before) : 0u;
    tracker_.clear_frame(); consecutive_failures_ = 0;
    available_ = true; reason_ = "ok";
    return create_result_ = S_OK;
}
void GpuSyncTiming::release() noexcept {
    PreserveCpuState guard;
    references_ = 0; // before the loop: a query's final Release re-enters the hooked device Release
    for (IDirect3DQuery9*& query : queries_) { IDirect3DQuery9* old = query; query = nullptr; if (old) old->Release(); }
    if (IDirect3DQuery9* old = census_) { census_ = nullptr; old->Release(); }
    census_state_ = Census::Idle;
    tracker_.clear_frame();
    available_ = false; release_pending_ = false; references_ = 0;
}
void GpuSyncTiming::before_reset() noexcept { if (device_) { release(); reason_ = "reset_pending"; } }
void GpuSyncTiming::after_reset(HRESULT reset) noexcept {
    if (!device_ || !native_) return;
    if (FAILED(reset)) { reason_ = "reset_failed"; return; } // still released; the next successful Reset recreates
    create();
}
void GpuSyncTiming::detach() noexcept { release(); device_ = nullptr; native_ = nullptr; reason_ = "detached"; create_result_ = S_FALSE; }

// One boundary: Issue(END) on its event query, then GetData with FLUSH until
// the GPU has retired every command before it. *after is the stamp once it has,
// *wait the spin's duration (the GPU work still pending when the CPU got here).
bool GpuSyncTiming::sync(unsigned boundary, std::uint64_t* after, std::uint64_t* wait) noexcept {
    IDirect3DQuery9* const query = queries_[boundary];
    ++stats_.syncs;
    HRESULT hr = query->Issue(D3DISSUE_END);
    const std::uint64_t start = qpc();
    if (FAILED(hr)) { ++stats_.issue_failures; stats_.last_failure = hr; return false; }
    const std::uint64_t limit = frequency_ * (consecutive_failures_ ? spin_retry_limit_ms : spin_limit_ms) / 1000u;
    for (;;) {
        BOOL done = FALSE;
        hr = query->GetData(&done, sizeof done, D3DGETDATA_FLUSH);
        ++stats_.polls;
        const std::uint64_t now = qpc();
        if (hr == S_OK) { *after = now; *wait = now - start; return true; }
        if (hr != S_FALSE) { ++stats_.data_failures; stats_.last_failure = hr; return false; } // D3DERR_DEVICELOST and any other refusal
        if (now - start > limit) { ++stats_.timeouts; stats_.last_failure = S_FALSE; return false; }
    }
}
void GpuSyncTiming::mark(unsigned pass, bool begin) noexcept {
    if (begin ? !tracker_.wants_begin(pass) : !tracker_.wants_end(pass)) return;
    PreserveCpuState guard; // the caller's x87/MXCSR state and LastError, as the passes keep them
    if (begin && pass == gpu_sync_timing::Scene) {
        // Once per frame (Scene opens once): a device that is not D3D_OK (lost,
        // not reset) abandons the frame with no Issue and no spin.
        const HRESULT cooperative = reinterpret_cast<CooperativeFn>(native_[TestCooperativeLevel])(device_);
        if (cooperative != D3D_OK) { ++stats_.not_cooperative; ++stats_.dropped_frames; tracker_.abandon_frame(); return; }
    }
    std::uint64_t after = 0, wait = 0;
    if (!sync(2u * pass + (begin ? 0u : 1u), &after, &wait)) {
        tracker_.abandon_frame(); ++stats_.dropped_frames;
        // Repeated failures (a lost device, a driver that never signals) switch
        // the boundaries off now; the queries are released at the frame's end by
        // the owner (release_deferred), never here inside a pass: their final
        // Release re-enters the hooked device Release while references() still
        // counts them. Timeouts add up over the session into a sticky cut-off.
        const bool timeouts = stats_.timeouts >= spin_failure_limit;
        if (++consecutive_failures_ >= spin_failure_limit || timeouts) {
            available_ = false; release_pending_ = true; create_result_ = stats_.last_failure;
            reason_ = timeouts ? "sync_timeouts" : "sync_failures";
            if (timeouts) tripped_ = true;
        }
        return;
    }
    consecutive_failures_ = 0;
    if (begin) tracker_.begin(pass, after); else tracker_.end(pass, after, wait);
}
void GpuSyncTiming::census_begin() noexcept {
    if (!available_ || !census_ || census_state_ != Census::Idle || tracker_.abandoned()) return;
    PreserveCpuState guard;
    census_state_ = SUCCEEDED(census_->Issue(D3DISSUE_BEGIN)) ? Census::Open : Census::Done;
}
void GpuSyncTiming::census_end(std::uint32_t area) noexcept {
    if (census_state_ != Census::Open || !census_) return; // closes a begun bracket even after the cut-off switched the boundaries off
    PreserveCpuState guard;
    census_state_ = SUCCEEDED(census_->Issue(D3DISSUE_END)) ? Census::Issued : Census::Done;
    census_area_ = area;
}
bool GpuSyncTiming::frame(std::uint64_t frame, gpu_sync_timing::Report* report) noexcept {
    if (!available_) return false;
    if (census_state_ == Census::Issued && !tracker_.abandoned()) {
        // The Present pair's end sync retired every command before it: the count is ready, no FLUSH, no spin.
        PreserveCpuState guard;
        DWORD pixels = 0;
        const HRESULT hr = census_->GetData(&pixels, sizeof pixels, 0);
        tracker_.census(hr == S_OK ? std::uint32_t(pixels) : 0u, census_area_,
                        hr == S_OK ? gpu_sync_timing::CensusOk : hr == S_FALSE ? gpu_sync_timing::CensusNotReady
                        : hr == D3DERR_DEVICELOST ? gpu_sync_timing::CensusLost : gpu_sync_timing::CensusFailed);
    }
    census_state_ = Census::Idle;
    if (!tracker_.frame(frame, qpc(), frequency_)) return false;
    const gpu_sync_timing::Report r = tracker_.report();
    if (report) *report = r;
    return report != nullptr;
}
} // namespace x3m::renderer
