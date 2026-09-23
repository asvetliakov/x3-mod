#pragma once
// Serialised GPU pass timing (X3M_GPU_SYNC_TIMING=1, launcher
// --gpu-sync-timing; docs/architecture/engine-frame-time.md, "GPU sync
// timing"). Diagnostic for one flight: it serialises CPU and GPU at every pass
// boundary. Documented D3D9 only: one IDirect3DQuery9 of D3DQUERYTYPE_EVENT
// per boundary (gpu_sync_timing::boundary_count, created once through the
// device's native CreateQuery and reused every frame), Issue(D3DISSUE_END)
// then GetData(D3DGETDATA_FLUSH) until S_OK, the wait measured with
// QueryPerformanceCounter. The frame's Scene begin first asks the native
// TestCooperativeLevel: anything but D3D_OK abandons the frame without a spin
// (a lost device never reaches GetData). A failed Issue/GetData or a spin past
// the limit drops the frame. spin_failure_limit consecutive failures
// switch the boundaries off at once and ask for the queries' release, which
// the owner performs at the frame's end under its reference-accounting guard
// (release_deferred: a query's final Release drops a device reference, which
// re-enters the hooked device Release); a successful Reset recreates them.
// spin_failure_limit timeouts in the session trip a sticky cut-off instead
// (no recreation, even after Reset): a driver that times out now and then
// costs at most that many spin_limit_ms hitches. Queries are device
// resources: released in before_reset/detach, recreated after a successful
// Reset. CreateQuery refused at attach: available() false, nothing held, and
// the caller drops the object (no per-frame work). Each boundary preserves the
// caller's x87/MXCSR state and LastError (PreserveCpuState). No heap after the
// object exists.
#include <d3d9.h>
#include <cstdint>
#include "gpu_sync_timing_core.h"

namespace x3m::renderer {
struct GpuSyncTimingStats {
    std::uint64_t syncs = 0, polls = 0, issue_failures = 0, data_failures = 0, timeouts = 0, dropped_frames = 0, not_cooperative = 0;
    HRESULT last_failure = S_OK;
};
class GpuSyncTiming final : public gpu_sync_timing::Marks {
public:
    GpuSyncTiming() = default;
    ~GpuSyncTiming() { detach(); }
    GpuSyncTiming(const GpuSyncTiming&) = delete; GpuSyncTiming& operator=(const GpuSyncTiming&) = delete;
    // Creates the boundary queries. S_OK when available; the support probe's or
    // the first failed CreateQuery's result otherwise, with nothing held.
    // `native` is the device's original vtable; `window` frames per report.
    HRESULT attach(IDirect3DDevice9* device, void* const* native, unsigned window = gpu_sync_timing::window_frames_default) noexcept;
    void before_reset() noexcept;             // releases every query; the frame in flight is dropped
    void after_reset(HRESULT reset) noexcept; // a successful Reset recreates them; a failed one leaves them released
    void detach() noexcept;
    bool available() const noexcept { return available_; }
    const char* reason() const noexcept { return reason_; }
    HRESULT create_result() const noexcept { return create_result_; }
    // Device references the live queries hold (the native AddRef/Release probe
    // around creation), for the final-release accounting.
    unsigned references() const noexcept { return references_; }
    // The failure cut-off switched the boundaries off; the queries (and their
    // references) are still held until release_deferred() runs them down.
    bool release_pending() const noexcept { return release_pending_; }
    void release_deferred() noexcept { if (release_pending_) release(); }
    bool tripped() const noexcept { return tripped_; } // the sticky timeout cut-off
    void begin(unsigned pass) noexcept override { if (available_) mark(pass, true); }
    void end(unsigned pass) noexcept override { if (available_) mark(pass, false); }
    // After the native Present of `frame` returned (the Present end already
    // marked): files the frame. True when a window closed and `report` holds it.
    bool frame(std::uint64_t frame, gpu_sync_timing::Report* report) noexcept;
    gpu_sync_timing::Report summary() const noexcept { return tracker_.summary(); }
    const gpu_sync_timing::Tracker& tracker() const noexcept { return tracker_; }
    const GpuSyncTimingStats& stats() const noexcept { return stats_; }
    std::uint64_t frequency() const noexcept { return frequency_; }
    // spin_limit_ms for a healthy device; spin_retry_limit_ms while the previous
    // boundary failed (consecutive_failures_ > 0), so a stuck driver costs one
    // long spin and then short ones until the cut-off.
    static constexpr unsigned spin_limit_ms = 500, spin_retry_limit_ms = 50, spin_failure_limit = 4;
private:
    HRESULT create() noexcept;
    void release() noexcept;
    void mark(unsigned pass, bool begin) noexcept;
    bool sync(unsigned boundary, std::uint64_t* after, std::uint64_t* wait) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* native_ = nullptr;
    IDirect3DQuery9* queries_[gpu_sync_timing::boundary_count]{};
    gpu_sync_timing::Tracker tracker_{};
    GpuSyncTimingStats stats_{};
    std::uint64_t frequency_ = 0;
    HRESULT create_result_ = S_FALSE;
    const char* reason_ = "not_attached";
    unsigned references_ = 0, consecutive_failures_ = 0;
    bool available_ = false, release_pending_ = false, tripped_ = false;
};
} // namespace x3m::renderer
