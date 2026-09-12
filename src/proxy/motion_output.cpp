#include "motion_output.h"
#include "capture.h"
#include "capture_state.h"
#include "scene_capture.h"
#include "telemetry.h"
#include "object_trace.h"
#include "object_lifetime.h"
#include "camera_state.h"
#include "../renderer/material_motion.h"
#include "../renderer/temporal_pass.h"
#include "../renderer/temporal_resolve_program.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace x3m {
namespace {
// The shadow captures every distinct clip-row window the profile table names
// (its rows read the clip rows from c24 with the point-light loop, or from c0
// without one) and gate 4 applies each row's own light-loop bound. The windows
// are derived from the table at compile time; a regenerated table naming more
// windows than the shadow holds, or a bounded row whose loop could reach its
// own clip rows, fails here rather than routing draws the shadow never
// captured.
struct MatrixWindows { UINT base[motion_matrix_windows_max]; std::size_t count; };
constexpr MatrixWindows derive_matrix_windows() noexcept {
    MatrixWindows windows{};
    for (const auto& row : renderer::motion_output_profiles) {
        bool seen = false;
        for (std::size_t i = 0; i < windows.count; ++i) seen = seen || windows.base[i] == row.matrix_register;
        if (seen) continue;
        if (windows.count == motion_matrix_windows_max) { windows.count = motion_matrix_windows_max + 1; break; }
        windows.base[windows.count++] = row.matrix_register;
    }
    return windows;
}
constexpr MatrixWindows matrix_windows = derive_matrix_windows();
static_assert(matrix_windows.count >= 1 && matrix_windows.count <= motion_matrix_windows_max,
              "the shadow holds at most max_matrix_windows distinct clip-row windows");
// Index of a row's window; every row has one (rows_match_shadow).
constexpr std::size_t window_of(UINT matrix_register) noexcept {
    for (std::size_t i = 0; i < matrix_windows.count; ++i)
        if (matrix_windows.base[i] == matrix_register) return i;
    return motion_matrix_windows_max;
}
// The candidate review bounds the relative light reads to c0-23 by requiring
// i0.x in [0, 8] (three constants per light); a bounded row's clip rows must
// lie above that block so the bound keeps the loop off them.
constexpr int light_loop_max_count = 8;
constexpr bool rows_match_shadow() noexcept {
    for (const auto& row : renderer::motion_output_profiles) {
        if (window_of(row.matrix_register) >= motion_matrix_windows_max) return false;
        if (row.light_loop_bound_required &&
            (row.light_loop_max_count != light_loop_max_count || row.matrix_register < 3u * light_loop_max_count))
            return false;
    }
    return true;
}
static_assert(rows_match_shadow(), "every profile row must name a shadowed clip-row window and, when bounded, the i0.x <= 8 bound below its rows");
// IDirect3DDevice9 vtable slots, verified against the MinGW d3d9.h method order
// by verification/probe/abi_check.cpp (compile-time offsetof assertions).
enum Slot : unsigned {
    AddRef = 1, Release = 2, GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9,
    CreateTexture = 23, CreateRenderTarget = 28, GetRenderTargetData = 32, StretchRect = 34,
    CreateOffscreenPlainSurface = 36, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93,
    SetVertexShaderConstantF = 94, GetVertexShaderConstantF = 95, GetVertexShaderConstantI = 97,
    SetStreamSource = 100, GetStreamSource = 101, GetStreamSourceFreq = 103, GetIndices = 105,
    CreatePixelShader = 106, SetPixelShader = 107, GetPixelShader = 108,
    SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110
};
using D = IDirect3DDevice9*;
using SetRenderTargetFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRenderTargetFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRenderStateFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using GetRenderStateFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetConstantsFFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstantsFFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
using GetConstantsIFn = HRESULT(WINAPI*)(D, UINT, int*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using GetStreamFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using GetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9**);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateRtFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
using CreateOffscreenFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
using GetRtDataFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, IDirect3DSurface9*);
using GetDirect3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetDisplayModeFn = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CountFn = ULONG(WINAPI*)(D);
using StretchFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);

// ps_2_0: def c0, 0, 0, 0, -1 ; mov oC0, c0 ; end. Writes the invalid-history
// sentinel of the RGBA32F motion ABI (alpha -1) to every covered texel.
constexpr DWORD sentinel_program[] = {
    0xffff0200u, 0x05000051u, 0xa00f0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// The same with a second output: oC1 = c0.wwww = (-1, -1, -1, -1) fills the
// R32F depth target (which stores .x only) with its sentinel -1 in the same
// draw; ps_2_0 `def c0, 0, 0, 0, -1; mov oC0, c0; mov oC1, c0.wwww`.
constexpr DWORD sentinel_mrt_program[] = {
    0xffff0200u, 0x05000051u, 0xa00f0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0ff0000u, 0x0000ffffu};
// ps_2_0 self-test: oC0 = (0.25, 0.5, 0.75, 1) into A8R8G8B8, oC1 = (1, 2, 3, -1)
// into A32B32G32R32F. Both targets are read back to prove mixed-format MRT.
constexpr DWORD self_test_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x0000ffffu};
// Three-format form: additionally oC2 = (0.625, 0.375, 0.125, 1) into R32F.
constexpr DWORD self_test_depth_program[] = {
    0xffff0200u,
    0x05000051u, 0xa00f0000u, 0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x05000051u, 0xa00f0002u, 0x3f200000u, 0x3ec00000u, 0x3e000000u, 0x3f800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x02000001u, 0x800f0802u, 0xa0e40002u,
    0x0000ffffu};
constexpr float self_test_depth_value = 0.625f;
// Halton sequences in bases 2 and 3 give the jitter offsets; index is 1-based
// so no sample lands on the raster centre twice in a row.
float halton(unsigned index, unsigned base) noexcept {
    float fraction = 1.f, result = 0.f;
    while (index) { fraction /= float(base); result += fraction * float(index % base); index /= base; }
    return result;
}
// Render states the injected fullscreen draws set; each is saved and restored.
constexpr D3DRENDERSTATETYPE touched_states[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
    D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_COLORWRITEENABLE, D3DRS_SCISSORTESTENABLE,
    D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2};
constexpr DWORD touched_values[] = {FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, D3DFILL_SOLID, 15, FALSE, FALSE, FALSE, FALSE, 0, 15, 15};
constexpr unsigned touched_count = sizeof(touched_states) / sizeof(touched_states[0]);
static_assert(touched_count == sizeof(touched_values) / sizeof(touched_values[0]));
constexpr unsigned failure_log_limit = 16;
// Render states the route reads per draw (motion_shadow_state_count of them):
// the selector's z states (every draw while tracking), the gate-4 opaque-draw
// checks and the COLORWRITEENABLE1/2 masks saved around RT1/RT2. With
// X3M_STATE_SHADOW on, the SetRenderState hook keeps the application's values
// here and the route issues no GetRenderState for them after the first read.
constexpr D3DRENDERSTATETYPE shadow_states[motion_shadow_state_count] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2};
constexpr unsigned shadow_index(D3DRENDERSTATETYPE state) noexcept {
    for (unsigned i = 0; i < motion_shadow_state_count; ++i) if (shadow_states[i] == state) return i;
    return unsigned(motion_shadow_state_count);
}
const char* scene_end_source_name(std::uint32_t source) noexcept {
    return source == unsigned(SceneEndSource::Hook) ? "hook" : source == unsigned(SceneEndSource::StretchRect) ? "stretchrect" : "none";
}

std::uint64_t hash_bytes(const void* data, std::size_t size) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    auto bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
// Clears the member before the COM call so a re-entrant path never sees it.
template<class T> void release(T*& object) noexcept { if (T* held = object) { object = nullptr; held->Release(); } }
bool same(const renderer::Surface& a, const renderer::Surface& b) noexcept {
    return a.known && b.known && a.identity && a.identity == b.identity && a.container == b.container &&
           a.width == b.width && a.height == b.height && a.format == b.format && a.msaa == b.msaa;
}
} // namespace

float motion_jitter_sample(unsigned index, unsigned axis) noexcept {
    return halton(index, axis ? 3u : 2u) - .5f;
}

// Everything the fullscreen draws touch. COM references returned by the getters
// are released by the destructor after restoration.
struct MotionOutput::SavedState {
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD fvf = 0;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stride = 0;
    DWORD states[touched_count]{};
    unsigned target_count = 1;
    ~SavedState() {
        for (auto& target : targets) release(target);
        release(depth); release(declaration); release(vs); release(ps); release(stream);
    }
};

MotionOutput::MotionOutput() noexcept = default;
MotionOutput::~MotionOutput() { release_resources(); }

unsigned MotionOutput::device_references() const noexcept {
    if (releasing_ || taa_busy_) return 0;
    unsigned count = (target_surface_ ? 1 : 0) + taa_references_;
    if (depth_surface_) ++count;
    if (sentinel_ps_) ++count;
    if (sentinel_mrt_ps_) ++count;
    for (const auto& entry : vertex_) if (entry.second.variant) ++count;
    for (const auto& entry : pixel_) if (entry.second.variant) ++count;
    return count;
}

// Releases every owned device object. Called before the application's final
// device Release, before destruction, and never while a draw is routed.
// Re-entrant by construction: each child's final Release calls the device's
// Release through the hooked vtable, so members are cleared before the call
// and the release hook sees zero held references meanwhile.
void MotionOutput::release_resources() noexcept {
    if (releasing_) return;
    releasing_ = true;
    release_target();
    // The pass is destroyed with its objects: the destructor's second call of
    // this function must not probe a device that no longer exists.
    if (taa_) { taa_call([&] { taa_->shutdown(); }); taa_.reset(); }
    release(sentinel_ps_);
    release(sentinel_mrt_ps_);
    for (auto& entry : vertex_) release(entry.second.variant);
    for (auto& entry : pixel_) release(entry.second.variant);
    shadow_.vs_variant = nullptr; shadow_.ps_variant = nullptr;
    history_.invalidate();
    fill_pending_ = false;
    releasing_ = false;
}

void MotionOutput::release_target() noexcept {
    release(depth_surface_);
    release(target_surface_);
    target_width_ = target_height_ = 0;
}

void MotionOutput::configure_jitter(bool enabled, unsigned samples) noexcept {
    jitter_requested_ = enabled;
    jitter_samples_ = samples < 2 ? 2u : samples > 64 ? 64u : samples;
}
void MotionOutput::configure_cut_bounds(float median_px_at_1280, float missing_fraction) noexcept {
    if (std::isfinite(median_px_at_1280) && median_px_at_1280 > 0) cut_median_bound_ = median_px_at_1280;
    if (std::isfinite(missing_fraction) && missing_fraction > 0 && missing_fraction <= 1) cut_missing_bound_ = missing_fraction;
}
void MotionOutput::configure_taa(bool requested, bool debug) noexcept { taa_requested_ = requested; taa_debug_ = debug; }
void MotionOutput::configure_sentinel(renderer::SentinelMode mode, float cut_degrees, unsigned log_frames) noexcept {
    sentinel_mode_ = mode;
    camera_cut_degrees_ = std::isfinite(cut_degrees) && cut_degrees > 0 ? cut_degrees : 20.f;
    camera_log_interval_ = log_frames ? log_frames : 300u;
}

// ---- cost telemetry --------------------------------------------------------

// QPC stamp while telemetry is on (begin_frame latches the switch), else 0:
// every tick total below then stays zero and no metric is recorded.
std::uint64_t MotionOutput::stamp() const noexcept { return telemetry_ ? telemetry::now() : 0; }
void MotionOutput::record(unsigned metric, std::uint64_t ticks, bool failed, std::uint64_t bytes) noexcept {
    if (telemetry_ && stats_) telemetry::record(*stats_, static_cast<telemetry::Metric>(metric), ticks, failed, bytes);
}
// One route-issued SetRenderTarget of the per-draw apply/undo path or the
// lazy flush: counted per frame and timed per call (route_set_rt).
HRESULT MotionOutput::bind_target(DWORD index, IDirect3DSurface9* surface) noexcept {
    const std::uint64_t begin = stamp();
    const HRESULT hr = native<SetRenderTargetFn>(SetRenderTarget)(device_, index, surface);
    const std::uint64_t ticks = stamp() - begin;
    ++counters_.set_rt; counters_.set_rt_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteSetRenderTarget), ticks, FAILED(hr));
    return hr;
}
// Binds RT1 (and RT2 for a depth row) with full write masks for a routed
// draw. Per-draw mode saves the application's masks into the route for undo;
// lazy mode saves them once at bind time, keeps the bindings across
// consecutive routed draws, only toggles RT2 when the row's depth output
// differs from the current binding, and releases them in restore_bindings.
HRESULT MotionOutput::bind_targets(MotionRoute& route) noexcept {
    HRESULT hr = S_OK;
    if (!lazy_mode_) {
        hr = render_state(D3DRS_COLORWRITEENABLE1, &route.saved_write1);
        if (SUCCEEDED(hr)) { hr = bind_target(1, target_surface_); route.rt_set = SUCCEEDED(hr); }
        if (SUCCEEDED(hr)) { hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, 15); route.write_set = SUCCEEDED(hr); }
        if (SUCCEEDED(hr) && route.depth) {
            hr = render_state(D3DRS_COLORWRITEENABLE2, &route.saved_write2);
            if (SUCCEEDED(hr)) { hr = bind_target(2, depth_surface_); route.rt2_set = SUCCEEDED(hr); }
            if (SUCCEEDED(hr)) { hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, 15); route.write2_set = SUCCEEDED(hr); }
        }
        return hr;
    }
    if (!lazy_rt1_) {
        hr = render_state(D3DRS_COLORWRITEENABLE1, &lazy_write1_);
        if (SUCCEEDED(hr)) { hr = bind_target(1, target_surface_); lazy_rt1_ = SUCCEEDED(hr); }
        if (SUCCEEDED(hr)) hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, 15);
    }
    if (SUCCEEDED(hr) && route.depth && !lazy_rt2_) {
        hr = render_state(D3DRS_COLORWRITEENABLE2, &lazy_write2_);
        if (SUCCEEDED(hr)) { hr = bind_target(2, depth_surface_); lazy_rt2_ = SUCCEEDED(hr); }
        if (SUCCEEDED(hr)) hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, 15);
    } else if (SUCCEEDED(hr) && !route.depth && lazy_rt2_) {
        // A motion-only row after a depth row: its variant writes no oC2, so
        // RT2 goes back exactly as the per-draw mode would leave it.
        hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, lazy_write2_);
        if (SUCCEEDED(hr)) { hr = bind_target(2, nullptr); lazy_rt2_ = FAILED(hr); }
    }
    return hr;
}
// Lazy mode: put the application's RT1/RT2 bindings and write masks back
// (reverse order of the bind). No-op in per-draw mode or when nothing is
// bound, so every hook may call it unconditionally before a native call.
void MotionOutput::restore_bindings() noexcept {
    record_deferred();
    if (!lazy_rt1_ && !lazy_rt2_) return;
    const HRESULT first = flush_bindings<false>();
    if (FAILED(first) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=lazy_flush", id_, frame_, counters_.draws, first);
    }
}
// The flush itself. The quiet instantiation is reached from the light
// SetRenderState hook (LightCallBoundary: no x87 code on its path, so no
// telemetry record and no log formatter here); it counts into the frame
// counters directly and leaves the metric sample and the failure line to
// record_deferred, which every heavy call runs first.
template<bool quiet> HRESULT MotionOutput::flush_bindings() noexcept {
    const std::uint64_t begin = stamp();
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    auto unbind = [&](DWORD index) {
        const std::uint64_t b = stamp();
        const HRESULT hr = native<SetRenderTargetFn>(SetRenderTarget)(device_, index, nullptr);
        const std::uint64_t ticks = stamp() - b;
        ++counters_.set_rt; counters_.set_rt_ticks += ticks;
        if constexpr (!quiet) record(unsigned(telemetry::Metric::RouteSetRenderTarget), ticks, FAILED(hr));
        return hr;
    };
    if (lazy_rt2_) { step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, lazy_write2_)); step(unbind(2)); }
    if (lazy_rt1_) { step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, lazy_write1_)); step(unbind(1)); }
    lazy_rt1_ = lazy_rt2_ = false;
    const std::uint64_t ticks = stamp() - begin;
    ++counters_.lazy_flushes; counters_.lazy_flush_ticks += ticks;
    if (FAILED(first)) { ++counters_.restore_failures; invalidate_render_states(); }
    if constexpr (quiet) {
        ++deferred_flushes_; deferred_flush_ticks_ += ticks;
        if (SUCCEEDED(deferred_flush_result_) && FAILED(first)) deferred_flush_result_ = first;
    } else record(unsigned(telemetry::Metric::RouteLazyFlush), ticks, FAILED(first));
    return first;
}
// Metric sample and failure line of the quiet flushes since the last heavy
// call (one aggregated RouteLazyFlush sample; the SetRenderTarget samples
// inside them are counted, not timed individually).
void MotionOutput::record_deferred() noexcept {
    if (!deferred_flushes_) return;
    record(unsigned(telemetry::Metric::RouteLazyFlush), deferred_flush_ticks_, FAILED(deferred_flush_result_));
    if (FAILED(deferred_flush_result_) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=lazy_flush_setstate flushes=%lu",
            id_, frame_, counters_.draws, deferred_flush_result_, static_cast<unsigned long>(deferred_flushes_));
    }
    deferred_flushes_ = 0; deferred_flush_ticks_ = 0; deferred_flush_result_ = S_OK;
}

// ---- render-state shadow ---------------------------------------------------

// Light path (no logging, no telemetry record): an application write to a
// write mask the route holds in lazy mode first restores the application's
// bindings, so the write lands on them and the next routed draw saves the new
// value. Other states need nothing before the call.
void MotionOutput::before_set_render_state(D3DRENDERSTATETYPE state) noexcept {
    if (!enabled_) return;
    if ((state == D3DRS_COLORWRITEENABLE1 && lazy_rt1_) || (state == D3DRS_COLORWRITEENABLE2 && lazy_rt2_)) flush_bindings<true>();
}
void MotionOutput::set_render_state(D3DRENDERSTATETYPE state, DWORD value) noexcept {
    // A recorded call does not reach the device (EndStateBlock resynchronizes).
    if (!enabled_ || shadow_.recording) return;
    const unsigned i = shadow_index(state);
    if (i < motion_shadow_state_count) { shadow_.states[i] = value; shadow_.states_known[i] = true; }
}
HRESULT MotionOutput::get_render_state_native(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
    ++counters_.rs_gets;
    return native<GetRenderStateFn>(GetRenderState)(device_, state, value);
}
HRESULT MotionOutput::render_state(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
    ++counters_.rs_queries;
    const unsigned i = shadow_index(state);
    if (state_shadow_ && i < motion_shadow_state_count && shadow_.states_known[i]) {
        *value = shadow_.states[i]; ++counters_.rs_hits; return S_OK;
    }
    const HRESULT hr = get_render_state_native(state, value);
    // The getter reports the device state whether or not a block is recording.
    if (state_shadow_ && i < motion_shadow_state_count && SUCCEEDED(hr)) { shadow_.states[i] = *value; shadow_.states_known[i] = true; }
    return hr;
}
// After a failed restoration of the route's own state changes the device's
// values are unknown: drop the shadow, the next queries read again.
void MotionOutput::invalidate_render_states() noexcept {
    for (bool& known : shadow_.states_known) known = false;
    ++counters_.rs_resyncs;
}

// ---- temporal resolve ------------------------------------------------------

// The device reference count through the native slots (no hook re-entry):
// AddRef returns the incremented count, Release the count after.
ULONG MotionOutput::probe_references() noexcept {
    native<CountFn>(AddRef)(device_);
    return native<CountFn>(Release)(device_);
}
// While the call runs, device_references() reports zero (like release_resources):
// each child the pass releases re-enters the device Release hook through the
// wrapper's parent release, and a count that still includes the objects being
// released could match the hook's final-release probe by coincidence. The
// application cannot issue its final Release inside a hook, so nothing is missed.
template<typename Fn> void MotionOutput::taa_call(Fn&& fn) noexcept {
    taa_busy_ = true;
    const ULONG before = probe_references();
    fn();
    const ULONG after = probe_references();
    taa_references_ = unsigned(long(taa_references_) + (long(after) - long(before)));
    taa_busy_ = false;
}
void MotionOutput::invalidate_taa() noexcept { if (taa_) taa_->invalidate(); camera_previous_ = renderer::CameraState{}; }
// Lazily creates the pass and its resolve shader (one device reference) the
// first time a frame reaches the copy with the route able to resolve.
bool MotionOutput::ensure_taa() noexcept {
    if (taa_ && !taa_failed_) return true;
    if (taa_failed_) return false;
    try { taa_ = std::make_unique<renderer::TemporalPass>(); } catch (...) { taa_failed_ = true; return false; }
    HRESULT hr = E_FAIL;
    taa_call([&] { hr = taa_->initialize(device_, nullptr, reinterpret_cast<const DWORD*>(renderer::temporal_resolve_program()), native_); });
    taa_failed_ = FAILED(hr);
    log("motion_output_taa device=%llu initialize=%08lx references=%u", id_, hr, taa_references_);
    return !taa_failed_;
}
// The whole resolve at the bloom copy: RT1/RT2 containers as inputs, the
// application's main surface as the 8-bit color input, then the copy-back.
// The main target is written only after run() succeeded; any failure leaves it
// untouched, invalidates history and is logged once for the frame.
HRESULT MotionOutput::resolve(IDirect3DSurface9* main_surface) noexcept {
    auto& t = counters_.taa;
    IDirect3DTexture9* motion = nullptr; IDirect3DTexture9* depth = nullptr;
    renderer::Output out{};
    HRESULT hr = E_FAIL;
    taa_call([&] {
        hr = target_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&motion));
        if (SUCCEEDED(hr)) hr = depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth));
        if (SUCCEEDED(hr) && (!motion || !depth)) hr = E_NOINTERFACE;
        if (FAILED(hr)) { t.skip = unsigned(TaaSkip::Container); t.result = hr; invalidate_taa(); }
        else {
            if (capture_ && taa_debug_)
                // GetRenderTargetData needs the exact format of the main target (A8R8G8B8 or X8R8G8B8).
                readback_surface(main_surface, static_cast<D3DFORMAT>(main_.format), 4, L"color", L"bgra8", "motion_output_color_readback", "bgra8_row_major");
            renderer::FrameInputs in{};
            in.color_surface = main_surface; in.current_depth = depth; in.motion = motion;
            in.width = main_.width; in.height = main_.height;
            in.epoch = generation_; // Dimension changes are compared by the pass itself.
            // Depth-sentinel policy: sentinel pixels (RT2 -1, RT1 alpha -1) are
            // reprojected at the far plane through the camera transform built
            // from this frame's scene view and the view of the frame the history
            // came from (policy 2); without a valid pair, or with the switch
            // off, they stay current-only (policy 1) and the matrix is the
            // identity, which the resolve then never applies (docs/architecture/
            // temporal-integration.md, "Camera reprojection for sentinel pixels").
            const auto decision = renderer::camera_sentinel_policy(sentinel_mode_, camera_scene_, camera_previous_, camera_cut_degrees_);
            t.camera_policy = decision.policy; t.camera_reason = unsigned(decision.reason);
            t.camera_cut = decision.cut; t.camera_rotation_deg = decision.rotation_degrees;
            std::memcpy(in.clip_to_previous, decision.matrix, sizeof decision.matrix);
            in.sentinel_camera = decision.policy == 2;
            in.current_jitter[0] = jitter_[0]; in.current_jitter[1] = jitter_[1];
            in.previous_jitter[0] = jitter_previous_[0]; in.previous_jitter[1] = jitter_previous_[1];
            in.motion_policy = renderer::MotionPolicy::PerPixel;
            in.reactive_policy = renderer::ReactivePolicy::DerivedFromDepthSentinel;
            in.history_allowed = true; in.cut = counters_.cut || decision.cut;
            in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording;
            in.caller_queries_idle = active_queries_ == 0;
            // Phase timing of the run (telemetry only): the pass stamps its own
            // five phases; the whole call is timed here and nests them.
            taa_->configure_timing(telemetry_);
            const std::uint64_t run_begin = stamp();
            hr = taa_->run(in, &out);
            const std::uint64_t run_ticks = stamp() - run_begin;
            const auto diagnostics = taa_->diagnostics();
            t.result = diagnostics.operation; t.restore = diagnostics.restoration;
            auto& c = counters_;
            c.taa_run_ticks += run_ticks; c.taa_capture_ticks += diagnostics.ticks_capture;
            c.taa_copy_color_ticks += diagnostics.ticks_copy_color; c.taa_copy_depth_ticks += diagnostics.ticks_copy_depth;
            c.taa_draw_ticks += diagnostics.ticks_draw; c.taa_apply_ticks += diagnostics.ticks_apply;
            record(unsigned(telemetry::Metric::TaaRun), run_ticks, FAILED(hr));
            record(unsigned(telemetry::Metric::TaaStateCapture), diagnostics.ticks_capture);
            record(unsigned(telemetry::Metric::TaaCopyColor), diagnostics.ticks_copy_color);
            record(unsigned(telemetry::Metric::TaaCopyDepth), diagnostics.ticks_copy_depth);
            record(unsigned(telemetry::Metric::TaaResolveDraw), diagnostics.ticks_draw);
            record(unsigned(telemetry::Metric::TaaStateApply), diagnostics.ticks_apply, FAILED(diagnostics.restoration));
            if (FAILED(diagnostics.restoration)) invalidate_render_states();
            if (SUCCEEDED(hr) && !out.color_surface) hr = E_FAIL;
            if (SUCCEEDED(hr)) {
                if (capture_ && taa_debug_)
                    readback_surface(out.color_surface, D3DFMT_A16B16G16R16F, 8, L"taa", L"rgba16f", "motion_output_taa_readback", "rgba16f_row_major");
                // Point-filtered full-rect copy of the resolved FP16 image back into
                // the 8-bit main target; StretchRect changes no device state.
                const std::uint64_t copy_begin = stamp();
                hr = native<StretchFn>(StretchRect)(device_, out.color_surface, nullptr, main_surface, nullptr, D3DTEXF_POINT);
                const std::uint64_t copy_ticks = stamp() - copy_begin;
                counters_.taa_copy_back_ticks += copy_ticks;
                record(unsigned(telemetry::Metric::TaaCopyBack), copy_ticks, FAILED(hr));
                t.copy = hr;
                if (FAILED(hr)) invalidate_taa();
                else {
                    t.resolved = true; t.used_history = out.used_history;
                    // The history now holds this frame: its scene view is the
                    // previous view of the next resolve (invalid when unread).
                    camera_previous_ = camera_scene_; camera_previous_frame_ = frame_;
                }
            }
        }
        release(depth); release(motion);
    });
    if (FAILED(hr) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_taa_failed device=%llu frame=%llu skip=%lu result=%08lx restore=%08lx copy=%08lx scene_open=%u",
            id_, frame_, static_cast<unsigned long>(t.skip), t.result, t.restore, t.copy, scene_open_);
    }
    return hr;
}
void MotionOutput::before_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                                  IDirect3DSurface9* destination, const RECT* destination_rect) noexcept {
    // The application's copy (and the resolve, which samples RT1/RT2) must
    // see the application's bindings.
    restore_bindings();
    if (!enabled_ || selector_.state() != renderer::BoundaryState::AwaitCopy) return;
    // Would this copy advance the selector out of AwaitCopy? Probe a copy of
    // it with the event after_stretch will feed (same sequence number, not
    // consumed here); only the main-target bloom copy qualifies.
    renderer::Event e{};
    e.kind = renderer::EventKind::Copy; e.sequence = sequence_ + 1; e.result_known = true; e.result = 0;
    e.source = describe_surface(source); e.destination = describe_surface(destination);
    e.source_rect_null = source_rect == nullptr; e.destination_rect_null = destination_rect == nullptr;
    renderer::SceneBoundarySelector probe = selector_;
    probe.observe(e);
    if (probe.state() != renderer::BoundaryState::AwaitBloomTarget) return;
    counters_.bloom_copy_seen = true; // The selector's scene end (cross-checked against the engine hook at Present).
    if (!taa_enabled_) return;
    // The copy path is the fallback: a frame the engine hook already resolved
    // (or attempted) is left alone here.
    if (!resolve_allowed(SceneEndSource::StretchRect)) return;
    resolve(source);
}
// Records this frame's single resolve attempt and its source, then the
// preconditions common to both resolve points. False: no resolve (the skip
// reason is recorded and the history invalidated, except for the no-op case
// of a second attempt in one frame).
bool MotionOutput::resolve_allowed(SceneEndSource source) noexcept {
    auto& t = counters_.taa;
    if (t.attempted) return false;
    t.attempted = true; t.source = unsigned(source);
    auto skip = [&](TaaSkip why) { t.skip = unsigned(why); invalidate_taa(); return false; };
    // The resolve without jitter is a no-op visually and would only blur.
    if (!jitter_active_) return skip(TaaSkip::NoJitter);
    if (!counters_.filled || !target_surface_ || !depth_surface_) return skip(TaaSkip::NotFilled);
    if (shadow_.recording) return skip(TaaSkip::Recording);
    if (active_queries_) return skip(TaaSkip::Queries);
    if (!ensure_taa()) return skip(TaaSkip::Initialize);
    // Strict mode (X3M_TAA_SENTINEL=2): a frame whose scene camera could not
    // be read, or whose transform failed, skips the resolve instead of
    // resolving current-only, so a broken camera read shows in gameplay and
    // in the log (skip 9) rather than degrading silently. A frame without a
    // previous view (the first one, after a cut or a Reset) still resolves:
    // that is how the history and its view are established.
    if (sentinel_mode_ == renderer::SentinelMode::Camera) {
        const auto decision = renderer::camera_sentinel_policy(sentinel_mode_, camera_scene_, camera_previous_, camera_cut_degrees_);
        if (decision.reason == renderer::SentinelReason::CurrentInvalid || decision.reason == renderer::SentinelReason::TransformFailed) {
            t.camera_policy = decision.policy; t.camera_reason = unsigned(decision.reason);
            t.camera_cut = decision.cut; t.camera_rotation_deg = decision.rotation_degrees;
            return skip(TaaSkip::CameraState);
        }
    }
    return true;
}
// The engine scene-end signal (X3M_SCENE_HOOK): the trampoline at the frame
// routine's compositing callsite 0x004721b1 calls this before the original
// 0x004c4750 runs, on the render thread, outside any device hook (the caller
// holds the capture mutex). In the Scene phase it is the primary scene end:
// routing and jitter stop for the frame, the cut verdict is final and the
// temporal resolve runs on the bound RT0 while the depth surface is still
// bound (the pass unbinds and restores it). RT2 holds the current depth, so
// the D24X8 surface is not read. The bloom copy that 0x004c4750 may issue
// afterwards (glow on) then finds the frame resolved and copies the resolved
// image; with glow off no copy follows and the frame is resolved all the same.
void MotionOutput::scene_end_hook() noexcept {
    if (!enabled_) return;
    record_deferred();
    ++counters_.hook_signals;
    const auto state = selector_.state();
    if (state != renderer::BoundaryState::Scene || counters_.hook_scene_end) {
        // Outside the Scene phase (menu, rejected or environment-map frame) or
        // a second signal: nothing to end; the cross-check at Present reports it.
        ++counters_.hook_outside_scene; counters_.hook_state = unsigned(state);
        return;
    }
    restore_bindings();
    if (!cut_finished_) finish_cut_detector();
    counters_.hook_scene_end = true;
    if (!taa_enabled_) return;
    if (!resolve_allowed(SceneEndSource::Hook)) return;
    // The resolve reads and rewrites RT0, which must be the latched main target
    // (the same surface the bloom copy would read).
    IDirect3DSurface9* rt0 = nullptr;
    const HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0);
    if (FAILED(hr) || !rt0 || !same(describe_surface(rt0), main_)) {
        counters_.taa.skip = unsigned(TaaSkip::Target); counters_.taa.result = FAILED(hr) ? hr : E_FAIL;
        invalidate_taa(); release(rt0); return;
    }
    resolve(rt0);
    release(rt0);
}
void MotionOutput::after_begin_scene(HRESULT result) noexcept { if (enabled_ && SUCCEEDED(result)) scene_open_ = true; }
void MotionOutput::after_end_scene(HRESULT result) noexcept { if (enabled_ && SUCCEEDED(result)) scene_open_ = false; }
void MotionOutput::query_active(bool active) noexcept {
    if (active) ++active_queries_;
    else if (active_queries_) --active_queries_;
}

// Lazily (re)creates the RGBA32F motion target and, when the device produces
// depth, the R32F depth target at the latched main dimensions. Only the
// level-0 surfaces are retained. A texture level keeps its container
// alive on D3D9 (the level shares the texture's reference count, which holds
// the device reference), so one owned object is one device reference. Holding
// the texture as well would be harmless natively but not through the ownership
// wrapper (X3M_OWNERSHIP=1), where the texture wrapper and the surface wrapper
// are two children of the device and each owns a logical device reference:
// device_references() would then under-count by one and the release hook's
// final-Release probe would never match, leaking the device.
bool MotionOutput::ensure_target(UINT width, UINT height) noexcept {
    if (target_surface_ && target_width_ == width && target_height_ == height) return true;
    release_target();
    if (target_failed_ || !width || !height) return false;
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = native<CreateTextureFn>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &texture, nullptr);
    HRESULT level = E_FAIL, depth_hr = S_FALSE, depth_level = S_FALSE;
    if (SUCCEEDED(hr) && texture) level = texture->GetSurfaceLevel(0, &target_surface_);
    release(texture);
    if (depth_enabled_ && SUCCEEDED(hr) && SUCCEEDED(level) && target_surface_) {
        // The variants of depth rows write oC2 unconditionally, so a device
        // producing depth must own RT2 whenever it routes: a failed depth
        // allocation disables routing like a failed motion allocation.
        depth_hr = native<CreateTextureFn>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_R32F, D3DPOOL_DEFAULT, &texture, nullptr);
        depth_level = E_FAIL;
        if (SUCCEEDED(depth_hr) && texture) depth_level = texture->GetSurfaceLevel(0, &depth_surface_);
        release(texture);
        if (FAILED(depth_hr) || FAILED(depth_level) || !depth_surface_) hr = FAILED(depth_hr) ? depth_hr : E_FAIL;
    }
    if (FAILED(hr) || FAILED(level) || !target_surface_) {
        log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx depth=%u depth_create=%08lx depth_level=%08lx",
            id_, width, height, hr, level, depth_enabled_, depth_hr, depth_level);
        release_target();
        target_failed_ = true; // Retry only after Reset; do not spam allocation per frame.
        return false;
    }
    target_width_ = width; target_height_ = height;
    history_.invalidate();
    log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx depth=%u depth_create=%08lx depth_level=%08lx",
        id_, width, height, hr, level, depth_enabled_, depth_hr, depth_level);
    return true;
}

void MotionOutput::attach(IDirect3DDevice9* device, void** native_table, std::uint64_t device_id,
                          const D3DCAPS9& caps, bool requested, telemetry::State* stats) noexcept {
    device_ = device; native_ = native_table; id_ = device_id; caps_ = caps; requested_ = requested;
    stats_ = stats; lazy_rt1_ = lazy_rt2_ = false;
    enabled_ = false; depth_enabled_ = false;
    if (!requested) return;
    history_ = renderer::MotionRowHistory(4096); // Reserves both tables once; ready() false on failure.
    try { displacements_.reserve(4096); } catch (...) { displacements_.clear(); displacements_.shrink_to_fit(); }
    history_available_ = object_trace::active() && object_lifetime::active();
    const char* reason = "ok";
    const char* depth_reason = "ok";
    const char* taa_reason = taa_requested_ ? "ok" : "off";
    char detail[160] = "";
    HRESULT format_result = S_OK, depth_format_result = S_OK, taa_format_result = S_OK;
    if (caps.NumSimultaneousRTs < 2) reason = "mrt_count";
    else if (caps.MaxVertexShaderConst < 256) reason = "vs_constants";
    else if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)) reason = "mrt_bit_depths";
    else if (D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion) < 3 ||
             D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion) < 3) reason = "shader_model";
    else {
        IDirect3D9* factory = nullptr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        if (FAILED(native<GetDirect3DFn>(GetDirect3D)(device_, &factory)) || !factory) reason = "factory";
        else if (FAILED(native<GetCreationFn>(GetCreationParameters)(device_, &creation)) ||
                 FAILED(native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &mode))) reason = "adapter_query";
        else {
            format_result = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F);
            if (FAILED(format_result)) reason = "rgba32f_target";
            // The current-depth target needs a third simultaneous target and
            // an R32F render target; without them the route stays motion-only.
            if (caps.NumSimultaneousRTs < 3) depth_reason = "mrt_count";
            else {
                depth_format_result = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                    D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F);
                if (FAILED(depth_format_result)) depth_reason = "r32f_target";
            }
            // The resolve needs FP16 render targets (history, scratch) and
            // point-sampled FP16, RGBA32F and R32F textures; it also needs RT2
            // (checked below) and the jitter.
            if (taa_requested_) {
                taa_format_result = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                    D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
                if (FAILED(taa_format_result)) taa_reason = "fp16_target";
                for (D3DFORMAT sampled : {D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F, D3DFMT_R32F}) {
                    if (!std::strcmp(taa_reason, "ok") &&
                        FAILED(taa_format_result = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                            0, D3DRTYPE_TEXTURE, sampled))) taa_reason = "float_sampling";
                }
                // The FP16 scratch copy and the copy-back convert between the
                // 8-bit main target and A16B16G16R16F through StretchRect, which
                // native D3D9 grants only where the driver reports the conversion.
                for (D3DFORMAT eight_bit : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8}) {
                    if (!std::strcmp(taa_reason, "ok") &&
                        (FAILED(taa_format_result = factory->CheckDeviceFormatConversion(creation.AdapterOrdinal, creation.DeviceType,
                             eight_bit, D3DFMT_A16B16G16R16F)) ||
                         FAILED(taa_format_result = factory->CheckDeviceFormatConversion(creation.AdapterOrdinal, creation.DeviceType,
                             D3DFMT_A16B16G16R16F, eight_bit)))) taa_reason = "format_conversion";
                }
            }
        }
        release(factory);
    }
    depth_enabled_ = !std::strcmp(reason, "ok") && !std::strcmp(depth_reason, "ok");
    if (!std::strcmp(reason, "ok")) {
        const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, sentinel_program, &sentinel_ps_);
        if (FAILED(hr) || !sentinel_ps_) { reason = "sentinel_shader"; std::snprintf(detail, sizeof detail, "%08lx", hr); }
    }
    if (!std::strcmp(reason, "ok") && depth_enabled_) {
        const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, sentinel_mrt_program, &sentinel_mrt_ps_);
        if (FAILED(hr) || !sentinel_mrt_ps_) { depth_enabled_ = false; depth_reason = "sentinel_shader"; release(sentinel_mrt_ps_); }
    }
    // The three-format self test proves depth; if it fails, the two-format
    // test decides whether the route runs motion-only.
    char depth_detail[160] = "";
    if (!std::strcmp(reason, "ok") && depth_enabled_ && !self_test(true, depth_detail, sizeof depth_detail)) {
        depth_enabled_ = false; depth_reason = "self_test"; release(sentinel_mrt_ps_);
    }
    if (!std::strcmp(reason, "ok") && !depth_enabled_ && !self_test(false, detail, sizeof detail)) reason = "self_test";
    if (!std::strcmp(reason, "ok") && depth_enabled_) std::memcpy(detail, depth_detail, sizeof detail);
    enabled_ = !std::strcmp(reason, "ok");
    if (!enabled_) { release(sentinel_ps_); release(sentinel_mrt_ps_); depth_enabled_ = false; }
    else { resync_shadow(); begin_frame(0, false); } // The first frame has no preceding Present.
    if (!history_available_) history_.invalidate();
    if (taa_requested_ && !std::strcmp(taa_reason, "ok")) {
        if (!enabled_) taa_reason = "route";
        else if (!depth_enabled_) taa_reason = "depth";
        else if (!jitter_requested_) taa_reason = "jitter";
    }
    taa_enabled_ = taa_requested_ && !std::strcmp(taa_reason, "ok");
    scene_open_ = false; active_queries_ = 0;
    log("motion_output_device device=%llu enabled=%u reason=%s detail=%s mrt=%lu vs_constants=%lu misc=%08lx vs=%08lx ps=%08lx rgba32f=%08lx history_available=%u history_capacity=%u depth=%u depth_reason=%s depth_detail=%s r32f=%08lx jitter=%u jitter_samples=%u taa=%u taa_reason=%s taa_format=%08lx taa_debug=%u rt_mode=%s camera=%s sentinel=%u camera_cut_deg=%.2f camera_log=%u state_shadow=%u scene_hook=%u",
        id_, enabled_, reason, detail[0] ? detail : "-", caps.NumSimultaneousRTs, caps.MaxVertexShaderConst,
        caps.PrimitiveMiscCaps, caps.VertexShaderVersion, caps.PixelShaderVersion, format_result,
        history_available_, unsigned(history_.stats().capacity), depth_enabled_, depth_reason,
        depth_detail[0] ? depth_detail : "-", depth_format_result, jitter_requested_, jitter_samples_,
        taa_enabled_, taa_reason, taa_format_result, taa_debug_, lazy_mode_ ? "lazy" : "perdraw",
        camera_state::status(), unsigned(sentinel_mode_), camera_cut_degrees_, camera_log_interval_, state_shadow_, scene_hook_installed_);
}

// One actual mixed-format MRT draw into a 4x4 A8R8G8B8 + A32B32G32R32F pair
// (plus an R32F third target when the device is to produce depth), read back
// through system memory. A device that reports the capability but cannot
// execute the combination is refused here rather than during gameplay.
bool MotionOutput::self_test(bool with_depth, char* reason, std::size_t reason_size) noexcept {
    IDirect3DSurface9* color = nullptr; IDirect3DTexture9* motion = nullptr; IDirect3DSurface9* motion_surface = nullptr;
    IDirect3DTexture9* depth = nullptr; IDirect3DSurface9* depth_surface = nullptr;
    IDirect3DSurface9* color_copy = nullptr; IDirect3DSurface9* motion_copy = nullptr; IDirect3DSurface9* depth_copy = nullptr;
    IDirect3DPixelShader9* shader = nullptr;
    bool ok = false;
    HRESULT hr = S_OK, restore = S_OK, draw = S_OK, scene = S_OK;
    unsigned color_errors = 0, motion_errors = 0, depth_errors = 0;
    const char* stage = "create";
    do {
        if (FAILED(hr = native<CreateRtFn>(CreateRenderTarget)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &color, nullptr))) break;
        if (FAILED(hr = native<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion, nullptr))) break;
        if (FAILED(hr = motion->GetSurfaceLevel(0, &motion_surface))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &color_copy, nullptr))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &motion_copy, nullptr))) break;
        if (with_depth) {
            if (FAILED(hr = native<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth, nullptr))) break;
            if (FAILED(hr = depth->GetSurfaceLevel(0, &depth_surface))) break;
            if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_copy, nullptr))) break;
        }
        if (FAILED(hr = native<CreatePsFn>(CreatePixelShader)(device_, with_depth ? self_test_depth_program : self_test_program, &shader))) break;
        stage = "scene";
        // Attach runs right after CreateDevice, outside any application scene.
        if (FAILED(scene = native<SceneFn>(BeginScene)(device_))) { hr = scene; break; }
        stage = "draw";
        draw = draw_quad(color, motion_surface, depth_surface, shader, 4, 4, &restore);
        scene = native<SceneFn>(EndScene)(device_);
        if (FAILED(draw)) { hr = draw; break; }
        if (FAILED(restore)) { hr = restore; stage = "restore"; break; }
        if (FAILED(scene)) { hr = scene; break; }
        stage = "readback";
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, color, color_copy))) break;
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, motion_surface, motion_copy))) break;
        if (with_depth && FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, depth_surface, depth_copy))) break;
        D3DLOCKED_RECT lock{};
        if (FAILED(hr = color_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            DWORD value = 0; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
            const int a = int(value >> 24), r = int((value >> 16) & 255), g = int((value >> 8) & 255), b = int(value & 255);
            if (a != 255 || r < 62 || r > 66 || g < 126 || g > 130 || b < 189 || b > 193) ++color_errors;
        }
        color_copy->UnlockRect();
        if (FAILED(hr = motion_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            float value[4]; std::memcpy(value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 16, 16);
            if (value[0] != 1.f || value[1] != 2.f || value[2] != 3.f || value[3] != -1.f) ++motion_errors;
        }
        motion_copy->UnlockRect();
        if (with_depth) {
            if (FAILED(hr = depth_copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) break;
            for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
                float value; std::memcpy(&value, static_cast<const char*>(lock.pBits) + y * lock.Pitch + x * 4, 4);
                if (value != self_test_depth_value) ++depth_errors;
            }
            depth_copy->UnlockRect();
        }
        stage = "compare";
        ok = !color_errors && !motion_errors && !depth_errors;
    } while (false);
    release(shader); release(depth_copy); release(motion_copy); release(color_copy);
    release(depth_surface); release(depth); release(motion_surface); release(motion); release(color);
    std::snprintf(reason, reason_size, "stage=%s result=%08lx draw=%08lx restore=%08lx scene=%08lx color_errors=%u motion_errors=%u depth_errors=%u targets=%u",
                  stage, hr, draw, restore, scene, color_errors, motion_errors, depth_errors, with_depth ? 3u : 2u);
    return ok;
}

HRESULT MotionOutput::save_state(SavedState& saved) noexcept {
    saved.target_count = caps_.NumSimultaneousRTs < 4 ? unsigned(caps_.NumSimultaneousRTs) : 4;
    if (!saved.target_count) saved.target_count = 1;
    HRESULT hr;
    for (unsigned i = 0; i < saved.target_count; ++i) {
        hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, i, &saved.targets[i]);
        // Unbound extra targets report D3DERR_NOTFOUND with a null surface.
        if (FAILED(hr) && !(i && hr == D3DERR_NOTFOUND && !saved.targets[i])) return hr;
        if (!i && !saved.targets[0]) return E_FAIL;
    }
    hr = native<GetDepthFn>(GetDepthStencilSurface)(device_, &saved.depth);
    if (FAILED(hr) && !(hr == D3DERR_NOTFOUND && !saved.depth)) return hr;
    if (FAILED(hr = native<GetViewportFn>(GetViewport)(device_, &saved.viewport))) return hr;
    if (FAILED(hr = native<GetScissorFn>(GetScissorRect)(device_, &saved.scissor))) return hr;
    if (FAILED(hr = native<GetFvfFn>(GetFVF)(device_, &saved.fvf))) return hr;
    if (FAILED(hr = native<GetDeclarationFn>(GetVertexDeclaration)(device_, &saved.declaration))) return hr;
    if (FAILED(hr = native<GetVsFn>(GetVertexShader)(device_, &saved.vs))) return hr;
    if (FAILED(hr = native<GetPsFn>(GetPixelShader)(device_, &saved.ps))) return hr;
    if (FAILED(hr = native<GetStreamFn>(GetStreamSource)(device_, 0, &saved.stream, &saved.offset, &saved.stride))) return hr;
    for (unsigned i = 0; i < touched_count; ++i)
        if (FAILED(hr = get_render_state_native(touched_states[i], &saved.states[i]))) return hr;
    return S_OK;
}

// Restores in an order that is correct under D3D9 semantics: SetRenderTarget(0)
// resets viewport and scissor, so those come after the targets; FVF and
// declaration are two views of one binding, restored through whichever the
// application used; DrawPrimitiveUP clears stream 0, so it is rebound.
HRESULT MotionOutput::restore_state(const SavedState& saved) noexcept {
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    step(native<SetRenderTargetFn>(SetRenderTarget)(device_, 0, saved.targets[0]));
    for (unsigned i = 1; i < saved.target_count; ++i)
        step(native<SetRenderTargetFn>(SetRenderTarget)(device_, i, saved.targets[i]));
    step(native<SetDepthFn>(SetDepthStencilSurface)(device_, saved.depth));
    step(native<SetViewportFn>(SetViewport)(device_, &saved.viewport));
    step(native<SetScissorFn>(SetScissorRect)(device_, &saved.scissor));
    if (saved.fvf) step(native<SetFvfFn>(SetFVF)(device_, saved.fvf));
    else step(native<SetDeclarationFn>(SetVertexDeclaration)(device_, saved.declaration));
    step(native<SetVsFn>(SetVertexShader)(device_, saved.vs));
    step(native<SetPsFn>(SetPixelShader)(device_, saved.ps));
    step(native<SetStreamFn>(SetStreamSource)(device_, 0, saved.stream, saved.offset, saved.stride));
    for (unsigned i = 0; i < touched_count; ++i)
        step(native<SetRenderStateFn>(SetRenderState)(device_, touched_states[i], saved.states[i]));
    return first;
}

// Fullscreen XYZRHW strip through the fixed-function vertex path with `shader`
// bound, RT0/RT1/RT2 as given (null unbinds), no depth, full viewport. Returns
// the operation result; *restore receives the restoration result separately.
// Nothing is changed if the initial state query fails.
HRESULT MotionOutput::draw_quad(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DSurface9* rt2,
                                IDirect3DPixelShader9* shader, UINT width, UINT height, HRESULT* restore) noexcept {
    *restore = S_OK;
    SavedState saved;
    HRESULT hr = save_state(saved);
    if (FAILED(hr)) return hr;
    HRESULT op = S_OK;
    auto step = [&](HRESULT result) { if (SUCCEEDED(op) && FAILED(result)) op = result; };
    step(native<SetRenderTargetFn>(SetRenderTarget)(device_, 0, rt0));
    for (unsigned i = 1; i < saved.target_count; ++i)
        step(native<SetRenderTargetFn>(SetRenderTarget)(device_, i, i == 1 ? rt1 : i == 2 ? rt2 : nullptr));
    step(native<SetDepthFn>(SetDepthStencilSurface)(device_, nullptr));
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.f, 1.f};
    step(native<SetViewportFn>(SetViewport)(device_, &viewport));
    step(native<SetFvfFn>(SetFVF)(device_, D3DFVF_XYZRHW));
    step(native<SetVsFn>(SetVertexShader)(device_, nullptr));
    step(native<SetPsFn>(SetPixelShader)(device_, shader));
    for (unsigned i = 0; i < touched_count; ++i)
        step(native<SetRenderStateFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (SUCCEEDED(op)) {
        // Integer raster sample positions: shift by -0.5 so every texel center is covered.
        const float w = float(width) - .5f, h = float(height) - .5f;
        const float quad[4][4] = {{-.5f, -.5f, 0.f, 1.f}, {w, -.5f, 0.f, 1.f}, {-.5f, h, 0.f, 1.f}, {w, h, 0.f, 1.f}};
        step(native<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]));
    }
    *restore = restore_state(saved);
    return op;
}

// Writes the invalid sentinel over the whole motion target and, when the
// device produces depth, over the depth target in the same draw (-1 in
// R32F). Deferred from the latching Clear to the next draw hook so it always
// runs inside the application's BeginScene/EndScene, on both Windows and Wine.
void MotionOutput::fill_sentinel() noexcept {
    fill_pending_ = false;
    restore_bindings(); // The fill's saved state must be the application's (never bound here in practice: the latching Clear restored).
    const bool depth = depth_enabled_ && depth_surface_ && sentinel_mrt_ps_;
    if (!target_surface_ || !sentinel_ps_ || shadow_.recording || (depth_enabled_ && !depth)) { counters_.fill_result = E_ABORT; return; }
    HRESULT restore = S_OK;
    const std::uint64_t begin = stamp();
    const HRESULT hr = draw_quad(target_surface_, depth ? depth_surface_ : nullptr, nullptr,
                                 depth ? sentinel_mrt_ps_ : sentinel_ps_, target_width_, target_height_, &restore);
    const std::uint64_t ticks = stamp() - begin;
    counters_.fill_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteFill), ticks, FAILED(hr) || FAILED(restore));
    counters_.fill_result = hr; counters_.fill_restore = restore;
    counters_.filled = SUCCEEDED(hr) && SUCCEEDED(restore);
    if ((FAILED(hr) || FAILED(restore)) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_fill_failed device=%llu frame=%llu result=%08lx restore=%08lx", id_, frame_, hr, restore);
    }
    if (FAILED(restore)) { ++counters_.restore_failures; invalidate_render_states(); }
}

void MotionOutput::before_reset() noexcept {
    // D3DPOOL_DEFAULT objects must not exist across Reset; shaders survive it.
    // The pass releases its histories, scratch and state block after RT1/RT2
    // and keeps its resolve shader. A lazily bound RT1/RT2 is unbound first.
    restore_bindings();
    release_target();
    if (taa_) taa_call([&] { taa_->before_reset(); });
    target_failed_ = false;
    history_.invalidate();
    selector_.invalidate();
    fill_pending_ = false; pending_valid_ = false;
    main_ = {}; main_depth_ = {};
    camera_state::reset(); camera_previous_ = renderer::CameraState{};
}
void MotionOutput::after_reset(HRESULT result) noexcept {
    ++generation_;
    if (taa_) taa_->after_reset(result);
    scene_open_ = false; // Reset ends any application scene; BeginScene follows.
    if (!enabled_) return;
    // The interrupted frame continues after a successful Reset; capture is off.
    if (SUCCEEDED(result)) { resync_shadow(); begin_frame(frame_, false); }
    log("motion_output_reset device=%llu result=%08lx generation=%llu taa_references=%u", id_, result, generation_, taa_references_);
}

// ---- shader registry -------------------------------------------------------

void MotionOutput::register_vertex_shader(IDirect3DVertexShader9* shader, const DWORD* code,
                                          std::size_t bytes, std::uint64_t hash) noexcept {
    if (!requested_ || !shader) return;
    try {
        auto& entry = vertex_[shader];
        release(entry.variant);
        entry.hash = hash;
        entry.row = nullptr;
        if (shadow_.vs == shader) { shadow_.vs_hash = hash; shadow_.vs_variant = nullptr; shadow_.vs_row = nullptr; }
        if (!enabled_ || !code || bytes % 4) return;
        // One variant per original program: rows sharing this VS agree on its
        // side of the splice (static_assert in motion_output_profiles.h), so
        // the same variant serves every reviewed pair it belongs to. Pair
        // eligibility is decided per draw in before_draw (gate 3). The row
        // lookup is a binary search over the table (no scan).
        entry.row = renderer::material_motion_vertex_row(hash, bytes / 4);
        if (shadow_.vs == shader) shadow_.vs_row = entry.row;
        if (!entry.row) return;
        std::vector<std::uint32_t> words;
        const auto result = renderer::material_motion_vertex_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words, depth_enabled_);
        IDirect3DVertexShader9* variant = nullptr;
        HRESULT hr = E_FAIL;
        if (result == renderer::MaterialMotionResult::Applied)
            hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
        if (SUCCEEDED(hr) && variant) entry.variant = variant;
        log("motion_output_variant device=%llu kind=vs original=%016llx transform=%u create=%08lx words=%u depth=%u",
            id_, hash, unsigned(result), hr, unsigned(words.size()), renderer::material_motion_vertex_exports_depth(*entry.row, depth_enabled_));
    } catch (...) {}
}
void MotionOutput::register_pixel_shader(IDirect3DPixelShader9* shader, const DWORD* code,
                                         std::size_t bytes, std::uint64_t hash) noexcept {
    if (!requested_ || !shader) return;
    try {
        auto& entry = pixel_[shader];
        release(entry.variant);
        entry.hash = hash;
        if (shadow_.ps == shader) { shadow_.ps_hash = hash; shadow_.ps_variant = nullptr; }
        if (!enabled_ || !code || bytes % 4) return;
        entry.row = renderer::material_motion_pixel_row(hash, bytes / 4);
        if (!entry.row) return;
        std::vector<std::uint32_t> words;
        const auto result = renderer::material_motion_pixel_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words, depth_enabled_);
        IDirect3DPixelShader9* variant = nullptr;
        HRESULT hr = E_FAIL;
        if (result == renderer::MaterialMotionResult::Applied)
            hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
        if (SUCCEEDED(hr) && variant) entry.variant = variant;
        log("motion_output_variant device=%llu kind=ps original=%016llx transform=%u create=%08lx words=%u depth=%u",
            id_, hash, unsigned(result), hr, unsigned(words.size()), renderer::material_motion_pixel_writes_depth(*entry.row, depth_enabled_));
    } catch (...) {}
}

// ---- shadow ----------------------------------------------------------------

void MotionOutput::set_vertex_shader(IDirect3DVertexShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.vs = shader; shadow_.vs_hash = 0; shadow_.vs_variant = nullptr; shadow_.vs_row = nullptr;
    if (!shader) return;
    const auto it = vertex_.find(shader);
    if (it == vertex_.end()) return;
    shadow_.vs_hash = it->second.hash;
    shadow_.vs_variant = static_cast<IDirect3DVertexShader9*>(it->second.variant);
    shadow_.vs_row = it->second.row;
}
void MotionOutput::set_pixel_shader(IDirect3DPixelShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.ps = shader; shadow_.ps_hash = 0; shadow_.ps_variant = nullptr;
    if (!shader) return;
    const auto it = pixel_.find(shader);
    if (it == pixel_.end()) return;
    shadow_.ps_hash = it->second.hash;
    shadow_.ps_variant = static_cast<IDirect3DPixelShader9*>(it->second.variant);
}
void MotionOutput::set_vertex_constants_f(UINT start, const float* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start > 4096 || count > 4096) return;
    const UINT end = start + count;
    // Every clip-row window of the table: the exact submitted position rows.
    for (std::size_t w = 0; w < matrix_windows.count; ++w) {
        const UINT base = matrix_windows.base[w], base_end = base + 4;
        if (start >= base_end || end <= base) continue;
        const UINT lo = start > base ? start : base;
        const UINT hi = end < base_end ? end : base_end;
        std::memcpy(shadow_.rows[w] + (lo - base) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        // A partial row update keeps prior knowledge of the other rows.
        shadow_.rows_known[w] = shadow_.rows_known[w] || (lo == base && hi == base_end);
    }
    // Reserved c252-255: remember the application values so a routed draw can put them back.
    if (start < 256 && end > 252) {
        const UINT lo = start > 252 ? start : 252, hi = end < 256 ? end : 256;
        std::memcpy(shadow_.vs_reserved + (lo - 252) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        shadow_.vs_reserved_written = true;
    }
}
void MotionOutput::set_vertex_constants_i(UINT start, const int* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start) return;
    std::memcpy(shadow_.integer0, data, 16);
    shadow_.integer0_known = true;
}
void MotionOutput::set_pixel_constants_f(UINT start, const float* data, UINT count) noexcept {
    if (!enabled_ || shadow_.recording || !data || !count || start > 4096 || count > 4096) return;
    const UINT end = start + count;
    if (start < 218 && end > 216) {
        const UINT lo = start > 216 ? start : 216, hi = end < 218 ? end : 218;
        std::memcpy(shadow_.ps_reserved + (lo - 216) * 4, data + (lo - start) * 4, (hi - lo) * 16);
        shadow_.ps_reserved_written = true;
    }
}
void MotionOutput::set_stream_source(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride) noexcept {
    if (!enabled_ || shadow_.recording || stream) return;
    shadow_.stream0 = buffer ? resource_id(buffer) : 0;
    shadow_.stream0_offset = offset; shadow_.stream0_stride = stride;
}
void MotionOutput::set_indices(IDirect3DIndexBuffer9* buffer) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.indices = buffer ? resource_id(buffer) : 0;
}
// Declaration identity is the hash of its elements (as draw_input does), plus
// the stream-0 POSITION0 layout the key records.
void MotionOutput::set_vertex_declaration(IDirect3DVertexDeclaration9* declaration) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.declaration = 0; shadow_.position_offset = shadow_.position_type = 0;
    if (!declaration) return;
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1]{};
    UINT count = MAXD3DDECLLENGTH + 1;
    if (FAILED(declaration->GetDeclaration(elements, &count)) || count < 2 || count > MAXD3DDECLLENGTH + 1) return;
    bool position = false;
    for (UINT i = 0; i + 1 < count; ++i)
        if (elements[i].Stream == 0 && elements[i].Usage == D3DDECLUSAGE_POSITION && elements[i].UsageIndex == 0) {
            position = true; shadow_.position_offset = elements[i].Offset; shadow_.position_type = elements[i].Type;
        }
    if (!position) return;
    shadow_.declaration = hash_bytes(elements, count * sizeof(elements[0]));
    if (!shadow_.declaration) shadow_.declaration = 1;
}
void MotionOutput::set_fvf(DWORD) noexcept {
    if (!enabled_ || shadow_.recording) return;
    // SetFVF binds a runtime-owned declaration; identify it the same way.
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration))) set_vertex_declaration(declaration);
    else { shadow_.declaration = 0; shadow_.position_offset = shadow_.position_type = 0; }
    release(declaration);
}
void MotionOutput::set_viewport(const D3DVIEWPORT9* viewport) noexcept {
    if (!enabled_ || shadow_.recording || !viewport) return;
    shadow_.viewport = {true, viewport->X, viewport->Y, viewport->Width, viewport->Height, viewport->MinZ, viewport->MaxZ};
}
void MotionOutput::begin_stateblock() noexcept { if (enabled_) shadow_.recording = true; }
void MotionOutput::end_stateblock() noexcept { if (enabled_) { shadow_.recording = false; resync_shadow(); } }
void MotionOutput::stateblock_applied() noexcept { if (enabled_ && !shadow_.recording) resync_shadow(); }

void MotionOutput::describe_binding(DWORD index, IDirect3DSurface9* surface) noexcept {
    if (index == 0) {
        shadow_.rt0 = describe_surface(surface);
        // SetRenderTarget(0) resets the viewport to the new target: read it once.
        D3DVIEWPORT9 viewport{};
        if (SUCCEEDED(native<GetViewportFn>(GetViewport)(device_, &viewport)))
            shadow_.viewport = {true, viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ};
        else shadow_.viewport = {};
    } else if (index < 4) shadow_.extra_rt[index] = surface != nullptr;
}

// Full shadow resynchronization from public getters. Used at attach, after
// Reset, after EndStateBlock and after every state block Apply. Reserved
// constant ranges are conservatively treated as application-written.
void MotionOutput::resync_shadow() noexcept {
    shadow_ = Shadow{}; // Render states included: they refill lazily from the next query.
    ++counters_.rs_resyncs;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    if (SUCCEEDED(native<GetVsFn>(GetVertexShader)(device_, &vs))) set_vertex_shader(vs);
    release(vs);
    if (SUCCEEDED(native<GetPsFn>(GetPixelShader)(device_, &ps))) set_pixel_shader(ps);
    release(ps);
    for (std::size_t w = 0; w < matrix_windows.count; ++w)
        shadow_.rows_known[w] = SUCCEEDED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, matrix_windows.base[w], shadow_.rows[w], 4));
    shadow_.vs_reserved_written = SUCCEEDED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, 252, shadow_.vs_reserved, 4));
    shadow_.integer0_known = SUCCEEDED(native<GetConstantsIFn>(GetVertexShaderConstantI)(device_, 0, shadow_.integer0, 1));
    shadow_.ps_reserved_written = SUCCEEDED(native<GetConstantsFFn>(GetPixelShaderConstantF)(device_, 216, shadow_.ps_reserved, 2));
    IDirect3DVertexBuffer9* stream = nullptr; UINT offset = 0, stride = 0;
    if (SUCCEEDED(native<GetStreamFn>(GetStreamSource)(device_, 0, &stream, &offset, &stride))) set_stream_source(0, stream, offset, stride);
    release(stream);
    IDirect3DIndexBuffer9* indices = nullptr;
    if (SUCCEEDED(native<GetIndicesFn>(GetIndices)(device_, &indices))) set_indices(indices);
    release(indices);
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(native<GetDeclarationFn>(GetVertexDeclaration)(device_, &declaration))) set_vertex_declaration(declaration);
    release(declaration);
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &surface))) shadow_.rt0 = describe_surface(surface);
    release(surface);
    const unsigned targets = caps_.NumSimultaneousRTs < 4 ? unsigned(caps_.NumSimultaneousRTs) : 4;
    for (unsigned i = 1; i < targets; ++i) {
        const HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, i, &surface);
        shadow_.extra_rt[i] = surface || (FAILED(hr) && hr != D3DERR_NOTFOUND);
        release(surface);
    }
    const HRESULT depth_hr = native<GetDepthFn>(GetDepthStencilSurface)(device_, &surface);
    if (SUCCEEDED(depth_hr)) shadow_.depth = describe_surface(surface);
    else if (depth_hr == D3DERR_NOTFOUND && !surface) shadow_.depth.known = true;
    release(surface);
    D3DVIEWPORT9 viewport{};
    if (SUCCEEDED(native<GetViewportFn>(GetViewport)(device_, &viewport)))
        shadow_.viewport = {true, viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ};
}

// ---- scene selector --------------------------------------------------------

renderer::SceneSignatures MotionOutput::signatures() const noexcept {
    renderer::SceneSignatures result{};
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_)
        for (unsigned i = 0; i < 3; ++i) result.background[i] = {fixture_.background_vs[i], fixture_.background_ps[i]};
#endif
    return result;
}
renderer::Event MotionOutput::event(renderer::EventKind kind) noexcept {
    renderer::Event result{}; result.kind = kind; result.sequence = ++sequence_; return result;
}
void MotionOutput::bindings(renderer::Event& e) const noexcept {
    e.rt = shadow_.rt0; e.depth = shadow_.depth; e.viewport = shadow_.viewport;
    e.only_rt0 = !(shadow_.extra_rt[1] || shadow_.extra_rt[2] || shadow_.extra_rt[3]);
}
void MotionOutput::observe(renderer::Event& e, HRESULT result) noexcept {
    e.result_known = true; e.result = static_cast<std::uint32_t>(result);
    const bool was_scene = selector_.state() == renderer::BoundaryState::Scene;
    selector_.observe(e);
    // The scene phase ends with the event that moves the selector past Scene
    // (the bloom copy or a rejection); the cut verdict is complete then, so a
    // future consumer at the copy boundary can read it before Present. A
    // lazily kept binding ends with the phase (already restored by the hook
    // of a Clear/SetRenderTarget/copy; this covers SetDepth and a draw).
    if (was_scene && selector_.state() != renderer::BoundaryState::Scene) {
        restore_bindings();
        if (!cut_finished_) finish_cut_detector();
    }
}
bool MotionOutput::scene_bound() const noexcept {
    const auto& v = shadow_.viewport;
    return selector_.state() == renderer::BoundaryState::Scene && !counters_.hook_scene_end && same(shadow_.rt0, main_) &&
           same(shadow_.depth, main_depth_) && v.known && !v.x && !v.y && v.width == main_.width &&
           v.height == main_.height && v.min_z == 0 && v.max_z == 1 &&
           !(shadow_.extra_rt[1] || shadow_.extra_rt[2] || shadow_.extra_rt[3]);
}

void MotionOutput::begin_frame(std::uint64_t frame, bool capture) noexcept {
    frame_ = frame; capture_ = capture; telemetry_ = telemetry::enabled();
    counters_ = {};
    counters_.cut_median_bound_px = cut_median_bound_; counters_.cut_missing_bound = cut_missing_bound_;
    sequence_ = 0; pending_valid_ = false; fill_pending_ = false; jitter_active_ = false; cut_finished_ = false;
    displacements_.clear();
    camera_scene_ = camera_background_ = renderer::CameraState{};
    camera_projection_address_ = camera_view_address_ = 0;
    if (!enabled_) return;
    selector_ = renderer::SceneBoundarySelector{signatures()};
    selector_.begin_frame(id_, generation_, frame + 1);
}

void MotionOutput::before_clear(DWORD count, DWORD flags, float z) noexcept {
    if (!enabled_) return;
    restore_bindings(); // The Clear must cover the application's targets only.
    pending_ = event(renderer::EventKind::Clear);
    bindings(pending_);
    pending_.clear_flags = flags; pending_.rect_count = count; pending_.clear_z = z;
    pending_valid_ = true;
}
void MotionOutput::after_clear(HRESULT result) noexcept {
    if (!enabled_) return;
    if (!pending_valid_) { selector_.invalidate(); return; }
    pending_valid_ = false;
    const auto before = selector_.state();
    observe(pending_, result);
    // The scene view's camera is final at the depth-only Clear that starts the
    // scene phase (the view activation issues that Clear right after building
    // the matrices); the background view's at the latching Clear (diagnostics).
    if (before == renderer::BoundaryState::Background && selector_.state() == renderer::BoundaryState::Scene) read_camera(true);
    if (before == renderer::BoundaryState::AwaitInitialClear && selector_.state() == renderer::BoundaryState::Background) {
        read_camera(false);
        // The frame's main color/depth pair is latched: own a matching motion
        // target and schedule the sentinel fill for the next draw.
        main_ = pending_.rt; main_depth_ = pending_.depth;
        counters_.latched = true;
        // Advance the jitter sequence once per latched frame: the previous
        // latched frame's jitter is what routed draws report in PS c216.zw.
        jitter_previous_[0] = jitter_[0]; jitter_previous_[1] = jitter_[1];
        const unsigned index = jitter_latched_++ % jitter_samples_;
        jitter_active_ = jitter_requested_;
        jitter_[0] = jitter_active_ ? motion_jitter_sample(index + 1, 0) : 0.f;
        jitter_[1] = jitter_active_ ? motion_jitter_sample(index + 1, 1) : 0.f;
        counters_.jitter_active = jitter_active_; counters_.jitter_index = index;
        counters_.jitter[0] = jitter_[0]; counters_.jitter[1] = jitter_[1];
        counters_.jitter_previous[0] = jitter_previous_[0]; counters_.jitter_previous[1] = jitter_previous_[1];
        if (ensure_target(main_.width, main_.height)) {
            fill_pending_ = true;
            history_.begin_frame({generation_, main_.width, main_.height});
        } else history_.invalidate();
    }
}
void MotionOutput::after_set_render_target(DWORD index, IDirect3DSurface9* surface, HRESULT result) noexcept {
    if (!enabled_) return;
    // Render-target bindings are not state-block state: they apply even while recording.
    if (SUCCEEDED(result)) describe_binding(index, surface);
    auto e = event(renderer::EventKind::SetRenderTarget);
    e.rt_index = index;
    e.rt = index == 0 ? shadow_.rt0 : describe_surface(surface);
    observe(e, result);
}
void MotionOutput::after_set_depth(IDirect3DSurface9* surface, HRESULT result) noexcept {
    if (!enabled_) return;
    if (SUCCEEDED(result)) shadow_.depth = describe_surface(surface);
    auto e = event(renderer::EventKind::SetDepth);
    e.depth = shadow_.depth;
    observe(e, result);
}
void MotionOutput::after_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                                 IDirect3DSurface9* destination, const RECT* destination_rect, HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::Copy);
    if (SUCCEEDED(result)) { e.source = describe_surface(source); e.destination = describe_surface(destination); }
    e.source_rect_null = source_rect == nullptr; e.destination_rect_null = destination_rect == nullptr;
    observe(e, result);
}
void MotionOutput::after_color_fill(IDirect3DSurface9* destination, const RECT* rect, HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::ColorFill);
    if (SUCCEEDED(result)) e.destination = describe_surface(destination);
    e.destination_rect_null = rect == nullptr;
    observe(e, result);
}
void MotionOutput::unsupported(HRESULT result) noexcept {
    if (!enabled_) return;
    auto e = event(renderer::EventKind::Unsupported);
    observe(e, result);
}

// ---- object scope ----------------------------------------------------------

bool MotionOutput::sample_scope(MotionRoute& route) noexcept {
    auto& key = route.key;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_) {
        const auto& s = fixture_.scope;
        if (!s.known) return false;
        key.object_lifetime = s.node_serial; key.camera_lifetime = s.camera_serial;
        key.node = s.node; key.camera = s.camera; key.mesh = s.mesh;
        key.node_handle = s.node_handle; key.camera_handle = s.camera_handle; key.model = s.model; key.lod = s.lod;
        route.load_epoch = s.load_epoch; route.registry_epoch = s.registry_epoch;
        key.draw_domain = (((s.load_epoch & 0xffffffffull) << 32) | (s.registry_epoch & 0xffffffffull)) + 1;
        return true;
    }
#endif
    if (!history_available_) return false;
    object_trace::Snapshot scope{};
    if (!object_trace::current(&scope)) return false;
    constexpr std::uint32_t required = object_trace::Node | object_trace::Camera | object_trace::Registry;
    if ((scope.valid & required) != required || !scope.node || !scope.camera) return false;
    object_lifetime::Snapshot lifetime{};
    if (!object_lifetime::current(scope.registry, scope.node, scope.node_handle, scope.camera, scope.camera_handle, &lifetime) ||
        !lifetime.known || !lifetime.node_serial || !lifetime.camera_serial) return false;
    key.object_lifetime = lifetime.node_serial; key.camera_lifetime = lifetime.camera_serial;
    key.node = scope.node; key.camera = scope.camera; key.mesh = scope.mesh;
    key.node_handle = scope.node_handle; key.camera_handle = scope.camera_handle; key.model = scope.model; key.lod = scope.lod;
    route.load_epoch = lifetime.load_epoch; route.registry_epoch = lifetime.registry_epoch;
    key.draw_domain = (((lifetime.load_epoch & 0xffffffffull) << 32) | (lifetime.registry_epoch & 0xffffffffull)) + 1;
    return true;
}

// ---- per-draw route --------------------------------------------------------

// Undo whatever before_draw already applied, in reverse order.
void MotionOutput::undo(MotionRoute& route) noexcept {
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    if (route.write2_set) step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, route.saved_write2));
    if (route.rt2_set) step(bind_target(2, nullptr));
    if (route.write_set) step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, route.saved_write1));
    if (route.rt_set) step(bind_target(1, nullptr));
    if (route.ps_set) step(native<SetPsFn>(SetPixelShader)(device_, shadow_.ps));
    if (route.vs_set) step(native<SetVsFn>(SetVertexShader)(device_, shadow_.vs));
    // Reserved ranges go back only if this draw changed them and the shadow
    // has seen the application write them; otherwise the application never
    // depends on their contents and the values are left as set.
    if (route.vs_constants_set && shadow_.vs_reserved_written)
        step(native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, 252, shadow_.vs_reserved, 4));
    if (route.ps_constants_set && shadow_.ps_reserved_written)
        step(native<SetConstantsFFn>(SetPixelShaderConstantF)(device_, 216, shadow_.ps_reserved, 2));
    route.write_set = route.rt_set = route.ps_set = route.vs_set = false;
    route.write2_set = route.rt2_set = false;
    route.vs_constants_set = route.ps_constants_set = false;
    if (FAILED(first)) {
        ++counters_.restore_failures; invalidate_render_states();
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx", id_, frame_, counters_.draws, first);
        }
    }
}

// Jitter the submitted clip rows of the bound VS row by this frame's sub-pixel
// offset: rows[0] += jx_ndc * rows[3], rows[1] += jy_ndc * rows[3] with
// jx_ndc = 2 * jx_px / width and jy_ndc = -2 * jy_px / height, so the raster
// image moves by (+jx_px right, +jy_px down), the resolve's convention
// (src/temporal/README.md). The shadow keeps the application's unjittered
// rows: history records them and after_draw writes them back bit-exactly.
void MotionOutput::apply_jitter(MotionRoute& route) noexcept {
    const auto& row = *shadow_.vs_row;
    const std::size_t window = window_of(row.matrix_register);
    if (!shadow_.rows_known[window] || !main_.width || !main_.height) return;
    float rows[16];
    std::memcpy(rows, shadow_.rows[window], sizeof rows);
    const float jx = 2.f * jitter_[0] / float(main_.width), jy = -2.f * jitter_[1] / float(main_.height);
    for (unsigned k = 0; k < 4; ++k) { rows[k] += jx * rows[12 + k]; rows[4 + k] += jy * rows[12 + k]; }
    const std::uint64_t begin = stamp();
    const HRESULT hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, row.matrix_register, rows, 4);
    const std::uint64_t ticks = stamp() - begin;
    ++counters_.jitter_writes; counters_.jitter_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteJitter), ticks, FAILED(hr));
    if (SUCCEEDED(hr)) {
        route.jittered = true; route.jitter_register = row.matrix_register;
        ++counters_.jittered;
    }
}
// The application's own rows, bit-exact from the shadow (after the draw).
void MotionOutput::restore_jitter(MotionRoute& route) noexcept {
    const std::size_t window = window_of(route.jitter_register);
    const std::uint64_t begin = stamp();
    const HRESULT hr = window < motion_matrix_windows_max
        ? native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, route.jitter_register, shadow_.rows[window], 4) : E_FAIL;
    const std::uint64_t ticks = stamp() - begin;
    ++counters_.jitter_writes; counters_.jitter_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteJitter), ticks, FAILED(hr));
    route.jittered = false;
    if (FAILED(hr)) {
        ++counters_.restore_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=jitter_rows", id_, frame_, counters_.draws, hr);
        }
    }
}

// Wraps the decision: times the gate evaluation (route_gate; the fill and
// the apply are excluded and timed on their own) and, in lazy mode, releases
// a kept RT1/RT2 binding before every draw that does not route so a foreign
// program never writes the route's targets.
MotionRoute MotionOutput::before_draw(const MotionDrawCall& call) noexcept {
    MotionRoute route{};
    ++counters_.draws;
    if (!enabled_) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return route; }
    const std::uint64_t begin = stamp(), fill_before = counters_.fill_ticks, flush_before = counters_.lazy_flush_ticks;
    evaluate_draw(call, route);
    if (!route.routed) restore_bindings();
    if (telemetry_) {
        const std::uint64_t total = stamp() - begin,
            excluded = route.ticks + (counters_.fill_ticks - fill_before) + (counters_.lazy_flush_ticks - flush_before);
        const std::uint64_t gate = total > excluded ? total - excluded : 0;
        counters_.gate_ticks += gate;
        record(unsigned(telemetry::Metric::RouteGate), gate);
    }
    return route;
}
void MotionOutput::evaluate_draw(const MotionDrawCall& call, MotionRoute& route) noexcept {
    // Selector event for this draw; z states are the only per-draw getters and
    // only while the selector can still use them.
    const auto state = selector_.state();
    const bool tracking = state == renderer::BoundaryState::Background || state == renderer::BoundaryState::Scene;
    pending_ = event(renderer::EventKind::Draw);
    DWORD z = 0, write = 0;
    HRESULT z_hr = E_FAIL, write_hr = E_FAIL;
    if (tracking) {
        bindings(pending_);
        pending_.topology = call.topology; pending_.primitives = call.primitives;
        pending_.vs = shadow_.vs_hash; pending_.ps = shadow_.ps_hash; pending_.texture0 = 0;
        z_hr = render_state(D3DRS_ZENABLE, &z);
        write_hr = render_state(D3DRS_ZWRITEENABLE, &write);
        pending_.z_enable = z; pending_.z_write = write;
        pending_.draw_state_known = SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && pending_.vs && pending_.ps &&
                                    shadow_.rt0.known && shadow_.depth.known && shadow_.viewport.known;
    }
    pending_valid_ = true;
    if (fill_pending_) fill_sentinel();
    // A draw after the engine's scene-end signal belongs to compositing or an
    // overlay: it neither routes nor jitters (scene_bound refuses); counted
    // for the cross-check (a disagreement when the bloom copy follows it).
    if (counters_.hook_scene_end && state == renderer::BoundaryState::Scene && !counters_.bloom_copy_seen) ++counters_.draws_after_hook;
    // Gate 1: feature/capability, target owned, not recording a state block.
    if (!target_surface_ || shadow_.recording) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return; }
    // Gate 2: scene phase with the latched main color/depth bound.
    if (!scene_bound()) { route.gate = MotionGate::Scene; ++counters_.gates[2]; return; }
    route.scene = true;
    // Every scene draw whose VS has a table row is jittered, routed or not,
    // so the rasterized coverage of the whole scene moves together.
    if (jitter_active_ && shadow_.vs_row) apply_jitter(route);
    // Gate 3: exact reviewed pair (one profile-table row) with both variants
    // registered. Variants are per program; the pair check is what keys
    // eligibility, so a VS alias shared with an unreviewed PS never routes.
    const renderer::MotionOutputProfile* pair = shadow_.vs_variant && shadow_.ps_variant && shadow_.vs_row
        ? renderer::material_motion_profile(shadow_.vs_hash, shadow_.ps_hash) : nullptr;
    if (!pair || !renderer::material_motion_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash)) {
        route.gate = MotionGate::Pair; ++counters_.gates[3]; return;
    }
    route.depth = depth_enabled_ && renderer::material_motion_pixel_writes_depth(*pair, depth_enabled_);
    // Gate 4: opaque ordinary draw state, known rows in the VS row's clip-row
    // window, the row's light-loop bound where it reads constants relatively,
    // no user-memory geometry, no instancing, known declaration/stream identity.
    const auto& profile = *shadow_.vs_row;
    const std::size_t window = window_of(profile.matrix_register);
    const bool loop_bounded = !profile.light_loop_bound_required ||
        (shadow_.integer0_known && shadow_.integer0[0] >= 0 && shadow_.integer0[0] <= int(profile.light_loop_max_count));
    DWORD blend = 1, test = 1, srgb = 1, color = 0; UINT frequency = 0;
    const bool draw_state_ok = !call.user_memory && SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && z == 1 && write == 1 &&
        SUCCEEDED(render_state(D3DRS_ALPHABLENDENABLE, &blend)) && !blend &&
        SUCCEEDED(render_state(D3DRS_ALPHATESTENABLE, &test)) && !test &&
        SUCCEEDED(render_state(D3DRS_SRGBWRITEENABLE, &srgb)) && !srgb &&
        SUCCEEDED(render_state(D3DRS_COLORWRITEENABLE, &color)) && color == 15 &&
        SUCCEEDED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency)) &&
        !(frequency & D3DSTREAMSOURCE_INDEXEDDATA) && (frequency & 0x3fffffffu) <= 1 &&
        shadow_.rows_known[window] && loop_bounded &&
        shadow_.stream0 && shadow_.stream0_stride && shadow_.declaration && call.primitives &&
        (!call.indexed || shadow_.indices);
    if (!draw_state_ok) { route.gate = MotionGate::DrawState; ++counters_.gates[4]; return; }
    // Key geometry fields come from the shadowed bindings and draw arguments.
    auto& key = route.key;
    key.vertex_buffer = shadow_.stream0; key.stream_offset = shadow_.stream0_offset; key.stride = shadow_.stream0_stride;
    key.index_buffer = call.indexed ? shadow_.indices : 0; key.declaration = shadow_.declaration;
    key.position_program = shadow_.vs_hash; key.position_offset = shadow_.position_offset; key.position_type = shadow_.position_type;
    key.topology = call.topology; key.first = call.first; key.primitives = call.primitives;
    key.base_vertex = call.base_vertex; key.min_vertex = call.min_vertex; key.vertex_count = call.vertex_count;
    key.indexed = call.indexed;
    // Pass field: gate 2 established the Scene phase on the latched main pair,
    // the only pass the route keys today (motion_history.h, MotionPass).
    key.pass = renderer::PassMainScene;
    renderer::SubmittedMatrix rows{}, previous{};
    std::memcpy(rows.data(), shadow_.rows[window], sizeof shadow_.rows[window]);
    if (capture_) route.rows_hash = hash_bytes(rows.data(), sizeof rows); // Diagnostics only.
    // Gate 5: verified object/camera scope. Failure still routes with mode 0 so
    // covered pixels of this material carry the sentinel, never stale history.
    bool matched = false;
    if (!sample_scope(route)) { route.gate = MotionGate::Scope; ++counters_.gates[5]; }
    else if (!history_.lookup_and_record(key, rows, previous)) { route.gate = MotionGate::History; ++counters_.gates[6]; ++counters_.keyed; ++counters_.missing; }
    else { matched = true; route.gate = MotionGate::None; ++counters_.gates[0]; ++counters_.keyed; }
    if (matched) {
        // Cut detector sample: screen displacement of the projected object
        // origin (translation column over w) between the previous and the
        // current unjittered rows. Bounded storage, reserved at attach.
        const float* c = rows.data(); const float* p = previous.data();
        if (c[15] > 1e-6f && p[15] > 1e-6f && displacements_.size() < displacements_.capacity()) {
            const float dx = (c[3] / c[15] - p[3] / p[15]) * .5f * float(target_width_);
            const float dy = (c[7] / c[15] - p[7] / p[15]) * .5f * float(target_height_);
            const float magnitude = std::sqrt(dx * dx + dy * dy);
            if (std::isfinite(magnitude)) displacements_.push_back(magnitude);
        }
    }
    // Apply: variant pair, previous rows (or zeros), pixel ABI, RT1/RT2 and
    // their write masks. c216.zw ("previous jitter", subtracted by the motion
    // fragment from the interpolated previous projection) is uploaded as zero:
    // the history rows are the application's unjittered rows, so the fragment
    // already produces the previous UNJITTERED UV of the content at the
    // jittered sample (a static object reports p - current jitter), which the
    // RGBA32F ABI specifies; the resolve reads history at that UV plus the
    // CURRENT jitter and never applies the previous one (src/temporal/README.md).
    // The previous jitter stays in the frame diagnostics only
    // (counters().jitter_previous, FrameInputs::previous_jitter for ABI stability).
    static const float zeros[16]{};
    const float pixel[8] = {1.f / float(target_width_), 1.f / float(target_height_), 0.f, 0.f,
                            matched ? 1.f : 0.f, 0.f, 0.f, 0.f};
    const std::uint64_t apply_begin = stamp();
    HRESULT hr = native<SetVsFn>(SetVertexShader)(device_, shadow_.vs_variant); route.vs_set = SUCCEEDED(hr);
    if (SUCCEEDED(hr)) { hr = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps_variant); route.ps_set = SUCCEEDED(hr); }
    if (SUCCEEDED(hr)) {
        hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_,
            renderer::MaterialMotionAbi::previous_vertex_constant, matched ? previous.data() : zeros, 4);
        route.vs_constants_set = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        hr = native<SetConstantsFFn>(SetPixelShaderConstantF)(device_,
            renderer::MaterialMotionAbi::pixel_coordinates_constant, pixel, 2);
        route.ps_constants_set = SUCCEEDED(hr);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (SUCCEEDED(hr)) { std::memcpy(fixture_last_pixel_abi_, pixel, sizeof pixel); fixture_abi_known_ = true; }
#endif
    }
    if (SUCCEEDED(hr)) hr = bind_targets(route);
    route.ticks = stamp() - apply_begin;
    if (FAILED(hr)) {
        // Partial application: put back what was set and draw the original.
        restore_bindings();
        undo(route);
        ++counters_.apply_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_apply_failed device=%llu frame=%llu index=%lu result=%08lx", id_, frame_, counters_.draws, hr);
        }
        return;
    }
    route.routed = true; route.matched = matched;
    ++counters_.routed; if (matched) ++counters_.matched; if (route.depth) ++counters_.depth_routed;
}

void MotionOutput::after_draw(MotionRoute& route, HRESULT result) noexcept {
    if (!enabled_) return;
    const bool jittered = route.jittered;
    if (route.routed) {
        // route_draw: the apply (before_draw) plus this undo, without the
        // native draw between them and without the jitter writes.
        const std::uint64_t begin = stamp();
        undo(route);
        route.ticks += stamp() - begin;
        counters_.route_draw_ticks += route.ticks;
        record(unsigned(telemetry::Metric::RouteDraw), route.ticks);
    }
    if (route.jittered) restore_jitter(route);
    if (pending_valid_) { pending_valid_ = false; observe(pending_, result); }
    if (capture_ && route.scene) {
        const auto& k = route.key;
        log("motion_route device=%llu frame=%llu index=%lu gate=%u routed=%u matched=%u depth=%u jittered=%u vs=%016llx ps=%016llx node=%p camera=%p node_handle=%lu camera_handle=%lu node_serial=%llu camera_serial=%llu load_epoch=%llu registry_epoch=%llu model=%08lx lod=%08lx vb=%llu ib=%llu declaration=%016llx offset=%u stride=%u position_offset=%u position_type=%u topology=%u first=%u primitives=%u base_vertex=%d min_vertex=%u vertex_count=%u indexed=%u pass=%lu rows_hash=%016llx result=%08lx",
            id_, frame_, counters_.draws, unsigned(route.gate), route.routed, route.matched, route.routed && route.depth, jittered, shadow_.vs_hash, shadow_.ps_hash,
            reinterpret_cast<void*>(k.node), reinterpret_cast<void*>(k.camera), static_cast<unsigned long>(k.node_handle),
            static_cast<unsigned long>(k.camera_handle), k.object_lifetime, k.camera_lifetime, route.load_epoch, route.registry_epoch,
            static_cast<unsigned long>(k.model), static_cast<unsigned long>(k.lod), k.vertex_buffer, k.index_buffer, k.declaration,
            k.stream_offset, k.stride, k.position_offset, k.position_type, k.topology, k.first, k.primitives, k.base_vertex,
            k.min_vertex, k.vertex_count, k.indexed, static_cast<unsigned long>(k.pass), route.rows_hash, result);
    }
}

// ---- frame end -------------------------------------------------------------

// Copies one owned target through system memory to <prefix>_<device>_<frame>.<extension>
// beside the capture log (row-major, bytes_per_pixel per pixel, no header).
HRESULT MotionOutput::readback_surface(IDirect3DSurface9* surface, D3DFORMAT format, unsigned bytes_per_pixel,
                                       const wchar_t* prefix, const wchar_t* extension, const char* tag, const char* format_name) noexcept {
    const std::uint64_t begin = stamp(); // route_readback: allocation, GetRenderTargetData, lock, file write.
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, target_width_, target_height_,
        format, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, surface, copy);
    std::size_t written = 0;
    // Fixed buffers: this runs inside a noexcept hook path, so no std::wstring.
    wchar_t name[96]{}, path[MAX_PATH + 96]{};
    const wchar_t* dir = capture_directory();
    const std::size_t dir_length = std::wcslen(dir);
    swprintf(name, 96, L"\\%ls_%llu_%llu.%ls", prefix, static_cast<unsigned long long>(id_), static_cast<unsigned long long>(frame_), extension);
    if (dir_length + std::wcslen(name) + 1 > sizeof path / sizeof path[0]) hr = E_FAIL;
    else { std::wmemcpy(path, dir, dir_length); std::wcscpy(path + dir_length, name); }
    if (SUCCEEDED(hr)) {
        D3DLOCKED_RECT lock{};
        hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            FILE* file = _wfopen(path, L"wb");
            if (file) {
                for (UINT y = 0; y < target_height_; ++y)
                    written += std::fwrite(static_cast<const char*>(lock.pBits) + y * lock.Pitch, 1, std::size_t(target_width_) * bytes_per_pixel, file);
                if (std::fclose(file)) hr = E_FAIL;
            } else hr = E_FAIL;
            copy->UnlockRect();
        }
    }
    release(copy);
    const std::uint64_t ticks = stamp() - begin;
    ++counters_.readbacks; counters_.readback_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteReadback), ticks, FAILED(hr), written);
    log("%s device=%llu frame=%llu file=%ls_%llu_%llu.%ls width=%u height=%u format=%s result=%08lx bytes=%u",
        tag, id_, frame_, prefix, id_, frame_, extension, target_width_, target_height_, format_name, hr, unsigned(written));
    return hr;
}
// Capture frames: RT1 as motion_<device>_<frame>.rgba32f and, when produced,
// RT2 as depth_<device>_<frame>.r32f (R32F device depth, -1 where no routed
// depth row covered the pixel).
void MotionOutput::readback() noexcept {
    if (!target_surface_ || !counters_.filled) return;
    readback_surface(target_surface_, D3DFMT_A32B32G32R32F, 16, L"motion", L"rgba32f", "motion_output_readback", "rgba32f_row_major");
    if (depth_enabled_ && depth_surface_)
        readback_surface(depth_surface_, D3DFMT_R32F, 4, L"depth", L"r32f", "motion_output_depth_readback", "r32f_row_major");
}

// One read of the engine's projection and view buffers (camera_state.cpp:
// two validated 64-byte copies) into the scene or the background slot.
void MotionOutput::read_camera(bool scene) noexcept {
    if (!taa_enabled_ || !camera_state::available()) return;
    camera_state::Sample sample{};
    const bool valid = camera_state::read(&sample);
    ++counters_.camera_reads;
    if (scene) {
        camera_scene_ = sample.state;
        counters_.camera_scene_valid = valid;
        counters_.camera_read_failure = sample.read_failure; counters_.camera_failure = unsigned(sample.failure);
        camera_projection_address_ = sample.projection; camera_view_address_ = sample.view;
    } else {
        camera_background_ = sample.state;
        counters_.camera_background_valid = valid;
    }
}
// Bounded diagnostics: the scene view's stable projection terms, rotation and
// translation, the background view's deviation from it, the view the history
// holds after this frame (this frame's when it resolved) and the decision of
// this frame's resolve. Capture frames and every X3M_CAMERA_LOG frames.
void MotionOutput::log_camera_state() noexcept {
    const auto& c = camera_scene_;
    const auto& b = camera_background_;
    const auto& t = counters_.taa;
    const float background_rotation = c.valid && b.valid ? renderer::camera_rotation_degrees(c, b) : 0.f;
    log("camera_state device=%llu frame=%llu status=%s reads=%lu valid=%u read_failure=%lu failure=%lu projection=%p view=%p"
        " p00=%.7g p11=%.7g p20=%.7g p21=%.7g r00=%.7g r01=%.7g r02=%.7g r10=%.7g r11=%.7g r12=%.7g r20=%.7g r21=%.7g r22=%.7g t=%.7g,%.7g,%.7g"
        " background_valid=%u background_p00=%.7g background_p11=%.7g background_rotation_deg=%.4f"
        " history_view_valid=%u history_view_frame=%llu rotation_deg=%.4f policy=%lu reason=%lu camera_cut=%u mode=%u cut_deg=%.2f",
        id_, frame_, camera_state::status(), static_cast<unsigned long>(counters_.camera_reads), c.valid,
        static_cast<unsigned long>(counters_.camera_read_failure), static_cast<unsigned long>(counters_.camera_failure),
        reinterpret_cast<void*>(camera_projection_address_), reinterpret_cast<void*>(camera_view_address_),
        c.m00, c.m11, c.m20, c.m21, c.r[0], c.r[1], c.r[2], c.r[3], c.r[4], c.r[5], c.r[6], c.r[7], c.r[8], c.t[0], c.t[1], c.t[2],
        b.valid, b.m00, b.m11, background_rotation,
        camera_previous_.valid, camera_previous_frame_, t.camera_rotation_deg, static_cast<unsigned long>(t.camera_policy),
        static_cast<unsigned long>(t.camera_reason), t.camera_cut, unsigned(sentinel_mode_), camera_cut_degrees_);
}

// End of the frame's scene phase (consumed by the resolve at the copy): the median of the
// matched draws' projected-origin displacements and the fraction of keyed
// routed draws whose key the previous frame lacked, against the bounds. Runs
// once per frame: when the selector leaves Scene, or before Present for a
// frame that never left it (the synthetic fixtures, or a rejected frame).
void MotionOutput::finish_cut_detector() noexcept {
    cut_finished_ = true;
    auto& c = counters_;
    c.displacement_samples = std::uint32_t(displacements_.size());
    c.cut_median_px = 0.f;
    if (!displacements_.empty()) {
        const std::size_t middle = displacements_.size() / 2;
        std::nth_element(displacements_.begin(), displacements_.begin() + middle, displacements_.end());
        c.cut_median_px = displacements_[middle];
    }
    c.cut_missing_fraction = c.keyed ? float(c.missing) / float(c.keyed) : 0.f;
    // The median bound is stated at 1280 px width and scales with the target.
    c.cut_median_bound_px = cut_median_bound_ * (target_width_ ? float(target_width_) / 1280.f : 1.f);
    c.cut_missing_bound = cut_missing_bound_;
    c.cut = (c.displacement_samples && c.cut_median_px > c.cut_median_bound_px) ||
            (c.keyed && c.cut_missing_fraction > c.cut_missing_bound);
    if (capture_)
        log("motion_output_cut device=%llu frame=%llu samples=%lu median_px=%.4f keyed=%lu missing=%lu missing_fraction=%.4f bound_px=%.3f bound_missing=%.3f cut=%u",
            id_, frame_, static_cast<unsigned long>(c.displacement_samples), c.cut_median_px, static_cast<unsigned long>(c.keyed),
            static_cast<unsigned long>(c.missing), c.cut_missing_fraction, c.cut_median_bound_px, c.cut_missing_bound, c.cut);
}

void MotionOutput::before_present() noexcept {
    if (!enabled_) return;
    restore_bindings();
    if (!cut_finished_) finish_cut_detector();
    // Engine hook against selector: the verdict of this frame (SceneEndCheck).
    // A frame that never latched a scene (menu) has nothing to compare.
    {
        auto& c = counters_;
        const bool hook = c.hook_scene_end, copy = c.bloom_copy_seen;
        unsigned check = unsigned(SceneEndCheck::None);
        if (!c.latched) check = unsigned(SceneEndCheck::None);
        else if (c.hook_outside_scene || c.hook_signals > 1 || (hook && copy && c.draws_after_hook) || (!hook && copy && scene_hook_installed_))
            check = unsigned(SceneEndCheck::Disagree);
        else if (hook && copy) check = unsigned(SceneEndCheck::Agree);
        else if (hook) check = unsigned(SceneEndCheck::HookOnly);
        else if (copy) check = unsigned(SceneEndCheck::StretchOnly);
        c.scene_end_check = check;
        if (check == unsigned(SceneEndCheck::Disagree) && logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_scene_hook_disagreement device=%llu frame=%llu installed=%u signals=%lu outside_scene=%lu selector_state=%lu draws_after_hook=%lu bloom_copy_seen=%u hook_scene_end=%u",
                id_, frame_, scene_hook_installed_, static_cast<unsigned long>(c.hook_signals), static_cast<unsigned long>(c.hook_outside_scene),
                static_cast<unsigned long>(c.hook_state), static_cast<unsigned long>(c.draws_after_hook), copy, hook);
        }
    }
    // A frame that did not resolve (menu, rejected before the copy,
    // unrecognized, skipped) leaves no usable history. A rejection after the
    // copy (a different bloom or overlay sequence) does not: the resolved
    // scene is complete and the next frame's correspondence is scene to scene.
    auto& t = counters_.taa;
    if (taa_enabled_) {
        if (!t.attempted) t.skip = unsigned(TaaSkip::NotReached);
        if (!t.resolved) invalidate_taa();
    } else t.skip = unsigned(TaaSkip::Disabled);
    if (capture_) readback();
}
void MotionOutput::after_present(HRESULT result) noexcept {
    if (!enabled_) return;
    if (FAILED(result)) invalidate_taa();
    const bool committed = history_.commit(SUCCEEDED(result) && counters_.filled);
    const auto stats = history_.stats();
    if (capture_ || (telemetry_ && frame_ % frame_log_interval_ == 0)) {
        // Appended cost fields (totals for this frame; docs/verification/telemetry.md):
        // counts are exact, the *_us totals are CPU-side QPC wall clock with
        // telemetry on (timing=cpu_qpc) and zero otherwise (timing=off).
        const auto& c = counters_;
        const auto us = [](std::uint64_t ticks) { return telemetry::microseconds(ticks); };
        log("motion_output_frame device=%llu frame=%llu latched=%u filled=%u fill_result=%08lx fill_restore=%08lx draws=%lu routed=%lu matched=%lu gate1=%lu gate2=%lu gate3=%lu gate4=%lu gate5=%lu gate6=%lu apply_failures=%lu restore_failures=%lu history_previous=%u history_current=%u committed=%u selector_state=%u present=%08lx depth=%u depth_routed=%lu jitter=%u jitter_index=%u jitter_x=%.6f jitter_y=%.6f jitter_previous_x=%.6f jitter_previous_y=%.6f jittered=%lu cut=%u cut_median_px=%.4f cut_missing=%.4f cut_samples=%lu taa=%u taa_attempted=%u taa_resolved=%u taa_history=%u taa_skip=%lu taa_result=%08lx taa_restore=%08lx taa_copy=%08lx scene_open=%u active_queries=%lu taa_references=%u"
            " camera_valid=%u camera_background_valid=%u camera_reads=%lu camera_policy=%lu camera_reason=%lu camera_cut=%u camera_rotation_deg=%.4f"
            " rt_mode=%s timing=%s set_rt=%lu lazy_flushes=%lu jitter_writes=%lu readbacks=%lu gate_us=%.1f route_draw_us=%.1f set_rt_us=%.1f lazy_flush_us=%.1f jitter_us=%.1f fill_us=%.1f taa_run_us=%.1f taa_capture_us=%.1f taa_copy_color_us=%.1f taa_copy_depth_us=%.1f taa_draw_us=%.1f taa_apply_us=%.1f taa_copy_back_us=%.1f readback_us=%.1f"
            " state_shadow=%u rs_queries=%lu rs_hits=%lu rs_gets=%lu rs_resyncs=%lu scene_hook=%u scene_end_source=%s scene_end_check=%lu hook_signals=%lu hook_outside_scene=%lu hook_state=%lu draws_after_hook=%lu bloom_copy_seen=%u",
            id_, frame_, counters_.latched, counters_.filled, counters_.fill_result, counters_.fill_restore,
            static_cast<unsigned long>(counters_.draws), static_cast<unsigned long>(counters_.routed), static_cast<unsigned long>(counters_.matched),
            static_cast<unsigned long>(counters_.gates[1]), static_cast<unsigned long>(counters_.gates[2]), static_cast<unsigned long>(counters_.gates[3]),
            static_cast<unsigned long>(counters_.gates[4]), static_cast<unsigned long>(counters_.gates[5]), static_cast<unsigned long>(counters_.gates[6]),
            static_cast<unsigned long>(counters_.apply_failures), static_cast<unsigned long>(counters_.restore_failures),
            unsigned(stats.previous), unsigned(stats.current), committed, unsigned(selector_.state()), result,
            depth_enabled_, static_cast<unsigned long>(counters_.depth_routed), counters_.jitter_active, counters_.jitter_index,
            counters_.jitter[0], counters_.jitter[1], counters_.jitter_previous[0], counters_.jitter_previous[1],
            static_cast<unsigned long>(counters_.jittered), counters_.cut, counters_.cut_median_px, counters_.cut_missing_fraction,
            static_cast<unsigned long>(counters_.displacement_samples), taa_enabled_, counters_.taa.attempted, counters_.taa.resolved,
            counters_.taa.used_history, static_cast<unsigned long>(counters_.taa.skip), counters_.taa.result, counters_.taa.restore,
            counters_.taa.copy, scene_open_, static_cast<unsigned long>(active_queries_), taa_references_,
            counters_.camera_scene_valid, counters_.camera_background_valid, static_cast<unsigned long>(counters_.camera_reads),
            static_cast<unsigned long>(counters_.taa.camera_policy), static_cast<unsigned long>(counters_.taa.camera_reason),
            counters_.taa.camera_cut, counters_.taa.camera_rotation_deg,
            lazy_mode_ ? "lazy" : "perdraw", telemetry_ ? "cpu_qpc" : "off",
            static_cast<unsigned long>(c.set_rt), static_cast<unsigned long>(c.lazy_flushes), static_cast<unsigned long>(c.jitter_writes),
            static_cast<unsigned long>(c.readbacks), us(c.gate_ticks), us(c.route_draw_ticks), us(c.set_rt_ticks), us(c.lazy_flush_ticks),
            us(c.jitter_ticks), us(c.fill_ticks), us(c.taa_run_ticks), us(c.taa_capture_ticks), us(c.taa_copy_color_ticks),
            us(c.taa_copy_depth_ticks), us(c.taa_draw_ticks), us(c.taa_apply_ticks), us(c.taa_copy_back_ticks), us(c.readback_ticks),
            state_shadow_, static_cast<unsigned long>(c.rs_queries), static_cast<unsigned long>(c.rs_hits), static_cast<unsigned long>(c.rs_gets),
            static_cast<unsigned long>(c.rs_resyncs), scene_hook_installed_, scene_end_source_name(c.taa.source), static_cast<unsigned long>(c.scene_end_check),
            static_cast<unsigned long>(c.hook_signals), static_cast<unsigned long>(c.hook_outside_scene), static_cast<unsigned long>(c.hook_state),
            static_cast<unsigned long>(c.draws_after_hook), c.bloom_copy_seen);
    }
    if (taa_enabled_ && (capture_ || frame_ % camera_log_interval_ == 0)) log_camera_state();
}

#ifdef X3M_MOTION_OUTPUT_FIXTURE
void MotionOutput::fixture_configure(const MotionOutputFixtureConfig& config) noexcept {
    fixture_ = config; fixture_configured_ = true;
}
HRESULT MotionOutput::fixture_last_pixel_abi(float* out, std::size_t floats) const noexcept {
    if (!out || floats < 8) return D3DERR_MOREDATA;
    if (!fixture_abi_known_) return D3DERR_NOTFOUND;
    std::memcpy(out, fixture_last_pixel_abi_, sizeof fixture_last_pixel_abi_);
    return S_OK;
}
HRESULT MotionOutput::fixture_readback(unsigned target, float* out, std::size_t floats, UINT* width, UINT* height) noexcept {
    if (width) *width = target_width_;
    if (height) *height = target_height_;
    if (target != 1 && target != 2) return D3DERR_INVALIDCALL;
    IDirect3DSurface9* surface = target == 1 ? target_surface_ : depth_surface_;
    const unsigned components = target == 1 ? 4u : 1u;
    if (!surface) return D3DERR_NOTFOUND;
    if (!out || floats < std::size_t(target_width_) * target_height_ * components) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, target_width_, target_height_,
        target == 1 ? D3DFMT_A32B32G32R32F : D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, surface, copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < target_height_; ++y)
            std::memcpy(out + std::size_t(y) * target_width_ * components, static_cast<const char*>(lock.pBits) + y * lock.Pitch, std::size_t(target_width_) * components * 4);
        copy->UnlockRect();
    }
    release(copy);
    return hr;
}
#endif
} // namespace x3m
