#include "motion_output.h"
#include "capture.h"
#include "capture_state.h"
#include "scene_capture.h"
#include "telemetry.h"
#include "object_trace.h"
#include "object_lifetime.h"
#include "engine_memory.h"
#include "camera_state.h"
#include "chase_camera.h"
#include "../renderer/material_motion.h"
#include "../renderer/temporal_pass.h"
#include "../renderer/temporal_resolve_program.h"
#include "../renderer/taa_sharpen_program.h"
#include "../renderer/hdr_writeback_program.h"
#include "../renderer/quad_vertex_program.h"
#include "../renderer/hdr_pass.h"
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
    CreateTexture = 23, CreateRenderTarget = 28, GetRenderTargetData = 32, StretchRect = 34, ColorFill = 35,
    CreateOffscreenPlainSurface = 36, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    GetTexture = 64, SetTexture = 65, GetSamplerState = 68, SetSamplerState = 69,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
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
using GetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9**);
using SetSamplerStateFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using GetSamplerStateFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
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
using ColorFillFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, D3DCOLOR);

// The route's own quads (sentinel fill, self test) draw through the shared
// vs_3_0 pass-through (renderer/quad_vertex_program.h), and D3D9 pairs vs_3_0
// with ps_3_0 only, so these hand-written programs carry the ps_3_0 version
// token; `def` and `mov oC0/oC1/oC2` encode identically in ps_2_0 and ps_3_0.
// ps_3_0: def c0, 0, 0, 0, -1 ; mov oC0, c0 ; end. Writes the invalid-history
// sentinel of the RGBA32F motion ABI (alpha -1) to every covered texel.
constexpr DWORD sentinel_program[] = {
    0xffff0300u, 0x05000051u, 0xa00f0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// The same with a second output: oC1 = c0.wwww = (-1, -1, -1, -1) fills the
// R32F depth target (which stores .x only) with its sentinel -1 in the same
// draw; ps_3_0 `def c0, 0, 0, 0, -1; mov oC0, c0; mov oC1, c0.wwww`.
constexpr DWORD sentinel_mrt_program[] = {
    0xffff0300u, 0x05000051u, 0xa00f0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0ff0000u, 0x0000ffffu};
// ps_3_0 self-test: oC0 = (0.25, 0.5, 0.75, 1) into A8R8G8B8, oC1 = (1, 2, 3, -1)
// into A32B32G32R32F. Both targets are read back to prove mixed-format MRT.
constexpr DWORD self_test_program[] = {
    0xffff0300u,
    0x05000051u, 0xa00f0000u, 0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,
    0x05000051u, 0xa00f0001u, 0x3f800000u, 0x40000000u, 0x40400000u, 0xbf800000u,
    0x02000001u, 0x800f0800u, 0xa0e40000u,
    0x02000001u, 0x800f0801u, 0xa0e40001u,
    0x0000ffffu};
// Three-format form: additionally oC2 = (0.625, 0.375, 0.125, 1) into R32F.
constexpr DWORD self_test_depth_program[] = {
    0xffff0300u,
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
const char* hdr_end_name(std::uint32_t end) noexcept {
    switch (static_cast<HdrEnd>(end)) {
    case HdrEnd::Hook: return "hook"; case HdrEnd::BloomCopy: return "bloom_copy"; case HdrEnd::ContentWrite: return "content_write";
    case HdrEnd::Present: return "present"; case HdrEnd::ClearFailed: return "clear_failed"; case HdrEnd::Dropped: return "dropped";
    default: return "none";
    }
}
const char* hdr_source_name(std::uint32_t source) noexcept {
    switch (static_cast<renderer::HdrWritebackSource>(source)) {
    case renderer::HdrWritebackSource::Shader: return "shader"; case renderer::HdrWritebackSource::Stretch: return "stretch";
    case renderer::HdrWritebackSource::Restore: return "restore"; default: return "none";
    }
}
constexpr unsigned hdr_recheck_interval = 60; // latches between recovery self tests while blocked

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
    if (hdr_) count += hdr_->references();
    if (depth_surface_) ++count;
    if (sentinel_ps_) ++count;
    if (sentinel_mrt_ps_) ++count;
    if (quad_vs_) ++count;
    if (quad_declaration_) ++count;
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
    drop_redirect();
    release_target();
    // The passes are destroyed with their objects: the destructor's second call
    // of this function must not probe a device that no longer exists.
    if (hdr_) { hdr_->shutdown(); hdr_.reset(); hdr_enabled_ = false; }
    if (taa_) { taa_call([&] { taa_->shutdown(); }); taa_.reset(); }
    release(sentinel_ps_);
    release(sentinel_mrt_ps_);
    release(quad_vs_); release(quad_declaration_);
    for (auto& entry : vertex_) release(entry.second.variant);
    for (auto& entry : pixel_) release(entry.second.variant);
    shadow_.vs_variant = nullptr; shadow_.ps_variant = nullptr;
    history_.invalidate();
    fill_pending_ = false;
    if (mip_bias_bits_ && !mip_bias_summary_logged_) {
        // Session summary of the bias path (the per-frame line carries the
        // frame's counts); every biased stage was restored by the release hook.
        // Once: the destructor reaches this function a second time.
        mip_bias_summary_logged_ = true;
        log("motion_output_mip_bias_summary device=%llu bias=%g sets=%lu restores=%lu reads=%lu game_writes=%lu failures=%lu biased_now=%04lx",
            id_, double(mip_bias_), static_cast<unsigned long>(mip_bias_total_sets_), static_cast<unsigned long>(mip_bias_total_restores_),
            static_cast<unsigned long>(mip_bias_total_reads_), static_cast<unsigned long>(mip_bias_total_game_writes_),
            static_cast<unsigned long>(mip_bias_total_failures_), static_cast<unsigned long>(sampler_biased_mask_));
    }
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
// Per-draw stamps (gate, apply/undo, SetRenderTarget, jitter, lazy flush) also
// need X3M_TELEMETRY_DRAW=1: under Wine every QPC is a syscall and a routed
// draw took up to 24 of them (docs/verification/route-cost-run1.md, 2.4).
namespace { inline std::uint64_t draw_stamp() noexcept { return telemetry::draw_enabled() ? telemetry::now() : 0; } }
void MotionOutput::record(unsigned metric, std::uint64_t ticks, bool failed, std::uint64_t bytes) noexcept {
    if (telemetry_ && stats_) telemetry::record(*stats_, static_cast<telemetry::Metric>(metric), ticks, failed, bytes);
}
// One route-issued SetRenderTarget of the per-draw apply/undo path or the
// lazy flush: counted per frame and timed per call (route_set_rt).
HRESULT MotionOutput::bind_target(DWORD index, IDirect3DSurface9* surface) noexcept {
    const std::uint64_t begin = draw_stamp();
    const HRESULT hr = native<SetRenderTargetFn>(SetRenderTarget)(device_, index, surface);
    const std::uint64_t ticks = draw_stamp() - begin;
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
    if (sampler_biased_mask_) restore_mip_bias(); // The mip LOD bias shares every restore point.
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
    const std::uint64_t begin = draw_stamp();
    HRESULT first = S_OK;
    auto step = [&](HRESULT hr) { if (SUCCEEDED(first) && FAILED(hr)) first = hr; };
    auto unbind = [&](DWORD index) {
        const std::uint64_t b = draw_stamp();
        const HRESULT hr = native<SetRenderTargetFn>(SetRenderTarget)(device_, index, nullptr);
        const std::uint64_t ticks = draw_stamp() - b;
        ++counters_.set_rt; counters_.set_rt_ticks += ticks;
        if constexpr (!quiet) record(unsigned(telemetry::Metric::RouteSetRenderTarget), ticks, FAILED(hr));
        return hr;
    };
    if (lazy_rt2_) { step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, lazy_write2_)); step(unbind(2)); }
    if (lazy_rt1_) { step(native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, lazy_write1_)); step(unbind(1)); }
    lazy_rt1_ = lazy_rt2_ = false;
    const std::uint64_t ticks = draw_stamp() - begin;
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

// ---- mip LOD bias (X3M_TAA_MIP_BIAS) ---------------------------------------
//
// docs/architecture/temporal-integration.md, "Mip LOD bias for routed
// material draws". The game's ID3DXEffectStateManager shadows sampler state
// per (stage, type) and never writes MIPMAPLODBIAS, so a value the route sets
// stays on the device until the route itself puts the saved value back; the
// restore therefore runs at every restore point of restore_bindings (every
// draw that does not route, Clear, StretchRect, EndScene, Present, Reset, the
// state block hooks, the getters the lazy mode hooks, the final Release).
// Eligibility is decided from the shadow the light SetTexture/SetSamplerState
// hooks feed: a bound texture with more than one level and a MIPFILTER other
// than NONE. The application's texture pointer is compared, never used.

void MotionOutput::configure_mip_bias(float bias) noexcept {
    mip_bias_ = bias;
    if (bias == 0.f || !std::isfinite(bias)) { mip_bias_ = 0.f; mip_bias_bits_ = 0; return; }
    std::memcpy(&mip_bias_bits_, &mip_bias_, sizeof mip_bias_bits_);
}
bool MotionOutput::texture_levels_wanted(DWORD stage, IDirect3DBaseTexture9* texture) const noexcept {
    return texture && stage < sampler_stage_count && samplers_[stage].texture != texture;
}
void MotionOutput::set_texture(DWORD stage, IDirect3DBaseTexture9* texture, DWORD levels, bool queried) noexcept {
    if (stage >= sampler_stage_count || shadow_.recording) return;
    auto& s = samplers_[stage];
    if (queried) s.levels = levels;      // a new pointer: the count the hook read from it
    else if (!texture) s.levels = 0;     // unbound
    s.texture = texture;                 // same pointer, still bound: the count stands
    const std::uint32_t bit = 1u << stage;
    sampler_bound_mask_ = texture ? sampler_bound_mask_ | bit : sampler_bound_mask_ & ~bit;
}
void MotionOutput::set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) noexcept {
    if (stage >= sampler_stage_count || shadow_.recording) return;
    auto& s = samplers_[stage];
    if (type == D3DSAMP_MIPFILTER) { s.mipfilter = value; s.mipfilter_known = true; return; }
    if (type != D3DSAMP_MIPMAPLODBIAS) return;
    // The application's own write replaced whatever the device held: it is
    // the value to restore, and the route's bias is no longer on the device.
    s.saved_bias = value; s.saved_known = true;
    if (s.biased) { s.biased = false; sampler_biased_mask_ &= ~(1u << stage); }
    ++counters_.mip_bias_game_writes; ++mip_bias_total_game_writes_;
    mip_bias_game_write_stage_ = stage; mip_bias_game_write_value_ = value;
}
// Logged from the heavy path (the light hook has no formatter): the first
// failure_log_limit application writes, one line per heavy call at most.
void MotionOutput::log_mip_bias_game_write() noexcept {
    if (mip_bias_logged_game_writes_ == mip_bias_total_game_writes_) return;
    if (mip_bias_logged_game_writes_ < failure_log_limit) {
        float value = 0.f; std::memcpy(&value, &mip_bias_game_write_value_, sizeof value);
        log("motion_output_mip_bias_game_write device=%llu frame=%llu stage=%lu value=%08lx bias=%g writes=%lu",
            id_, frame_, mip_bias_game_write_stage_, mip_bias_game_write_value_, double(value),
            static_cast<unsigned long>(mip_bias_total_game_writes_));
    }
    mip_bias_logged_game_writes_ = mip_bias_total_game_writes_;
}
void MotionOutput::restore_mip_bias_stage(unsigned stage, HRESULT* first) noexcept {
    auto& s = samplers_[stage];
    const HRESULT hr = native<SetSamplerStateFn>(SetSamplerState)(device_, stage, D3DSAMP_MIPMAPLODBIAS, s.saved_bias);
    // Cleared either way: a failed restore leaves the device unknown, and the
    // next routed draw re-reads the value before it sets the bias again.
    s.biased = false; sampler_biased_mask_ &= ~(1u << stage);
    ++counters_.mip_bias_restores; ++mip_bias_total_restores_;
    if (FAILED(hr)) { s.saved_known = false; if (SUCCEEDED(*first)) *first = hr; }
}
void MotionOutput::restore_mip_bias() noexcept {
    HRESULT first = S_OK;
    for (std::uint32_t mask = sampler_biased_mask_; mask; mask &= mask - 1) restore_mip_bias_stage(unsigned(__builtin_ctz(mask)), &first);
    if (FAILED(first)) {
        ++counters_.mip_bias_failures; ++mip_bias_total_failures_; ++counters_.restore_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=mip_bias", id_, frame_, counters_.draws, first);
        }
    }
}
// A routed draw: every bound stage that samples a mip chain gets the bias
// (once; consecutive routed draws find it set), a biased stage whose texture
// or filter no longer qualifies gets its value back.
void MotionOutput::apply_mip_bias() noexcept {
    HRESULT first = S_OK;
    bool any = false;
    for (std::uint32_t mask = sampler_bound_mask_ | sampler_biased_mask_; mask; mask &= mask - 1) {
        const unsigned stage = unsigned(__builtin_ctz(mask));
        auto& s = samplers_[stage];
        bool eligible = s.texture && s.levels > 1;
        if (eligible && !s.mipfilter_known) {
            DWORD value = 0;
            ++counters_.mip_bias_reads; ++mip_bias_total_reads_;
            if (SUCCEEDED(native<GetSamplerStateFn>(GetSamplerState)(device_, stage, D3DSAMP_MIPFILTER, &value))) { s.mipfilter = value; s.mipfilter_known = true; }
            else eligible = false;
        }
        if (eligible) eligible = s.mipfilter != D3DTEXF_NONE;
        if (eligible == s.biased) { any = any || s.biased; continue; }
        if (!eligible) { restore_mip_bias_stage(stage, &first); continue; }
        if (!s.saved_known) {
            DWORD value = 0;
            ++counters_.mip_bias_reads; ++mip_bias_total_reads_;
            if (FAILED(native<GetSamplerStateFn>(GetSamplerState)(device_, stage, D3DSAMP_MIPMAPLODBIAS, &value))) { if (SUCCEEDED(first)) first = E_FAIL; continue; }
            s.saved_bias = value; s.saved_known = true;
        }
        const HRESULT hr = native<SetSamplerStateFn>(SetSamplerState)(device_, stage, D3DSAMP_MIPMAPLODBIAS, mip_bias_bits_);
        ++counters_.mip_bias_sets; ++mip_bias_total_sets_;
        if (FAILED(hr)) { if (SUCCEEDED(first)) first = hr; continue; }
        s.biased = true; sampler_biased_mask_ |= 1u << stage; counters_.mip_bias_stages |= 1u << stage; any = true;
    }
    if (any) ++counters_.mip_bias_draws;
    if (FAILED(first)) {
        ++counters_.mip_bias_failures; ++mip_bias_total_failures_;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_apply_failed device=%llu frame=%llu index=%lu result=%08lx what=mip_bias", id_, frame_, counters_.draws, first);
        }
    }
}
// State block Apply and Reset can change bindings and sampler state behind
// the hooks: the bias is already restored (both are restore points), the
// shadow re-reads the bindings natively and forgets the filter and saved values.
void MotionOutput::resync_samplers() noexcept {
    sampler_bound_mask_ = sampler_biased_mask_ = 0;
    for (unsigned stage = 0; stage < sampler_stage_count; ++stage) {
        auto& s = samplers_[stage];
        s = SamplerShadow{};
        if (!mip_bias_bits_) continue;
        IDirect3DBaseTexture9* texture = nullptr;
        if (SUCCEEDED(native<GetTextureFn>(GetTexture)(device_, stage, &texture)) && texture) {
            s.texture = texture; s.levels = texture->GetLevelCount();
            sampler_bound_mask_ |= 1u << stage;
            release(texture);
        }
    }
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
    // The sharpen program is created only when the switch is on: with it off
    // the pass is the pre-sharpen pass, shader for shader.
    // The identity copy program (the HDR write-back's) serves the draw copy
    // mode; it is created in both modes so the pass holds the same references
    // whichever mode the device decided (taa_copy in motion_output_device).
    taa_call([&] { hr = taa_->initialize(device_, nullptr, reinterpret_cast<const DWORD*>(renderer::temporal_resolve_program()), native_,
                                         taa_sharpen_ > 0.f ? reinterpret_cast<const DWORD*>(renderer::taa_sharpen_program()) : nullptr,
                                         reinterpret_cast<const DWORD*>(renderer::hdr_writeback_program())); });
    if (SUCCEEDED(hr)) taa_->configure_copy(taa_copy_draw_);
    taa_failed_ = FAILED(hr);
    log("motion_output_taa device=%llu initialize=%08lx references=%u sharpen=%.3f copy=%s", id_, hr, taa_references_, double(taa_sharpen_), taa_copy_draw_ ? "draw" : "stretch");
    return !taa_failed_;
}
// The whole resolve at the bloom copy: RT1/RT2 containers as inputs, the
// application's main surface as the 8-bit color input, then the copy-back.
// The main target is written only after run() succeeded; any failure leaves it
// untouched, invalidates history and is logged once for the frame.
HRESULT MotionOutput::resolve(IDirect3DSurface9* main_surface, IDirect3DTexture9* hdr_scene) noexcept {
    auto& t = counters_.taa;
    IDirect3DTexture9* motion = nullptr; IDirect3DTexture9* depth = nullptr;
    renderer::Output out{};
    HRESULT hr = E_FAIL;
    hdr_resolved_ = nullptr;
    t.hdr = hdr_scene != nullptr; t.k = hdr_scene ? hdr_taa_k_ : 0.f;
    // Debug readback of the pre-resolve colour on the HDR path: the 8-bit
    // main target holds the previous write-back, so the unresolved scene is
    // written back first (a flush; the redirect continues) and read.
    if (hdr_scene && capture_ && taa_debug_) flush_redirect();
    taa_call([&] {
        hr = target_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&motion));
        if (SUCCEEDED(hr)) hr = depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth));
        if (SUCCEEDED(hr) && (!motion || !depth)) hr = E_NOINTERFACE;
        if (FAILED(hr)) { t.skip = unsigned(TaaSkip::Container); t.result = hr; invalidate_taa(); }
        else {
            if (capture_ && taa_debug_)
                // GetRenderTargetData needs the exact format of the main target (A8R8G8B8 or X8R8G8B8).
                readback_surface(main_surface, static_cast<D3DFORMAT>(main_.format), 4, L"color", L"bgra8", "motion_output_color_readback", "bgra8_row_major", target_width_, target_height_);
            renderer::FrameInputs in{};
            if (hdr_scene) { in.color = hdr_scene; in.luminance_k = hdr_taa_k_; } else in.color_surface = main_surface;
            // Post-resolve sharpen on the 8-bit route: the pass draws RCAS of
            // its history into the main target in place of the copy-back
            // below (the HDR route sharpens in the write-back instead).
            in.sharpen = hdr_scene || taa_sharpen_failures_ >= sharpen_failure_limit ? 0.f : taa_sharpen_;
            in.current_depth = depth; in.motion = motion;
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
            // A chase-camera snap (X3M_CAMERA=chase) is a cut too: the smoothed
            // view is discontinuous there even when the rotation stays under
            // the bound (a gate jump keeps the orientation and moves the world).
            // Each device observes the broadcast independently. A failed run
            // below invalidates this history, so submitting the cut here may
            // advance its cursor even if no resolved image is published.
            const bool chase_snap = chase_snap_cursor_.observe(chase_camera::snap_generation());
            in.history_allowed = true; in.cut = counters_.cut || decision.cut || chase_snap;
            if (chase_snap) t.camera_cut = true;
            in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording;
            in.caller_queries_idle = active_queries_ == 0;
            // Phase timing of the run (telemetry only): the pass stamps its own
            // five phases; the whole call is timed here and nests them.
            taa_->configure_timing(telemetry_);
            const std::uint64_t run_begin = stamp();
#ifdef X3M_MOTION_OUTPUT_FIXTURE
            // Fixture seam (HdrFault::Resolve): the run "fails" without touching
            // the device; the pass drops its history as a failed run would.
            const bool injected = hdr_scene && hdr_ && hdr_->take_fault(renderer::HdrFault::Resolve);
#else
            constexpr bool injected = false;
#endif
            if (injected) { hr = E_FAIL; taa_->invalidate(); } else hr = taa_->run(in, &out);
            const std::uint64_t run_ticks = stamp() - run_begin;
            const auto diagnostics = taa_->diagnostics();
            t.result = injected ? hr : diagnostics.operation; t.restore = diagnostics.restoration;
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
                    readback_surface(out.color_surface, D3DFMT_A16B16G16R16F, 8, L"taa", L"rgba16f", "motion_output_taa_readback", "rgba16f_row_major", target_width_, target_height_);
                if (hdr_scene) {
                    // Stage 3: no copy. The write-back that ends the redirect
                    // samples the resolved FP16 image (tonemap and meter), and
                    // the history already holds it.
                    hdr_resolved_ = out.color; t.copy = S_FALSE;
                } else if (out.display_written) {
                    // The pass drew the display image into the main target
                    // itself (the sharpened image, or the draw copy mode's
                    // identity write-back): no copy-back (taa_copy stays S_FALSE).
                    t.copy = S_FALSE; t.sharpened = in.sharpen > 0.f;
                } else {
                    if (in.sharpen > 0.f) {
                        // The sharpened draw failed without losing the device: the
                        // resolve stands, the copy-back presents it unsharpened;
                        // repeated failures disable the sharpen for the device.
                        ++taa_sharpen_failures_;
                        if (logged_failures_ < failure_log_limit) {
                            ++logged_failures_;
                            log("motion_output_sharpen_failed device=%llu frame=%llu result=%08lx failures=%u disabled=%u",
                                id_, frame_, out.sharpen_result, taa_sharpen_failures_, taa_sharpen_failures_ >= sharpen_failure_limit);
                        }
                    }
                    // Point-filtered full-rect copy of the resolved FP16 image back into
                    // the 8-bit main target; StretchRect changes no device state.
                    const std::uint64_t copy_begin = stamp();
                    hr = native<StretchFn>(StretchRect)(device_, out.color_surface, nullptr, main_surface, nullptr, D3DTEXF_POINT);
                    const std::uint64_t copy_ticks = stamp() - copy_begin;
                    counters_.taa_copy_back_ticks += copy_ticks;
                    record(unsigned(telemetry::Metric::TaaCopyBack), copy_ticks, FAILED(hr));
                    t.copy = hr;
                }
                if (FAILED(hr)) invalidate_taa();
                else {
                    t.resolved = true; t.used_history = out.used_history;
                    // The history now holds this frame: its scene view is the
                    // previous view of the next resolve (invalid when unread).
                    camera_previous_ = camera_scene_; camera_previous_frame_ = frame_;
                }
                // --taa-debug: the main target after the sharpen draw or the
                // copy-back, i.e. the image this resolve presents (the taa
                // readback above is the unsharpened history input); the HDR
                // route reads it after its write-back instead (hdr_writeback).
                if (SUCCEEDED(hr) && !hdr_scene && capture_ && taa_debug_)
                    readback_surface(main_surface, static_cast<D3DFORMAT>(main_.format), 4, L"present", L"bgra8", "motion_output_present_readback", "bgra8_row_major", target_width_, target_height_);
            }
        }
        release(depth); release(motion);
    });
    if (FAILED(hr) && logged_failures_ < failure_log_limit) {
        ++logged_failures_;
        log("motion_output_taa_failed device=%llu frame=%llu skip=%lu result=%08lx restore=%08lx copy=%08lx scene_open=%u hdr=%u",
            id_, frame_, static_cast<unsigned long>(t.skip), t.result, t.restore, t.copy, scene_open_, t.hdr);
    }
    return hr;
}
// Stage 3 of the HDR scene path (docs/architecture/hdr-scene-path.md,
// section 4): while the redirect is active the frame's resolve consumes the
// FP16 scene target directly -- RT0 is the target, the pass saves and restores
// that physical binding -- and its output becomes the source of the write-back
// that follows (end_redirect). A failed run leaves hdr_resolved_ null: the
// write-back then presents the unresolved scene (never a black frame) and the
// pass has dropped its history; motion_output_taa_failed names the reason.
bool MotionOutput::resolve_hdr(SceneEndSource source) noexcept {
    hdr_resolved_ = nullptr;
    if (!taa_enabled_ || hdr_state_ != HdrState::Active || !hdr_ || !hdr_->target() || !hdr_main_) return false;
    if (!resolve_allowed(source)) return false;
    auto& t = counters_.taa;
    // The latch bound the target as RT0; anything else (an application bind
    // the shim did not substitute) is skip 10, exactly as on the 8-bit path.
    IDirect3DSurface9* rt0 = nullptr;
    const HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0);
    const bool bound = SUCCEEDED(hr) && rt0 == hdr_->target();
    release(rt0);
    if (!bound) { t.skip = unsigned(TaaSkip::Target); t.result = FAILED(hr) ? hr : E_FAIL; t.hdr = true; invalidate_taa(); return true; }
    // The target's texture (the pass validates format, size and device; one
    // reference for the duration of the run).
    IDirect3DTexture9* scene = nullptr;
    HRESULT container = hdr_->target()->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&scene));
    if (SUCCEEDED(container) && !scene) container = E_NOINTERFACE;
    if (FAILED(container)) { t.skip = unsigned(TaaSkip::Container); t.result = container; t.hdr = true; invalidate_taa(); return true; }
    resolve(hdr_main_, scene);
    release(scene);
    return true;
}
void MotionOutput::before_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                                  IDirect3DSurface9* destination, const RECT* destination_rect) noexcept {
    // The application's copy (and the resolve, which samples RT1/RT2) must
    // see the application's bindings.
    restore_bindings();
    if (!enabled_) return;
    // Would this copy advance the selector out of AwaitCopy? Probe a copy of
    // it with the event after_stretch will feed (same sequence number, not
    // consumed here); only the main-target bloom copy qualifies.
    bool bloom = false;
    if (selector_.state() == renderer::BoundaryState::AwaitCopy) {
        renderer::Event e{};
        e.kind = renderer::EventKind::Copy; e.sequence = sequence_ + 1; e.result_known = true; e.result = 0;
        e.source = describe_surface(source); e.destination = describe_surface(destination);
        e.source_rect_null = source_rect == nullptr; e.destination_rect_null = destination_rect == nullptr;
        renderer::SceneBoundarySelector probe = selector_;
        probe.observe(e);
        bloom = probe.state() == renderer::BoundaryState::AwaitBloomTarget;
    }
    // HDR redirect: the bloom copy is the scene end (stage 3: the resolve on
    // the FP16 target first, then the write-back of its output and the rebind
    // of the main target, so the application copies the resolved image); any
    // other copy reading the main target flushes first, one writing it ends
    // first.
    if (hdr_state_ != HdrState::Off) {
        if (bloom) { resolve_hdr(SceneEndSource::StretchRect); end_redirect(HdrEnd::BloomCopy); }
        else if (hdr_is_main(destination)) end_redirect(HdrEnd::ContentWrite);
        else if (hdr_is_main(source)) flush_redirect();
    }
    if (!bloom) return;
    counters_.bloom_copy_seen = true; // The selector's scene end (cross-checked against the engine hook at Present).
    if (!taa_enabled_) return;
    // The copy path is the fallback: a frame the engine hook (or the HDR
    // resolve above) already resolved or attempted is left alone here.
    if (!resolve_allowed(SceneEndSource::StretchRect)) return;
    resolve(source, nullptr);
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
    // A multisampled main target routed nothing (no RT1/RT2, no jitter).
    if (main_msaa_) return skip(TaaSkip::Msaa);
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
void MotionOutput::scene_end_hook(MotionHdrSceneCallback callback, void* context) noexcept {
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
    // The FP16 scene ends here. Stage 3 order (section 4 of the HDR design):
    // the resolve on the FP16 target while it is RT0, then the write-back of
    // the resolved image (meter, tonemap) and the rebind of the main target
    // before the compositor's GetRenderTarget(0)
    // (docs/reverse-engineering/compositor-and-glow.md, 7.3). Without the
    // redirect the write-back is a no-op and the resolve runs on the 8-bit
    // RT0 below, as before.
    resolve_hdr(SceneEndSource::Hook);
    end_redirect(HdrEnd::Hook, callback, context);
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
    resolve(rt0, nullptr);
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
    HRESULT format_result = S_OK, depth_format_result = S_OK, taa_format_result = S_OK, stretch_query = S_OK;
    quad_fvf_ = renderer::quad_fvf_requested();
    taa_copy_draw_ = false; std::snprintf(taa_stretch_test_, sizeof taa_stretch_test_, "off");
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    { char setting[8]{}; fixture_stretch_fault_ = GetEnvironmentVariableA("X3M_FIXTURE_STRETCH_FAULT", setting, sizeof setting) == 1 && setting[0] == '1'; }
#endif
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
                // The FP16 scratch copy and the copy-back may convert between
                // the 8-bit main target and A16B16G16R16F through StretchRect,
                // which native D3D9 grants only where the driver reports the
                // conversion; the adapter's answer is one half of the taa_copy
                // decision below (the live round trip is the other), never a
                // refusal: without it the pass copies by same-format
                // StretchRect and identity draws.
                for (D3DFORMAT eight_bit : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8}) {
                    if (SUCCEEDED(stretch_query) &&
                        (FAILED(stretch_query = factory->CheckDeviceFormatConversion(creation.AdapterOrdinal, creation.DeviceType,
                             eight_bit, D3DFMT_A16B16G16R16F)) ||
                         FAILED(stretch_query = factory->CheckDeviceFormatConversion(creation.AdapterOrdinal, creation.DeviceType,
                             D3DFMT_A16B16G16R16F, eight_bit)))) break;
                }
            }
        }
        release(factory);
    }
    depth_enabled_ = !std::strcmp(reason, "ok") && !std::strcmp(depth_reason, "ok");
    if (!std::strcmp(reason, "ok")) {
        // The quad's vs_3_0 pass-through and declaration (every route quad binds them).
        HRESULT hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(renderer::quad_vertex_program()), &quad_vs_);
        if (SUCCEEDED(hr)) hr = native<CreateDeclarationFn>(CreateVertexDeclaration)(device_, renderer::quad_declaration, &quad_declaration_);
        if (FAILED(hr) || !quad_vs_ || !quad_declaration_) { reason = "quad_shader"; std::snprintf(detail, sizeof detail, "%08lx", hr); release(quad_vs_); release(quad_declaration_); }
    }
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
    if (!enabled_) { release(sentinel_ps_); release(sentinel_mrt_ps_); release(quad_vs_); release(quad_declaration_); depth_enabled_ = false; }
    else { resync_shadow(); begin_frame(0, false); } // The first frame has no preceding Present.
    if (!history_available_) history_.invalidate();
    if (taa_requested_ && !std::strcmp(taa_reason, "ok")) {
        if (!enabled_) taa_reason = "route";
        else if (!depth_enabled_) taa_reason = "depth";
        else if (!jitter_requested_) taa_reason = "jitter";
    }
    taa_enabled_ = taa_requested_ && !std::strcmp(taa_reason, "ok");
    // D1: the format-converting StretchRect is used only where the adapter
    // grants the conversion AND a live 4x4 round trip per 8-bit format
    // reproduces the bytes; otherwise the pass copies by same-format
    // StretchRect plus identity draws (taa_copy=draw). Never a refusal.
    if (taa_enabled_) {
        taa_copy_draw_ = true;
        char stretch_detail[96] = "";
        if (fixture_stretch_fault()) std::snprintf(taa_stretch_test_, sizeof taa_stretch_test_, "fault");
        else if (FAILED(stretch_query)) std::snprintf(taa_stretch_test_, sizeof taa_stretch_test_, "query:%08lx", stretch_query);
        else {
            bool pass = true;
            for (D3DFORMAT eight_bit : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8})
                if (pass && !stretch_round_trip(eight_bit, stretch_detail, sizeof stretch_detail)) pass = false;
            if (pass) std::snprintf(taa_stretch_test_, sizeof taa_stretch_test_, "pass");
            else std::snprintf(taa_stretch_test_, sizeof taa_stretch_test_, "%s", stretch_detail);
            taa_copy_draw_ = !pass;
        }
    }
    scene_open_ = false; active_queries_ = 0;
    // FP16 HDR scene path: the pass gates itself (section 5 of the design) and
    // runs the four-format self test outside the application's scene.
    hdr_enabled_ = false;
    if (hdr_requested_) {
        const char* hdr_reason = "route";
        renderer::HdrCaps hdr_caps{};
        D3DFORMAT hdr_main_format = D3DFMT_A8R8G8B8;
        if (enabled_) {
            try { hdr_ = std::make_unique<renderer::HdrPass>(); } catch (...) { hdr_.reset(); }
            if (hdr_) {
#ifdef X3M_MOTION_OUTPUT_FIXTURE
                if (fixture_hdr_fault_count_) { hdr_->set_fault(static_cast<renderer::HdrFault>(fixture_hdr_fault_kind_), fixture_hdr_fault_count_); fixture_hdr_fault_count_ = 0; }
#endif
                // The back buffer's format (A8R8G8B8 for the game, X8R8G8B8
                // possible) decides the emergency StretchRect conversion query.
                IDirect3DSurface9* rt0 = nullptr; D3DSURFACE_DESC rt0_desc{};
                if (SUCCEEDED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0)) && rt0 && SUCCEEDED(rt0->GetDesc(&rt0_desc)) && rt0_desc.Format != D3DFMT_UNKNOWN)
                    hdr_main_format = rt0_desc.Format;
                release(rt0);
                hdr_->configure(hdr_config_);
                hdr_->attach(device_, native_, caps, hdr_main_format, depth_enabled_);
                hdr_caps = hdr_->caps(); hdr_reason = hdr_caps.reason;
                hdr_enabled_ = hdr_->enabled();
                if (!hdr_enabled_) { hdr_->shutdown(); hdr_.reset(); }
            } else hdr_reason = "allocation";
        }
        log("hdr_device device=%llu enabled=%u reason=%s fp16_target=%08lx fp16_blending=%08lx fp16_filter=%08lx fp16_sampling=%08lx stretch_conversion=%08lx main_format=%u mrt_blending=%u self_test_targets=%u self_test=%s route=%u depth=%u",
            id_, hdr_enabled_, hdr_reason, hdr_caps.fp16_target, hdr_caps.fp16_blending, hdr_caps.fp16_filter, hdr_caps.fp16_sampling,
            hdr_caps.stretch_conversion, unsigned(hdr_main_format), hdr_caps.mrt_blending, hdr_caps.self_test_targets, hdr_caps.self_test_detail, enabled_, depth_enabled_);
        // Stage 2: the tonemap and meter verdicts and the switches in force.
        const auto& c = hdr_config_; const auto& x = c.params;
        log("hdr_tonemap device=%llu enabled=%u requested=%s tonemap=%u tonemap_reason=%s meter=%u meter_reason=%s look=%s decode=%s clamp=%g exposure=%s ev_manual=%.4f ev_offset=%.4f key=%.4f ev_min=%.2f ev_max=%.2f tau_up=%.3f tau_down=%.3f meter_floor=%g meter_clip=%g meter_bg=%g meter_min_lit=%g white_target=%g key_pull=%g ev_deadband=%g edge_weight=%g tile_max=%u fixed_dt_ms=%.3f tonemap_shader=%08lx meter_shader=%08lx chain_format=%s chain_target=%08lx chain_sampling=%08lx",
            id_, hdr_enabled_, renderer::hdr_tonemap_name(c.tonemap), hdr_caps.tonemap, hdr_caps.tonemap_reason, hdr_caps.meter, hdr_caps.meter_reason,
            renderer::hdr_look_name(c.look), renderer::hdr_decode_name(c.decode), double(c.clamp_max), renderer::hdr_exposure_name(c.exposure),
            double(c.ev_manual), double(x.ev_offset), double(x.key), double(x.ev_min), double(x.ev_max), double(x.tau_up), double(x.tau_down),
            double(x.meter_floor), double(x.meter_clip), double(x.meter_bg), double(x.meter_min_lit), double(x.white_target), double(x.key_pull),
            double(x.ev_deadband), double(x.meter_edge_weight), renderer::kMeterTileMax,
            double(c.fixed_dt) * 1000., hdr_caps.tonemap_shader, hdr_caps.meter_shader, hdr_caps.chain_format_name, hdr_caps.chain_target, hdr_caps.chain_sampling);
        hdr_tonemap_disabled_logged_ = false; hdr_taa_k_ = 0.f;
    }
    log("motion_output_device device=%llu enabled=%u reason=%s detail=%s mrt=%lu vs_constants=%lu misc=%08lx vs=%08lx ps=%08lx rgba32f=%08lx history_available=%u history_capacity=%u depth=%u depth_reason=%s depth_detail=%s r32f=%08lx jitter=%u jitter_samples=%u taa=%u taa_reason=%s taa_format=%08lx taa_copy=%s taa_stretch_query=%08lx taa_stretch_test=%s taa_debug=%u rt_mode=%s camera=%s sentinel=%u camera_cut_deg=%.2f camera_log=%u state_shadow=%u scene_hook=%u hdr=%u mip_bias=%g quad_fvf=%u",
        id_, enabled_, reason, detail[0] ? detail : "-", caps.NumSimultaneousRTs, caps.MaxVertexShaderConst,
        caps.PrimitiveMiscCaps, caps.VertexShaderVersion, caps.PixelShaderVersion, format_result,
        history_available_, unsigned(history_.stats().capacity), depth_enabled_, depth_reason,
        depth_detail[0] ? depth_detail : "-", depth_format_result, jitter_requested_, jitter_samples_,
        taa_enabled_, taa_reason, taa_format_result, taa_enabled_ ? (taa_copy_draw_ ? "draw" : "stretch") : "off", stretch_query, taa_stretch_test_, taa_debug_, lazy_mode_ ? "lazy" : "perdraw",
        camera_state::status(), unsigned(sentinel_mode_), camera_cut_degrees_, camera_log_interval_, state_shadow_, scene_hook_installed_, hdr_enabled_,
        double(mip_bias_), quad_fvf_);
}

// One 4x4 round trip of `format` through A16B16G16R16F and back with the
// format-converting StretchRect the stretch copy mode relies on: 16 ColorFills
// of distinct colours into a render-target surface, StretchRect into an FP16
// render-target texture level and from there into a second surface of the
// same 8-bit format, inside our own scene bracket (attach runs outside the
// application's), then GetRenderTargetData of all three. The 8-bit bytes
// must come back exactly (RGB only for X8R8G8B8: its alpha is undefined) and
// the FP16 values must be within 1/1024 of v/255. Nothing here changes device
// state. `detail` receives a single-token verdict for the device line.
bool MotionOutput::stretch_round_trip(D3DFORMAT format, char* detail, std::size_t detail_size) noexcept {
    IDirect3DSurface9* source = nullptr; IDirect3DSurface9* result = nullptr;
    IDirect3DTexture9* middle = nullptr; IDirect3DSurface9* middle_surface = nullptr;
    IDirect3DSurface9* source_copy = nullptr; IDirect3DSurface9* result_copy = nullptr; IDirect3DSurface9* middle_copy = nullptr;
    const char* stage = "create";
    HRESULT hr = S_OK, first = S_FALSE, second = S_FALSE, scene = S_OK;
    unsigned byte_errors = 0, half_errors = 0;
    bool ok = false;
    const DWORD mask = format == D3DFMT_X8R8G8B8 ? 0x00ffffffu : 0xffffffffu;
    auto pattern = [](unsigned i) { return D3DCOLOR(((255u - i * 13u) << 24) | ((i * 16u + 7u) << 16) | ((255u - i * 16u) << 8) | ((i * 37u + 3u) & 255u)); };
    do {
        if (FAILED(hr = native<CreateRtFn>(CreateRenderTarget)(device_, 4, 4, format, D3DMULTISAMPLE_NONE, 0, FALSE, &source, nullptr))) break;
        if (FAILED(hr = native<CreateRtFn>(CreateRenderTarget)(device_, 4, 4, format, D3DMULTISAMPLE_NONE, 0, FALSE, &result, nullptr))) break;
        if (FAILED(hr = native<CreateTextureFn>(CreateTexture)(device_, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &middle, nullptr))) break;
        if (FAILED(hr = middle->GetSurfaceLevel(0, &middle_surface))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, format, D3DPOOL_SYSTEMMEM, &source_copy, nullptr))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, format, D3DPOOL_SYSTEMMEM, &result_copy, nullptr))) break;
        if (FAILED(hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, 4, 4, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &middle_copy, nullptr))) break;
        stage = "fill";
        for (unsigned i = 0; i < 16 && SUCCEEDED(hr); ++i) {
            const RECT cell{LONG(i % 4), LONG(i / 4), LONG(i % 4 + 1), LONG(i / 4 + 1)};
            hr = native<ColorFillFn>(ColorFill)(device_, source, &cell, pattern(i));
        }
        if (FAILED(hr)) break;
        if (FAILED(hr = native<ColorFillFn>(ColorFill)(device_, result, nullptr, 0))) break;
        stage = "scene";
        if (FAILED(scene = native<SceneFn>(BeginScene)(device_))) { hr = scene; break; }
        stage = "stretch";
        first = native<StretchFn>(StretchRect)(device_, source, nullptr, middle_surface, nullptr, D3DTEXF_POINT);
        if (SUCCEEDED(first)) second = native<StretchFn>(StretchRect)(device_, middle_surface, nullptr, result, nullptr, D3DTEXF_POINT);
        scene = native<SceneFn>(EndScene)(device_);
        if (FAILED(first)) { hr = first; break; }
        if (FAILED(second)) { hr = second; break; }
        if (FAILED(scene)) { hr = scene; break; }
        stage = "readback";
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, source, source_copy))) break;
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, result, result_copy))) break;
        if (FAILED(hr = native<GetRtDataFn>(GetRenderTargetData)(device_, middle_surface, middle_copy))) break;
        D3DLOCKED_RECT in{}, out{}, mid{};
        if (FAILED(hr = source_copy->LockRect(&in, nullptr, D3DLOCK_READONLY))) break;
        if (FAILED(hr = result_copy->LockRect(&out, nullptr, D3DLOCK_READONLY))) { source_copy->UnlockRect(); break; }
        if (FAILED(hr = middle_copy->LockRect(&mid, nullptr, D3DLOCK_READONLY))) { result_copy->UnlockRect(); source_copy->UnlockRect(); break; }
        stage = "compare";
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            DWORD a = 0, b = 0;
            std::memcpy(&a, static_cast<const char*>(in.pBits) + y * in.Pitch + x * 4, 4);
            std::memcpy(&b, static_cast<const char*>(out.pBits) + y * out.Pitch + x * 4, 4);
            if ((a & mask) != (b & mask)) ++byte_errors;
            unsigned short h[4]; std::memcpy(h, static_cast<const char*>(mid.pBits) + y * mid.Pitch + x * 8, 8);
            const unsigned channels = format == D3DFMT_X8R8G8B8 ? 3u : 4u;
            for (unsigned c = 0; c < channels; ++c) {
                // FP16 channel c holds R, G, B, A; the 8-bit word is A8 R8 G8 B8.
                const unsigned code = c == 3 ? (a >> 24) & 255u : (a >> (16 - 8 * c)) & 255u;
                const float expected = float(code) / 255.f;
                const unsigned exponent = (h[c] >> 10) & 31u, mantissa = h[c] & 1023u;
                const float value = (h[c] & 0x8000u ? -1.f : 1.f) *
                    (exponent == 0 ? std::ldexp(float(mantissa), -24) : exponent == 31 ? 65504.f * 2.f : std::ldexp(float(1024u + mantissa), int(exponent) - 25));
                if (!(std::fabs(value - expected) <= 1.f / 1024.f)) ++half_errors;
            }
        }
        middle_copy->UnlockRect(); result_copy->UnlockRect(); source_copy->UnlockRect();
        ok = !byte_errors && !half_errors;
    } while (false);
    release(middle_copy); release(result_copy); release(source_copy);
    release(middle_surface); release(middle); release(result); release(source);
    std::snprintf(detail, detail_size, "%s:format=%u:stage=%s:result=%08lx:to_fp16=%08lx:to_8bit=%08lx:byte_errors=%u:half_errors=%u",
                  ok ? "pass" : "fail", unsigned(format), stage, hr, first, second, byte_errors, half_errors);
    return ok;
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

// Binds the quad's vertex program and declaration (or the fixture twin's
// XYZRHW fixed-function path); saved and restored with the other bindings.
HRESULT MotionOutput::bind_quad_program() noexcept {
    if (quad_fvf_) {
        const HRESULT hr = native<SetVsFn>(SetVertexShader)(device_, nullptr);
        return FAILED(hr) ? hr : native<SetFvfFn>(SetFVF)(device_, renderer::quad_fvf);
    }
    const HRESULT hr = native<SetDeclarationFn>(SetVertexDeclaration)(device_, quad_declaration_);
    return FAILED(hr) ? hr : native<SetVsFn>(SetVertexShader)(device_, quad_vs_);
}
// Fullscreen strip through the quad's vs_3_0 pass-through with `shader`
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
    step(bind_quad_program());
    step(native<SetPsFn>(SetPixelShader)(device_, shader));
    for (unsigned i = 0; i < touched_count; ++i)
        step(native<SetRenderStateFn>(SetRenderState)(device_, touched_states[i], touched_values[i]));
    if (SUCCEEDED(op)) {
        // The -0.5 pixel shift of quad_vertices covers every texel centre.
        renderer::QuadVertex quad[4];
        if (quad_fvf_) renderer::quad_vertices_xyzrhw(width, height, quad); else renderer::quad_vertices(width, height, quad);
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
    // Reset discards the frame: no write-back, but the application's main
    // surface goes back to RT0 first so the FP16 texture is not kept alive by
    // the binding through the Reset (and is not RT0 after a failed one).
    if (hdr_state_ == HdrState::Active && hdr_ && hdr_main_) hdr_->bind(hdr_main_, nullptr);
    drop_redirect(); // The FP16 target goes with RT1/RT2.
    release_target();
    if (hdr_) hdr_->before_reset();
    hdr_target_failed_ = false; hdr_blocked_ = false; hdr_blocked_latches_ = 0; // a Reset clears the cause of an unwind
    if (taa_) taa_call([&] { taa_->before_reset(); });
    target_failed_ = false;
    history_.invalidate();
    selector_.invalidate();
    fill_pending_ = false; pending_valid_ = false;
    main_ = {}; main_depth_ = {}; main_msaa_ = false; main_msaa_samples_ = 0; msaa_logged_ = false;
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
void MotionOutput::end_stateblock() noexcept { if (enabled_) { shadow_.recording = false; ++counters_.sb_resyncs; resync_shadow(); } }
void MotionOutput::stateblock_applied() noexcept { if (enabled_ && !shadow_.recording) { ++counters_.sb_resyncs; resync_shadow(); } }

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
    if (SUCCEEDED(native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &surface))) {
        shadow_.rt0 = describe_surface(surface);
        // The device holds the FP16 target while redirected; the shadow keeps
        // the application's logical binding (the latched main surface).
        if (hdr_state_ == HdrState::Active && hdr_main_ && same(shadow_.rt0, hdr_target_)) shadow_.rt0 = describe_surface(hdr_main_);
    }
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
    resync_samplers();
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
    engine_memory::next_frame(); // the object observers' direct-read regions are re-validated once per frame
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
    // The latching Clear redirects RT0 to the FP16 target before it runs, so
    // the application's own Clear clears the target; a later Clear with the
    // target flag while redirected clears it too (content pending).
    if (hdr_enabled_) {
        if (hdr_state_ == HdrState::Off) begin_redirect();
        else if (hdr_state_ == HdrState::Active && (flags & D3DCLEAR_TARGET)) hdr_dirty_ = true;
    }
}
void MotionOutput::after_clear(HRESULT result) noexcept {
    if (!enabled_) return;
    if (!pending_valid_) { selector_.invalidate(); return; }
    pending_valid_ = false;
    const auto before = selector_.state();
    observe(pending_, result);
    if (hdr_latch_pending_) {
        hdr_latch_pending_ = false;
        // The redirect was bound for this Clear; a failed Clear (the selector
        // rejects) leaves nothing to keep: the binding goes back at once and
        // nothing is written back (the target holds no content of this frame;
        // the main target keeps what the application's failed Clear left).
        bool failed = FAILED(result) || selector_.state() != renderer::BoundaryState::Background;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (hdr_ && hdr_->take_fault(renderer::HdrFault::Clear)) failed = true; // seam: the Clear "failed"
#endif
        if (failed) { hdr_dirty_ = false; end_redirect(HdrEnd::ClearFailed); }
    }
    // The scene view's camera is final at the depth-only Clear that starts the
    // scene phase (the view activation issues that Clear right after building
    // the matrices); the background view's at the latching Clear (diagnostics).
    if (before == renderer::BoundaryState::Background && selector_.state() == renderer::BoundaryState::Scene) read_camera(true);
    // D3: a multisampled RT0 never latches (the selector's main-target rule
    // requires a single-sampled A8R8G8B8 surface), so a frame whose initial
    // Clear lands on one is refused here by name: RT1/RT2 textures cannot
    // share its sample count and D3D9 requires every simultaneous target to
    // match RT0. Nothing routes or jitters (gate 1), the resolve skips
    // (TaaSkip::Msaa), the history drops, the frame line carries msaa=; the
    // HDR redirect refuses on its own (refused_msaa). Logged once until a
    // single-sampled frame latches or Reset changes the sample count.
    if (before == renderer::BoundaryState::AwaitInitialClear && selector_.state() != renderer::BoundaryState::Background &&
        SUCCEEDED(result) && pending_.rt.known && pending_.rt.identity && pending_.rt.msaa && pending_.rt.format == 21 && pending_.rt.width && pending_.rt.height) {
        main_msaa_ = true; main_msaa_samples_ = pending_.rt.msaa;
        jitter_active_ = false; history_.invalidate();
        if (!msaa_logged_) {
            msaa_logged_ = true;
            log("motion_output_msaa_refused device=%llu frame=%llu msaa=%lu width=%lu height=%lu", id_, frame_,
                static_cast<unsigned long>(pending_.rt.msaa), static_cast<unsigned long>(pending_.rt.width), static_cast<unsigned long>(pending_.rt.height));
        }
    }
    if (before == renderer::BoundaryState::AwaitInitialClear && selector_.state() == renderer::BoundaryState::Background) {
        read_camera(false);
        // The frame's main color/depth pair is latched: own a matching motion
        // target and schedule the sentinel fill for the next draw.
        main_ = pending_.rt; main_depth_ = pending_.depth;
        counters_.latched = true;
        main_msaa_ = false; main_msaa_samples_ = 0; msaa_logged_ = false; // a single-sampled frame latched (the selector admits no other)
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
    if (index == 0 && hdr_pending_state_) {
        // The substitution decided in before_set_render_target took effect
        // only if the native call succeeded (else the binding is unchanged).
        const auto next = static_cast<HdrState>(hdr_pending_state_);
        hdr_pending_state_ = 0;
        if (SUCCEEDED(result) && next != hdr_state_) {
            if (next == HdrState::Suspended) ++counters_.hdr.suspended; else ++counters_.hdr.resumed;
            hdr_state_ = next;
        }
    }
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
    if (!object_trace::current(&scope, capture_)) return false; // matrices are capture-frame diagnostics
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
    const std::uint64_t begin = draw_stamp();
    const HRESULT hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, row.matrix_register, rows, 4);
    const std::uint64_t ticks = draw_stamp() - begin;
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
    const std::uint64_t begin = draw_stamp();
    const HRESULT hr = window < motion_matrix_windows_max
        ? native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, route.jitter_register, shadow_.rows[window], 4) : E_FAIL;
    const std::uint64_t ticks = draw_stamp() - begin;
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
    if (hdr_state_ == HdrState::Active) hdr_dirty_ = true; // every application draw lands in the FP16 target
    if (mip_bias_total_game_writes_ != mip_bias_logged_game_writes_) log_mip_bias_game_write();
    const std::uint64_t begin = draw_stamp(), fill_before = counters_.fill_ticks, flush_before = counters_.lazy_flush_ticks;
    evaluate_draw(call, route);
    if (!route.routed) restore_bindings();
    if (telemetry::draw_enabled()) {
        const std::uint64_t total = draw_stamp() - begin,
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
    if (!target_surface_ || shadow_.recording || main_msaa_) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return; }
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
    const std::uint64_t apply_begin = draw_stamp();
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
    route.ticks = draw_stamp() - apply_begin;
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
    // The mip LOD bias of the routed material stages, while the jitter is on
    // (a failed sampler call is counted and logged; the draw still routes).
    if (mip_bias_bits_ && jitter_active_) apply_mip_bias();
}

void MotionOutput::after_draw(MotionRoute& route, HRESULT result) noexcept {
    if (!enabled_) return;
    const bool jittered = route.jittered;
    if (route.routed) {
        // route_draw: the apply (before_draw) plus this undo, without the
        // native draw between them and without the jitter writes.
        const std::uint64_t begin = draw_stamp();
        undo(route);
        route.ticks += draw_stamp() - begin;
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

// ---- FP16 HDR redirect (stage 1) ------------------------------------------
//
// State machine (docs/architecture/hdr-scene-path.md, "Stage 1
// implementation"): Off -> Active at the latching colour+depth Clear (the
// FP16 target replaces the application's main surface as the device's RT0;
// the application's own Clear then clears the target); Active -> Suspended
// when the application binds another surface at RT0 (forwarded verbatim after
// the pending content was written back, so the main target always holds a
// valid image); Suspended -> Active when it binds the main surface again (the
// target is substituted; its content is intact); -> Off at the scene end
// (engine hook, bloom copy), before an application write into the main
// target's contents, or at Present, with the write-back of any pending
// content and the main surface rebound. EndScene flushes without ending, so
// an environment-map excursion after a glow-off scene keeps the redirect and
// the terminal Present end normally finds nothing pending.

// True when `surface` is the application's latched main surface (pointer or
// resource identity: through the ownership wrapper the application's pointer
// is the canonical wrapper, natively the same object).
bool MotionOutput::hdr_is_main(IDirect3DSurface9* surface) noexcept {
    if (!surface || !hdr_main_) return false;
    return surface == hdr_main_ || same(describe_surface(surface), main_);
}

IDirect3DSurface9* MotionOutput::before_set_render_target(DWORD index, IDirect3DSurface9* surface) noexcept {
    hdr_pending_state_ = 0;
    if (index != 0 || hdr_state_ == HdrState::Off || !hdr_ || !hdr_->target()) return surface;
    if (hdr_is_main(surface)) { hdr_pending_state_ = std::uint32_t(HdrState::Active); return hdr_->target(); }
    // Another surface (an environment-map face): forwarded verbatim; the scene
    // so far is written back first so the main target is valid whether or not
    // the application ever rebinds it.
    if (hdr_state_ == HdrState::Active) flush_redirect();
    hdr_pending_state_ = std::uint32_t(HdrState::Suspended);
    return surface;
}
bool MotionOutput::hdr_logical_render_target(DWORD index, IDirect3DSurface9** out) noexcept {
    if (index != 0 || hdr_state_ != HdrState::Active || !hdr_main_ || !out) return false;
    hdr_main_->AddRef();
    *out = hdr_main_;
    return true;
}
void MotionOutput::before_render_target_read(IDirect3DSurface9* surface) noexcept {
    if (hdr_state_ == HdrState::Active && hdr_dirty_ && hdr_is_main(surface)) flush_redirect();
}
void MotionOutput::before_render_target_write(IDirect3DSurface9* surface) noexcept {
    if (hdr_state_ != HdrState::Off && hdr_is_main(surface)) end_redirect(HdrEnd::ContentWrite);
}
void MotionOutput::before_end_scene() noexcept { if (hdr_state_ == HdrState::Active) flush_redirect(); }

// At the latching Clear, before it is forwarded: only the Clear the selector
// will latch (probed on a copy), never a multisampled target, never while
// blocked after an unwind unless the recovery self test passes now.
void MotionOutput::begin_redirect() noexcept {
    auto& h = counters_.hdr;
    if (selector_.state() != renderer::BoundaryState::AwaitInitialClear || !hdr_) return;
    renderer::SceneBoundarySelector probe = selector_;
    renderer::Event e = pending_;
    e.result_known = true; e.result = 0;
    probe.observe(e);
    if (probe.state() != renderer::BoundaryState::Background) return;
    if (pending_.rt.msaa || pending_.depth.msaa) { h.refused_msaa = true; return; }
    if (hdr_blocked_) {
        h.blocked = true;
        if (hdr_blocked_latches_++ % hdr_recheck_interval != 0) return;
        char detail[320] = "";
        const std::uint64_t begin = stamp();
        const bool passed = hdr_->recheck(scene_open_, detail, sizeof detail);
        h.recheck_ticks = stamp() - begin;
        record(unsigned(telemetry::Metric::HdrRecheck), h.recheck_ticks, !passed);
        h.recheck_ran = true; h.recheck_passed = passed;
        log("hdr_recheck device=%llu frame=%llu passed=%u %s", id_, frame_, passed, detail);
        if (!passed) return;
        hdr_blocked_ = false; hdr_blocked_latches_ = 0; h.blocked = false;
    }
    if (hdr_target_failed_) return;
    const bool had_target = hdr_->target() && hdr_->width() == pending_.rt.width && hdr_->height() == pending_.rt.height;
    const HRESULT create = hdr_->ensure_target(pending_.rt.width, pending_.rt.height);
    h.target_create = create;
    if (!had_target || FAILED(create))
        log("hdr_target device=%llu frame=%llu width=%u height=%u format=A16B16G16R16F bytes=%llu create=%08lx chain_levels=%u chain_bytes=%llu meter=%u meter_reason=%s",
            id_, frame_, pending_.rt.width, pending_.rt.height, FAILED(create) ? 0ull : hdr_->target_bytes(), create,
            hdr_->chain_levels(), hdr_->chain_bytes(), hdr_->caps().meter, hdr_->caps().meter_reason);
    if (FAILED(create)) { hdr_target_failed_ = true; return; } // Retry only after Reset, like the motion target.
    // The application's RT0 (one reference, held until the redirect ends) must
    // be the logical binding the shadow describes; else nothing is redirected.
    IDirect3DSurface9* main = nullptr;
    HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &main);
    if (FAILED(hr) || !main || !same(describe_surface(main), pending_.rt)) {
        if (hdr_logged_ < failure_log_limit) { ++hdr_logged_; log("hdr_redirect_refused device=%llu frame=%llu reason=binding result=%08lx", id_, frame_, hr); }
        release(main); return;
    }
    std::uint64_t ticks = 0;
    hr = hdr_->bind(hdr_->target(), telemetry_ ? &ticks : nullptr);
    h.latch_bind = hr; h.redirect_ticks = ticks;
    record(unsigned(telemetry::Metric::HdrRedirect), ticks, FAILED(hr));
    if (FAILED(hr)) {
        // A failed bind changes nothing (D3D9 keeps the previous target); the
        // frame runs without the redirect.
        if (hdr_logged_ < failure_log_limit) { ++hdr_logged_; log("hdr_redirect_refused device=%llu frame=%llu reason=bind result=%08lx", id_, frame_, hr); }
        release(main); return;
    }
    hdr_main_ = main;
    hdr_target_ = describe_surface(hdr_->target());
    hdr_state_ = HdrState::Active; hdr_dirty_ = true; hdr_latch_pending_ = true;
    h.redirected = true;
    // Stage 2: consume the previous frame's meter and adapt the EV this
    // frame's tonemap consumes (a no-op with the identity write-back).
    if (hdr_->tonemap_active()) {
        static std::uint64_t frequency = 0;
        if (!frequency) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); frequency = std::uint64_t(f.QuadPart); }
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        const renderer::HdrFrameBegin b = hdr_->begin_frame(std::uint64_t(now.QuadPart), frequency, telemetry_);
        h.stepped = b.stepped; h.readback = b.readback; h.readback_ticks = b.ticks_readback;
        if (b.readback != S_FALSE) record(unsigned(telemetry::Metric::HdrMeterReadback), b.ticks_readback, FAILED(b.readback));
    }
    // Stage 3: k of the resolve's luminance weighting for this frame = the
    // exposure multiplier exp2(EV) the AgX write-back applies to the resolved
    // image (EV manual or adapted at this latch), so the weighted domain is
    // the display-relative luminance the tonemap sees; 0 (unweighted) with the
    // identity write-back, which applies no exposure; X3M_TAA_K overrides.
    hdr_taa_k_ = taa_k_override_ >= 0.f ? taa_k_override_ : hdr_->tonemap_active() ? hdr_->exposure().k() : 0.f;
}
// One write-back through the pass (the ladder), with the capture-frame
// readback of the FP16 image before the first one of the frame, the
// telemetry and the block after an unwind.
renderer::HdrWriteback MotionOutput::hdr_writeback(IDirect3DSurface9* final_rt0, bool write,
                                                renderer::HdrDisplaySnapshot* display) noexcept {
    auto& h = counters_.hdr;
    if (write && capture_ && !h.writebacks && hdr_->target())
        readback_surface(hdr_->target(), D3DFMT_A16B16G16R16F, 8, L"hdr", L"rgba16f", "hdr_readback", "rgba16f_row_major", hdr_->width(), hdr_->height());
    const std::uint64_t begin = stamp();
    const renderer::HdrWriteback r = hdr_->write_back(hdr_main_, final_rt0, scene_open_, write, telemetry_, hdr_resolved_, display);
    const std::uint64_t ticks = stamp() - begin;
    if (write) {
        ++h.writebacks; h.source = unsigned(r.source);
        h.writeback_ticks += ticks; h.writeback_draw_ticks += r.ticks_draw; h.writeback_stretch_ticks += r.ticks_stretch;
        h.tonemap = r.tonemap; h.fallback = h.fallback || r.fallback; h.tonemap_draw = r.tonemap_draw;
        h.sharpened = r.sharpened; h.sharpen_fallback = h.sharpen_fallback || r.sharpen_fallback;
        if (r.sharpened) counters_.taa.sharpened = true;
        if (r.meter != S_FALSE) { h.meter = r.meter; h.meter_ticks += r.ticks_meter; record(unsigned(telemetry::Metric::HdrMeter), r.ticks_meter, FAILED(r.meter)); }
        record(unsigned(telemetry::Metric::HdrWriteback), ticks, r.unwind);
        record(unsigned(telemetry::Metric::HdrWritebackDraw), r.ticks_draw, FAILED(r.draw) || FAILED(r.restore));
        if (r.ticks_stretch) record(unsigned(telemetry::Metric::HdrWritebackStretch), r.ticks_stretch, FAILED(r.stretch));
        if (r.fallback && !hdr_->tonemap_active() && !hdr_tonemap_disabled_logged_) {
            hdr_tonemap_disabled_logged_ = true;
            log("hdr_tonemap_disabled device=%llu frame=%llu reason=draw_failures draw=%08lx", id_, frame_, r.tonemap_draw);
        }
        // --taa-debug: the 8-bit main target after the write-back of a resolved
        // frame (tonemapped and/or sharpened as configured): the presented image
        // of this resolve, the counterpart of the 8-bit route's readback in resolve().
        if (capture_ && taa_debug_ && hdr_resolved_ && !r.unwind)
            readback_surface(hdr_main_, static_cast<D3DFORMAT>(main_.format), 4, L"present", L"bgra8", "motion_output_present_readback", "bgra8_row_major", target_width_, target_height_);
    }
    if (r.ticks_bind) { h.bind_ticks += r.ticks_bind; record(unsigned(telemetry::Metric::HdrBind), r.ticks_bind, FAILED(r.bind)); }
    hdr_dirty_ = false;
    if (r.unwind) {
        // The must-unwind ladder was taken: the main target holds the shader
        // copy, the StretchRect copy or its previous content, and RT0 is the
        // main target again (or the target, for a flush). The device state may
        // be unknown after a failed restoration; the next latch redirects only
        // after the recovery self test passes.
        h.unwind = true; h.unwind_reason = r.unwind_reason;
        h.unwind_draw = r.draw; h.unwind_restore = r.restore; h.unwind_stretch = r.stretch; h.unwind_bind = r.bind;
        hdr_blocked_ = true; hdr_blocked_latches_ = 0;
        if (FAILED(r.restore)) { ++counters_.restore_failures; invalidate_render_states(); }
        if (hdr_logged_ < failure_log_limit) {
            ++hdr_logged_;
            log("hdr_unwind=%s device=%llu frame=%llu source=%s draw=%08lx restore=%08lx stretch=%08lx bind=%08lx write=%u final=%s",
                r.unwind_reason, id_, frame_, hdr_source_name(unsigned(r.source)), r.draw, r.restore, r.stretch, r.bind, write,
                final_rt0 == hdr_main_ ? "main" : "target");
        }
    }
    return r;
}
void MotionOutput::flush_redirect() noexcept {
    if (hdr_state_ != HdrState::Active || !hdr_dirty_ || !hdr_ || !hdr_main_) return;
    ++counters_.hdr.flushes;
    hdr_writeback(hdr_->target(), true);
}
void MotionOutput::end_redirect(HdrEnd reason, MotionHdrSceneCallback callback, void* context) noexcept {
    if (hdr_state_ == HdrState::Off) return;
    if (hdr_state_ == HdrState::Active && hdr_ && hdr_main_) {
        const bool write = hdr_dirty_ || hdr_resolved_ != nullptr;
        const auto& t = counters_.taa;
        // This optional handoff is stricter than the display fallback: failed
        // or skipped TAA still presents the unresolved image as before, but
        // cannot arm bloom or trigger another resolve through this callback.
        const bool handoff = callback && reason == HdrEnd::Hook && bloom_boundary_available()
            && selector_.state() == renderer::BoundaryState::Scene && counters_.hook_scene_end
            && !main_msaa_
            && (!taa_enabled_ || (hdr_resolved_ && t.attempted && t.resolved && t.hdr
                && t.source == unsigned(SceneEndSource::Hook) && SUCCEEDED(t.result) && SUCCEEDED(t.restore)));
        if (handoff) {
            renderer::HdrDisplaySnapshot display;
            const auto r = hdr_writeback(hdr_main_, write, &display);
            if (display.valid && !r.unwind && r.tonemap && r.source == renderer::HdrWritebackSource::Shader
                && SUCCEEDED(r.draw) && SUCCEEDED(r.restore)
                && display.resolved == (hdr_resolved_ != nullptr)
                && display.width && display.height && display.width == hdr_->width() && display.height == hdr_->height()
                && display.width == main_.width && display.height == main_.height
                && same(describe_surface(hdr_main_), main_)) {
                IDirect3DTexture9* scene = hdr_resolved_;
                const bool temporary = scene == nullptr;
                HRESULT container = S_OK;
                // TAA owns a resolved source. Without TAA, GetContainer takes
                // a temporary public COM reference (canonical wrapper under
                // ownership), released here after the caller pins its own.
                if (temporary && hdr_->target())
                    container = hdr_->target()->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&scene));
                if (SUCCEEDED(container) && scene) {
                    const MotionHdrScene ready{device_, scene, hdr_main_, id_, frame_, generation_, display};
                    callback(context, ready);
                }
                if (temporary) release(scene); // includes a non-null output accompanying a failed HRESULT
            }
        } else hdr_writeback(hdr_main_, write);
    }
    // Suspended: the application bound another surface itself and the main
    // target already holds the write-back of the switch; nothing to rebind.
    release(hdr_main_);
    hdr_state_ = HdrState::Off; hdr_dirty_ = false; hdr_latch_pending_ = false; hdr_pending_state_ = 0; hdr_resolved_ = nullptr;
    counters_.hdr.end = std::uint32_t(reason);
}
void MotionOutput::drop_redirect() noexcept {
    hdr_resolved_ = nullptr;
    if (hdr_state_ == HdrState::Off) { release(hdr_main_); return; }
    release(hdr_main_);
    hdr_state_ = HdrState::Off; hdr_dirty_ = false; hdr_latch_pending_ = false; hdr_pending_state_ = 0;
    counters_.hdr.end = std::uint32_t(HdrEnd::Dropped);
}
void MotionOutput::log_hdr_frame() noexcept {
    const auto& h = counters_.hdr;
    const auto& caps = hdr_ ? hdr_->caps() : renderer::HdrCaps{};
    const auto us = [](std::uint64_t ticks) { return telemetry::microseconds(ticks); };
    // Stage 2 fields: the tonemap in force (identity | agx), its look and
    // decode, the exposure mode, the EV the frame's tonemap consumed, the
    // adapted/target EV, the metered log2 mean (avg_log_l) and its linear
    // value (luma_mean), the space-aware statistic (lit_fraction, luma_lit =
    // the lit tiles' median, luma_p99 = the tile maxima's 99th percentile,
    // ev_key, ev_limit, ev_fresh = this step's target before the dead band
    // (ev_target is the held one), tiles, lit), the dt of the step, the
    // meter/readback HRESULTs and their CPU phases, the TAA weighting k
    // exported for stage 3.
    const bool tonemap = hdr_ && hdr_->tonemap_active();
    const auto& e = hdr_ ? hdr_->exposure() : renderer::ExposureState{};
    const auto& c = hdr_ ? hdr_->config() : renderer::HdrConfig{};
    log("hdr_frame device=%llu frame=%llu hdr=%u redirected=%u end=%s writebacks=%lu flushes=%lu writeback_source=%s unwind=%u unwind_reason=%s unwind_draw=%08lx unwind_restore=%08lx unwind_stretch=%08lx unwind_bind=%08lx blocked=%u recheck=%s suspended=%lu resumed=%lu dirty_at_present=%u refused_msaa=%u target_create=%08lx latch_bind=%08lx target=%ux%u target_bytes=%llu caps=%s stretch_conversion=%08lx timing=%s redirect_us=%.1f writeback_us=%.1f writeback_draw_us=%.1f writeback_stretch_us=%.1f bind_us=%.1f recheck_us=%.1f tonemap=%s tonemapped=%u look=%s decode=%s clamp=%g exposure=%s ev=%.5f ev_adapted=%.5f ev_target=%.5f avg_log_l=%.5f luma_mean=%.6g lit_fraction=%.4f luma_lit=%.6g luma_p99=%.6g ev_key=%.5f ev_limit=%.5f ev_fresh=%.5f tiles=%u lit=%u dt_ms=%.3f stepped=%u steps=%u meter=%08lx readback=%08lx tonemap_draw=%08lx fallback=%u meter_us=%.1f readback_us=%.1f k=%.5f chain_bytes=%llu sharpen=%s sharpened=%u sharpen_fallback=%u",
        id_, frame_, hdr_enabled_, h.redirected, hdr_end_name(h.end), static_cast<unsigned long>(h.writebacks), static_cast<unsigned long>(h.flushes),
        hdr_source_name(h.source), h.unwind, h.unwind_reason, h.unwind_draw, h.unwind_restore, h.unwind_stretch, h.unwind_bind,
        h.blocked, h.recheck_ran ? (h.recheck_passed ? "pass" : "fail") : "none", static_cast<unsigned long>(h.suspended),
        static_cast<unsigned long>(h.resumed), h.dirty_at_present, h.refused_msaa, h.target_create, h.latch_bind,
        hdr_ ? hdr_->width() : 0u, hdr_ ? hdr_->height() : 0u, hdr_ ? hdr_->target_bytes() : 0ull, caps.reason, caps.stretch_conversion,
        telemetry_ ? "cpu_qpc" : "off", us(h.redirect_ticks), us(h.writeback_ticks), us(h.writeback_draw_ticks), us(h.writeback_stretch_ticks),
        us(h.bind_ticks), us(h.recheck_ticks),
        tonemap ? "agx" : "identity", h.tonemap, renderer::hdr_look_name(c.look), renderer::hdr_decode_name(c.decode), double(c.clamp_max),
        renderer::hdr_exposure_name(c.exposure), double(e.ev()), double(e.ev_adapted()), double(e.ev_target()), double(e.avg_log_l()),
        double(std::exp2(e.avg_log_l())), double(e.meter().lit_fraction), double(std::exp2(e.meter().lit_median_log)), double(std::exp2(e.meter().p99_max_log)),
        double(e.ev_key()), double(e.ev_limit()), double(e.ev_fresh()), e.meter().tiles, e.meter().lit,
        double(e.dt()) * 1000., h.stepped, e.steps(), h.meter, h.readback, h.tonemap_draw, h.fallback,
        us(h.meter_ticks), us(h.readback_ticks), double(hdr_taa_k_), hdr_ ? hdr_->chain_bytes() : 0ull,
        caps.sharpen_reason, h.sharpened, h.sharpen_fallback);
}

// ---- frame end -------------------------------------------------------------

// Copies one owned target through system memory to <prefix>_<device>_<frame>.<extension>
// beside the capture log (row-major, bytes_per_pixel per pixel, no header).
HRESULT MotionOutput::readback_surface(IDirect3DSurface9* surface, D3DFORMAT format, unsigned bytes_per_pixel,
                                       const wchar_t* prefix, const wchar_t* extension, const char* tag, const char* format_name,
                                       UINT width, UINT height) noexcept {
    const std::uint64_t begin = stamp(); // route_readback: allocation, GetRenderTargetData, lock, file write.
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = !width || !height ? E_INVALIDARG : native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, width, height,
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
                for (UINT y = 0; y < height; ++y)
                    written += std::fwrite(static_cast<const char*>(lock.pBits) + y * lock.Pitch, 1, std::size_t(width) * bytes_per_pixel, file);
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
        tag, id_, frame_, prefix, id_, frame_, extension, width, height, format_name, hr, unsigned(written));
    return hr;
}
// Capture frames: RT1 as motion_<device>_<frame>.rgba32f and, when produced,
// RT2 as depth_<device>_<frame>.r32f (R32F device depth, -1 where no routed
// depth row covered the pixel).
void MotionOutput::readback() noexcept {
    if (!target_surface_ || !counters_.filled) return;
    readback_surface(target_surface_, D3DFMT_A32B32G32R32F, 16, L"motion", L"rgba32f", "motion_output_readback", "rgba32f_row_major", target_width_, target_height_);
    if (depth_enabled_ && depth_surface_)
        readback_surface(depth_surface_, D3DFMT_R32F, 4, L"depth", L"r32f", "motion_output_depth_readback", "r32f_row_major", target_width_, target_height_);
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
    // Terminal end of the redirect: a frame without a recognized scene end
    // (glow off without the engine hook, the synthetic scripts) is written
    // back here (normally flushed at EndScene already: nothing pending).
    if (hdr_state_ != HdrState::Off) { counters_.hdr.dirty_at_present = hdr_state_ == HdrState::Active && hdr_dirty_; end_redirect(HdrEnd::Present); }
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
        // Own log budget (iteration 10: a latch-only transition screen, nothing
        // routed and nothing resolved by either boundary, produced 16 consecutive
        // disagreements that would otherwise consume the failure log's limit).
        if (check == unsigned(SceneEndCheck::Disagree) && hook_disagreements_logged_ < failure_log_limit) {
            ++hook_disagreements_logged_;
            log("motion_output_scene_hook_disagreement device=%llu frame=%llu installed=%u signals=%lu outside_scene=%lu selector_state=%lu draws_after_hook=%lu bloom_copy_seen=%u hook_scene_end=%u routed=%lu",
                id_, frame_, scene_hook_installed_, static_cast<unsigned long>(c.hook_signals), static_cast<unsigned long>(c.hook_outside_scene),
                static_cast<unsigned long>(c.hook_state), static_cast<unsigned long>(c.draws_after_hook), copy, hook, static_cast<unsigned long>(c.routed));
        }
    }
    // A frame that did not resolve (menu, rejected before the copy,
    // unrecognized, skipped) leaves no usable history. A rejection after the
    // copy (a different bloom or overlay sequence) does not: the resolved
    // scene is complete and the next frame's correspondence is scene to scene.
    auto& t = counters_.taa;
    if (taa_enabled_) {
        if (!t.attempted) t.skip = unsigned(main_msaa_ ? TaaSkip::Msaa : TaaSkip::NotReached);
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
        log("motion_output_frame device=%llu frame=%llu latched=%u msaa=%lu filled=%u fill_result=%08lx fill_restore=%08lx draws=%lu routed=%lu matched=%lu gate1=%lu gate2=%lu gate3=%lu gate4=%lu gate5=%lu gate6=%lu apply_failures=%lu restore_failures=%lu history_previous=%u history_current=%u committed=%u selector_state=%u present=%08lx depth=%u depth_routed=%lu jitter=%u jitter_index=%u jitter_x=%.6f jitter_y=%.6f jitter_previous_x=%.6f jitter_previous_y=%.6f jittered=%lu cut=%u cut_median_px=%.4f cut_missing=%.4f cut_samples=%lu taa=%u taa_attempted=%u taa_resolved=%u taa_history=%u taa_skip=%lu taa_result=%08lx taa_restore=%08lx taa_copy=%08lx taa_hdr=%u taa_k=%.5f taa_sharpen=%u scene_open=%u active_queries=%lu taa_references=%u"
            " camera_valid=%u camera_background_valid=%u camera_reads=%lu camera_policy=%lu camera_reason=%lu camera_cut=%u camera_rotation_deg=%.4f"
            " rt_mode=%s timing=%s set_rt=%lu lazy_flushes=%lu jitter_writes=%lu readbacks=%lu gate_us=%.1f route_draw_us=%.1f set_rt_us=%.1f lazy_flush_us=%.1f jitter_us=%.1f fill_us=%.1f taa_run_us=%.1f taa_capture_us=%.1f taa_copy_color_us=%.1f taa_copy_depth_us=%.1f taa_draw_us=%.1f taa_apply_us=%.1f taa_copy_back_us=%.1f readback_us=%.1f"
            " state_shadow=%u rs_queries=%lu rs_hits=%lu rs_gets=%lu rs_resyncs=%lu sb_resyncs=%lu scene_hook=%u scene_end_source=%s scene_end_check=%lu hook_signals=%lu hook_outside_scene=%lu hook_state=%lu draws_after_hook=%lu bloom_copy_seen=%u"
            " mip_bias=%g mip_bias_sets=%lu mip_bias_restores=%lu mip_bias_draws=%lu mip_bias_stages=%04lx mip_bias_reads=%lu mip_bias_game_writes=%lu mip_bias_game_writes_total=%lu mip_bias_failures=%lu mip_bias_biased_now=%04lx",
            id_, frame_, counters_.latched, static_cast<unsigned long>(main_msaa_ ? main_msaa_samples_ : 0u), counters_.filled, counters_.fill_result, counters_.fill_restore,
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
            counters_.taa.copy, counters_.taa.hdr, counters_.taa.k, counters_.taa.sharpened, scene_open_, static_cast<unsigned long>(active_queries_), taa_references_,
            counters_.camera_scene_valid, counters_.camera_background_valid, static_cast<unsigned long>(counters_.camera_reads),
            static_cast<unsigned long>(counters_.taa.camera_policy), static_cast<unsigned long>(counters_.taa.camera_reason),
            counters_.taa.camera_cut, counters_.taa.camera_rotation_deg,
            lazy_mode_ ? "lazy" : "perdraw", telemetry_ ? "cpu_qpc" : "off",
            static_cast<unsigned long>(c.set_rt), static_cast<unsigned long>(c.lazy_flushes), static_cast<unsigned long>(c.jitter_writes),
            static_cast<unsigned long>(c.readbacks), us(c.gate_ticks), us(c.route_draw_ticks), us(c.set_rt_ticks), us(c.lazy_flush_ticks),
            us(c.jitter_ticks), us(c.fill_ticks), us(c.taa_run_ticks), us(c.taa_capture_ticks), us(c.taa_copy_color_ticks),
            us(c.taa_copy_depth_ticks), us(c.taa_draw_ticks), us(c.taa_apply_ticks), us(c.taa_copy_back_ticks), us(c.readback_ticks),
            state_shadow_, static_cast<unsigned long>(c.rs_queries), static_cast<unsigned long>(c.rs_hits), static_cast<unsigned long>(c.rs_gets),
            static_cast<unsigned long>(c.rs_resyncs), static_cast<unsigned long>(c.sb_resyncs), scene_hook_installed_, scene_end_source_name(c.taa.source), static_cast<unsigned long>(c.scene_end_check),
            static_cast<unsigned long>(c.hook_signals), static_cast<unsigned long>(c.hook_outside_scene), static_cast<unsigned long>(c.hook_state),
            static_cast<unsigned long>(c.draws_after_hook), c.bloom_copy_seen,
            double(mip_bias_), static_cast<unsigned long>(c.mip_bias_sets), static_cast<unsigned long>(c.mip_bias_restores),
            static_cast<unsigned long>(c.mip_bias_draws), static_cast<unsigned long>(c.mip_bias_stages), static_cast<unsigned long>(c.mip_bias_reads),
            static_cast<unsigned long>(c.mip_bias_game_writes), static_cast<unsigned long>(mip_bias_total_game_writes_),
            static_cast<unsigned long>(c.mip_bias_failures), static_cast<unsigned long>(sampler_biased_mask_));
    }
    if (taa_enabled_ && (capture_ || frame_ % camera_log_interval_ == 0)) log_camera_state();
    if (hdr_enabled_ && (capture_ || (telemetry_ && frame_ % frame_log_interval_ == 0))) log_hdr_frame();
}

#ifdef X3M_MOTION_OUTPUT_FIXTURE
void MotionOutput::fixture_configure(const MotionOutputFixtureConfig& config) noexcept {
    fixture_ = config; fixture_configured_ = true;
}
void MotionOutput::fixture_hdr_fault(unsigned kind, unsigned count) noexcept {
    if (hdr_) hdr_->set_fault(static_cast<renderer::HdrFault>(kind), count);
    else { fixture_hdr_fault_kind_ = kind; fixture_hdr_fault_count_ = count; }
}
HRESULT MotionOutput::fixture_hdr_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept {
    if (!hdr_) return D3DERR_NOTAVAILABLE;
    return hdr_->fixture_readback(out, floats, width, height);
}
HRESULT MotionOutput::fixture_hdr_exposure(float* out, std::size_t floats) const noexcept {
    if (!hdr_) return D3DERR_NOTAVAILABLE;
    if (!out || floats < 8) return D3DERR_MOREDATA;
    const auto& e = hdr_->exposure();
    out[0] = e.ev(); out[1] = e.ev_adapted(); out[2] = e.ev_target(); out[3] = e.avg_log_l();
    out[4] = e.dt(); out[5] = e.exposure(); out[6] = float(e.steps()); out[7] = hdr_taa_k_;
    if (floats >= 16) {
        const auto& m = e.meter();
        out[8] = m.lit_fraction; out[9] = m.lit_median_log; out[10] = m.p99_max_log; out[11] = e.ev_key();
        out[12] = e.ev_limit(); out[13] = float(m.tiles); out[14] = float(m.lit); out[15] = m.lit_mean_log;
    }
    if (floats >= 18) { out[16] = e.ev_fresh(); out[17] = e.meter().lit_weight; }
    return S_OK;
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
