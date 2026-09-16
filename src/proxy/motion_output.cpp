#include "motion_output.h"
#include "sse_scalar.h"
#include "capture.h"
#include "cpu_state.h"
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
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
#include "screen_emission_admission.h"
#include "frame_timing.h" // X3M_FRAME_TIMING only: the redundant-state counters
#include "../renderer/linear_emission_sm1.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

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
constexpr void add_matrix_window(MatrixWindows& windows, UINT matrix_register) noexcept {
    if (windows.count > motion_matrix_windows_max) return; // the overflow sentinel stands; base[] holds `max` entries
    bool seen = false;
    for (std::size_t i = 0; i < windows.count; ++i) seen = seen || windows.base[i] == matrix_register;
    if (seen) return;
    if (windows.count == motion_matrix_windows_max) { windows.count = motion_matrix_windows_max + 1; return; }
    windows.base[windows.count++] = matrix_register;
}
// Clip-row windows of every jittered program: the pair table's VS rows and
// the depth-only prepass programs (jitter only, depth_prepass_profiles.h).
constexpr MatrixWindows derive_matrix_windows() noexcept {
    MatrixWindows windows{};
    for (const auto& row : renderer::motion_output_profiles) add_matrix_window(windows, row.matrix_register);
    for (const auto& row : renderer::depth_prepass_profiles) add_matrix_window(windows, row.matrix_register);
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
constexpr bool prepass_rows_match_shadow() noexcept {
    for (const auto& row : renderer::depth_prepass_profiles)
        if (window_of(row.matrix_register) >= motion_matrix_windows_max) return false;
    return true;
}
static_assert(prepass_rows_match_shadow(), "every depth-prepass row must name a shadowed clip-row window");
// IDirect3DDevice9 vtable slots, verified against the MinGW d3d9.h method order
// by verification/probe/abi_check.cpp (compile-time offsetof assertions).
enum Slot : unsigned {
    AddRef = 1, Release = 2, GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9,
    CreateTexture = 23, CreateRenderTarget = 28, GetRenderTargetData = 32, StretchRect = 34, ColorFill = 35,
    CreateOffscreenPlainSurface = 36, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    GetTexture = 64, SetTexture = 65, GetTextureStageState = 66, GetSamplerState = 68, SetSamplerState = 69,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93,
    SetVertexShaderConstantF = 94, GetVertexShaderConstantF = 95, GetVertexShaderConstantI = 97, GetVertexShaderConstantB = 99,
    SetStreamSource = 100, GetStreamSource = 101, GetStreamSourceFreq = 103, GetIndices = 105,
    CreatePixelShader = 106, SetPixelShader = 107, GetPixelShader = 108,
    SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110, CreateQuery = 118
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
using GetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD*);
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
using GetStreamFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using SetConstantsFFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstantsFFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
using GetConstantsIFn = HRESULT(WINAPI*)(D, UINT, int*, UINT);
using GetConstantsBFn = HRESULT(WINAPI*)(D, UINT, BOOL*, UINT);
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
using CreateQueryFn = HRESULT(WINAPI*)(D, D3DQUERYTYPE, IDirect3DQuery9**);

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
// Render states the route may read (motion_shadow_state_count in the shadow):
// the selector's z states (every draw while tracking), the gate-4 opaque-draw
// checks and the COLORWRITEENABLE1/2 masks saved around RT1/RT2. With
// X3M_STATE_SHADOW on, the SetRenderState hook keeps the application's values
// here and the route issues no GetRenderState for them after the first read.
// Slots 8–23 cache WRAP0–15; routed draws query only their owned indices.
// Slots 24–31 are the eight selected cutout states; opaque draws do not query them.
constexpr D3DRENDERSTATETYPE shadow_states[motion_shadow_state_count] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2,
    D3DRS_WRAP0, D3DRS_WRAP1, D3DRS_WRAP2, D3DRS_WRAP3, D3DRS_WRAP4, D3DRS_WRAP5, D3DRS_WRAP6, D3DRS_WRAP7,
    D3DRS_WRAP8, D3DRS_WRAP9, D3DRS_WRAP10, D3DRS_WRAP11, D3DRS_WRAP12, D3DRS_WRAP13, D3DRS_WRAP14, D3DRS_WRAP15,
    D3DRS_ALPHAFUNC, D3DRS_ALPHAREF, D3DRS_ZFUNC, D3DRS_FOGENABLE, D3DRS_DITHERENABLE,
    D3DRS_STENCILENABLE, D3DRS_CULLMODE, D3DRS_FILLMODE};
// Blend states kept outside the indexed shadow: the nine-state fade check's
// triple plus SEPARATEALPHABLENDENABLE, then the separate alpha triple that
// only the source-gain refusal lines read (never a gate), then BLENDFACTOR,
// which only the additive option's alpha attenuation writes and restores.
constexpr unsigned composition_blend_count = 8;
constexpr D3DRENDERSTATETYPE composition_blend_states[composition_blend_count] = {
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA, D3DRS_BLENDFACTOR};
constexpr unsigned composition_blend_index_scan(D3DRENDERSTATETYPE state) noexcept {
    for (unsigned i = 0; i < composition_blend_count; ++i) if (composition_blend_states[i] == state) return i;
    return composition_blend_count;
}
constexpr unsigned shadow_index_scan(D3DRENDERSTATETYPE state) noexcept {
    // WRAP8 starts a second, non-contiguous D3DRENDERSTATETYPE range.
    if (state >= D3DRS_WRAP0 && state <= D3DRS_WRAP7) return 8u + unsigned(state - D3DRS_WRAP0);
    if (state >= D3DRS_WRAP8 && state <= D3DRS_WRAP15) return 16u + unsigned(state - D3DRS_WRAP8);
    for (unsigned i = 0; i < 8; ++i) if (shadow_states[i] == state) return i;
    for (unsigned i = 24; i < motion_shadow_state_count; ++i) if (shadow_states[i] == state) return i;
    return unsigned(motion_shadow_state_count);
}
// Dispatch trim (docs/architecture/state-call-fast-path.md, step 4): the
// SetRenderState hook ran both scans on every application write, up to 16
// compares for the shadow plus 8 for the blend shadow. The largest documented
// D3DRENDERSTATETYPE is D3DRS_BLENDOPALPHA (209), so a 256-entry byte table
// built at compile time from the scans answers every defined state with one
// range test and one load; a value outside the table keeps the scans' answer
// for an unknown state (not shadowed), so the shadow's contents are unchanged.
constexpr unsigned state_index_table_size = 256;
static_assert(motion_shadow_state_count < state_index_table_size && composition_blend_count < state_index_table_size,
              "shadow/blend indices fit one table byte");
// Every state the tables must answer for is inside them, so the out-of-range
// arm below can only be reached by a state neither shadow tracks (largest
// today: D3DRS_BLENDOPALPHA, 209).
constexpr bool states_within_index_table() noexcept {
    for (const D3DRENDERSTATETYPE state : shadow_states) if (unsigned(state) >= state_index_table_size) return false;
    for (const D3DRENDERSTATETYPE state : composition_blend_states) if (unsigned(state) >= state_index_table_size) return false;
    return unsigned(D3DRS_WRAP7) < state_index_table_size && unsigned(D3DRS_WRAP15) < state_index_table_size;
}
static_assert(states_within_index_table(), "every shadowed render state is inside the index tables");
struct StateIndexTables {
    unsigned char shadow[state_index_table_size]{};
    unsigned char blend[state_index_table_size]{};
};
constexpr StateIndexTables make_state_index_tables() noexcept {
    StateIndexTables tables{};
    for (unsigned i = 0; i < state_index_table_size; ++i) {
        tables.shadow[i] = static_cast<unsigned char>(shadow_index_scan(D3DRENDERSTATETYPE(i)));
        tables.blend[i] = static_cast<unsigned char>(composition_blend_index_scan(D3DRENDERSTATETYPE(i)));
    }
    return tables;
}
constexpr StateIndexTables state_index_tables = make_state_index_tables();
// The table equals the scan for every value it covers, proved at compile time.
constexpr bool state_index_tables_match_scans() noexcept {
    for (unsigned i = 0; i < state_index_table_size; ++i)
        if (state_index_tables.shadow[i] != shadow_index_scan(D3DRENDERSTATETYPE(i))
            || state_index_tables.blend[i] != composition_blend_index_scan(D3DRENDERSTATETYPE(i))) return false;
    return true;
}
static_assert(state_index_tables_match_scans(), "state index tables equal the scans for 0..255");
constexpr unsigned shadow_index(D3DRENDERSTATETYPE state) noexcept {
    const unsigned value = unsigned(state);
    return value < state_index_table_size ? state_index_tables.shadow[value] : unsigned(motion_shadow_state_count);
}
constexpr unsigned composition_blend_index(D3DRENDERSTATETYPE state) noexcept {
    const unsigned value = unsigned(state);
    return value < state_index_table_size ? state_index_tables.blend[value] : composition_blend_count;
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
    if (reference_accounting_busy()) return 0;
    unsigned count = (target_surface_ ? 1 : 0) + taa_references_;
    if (hdr_) count += hdr_->references();
    if (composition_) count += composition_->references();
    if (depth_surface_) ++count;
    if (fade_witness_.copy) ++count; // the witness's retained system-memory readback surface
    if (packed_sample_.copy) ++count; // the packed_sample diagnostic's retained readback surfaces (post and pre)
    if (packed_sample_.pre_copy) ++count;
    if (sentinel_ps_) ++count;
    if (sentinel_mrt_ps_) ++count;
    if (sun_sentinel_ps_) ++count;
    if (quad_vs_) ++count;
    if (quad_declaration_) ++count;
    for (const auto& entry : vertex_) { if (entry.second.variant) ++count; if (entry.second.material_variant) ++count; if (entry.second.xt_default_ordinary_variant) ++count; if (entry.second.xt_default_linear_variant) ++count; if (entry.second.distance_fade_variant) ++count; }
    for (const auto& entry : pixel_) { if (entry.second.variant) ++count; if (entry.second.material_variant) ++count; if (entry.second.xt_default_ordinary_variant) ++count; if (entry.second.xt_default_linear_variant) ++count; if (entry.second.distance_fade_variant) ++count; if (entry.second.emission_variant) ++count; if (entry.second.source_gain_variant) ++count; if (entry.second.original_fill_variant) ++count; if (entry.second.screen_variant) ++count; if (entry.second.screen_additive_variant) ++count; if(entry.second.sun_motion_variant)++count; if(entry.second.sun_material_variant)++count; if(entry.second.sun_xt_variant)++count; }
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
    // Invalidate borrowed pair pointers before any reentrant owned Release.
    shadow_.xt_default_pair = shadow_.xt_default_ready = false;
    shadow_.vs_xt_default_ordinary = shadow_.vs_xt_default_linear = nullptr;
    shadow_.ps_xt_default_ordinary = nullptr;
    shadow_.ps_sun_motion=shadow_.ps_sun_material=shadow_.ps_sun_xt=nullptr;shadow_.ps_sun_extraction=false;
    shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
    shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.ps_emission_variant = nullptr;
    shadow_.ps_source_gain_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.ps_original_fill_variant = nullptr; shadow_.original_fill_pair = false;
    shadow_.screen_pair = false; shadow_.screen_eligible_variant = nullptr; shadow_.ps_screen_variant = nullptr;
    shadow_.screen_additive_pair = false; shadow_.screen_additive_index = screen_emission::pair_count; shadow_.ps_screen_additive_variant = nullptr;
    shadow_.vs_registered = false; shadow_.vs_fade_variant = nullptr; shadow_.ps_registered = false; shadow_.ps_fade_variant = nullptr;
    drop_redirect();
    if (composition_) { composition_->detach(); composition_.reset(); }
    fade_bounds_.clear();
    release_fade_witness(); release_packed_sample();
    release_target();
    // The passes are destroyed with their objects: the destructor's second call
    // of this function must not probe a device that no longer exists.
    if (hdr_) { hdr_->shutdown(); hdr_.reset(); hdr_enabled_ = false; }
    if (taa_) { taa_call([&] { taa_->shutdown(); }); taa_.reset(); }
    if (ao_ || ao_timing_created_) { taa_call([&] { ao_timing_release(); if (ao_) ao_->detach(); }); ao_.reset(); }
    release_depth_leases();
    if (depth_replay_) { taa_call([&] { depth_replay_->detach(); }); depth_replay_.reset(); }
    release(sentinel_ps_);
    release(sentinel_mrt_ps_); release(sun_sentinel_ps_);
    release(quad_vs_); release(quad_declaration_);
    for (auto& entry : vertex_) { entry.second.registered = false; release(entry.second.variant); release(entry.second.material_variant); release(entry.second.xt_default_ordinary_variant); release(entry.second.xt_default_linear_variant); release(entry.second.distance_fade_variant); }
    for (auto& entry : pixel_) { entry.second.registered = false; release(entry.second.variant); release(entry.second.material_variant); release(entry.second.xt_default_ordinary_variant); release(entry.second.xt_default_linear_variant); release(entry.second.distance_fade_variant); release(entry.second.emission_variant); release(entry.second.source_gain_variant); release(entry.second.original_fill_variant); release(entry.second.screen_variant); release(entry.second.screen_additive_variant); release(entry.second.sun_motion_variant); release(entry.second.sun_material_variant); release(entry.second.sun_xt_variant); }
    shadow_.vs_variant = nullptr; shadow_.ps_variant = nullptr;
    shadow_.vs_material_variant = nullptr; shadow_.ps_material_variant = nullptr;
    shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
    history_.invalidate();
    fill_pending_ = false;
    // Final retirement already owns the full logging/CPU-state boundary; do
    // not lose a first unavailable event when no Present follows its setter.
    report_xt_default_unavailable();
    report_mip_bias_game_write_failure();
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
    sun_frame_.available=false; sun_lane_active_=false;
    release(depth_surface_);
    release(target_surface_);
    target_width_ = target_height_ = 0; target_generation_ = 0;
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
    // A valid saved mask precedes each attempted write. Failed setters may
    // mutate, so retain the attempt until rollback restores the slot.
    if (!lazy_mode_) {
        hr = render_state(D3DRS_COLORWRITEENABLE1, &route.saved_write1);
        if (SUCCEEDED(hr)) { route.rt_set = true; hr = bind_target(1, target_surface_); }
        if (SUCCEEDED(hr)) { route.write_set = true; hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, 15); }
        if (SUCCEEDED(hr) && route.depth) {
            // A fade-band draw keeps RT2 bound for its oC2 write but masks it
            // off: the depth fragment's alpha is z/w, which the draw's
            // SRCALPHA/INVSRCALPHA blend would fold into the stored depth.
            hr = render_state(D3DRS_COLORWRITEENABLE2, &route.saved_write2);
            if (SUCCEEDED(hr)) { route.rt2_set = true; hr = bind_target(2, depth_surface_); }
            if (SUCCEEDED(hr)) { route.write2_set = true; hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, route.fade_arm ? 0 : 15); }
        }
        return hr;
    }
    if (!lazy_rt1_) {
        hr = render_state(D3DRS_COLORWRITEENABLE1, &lazy_write1_);
        if (SUCCEEDED(hr)) { lazy_rt1_ = true; hr = bind_target(1, target_surface_); }
        if (SUCCEEDED(hr)) hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE1, 15);
    }
    if (SUCCEEDED(hr) && route.depth && !lazy_rt2_) {
        hr = render_state(D3DRS_COLORWRITEENABLE2, &lazy_write2_);
        if (SUCCEEDED(hr)) { lazy_rt2_ = true; hr = bind_target(2, depth_surface_); }
        if (SUCCEEDED(hr)) hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, 15);
    } else if (SUCCEEDED(hr) && !route.depth && lazy_rt2_) {
        // A motion-only row after a depth row: its variant writes no oC2, so
        // RT2 goes back exactly as the per-draw mode would leave it.
        hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, lazy_write2_);
        if (SUCCEEDED(hr)) { hr = bind_target(2, nullptr); lazy_rt2_ = FAILED(hr); }
    }
    if (SUCCEEDED(hr) && route.depth && route.fade_arm && lazy_rt2_) {
        // Fade-band draw under the kept binding: mask RT2 for this draw only;
        // the undo puts the lazy mask (15) back.
        route.saved_write2 = 15; route.write2_set = true;
        hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_COLORWRITEENABLE2, 0);
    }
    return hr;
}
// Lazy mode: put the application's RT1/RT2 bindings and write masks back
// (reverse order of the bind). No-op in per-draw mode or when nothing is
// bound, so every hook may call it unconditionally before a native call.
void MotionOutput::restore_bindings() noexcept {
    restore_bindings_checked(); // The checked path owns sticky quarantine.
}
HRESULT MotionOutput::restore_bindings_checked() noexcept {
    HRESULT first = deferred_flush_result_;
    record_deferred();
    if (sampler_biased_mask_) {
        const HRESULT mip = restore_mip_bias();
        if (SUCCEEDED(first)) first = mip;
    }
    // Preserve an earlier deferred/mip restoration failure before a later
    // lazy flush can latch its own error. Every caller, including a void
    // restore point before draw admission, leaves unknown state quarantined.
    auto quarantine = [&](HRESULT hr) {
        if (SUCCEEDED(hr)) return;
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = hr; invalidate_taa(TaaInvalidateSite::RestoreFailed); }
        if (composition_effective_) { composition_state_lost_ = true; composition_frame_stopped_ = true; }
    };
    quarantine(first);
    if (lazy_rt1_ || lazy_rt2_) {
        const HRESULT lazy = flush_bindings<false>();
        if (SUCCEEDED(first)) first = lazy;
        if (FAILED(first) && logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=lazy_flush", id_, frame_, counters_.draws, first);
        }
    }
    quarantine(first);
    return first;
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
    if (FAILED(first)) {
        ++counters_.restore_failures; invalidate_render_states();
        // Integer-only even through the light SetRenderState path. A later
        // wrapper cannot erase state loss by consuming the deferred HRESULT.
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = first; invalidate_taa(TaaInvalidateSite::RestoreFailed); }
        if (composition_effective_) { composition_state_lost_ = true; composition_frame_stopped_ = true; }
    }
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

void MotionOutput::configure_linear_materials(bool requested, const renderer::LinearMaterialConfig& config) noexcept {
    if (device_) return; // Creation-time configuration is immutable after attach.
    linear_material_requested_ = requested && renderer::linear_material_config_valid(config);
    linear_material_config_ = config;
}
void MotionOutput::configure_linear_emissions(bool requested, float gain) noexcept {
    if (device_) return; // Shader-local gain/coverage cannot change after attach.
    linear_emission_config_ = {gain, true};
    linear_emission_requested_ = requested && renderer::linear_emission_config_valid(linear_emission_config_);
}
void MotionOutput::configure_emission_source_gain(float gain) noexcept {
    if (device_) return; // Creation-time shader variant: immutable after attach.
    // One gain for all twenty pairs; gain 1 keeps the native bytes (no
    // variant, no admission, no substitution).
    emission_source_gain_requested_ = renderer::linear_emission_source_gain_valid(gain) && gain != 1.f;
    emission_source_gain_ = emission_source_gain_requested_ ? gain : 1.f;
}
void MotionOutput::configure_linear_distance_fade(bool requested) noexcept {
    if (device_) return; // Process-start shader-cache configuration only.
    distance_fade_requested_ = requested && linear_material_requested_;
}
void MotionOutput::configure_original_fill(float fill) noexcept {
    if (device_) return; // Creation-time shader variant: immutable after attach.
    original_fill_requested_ = std::isfinite(fill) && fill > 0.f && fill <= .5f && !linear_material_requested_;
    original_fill_ = original_fill_requested_ ? fill : 0.f;
}
void MotionOutput::configure_screen_emission(bool requested, float gain) noexcept {
    if (device_) return; // Process-start shader-cache configuration only.
    // No linear-material prerequisite: the packed producer is an SM1 emitter
    // promotion and the bracket composes on the HDR scene the AgX gamma2.2
    // pass owns (TAA/HDR are gated per frame and per draw). Only the stage-0
    // sRGB shadow the readiness gate reads is fed for this route below.
    screen_emission_requested_ = requested && taa_requested_;
    screen_emission_gain_ = gain;
    if (screen_emission_requested_) { screen_additive_requested_ = false; screen_additive_gain_ = 1.f; } // exclusive in either configure order
}
void MotionOutput::configure_screen_emission_additive(bool requested, float gain, bool alpha_requested, float alpha) noexcept {
    if (device_) return; // Process-start shader-cache configuration only.
    // Exclusive with the packed route (the caller already refuses both; the
    // DLL keeps the packed route). No TAA/ownership/linear-material need: the
    // draw stays in place and its pair identity comes from the shader hooks
    // alone; the FP16 target is checked per draw (hdr_state_).
    const bool valid = std::isfinite(gain) && gain >= 1.f && gain <= 8.f;
    screen_additive_requested_ = requested && valid && !screen_emission_requested_;
    screen_additive_gain_ = screen_additive_requested_ ? gain : 1.f;
    // Per-source bloom attenuation (bloom-per-source-attenuation.md, option 1).
    // The factor is quantised once here, never per draw: D3DCOLOR is 8 bits a
    // lane, so the applied k is the rounded value and the log prints it.
    const bool alpha_valid = alpha_requested && std::isfinite(alpha) && alpha >= 0.f && alpha <= 1.f;
    screen_additive_alpha_requested_ = screen_additive_requested_ && alpha_valid;
    screen_additive_alpha_ = screen_additive_alpha_requested_ ? alpha : 1.f;
    // Only a k strictly between 0 and 1 needs the blend constant; 0 is ZERO
    // and 1 is ONE, so those two need no D3DPBLENDCAPS_BLENDFACTOR.
    screen_additive_alpha_constant_ = screen_additive_alpha_requested_ && screen_additive_alpha_ > 0.f && screen_additive_alpha_ < 1.f;
    const DWORD quantised = DWORD(screen_additive_alpha_ * 255.f + .5f);
    screen_additive_alpha_factor_ = (quantised << 24) | (quantised << 16) | (quantised << 8) | quantised;
}
void MotionOutput::configure_fade_route(unsigned threshold_permille) noexcept {
    if (device_) return; // Process-start configuration only.
    fade_route_threshold_ = threshold_permille <= 1000u ? threshold_permille : fade_route::threshold_off;
}
void MotionOutput::configure_fade_witness(unsigned frames) noexcept {
    if (device_) return;
    fade_witness_interval_ = distance_fade_requested_ || screen_emission_requested_ ? frames : 0u;
}
void MotionOutput::configure_shimmer_trace(bool requested) noexcept {
    if (device_) return;
    shimmer_trace_ = requested;
}
void MotionOutput::configure_screen_emission_timing(bool requested) noexcept {
    if (device_) return; // Process-start diagnostic configuration only.
    screen_emission_timing_ = requested && screen_emission_requested_;
}

void MotionOutput::configure_mip_bias(float bias) noexcept {
    mip_bias_ = bias;
    if (bias == 0.f || !std::isfinite(bias)) { mip_bias_ = 0.f; mip_bias_bits_ = 0; return; }
    std::memcpy(&mip_bias_bits_, &mip_bias_, sizeof mip_bias_bits_);
}
bool MotionOutput::texture_levels_wanted(DWORD stage, IDirect3DBaseTexture9* texture) const noexcept {
    return mip_bias_bits_ && texture && stage < sampler_stage_count && samplers_[stage].texture != texture;
}
void MotionOutput::set_texture(DWORD stage, IDirect3DBaseTexture9* texture, DWORD levels, bool queried, int reader) noexcept {
    if (composition_requested() && !shadow_.recording) {
        const unsigned i = stage < 16 ? unsigned(stage) : stage >= D3DVERTEXTEXTURESAMPLER0 && stage <= D3DVERTEXTEXTURESAMPLER3 ? 16u + stage - D3DVERTEXTEXTURESAMPLER0 : stage == D3DDMAPSAMPLER ? 20u : 21u;
        if (i < 21) {
            composition_textures_[i] = texture;
            const unsigned bit = 1u << i;
            if (reader != 2) {
                composition_reader_known_mask_ = reader < 0 ? composition_reader_known_mask_ & ~bit : composition_reader_known_mask_ | bit;
                composition_main_sampler_mask_ = reader == 1 ? composition_main_sampler_mask_ | bit : composition_main_sampler_mask_ & ~bit;
                composition_readers_known_ = composition_reader_known_mask_ == 0x1fffffu;
            }
        }
    }
    if (stage >= sampler_stage_count || shadow_.recording) return;
    auto& s = samplers_[stage];
    if (queried) s.levels = levels;      // a new pointer: the count the hook read from it
    else if (!texture) s.levels = 0;     // unbound
    // Count-only diagnostic (X3M_FRAME_TIMING): rebinding the texture already
    // on the stage. The pointer shadow always holds a current value (no
    // texture is bound at attach and after Reset), so every call is a
    // denominator. Never elided.
    frame_timing::state_write(frame_timing::StateSet::Texture, unsigned(stage), true, s.texture == texture);
    if (!state_hooks_ && s.texture != texture && !s.biased) { s.mipfilter_known = false; s.saved_known = false; } // hooks off: re-read for the new binding
    s.texture = texture;                 // same pointer, still bound: the count stands
    const std::uint32_t bit = 1u << stage;
    sampler_bound_mask_ = texture ? sampler_bound_mask_ | bit : sampler_bound_mask_ & ~bit;
}
void MotionOutput::set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) noexcept {
    if (stage >= sampler_stage_count || shadow_.recording) return;
    auto& s = samplers_[stage];
    // Count-only diagnostic (X3M_FRAME_TIMING) on the three shadowed sampler
    // states: a write of the value already on the device. Never elided.
    if (type == D3DSAMP_SRGBTEXTURE) {
        frame_timing::state_write(frame_timing::StateSet::SamplerState, unsigned(type), s.srgb_known, s.srgb == value);
        s.srgb = value; s.srgb_known = true; return;
    }
    if (!mip_bias_bits_) return;
    if (type == D3DSAMP_MIPFILTER) {
        frame_timing::state_write(frame_timing::StateSet::SamplerState, unsigned(type), s.mipfilter_known, s.mipfilter == value);
        s.mipfilter = value; s.mipfilter_known = true; return;
    }
    if (type != D3DSAMP_MIPMAPLODBIAS) return;
    frame_timing::state_write(frame_timing::StateSet::SamplerState, unsigned(type), s.saved_known, s.saved_bias == value);
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
    const std::uint32_t bit = 1u << stage;
    // A stage whose restore already failed this frame is not retried before
    // the next Present (no re-latching or counter growth at every restore
    // point); the obligation stays recorded until a successful restore, an
    // accepted application write or Reset clears it. Only a trusted saved
    // value is ever written: after a failed application write there is none.
    if (sampler_restore_failed_mask_ & bit) return;
    if (!s.saved_known) { sampler_restore_failed_mask_ |= bit; return; }
    const HRESULT hr = native<SetSamplerStateFn>(SetSamplerState)(device_, stage, D3DSAMP_MIPMAPLODBIAS, s.saved_bias);
    ++counters_.mip_bias_restores; ++mip_bias_total_restores_;
    if (SUCCEEDED(hr)) { s.biased = false; sampler_biased_mask_ &= ~bit; return; }
    sampler_restore_failed_mask_ |= bit; if (SUCCEEDED(*first)) *first = hr;
}
void MotionOutput::release_mip_bias_retry_bound() noexcept { sampler_restore_failed_mask_ = 0; }
HRESULT MotionOutput::restore_mip_bias() noexcept {
    HRESULT first = S_OK;
    for (std::uint32_t mask = sampler_biased_mask_; mask; mask &= mask - 1) restore_mip_bias_stage(unsigned(__builtin_ctz(mask)), &first);
    if (FAILED(first)) {
        if (composition_effective_) { composition_state_lost_ = true; composition_frame_stopped_ = true; }
        ++counters_.mip_bias_failures; ++mip_bias_total_failures_; ++counters_.restore_failures;
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=mip_bias", id_, frame_, counters_.draws, first);
        }
    }
    return first;
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
// Material sampler decode is refreshed once here, independently of mip bias;
// a failed read remains unknown until another resync or successful setter.
// The union of reviewed material requirements is s0-s5. Each exact pair
// admits only its own cached mask; unrelated stages do not affect admission.
void MotionOutput::resync_samplers() noexcept {
    sampler_bound_mask_ = sampler_biased_mask_ = 0; release_mip_bias_retry_bound();
    composition_main_sampler_mask_ = composition_reader_known_mask_ = 0; composition_readers_known_ = true;
    for (auto& texture : composition_textures_) texture = nullptr;
    for (unsigned stage = 0; stage < sampler_stage_count; ++stage) {
        auto& s = samplers_[stage];
        s = SamplerShadow{};
        // The packed screen readiness gate reads stage 0 only (the diffuse sampler).
        if (state_hooks_ && ((linear_material_requested_ && stage < 6) || ((screen_emission_requested_ || screen_additive_requested_) && stage == 0)))
            s.srgb_known = SUCCEEDED(native<GetSamplerStateFn>(GetSamplerState)(device_, stage, D3DSAMP_SRGBTEXTURE, &s.srgb));
        if (!mip_bias_bits_ && !composition_requested()) continue;
        IDirect3DBaseTexture9* texture = nullptr;
        const HRESULT get = native<GetTextureFn>(GetTexture)(device_, stage, &texture);
        if (composition_requested()) {
            const int reader = FAILED(get) ? -1 : composition_texture_reader(stage, texture);
            set_texture(stage, texture, 0, false, reader);
        }
        if (SUCCEEDED(get) && texture) {
            s.texture = texture; s.levels = mip_bias_bits_ ? texture->GetLevelCount() : 0;
            sampler_bound_mask_ |= 1u << stage;
        }
        release(texture);
    }
    if (composition_requested()) for (unsigned i = 16; i < 21; ++i) {
        const DWORD stage = i < 20 ? D3DVERTEXTEXTURESAMPLER0 + i - 16 : D3DDMAPSAMPLER;
        const bool supported = i < 20 ? caps_.VertexTextureFilterCaps != 0 : (caps_.DevCaps2 & D3DDEVCAPS2_DMAPNPATCH) != 0;
        IDirect3DBaseTexture9* texture = nullptr;
        const HRESULT hr = supported ? native<GetTextureFn>(GetTexture)(device_, stage, &texture) : S_OK;
        set_texture(stage, texture, 0, false, FAILED(hr) ? -1 : composition_texture_reader(stage, texture));
        release(texture);
    }
}

// ---- selected native cutout admission -------------------------------------
static_assert(D3DCMP_GREATEREQUAL == cutout::values[0] && D3DCMP_LESSEQUAL == cutout::values[2]
    && D3DCULL_NONE == cutout::values[6] && D3DFILL_SOLID == cutout::values[7]);
static_assert(static_cast<std::uint32_t>(D3DERR_NOTAVAILABLE) == 0x8876086au);
void MotionOutput::probe_cutout_caps(bool force) noexcept {
    if (!linear_material_requested_ || !device_ || cutout_reset_pending_) return;
    if (!force && (cutout_caps_ == cutout::Capability::Ready || cutout_caps_ == cutout::Capability::Unsupported
        || (cutout_probe_frame_known_ && cutout_probe_frame_ == frame_))) return;
    cutout_probe_frame_ = frame_; cutout_probe_frame_known_ = true; ++cutout_cap_queries_;
    D3DCAPS9 caps = caps_;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    switch (fixture_cutout_cap_fault_) {
    case 1: caps.NumSimultaneousRTs = 2; break;
    case 2: caps.PrimitiveMiscCaps &= ~D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS; break;
    case 3: caps.PrimitiveMiscCaps &= ~D3DPMISCCAPS_INDEPENDENTWRITEMASKS; break;
    case 4: caps.PrimitiveMiscCaps &= ~D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING; break;
    case 5: caps.AlphaCmpCaps &= ~D3DPCMPCAPS_GREATEREQUAL; break;
    default: break;
    }
#endif
    constexpr DWORD required = D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS
        | D3DPMISCCAPS_INDEPENDENTWRITEMASKS | D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
    HRESULT result = D3DERR_NOTAVAILABLE;
    auto verdict = cutout::Capability::Unsupported;
    HRESULT formats[3] = {S_FALSE,S_FALSE,S_FALSE};
    D3DDEVICE_CREATION_PARAMETERS creation{}; D3DDISPLAYMODE display{};
    if (caps.NumSimultaneousRTs >= 3 && (caps.PrimitiveMiscCaps & required) == required
        && (caps.AlphaCmpCaps & D3DPCMPCAPS_GREATEREQUAL)) {
        IDirect3D9* factory = nullptr;
        result = native<GetDirect3DFn>(GetDirect3D)(device_, &factory);
        if (SUCCEEDED(result) && !factory) result = E_FAIL;
        if (SUCCEEDED(result)) result = native<GetCreationFn>(GetCreationParameters)(device_, &creation);
        if (SUCCEEDED(result)) result = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (SUCCEEDED(result) && fixture_cutout_cap_fault_ == 10) { fixture_cutout_cap_fault_ = 0; result = E_FAIL; }
#endif
        const bool metadata_ready = SUCCEEDED(result);
        const D3DFORMAT targets[] = {D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F,D3DFMT_R32F};
        for (unsigned i=0;i<3 && SUCCEEDED(result);++i) {
            result = factory->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,
                D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,targets[i]);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
            if (fixture_cutout_cap_fault_ == i+6) result = D3DERR_NOTAVAILABLE;
            if (!i && fixture_cutout_cap_fault_ == 9) { fixture_cutout_cap_fault_ = 0; result = E_OUTOFMEMORY; }
#endif
            formats[i] = result;
        }
        // Only an actual format NOTAVAILABLE is a permanent capability refusal.
        // Failed adapter metadata (even NOTAVAILABLE) leaves support unknown.
        verdict = metadata_ready ? cutout::query_result(result) : cutout::Capability::Retry;
        release(factory);
    }
    cutout_cap_result_ = result; cutout_caps_ = verdict;
    // The verdict is capability configuration, not transient HDR state: it
    // refreshes the frame's arm latch (the probe runs at the HDR latch).
    cutout_arm_active_ = cutout_arm_configured();
    if (cutout_cap_logs_ < failure_log_limit) {
        ++cutout_cap_logs_;
        log("linear_cutout_device device=%llu verdict=%u result=%08lx attempts=%lu mrt=%lu misc=%08lx alpha=%08lx adapter=%u type=%u display=%u fp16=%08lx motion=%08lx depth=%08lx",
            id_,unsigned(cutout_caps_),result,cutout_cap_queries_,caps.NumSimultaneousRTs,caps.PrimitiveMiscCaps,
            caps.AlphaCmpCaps,creation.AdapterOrdinal,unsigned(creation.DeviceType),unsigned(display.Format),formats[0],formats[1],formats[2]);
    }
}
// The cutout arm is configured active by the session's options and the
// capability verdict alone: feature requested, capabilities Ready, HDR
// enabled by configuration and zero configured mip bias (a separate,
// unqualified coverage modifier). begin_frame latches it for the frame and a
// capability verdict refreshes it.
// While the configured arm is active, an opaque exact pair drawn before the
// frame's HDR latch, after a mid-frame Suspend or otherwise refused by the
// per-draw gate misses coverage; the exact source-over pair and a wholly
// inactive configuration forward it as an ordinary draw with history retained
// and no coverage verdict.
bool MotionOutput::cutout_arm_configured() const noexcept {
    return linear_material_requested_ && cutout_caps_ == cutout::Capability::Ready && hdr_enabled_ && !mip_bias_bits_;
}
bool MotionOutput::cutout_draw_state() noexcept {
    if (cutout_caps_ != cutout::Capability::Ready || cutout_reset_pending_ || !hdr_enabled_
        || hdr_state_ != HdrState::Active || !hdr_target_.known || hdr_target_.format != D3DFMT_A16B16G16R16F
        || mip_bias_bits_) return false;
    std::array<std::uint32_t,8> states{};
    for (unsigned i=0;i<states.size();++i) {
        DWORD value=0;
        if (FAILED(render_state(shadow_states[24+i],&value))) return false;
        states[i]=value;
    }
    return cutout::state(states);
}
// Fade-band arm (docs/architecture/linear-distance-fade-region.md, "Fade-band
// route"). Called only when the opaque/cutout gate-4 state failed, for the
// seven fade pairs (shadow identity, no lookup here). The exact fade-band
// state is read from the shadow (blend triple from the composition shadow
// when it is maintained, else one Get each); the fraction from the
// program's own g_AlphaValue / g_FogClip / b0 (three documented Gets) at the
// origin distance of the draw's rows. Only a recognised fade-band draw the
// arm refuses is counted as fade_refused; the device readiness reuses the
// cutout probe's verdict, whose caps (MRT post-pixel-shader blending,
// independent write masks, RGBA32F/R32F blend queries) are exactly what the
// masked RT2 and the alpha-1 RT1 blend rely on.
bool MotionOutput::fade_arm_admits(MotionRoute& route, const MotionDrawCall& call, DWORD z, DWORD z_write, std::size_t window, bool loop_bounded) noexcept {
    if (!shadow_.fade_route_pair || call.user_memory) return false;
    DWORD blend = 0, test = 1, srgb = 1, color = 0, factor[4] = {0, 0, 0, 1};
    if (FAILED(render_state(D3DRS_ALPHABLENDENABLE, &blend)) || !blend) return false;
    if (FAILED(render_state(D3DRS_ALPHATESTENABLE, &test)) || FAILED(render_state(D3DRS_SRGBWRITEENABLE, &srgb))
        || FAILED(render_state(D3DRS_COLORWRITEENABLE, &color))) return false;
    constexpr D3DRENDERSTATETYPE blend_states[4] = {D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE};
    for (unsigned i = 0; i < 4; ++i) { // blend_known: the shadow's flag (hooks on) or this draw's cache (hooks off)
        if (blend_known(i)) factor[i] = shadow_.composition_blend[i];
        else if (FAILED(render_state(blend_states[i], &factor[i]))) return false;
    }
    if (!fade_route::state(z, z_write, test, blend, color, srgb, factor[0], factor[1], factor[2], factor[3])) return false;
    // Recognised fade-band draw of a fade pair: a refusal below is counted.
    UINT frequency = 0;
    if (cutout_caps_ != cutout::Capability::Ready || cutout_reset_pending_ || !taa_enabled_ || !hdr_enabled_
        || hdr_state_ != HdrState::Active || !hdr_target_.known || hdr_target_.format != D3DFMT_A16B16G16R16F
        || FAILED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency))
        || (frequency & D3DSTREAMSOURCE_INDEXEDDATA) || (frequency & 0x3fffffffu) > 1
        || window >= motion_matrix_windows_max || !shadow_.rows_known[window] || !loop_bounded
        || !shadow_.stream0 || !shadow_.stream0_stride || !shadow_.declaration || !call.primitives
        || (call.indexed && !shadow_.indices)) { ++counters_.fade_refused; return false; }
    float alpha[4]{}, fog[4]{}; BOOL enable = FALSE; float distance = 0.f;
    const auto& r = shadow_.fade_route_registers;
    if (FAILED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, r.alpha, alpha, 1))
        || FAILED(native<GetConstantsFFn>(GetVertexShaderConstantF)(device_, r.fog, fog, 1))
        || FAILED(native<GetConstantsBFn>(GetVertexShaderConstantB)(device_, 0, &enable, 1))
        || !fade_route::origin_distance(shadow_.rows[window], camera_scene_.valid, camera_scene_.m00, camera_scene_.m11,
                                        camera_scene_.m20, camera_scene_.m21, distance)) { ++counters_.fade_refused; return false; }
    route.fade_permille = fade_route::permille(fade_route::fraction(alpha[0], enable != FALSE, fog[0], fog[1], distance));
    bool held = false;
    if (!fade_hysteresis_.admit(fade_identity(), frame_, route.fade_permille, fade_route_threshold_, held)) { ++counters_.fade_refused; return false; }
    route.fade_arm = true; route.fade_held = held;
    return true;
}
// The node identity of the current draw for the arm's hysteresis, read the
// way record_fade_refused reads it (identity only, no lifetime lookup): gate
// 4 runs before scope sampling. 0 without observation.
std::uint64_t MotionOutput::fade_identity() noexcept {
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_) return fixture_.scope.known ? fixture_.scope.node : 0;
#endif
    object_trace::Snapshot scope{};
    if (object_trace::current(&scope, false) && (scope.valid & object_trace::Node)) return scope.node;
    return 0;
}
void MotionOutput::mark_cutout_candidate(MotionRoute& route) noexcept {
    // Only the frame's configured-active arm can miss coverage: an exact pair
    // the per-draw gate refused or that failed to route. Disabled, Unsupported,
    // Retry pending, HDR off by configuration or a nonzero bias never raise a
    // reactive Unavailable.
    if (!shadow_.cutout_pair || !cutout_arm_active_) return;
    route.cutout_test_known = SUCCEEDED(render_state(D3DRS_ALPHATESTENABLE,&route.cutout_test));
    if (route.cutout_test_known && !route.cutout_test) return;
    // The game's source-over pass of the same pair (blend on, SRCALPHA /
    // INVSRCALPHA, observed exactly; ZWRITEENABLE is deliberately not part of
    // the key: a z-writing source-over draw has no cutout coverage semantics
    // either) is refused by the gate every frame the object is in view; it is
    // an ordinary native colour draw (camera reprojection only), not a
    // coverage miss. Any other or unknown factor stays a conservative miss.
    // ALPHABLENDENABLE is a warm shadow slot the gate already read; the two
    // factors come from the composition blend shadow when it is maintained
    // (composition requested) and otherwise from one native GetRenderState
    // each, only on a blended exact-pair draw.
    route.cutout_blend_known = SUCCEEDED(render_state(D3DRS_ALPHABLENDENABLE,&route.cutout_blend));
    if (route.cutout_blend_known && route.cutout_blend) {
        DWORD factor[2]{}; bool known[2]{};
        const D3DRENDERSTATETYPE states[2] = {D3DRS_SRCBLEND, D3DRS_DESTBLEND};
        for (unsigned i = 0; i < 2; ++i) {
            if (blend_known(i)) { factor[i] = shadow_.composition_blend[i]; known[i] = true; }
            else known[i] = SUCCEEDED(render_state(states[i], &factor[i]));
        }
        route.cutout_source_over = cutout::source_over(true, route.cutout_blend, known[0], factor[0], known[1], factor[1]);
        if (route.cutout_source_over) return;
    }
    route.cutout_color_known = SUCCEEDED(render_state(D3DRS_COLORWRITEENABLE,&route.cutout_color));
    if (route.cutout_color_known && !(route.cutout_color & 7u)) return;
    route.cutout_candidate = true;
    route.cutout_alpha_known = SUCCEEDED(render_state(D3DRS_ALPHAFUNC,&route.cutout_alpha));
    route.cutout_z_known = SUCCEEDED(render_state(D3DRS_ZENABLE,&route.cutout_z));
    route.cutout_zfunc_known = SUCCEEDED(render_state(D3DRS_ZFUNC,&route.cutout_zfunc));
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
    if (i < motion_shadow_state_count) {
        // Count-only diagnostic (X3M_FRAME_TIMING): a write of the value the
        // device already holds. Nothing is elided, the native call already
        // happened (docs/architecture/state-call-fast-path.md, section (d)).
        frame_timing::state_write(frame_timing::StateSet::RenderState, unsigned(state),
                                  shadow_.states_known[i], shadow_.states[i] == value);
        shadow_.states[i] = value; shadow_.states_known[i] = true;
    }
    if (blend_shadow_requested()) {
        const unsigned blend = composition_blend_index(state);
        if (blend < composition_blend_count) { shadow_.composition_blend[blend] = value; shadow_.composition_blend_known[blend] = true; }
    }
    if ((composition_requested() || screen_emission_bound_) && state == D3DRS_FILLMODE) { shadow_.fill_mode = value; shadow_.fill_mode_known = true; }
}
// X3M_FRAME_TIMING only, once per hooked draw: a copy of the binding shadow
// the game last set. No device call, no allocation.
MotionOutput::BindingShadow MotionOutput::binding_shadow() const noexcept {
    BindingShadow out;
    if (!enabled_ || shadow_.recording) return out;
    out.vs_hash = shadow_.vs_hash; out.ps_hash = shadow_.ps_hash;
    out.stream0 = shadow_.stream0; out.indices = shadow_.indices; out.declaration = shadow_.declaration;
    for (unsigned stage = 0; stage < 4; ++stage)
        out.textures[stage] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(samplers_[stage].texture));
    out.valid = true;
    return out;
}
// Capture-log accessors: the shadowed application value or -1 when unknown.
// Pure shadow reads (no GetRenderState, no query counters), so the capture
// line cannot perturb the render-state counter invariants a fixture checks.
long MotionOutput::shadow_state_field(D3DRENDERSTATETYPE state) const noexcept {
    const unsigned i = shadow_index(state);
    return i < motion_shadow_state_count && shadow_.states_known[i] ? long(shadow_.states[i]) : -1;
}
long MotionOutput::composition_blend_field(unsigned index) const noexcept {
    return index < composition_blend_count && shadow_.composition_blend_known[index] ? long(shadow_.composition_blend[index]) : -1;
}
void MotionOutput::render_state_failed(D3DRENDERSTATETYPE state) noexcept {
    if (!enabled_ || shadow_.recording) return;
    const unsigned i=shadow_index(state);
    if (i<motion_shadow_state_count) { shadow_.states_known[i]=false; ++counters_.rs_invalidations; }
    const unsigned blend=composition_blend_index(state);
    if (blend<composition_blend_count) shadow_.composition_blend_known[blend]=false;
}
void MotionOutput::before_set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type) noexcept {
    if (!enabled_ || shadow_.recording || stage >= sampler_stage_count
        || type != D3DSAMP_MIPMAPLODBIAS || !samplers_[stage].biased) return;
    // Put back only this stage's owned bias before the application's write.
    // Otherwise a failed setter with/without mutation is indistinguishable:
    // either retaining or clearing our obligation could overwrite/leak state.
    // Preserve legacy CPU state around the additional foreign native call;
    // the ordinary light sampler path still performs integer work only.
    PreserveCpuState cpu;
    HRESULT restored = S_OK;
    restore_mip_bias_stage(unsigned(stage), &restored);
    if (FAILED(restored)) {
        ++counters_.mip_bias_failures; ++mip_bias_total_failures_; ++counters_.restore_failures;
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = restored; invalidate_taa(TaaInvalidateSite::RestoreFailed); }
        if (composition_effective_) { composition_state_lost_ = true; composition_frame_stopped_ = true; }
        // No log() here: this hook is an audited light root and even an
        // integer-only format reaches the CRT's x87 formatter. Keep the first
        // unreported failure; the counters above carry any that follow.
        auto& event = mip_bias_game_write_failure_;
        if (!event.pending && logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            event.device = id_; event.frame = frame_; event.index = counters_.draws;
            event.result = static_cast<unsigned long>(restored); event.pending = true;
        }
    }
}
void MotionOutput::report_mip_bias_game_write_failure() noexcept {
    auto& event = mip_bias_game_write_failure_;
    if (!event.pending) return;
    event.pending = false;
    log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=mip_bias_game_write",
        event.device, event.frame, event.index, event.result);
}
void MotionOutput::sampler_state_failed(DWORD stage,D3DSAMPLERSTATETYPE type) noexcept {
    if (stage>=sampler_stage_count || shadow_.recording) return;
    auto& s=samplers_[stage];
    if (type==D3DSAMP_SRGBTEXTURE) s.srgb_known=false;
    if (type==D3DSAMP_MIPFILTER) s.mipfilter_known=false;
    if (type==D3DSAMP_MIPMAPLODBIAS) {
        // before_set_sampler_state restored any owned bias; if that restore
        // failed the obligation is still recorded. Both native mutation
        // outcomes leave the device value unknown: the saved value is no
        // longer trusted, so no retry may overwrite the application's value.
        s.saved_known=false;
    }
}
HRESULT MotionOutput::get_render_state_native(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
    ++counters_.rs_gets;
    return native<GetRenderStateFn>(GetRenderState)(device_, state, value);
}
HRESULT MotionOutput::render_state(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
    ++counters_.rs_queries;
    const unsigned i = shadow_index(state);
    // Cached across draws with the shadow (hooks on), within the draw without
    // the hooks (begin_draw_reads drops it; every state is read once per draw).
    const bool cached = state_shadow_ || !state_hooks_;
    if (cached && i < motion_shadow_state_count && shadow_.states_known[i]) {
        *value = shadow_.states[i]; ++counters_.rs_hits; return S_OK;
    }
    const HRESULT hr = get_render_state_native(state, value);
    // The getter reports the device state whether or not a block is recording.
    if (cached && i < motion_shadow_state_count && SUCCEEDED(hr)) { shadow_.states[i] = *value; shadow_.states_known[i] = true; }
    return hr;
}
// Hybrid unhook (hooks off): the per-draw cache. Dropped at the top of every
// before_draw (integer stores only: the draw hooks are audited light roots),
// filled on demand by the helpers below, one native read per state per draw.
// A biased stage keeps its saved LODBIAS: after a failed after-draw restore
// the device holds the route's bias and a re-read would save that instead of
// the application's value (the hooked design's "only a trusted saved value is
// ever written"). MIPFILTER is re-read per routed draw (no hook sees the
// application change it).
void MotionOutput::begin_draw_reads() noexcept {
    std::memset(shadow_.states_known, 0, sizeof shadow_.states_known);
    std::memset(shadow_.composition_blend_known, 0, sizeof shadow_.composition_blend_known);
    shadow_.fill_mode_known = false;
    for (auto& s : samplers_) { s.srgb_known = false; s.mipfilter_known = false; if (!s.biased) s.saved_known = false; }
}
bool MotionOutput::state_known(unsigned index) noexcept {
    if (index >= motion_shadow_state_count) return false;
    if (state_hooks_) return shadow_.states_known[index];
    ++counters_.rs_queries;
    if (shadow_.states_known[index]) { ++counters_.rs_hits; return true; }
    DWORD value = 0;
    if (FAILED(get_render_state_native(shadow_states[index], &value))) return false;
    shadow_.states[index] = value; shadow_.states_known[index] = true;
    return true;
}
bool MotionOutput::blend_known(unsigned index) noexcept {
    if (index >= composition_blend_count) return false;
    if (state_hooks_) return shadow_.composition_blend_known[index];
    ++counters_.rs_queries;
    if (shadow_.composition_blend_known[index]) { ++counters_.rs_hits; return true; }
    DWORD value = 0;
    if (FAILED(get_render_state_native(composition_blend_states[index], &value))) return false;
    shadow_.composition_blend[index] = value; shadow_.composition_blend_known[index] = true;
    return true;
}
bool MotionOutput::fill_mode_known() noexcept {
    if (state_hooks_) return shadow_.fill_mode_known;
    ++counters_.rs_queries;
    if (shadow_.fill_mode_known) { ++counters_.rs_hits; return true; }
    DWORD value = 0;
    if (FAILED(get_render_state_native(D3DRS_FILLMODE, &value))) return false;
    shadow_.fill_mode = value; shadow_.fill_mode_known = true;
    return true;
}
bool MotionOutput::sampler_srgb_known(unsigned stage) noexcept {
    if (stage >= sampler_stage_count) return false;
    auto& s = samplers_[stage];
    if (state_hooks_) return s.srgb_known;
    ++counters_.rs_queries; // counted with the render-state reads: one query, a hit or one native read
    if (s.srgb_known) { ++counters_.rs_hits; return true; }
    ++counters_.rs_gets;
    DWORD value = 0;
    if (FAILED(native<GetSamplerStateFn>(GetSamplerState)(device_, stage, D3DSAMP_SRGBTEXTURE, &value))) return false;
    s.srgb = value; s.srgb_known = true;
    return true;
}
long MotionOutput::state_field(unsigned index) noexcept {
    return state_known(index) ? long(shadow_.states[index]) : -1;
}
// After a failed restoration of the route's own state changes the device's
// values are unknown: drop the shadow, the next queries read again.
void MotionOutput::invalidate_render_states() noexcept {
    for (bool& known : shadow_.states_known) known = false;
    for (bool& known : shadow_.composition_blend_known) known = false;
    ++counters_.rs_resyncs;
}

// Snapshot every consumed application state before mutation. Temporal varyings
// disable wrapping; relocated native scalars keep their original component bit
// at the new carrier, without changing the carrier's other components. Native
// setters bypass the logical shadow, and attempted writes restore per draw.
HRESULT MotionOutput::apply_wrap_states(MotionRoute& route, const renderer::MotionOutputProfile& row) noexcept {
    if (route.wrap_count || route.wrap_attempted) return D3DERR_INVALIDCALL;
    constexpr unsigned capacity = 6;
    auto slot = [&](unsigned index) noexcept -> unsigned {
        if (index >= 16) return capacity;
        for (unsigned i = 0; i < route.wrap_count; ++i)
            if (route.wrap_index[i] == index) return i;
        if (route.wrap_count == capacity) return capacity;
        route.wrap_index[route.wrap_count] = static_cast<std::uint8_t>(index);
        return route.wrap_count++;
    };
    const unsigned motion = slot(row.texcoord_index);
    const unsigned depth = route.depth ? slot(row.depth_texcoord_index) : capacity;
    if (motion == capacity || (route.depth && (depth == capacity || depth == motion))) return D3DERR_INVALIDCALL;
    const auto& contract = shadow_.material_contract;
    const unsigned transports = route.linear_material ? contract.scalar_transport_count : 0;
    if (transports > contract.scalar_transport.size() || (transports && !contract.sampler_mask)) return D3DERR_INVALIDCALL;
    unsigned sources[2]{}, destinations[2]{};
    DWORD destination_bits[capacity]{};
    for (unsigned i = 0; i < transports; ++i) {
        const auto& map = contract.scalar_transport[i];
        if (map.source_component >= 4 || map.destination_component >= 4) return D3DERR_INVALIDCALL;
        destinations[i] = slot(map.destination_texcoord);
        sources[i] = slot(map.source_texcoord);
        if (sources[i] == capacity || destinations[i] == capacity ||
            sources[i] == motion || sources[i] == depth || destinations[i] == motion || destinations[i] == depth)
            return D3DERR_INVALIDCALL;
        const DWORD bit = DWORD(1) << map.destination_component;
        if (destination_bits[destinations[i]] & bit) return D3DERR_INVALIDCALL;
        destination_bits[destinations[i]] |= bit;
    }
    DWORD desired[capacity]{};
    for (unsigned i = 0; i < route.wrap_count; ++i) {
        const HRESULT hr = render_state(shadow_states[8u + route.wrap_index[i]], &route.saved_wrap[i]);
        if (FAILED(hr)) return hr;
        desired[i] = route.saved_wrap[i];
    }
    desired[motion] = 0;
    if (route.depth) desired[depth] = 0;
    for (unsigned i = 0; i < transports; ++i) {
        const auto& map = contract.scalar_transport[i];
        const DWORD bit = DWORD(1) << map.destination_component;
        const bool wrapped = (route.saved_wrap[sources[i]] & (DWORD(1) << map.source_component)) != 0;
        desired[destinations[i]] = (desired[destinations[i]] & ~bit) | (wrapped ? bit : 0);
    }
    for (unsigned i = 0; i < route.wrap_count; ++i) {
        if (desired[i] == route.saved_wrap[i]) continue;
        route.wrap_attempted |= std::uint8_t(1u << i);
        const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, shadow_states[8u + route.wrap_index[i]], desired[i]);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
HRESULT MotionOutput::restore_wrap_states(MotionRoute& route) noexcept {
    HRESULT first = S_OK;
    for (unsigned i = route.wrap_count; i-- > 0;) {
        if (!(route.wrap_attempted & (1u << i))) continue;
        const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, shadow_states[8u + route.wrap_index[i]], route.saved_wrap[i]);
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    }
    route.wrap_attempted = route.wrap_count = 0;
    return first;
}
void MotionOutput::recover_motion_state() noexcept {
    if (!motion_state_lost_) return;
    // A successful Reset restores the API state contract. A failed resync must
    // not clear quarantine; a later successful Reset may retry all sixteen reads.
    bool known = true;
    for (unsigned i = 8; i < 24; ++i) {
        const HRESULT hr = get_render_state_native(shadow_states[i], &shadow_.states[i]);
        shadow_.states_known[i] = SUCCEEDED(hr);
        known = known && SUCCEEDED(hr);
    }
    if (known) { motion_state_lost_ = false; motion_state_error_ = D3DERR_INVALIDCALL; }
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
// Integer-only (light setter paths reach it): the site is recorded and logged
// by flush_taa_invalidate_log at the frame boundary.
void MotionOutput::invalidate_taa(TaaInvalidateSite site) noexcept {
    if (taa_) taa_->invalidate();
    camera_previous_ = renderer::CameraState{};
    taa_invalidate_pending_ |= 1u << unsigned(site);
}
const char* taa_invalidate_site_name(TaaInvalidateSite site) noexcept {
    static const char* const names[unsigned(TaaInvalidateSite::Count)] = {
        "restore_failed", "state_lost", "skip", "target", "container", "resolve_failed", "not_resolved", "present_failed",
        "reset", "comparison_exposure", "comparison_state_failed", "composition_state_lost", "composition_readers",
        "composition_export", "composition_attach", "composition_begin", "composition_refused", "composition_prepare",
        "composition_incomplete", "cutout_missed"};
    return unsigned(site) < unsigned(TaaInvalidateSite::Count) ? names[unsigned(site)] : "unknown";
}
void MotionOutput::flush_taa_invalidate_log() noexcept {
    std::uint32_t pending = taa_invalidate_pending_;
    if (!pending) return;
    taa_invalidate_pending_ = 0;
    for (unsigned site = 0; pending; ++site, pending >>= 1)
        if (pending & 1u) log("taa_invalidate device=%llu frame=%llu site=%s", id_, frame_, taa_invalidate_site_name(TaaInvalidateSite(site)));
}

MotionOutput::ComparisonExposure MotionOutput::comparison_exposure() const noexcept {
    ComparisonExposure result{};
    result.automatic = hdr_config_.exposure == renderer::ExposureMode::Auto;
    if (!hdr_enabled_ || !hdr_) return result;
    result.automatic = hdr_->exposure_mode() == renderer::ExposureMode::Auto;
    result.ev = hdr_->exposure().ev();
    result.frame_used = counters_.hdr.writebacks && counters_.hdr.tonemap
        && SUCCEEDED(counters_.hdr.tonemap_draw) && !counters_.hdr.unwind;
    if (!hdr_->tonemap_active()) { result.reason = "tonemap_unavailable"; return result; }
    if (!hdr_->caps().meter) { result.reason = "auto_not_prepared"; return result; }
    result.ready = true; result.reason = "ready";
    return result;
}
bool MotionOutput::comparison_toggle_exposure() noexcept {
    if (!comparison_boundary_available() || !comparison_exposure().ready) return false;
    const auto mode = hdr_->exposure_mode() == renderer::ExposureMode::Auto
        ? renderer::ExposureMode::Manual : renderer::ExposureMode::Auto;
    if (!hdr_->comparison_exposure(mode)) return false;
    // Exposure controls tonemap and the HDR resolve's luminance weighting.
    // Keep TAA enabled, but make its next resolve seed a fresh history.
    invalidate_taa(TaaInvalidateSite::ComparisonExposure);
    return true;
}
void MotionOutput::comparison_state_failed(HRESULT result) noexcept {
    if (FAILED(result) && !motion_state_lost_) {
        motion_state_lost_ = true; motion_state_error_ = result;
        invalidate_taa(TaaInvalidateSite::ComparisonStateFailed);
    }
}
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
    if (hdr_scene && capture_ && taa_debug_) {
        composition_diagnostic_export_ = true; flush_redirect(); composition_diagnostic_export_ = false;
    }
    taa_call([&] {
        hr = target_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&motion));
        if (SUCCEEDED(hr)) hr = depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth));
        if (SUCCEEDED(hr) && (!motion || !depth)) hr = E_NOINTERFACE;
        if (FAILED(hr)) { t.skip = unsigned(TaaSkip::Container); t.result = hr; invalidate_taa(TaaInvalidateSite::Container); }
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
            t.camera_previous_valid = camera_previous_.valid;
            const auto decision = renderer::camera_sentinel_policy(sentinel_mode_, camera_scene_, camera_previous_, camera_cut_degrees_);
            t.camera_policy = decision.policy; t.camera_reason = unsigned(decision.reason);
            t.camera_cut = decision.cut; t.camera_rotation_deg = decision.rotation_degrees;
            std::memcpy(in.clip_to_previous, decision.matrix, sizeof decision.matrix);
            in.sentinel_camera = decision.policy == 2;
            in.current_jitter[0] = jitter_[0]; in.current_jitter[1] = jitter_[1];
            in.previous_jitter[0] = jitter_previous_[0]; in.previous_jitter[1] = jitter_previous_[1];
            in.motion_policy = renderer::MotionPolicy::PerPixel;
            in.reactive_policy = renderer::ReactivePolicy::DerivedFromDepthSentinel;
            IDirect3DTexture9* composition_mask = nullptr;
            if (composition_effective_ || composition_required_producers_) {
                in.reactive_policy = renderer::ReactivePolicy::Unavailable;
                if (!composition_quarantined_ && !composition_state_lost_ && !composition_frame_stopped_
                    && composition_ && composition_->coverage_valid() && composition_->coverage_target()
                    && SUCCEEDED(composition_->coverage_target()->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&composition_mask)))
                    && composition_mask) {
                    in.reactive = composition_mask;
                    in.reactive_policy = renderer::ReactivePolicy::SupplementalMaskWithDepthSentinel;
                }
            }
            if (cutout::unavailable(cutout_coverage_missed_,
                    composition_effective_ || composition_required_producers_, composition_mask != nullptr)) {
                in.reactive = nullptr;
                in.reactive_policy = renderer::ReactivePolicy::Unavailable; // no reuse AND no history seed
            }
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
            release(composition_mask);
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
                if ((capture_ && taa_debug_)
#ifdef X3M_MOTION_OUTPUT_FIXTURE
                    || (fixture_configured_ && fixture_.force_taa_readback)
#endif
                    )
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
                if (FAILED(hr)) invalidate_taa(TaaInvalidateSite::ResolveFailed);
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
    if (!bound) { t.skip = unsigned(TaaSkip::Target); t.result = FAILED(hr) ? hr : E_FAIL; t.hdr = true; invalidate_taa(TaaInvalidateSite::Target); return true; }
    // The target's texture (the pass validates format, size and device; one
    // reference for the duration of the run).
    IDirect3DTexture9* scene = nullptr;
    HRESULT container = hdr_->target()->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&scene));
    if (SUCCEEDED(container) && !scene) container = E_NOINTERFACE;
    if (FAILED(container)) { t.skip = unsigned(TaaSkip::Container); t.result = container; t.hdr = true; invalidate_taa(TaaInvalidateSite::Container); return true; }
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
    // The bloom copy is the fallback scene end: the AO chain runs here under
    // the same contract as at the hook (RT2 complete, brackets finished, the
    // resolve follows on the same target) when the hook did not run it.
    if(bloom&&!counters_.hook_scene_end){publish_sun_lane("copy");if(candidates_requested_)publish_shadow_replay_candidates();}
    if (bloom && ao_requested_ && !counters_.ao.attempted) { counters_.ao.source = "copy"; run_ambient_occlusion(); }
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
    if (composition_state_lost_ || motion_state_lost_) { invalidate_taa(TaaInvalidateSite::StateLost); return false; }
    if (t.attempted) return false;
    t.attempted = true; t.source = unsigned(source);
    auto skip = [&](TaaSkip why) { t.skip = unsigned(why); invalidate_taa(TaaInvalidateSite::Skip); return false; };
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
        t.camera_previous_valid = camera_previous_.valid;
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
    // Capture-frame marker (ambient-occlusion.md section 8, "In-scene HUD"):
    // the frame's draw counter at the signal, so draws after the scene end
    // are identifiable by index in the per-draw capture log.
    if (capture_) log("scene_end_marker device=%llu frame=%llu draw_index=%lu", id_, frame_, static_cast<unsigned long>(counters_.draws));
    publish_sun_lane("hook");
    if (candidates_requested_) publish_shadow_replay_candidates();
    // Ambient occlusion on the owning scene target (RT2 complete, every
    // in-place bracket finished, the resolve not yet run): the resolve below
    // consumes the darkened target.
    if (ao_requested_) { counters_.ao.source = "hook"; run_ambient_occlusion(); }
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
        invalidate_taa(TaaInvalidateSite::Target); release(rt0); return;
    }
    resolve(rt0, nullptr);
    release(rt0);
}

void MotionOutput::publish_sun_lane(const char* source) noexcept {
    if(!sun_lane_requested_)return;
    const bool coverage=composition_&&sun_coverage_current_&&composition_->coverage_valid()&&
        !composition_frame_stopped_&&!composition_quarantined_&&!composition_state_lost_&&!composition_busy_;
    const bool owner=hdr_enabled_&&hdr_state_==HdrState::Active&&hdr_&&hdr_->tonemap_active()&&
        hdr_target_.known&&hdr_target_.format==D3DFMT_A16B16G16R16F&&
        hdr_config_.decode==x3::temporal::AgxDecode::gamma22&&hdr_config_.tonemap==renderer::HdrTonemap::Agx&&
        counters_.filled&&!motion_state_lost_&&!composition_state_lost_&&!composition_frame_stopped_&&
        !composition_quarantined_&&!composition_busy_&&!cutout_coverage_missed_;
    sun_frame_.failed=sun_frame_.failed||sun_lane_failed_;
    const bool available=sun_frame_.publish(sun_lane_active_&&depth_surface_,owner,coverage);
    log("sun_shadow_lane_frame device=%llu frame=%llu source=%s format=%u available=%u receiver_draws=%u covered_draws=%u untracked_writers=%u non_depth_writers=%u failed=%u owner=%u exclusion_required=%u exclusion_valid=%u shadows=0",
        id_,frame_,source,unsigned(sun_lane_active_?D3DFMT_G32R32F:D3DFMT_R32F),available,sun_frame_.receivers,sun_frame_.covered,sun_frame_.untracked,sun_frame_.non_writers,sun_frame_.failed,owner,sun_frame_.coverage_required,coverage);
    // Refusal buckets for the untracked-writer veto: one line per frame with
    // untracked writers, never per draw (docs/verification/directional-shadows.md).
    if(!sun_frame_.untracked)return;
    char buckets[320]; int used=0;
    for(unsigned i=0;i<renderer::sun_untracked_reason_count&&used>=0&&std::size_t(used)<sizeof buckets;++i){
        const int n=std::snprintf(buckets+used,sizeof buckets-std::size_t(used)," %s=%lu",renderer::sun_untracked_reason_name(i),static_cast<unsigned long>(sun_frame_.reasons[i]));
        used=n<0?-1:used+n;
    }
    if(used<0||std::size_t(used)>=sizeof buckets)buckets[0]='\0';
    log("sun_shadow_lane_refusals device=%llu frame=%llu untracked=%lu%s signatures=%u overflow=%u",
        id_,frame_,static_cast<unsigned long>(sun_frame_.untracked),buckets,sun_writer_count_,sun_writer_overflow_);
}
// Distinct untracked-writer identity (program pair, refusal reason, z state,
// declaration and stride): logged once per signature per device, at most
// sun_writer_capacity entries; the rest only increment the overflow counter.
// Reached only for a draw the frame already counted untracked (lane on).
void MotionOutput::note_sun_untracked_writer(const MotionRoute& route, renderer::SunUntrackedReason reason) noexcept {
    const SunWriterSignature signature{shadow_.vs_hash,shadow_.ps_hash,shadow_.declaration,
        std::uint32_t(shadow_.stream0_stride),std::uint8_t(reason),route.sun_z_state,std::uint8_t(shadow_.ps_registered?1u:0u)};
    for(unsigned i=0;i<sun_writer_count_;++i){
        const auto& s=sun_writers_[i];
        if(s.vs==signature.vs&&s.ps==signature.ps&&s.declaration==signature.declaration&&s.stride==signature.stride&&
           s.reason==signature.reason&&s.z_state==signature.z_state&&s.registered==signature.registered)return;
    }
    if(sun_writer_count_>=sun_writer_capacity){++sun_writer_overflow_;return;}
    sun_writers_[sun_writer_count_++]=signature;
    // The gate-4 states the chain read for this draw at the first sighting (-1
    // when the chain did not read them; not part of the signature key): alpha
    // test, RT0 mask, sRGB write, plus cutout-pair identity and whether the
    // exact cutout arm was configured on the frame, so a `state` refusal names
    // its failing term (run 28 session B: the two cutout pairs refused on every
    // frame under a nonzero mip bias, arm=0).
    const unsigned s=route.sun_draw_state;
    log("sun_shadow_lane_writer device=%llu frame=%llu index=%u vs=%016llx ps=%016llx reason=%s gate=%u registered=%u z=%u zwrite=%u z_known=%u declaration=%016llx stride=%lu test=%d mask=%d srgb=%d cutout_pair=%u arm=%u",
        id_,frame_,sun_writer_count_,signature.vs,signature.ps,renderer::sun_untracked_reason_name(unsigned(reason)),unsigned(route.gate),unsigned(signature.registered),
        unsigned(signature.z_state&1u),unsigned((signature.z_state>>1)&1u),unsigned((signature.z_state>>2)&1u),
        signature.declaration,static_cast<unsigned long>(signature.stride),
        (s&(1u<<7))?int((s>>4)&1u):-1,(s&(1u<<6))?int(s&15u):-1,(s&(1u<<8))?int((s>>5)&1u):-1,
        unsigned(shadow_.cutout_pair),unsigned(cutout_arm_active_));
}

// ---- ambient occlusion at the scene end (ambient-occlusion.md, step 2) ------
namespace {
// Default projection scratch (camera-state-and-frame-routine.md sections 3-4):
// zn = 6, zf = 2e6. The route latches m00/m11/m20/m21 only.
constexpr float ao_default_m22 = 1.000003f, ao_default_m32 = -6.0000184f;
constexpr float ao_units_per_metre = 5.f;    // 1 view unit = 0.2 m
constexpr float ao_reference_distance = 100.f; // 20 m: the distance radius_px is reported at
constexpr unsigned ao_log_limit = 8;
}
// Lazily attaches the pass for the owning target's format (the gates run once
// per format; a failed gate is remembered until the format changes).
bool MotionOutput::ensure_ambient_occlusion(D3DFORMAT target_format) noexcept {
    if (ao_ && ao_->caps().enabled && ao_target_format_ == target_format) return true;
    if (ao_attach_failed_ && ao_target_format_ == target_format) return false;
    // Hysteresis: a target format alternating with the redirect state (FP16
    // active / suspended) re-attaches at most once per ao_reattach_frames.
    if (ao_attach_count_ && frame_ < ao_attach_frame_ + ao_reattach_frames) return false;
    ao_target_format_ = target_format; ao_attach_failed_ = true; ao_attach_frame_ = frame_; ++ao_attach_count_;
    if (!ao_) { try { ao_ = std::make_unique<renderer::AmbientOcclusionPass>(); } catch (...) { ao_attach_result_ = E_OUTOFMEMORY; return false; } }
    const char* reason = "";
    HRESULT hr = E_FAIL;
    if (ao_adapter_format_ == D3DFMT_UNKNOWN) {
        D3DDISPLAYMODE display{};
        if (SUCCEEDED(hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display))) ao_adapter_format_ = display.Format;
        else reason = "adapter_query";
    }
    if (ao_adapter_format_ != D3DFMT_UNKNOWN) {
        D3DCAPS9 caps = caps_;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        // Fixture seam: X3M_FIXTURE_AO_FAULT=attach makes the shader-model gate
        // refuse (fail-closed twin of the live fixture).
        char fault[16]{};
        if (GetEnvironmentVariableA("X3M_FIXTURE_AO_FAULT", fault, sizeof fault) > 0 && !std::strcmp(fault, "attach")) caps.PixelShaderVersion = 0;
#endif
        taa_call([&] { hr = ao_->attach(device_, native_, caps, ao_adapter_format_, target_format); });
        reason = ao_->caps().reason;
        ao_attach_failed_ = FAILED(hr) || !ao_->caps().enabled;
        if (!ao_attach_failed_) ao_chain_failures_ = 0;
    }
    ao_attach_result_ = hr;
    if (ao_attach_logs_ < ao_log_limit) {
        ++ao_attach_logs_;
        log("ambient_occlusion_device device=%llu attached=%u reason=%s result=%08lx target_format=%u adapter_format=%u slots=%u radius_m=%.3f strength=%.3f debug=%u timing=%u taa_references=%u",
            id_, !ao_attach_failed_, ao_attach_failed_ ? reason : "ok", hr, unsigned(target_format), unsigned(ao_adapter_format_),
            ao_->caps().largest_program_slots, double(ao_radius_metres_), double(ao_strength_), ao_debug_, ao_timing_, taa_references_);
    }
    return !ao_attach_failed_;
}
// Timestamp queries (documented D3D9: TIMESTAMPDISJOINT brackets the pair,
// TIMESTAMPFREQ scales it). A CreateQuery refusal (D3DERR_NOTAVAILABLE) fails
// closed to CPU wall time only. Called inside taa_call.
bool MotionOutput::ao_timing_create() noexcept {
    if (ao_timing_created_) return true;
    if (ao_timing_failed_) return false;
    HRESULT hr = S_OK;
    for (auto& slot : ao_timing_slots_) {
        if (SUCCEEDED(hr)) hr = native<CreateQueryFn>(CreateQuery)(device_, D3DQUERYTYPE_TIMESTAMPDISJOINT, &slot.disjoint);
        if (SUCCEEDED(hr)) hr = native<CreateQueryFn>(CreateQuery)(device_, D3DQUERYTYPE_TIMESTAMPFREQ, &slot.frequency);
        if (SUCCEEDED(hr)) hr = native<CreateQueryFn>(CreateQuery)(device_, D3DQUERYTYPE_TIMESTAMP, &slot.begin);
        if (SUCCEEDED(hr)) hr = native<CreateQueryFn>(CreateQuery)(device_, D3DQUERYTYPE_TIMESTAMP, &slot.end);
        if (SUCCEEDED(hr) && !(slot.disjoint && slot.frequency && slot.begin && slot.end)) hr = E_FAIL;
    }
    if (FAILED(hr)) {
        ao_timing_release(); ao_timing_failed_ = true;
        if (ao_failure_logs_ < ao_log_limit) { ++ao_failure_logs_; log("ambient_occlusion_timing device=%llu queries=unavailable result=%08lx", id_, hr); }
        return false;
    }
    ao_timing_created_ = true;
    return true;
}
void MotionOutput::ao_timing_release() noexcept {
    for (auto& slot : ao_timing_slots_) { release(slot.disjoint); release(slot.frequency); release(slot.begin); release(slot.end); slot.issued = false; }
    ao_timing_created_ = false; ao_gpu_us_ = -1.; ao_gpu_frame_ = 0;
}
// Non-blocking poll of an issued slot (D3DGETDATA_FLUSH submits pending work
// without waiting); S_FALSE leaves the slot issued for the next poll.
void MotionOutput::ao_timing_poll(AoTimingSlot& slot) noexcept {
    if (!slot.issued) return;
    BOOL disjoint = FALSE; UINT64 frequency = 0, begin = 0, end = 0;
    HRESULT hr = D3DERR_DEVICELOST;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    // Fixture seam (X3M_FIXTURE_AO_FAULT=poll): the slot was marked issued
    // without queries; the poll reports a lost device before any dereference.
    if (!slot.disjoint) { ao_timing_release(); ao_timing_failed_ = true; ao_timing_lost_ = true;
        if (ao_failure_logs_ < ao_log_limit) { ++ao_failure_logs_; log("ambient_occlusion_timing device=%llu queries=lost result=%08lx frame=%llu", id_, hr, frame_); }
        return; }
#endif
    hr = slot.disjoint->GetData(&disjoint, sizeof disjoint, D3DGETDATA_FLUSH);
    if (hr == S_OK) hr = slot.frequency->GetData(&frequency, sizeof frequency, D3DGETDATA_FLUSH);
    if (hr == S_OK) hr = slot.begin->GetData(&begin, sizeof begin, D3DGETDATA_FLUSH);
    if (hr == S_OK) hr = slot.end->GetData(&end, sizeof end, D3DGETDATA_FLUSH);
    if (hr == S_FALSE) return;
    slot.issued = false;
    if (hr != S_OK) {
        // A lost device or a refused query: release the sets, CPU time only
        // until Reset. The caller re-checks ao_timing_created_ before issuing.
        ao_timing_release(); ao_timing_failed_ = true; ao_timing_lost_ = true;
        if (ao_failure_logs_ < ao_log_limit) { ++ao_failure_logs_; log("ambient_occlusion_timing device=%llu queries=lost result=%08lx frame=%llu", id_, hr, frame_); }
        return;
    }
    ao_gpu_frame_ = slot.frame;
    ao_gpu_us_ = (disjoint || !frequency || end < begin) ? -1. : double(end - begin) * 1e6 / double(frequency);
}
void MotionOutput::run_ambient_occlusion() noexcept {
    auto& a = counters_.ao;
    a.attempted = true; a.reason = "ok";
    auto skip = [&](const char* why) { a.reason = why; if (ao_timing_) log_ambient_occlusion_frame(); };
    a.enabled = ao_enabled_;
    if (!ao_enabled_) return skip("disabled");
    // The chain runs only on a frame the resolve will take (resolve_allowed's
    // preconditions, evaluated here without consuming the single attempt): the
    // darkened sample is temporally filtered or not presented at all.
    if (!taa_enabled_ || taa_failed_) return skip("taa");
    if (composition_state_lost_ || motion_state_lost_) return skip("state_lost");
    if (counters_.taa.attempted) return skip("resolved");
    if (main_msaa_) return skip("msaa");
    if (!jitter_active_) return skip("no_jitter");
    if (!counters_.filled || !target_surface_ || !depth_surface_ || !depth_enabled_) return skip("no_depth");
    if (shadow_.recording) return skip("recording");
    if (active_queries_) return skip("queries");
    if (!camera_scene_.valid) return skip("camera");
    if (!ensure_taa()) return skip("taa"); // the resolve's lazy initialization; a failure skips the resolve too
    if (sentinel_mode_ == renderer::SentinelMode::Camera) {
        // Strict mode skips the resolve on a failed transform (resolve_allowed); so does the chain.
        const auto decision = renderer::camera_sentinel_policy(sentinel_mode_, camera_scene_, camera_previous_, camera_cut_degrees_);
        if (decision.reason == renderer::SentinelReason::CurrentInvalid || decision.reason == renderer::SentinelReason::TransformFailed) return skip("camera");
    }
    if (ao_chain_failures_ >= ao_failure_limit) return skip("failed_limit");
    // The owning scene target: the FP16 target while the redirect is active,
    // else the latched main target; anything else is not the scene.
    IDirect3DSurface9* rt0 = nullptr;
    HRESULT hr = native<GetRenderTargetFn>(GetRenderTarget)(device_, 0, &rt0);
    if (FAILED(hr) || !rt0) { release(rt0); a.result = FAILED(hr) ? hr : E_FAIL; return skip("target"); }
    D3DFORMAT format = D3DFMT_UNKNOWN;
    if (hdr_state_ == HdrState::Active && hdr_ && hdr_->target() && rt0 == hdr_->target()) format = D3DFMT_A16B16G16R16F;
    else if (hdr_state_ == HdrState::Off && same(describe_surface(rt0), main_)) format = static_cast<D3DFORMAT>(main_.format);
    if (format == D3DFMT_UNKNOWN) { release(rt0); return skip("target"); }
    if (!ensure_ambient_occlusion(format)) { release(rt0); a.result = ao_attach_result_; return skip("attach"); }
    a.attached = true;
    if (ao_->reset_pending()) { release(rt0); return skip("reset_pending"); }
    IDirect3DTexture9* depth = nullptr;
    hr = depth_surface_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depth));
    if (SUCCEEDED(hr) && !depth) hr = E_NOINTERFACE;
    if (FAILED(hr)) { release(rt0); a.result = hr; return skip("depth_container"); }
    renderer::AmbientOcclusionFrame in{};
    in.depth = depth; in.target = rt0; in.width = main_.width; in.height = main_.height;
    in.params.m00 = camera_scene_.m00; in.params.m11 = camera_scene_.m11; in.params.m20 = camera_scene_.m20; in.params.m21 = camera_scene_.m21;
    in.params.m22 = ao_default_m22; in.params.m32 = ao_default_m32;
    in.params.radius_metres = ao_radius_metres_; in.params.units_per_metre = ao_units_per_metre; in.params.strength = ao_strength_;
    in.params.jitter_index = counters_.jitter_index;
    in.caller_scene_open = scene_open_; in.caller_stateblock_recording = shadow_.recording; in.caller_queries_idle = active_queries_ == 0;
    in.debug_view = ao_debug_;
    a.width = in.width; a.height = in.height;
    {
        const double hh = double((in.height + 1) / 2);
        const double px = double(ao_radius_metres_) * double(ao_units_per_metre) * double(camera_scene_.m11) * hh * .5 / double(ao_reference_distance);
        a.radius_px = float(px < double(in.params.max_radius_px) ? px : double(in.params.max_radius_px));
    }
    renderer::AmbientOcclusionResult out{};
    LARGE_INTEGER t0{}, t1{};
    taa_call([&] {
        AoTimingSlot* slot = nullptr;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        // Fixture seam (X3M_FIXTURE_AO_FAULT=poll, once): a set that looks issued
        // with no queries behind it, so the poll below fails as a lost device would.
        if (ao_timing_ && !ao_timing_created_ && !ao_timing_failed_) {
            char fault[16]{};
            if (GetEnvironmentVariableA("X3M_FIXTURE_AO_FAULT", fault, sizeof fault) > 0 && !std::strcmp(fault, "poll")) {
                ao_timing_created_ = true; ao_timing_slots_[ao_timing_cursor_ ^ 1u].issued = true; ao_timing_slots_[ao_timing_cursor_ ^ 1u].frame = frame_;
            }
        }
#endif
        if (ao_timing_ && ao_timing_create()) {
            // Poll last frame's pair first (its result is this line's gpu_us), then
            // take the other slot; a slot still pending is left alone. A failed
            // poll releases the sets, so the created state is re-checked before
            // any query is issued.
            ao_timing_poll(ao_timing_slots_[ao_timing_cursor_ ^ 1u]);
            if (ao_timing_created_) {
                slot = &ao_timing_slots_[ao_timing_cursor_];
                ao_timing_poll(*slot);
                if (!ao_timing_created_ || slot->issued || !slot->disjoint || !slot->begin) slot = nullptr;
                else {
                    HRESULT q = slot->disjoint->Issue(D3DISSUE_BEGIN);
                    if (SUCCEEDED(q)) q = slot->begin->Issue(D3DISSUE_END);
                    if (FAILED(q)) { ao_timing_release(); ao_timing_failed_ = true; slot = nullptr; }
                }
            }
        }
        if (ao_timing_) QueryPerformanceCounter(&t0);
        hr = ao_->execute(in, &out);
        if (ao_timing_) QueryPerformanceCounter(&t1);
        if (slot && ao_timing_created_) {
            HRESULT q = slot->end->Issue(D3DISSUE_END);
            if (SUCCEEDED(q)) q = slot->frequency->Issue(D3DISSUE_END);
            if (SUCCEEDED(q)) q = slot->disjoint->Issue(D3DISSUE_END);
            if (FAILED(q)) { ao_timing_release(); ao_timing_failed_ = true; }
            else { slot->issued = true; slot->frame = frame_; ao_timing_cursor_ ^= 1u; }
        }
    });
    release(depth); release(rt0);
    if (ao_timing_) {
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f);
        a.cpu_ticks = f.QuadPart ? std::uint64_t((double(t1.QuadPart - t0.QuadPart) * 1e6) / double(f.QuadPart)) : 0; // microseconds
    }
    a.result = out.operation; a.restore = out.restore; a.failed_stage = unsigned(out.failed);
    if (FAILED(out.restore)) invalidate_render_states();
    if (FAILED(hr)) {
        // Fail closed: the pass restored its block and published nothing; the
        // frame continues untouched (a lost device is reported by the frame line).
        // ao_failure_limit consecutive failures refuse the device until Reset.
        a.reason = "failed"; ++ao_chain_failures_;
        if (ao_failure_logs_ < ao_log_limit) {
            ++ao_failure_logs_;
            log("ambient_occlusion_failed device=%llu frame=%llu result=%08lx restore=%08lx stage=%u failures=%u limit=%u", id_, frame_, out.operation, out.restore, unsigned(out.failed), ao_chain_failures_, ao_failure_limit);
        }
    } else { a.ran = true; a.applied = out.applied; ao_chain_failures_ = 0; }
    if (ao_timing_) log_ambient_occlusion_frame();
}
// One line per frame in timing/debug mode. gpu_us is the most recently
// completed timestamp pair (gpu_frame names its frame; normally the previous
// one), -1 while none completed, the queries are unavailable or the interval
// was disjoint. cpu_us is the wall time of the execute call. radius_px is the
// half-resolution screen radius at 20 m, capped at the pass's limit.
void MotionOutput::log_ambient_occlusion_frame() noexcept {
    const auto& a = counters_.ao;
    const char* timing = ao_timing_created_ ? "queries" : ao_timing_lost_ ? "lost" : ao_timing_failed_ ? "unavailable" : "pending";
    log("ambient_occlusion_frame device=%llu frame=%llu attached=%u ran=%u reason=%s gpu_us=%.1f cpu_us=%.1f width=%u height=%u radius_px=%.2f gpu_frame=%llu enabled=%u source=%s gpu_timing=%s applied=%u result=%08lx restore=%08lx stage=%u debug=%u",
        id_, frame_, a.attached, a.ran, a.reason, ao_gpu_us_, double(a.cpu_ticks), a.width, a.height, double(a.radius_px), ao_gpu_frame_,
        a.enabled, a.source, timing, a.applied, a.result, a.restore, a.failed_stage, ao_debug_);
}
// Runtime emitter A/B (comparison-hotkeys.md). Nothing is created, released
// or reconfigured here: the flag only decides whether the per-draw admission
// runs at all, so an off option draws exactly as it would without it. The
// source gain at 1 has no variant; its key is a logged no-op. The additive option has no such case: gain 1 still draws with
// DESTBLEND ONE (and the alpha attenuation, if any), so F5 switches it
// whenever the option is requested, variant or not.
int MotionOutput::screen_emission_additive_toggle() noexcept {
    const bool available = screen_additive_requested_;
    if (available) screen_additive_enabled_ = !screen_additive_enabled_;
    log("screen_emission_additive_toggle device=%llu frame=%llu accepted=%u enabled=%u requested=%u gain=%g",
        id_, frame_, unsigned(available), unsigned(screen_additive_enabled_), unsigned(screen_additive_requested_), double(screen_additive_gain_));
    return available ? (screen_additive_enabled_ ? 1 : 0) : -1;
}
int MotionOutput::emission_source_gain_toggle() noexcept {
    const bool available = emission_source_gain_requested_ && emission_source_gain_ != 1.f;
    if (available) source_gain_enabled_ = !source_gain_enabled_;
    log("emission_source_gain_toggle device=%llu frame=%llu accepted=%u enabled=%u requested=%u gain=%g",
        id_, frame_, unsigned(available), unsigned(source_gain_enabled_), unsigned(emission_source_gain_requested_), double(emission_source_gain_));
    return available ? (source_gain_enabled_ ? 1 : 0) : -1;
}
int MotionOutput::ambient_occlusion_toggle() noexcept {
    if (!ao_requested_) return -1;
    ao_enabled_ = !ao_enabled_;
    log("ambient_occlusion_toggle device=%llu frame=%llu enabled=%u", id_, frame_, ao_enabled_);
    return ao_enabled_ ? 1 : 0;
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
    const bool lane=sun_lane_requested_&&sun_lane_qualified_&&!sun_lane_failed_&&
        sun_lane_depth_qualified(static_cast<D3DFORMAT>(main_depth_.format));
    // Scene latch only, before any fill/routing. Switching an exact qualified
    // attachment or retiring a failed lane never changes storage mid-frame.
    if (target_surface_ && target_width_ == width && target_height_ == height && sun_lane_active_==lane) return true;
    // Only a failed enhanced lane changing storage within this exact device,
    // size and reset generation may retain row correspondence. R32F temporal
    // history already contains the prior G32 input's point-copied .r; the new
    // current RT1/RT2 receive the ordinary sentinel fill before any draw.
    const bool preserve_rows = target_surface_ && sun_lane_active_ && !lane && sun_lane_failed_
        && target_generation_ == generation_ && target_width_ == width && target_height_ == height;
    release_target();
    if (target_failed_ || !width || !height) return false;
    sun_lane_active_=lane;
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
            sun_lane_active_?D3DFMT_G32R32F:D3DFMT_R32F, D3DPOOL_DEFAULT, &texture, nullptr);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
         if(sun_lane_active_&&!std::strcmp(fault,"allocation")){release(texture);depth_hr=E_OUTOFMEMORY;}}
#endif
        depth_level = E_FAIL;
        if (SUCCEEDED(depth_hr) && texture) depth_level = texture->GetSurfaceLevel(0, &depth_surface_);
        release(texture);
        if(sun_lane_active_&&(FAILED(depth_hr)||FAILED(depth_level)||!depth_surface_)){
            // Discard the incomplete enhancement and retry only the ordinary
            // depth allocation. RT1 and all ordinary cached shaders survive.
            release(depth_surface_); sun_lane_active_=false; sun_lane_failed_=true;
            depth_hr=native<CreateTextureFn>(CreateTexture)(device_,width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&texture,nullptr);
            depth_level=E_FAIL;
            if(SUCCEEDED(depth_hr)&&texture)depth_level=texture->GetSurfaceLevel(0,&depth_surface_);
            release(texture);
        }
        if (FAILED(depth_hr) || FAILED(depth_level) || !depth_surface_) hr = FAILED(depth_hr) ? depth_hr : E_FAIL;
    }
    if (FAILED(hr) || FAILED(level) || !target_surface_) {
        log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx depth=%u depth_create=%08lx depth_level=%08lx",
            id_, width, height, hr, level, depth_enabled_, depth_hr, depth_level);
        release_target();
        target_failed_ = true; // Retry only after Reset; do not spam allocation per frame.
        return false;
    }
    target_width_ = width; target_height_ = height; target_generation_ = generation_;
    if (!preserve_rows) history_.invalidate();
    log("motion_output_target device=%llu width=%u height=%u create=%08lx level=%08lx depth=%u depth_create=%08lx depth_level=%08lx",
        id_, width, height, hr, level, depth_enabled_, depth_hr, depth_level);
    return true;
}

void MotionOutput::attach(IDirect3DDevice9* device, void** native_table, std::uint64_t device_id,
                          const D3DCAPS9& caps, bool requested, telemetry::State* stats) noexcept {
    device_ = device; native_ = native_table; id_ = device_id; caps_ = caps; requested_ = requested;
    stats_ = stats; lazy_rt1_ = lazy_rt2_ = false;
    enabled_ = false; depth_enabled_ = false;
    sun_writer_count_ = sun_writer_overflow_ = 0; // signature cache is per device
    candidate_witnesses_ = 0; candidates_.reset(); candidate_pools_ = {}; candidates_published_frame_ = ~std::uint64_t(0); // witness cap, pool cache and frame serial are per device
    release_depth_leases(); depth_sun_written_ = false; depth_replay_attach_failed_ = false; depth_basis_ = {}; // depth replay state is per device
    for (unsigned& logged : depth_refusal_logs_) logged = 0;
    depth_cascade_ = renderer::ShadowReplayCascade{}; depth_cascade_.size = depth_replay_size_;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    { // Seam only: X3M_FIXTURE_SHADOW_EXTENT narrows cascade 0 (half-extent E,
      // centred on the camera, depth +-2E) to the fixture's unit-size geometry.
      char extent_text[16]{};
      if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_EXTENT", extent_text, sizeof extent_text) > 0) {
          const float value = std::strtof(extent_text, nullptr);
          if (std::isfinite(value) && value > 0.f) { depth_cascade_.half_extent = value; depth_cascade_.forward_offset = 0.f; depth_cascade_.depth_half_range = 2.f * value; }
      } }
#endif
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    { // Seam only: X3M_FIXTURE_SLICE_NEAR moves the slice-0 near bound so the
      // fixture's unit-distance triangles are candidates; production keeps 6.
      char near_text[16]{}; candidate_slice_near_ = shadow_replay::slice0_near;
      if (GetEnvironmentVariableA("X3M_FIXTURE_SLICE_NEAR", near_text, sizeof near_text) > 0) {
          const float value = std::strtof(near_text, nullptr);
          if (std::isfinite(value) && value >= 0.f) candidate_slice_near_ = value;
      } }
#endif
    if (!requested) return;
    probe_cutout_caps(true);
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
    // Step C implies step B through the route's own gate (the option with its
    // prerequisites, capture.cpp), never through the raw variable.
    { char setting[8]{}; screen_emission_bound_ = screen_emission_requested_
        || (GetEnvironmentVariableA("X3M_SCREEN_EMISSION_BOUND", setting, sizeof setting) == 1 && setting[0] == '1'); }
    { char setting[8]{}; locked_prefix_log_ = GetEnvironmentVariableA("X3M_LOCKED_PREFIX_LOG", setting, sizeof setting) == 1 && setting[0] == '1'; }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    { char setting[8]{}; fixture_stretch_fault_ = GetEnvironmentVariableA("X3M_FIXTURE_STRETCH_FAULT", setting, sizeof setting) == 1 && setting[0] == '1'; }
    {
        char setting[64]{}; long l = 0, t = 0, r = 0, b = 0;
        fixture_fade_rect_set_ = GetEnvironmentVariableA("X3M_FIXTURE_FADE_RECT", setting, sizeof setting) > 0
            && std::sscanf(setting, "%ld,%ld,%ld,%ld", &l, &t, &r, &b) == 4 && l < r && t < b;
        if (fixture_fade_rect_set_) fixture_fade_rect_ = {std::int32_t(l), std::int32_t(t), std::int32_t(r), std::int32_t(b)};
        char screen_setting[64]{}; l = t = r = b = 0;
        fixture_screen_rect_set_ = GetEnvironmentVariableA("X3M_FIXTURE_SCREEN_RECT", screen_setting, sizeof screen_setting) > 0
            && std::sscanf(screen_setting, "%ld,%ld,%ld,%ld", &l, &t, &r, &b) == 4 && l < r && t < b;
        if (fixture_screen_rect_set_) fixture_screen_rect_ = {std::int32_t(l), std::int32_t(t), std::int32_t(r), std::int32_t(b)};
        char caps_setting[8]{};
        fixture_screen_caps_fault_ = GetEnvironmentVariableA("X3M_FIXTURE_SCREEN_CAPS_FAULT", caps_setting, sizeof caps_setting) == 1 && caps_setting[0] == '1';
    }
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
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    // Attach-only capability-subset witness: exercise the existing motion-only
    // variant/target path without changing the device's advertised caps.
    { char setting[8]{};
      if(GetEnvironmentVariableA("X3M_FIXTURE_MOTION_DEPTH",setting,sizeof setting)==1&&setting[0]=='0')
          depth_reason="fixture_motion_only";
    }
#endif
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
    qualify_sun_lane();
    if (composition_requested() && taa_enabled_ && hdr_enabled_) {
        D3DDISPLAYMODE display{};
        composition_busy_ = true;
        if (SUCCEEDED(native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display))) composition_adapter_format_ = display.Format;
        composition_busy_ = false;
    }
    log("motion_output_device device=%llu enabled=%u reason=%s detail=%s mrt=%lu vs_constants=%lu misc=%08lx vs=%08lx ps=%08lx rgba32f=%08lx history_available=%u history_capacity=%u depth=%u depth_reason=%s depth_detail=%s r32f=%08lx jitter=%u jitter_samples=%u taa=%u taa_reason=%s taa_format=%08lx taa_copy=%s taa_stretch_query=%08lx taa_stretch_test=%s taa_debug=%u rt_mode=%s camera=%s sentinel=%u camera_cut_deg=%.2f camera_log=%u state_shadow=%u state_hooks=%u scene_hook=%u hdr=%u mip_bias=%g quad_fvf=%u",
        id_, enabled_, reason, detail[0] ? detail : "-", caps.NumSimultaneousRTs, caps.MaxVertexShaderConst,
        caps.PrimitiveMiscCaps, caps.VertexShaderVersion, caps.PixelShaderVersion, format_result,
        history_available_, unsigned(history_.stats().capacity), depth_enabled_, depth_reason,
        depth_detail[0] ? depth_detail : "-", depth_format_result, jitter_requested_, jitter_samples_,
        taa_enabled_, taa_reason, taa_format_result, taa_enabled_ ? (taa_copy_draw_ ? "draw" : "stretch") : "off", stretch_query, taa_stretch_test_, taa_debug_, lazy_mode_ ? "lazy" : "perdraw",
        camera_state::status(), unsigned(sentinel_mode_), camera_cut_degrees_, camera_log_interval_, state_shadow_, state_hooks_, scene_hook_installed_, hdr_enabled_,
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

#include "sun_share_lane_inc.h"

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
                                 depth ? (sun_lane_active_?sun_sentinel_ps_:sentinel_mrt_ps_) : sentinel_ps_, target_width_, target_height_, &restore);
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
    sun_writer_count_ = sun_writer_overflow_ = 0; // declaration ids may be recycled across Reset
    cutout_caps_ = cutout::Capability::Pending; cutout_cap_result_ = S_FALSE;
    cutout_probe_frame_known_ = false; cutout_reset_pending_ = true;
    for (auto& logged : source_gain_logged_) logged = 0; // a new device epoch may log its refusal samples again (same per-reason cap)
    source_gain_pair_logged_ = 0; // and its first admission per pair
    shadow_.xt_default_pair = shadow_.xt_default_ready = false;
    shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
    shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.ps_emission_variant = nullptr;
    shadow_.ps_source_gain_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.ps_original_fill_variant = nullptr; shadow_.original_fill_pair = false;
    shadow_.vs_registered = false; shadow_.vs_fade_variant = nullptr; shadow_.ps_registered = false; shadow_.ps_fade_variant = nullptr;
    // D3DPOOL_DEFAULT objects must not exist across Reset; shaders survive it.
    // The pass releases its histories, scratch and state block after RT1/RT2
    // and keeps its resolve shader. A lazily bound RT1/RT2 is unbound first.
    restore_bindings();
    for (auto& sampler : samplers_) sampler.srgb_known = false;
    // Reset discards the frame: no write-back, but the application's main
    // surface goes back to RT0 first so the FP16 texture is not kept alive by
    // the binding through the Reset (and is not RT0 after a failed one).
    if (hdr_state_ == HdrState::Active && hdr_ && hdr_main_) hdr_->bind(hdr_main_, nullptr);
    drop_redirect(); // The FP16 target goes with RT1/RT2.
    release_target();
    if (composition_) { composition_busy_ = true; composition_->before_reset(); composition_busy_ = false; }
    fade_bounds_.clear(); // relearned after Reset; allocation ids never recur
    fade_hysteresis_.clear(); // the arm starts again at the threshold after Reset
    release_fade_witness(); release_packed_sample(); // the M target is recreated after Reset; the copies follow its size
    composition_state_lost_ = false; composition_frame_stopped_ = false; composition_attach_attempted_ = false;
    composition_adapter_format_ = D3DFMT_UNKNOWN; // Reset may change the adapter display format.
    if (hdr_) hdr_->before_reset();
    hdr_target_failed_ = false; hdr_blocked_ = false; hdr_blocked_latches_ = 0; // a Reset clears the cause of an unwind
    if (taa_) taa_call([&] { taa_->before_reset(); });
    // The AO targets and block go after RT1/RT2 (design section 5); the
    // timestamp queries are device objects and go with them (recreated lazily).
    if (ao_ || ao_timing_created_) taa_call([&] { ao_timing_release(); if (ao_) ao_->before_reset(); });
    ao_timing_failed_ = false; ao_timing_lost_ = false; ao_chain_failures_ = 0;
    if (depth_replay_requested_) { release_depth_leases(); if (depth_replay_) taa_call([&] { depth_replay_->before_reset(); }); depth_replay_attach_failed_ = false; }
    ao_attach_failed_ = false; ao_target_format_ = D3DFMT_UNKNOWN; // a transient attach failure is retried after Reset
    // The re-attach hysteresis counts format alternation within one device
    // lifetime; a Reset starts a new one, so the first post-Reset frame must be
    // free to re-attach instead of waiting out ao_reattach_frames.
    ao_attach_count_ = 0; ao_attach_frame_ = 0;
    ao_adapter_format_ = D3DFMT_UNKNOWN; // Reset may change the adapter display format (as for the composition pass).
    target_failed_ = false;
    history_.invalidate();
    selector_.invalidate();
    fill_pending_ = false; pending_valid_ = false;
    main_ = {}; main_depth_ = {}; main_msaa_ = false; main_msaa_samples_ = 0; msaa_logged_ = false;
    camera_state::reset(); camera_previous_ = renderer::CameraState{};
    taa_invalidate_pending_ |= 1u << unsigned(TaaInvalidateSite::Reset); // logged by after_reset's begin_frame (the frame begun at the last Present)
}
void MotionOutput::after_reset(HRESULT result) noexcept {
    ++generation_;
    if (taa_) taa_->after_reset(result);
    if (ao_) ao_->after_reset(result);
    if (depth_replay_) depth_replay_->after_reset(result);
    scene_open_ = false; // Reset ends any application scene; BeginScene follows.
    if (!enabled_) return;
    // The interrupted frame continues after a successful Reset; capture is off.
    if (SUCCEEDED(result)) { sun_lane_failed_=false; qualify_sun_lane(); cutout_reset_pending_ = false; probe_cutout_caps(true); resync_shadow(); recover_motion_state(); begin_frame(frame_, false); }
    log("motion_output_reset device=%llu result=%08lx generation=%llu taa_references=%u", id_, result, generation_, taa_references_);
}

// ---- shader registry -------------------------------------------------------

void MotionOutput::register_vertex_shader(IDirect3DVertexShader9* shader, const DWORD* code,
                                          std::size_t bytes, std::uint64_t hash) noexcept {
    // Invalidate before map allocation, any early exit or owned-object
    // Release: a reentrant observer must never see the replaced pair contract.
    if (shader && shadow_.vs == shader) {
        shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.original_fill_pair = false; shadow_.screen_pair = false; shadow_.screen_eligible_variant = nullptr; shadow_.screen_additive_pair = false; shadow_.screen_additive_index = screen_emission::pair_count; shadow_.vs_registered = false; shadow_.vs_fade_variant = nullptr;
        shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
        shadow_.xt_default_pair = shadow_.xt_default_ready = false;
        shadow_.vs_xt_default_ordinary = shadow_.vs_xt_default_linear = nullptr;
        shadow_.vs_hash = 0; shadow_.vs_variant = nullptr; shadow_.vs_material_variant = nullptr; shadow_.vs_row = nullptr; shadow_.vs_prepass = nullptr;
    }
    if (!requested_ || !shader) return;
    try {
        auto& entry = vertex_[shader];
        entry.registered = false;
        release(entry.variant);
        release(entry.sun_motion_variant); release(entry.sun_material_variant); release(entry.sun_xt_variant); entry.sun_extraction=false;
        release(entry.material_variant);
        release(entry.xt_default_ordinary_variant);
        release(entry.xt_default_linear_variant);
        release(entry.distance_fade_variant);
        entry.hash = hash;
        entry.row = nullptr; entry.prepass = nullptr;
        if (!enabled_ || !code || !bytes || bytes % 4) return;
        if (distance_fade_requested_) {
            std::vector<std::uint32_t> words;
            const auto transformed = renderer::linear_distance_fade_vertex_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words);
            IDirect3DVertexShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (transformed == renderer::LinearMaterialResult::Applied)
                hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.distance_fade_variant = variant;
            else release(variant);
            if (transformed != renderer::LinearMaterialResult::UnsupportedShader)
                log("linear_distance_fade_variant device=%llu kind=vertex original=%016llx transform=%u create=%08lx words=%u", id_, hash, unsigned(transformed), hr, unsigned(words.size()));
        }
        // One variant per original program: rows sharing this VS agree on its
        // side of the splice (static_assert in motion_output_profiles.h), so
        // the same variant serves every reviewed pair it belongs to. Pair
        // eligibility is decided per draw in before_draw (gate 3). The row
        // lookup is a binary search over the table (no scan).
        entry.row = renderer::material_motion_vertex_row(hash, bytes / 4);
        // Depth-only prepass programs: jitter identity only (no variant, no
        // pair); a program with a pair row never needs the second path.
        if (!entry.row) entry.prepass = renderer::depth_prepass_vertex_row(hash, bytes / 4, code[0]);
        if (entry.row) {
            std::vector<std::uint32_t> words;
            const auto result = renderer::material_motion_vertex_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words, depth_enabled_);
            IDirect3DVertexShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::MaterialMotionResult::Applied)
                hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.variant = variant;
            else release(variant);
            log("motion_output_variant device=%llu kind=vs original=%016llx transform=%u create=%08lx words=%u depth=%u",
                id_, hash, unsigned(result), hr, unsigned(words.size()), renderer::material_motion_vertex_exports_depth(*entry.row, depth_enabled_));
            if (linear_material_requested_ && entry.variant) {
                words.clear();
                const auto material = renderer::linear_material_vertex_variant(
                    reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words, depth_enabled_);
                IDirect3DVertexShader9* combined = nullptr;
                HRESULT material_hr = E_FAIL;
                if (material == renderer::LinearMaterialResult::Applied)
                    material_hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &combined);
                if (SUCCEEDED(material_hr) && combined) entry.material_variant = combined;
                else release(combined);
                if (material != renderer::LinearMaterialResult::UnsupportedShader)
                    log("linear_material_variant device=%llu kind=vs original=%016llx transform=%u create=%08lx words=%u depth=%u fill_applied=0",
                        id_, hash, unsigned(material), material_hr, unsigned(words.size()), depth_enabled_);
            }
            if (linear_material_requested_) {
                // These extra programs repair only the four XT DEFAULT pairs.
                // Retain the generic shared-VS programs for all other mates.
                for (bool linear : {false, true}) {
                    words.clear();
                    const auto result = renderer::linear_material_xt_default_vertex_variant(
                        reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words, depth_enabled_, linear);
                    IDirect3DVertexShader9* repaired = nullptr;
                    HRESULT hr = E_FAIL;
                    if (result == renderer::LinearMaterialResult::Applied)
                        hr = native<CreateVsFn>(CreateVertexShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &repaired);
                    if (SUCCEEDED(hr) && repaired) {
                        if (linear) entry.xt_default_linear_variant = repaired;
                        else entry.xt_default_ordinary_variant = repaired;
                    } else release(repaired);
                    if (result != renderer::LinearMaterialResult::UnsupportedShader)
                        log("linear_material_xt_default_variant device=%llu kind=vs original=%016llx linear=%u transform=%u create=%08lx words=%u depth=%u",
                            id_, hash, linear, unsigned(result), hr, unsigned(words.size()), depth_enabled_);
                }
            }
        }
        entry.registered = true;
        if (shadow_.vs == shader) set_vertex_shader(shader);
    } catch (...) {}
}
void MotionOutput::register_pixel_shader(IDirect3DPixelShader9* shader, const DWORD* code,
                                         std::size_t bytes, std::uint64_t hash) noexcept {
    // Invalidate before map allocation, any early exit or owned-object
    // Release: a reentrant observer must never see the replaced pair contract.
    if (shader && shadow_.ps == shader) {
        shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.screen_pair = false; shadow_.screen_eligible_variant = nullptr; shadow_.screen_additive_pair = false; shadow_.screen_additive_index = screen_emission::pair_count; shadow_.ps_screen_additive_variant = nullptr; shadow_.ps_registered = false; shadow_.ps_fade_variant = nullptr; shadow_.ps_emission_variant = nullptr; shadow_.ps_source_gain_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.ps_original_fill_variant = nullptr; shadow_.original_fill_pair = false; shadow_.ps_screen_variant = nullptr;
        shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
        shadow_.xt_default_pair = shadow_.xt_default_ready = false;
        shadow_.ps_xt_default_ordinary = nullptr;
        shadow_.ps_hash = 0; shadow_.ps_variant = nullptr; shadow_.ps_material_variant = nullptr;
        shadow_.ps_sun_motion=shadow_.ps_sun_material=shadow_.ps_sun_xt=nullptr; shadow_.ps_sun_extraction=false;
    }
    if (!requested_ || !shader) return;
    try {
        auto& entry = pixel_[shader];
        entry.registered = false;
        release(entry.variant);
        release(entry.sun_motion_variant); release(entry.sun_material_variant); release(entry.sun_xt_variant); entry.sun_extraction=false;
        release(entry.material_variant);
        release(entry.xt_default_ordinary_variant);
        release(entry.xt_default_linear_variant);
        release(entry.distance_fade_variant);
        release(entry.emission_variant);
        release(entry.source_gain_variant);
        release(entry.original_fill_variant);
        release(entry.screen_variant);
        release(entry.screen_additive_variant);
        entry.hash = hash;
        entry.row = nullptr;
        if (!enabled_ || !code || !bytes || bytes % 4) return;
        if (distance_fade_requested_) {
            std::vector<std::uint32_t> words;
            bool fill_applied = false;
            const auto transformed = renderer::linear_distance_fade_pixel_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words, &fill_applied);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (transformed == renderer::LinearMaterialResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.distance_fade_variant = variant;
            else release(variant);
            if (transformed != renderer::LinearMaterialResult::UnsupportedShader)
                log("linear_distance_fade_variant device=%llu kind=pixel original=%016llx transform=%u create=%08lx words=%u fill_applied=%u",
                    id_, hash, unsigned(transformed), hr, unsigned(words.size()), unsigned(fill_applied));
        }
        // PS2 emission sources have no motion-profile row. Their augmentation
        // must complete independently, retaining the original VS and native oC0.
        if (linear_emission_requested_) {
            std::vector<std::uint32_t> words;
            const auto result = renderer::linear_emission_pixel_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_emission_config_, words);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::LinearEmissionResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.emission_variant = variant;
            else release(variant);
            if (result != renderer::LinearEmissionResult::UnsupportedShader)
                log("linear_emission_variant device=%llu original=%016llx transform=%u create=%08lx words=%u gain=%g coverage=1",
                    id_, hash, unsigned(result), hr, unsigned(words.size()), double(linear_emission_config_.gain));
        }
        // Source-only encoded gain (linear-emission-cost.md, "Implemented"):
        // the same ten PS2 emission programs with one colour MUL, one variant
        // per original program at the one gain (never 1 here); created once,
        // selected per draw by the bound pair in refresh_linear_emission_contract.
        if (emission_source_gain_requested_) {
            std::vector<std::uint32_t> words;
            const auto result = renderer::linear_emission_source_gain_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, emission_source_gain_, words);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::LinearEmissionResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.source_gain_variant = variant;
            else release(variant);
            if (result != renderer::LinearEmissionResult::UnsupportedShader)
                log("emission_source_gain_variant device=%llu original=%016llx transform=%u create=%08lx words=%u gain=%g",
                    id_, hash, unsigned(result), hr, unsigned(words.size()), double(emission_source_gain_));
        }
        // Step C (screen-emission-region.md): the promoted PackedScreen
        // producer of one of the six SM1 screen pixel shaders, from the
        // pure-promotion checkpoint (linear_emission_sm1.cpp): M = (1,0,0,a)
        // and the three channel planes. Created once here, never at a draw;
        // the original VS stays untouched in the bracket.
        if (screen_emission_requested_ && screen_emission::admitted_pixel_shader(hash)) {
            std::vector<std::uint32_t> words;
            renderer::LinearEmissionSm1Config config{};
            config.gain = linear_emission_config_.gain; config.outputs = renderer::LinearEmissionSm1Outputs::PackedScreen;
            const auto result = renderer::linear_emission_sm1_pixel_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, config, words);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::LinearEmissionResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.screen_variant = variant;
            else release(variant);
            log("screen_emission_variant device=%llu original=%016llx transform=%u create=%08lx words=%u gain=%g outputs=packed",
                id_, hash, unsigned(result), hr, unsigned(words.size()), double(config.gain));
        }
        // Additive option: the native PS2 path with its colour lanes times G
        // (AdditiveGain), created once here; G = 1 binds the original instead.
        if (screen_additive_requested_ && screen_additive_gain_ != 1.f && screen_emission::admitted_pixel_shader(hash)) {
            std::vector<std::uint32_t> words;
            renderer::LinearEmissionSm1Config config{};
            config.gain = screen_additive_gain_; config.outputs = renderer::LinearEmissionSm1Outputs::AdditiveGain;
            const auto result = renderer::linear_emission_sm1_pixel_variant(
                reinterpret_cast<const std::uint32_t*>(code), bytes / 4, config, words);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::LinearEmissionResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.screen_additive_variant = variant;
            else release(variant);
            log("screen_emission_additive_variant device=%llu original=%016llx transform=%u create=%08lx words=%u gain=%g",
                id_, hash, unsigned(result), hr, unsigned(words.size()), double(config.gain));
        }
        entry.row = renderer::material_motion_pixel_row(hash, bytes / 4);
        if (entry.row) {
            std::vector<std::uint32_t> words;
            const auto result = renderer::material_motion_pixel_variant(reinterpret_cast<const std::uint32_t*>(code), bytes / 4, words, depth_enabled_);
            IDirect3DPixelShader9* variant = nullptr;
            HRESULT hr = E_FAIL;
            if (result == renderer::MaterialMotionResult::Applied)
                hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
            if (SUCCEEDED(hr) && variant) entry.variant = variant;
            else release(variant);
            if(sun_lane_requested_&&entry.variant&&renderer::material_motion_pixel_writes_depth(*entry.row,depth_enabled_)) {
                const HRESULT lane_hr=renderer::material_motion_invalid_sun_share(words)?native<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words.data()),&entry.sun_motion_variant):E_FAIL;
                if(FAILED(lane_hr)||!entry.sun_motion_variant){release(entry.sun_motion_variant);sun_lane_failed_=true;}
            }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
            {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
             if(sun_lane_active_&&!std::strcmp(fault,"late_shader")){release(entry.sun_motion_variant);sun_lane_failed_=true;}}
#endif
            log("motion_output_variant device=%llu kind=ps original=%016llx transform=%u create=%08lx words=%u depth=%u",
                id_, hash, unsigned(result), hr, unsigned(words.size()), renderer::material_motion_pixel_writes_depth(*entry.row, depth_enabled_));
            // Option C fill (original-shading-critique.md 1a): the same motion
            // variant with the fill block, created once here and selected in
            // bind_variant_pair for the routed reviewed pair. fill_applied=0 is
            // the fail-closed refusal of a program without a unique lobe sum:
            // no object, the plain motion variant stays bound.
            if (original_fill_requested_ && entry.variant) {
                words.clear();
                bool fill_applied = false;
                const auto filled = renderer::linear_material_original_fill_pixel_variant(
                    reinterpret_cast<const std::uint32_t*>(code), bytes / 4, original_fill_, words, depth_enabled_, fill_applied);
                IDirect3DPixelShader9* variant = nullptr;
                HRESULT fill_hr = E_FAIL;
                if (filled == renderer::LinearMaterialResult::Applied && fill_applied)
                    fill_hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &variant);
                if (SUCCEEDED(fill_hr) && variant) entry.original_fill_variant = variant;
                else release(variant);
                if (filled != renderer::LinearMaterialResult::UnsupportedShader)
                    log("original_fill_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u fill=%g fill_applied=%u",
                        id_, hash, unsigned(filled), fill_hr, unsigned(words.size()), depth_enabled_, double(original_fill_), unsigned(fill_applied));
            }
            if (linear_material_requested_ && entry.variant) {
                words.clear();
                bool fill_applied = false;
                const auto material = renderer::linear_material_pixel_variant_fill(
                    reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words, depth_enabled_, fill_applied);
                IDirect3DPixelShader9* combined = nullptr;
                HRESULT material_hr = E_FAIL;
                if (material == renderer::LinearMaterialResult::Applied)
                    material_hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &combined);
                if (SUCCEEDED(material_hr) && combined) entry.material_variant = combined;
                else release(combined);
                if(sun_lane_requested_&&entry.material_variant&&renderer::material_motion_pixel_writes_depth(*entry.row,depth_enabled_)) {
                    std::vector<std::uint32_t> lane;
                    bool extraction=false;
                    const auto extracted=renderer::linear_material_pixel_variant_sun_share(
                        reinterpret_cast<const std::uint32_t*>(code),bytes/4,linear_material_config_,lane,depth_enabled_,extraction);
                    bool ready=extracted==renderer::LinearMaterialResult::Applied;
                    if(!ready){lane.swap(words);ready=renderer::material_motion_invalid_sun_share(lane);}
                    HRESULT lane_hr=E_FAIL;
                    if(ready)lane_hr=native<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(lane.data()),&entry.sun_material_variant);
                    if(FAILED(lane_hr)||!entry.sun_material_variant){release(entry.sun_material_variant);sun_lane_failed_=true;}
                    entry.sun_extraction=entry.sun_material_variant&&extraction;
                    log("sun_shadow_lane_variant device=%llu original=%016llx extraction=%u create=%08lx words=%u",id_,hash,unsigned(entry.sun_extraction),lane_hr,unsigned(lane.size()));
                }
                // fill_applied=0 with a configured fill is the fail-closed
                // refusal of a program without a unique lobe sum.
                if (material != renderer::LinearMaterialResult::UnsupportedShader)
                    log("linear_material_variant device=%llu kind=ps original=%016llx transform=%u create=%08lx words=%u depth=%u fill_applied=%u",
                        id_, hash, unsigned(material), material_hr, unsigned(words.size()), depth_enabled_, unsigned(fill_applied));
            }
            if (linear_material_requested_) {
                words.clear();
                const auto result = renderer::linear_material_xt_default_pixel_variant(
                    reinterpret_cast<const std::uint32_t*>(code), bytes / 4, linear_material_config_, words, depth_enabled_, false);
                IDirect3DPixelShader9* repaired = nullptr;
                HRESULT hr = E_FAIL;
                if (result == renderer::LinearMaterialResult::Applied)
                    hr = native<CreatePsFn>(CreatePixelShader)(device_, reinterpret_cast<const DWORD*>(words.data()), &repaired);
                if (SUCCEEDED(hr) && repaired) entry.xt_default_ordinary_variant = repaired;
                else release(repaired);
                if(sun_lane_requested_&&entry.xt_default_ordinary_variant&&renderer::material_motion_pixel_writes_depth(*entry.row,depth_enabled_)) {
                    const HRESULT lane_hr=renderer::material_motion_invalid_sun_share(words)?native<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(words.data()),&entry.sun_xt_variant):E_FAIL;
                    if(FAILED(lane_hr)||!entry.sun_xt_variant){release(entry.sun_xt_variant);sun_lane_failed_=true;}
                }
                if (result != renderer::LinearMaterialResult::UnsupportedShader)
                    log("linear_material_xt_default_variant device=%llu kind=ps original=%016llx linear=0 transform=%u create=%08lx words=%u depth=%u",
                        id_, hash, unsigned(result), hr, unsigned(words.size()), depth_enabled_);
            }
        }
        entry.registered = true;
        if (shadow_.ps == shader) set_pixel_shader(shader);
    } catch (...) {}
}

// ---- shadow ----------------------------------------------------------------

void MotionOutput::set_vertex_shader(IDirect3DVertexShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.original_fill_pair = false; shadow_.screen_pair = false; shadow_.screen_eligible_variant = nullptr; shadow_.screen_additive_pair = false; shadow_.screen_additive_index = screen_emission::pair_count; shadow_.vs_registered = false; shadow_.vs_fade_variant = nullptr;
    shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
    shadow_.xt_default_pair = shadow_.xt_default_ready = false;
    shadow_.vs_xt_default_ordinary = shadow_.vs_xt_default_linear = nullptr;
    shadow_.vs = shader; shadow_.vs_hash = 0; shadow_.vs_variant = nullptr; shadow_.vs_material_variant = nullptr; shadow_.vs_row = nullptr; shadow_.vs_prepass = nullptr;
    if (!shader) return;
    const auto it = vertex_.find(shader);
    if (it == vertex_.end()) return;
    shadow_.vs_hash = it->second.hash;
    shadow_.vs_registered = it->second.registered;
    shadow_.vs_fade_variant = static_cast<IDirect3DVertexShader9*>(it->second.distance_fade_variant);
    shadow_.vs_variant = static_cast<IDirect3DVertexShader9*>(it->second.variant);
    shadow_.vs_row = it->second.row;
    shadow_.vs_prepass = it->second.prepass;
    shadow_.vs_material_variant = static_cast<IDirect3DVertexShader9*>(it->second.material_variant);
    shadow_.vs_xt_default_ordinary = static_cast<IDirect3DVertexShader9*>(it->second.xt_default_ordinary_variant);
    shadow_.vs_xt_default_linear = it->second.xt_default_linear_variant;
    refresh_linear_material_contract();
    refresh_linear_emission_contract();
}
void MotionOutput::set_pixel_shader(IDirect3DPixelShader9* shader) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.fade_sampler_mask = 0; shadow_.emission_pair = false; shadow_.emission_eligible_variant = nullptr; shadow_.screen_pair = false; shadow_.screen_eligible_variant = nullptr; shadow_.screen_additive_pair = false; shadow_.screen_additive_index = screen_emission::pair_count; shadow_.ps_screen_additive_variant = nullptr; shadow_.ps_registered = false; shadow_.ps_fade_variant = nullptr; shadow_.ps_emission_variant = nullptr; shadow_.ps_source_gain_variant = nullptr; shadow_.source_gain_eligible_variant = nullptr; shadow_.source_gain_pair = renderer::linear_emission_pair_count; shadow_.ps_original_fill_variant = nullptr; shadow_.original_fill_pair = false; shadow_.ps_screen_variant = nullptr;
    shadow_.material_contract = {};
    shadow_.cutout_pair = false; shadow_.asteroid_pair = false;
    shadow_.xt_default_pair = shadow_.xt_default_ready = false;
    shadow_.ps_xt_default_ordinary = nullptr;
    shadow_.ps_sun_motion=nullptr; shadow_.ps_sun_material=nullptr; shadow_.ps_sun_xt=nullptr; shadow_.ps_sun_extraction=false;
    shadow_.ps = shader; shadow_.ps_hash = 0; shadow_.ps_variant = nullptr; shadow_.ps_material_variant = nullptr;
    if (!shader) return;
    const auto it = pixel_.find(shader);
    if (it == pixel_.end()) return;
    shadow_.ps_hash = it->second.hash;
    shadow_.ps_registered = it->second.registered;
    shadow_.ps_fade_variant = static_cast<IDirect3DPixelShader9*>(it->second.distance_fade_variant);
    shadow_.ps_emission_variant = it->second.emission_variant;
    shadow_.ps_source_gain_variant = it->second.source_gain_variant;
    shadow_.ps_original_fill_variant = it->second.original_fill_variant;
    shadow_.ps_screen_variant = it->second.screen_variant;
    shadow_.ps_screen_additive_variant = it->second.screen_additive_variant;
    shadow_.ps_variant = static_cast<IDirect3DPixelShader9*>(it->second.variant);
    shadow_.ps_material_variant = static_cast<IDirect3DPixelShader9*>(it->second.material_variant);
    shadow_.ps_sun_motion=it->second.sun_motion_variant;
    shadow_.ps_sun_material=it->second.sun_material_variant;
    shadow_.ps_sun_xt=it->second.sun_xt_variant;
    shadow_.ps_sun_extraction=it->second.sun_extraction;
    shadow_.ps_xt_default_ordinary = static_cast<IDirect3DPixelShader9*>(it->second.xt_default_ordinary_variant);
    refresh_linear_material_contract();
    refresh_linear_emission_contract();
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
    // Depth replay: the world sun direction (LightDir_Dir0, shadow_replay_depth.h) as last written.
    if (depth_replay_requested_ && start <= shadow_replay::depth_sun_register && end > shadow_replay::depth_sun_register) {
        std::memcpy(depth_sun_constant_, data + (shadow_replay::depth_sun_register - start) * 4, sizeof depth_sun_constant_);
        depth_sun_written_ = true;
    }
}
void MotionOutput::set_stream_source(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride) noexcept {
    if (!enabled_ || shadow_.recording || stream) return;
    shadow_.stream0 = buffer ? resource_id(buffer) : 0;
    shadow_.stream0_identity = shadow_.stream0 ? reinterpret_cast<std::uintptr_t>(buffer) : 0;
    shadow_.stream0_offset = offset; shadow_.stream0_stride = stride;
    if (candidates_requested_) shadow_.stream0_pool = candidate_pool_of(shadow_.stream0, buffer, true);
}
void MotionOutput::set_indices(IDirect3DIndexBuffer9* buffer) noexcept {
    if (!enabled_ || shadow_.recording) return;
    shadow_.indices = buffer ? resource_id(buffer) : 0;
    shadow_.indices_identity = shadow_.indices ? reinterpret_cast<std::uintptr_t>(buffer) : 0;
    if (candidates_requested_) shadow_.indices_pool = candidate_pool_of(shadow_.indices, buffer, false);
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
    if (blend_shadow_requested() && state_hooks_) { // hooks off: the next draw reads what it needs
        // Admission reads only cached state. Refresh the consumed common state
        // even with the ordinary motion state-shadow experiment disabled.
        for (unsigned i = 0; i < 6; ++i)
            shadow_.states_known[i] = SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, shadow_states[i], &shadow_.states[i]));
        for (unsigned i = 0; i < composition_blend_count; ++i)
            shadow_.composition_blend_known[i] = SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, composition_blend_states[i], &shadow_.composition_blend[i]));
    }
    if ((composition_requested() || screen_emission_bound_) && state_hooks_)
        shadow_.fill_mode_known = SUCCEEDED(native<GetRenderStateFn>(GetRenderState)(device_, D3DRS_FILLMODE, &shadow_.fill_mode));
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
    flush_taa_invalidate_log(); // sites that fired since the last flush (Reset) carry the frame begun at the last Present
    frame_ = frame; capture_ = capture; telemetry_ = telemetry::enabled();
    packed_sample_.valid = false; packed_sample_.sampled = 0; // an unmatched pre never pairs with a later frame's post
    engine_memory::next_frame(); // the object observers' direct-read regions are re-validated once per frame
    counters_ = {}; sun_frame_={}; sun_coverage_current_=sun_composition_completed_=false;
    if (candidates_requested_) { if (depth_replay_requested_) { release_depth_leases(); depth_sun_written_ = false; } candidates_.reset(); } // a frame that never reached a scene end keeps no records or leases; the sun is per frame
    // Keep failed-lane storage until the next scene latch can compare its
    // exact dimensions/generation before transactionally replacing it. The
    // reset frame snapshot above is unavailable; no stale lane is published.
    cutout_coverage_missed_ = false; cutout_arm_active_ = cutout_arm_configured();
    composition_required_producers_ = 0;
    composition_counts_ = {}; composition_enhanced_ = false; composition_frame_stopped_ = false; composition_published_ = false;
    if (fade_witness_interval_) {
        auto& w = fade_witness_;
        w.count = w.prepared_count = w.logged = 0; w.last = FadeWitness::rect_capacity; w.overflow = false;
        std::memset(w.f_hist, 0, sizeof w.f_hist);
    }
    shimmer_count_ = 0; fade_refused_count_ = 0;
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
        else begin_composition_frame();
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
HRESULT MotionOutput::undo(MotionRoute& route) noexcept {
    HRESULT first = restore_wrap_states(route);
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
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = first; }
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
        ++counters_.restore_failures; invalidate_render_states();
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx", id_, frame_, counters_.draws, first);
        }
    }
    return first;
}

// Rollback follows chronological failure order: a material-bind undo may have
// already latched its error, then lazy bindings restore, then remaining route
// state. A later cleanup failure must never replace that first failure.
void MotionOutput::rollback_route(MotionRoute& route) noexcept {
    const HRESULT bindings_restored = restore_bindings_checked();
    if (FAILED(bindings_restored) && !motion_state_lost_) {
        motion_state_lost_ = true; motion_state_error_ = bindings_restored;
    }
    undo(route); // Latches only if no earlier restoration failed.
    if (motion_state_lost_) {
        route.submit = false; route.submission_error = motion_state_error_;
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
    }
}

// Jitter the submitted clip rows of the bound VS row by this frame's sub-pixel
// offset: rows[0] += jx_ndc * rows[3], rows[1] += jy_ndc * rows[3] with
// jx_ndc = 2 * jx_px / width and jy_ndc = -2 * jy_px / height, so the raster
// image moves by (+jx_px right, +jy_px down), the resolve's convention
// (src/temporal/README.md). The shadow keeps the application's unjittered
// rows: history records them and after_draw writes them back bit-exactly.
void MotionOutput::apply_jitter(MotionRoute& route) noexcept {
    // A pair row's VS or a depth-only prepass program: the same clip-row
    // jitter either way, so the prepass depth and the later jittered draw of
    // the same surface agree under LESSEQUAL.
    const UINT matrix_register = shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass->matrix_register;
    const std::size_t window = window_of(matrix_register);
    if (!shadow_.rows_known[window] || !main_.width || !main_.height) return;
    float rows[16];
    std::memcpy(rows, shadow_.rows[window], sizeof rows);
    fade_region::jitter_rows(rows, jitter_[0], jitter_[1], main_.width, main_.height); // shared with the region projection
    const std::uint64_t begin = draw_stamp();
    const HRESULT hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_, matrix_register, rows, 4);
    const std::uint64_t ticks = draw_stamp() - begin;
    ++counters_.jitter_writes; counters_.jitter_ticks += ticks;
    record(unsigned(telemetry::Metric::RouteJitter), ticks, FAILED(hr));
    if (SUCCEEDED(hr)) {
        route.jittered = true; route.jitter_register = matrix_register;
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
    if (motion_state_lost_) {
        route.submit = false; route.submission_error = motion_state_error_;
        ++counters_.gates[1]; invalidate_taa(TaaInvalidateSite::StateLost); return route;
    }
    if (!enabled_) { route.gate = MotionGate::Feature; ++counters_.gates[1]; return route; }
    if (!state_hooks_) begin_draw_reads(); // hooks off: this draw's state comes from Get*, read once each below
    if (hdr_state_ == HdrState::Active) hdr_dirty_ = true; // every application draw lands in the FP16 target
    if (mip_bias_total_game_writes_ != mip_bias_logged_game_writes_) log_mip_bias_game_write();
    const std::uint64_t begin = draw_stamp(), fill_before = counters_.fill_ticks, flush_before = counters_.lazy_flush_ticks;
    if (composition_state_lost_ || composition_busy_) { route.submit = false; ++composition_counts_.suppressed; if (composition_state_lost_) invalidate_taa(TaaInvalidateSite::CompositionStateLost); return route; }
    if (composition_effective_ && hdr_state_ != HdrState::Off && composition_enhanced_ && !composition_readers_known_) {
        composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionReaders);
    }
    if (composition_published_ && hdr_state_ != HdrState::Off && (composition_main_sampler_mask_ || !composition_readers_known_)) composition_export();
    route.evaluated = true;
    route.sun_color_writer=sun_lane_active_&&call.primitives&&selector_.state()==renderer::BoundaryState::Scene&&
        !counters_.hook_scene_end&&(hdr_state_==HdrState::Active||scene_bound())&&state_field(4)!=0; // slot 4: COLORWRITEENABLE
    evaluate_draw(call, route);
    // Step B rectangle before step C's admission: an admitted screen draw
    // composes inside it; without it the draw stays native.
    if (screen_emission_bound_) derive_prefix_region(call, route);
    if (!route.routed) {
        const HRESULT restored = restore_bindings_checked();
        if (FAILED(restored)) {
            // An unavailable pair still forwards only after known restoration,
            // independently of the optional emission route.
            if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = restored; }
            route.submit = false; route.submission_error = motion_state_error_;
            if (composition_effective_) { composition_state_lost_ = true; ++composition_counts_.suppressed; }
            invalidate_taa(TaaInvalidateSite::RestoreFailed);
        } else if (route.submit) {
            prepare_composition(call, route);
            // Additive option: its pair check is the only per-draw cost for
            // any other draw; never on a draw the bracket already took.
            if (shadow_.screen_additive_pair && screen_additive_enabled_ && route.submit && !route.composition) prepare_screen_additive(call, route);
        }
    }
    // Source-only gain: a null pointer test when the option is off or the
    // bound pair is not one of the twenty; the bracket routes take precedence.
    if (shadow_.source_gain_eligible_variant && source_gain_enabled_
            && !route.routed && !route.composition && route.submit) prepare_source_gain(call, route);
    if (!route.routed && !route.composition && route.submit && route.scene && call.primitives)
        mark_cutout_candidate(route);
    if (telemetry::draw_enabled()) {
        const std::uint64_t total = draw_stamp() - begin,
            excluded = route.ticks + (counters_.fill_ticks - fill_before) + (counters_.lazy_flush_ticks - flush_before);
        const std::uint64_t gate = total > excluded ? total - excluded : 0;
        counters_.gate_ticks += gate;
        record(unsigned(telemetry::Metric::RouteGate), gate);
    }
    return route;
}
// Canonical COM identity is queried only at changed setters and resync/latch,
// within the caller's native CPU section, never while evaluating a draw.
int MotionOutput::composition_texture_reader(DWORD stage, IDirect3DBaseTexture9* texture) noexcept {
    if (!composition_requested()) return 2;
    const unsigned i = stage < 16 ? unsigned(stage) : stage >= D3DVERTEXTEXTURESAMPLER0 && stage <= D3DVERTEXTEXTURESAMPLER3 ? 16u + stage - D3DVERTEXTEXTURESAMPLER0 : stage == D3DDMAPSAMPLER ? 20u : 21u;
    if (i >= 21) return 2;
    if (texture == composition_textures_[i] && (composition_reader_known_mask_ & (1u << i))) return 2;
    if (!texture) return 0;
    if (!composition_identity_known_) return -1;
    if (!composition_main_identity_) return 0;
    IUnknown* identity = nullptr;
    const HRESULT hr = texture->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
    const int result = FAILED(hr) || !identity ? -1 : identity == composition_main_identity_ ? 1 : 0;
    release(identity);
    return result;
}
void MotionOutput::before_texture_write(IDirect3DBaseTexture9* texture) noexcept {
    if (!composition_requested() || !texture || !hdr_main_ || hdr_state_ == HdrState::Off) return;
    if (texture == composition_main_texture_) { before_render_target_write(hdr_main_); return; }
    // A back-buffer-only main has no texture destination. Unknown container
    // identity is different: finish the redirect before the incoming write.
    if (composition_identity_known_ && !composition_main_identity_) return;
    const bool busy = composition_busy_; composition_busy_ = true;
    IUnknown* identity = nullptr;
    const HRESULT hr = texture->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
    const bool unknown = FAILED(hr) || !identity || !composition_identity_known_;
    const bool main = !unknown && identity == composition_main_identity_;
    release(identity); composition_busy_ = busy;
    if (main || unknown) before_render_target_write(hdr_main_);
}

void MotionOutput::release_composition_identity() noexcept {
    const bool busy = composition_busy_; composition_busy_ = true;
    composition_main_identity_ = nullptr; composition_main_sampler_mask_ = 0;
    release(composition_main_texture_); // App-owned alias: excluded from device-object inventory.
    composition_busy_ = busy;
}
void MotionOutput::composition_export() noexcept {
    if (!composition_requested() || !composition_enhanced_ || composition_quarantined_) return;
    composition_quarantined_ = true; composition_frame_stopped_ = true;
    ++composition_counts_.exports; invalidate_taa(TaaInvalidateSite::CompositionExport);
}
void MotionOutput::begin_composition_frame() noexcept {
    if (!composition_requested()) return;
    composition_frame_stopped_ = true;
    if (composition_quarantined_ || composition_state_lost_ || motion_state_lost_ || !taa_enabled_
        || !hdr_enabled_ || !hdr_ || !hdr_->tonemap_active()
        || hdr_config_.tonemap != renderer::HdrTonemap::Agx || hdr_config_.decode != x3::temporal::AgxDecode::gamma22) return;
    composition_busy_ = true;
    // Producer bits 1|2 name the required coverage; bit 4 asks for the in-place
    // fade bracket beside the exchange fade (policy 2 stays the fallback when the
    // device lacks D3DPRASTERCAPS_SCISSORTEST: the pass then reports 4 unsupported).
    const unsigned producers = (linear_emission_requested_ ? 1u : 0u) | (distance_fade_requested_ ? 2u : 0u);
    // Bit 8 asks for the packed screen bracket (step C); it never joins the
    // required producers, so a device without its caps refuses screen draws
    // to native without stopping any frame.
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    const bool screen_policy = screen_emission_requested_ && !fixture_screen_caps_fault_;
#else
    const bool screen_policy = screen_emission_requested_;
#endif
    const unsigned requested = producers | (distance_fade_requested_ ? 4u : 0u) | (screen_policy ? 8u : 0u);
    if (composition_adapter_format_ == D3DFMT_UNKNOWN) {
        // An unanswered capability query is not proof of immutable exclusion.
        // Retry only at the frame latch; this frame must not seed ordinary TAA
        // while its requested producer set remains unresolved.
        D3DDISPLAYMODE display{};
        const HRESULT hr = native<GetDisplayModeFn>(GetDisplayMode)(device_, 0, &display);
        if (FAILED(hr) || display.Format == D3DFMT_UNKNOWN) {
            const HRESULT failure = FAILED(hr) ? hr : D3DERR_INVALIDCALL;
            if (failure != composition_attach_result_)
                log("linear_composition_device device=%llu result=%08lx requested=%u supported=0 available=0 reason=adapter_query", id_, failure, requested);
            composition_attach_result_ = failure;
            composition_required_producers_ = producers;
            composition_busy_ = false;
            invalidate_taa(TaaInvalidateSite::CompositionAttach);
            return;
        }
        composition_adapter_format_ = display.Format;
    }
    const auto depth_format = static_cast<D3DFORMAT>(pending_.depth.format);
    const bool retry = composition_attach_result_ == E_OUTOFMEMORY
        || (composition_ && composition_->caps().supported_policies
            && composition_->caps().available_policies != composition_->caps().supported_policies);
    if (!composition_attach_attempted_ || depth_format != composition_depth_format_ || retry || !composition_) {
        const bool first = !composition_attach_attempted_;
        composition_depth_format_ = depth_format; composition_attach_attempted_ = true;
        try { if (!composition_) composition_ = std::make_unique<renderer::LinearEmissionPass>(); } catch (...) {}
        if ((requested & 2u) && !fade_bounds_.reserved() && !fade_bounds_.reserve())
            log("fade_region_table device=%llu reserve=failed", id_);
        if (composition_) {
            // Step E: the packed composite carries the gain as a literal; a
            // refused value (out of the pass's domain) leaves the default 1.
            if ((requested & 8u) && !composition_->configure_packed_gain(screen_emission_gain_))
                log("screen_emission_gain device=%llu requested=%g refused=1 applied=%g", id_, double(screen_emission_gain_), double(composition_->packed_gain()));
            const HRESULT hr = composition_->attach(device_, native_, caps_, composition_adapter_format_, depth_format, requested);
            if (first || hr != composition_attach_result_)
                log("linear_composition_device device=%llu result=%08lx requested=%u supported=%u available=%u reason=%s depth_format=%u", id_, hr,
                    requested, composition_->caps().supported_policies, composition_->caps().available_policies,
                    composition_->caps().reason, unsigned(depth_format));
            composition_attach_result_ = hr;
        }
    }
    // Capability support fixes the frame producer set. Allocation/program
    // failures cannot silently remove a source class from required coverage.
    composition_required_producers_ = (composition_ ? composition_->caps().supported_policies : producers) & 3u;
    if (!composition_required_producers_ && composition_attach_result_ == E_OUTOFMEMORY)
        composition_required_producers_ = producers; // Impl allocation failed before caps could be retained.
    composition_effective_ = composition_effective_ || (composition_ && composition_->caps().supported_policies != 0);
    if (!composition_ || !composition_->caps().enabled ||
        (composition_->caps().available_policies & composition_required_producers_) != composition_required_producers_) {
        composition_busy_ = false; if (composition_effective_ || composition_required_producers_) invalidate_taa(TaaInvalidateSite::CompositionAttach); return;
    }
    release(composition_main_texture_); composition_main_identity_ = nullptr; composition_identity_known_ = true;
    const HRESULT container = hdr_main_ ? hdr_main_->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&composition_main_texture_)) : E_FAIL;
    if (SUCCEEDED(container) && composition_main_texture_) {
        IUnknown* identity = nullptr;
        composition_identity_known_ = SUCCEEDED(composition_main_texture_->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity))) && identity;
        if (composition_identity_known_) composition_main_identity_ = identity;
        release(identity);
    } else {
        composition_identity_known_ = container == E_NOINTERFACE;
        release(composition_main_texture_);
    }
    const HRESULT restored = restore_bindings_checked();
    if (SUCCEEDED(restored)) resync_samplers(); // Includes prebound pixel/vertex readers, once at latch.
    if (FAILED(restored)) { composition_state_lost_ = true; composition_frame_stopped_ = true; }
    else if (FAILED(composition_->ensure_targets(hdr_->width(), hdr_->height()))) composition_frame_stopped_ = true;
    else {
        const auto begin = composition_->begin_frame(frame_);
        composition_frame_stopped_ = !begin.ready;
        composition_state_lost_ = !begin.state_preserved;
    }
    composition_busy_ = false;
    if (composition_frame_stopped_) invalidate_taa(TaaInvalidateSite::CompositionBegin);
}
void MotionOutput::prepare_composition(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (!composition_requested()) return;
    // A fade pair whose blend state is known off is an ordinary opaque draw
    // (the station hull pair is mostly drawn opaque): it never enters the
    // nine-state check, so an unknown other state cannot stop the frame. A
    // blended or unknown-blend draw is checked as before (fail closed).
    const bool fade_pair = shadow_.fade_sampler_mask != 0 && !(state_known(3) && !shadow_.states[3]);
    bool fade = false;
    if (fade_pair) {
        bool known = true;
        for (unsigned i = 0; i < 6; ++i) known = state_known(i) && known;
        for (unsigned i = 0; i < 3; ++i) known = blend_known(i) && known; // the blend triple only
        if (!known) {
            if (composition_required_producers_ & 2u) { composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionRefused); }
            ++composition_counts_.refused; ++composition_counts_.refusal[2]; return;
        }
        fade = shadow_.states[0] == D3DZB_TRUE && !shadow_.states[1] && !shadow_.states[2]
            && shadow_.states[3] && shadow_.states[4] == 7 && !shadow_.states[5]
            && shadow_.composition_blend[0] == D3DBLEND_SRCALPHA
            && shadow_.composition_blend[1] == D3DBLEND_INVSRCALPHA && shadow_.composition_blend[2] == D3DBLENDOP_ADD;
    }
    // Step C (screen-emission-region.md, section 4): an exact SM1 screen pair
    // in the native screen state (ALPHABLENDENABLE, ADD, ONE/INVSRCCOLOR,
    // mask 15, separate alpha off, Z-write off, sRGB write off, dither off;
    // alpha test and Z test any). An unknown state refuses as readiness, a
    // different state is an ordinary (pair) refusal; policy 8 is not a
    // required producer, so no screen refusal ever stops the frame.
    bool screen = false;
    if (shadow_.screen_pair && !fade) {
        bool known = true;
        for (unsigned i : {1u, 3u, 4u, 5u}) known = state_known(i) && known;
        for (unsigned i = 0; i < 4; ++i) known = blend_known(i) && known;
        DWORD dither = 0; // shadowed lazily like the cutout states (one Get, then the shadow)
        if (!known || FAILED(render_state(D3DRS_DITHERENABLE, &dither))) { ++composition_counts_.refused; ++composition_counts_.refusal[2]; return; }
        screen = shadow_.states[3] && !shadow_.states[1] && shadow_.states[4] == 15 && !shadow_.states[5] && !dither
            && shadow_.composition_blend[0] == D3DBLEND_ONE && shadow_.composition_blend[1] == D3DBLEND_INVSRCCOLOR
            && shadow_.composition_blend[2] == D3DBLENDOP_ADD && !shadow_.composition_blend[3];
    }
    const bool emitter = shadow_.emission_pair;
    if (!fade && !emitter && !screen) { ++composition_counts_.refused; ++composition_counts_.refusal[0]; return; }
    if (fade) ++composition_counts_.eligible_fade;
    if (screen) {
        ++composition_counts_.packed_eligible;
        // The bound first: a screen draw without a locked-prefix rectangle
        // (first draw after creation, scan refused, indexed/instanced/other
        // layout, scalar-fade body) stays native, never the full viewport;
        // so does one whose device lacks policy 8. Neither is a refusal of
        // the histogram: the native draw is the status quo of every bullet.
        if (!route.prefix_evaluated || !route.prefix_region.bound) { ++composition_counts_.packed_unbounded_refused; return; }
        if (!composition_ || !composition_->caps().supports(renderer::LinearCompositionPolicy::PackedScreenInPlace)) { ++composition_counts_.packed_caps_refused; return; }
    }
    const unsigned producer = fade ? 2u : screen ? 8u : 1u;
    // Fade composes in place (section 3 of linear-distance-fade-region.md) when
    // the pass offers policy 4; otherwise the exchange policy 2 remains the
    // route. Emission keeps its exchange bracket in either case. The packed
    // screen bracket is in place only (policy 8; no exchange, no full viewport).
    const bool in_place = screen || (fade && composition_ && composition_->caps().supports(renderer::LinearCompositionPolicy::DistanceFadeInPlace));
    const auto policy = screen ? renderer::LinearCompositionPolicy::PackedScreenInPlace
        : in_place ? renderer::LinearCompositionPolicy::DistanceFadeInPlace
        : fade ? renderer::LinearCompositionPolicy::DistanceFade : renderer::LinearCompositionPolicy::AdditiveEmission;
    const bool required = (composition_required_producers_ & producer) != 0;
    unsigned refusal = 6;
    // The bullet screen draws are non-indexed (DrawPrimitive from StartVertex
    // 0, the bound's contract); the fade/emission sources are indexed DIPs.
    if (!call.composition_permission || (screen ? call.indexed : !call.indexed) || call.user_memory || !call.primitives
        || !scene_open_ || active_queries_ || shadow_.recording || !scene_bound() || main_msaa_) refusal = 1;
    else if (!taa_enabled_ || !hdr_enabled_ || hdr_state_ != HdrState::Active || !hdr_ || !hdr_->tonemap_active()
        || hdr_config_.decode != x3::temporal::AgxDecode::gamma22 || hdr_config_.tonemap != renderer::HdrTonemap::Agx
        || !composition_ || !composition_->caps().supports(policy) || composition_busy_
        || (fade ? (!shadow_.vs_fade_variant || !shadow_.ps_fade_variant) : screen ? !shadow_.screen_eligible_variant : !shadow_.emission_eligible_variant)) refusal = 2;
    else if (!composition_readers_known_ || composition_main_sampler_mask_) refusal = 3;
    else if (composition_frame_stopped_ || composition_quarantined_) refusal = 4;
    if (refusal == 6 && fade) {
        // Every sampler the mask names (Asteroid s0-s3, hull BUMPMAP s0-s4).
        for (unsigned stage = 0; stage < 8; ++stage)
            if ((shadow_.fade_sampler_mask & (1u << stage)) && (!sampler_srgb_known(stage) || samplers_[stage].srgb)) refusal = 2;
        const auto* row = shadow_.vs_row;
        if (!row || (row->light_loop_bound_required && (!shadow_.integer0_known || shadow_.integer0[0] < 0 || shadow_.integer0[0] > int(row->light_loop_max_count)))) refusal = 2;
    }
    if (refusal == 6 && screen) {
        // The diffuse sampler must not decode sRGB (the promoted PS samples
        // it as the original does) and stage 0 must not project its
        // coordinates: the SM1 originals leave W undefined under PROJECTED
        // (linear-emission-sm1.md). One documented Get per bounded candidate.
        DWORD flags = 0;
        if (!sampler_srgb_known(0) || samplers_[0].srgb
            || FAILED(native<GetStageFn>(GetTextureStageState)(device_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, &flags))
            || (flags & D3DTTFF_PROJECTED)) refusal = 2;
    }
    if (refusal != 6) {
        ++composition_counts_.refused; ++composition_counts_.refusal[refusal];
        // Only missing required scene-source coverage poisons this frame.
        // Unrelated draws and permanently unsupported policies stay native.
        if (required && route.scene) { composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionRefused); }
        if (fade && capture_) record_fade_refused(route, refusal);
        return;
    }
    if (fade) derive_fade_region(route);
    if (screen && witness_frame()) {
        // The packed rectangle joins the witness union exactly as a fade
        // rectangle does (derive_fade_region records those); prepared below.
        auto& w = fade_witness_;
        w.last = FadeWitness::rect_capacity;
        if (w.count < FadeWitness::rect_capacity) { w.last = w.count; w.rects[w.count] = route.prefix_region.rect; w.prepared[w.count] = false; }
        else w.overflow = true;
        ++w.count;
        const unsigned permille = route.prefix_region_permille;
        const unsigned bucket = permille <= 10 ? 0u : permille <= 20 ? 1u : permille <= 50 ? 2u : permille <= 100 ? 3u
            : permille <= 250 ? 4u : permille <= 500 ? 5u : permille < 1000 ? 6u : 7u;
        ++w.f_hist[bucket];
        const bool witness_line = w.logged < FadeWitness::line_budget;
        if (witness_line) ++w.logged;
        if (capture_ || witness_line) {
            const auto& r = route.prefix_region.rect;
            log("packed_region device=%llu frame=%llu index=%lu vs=%016llx ps=%016llx vb=%llu rect=%ld,%ld,%ld,%ld f_permille=%u",
                id_, frame_, static_cast<unsigned long>(counters_.draws), shadow_.vs_hash, shadow_.ps_hash, shadow_.stream0,
                long(r.left), long(r.top), long(r.right), long(r.bottom), permille);
        }
    }
    composition_busy_ = true;
    renderer::LinearEmissionBoundary boundary{hdr_->target(), fade ? shadow_.ps_fade_variant : screen ? shadow_.screen_eligible_variant : shadow_.emission_eligible_variant, frame_, true};
    boundary.policy = policy;
    boundary.augmented_vertex = fade ? shadow_.vs_fade_variant : nullptr; // packed: the VS stays the application's
    if (in_place) {
        // derive_fade_region always yields a rectangle: the box projection or,
        // on every doubt, the full viewport (whole target while the viewport is
        // unknown), already clipped to the owning target. The pass intersects
        // it further with the saved viewport and an enabled application scissor.
        // The packed rectangle is the bound locked-prefix projection (never the
        // full viewport: an unbounded draw was refused above).
        const auto& r = screen ? route.prefix_region.rect : route.fade_region.rect;
        boundary.region = RECT{r.left, r.top, r.right, r.bottom};
        boundary.region_known = true;
    }
    const auto prepared = composition_->prepare(boundary);
    composition_counts_.prepare = FAILED(prepared.saved) ? prepared.saved : prepared.operation;
    composition_counts_.prepare_restore = prepared.restore;
    if (prepared.ready) {
        route.composition = true; route.composition_policy = policy;
        ++composition_counts_.prepared;
        if (fade) ++composition_counts_.prepared_fade;
        if (screen) { ++composition_counts_.packed_admitted; if (capture_) sample_packed_pre(route); }
        if ((fade || screen) && witness_frame() && fade_witness_.last < FadeWitness::rect_capacity) {
            fade_witness_.prepared[fade_witness_.last] = true; ++fade_witness_.prepared_count;
        }
        if (in_place) ++composition_counts_.in_place; // traffic and region pixels are added at finish from the pass's rectangle
        else composition_counts_.pool_traffic_bytes += std::uint64_t(hdr_->width()) * hdr_->height() * 56u;
        return;
    }
    composition_busy_ = false; ++composition_counts_.refused; ++composition_counts_.refusal[5]; ++composition_counts_.prepare_failures;
    if (fade && capture_) record_fade_refused(route, 5);
    if (required) { composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionPrepare); }
    if (!prepared.state_preserved) {
        composition_state_lost_ = true; composition_frame_stopped_ = true; route.submit = false;
        route.submission_error = FAILED(prepared.saved) ? prepared.saved : FAILED(prepared.operation) ? prepared.operation : FAILED(prepared.restore) ? prepared.restore : D3DERR_INVALIDCALL;
        ++composition_counts_.suppressed; invalidate_taa(TaaInvalidateSite::CompositionStateLost);
    }
}
// Additive option (screen-emission-region.md, "Additive option"): an exact
// screen pair in the exact native screen state (the packed route's state
// set: ALPHABLENDENABLE, ADD, ONE/INVSRCCOLOR, mask 15, separate alpha off,
// Z-write off, sRGB write off; alpha test and Z test any) draws in place
// into the FP16 target with DESTBLEND ONE, so D = G q + D instead of the
// saturating q + (1 - q) D. The PROJECTED stage-0 refusal of the packed route
// stays (the PS2 variant does not project). Unknown state refuses; nothing
// here stops a frame or touches the composition counters.
void MotionOutput::prepare_screen_additive(const MotionDrawCall& call, MotionRoute& route) noexcept {
    // Refusal reasons, each logged once per device (the counters are the
    // fixture's key 61 in total; the reason names are the log's vocabulary).
    static constexpr const char* reasons[] = {"state", "draw_shape", "scene", "no_fp16_target", "no_variant", "srgb_sampler", "projected", "dither",
        "alpha_state", "alpha_caps"};
    constexpr unsigned reason_count = sizeof reasons / sizeof reasons[0];
    const auto refuse = [&](unsigned reason) {
        ++screen_additive_refused_; ++screen_additive_frame_refused_;
        if (reason < reason_count && !(screen_additive_refusal_logged_ & (1u << reason))) {
            screen_additive_refusal_logged_ |= 1u << reason;
            log("screen_emission_additive_refused device=%llu frame=%llu index=%lu reason=%s", id_, frame_, counters_.draws, reasons[reason]);
        }
    };
    bool known = true;
    for (unsigned i : {1u, 3u, 4u, 5u}) known = state_known(i) && known;
    for (unsigned i = 0; i < 4; ++i) known = blend_known(i) && known;
    DWORD dither = 0; // shadowed lazily like the packed route (one Get, then the shadow)
    if (!known || FAILED(render_state(D3DRS_DITHERENABLE, &dither))) { refuse(0); return; }
    if (!(shadow_.states[3] && !shadow_.states[1] && shadow_.states[4] == 15 && !shadow_.states[5]
        && shadow_.composition_blend[0] == D3DBLEND_ONE && shadow_.composition_blend[1] == D3DBLEND_INVSRCCOLOR
        && shadow_.composition_blend[2] == D3DBLENDOP_ADD && !shadow_.composition_blend[3])) { refuse(0); return; }
    if (dither) { refuse(7); return; }
    // The packed route's draw-shape and scene guards (non-indexed from device
    // memory, an open bound scene, no active query, no recording block, no MSAA).
    if (!call.primitives || call.indexed || call.user_memory) { refuse(1); return; }
    if (!scene_open_ || active_queries_ || shadow_.recording || !scene_bound() || main_msaa_) { refuse(2); return; }
    if (hdr_state_ != HdrState::Active || !hdr_) { refuse(3); return; }
    const bool gained = screen_additive_gain_ != 1.f;
    if (gained && !shadow_.ps_screen_additive_variant) { refuse(4); return; }
    // The PS2 promotion samples the diffuse texture itself: the packed route's
    // sRGB-sampler refusal applies (unknown or decoding sampler 0 refuses).
    if (!sampler_srgb_known(0) || samplers_[0].srgb) { refuse(5); return; }
    DWORD flags = 0;
    if (FAILED(native<GetStageFn>(GetTextureStageState)(device_, 0, D3DTSS_TEXTURETRANSFORMFLAGS, &flags))
        || (flags & D3DTTFF_PROJECTED)) { refuse(6); return; }
    // Per-source bloom attenuation (bloom-per-source-attenuation.md, option 1):
    // the alpha law becomes k*a + D.a for this draw only. Everything it needs
    // is checked before the first setter, so a refusal leaves the draw native.
    // The separate alpha triple and BLENDFACTOR must be shadowed: they are the
    // values restored after the draw, and an unknown one cannot be put back.
    if (screen_additive_alpha_requested_) {
        bool alpha_known = true;
        for (unsigned i = 4; i < composition_blend_count; ++i) alpha_known = blend_known(i) && alpha_known;
        if (!alpha_known) { refuse(8); return; }
        if (!(caps_.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND)) { refuse(9); return; }
        if (screen_additive_alpha_constant_ && !(caps_.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR)) { refuse(9); return; }
    }
    HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_ONE);
    if (FAILED(hr)) { ++screen_additive_failures_; return; } // nothing applied: the draw stays native
    route.screen_additive = true;
    if (gained) {
        hr = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps_screen_additive_variant);
        if (SUCCEEDED(hr)) route.screen_additive_ps = true;
        else {
            // Roll the first step back; a failed rollback leaves the device
            // state unknown exactly as a failed route undo does.
            ++screen_additive_failures_;
            const HRESULT back = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
            route.screen_additive = false;
            if (FAILED(back)) {
                if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = back; }
                ++counters_.restore_failures; invalidate_render_states(); invalidate_taa(TaaInvalidateSite::RestoreFailed);
            }
            return;
        }
    }
    if (screen_additive_alpha_requested_) {
        const HRESULT alpha = apply_screen_additive_alpha();
        if (FAILED(alpha)) {
            // Unwind this draw completely: the alpha states applied so far,
            // then the program and DESTBLEND, exactly like the PS rollback.
            ++screen_additive_failures_;
            const HRESULT alpha_back = restore_screen_additive_alpha();
            HRESULT back = route.screen_additive_ps ? native<SetPsFn>(SetPixelShader)(device_, shadow_.ps) : S_OK;
            const HRESULT destination = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
            if (SUCCEEDED(back)) back = destination;
            if (FAILED(alpha_back) && SUCCEEDED(back)) back = alpha_back;
            route.screen_additive = route.screen_additive_ps = false;
            if (FAILED(back)) {
                if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = back; }
                ++counters_.restore_failures; invalidate_render_states(); invalidate_taa(TaaInvalidateSite::RestoreFailed);
            }
            return;
        }
        route.screen_additive_alpha = true;
    }
    ++screen_additive_admitted_; ++screen_additive_frame_admitted_;
    if (shadow_.screen_additive_index < screen_emission::pair_count)
        screen_additive_frame_pairs_ |= 1u << shadow_.screen_additive_index;
}
// The attenuation's render states in apply order. SEPARATEALPHABLENDENABLE is
// last so the alpha triple is already in place when it starts to matter, and
// first on the way back so the triple is inert again before it is restored.
// BLENDFACTOR is only touched when k is strictly between 0 and 1.
namespace {
unsigned screen_additive_alpha_steps(float alpha, bool constant, DWORD factor,
    D3DRENDERSTATETYPE* states, DWORD* values) noexcept {
    unsigned count = 0;
    if (constant) { states[count] = D3DRS_BLENDFACTOR; values[count++] = factor; }
    states[count] = D3DRS_SRCBLENDALPHA;
    values[count++] = alpha == 0.f ? D3DBLEND_ZERO : constant ? D3DBLEND_BLENDFACTOR : D3DBLEND_ONE;
    states[count] = D3DRS_DESTBLENDALPHA; values[count++] = D3DBLEND_ONE;
    states[count] = D3DRS_BLENDOPALPHA; values[count++] = D3DBLENDOP_ADD;
    states[count] = D3DRS_SEPARATEALPHABLENDENABLE; values[count++] = TRUE;
    return count;
}
} // namespace
HRESULT MotionOutput::apply_screen_additive_alpha() noexcept {
    D3DRENDERSTATETYPE states[5]{}; DWORD values[5]{};
    const unsigned count = screen_additive_alpha_steps(screen_additive_alpha_, screen_additive_alpha_constant_,
        screen_additive_alpha_factor_, states, values);
    screen_additive_alpha_applied_ = 0;
    for (unsigned i = 0; i < count; ++i) {
        const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, states[i], values[i]);
        if (FAILED(hr)) return hr; // the caller unwinds exactly what was applied
        ++screen_additive_alpha_applied_;
    }
    return S_OK;
}
// Back to the application's own values (composition_blend[3..7]: the setter
// hook's shadow with the hooks on, this draw's Get* cache with them off); the
// admission refused unless every one of them was known. Only the states this draw actually applied are written.
HRESULT MotionOutput::restore_screen_additive_alpha() noexcept {
    D3DRENDERSTATETYPE states[5]{}; DWORD values[5]{};
    screen_additive_alpha_steps(screen_additive_alpha_, screen_additive_alpha_constant_,
        screen_additive_alpha_factor_, states, values);
    HRESULT first = S_OK;
    for (unsigned i = screen_additive_alpha_applied_; i; --i) {
        const unsigned index = composition_blend_index(states[i - 1]);
        const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, states[i - 1], shadow_.composition_blend[index]);
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    }
    screen_additive_alpha_applied_ = 0;
    return first;
}
void MotionOutput::finish_screen_additive(MotionRoute& route) noexcept {
    HRESULT first = S_OK;
    if (route.screen_additive_alpha) { const HRESULT hr = restore_screen_additive_alpha(); if (FAILED(hr)) first = hr; route.screen_additive_alpha = false; }
    if (route.screen_additive_ps) { const HRESULT hr = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps); if (FAILED(hr)) first = hr; }
    const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR); // the admitted value
    if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    route.screen_additive = route.screen_additive_ps = false;
    if (FAILED(first)) {
        ++screen_additive_failures_;
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = first; }
        ++counters_.restore_failures; invalidate_render_states(); invalidate_taa(TaaInvalidateSite::RestoreFailed);
    }
}
bool MotionOutput::publish_composition() noexcept {
    auto** slot = composition_->owning_candidate();
    if (!slot) return false;
    HRESULT exchange;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_emission_exchange_fault_) { --fixture_emission_exchange_fault_; exchange = E_FAIL; }
    else
#endif
    exchange = hdr_->exchange_target(*slot);
    const HRESULT ack = composition_->acknowledge_exchange(SUCCEEDED(exchange));
    composition_counts_.exchange = exchange; composition_counts_.ack = ack;
    if (FAILED(exchange)) ++composition_counts_.exchange_failures;
    if (FAILED(ack)) ++composition_counts_.ack_failures;
    if (FAILED(exchange)) return false;
    if (FAILED(ack)) { composition_state_lost_ = true; return false; }
    // The pass has already restored bindings around this exact owning target.
    hdr_target_ = describe_surface(hdr_->target()); hdr_dirty_ = true; hdr_resolved_ = nullptr;
    ++composition_counts_.exchanged;
    if (!hdr_target_.known) { composition_state_lost_ = true; return false; }
    return true;
}
void MotionOutput::finish_composition(HRESULT source, renderer::LinearCompositionPolicy policy) noexcept {
    sun_composition_completed_=false;
    composition_counts_.source = source;
    auto completed = composition_->finish(source);
    composition_counts_.composition = completed.composition; composition_counts_.restore = completed.restore;
    if (FAILED(completed.composition)) ++composition_counts_.composition_failures;
    if (FAILED(completed.restore)) ++composition_counts_.restore_failures;
    if (policy == renderer::LinearCompositionPolicy::DistanceFadeInPlace || policy == renderer::LinearCompositionPolicy::PackedScreenInPlace) {
        // Policy 8 takes the same branch: the packed composite lands in A|R,
        // the failure ladder is the fade one (screen-emission-region.md,
        // section 2), 112 bytes of pool traffic per region pixel.
        const bool packed = policy == renderer::LinearCompositionPolicy::PackedScreenInPlace;
        // No candidate, exchange or acknowledgement: finish() composed A|R in
        // place from B|R and E|R and returned the pass to idle. Linear means A
        // holds the composed rectangle and the frame's M stays valid. Anything
        // else is Incomplete: the pass recovered A|R exactly from B|R (the
        // object is absent from the frame, recovery S_OK) or could not
        // (partial source writes remain, recovery failed); both stop the frame,
        // select the Unavailable reactive policy with a null mask and drop the
        // history, never a native publication. A failed restore leaves the
        // device state unknown, as a failed exchange acknowledgement does.
        const auto& r = completed.region;
        const std::uint64_t pixels = r.right > r.left && r.bottom > r.top
            ? std::uint64_t(r.right - r.left) * std::uint64_t(r.bottom - r.top) : 0u;
        composition_counts_.region_pixels += pixels;
        composition_counts_.pool_traffic_bytes += pixels * (packed ? 112u : 48u);
        if (packed) composition_counts_.packed_region_pixels += pixels;
        if (packed && capture_) sample_packed_post(r);
        composition_counts_.recovery = completed.recovery;
        if (FAILED(completed.recovery)) ++composition_counts_.recovery_failures;
        if (FAILED(completed.restore)) composition_state_lost_ = true;
        composition_busy_ = false;
        if (FAILED(source) || completed.image != renderer::LinearEmissionImage::Linear || composition_state_lost_ || !composition_->coverage_valid()) {
            ++composition_counts_.incomplete; ++composition_counts_.in_place_incomplete;
            if (packed) ++composition_counts_.packed_incomplete;
            composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionIncomplete);
        } else {
            ++composition_counts_.linear; ++composition_counts_.in_place_linear;
            if (packed) ++composition_counts_.packed_linear; else ++composition_counts_.linear_fade;
            composition_enhanced_ = true;
            sun_coverage_current_=sun_composition_completed_=true;
        }
        return;
    }
    bool published = publish_composition();
    if (!published && !composition_state_lost_) {
        completed = composition_->recover_native();
        composition_counts_.restore = completed.restore;
        if (FAILED(completed.restore)) ++composition_counts_.restore_failures;
        published = publish_composition();
    }
    composition_busy_ = false;
    if (!published) composition_state_lost_ = true;
    if (FAILED(source) || completed.image == renderer::LinearEmissionImage::Incomplete || !published || !composition_->coverage_valid()) {
        ++composition_counts_.incomplete; composition_frame_stopped_ = true; invalidate_taa(TaaInvalidateSite::CompositionIncomplete);
    } else if (completed.image == renderer::LinearEmissionImage::Linear) {
        sun_coverage_current_=sun_composition_completed_=true;
        ++composition_counts_.linear; composition_enhanced_ = true;
        if (policy == renderer::LinearCompositionPolicy::DistanceFade) ++composition_counts_.linear_fade;
    } else {++composition_counts_.native;sun_coverage_current_=sun_composition_completed_=true;}
}
void MotionOutput::refresh_linear_material_contract() noexcept {
    // Pair identity and complete corrected availability are computed only
    // at binding/registration. No stage-wide DEFAULT substitution is safe.
    shadow_.xt_default_pair = linear_material_requested_
        && renderer::linear_material_xt_default_pair(shadow_.vs_hash, shadow_.ps_hash);
    shadow_.xt_default_ready = shadow_.xt_default_pair && shadow_.vs_registered && shadow_.ps_registered
        && shadow_.vs_xt_default_ordinary
        && shadow_.vs_xt_default_linear && shadow_.ps_xt_default_ordinary && shadow_.ps_material_variant;
    const auto contract = linear_material_requested_ && shadow_.vs_variant && shadow_.ps_variant
        && (!shadow_.xt_default_pair || shadow_.xt_default_ready)
        ? renderer::linear_material_pair_contract(shadow_.vs_hash, shadow_.ps_hash) : renderer::LinearMaterialPairContract{};
    shadow_.material_contract = contract;
    // Original fill: the draws the linear route would convert (the reviewed
    // exact pairs) with both ordinary motion variants; the draw-time gates
    // (opaque state, scene, FP16 target) are the ordinary route's.
    shadow_.original_fill_pair = original_fill_requested_ && shadow_.ps_original_fill_variant && shadow_.vs_variant && shadow_.ps_variant
        && shadow_.vs_registered && shadow_.ps_registered && renderer::linear_material_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash);
    shadow_.cutout_pair = linear_material_requested_ && cutout::pair(shadow_.vs_hash, shadow_.ps_hash);
    // Diagnostic only, and only while the trace is on: integer table lookup at
    // the shader setter, never at a draw.
    shadow_.asteroid_pair = shimmer_trace_ && renderer::linear_material_asteroid_pair(shadow_.vs_hash, shadow_.ps_hash);
    if (shadow_.xt_default_pair && !shadow_.xt_default_ready && !xt_default_unavailable_.seen) {
        // Called by lightweight shader hooks: even integer-only printf formats
        // can reach the CRT's x87 formatter. Keep this path integer-only.
        auto& event = xt_default_unavailable_;
        event.device = id_; event.vs = shadow_.vs_hash; event.ps = shadow_.ps_hash;
        event.ready_mask = unsigned(bool(shadow_.vs_xt_default_ordinary))
            | (unsigned(bool(shadow_.vs_xt_default_linear)) << 1)
            | (unsigned(bool(shadow_.ps_xt_default_ordinary)) << 2)
            | (unsigned(bool(shadow_.ps_material_variant)) << 3);
        event.seen = true; event.pending = true;
    }
}
void MotionOutput::report_xt_default_unavailable() noexcept {
    auto& event = xt_default_unavailable_;
    if (!event.pending) return;
    event.pending = false; // Reentrant reporting/setters cannot duplicate or replace it.
    log("linear_material_xt_default_unavailable device=%llu vs=%016llx ps=%016llx ordinary_vs=%u linear_vs=%u ordinary_ps=%u linear_ps=%u native_forward=1",
        event.device, event.vs, event.ps, event.ready_mask & 1u, (event.ready_mask >> 1) & 1u,
        (event.ready_mask >> 2) & 1u, (event.ready_mask >> 3) & 1u);
}
void MotionOutput::refresh_linear_emission_contract() noexcept {
    // One pair lookup at the setter serves the linear route and the
    // source-only gain; the draw-time checks read the cached pointers.
    const bool lookup = (linear_emission_requested_ || (emission_source_gain_requested_ && shadow_.ps_source_gain_variant))
        && shadow_.vs_registered && shadow_.ps_registered;
    const unsigned index = lookup ? renderer::linear_emission_pair_index(shadow_.vs_hash, shadow_.ps_hash) : renderer::linear_emission_pair_count;
    const bool pair_reviewed = index < renderer::linear_emission_pair_count;
    shadow_.emission_pair = linear_emission_requested_ && pair_reviewed;
    shadow_.emission_eligible_variant = shadow_.emission_pair ? shadow_.ps_emission_variant : nullptr;
    shadow_.source_gain_pair = index;
    shadow_.source_gain_eligible_variant = pair_reviewed ? shadow_.ps_source_gain_variant : nullptr;
    shadow_.screen_pair = screen_emission_requested_ && shadow_.vs_registered && shadow_.ps_registered
        && screen_emission::admitted_pair(shadow_.vs_hash, shadow_.ps_hash);
    shadow_.screen_eligible_variant = shadow_.screen_pair ? shadow_.ps_screen_variant : nullptr;
    shadow_.screen_additive_index = screen_additive_requested_ && shadow_.vs_registered && shadow_.ps_registered
        ? screen_emission::admitted_pair_index(shadow_.vs_hash, shadow_.ps_hash) : screen_emission::pair_count;
    shadow_.screen_additive_pair = shadow_.screen_additive_index < screen_emission::pair_count;
    shadow_.fade_sampler_mask = distance_fade_requested_ && shadow_.vs_registered && shadow_.ps_registered
        ? renderer::linear_distance_fade_sampler_mask(shadow_.vs_hash, shadow_.ps_hash) : 0;
    // The fade-band arm keys on the pair identity alone (independent of the
    // fade route switch: the arm needs no bracket); the draw-time gate adds
    // the state, the device readiness and the fraction.
    shadow_.fade_route_pair = fade_route_threshold_ <= 1000u && shadow_.vs_registered && shadow_.ps_registered
        && renderer::linear_distance_fade_pair(shadow_.vs_hash, shadow_.ps_hash)
        && fade_route::registers(shadow_.vs_hash, shadow_.fade_route_registers);
}
// Called only after the ordinary opaque/no-MSAA motion gates. All fields are
// cached and no bytecode is revalidated in this draw-time check; the sampler
// sRGB flags come from the shadow with the hooks on and, with them off, from
// one GetSamplerState per required stage per draw (sampler_srgb_known).
unsigned MotionOutput::linear_material_refusal() noexcept {
    if (!shadow_.material_contract.sampler_mask) return 1;
    if (shadow_.xt_default_pair ? !shadow_.xt_default_ready : (!shadow_.vs_material_variant || !shadow_.ps_material_variant)) return 2;
    if (!hdr_enabled_ || hdr_state_ != HdrState::Active || !hdr_ || !hdr_->tonemap_active()
        || hdr_config_.tonemap != renderer::HdrTonemap::Agx
        || hdr_config_.decode != x3::temporal::AgxDecode::gamma22) return 3;
    for (std::uint32_t mask = shadow_.material_contract.sampler_mask; mask; mask &= mask - 1) {
        const unsigned stage = unsigned(__builtin_ctz(mask));
        if (!sampler_srgb_known(stage) || samplers_[stage].srgb != FALSE) return 4;
    }
    return 0;
}

// Combined-stage refusal is never a temporal refusal. A failed VS/PS bind is
// restored before exactly one ordinary motion attempt; jitter rows, history
// mode and RT1/RT2 setup live outside this function and are retained.
HRESULT MotionOutput::bind_variant_pair(MotionRoute& route, bool material) noexcept {
    if (shadow_.xt_default_pair && !shadow_.xt_default_ready) return E_FAIL;
    // Attempted setters may mutate before reporting failure. Every attempted
    // stage must therefore be restored, including the setter that failed.
    const auto vs = shadow_.xt_default_ready
        ? (material ? shadow_.vs_xt_default_linear : shadow_.vs_xt_default_ordinary)
        : (material ? shadow_.vs_material_variant : shadow_.vs_variant);
    auto ps = !material && shadow_.xt_default_ready ? shadow_.ps_xt_default_ordinary
        : (material ? shadow_.ps_material_variant : shadow_.ps_variant);
    route.sun_receiver=false;
    if(sun_lane_active_&&route.depth&&!route.fade_arm){
        const auto lane=material?shadow_.ps_sun_material:shadow_.xt_default_ready?shadow_.ps_sun_xt:shadow_.ps_sun_motion;
        if(lane){ps=lane;route.sun_receiver=material&&shadow_.ps_sun_extraction;}
        else {sun_frame_.failed=true;sun_lane_failed_=true;}
    }
    // Original fill: the same one bind pair, the PS being the motion variant
    // with the fill block. Only the ordinary (non-material) route, never over
    // an XT repaired or sun-lane program, only into the FP16 scene target.
    bool fill = false;
    if (shadow_.original_fill_pair && !material && !shadow_.xt_default_ready && hdr_state_ == HdrState::Active
        && ps == shadow_.ps_variant && !route.fade_arm && shadow_.ps_original_fill_variant) { ps = shadow_.ps_original_fill_variant; fill = true; }
    route.vs_set = true;
    HRESULT hr = native<SetVsFn>(SetVertexShader)(device_, vs);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (route.cutout && material && SUCCEEDED(hr) && fixture_cutout_vs_fault_) { --fixture_cutout_vs_fault_; hr = E_FAIL; }
#endif
    if (SUCCEEDED(hr)) {
        route.ps_set = true;
        hr = native<SetPsFn>(SetPixelShader)(device_, ps);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (route.cutout && material && SUCCEEDED(hr) && fixture_cutout_ps_fault_) { --fixture_cutout_ps_fault_; hr = E_FAIL; }
#endif
    }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
     if(sun_lane_active_&&!sun_lane_failed_&&!std::strcmp(fault,"bind"))hr=E_FAIL;}
#endif
    if (FAILED(hr) && SUCCEEDED(route.preparation_error)) route.preparation_error = hr;
    if(sun_lane_active_&&FAILED(hr)){sun_frame_.failed=true;sun_lane_failed_=true;route.sun_receiver=false;}
    if (material && FAILED(hr)) {
        ++counters_.material_bind_failures;
        const HRESULT restored = undo(route);
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("linear_material_bind_failed device=%llu frame=%llu result=%08lx first_prepare=%08lx restore=%08lx xt_default=%u",
                id_, frame_, hr, route.preparation_error, restored, shadow_.xt_default_pair);
        }
        if (FAILED(restored)) return restored;
        return bind_variant_pair(route, false);
    }
    route.linear_material = material && SUCCEEDED(hr);
    if (fill && SUCCEEDED(hr)) route.original_fill = true;
    return hr;
}

// Source-only encoded gain admission (linear-emission-cost.md, "Implemented"
// and "Screen substitution"). The variant scales the colour lanes the game
// blends with ADD/ONE/ONE, so it is exactly a brightness change only under
// an additive blend into the FP16 scene target (values above 1 survive
// there). The colour law is the shared renderer::linear_emission_source_gain_blend:
// ONE/ONE/ADD admits whatever SEPARATEALPHABLENDENABLE and the alpha triple
// say (the variant multiplies rgb only; run 26 refused every engine draw on
// sepalpha=1 before this); screen ONE/INVSRCCOLOR/ADD (92 of the 122 engine
// materials, docs/reverse-engineering/effect-shader-users.md) admits with
// DESTBLEND ONE substituted for this draw, exactly as the additive bullet
// option does (prepare_screen_additive): `G*s + bg` instead of the native
// `s + bg*(1-s)`, identical over black for s <= 1, brighter by bg*s over a
// lit background (the accepted trade), never negative. The substitution is
// applied before the program bind so a failed bind unwinds it; both are put
// back after the draw (finish_source_gain) through the setter shadow's
// value. Any other blend, sRGB write, an unknown state, an inactive
// redirect or a state block being recorded keeps the native program: fail
// closed, counted per reason, the first failure_log_limit samples of each
// reason logged per device epoch with the alpha triple for the record
// (-1 = not shadowed). Per-draw cost of the addition: one SetRenderState
// before and one after a screen draw; an additive draw is unchanged.
void MotionOutput::prepare_source_gain(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (hdr_state_ != HdrState::Active || !route.scene || !scene_open_ || shadow_.recording || !call.primitives || main_msaa_) {
        // Run 27 counted 66,024 of these (refused_other) without a witness:
        // the first failure_log_limit per device epoch carry the state bits
        // and the shadowed blend triple so the population can be named.
        ++source_gain_counts_.refused_state;
        if (source_gain_logged_[3] < failure_log_limit) {
            ++source_gain_logged_[3];
            log("emission_source_gain_refused_state device=%llu frame=%llu vs=%016llx ps=%016llx hdr=%u scene=%u open=%u recording=%u primitives=%lu msaa=%u blend=%ld src=%ld dst=%ld op=%ld sepalpha=%ld",
                id_, frame_, shadow_.vs_hash, shadow_.ps_hash,
                unsigned(hdr_state_ == HdrState::Active), unsigned(route.scene), unsigned(scene_open_), unsigned(shadow_.recording), static_cast<unsigned long>(call.primitives), unsigned(main_msaa_),
                shadow_.states_known[3] ? long(shadow_.states[3]) : -1L, composition_blend_field(0), composition_blend_field(1), composition_blend_field(2), composition_blend_field(3));
        }
        return;
    }
    bool known = state_known(3); known = state_known(5) && known;
    for (unsigned i = 0; i < 3; ++i) known = blend_known(i) && known; // the colour triple gates; sepalpha and the alpha triple are logged only
    if (!known) { ++source_gain_counts_.refused_unknown; return; }
    const auto verdict = renderer::linear_emission_source_gain_blend(shadow_.states[3], shadow_.states[5],
        shadow_.composition_blend[0], shadow_.composition_blend[1], shadow_.composition_blend[2]);
    if (verdict == renderer::SourceGainBlend::Blend) {
        ++source_gain_counts_.refused_blend;
        if (source_gain_logged_[0] < failure_log_limit) {
            ++source_gain_logged_[0];
            log("emission_source_gain_refused device=%llu frame=%llu vs=%016llx ps=%016llx reason=blend blend=%lu src=%lu dst=%lu op=%lu sepalpha=%ld srcalpha=%ld dstalpha=%ld opalpha=%ld srgb=%lu",
                id_, frame_, shadow_.vs_hash, shadow_.ps_hash, shadow_.states[3], shadow_.composition_blend[0], shadow_.composition_blend[1],
                shadow_.composition_blend[2], composition_blend_field(3), composition_blend_field(4), composition_blend_field(5), composition_blend_field(6), shadow_.states[5]);
        }
        return;
    }
    const bool screen = verdict == renderer::SourceGainBlend::Screen;
    if (screen) {
        // Screen substitution: DESTBLEND ONE for this draw. The shadow holds
        // the application's INVSRCCOLOR (known: the law just read it), which
        // finish_source_gain puts back. A failed setter applies nothing the
        // proxy can name, so the draw stays native and is counted as a
        // screen refusal; the shadow keeps the application's value.
        const HRESULT hr = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, D3DBLEND_ONE);
        if (FAILED(hr)) {
            ++source_gain_counts_.refused_screen;
            if (source_gain_logged_[1] < failure_log_limit) {
                ++source_gain_logged_[1];
                log("emission_source_gain_refused device=%llu frame=%llu vs=%016llx ps=%016llx reason=screen_substitute_failed blend=%lu src=%lu dst=%lu op=%lu sepalpha=%ld srcalpha=%ld dstalpha=%ld opalpha=%ld srgb=%lu result=%08lx",
                    id_, frame_, shadow_.vs_hash, shadow_.ps_hash, shadow_.states[3], shadow_.composition_blend[0], shadow_.composition_blend[1],
                    shadow_.composition_blend[2], composition_blend_field(3), composition_blend_field(4), composition_blend_field(5), composition_blend_field(6), shadow_.states[5], hr);
            }
            return;
        }
        route.source_gain_screen = true;
    }
    const HRESULT hr = native<SetPsFn>(SetPixelShader)(device_, shadow_.source_gain_eligible_variant);
    if (FAILED(hr)) {
        // A failed setter may have mutated the binding: put the application's
        // program back (and the substituted DESTBLEND) before the native draw
        // goes out; a failed restore is the same lost-state condition as a
        // failed route restore.
        ++source_gain_counts_.bind_failures;
        HRESULT restored = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps);
        if (route.source_gain_screen) {
            const HRESULT back = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, shadow_.composition_blend[1]);
            route.source_gain_screen = false;
            if (SUCCEEDED(restored) && FAILED(back)) restored = back;
            if (FAILED(back)) invalidate_render_states();
        }
        if (FAILED(restored)) {
            if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = restored; }
            route.submit = false; route.submission_error = motion_state_error_;
            ++counters_.restore_failures; invalidate_taa(TaaInvalidateSite::RestoreFailed);
        }
        if (source_gain_logged_[2] < failure_log_limit) {
            ++source_gain_logged_[2];
            log("emission_source_gain_bind_failed device=%llu frame=%llu ps=%016llx screen=%u result=%08lx restore=%08lx", id_, frame_, shadow_.ps_hash, unsigned(screen), hr, restored);
        }
        return;
    }
    route.source_gain = true;
    ++source_gain_counts_.admitted;
    if (screen) ++source_gain_counts_.admitted_screen;
    // First admission of each pair per device epoch (at most twenty lines):
    // which pairs actually draw gained, and whether the first one substituted.
    const std::uint32_t bit = shadow_.source_gain_pair < renderer::linear_emission_pair_count ? 1u << shadow_.source_gain_pair : 0u;
    if (bit && !(source_gain_pair_logged_ & bit)) {
        source_gain_pair_logged_ |= bit;
        log("emission_source_gain_pair device=%llu frame=%llu vs=%016llx ps=%016llx gain=%g screen=%u",
            id_, frame_, shadow_.vs_hash, shadow_.ps_hash, double(emission_source_gain_), unsigned(screen));
    }
}
// After the native draw: the application's program, then its DESTBLEND when
// the screen substitution was applied (the shadowed INVSRCCOLOR: the setter
// hook's value with the hooks on, the value this draw's admission read with
// them off; nothing of the application's runs between prepare and finish,
// both sit under the hook mutex of one draw). Mirrors finish_screen_additive.
void MotionOutput::finish_source_gain(MotionRoute& route) noexcept {
    route.source_gain = false;
    HRESULT first = native<SetPsFn>(SetPixelShader)(device_, shadow_.ps);
    if (route.source_gain_screen) {
        route.source_gain_screen = false;
        const HRESULT back = native<SetRenderStateFn>(SetRenderState)(device_, D3DRS_DESTBLEND, shadow_.composition_blend[1]);
        if (FAILED(back)) { invalidate_render_states(); if (SUCCEEDED(first)) first = back; }
    }
    if (FAILED(first)) {
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = first; }
        ++counters_.restore_failures; invalidate_taa(TaaInvalidateSite::RestoreFailed);
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=source_gain", id_, frame_, counters_.draws, first);
        }
    }
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
        // Diagnostics and the non-writer verdict: bit0 is any depth test
        // (D3DZB_TRUE or D3DZB_USEW: both write depth with z write on, so
        // USEW is a writer, fail closed), bit1 z write on, bit2 both read.
        if (route.sun_color_writer)
            route.sun_z_state = std::uint8_t((z != 0 ? 1u : 0u) | (write != 0 ? 2u : 0u) | (SUCCEEDED(z_hr) && SUCCEEDED(write_hr) ? 4u : 0u));
    }
    pending_valid_ = true;
    if (fill_pending_) fill_sentinel();
    // A draw after the engine's scene-end signal belongs to compositing or an
    // overlay: it neither routes nor jitters (scene_bound refuses); counted
    // for the cross-check (a disagreement when the bloom copy follows it).
    if (counters_.hook_scene_end && state == renderer::BoundaryState::Scene && !counters_.bloom_copy_seen) ++counters_.draws_after_hook;
    // Gate 1: feature/capability, target owned, not recording a state block.
    if (!target_surface_ || shadow_.recording || main_msaa_) { route.gate = MotionGate::Feature; ++counters_.gates[1]; route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::Feature); return; }
    // Gate 2: scene phase with the latched main color/depth bound.
    if (!scene_bound()) { route.gate = MotionGate::Scene; ++counters_.gates[2]; route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::Scene); return; }
    route.scene = true;
    // Unavailable repair is feature refusal, not an enhanced fallback through
    // the malformed original linkage. Preserve the original bindings and rows.
    if (shadow_.xt_default_pair && !shadow_.xt_default_ready) {
        route.gate = MotionGate::Pair; ++counters_.gates[3]; route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::Pair); return;
    }
    // Every scene draw whose VS has a table row or is a reviewed depth-only
    // prepass program is jittered, routed or not, so the rasterized coverage
    // (and depth) of the whole scene moves together. A depth writer that
    // still goes out unjittered is counted: it breaks that invariant for every
    // later jittered draw depth-tested against it (asteroid-fog-temporal.md).
    if (jitter_active_ && (shadow_.vs_row || shadow_.vs_prepass)) apply_jitter(route);
    if (jitter_active_ && !route.jittered && SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && z == 1 && write == 1) ++counters_.unjittered_depth_writers;
    // Gate 3: exact reviewed pair (one profile-table row) with both variants
    // registered. Variants are per program; the pair check is what keys
    // eligibility, so a VS alias shared with an unreviewed PS never routes.
    const renderer::MotionOutputProfile* pair = shadow_.vs_variant && shadow_.ps_variant && shadow_.vs_row
        ? renderer::material_motion_profile(shadow_.vs_hash, shadow_.ps_hash) : nullptr;
    if (!pair || !renderer::material_motion_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash)) {
        route.gate = MotionGate::Pair; ++counters_.gates[3];
        // Diagnostics: a program outside the registry (unknown PS, VS without
        // a profile row) versus a registered row without a reviewed pair.
        route.sun_refusal = std::uint8_t(!shadow_.ps_hash || !shadow_.ps_registered || !shadow_.vs_row
            ? renderer::SunUntrackedReason::Unregistered : renderer::SunUntrackedReason::Pair);
        return;
    }
    route.depth = depth_enabled_ && renderer::material_motion_pixel_writes_depth(*pair, depth_enabled_);
    // Gate 4: opaque state, the separately qualified exact cutout arm, or the
    // tested-opaque arm; known rows in the VS row's clip-row
    // window, the row's light-loop bound where it reads constants relatively,
    // no user-memory geometry, no instancing, known declaration/stream identity.
    // Color-only state never decides depth tracking: COLORWRITEENABLE masks
    // RT0 alone (RT1/RT2 carry their own masks, set to 15 for the draw), and
    // alpha test discards a fragment before the depth write and before every
    // target write, so the fragments that write depth are exactly the ones
    // that write the lane, as long as the variant's oC0.a is the original's
    // (material_motion / linear_sun_share alpha identity). The tested-opaque
    // arm therefore admits z on, z write on, blend off, sRGB off, any nonzero
    // RT0 mask, alpha test on or off, for a registered pair; the two cutout
    // pairs (shadow_.cutout_pair, cutout::pair by identity) enter it only on
    // a frame whose exact cutout arm is not configured (cutout_arm_active_:
    // off under a nonzero configured mip bias, an Unsupported/Retry verdict
    // or HDR off). With the exact arm configured they keep that arm, or the
    // gate-4 state refusal outside its exact state; their coverage semantics
    // (mark_cutout_candidate, cutout::missed, linear_cutout.h) key on the
    // same latch, so a pair the tested-opaque arm admits is never a cutout
    // candidate and route.cutout stays exact-arm-only. The arm exists only
    // for the sun lane: it is
    // active only with the lane latched on this frame (sun_lane_active_) and
    // the lane's linear-material prerequisite, so with --sun-shadow-lane off
    // every alpha-tested or partial-mask pair routes exactly as before. It
    // checks no capability: the RT1/RT2 masks and formats are the ordinary
    // route's, and no MRT blending or independent-mask caps are needed because
    // blending stays refused. Blending: D3D9 blends every bound target, which
    // would blend the lane's depth/share too. The XT class-C hull/station materials
    // (37c34a7478544c14/f1b0e820c7b488c3 and siblings) draw with alpha test
    // on, ALPHAREF 1 GREATEREQUAL and mask 7 (docs/verification/directional-shadows.md).
    const auto& profile = *shadow_.vs_row;
    const std::size_t window = window_of(profile.matrix_register);
    const bool loop_bounded = !profile.light_loop_bound_required ||
        (shadow_.integer0_known && shadow_.integer0[0] >= 0 && shadow_.integer0[0] <= int(profile.light_loop_max_count));
    DWORD blend = 1, test = 1, srgb = 1, color = 0; UINT frequency = 0;
    // read_failed / cutout_ok exist for the sun-lane refusal buckets only: the
    // chain below is unchanged in order and in the getters it calls.
    bool read_failed = false, cutout_ok = true;
    // read_ok (three compares per read, four reads per draw) records which of
    // the test/mask/sRGB values were actually read, for the writer line only.
    std::uint32_t read_ok = 0;
    const auto read = [&](D3DRENDERSTATETYPE state, DWORD* value) noexcept {
        const bool ok = SUCCEEDED(render_state(state, value)); read_failed |= !ok;
        if (ok) read_ok |= state == D3DRS_ALPHATESTENABLE ? 1u : state == D3DRS_COLORWRITEENABLE ? 2u : state == D3DRS_SRGBWRITEENABLE ? 4u : 0u;
        return ok;
    };
    const auto read_frequency = [&]() noexcept { const bool ok = SUCCEEDED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency)); read_failed |= !ok; return ok; };
    const bool draw_state_ok = !call.user_memory && SUCCEEDED(z_hr) && SUCCEEDED(write_hr) && z == 1 && write == 1 &&
        read(D3DRS_ALPHABLENDENABLE, &blend) && !blend &&
        read(D3DRS_ALPHATESTENABLE, &test) &&
        read(D3DRS_SRGBWRITEENABLE, &srgb) && !srgb &&
        read(D3DRS_COLORWRITEENABLE, &color) &&
        ((!test && color == 15) || (test == 1 && color == 7 && shadow_.cutout_pair && (cutout_ok = cutout_draw_state()))
         || (sun_lane_active_ && linear_material_requested_ && !(shadow_.cutout_pair && cutout_arm_active_) && test <= 1 && color != 0)) &&
        read_frequency() &&
        !(frequency & D3DSTREAMSOURCE_INDEXEDDATA) && (frequency & 0x3fffffffu) <= 1 &&
        shadow_.rows_known[window] && loop_bounded &&
        shadow_.stream0 && shadow_.stream0_stride && shadow_.declaration && call.primitives &&
        (!call.indexed || shadow_.indices);
    // The fade-band arm (fade_route_core.h): a fade pair in the engine's
    // exact fade-band state whose fade fraction estimate reaches the
    // threshold routes like an opaque draw (RT1 from its own rows, RT2
    // masked); every other refusal stays gate 4.
    if (!draw_state_ok && !fade_arm_admits(route, call, z, write, window, loop_bounded)) {
        route.gate = MotionGate::DrawState; ++counters_.gates[4];
        // Diagnostics only (lane on): the first failing check in the chain's
        // order, using the values the chain read (a failed getter is its own
        // bucket; the cutout verdict is the one the chain computed). No
        // getter is repeated.
        if (route.sun_color_writer) {
            using renderer::SunUntrackedReason;
            route.sun_draw_state = std::uint16_t((color & 15u) | (test ? 16u : 0u) | (srgb ? 32u : 0u) | ((read_ok & 7u) << 6));
            route.sun_refusal = std::uint8_t(
                call.user_memory ? SunUntrackedReason::Geometry
                : (read_failed || FAILED(z_hr) || FAILED(write_hr)) ? SunUntrackedReason::ReadFailed
                : !(z == 1 && write == 1) ? SunUntrackedReason::NoZWrite
                : blend ? SunUntrackedReason::Blended
                : (srgb || !((!test && color == 15) || (test == 1 && color == 7 && shadow_.cutout_pair && cutout_ok)
                             || (sun_lane_active_ && linear_material_requested_ && !(shadow_.cutout_pair && cutout_arm_active_) && test <= 1 && color != 0))) ? SunUntrackedReason::State
                : ((frequency & D3DSTREAMSOURCE_INDEXEDDATA) || (frequency & 0x3fffffffu) > 1) ? SunUntrackedReason::Geometry
                : (!shadow_.rows_known[window] || !loop_bounded) ? SunUntrackedReason::Rows
                : SunUntrackedReason::Geometry); // stream, declaration, primitives or indices
        }
        return;
    }
    route.alpha_tested = !route.fade_arm && test != 0;
    // Under a configured bias an alpha-tested cutout pair can only be here through
    // the tested-opaque arm (cutout_draw_state refuses a nonzero bias): it keeps
    // its native LOD bias so the alpha source, and with it the alpha-tested
    // coverage, is the native draw's.
    route.native_mip_bias = route.alpha_tested && shadow_.cutout_pair;
    route.cutout = route.alpha_tested && shadow_.cutout_pair && cutout_arm_active_; // the exact cutout arm only (cutout_routed, fixture faults); never the tested-opaque arm
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
    if (candidates_requested_) note_candidate_distance(route, rows.data()); // Diagnostics only (candidate counter).
    // Gate 5: verified object/camera scope. Failure still routes with mode 0 so
    // covered pixels of this material carry the sentinel, never stale history.
    bool matched = false;
    if (!sample_scope(route)) { route.gate = MotionGate::Scope; ++counters_.gates[5]; route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::Scope); }
    else if (!history_.lookup_and_record(key, rows, previous)) { route.gate = MotionGate::History; ++counters_.gates[6]; ++counters_.keyed; ++counters_.missing; route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::History); }
    else { matched = true; route.gate = MotionGate::None; ++counters_.gates[0]; ++counters_.keyed; }
    if (matched) {
        // Cut detector sample: screen displacement of the projected object
        // origin (translation column over w) between the previous and the
        // current unjittered rows. Bounded storage, reserved at attach.
        const float* c = rows.data(); const float* p = previous.data();
        if (c[15] > 1e-6f && p[15] > 1e-6f && displacements_.size() < displacements_.capacity()) {
            const float dx = (c[3] / c[15] - p[3] / p[15]) * .5f * float(target_width_);
            const float dy = (c[7] / c[15] - p[7] / p[15]) * .5f * float(target_height_);
            const float magnitude = scalar::sqrt(dx * dx + dy * dy); // sse_scalar.h: the draw path is light-envelope code
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
    bool material = false;
    if (linear_material_requested_) {
        const unsigned refusal = linear_material_refusal();
        material = refusal == 0;
        if (refusal) {
            ++counters_.material_refused;
            const unsigned bit = 1u << refusal;
            if (!(material_refusals_logged_ & bit)) {
                material_refusals_logged_ |= bit;
                // Build masks only for this bounded first refusal log. The
                // steady-state draw gate visits cached required samplers only;
                // with the hooks off this one log reads the six stages
                // (sampler_srgb_known), so "unknown" means a failed read there.
                std::uint32_t unknown = 0, srgb_enabled = 0;
                for (unsigned stage = 0; stage < 6; ++stage) {
                    if (!sampler_srgb_known(stage)) unknown |= 1u << stage;
                    else if (samplers_[stage].srgb != FALSE) srgb_enabled |= 1u << stage;
                }
                log("linear_material_refused device=%llu reason=%u vs=%016llx ps=%016llx required=%02lx unknown=%02lx srgb_enabled=%02lx",
                    id_, refusal, shadow_.vs_hash, shadow_.ps_hash, static_cast<unsigned long>(shadow_.material_contract.sampler_mask),
                    static_cast<unsigned long>(unknown), static_cast<unsigned long>(srgb_enabled));
            }
        }
    }
    HRESULT hr = bind_variant_pair(route, material);
    if (SUCCEEDED(hr)) {
        route.vs_constants_set = true;
        hr = native<SetConstantsFFn>(SetVertexShaderConstantF)(device_,
            renderer::MaterialMotionAbi::previous_vertex_constant, matched ? previous.data() : zeros, 4);
    }
    if (SUCCEEDED(hr)) {
        route.ps_constants_set = true;
        hr = native<SetConstantsFFn>(SetPixelShaderConstantF)(device_,
            renderer::MaterialMotionAbi::pixel_coordinates_constant, pixel, 2);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if (SUCCEEDED(hr)) { std::memcpy(fixture_last_pixel_abi_, pixel, sizeof pixel); fixture_abi_known_ = true; }
#endif
    }
    if (SUCCEEDED(hr)) hr = bind_targets(route);
    if (SUCCEEDED(hr)) hr = apply_wrap_states(route, *shadow_.vs_row);
    route.ticks = draw_stamp() - apply_begin;
    if (FAILED(hr)) {
        if (SUCCEEDED(route.preparation_error)) route.preparation_error = hr;
        // A native fallback is safe only after the complete rollback succeeds.
        rollback_route(route);
        ++counters_.apply_failures;
        route.sun_refusal = std::uint8_t(renderer::SunUntrackedReason::ApplyFailed);
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_apply_failed device=%llu frame=%llu index=%lu result=%08lx first_prepare=%08lx", id_, frame_, counters_.draws, hr, route.preparation_error);
        }
        return;
    }
    route.routed = true; route.matched = matched;
    if (route.fade_arm) { ++counters_.fade_routed; if (route.fade_held) ++counters_.fade_held; }
    if (route.linear_material) {
        ++counters_.material_routed;
        if (shadow_.material_contract.bump) ++counters_.material_bump_routed;
    }
    ++counters_.routed; if (matched) ++counters_.matched; if (route.depth) ++counters_.depth_routed;
    // The mip LOD bias of the routed material stages, while the jitter is on
    // (a failed sampler call is counted and logged; the draw still routes).
    // A cutout pair on the tested-opaque arm instead restores any stage still
    // holding the route's bias (one SetSamplerState per biased stage, none when
    // no stage is biased; the next ordinary routed draw re-applies it).
    if (mip_bias_bits_ && jitter_active_) { if (route.native_mip_bias) restore_mip_bias(); else apply_mip_bias(); }
}

// Step 1 of the region design: the rectangle for an admitted fade draw, from
// the owning part's AABB (fade_bounds_) through the rows the draw uses
// (jittered like apply_jitter when route.jittered), intersected with the
// application viewport and the owning target. Every doubt selects the full
// viewport (an unknown viewport selects the whole target); nothing here
// changes the bracket yet. Per draw: one table probe, on a hit two content
// views (each one recursive registry_mutex take, one map find and one native
// GetPrivateData on the backend buffer: two native calls and two mutex takes
// per admitted fade draw), eight corner projections; no allocation, no device
// Get, no float formatting (the per-draw line carries the fraction as an
// integer per mille of the viewport area, or of the target area when the
// viewport is unknown).
fade_region::Region MotionOutput::fade_rectangle(const MotionRoute& route, fade_region::Result& bound, bool& of_viewport, unsigned& permille, bool read_only,
                                                 fade_region::BoundSource source, std::uint32_t vertex_count, std::uint64_t* aabb_px) noexcept {
    using namespace fade_region;
    const Query query{shadow_.stream0, shadow_.indices, shadow_.stream0_identity, shadow_.indices_identity};
    // The admitted route learns (resolve); the capture-only diagnostic only
    // peeks: the table's entries, stamps and counters are never touched by a
    // draw the route did not admit. The locked-prefix source (step B) has no
    // table here: one ownership lookup of the scan published at Unlock.
    bound = source == BoundSource::LockedPrefix ? fade_region::resolve_locked_prefix(query, vertex_count)
        : read_only ? fade_region::peek(fade_bounds_, query) : fade_region::resolve(fade_bounds_, query);
    const auto& v = shadow_.viewport;
    const Viewport viewport{v.x, v.y, v.known ? v.width : 0u, v.known ? v.height : 0u};
    const float* rows = nullptr;
    float jittered[16];
    // The rows the draw uses: the row's clip window for a part bound; c0-3
    // for the locked prefix (the bullet VS transforms world positions through
    // g_mViewProjection at c0-3 only, effects-engine-remaining-emission.md).
    const bool rows_addressed = source == BoundSource::LockedPrefix || shadow_.vs_row;
    if (rows_addressed) {
        const std::size_t window = window_of(source == BoundSource::LockedPrefix ? 0u : shadow_.vs_row->matrix_register);
        if (window < motion_matrix_windows_max && shadow_.rows_known[window]) {
            std::memcpy(jittered, shadow_.rows[window], sizeof jittered);
            if (route.jittered && main_.width && main_.height) jitter_rows(jittered, jitter_[0], jitter_[1], main_.width, main_.height);
            rows = jittered;
        }
    }
    const bool fill_solid = fill_mode_known() && shadow_.fill_mode == D3DFILL_SOLID;
    // The owning target: the FP16 main while the HDR path holds it; for the
    // locked-prefix diagnostic without it, the application's RT0 (step C
    // composes into the HDR target and takes the first branch).
    const bool rt0_target = source == BoundSource::LockedPrefix && !hdr_ && shadow_.rt0.known;
    const Rect target{0, 0, std::int32_t(hdr_ ? hdr_->width() : rt0_target ? shadow_.rt0.width : 0u), std::int32_t(hdr_ ? hdr_->height() : rt0_target ? shadow_.rt0.height : 0u)};
    // The locked prefix (step D) is the drawn vertices themselves, cut per
    // triangle against the D3D near plane (z >= 0) before the divide: a
    // bullet batch that starts behind the camera is bounded by its visible
    // part, one entirely behind is refused as BehindNear. The part-bound fade
    // route projects its AABB unclipped (NonPositiveW -> full viewport).
    Region region{};
    if (source == BoundSource::LockedPrefix) {
        PrefixHull hull{};
        region = derive_prefix(rows, bound.status == Status::Bound ? bound.positions : nullptr, vertex_count, viewport, fill_solid, target, &hull);
        bound.box = hull.aabb;
        // The step-B comparison: the near-clipped rectangle of the prefix's
        // own AABB (eight corner projections; the per-draw and frame lines
        // carry both areas so run 20 shows the reduction). Capture frames
        // and the timing diagnostic only: the steady-state draw path skips it.
        if (aabb_px && (capture_ || screen_emission_timing_)) {
            *aabb_px = 0;
            if (region.bound) {
                Rect box_rect{}; NearClip cut{true, 0};
                if (project_box(rows, hull.aabb, viewport, &box_rect, &cut) == Reason::Bound) *aabb_px = area(intersect(box_rect, target));
            }
        }
    } else region = derive(rows, bound.status == Status::Bound, bound.box, viewport, fill_solid, target, false);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_fade_rect_set_ && source == BoundSource::Part) { region.rect = fixture_fade_rect_; region.reason = Reason::Bound; region.bound = true; }
#endif
    // Never beyond the owning target, on every path; empty -> 1x1 at its origin.
    region.rect = intersect(region.rect, target);
    if (empty(region.rect)) region.rect = {target.left, target.top, target.left + 1, target.top + 1};
    // One denominator: the viewport area; the target area only while the
    // viewport is unknown (the log names which one).
    of_viewport = region.reason != Reason::Viewport;
    const std::uint64_t whole = of_viewport ? std::uint64_t(viewport.width) * viewport.height : area(target);
    permille = whole ? unsigned(area(region.rect) * 1000u / whole) : 1000u;
    return region;
}
void MotionOutput::derive_fade_region(MotionRoute& route) noexcept {
    using namespace fade_region;
    route.fade_region_evaluated = true;
    Result bound{};
    bool of_viewport = false;
    unsigned permille = 0;
    const Region region = fade_rectangle(route, bound, of_viewport, permille, false);
    auto& counts = composition_counts_;
    ++counts.region_status[unsigned(bound.status) < unsigned(Status::Count) ? unsigned(bound.status) : 0u];
    if (bound.hit) ++counts.region_hit; else if (bound.status == Status::Bound) ++counts.region_miss;
    if (bound.poisoned_now) ++counts.region_poisoned;
    if (bound.evicted) ++counts.region_evicted;
    route.fade_region = region;
    ++counts.region_reason[unsigned(region.reason) < unsigned(Reason::Count) ? unsigned(region.reason) : 0u];
    counts.region_permille_sum += permille;
    route.fade_region_permille = permille;
    if (region.bound) ++counts.region_bound; else ++counts.region_full;
    const bool witness_frame = this->witness_frame();
    bool witness_line = false;
    if (witness_frame) {
        auto& w = fade_witness_;
        w.last = FadeWitness::rect_capacity;
        if (w.count < FadeWitness::rect_capacity) { w.last = w.count; w.rects[w.count] = region.rect; w.prepared[w.count] = false; }
        else w.overflow = true;
        ++w.count;
        witness_line = w.logged < FadeWitness::line_budget;
        if (witness_line) ++w.logged;
        // Buckets of the viewport-area fraction f (per mille): <=10, <=20, <=50, <=100, <=250, <=500, <1000, full.
        const unsigned bucket = permille <= 10 ? 0u : permille <= 20 ? 1u : permille <= 50 ? 2u : permille <= 100 ? 3u
            : permille <= 250 ? 4u : permille <= 500 ? 5u : permille < 1000 ? 6u : 7u;
        ++w.f_hist[bucket];
    }
    if (capture_ || witness_line)
        log("fade_region device=%llu frame=%llu index=%lu bound=%u reason=%u status=%s hit=%u poisoned=%u evicted=%u depth=%lu descriptor=%p part=%p aabb=%ld,%ld,%ld,%ld,%ld,%ld vb=%llu ib=%llu vb_rev=%llu ib_rev=%llu jittered=%u rect=%ld,%ld,%ld,%ld f_permille=%u f_of=%s table_used=%u table_poisoned=%u",
            id_, frame_, static_cast<unsigned long>(counters_.draws), region.bound, unsigned(region.reason), status_name(bound.status), bound.hit, bound.poisoned_now, bound.evicted,
            static_cast<unsigned long>(bound.depth), reinterpret_cast<void*>(bound.descriptor), reinterpret_cast<void*>(bound.part),
            long(bound.aabb[0]), long(bound.aabb[1]), long(bound.aabb[2]), long(bound.aabb[3]), long(bound.aabb[4]), long(bound.aabb[5]),
            shadow_.stream0, shadow_.indices, bound.vb_revision, bound.ib_revision, route.jittered,
            long(region.rect.left), long(region.rect.top), long(region.rect.right), long(region.rect.bottom), permille, of_viewport ? "viewport" : "target",
            fade_bounds_.used(), fade_bounds_.poisoned());
}

// Step B/D (screen-emission-region.md, screen-emission-bullet-bound.md): a
// non-indexed TRIANGLELIST draw from StartVertex 0 whose stream 0 is a
// stride-24 buffer with POSITION FLOAT3 at offset 0 (the bullet writer's
// layout) gets the locked-prefix rectangle: the leading primCount*3
// positions the ownership layer copied at the buffer's DISCARD Unlock (exact
// prefix: the Lock-time sentinel delimits it), projected per triangle through
// c0-3 with the near-plane cut (project_prefix). Every doubt (indexed, other
// topology, StartVertex, unknown declaration or stride, no published scan,
// lock since or during, nonfinite or absurd vertex in the prefix, draw past
// the scan) leaves prefix_region.bound false: such a draw is refused by
// step C, never given the full viewport. Per qualifying draw: the shadow
// tests, two registry finds and fixed-table probes (lookup, recheck), four
// double dot products per vertex plus eight corner projections for the
// AABB comparison; no allocation, one documented Get (stream frequency).
// Off (the default) costs one bool test per draw.
void MotionOutput::derive_prefix_region(const MotionDrawCall& call, MotionRoute& route) noexcept {
    using namespace fade_region;
    // The producer first (screen_emission_admission.h, shared with step C's
    // admission): only an admitted vertex shader's draws are evaluated, so
    // unrelated stride-24 draws never get a bound from the wrong matrix and
    // only their buffers are marked for the Unlock scan.
    if (!screen_emission::admitted_vertex_shader(shadow_.vs_hash)) return;
    if (call.indexed || call.user_memory || call.topology != D3DPT_TRIANGLELIST || call.first != 0 || !call.primitives) return;
    if (!shadow_.stream0 || !shadow_.stream0_identity || shadow_.stream0_stride != prefix::stride || shadow_.stream0_offset != 0) return;
    if (!shadow_.declaration || shadow_.position_offset != 0 || shadow_.position_type != D3DDECLTYPE_FLOAT3) return;
    const std::uint64_t begin = draw_stamp();
    auto& counts = composition_counts_;
    ++counts.prefix_draws;
    route.prefix_evaluated = true;
    // primCount*3 without overflow: anything past the scan bound is refused.
    const std::uint32_t vertex_count = call.primitives > prefix::max_vertices ? prefix::max_vertices + 1u : call.primitives * 3u;
    Result bound{};
    bool of_viewport = false;
    unsigned permille = 0;
    std::uint64_t aabb_px = 0; // 0 outside capture frames / the timing diagnostic
    Region region = fade_rectangle(route, bound, of_viewport, permille, true, BoundSource::LockedPrefix, vertex_count, &aabb_px);
    // The positions were projected without the registry mutex: the record
    // must still be published at the revision the lookup saw (a Lock in
    // between may have rewritten them under the projection).
    if (region.bound && !fade_region::recheck_locked_prefix(Query{shadow_.stream0, shadow_.indices, shadow_.stream0_identity, shadow_.indices_identity}, vertex_count, bound.vb_revision)) {
        region.bound = false; region.reason = Reason::BoundUnknown; bound.status = Status::ContentUnknown; bound.prefix_refusal = unsigned(prefix::Lookup::Pending);
        ++counts.prefix_rechecks;
    }
    // Instanced geometry (stream 0 frequency other than the default 1) draws
    // more than the prefix; one documented Get per admitted draw, refused
    // when it fails.
    UINT frequency = 0;
    if (region.bound && (FAILED(native<GetStreamFreqFn>(GetStreamSourceFreq)(device_, 0, &frequency)) || frequency != 1)) {
        region.bound = false; region.reason = Reason::BoundUnknown; ++counts.prefix_instanced;
    }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    // Straddling case of the live fixture: a bound rectangle smaller than
    // the draw, so the packed bracket leaves the outside native and the
    // witness must fire.
    if (fixture_screen_rect_set_ && region.bound) {
        region.rect = fixture_screen_rect_;
        const auto& v = shadow_.viewport;
        const std::uint64_t whole = of_viewport ? std::uint64_t(v.width) * v.height : std::uint64_t(hdr_ ? hdr_->width() : 0u) * (hdr_ ? hdr_->height() : 0u);
        permille = whole ? unsigned(area(region.rect) * 1000u / whole) : 1000u;
    }
#endif
    if (!region.bound) { region.rect = {0, 0, 0, 0}; permille = 0; aabb_px = 0; } // never the full viewport for this kind
    const std::uint64_t hull_px = region.bound ? area(region.rect) : 0u;
    route.prefix_region = region;
    route.prefix_region_permille = permille;
    const std::uint64_t ticks = draw_stamp() - begin;
    route.ticks += ticks;
    counts.prefix_ticks += ticks;
    ++counts.prefix_reason[unsigned(region.reason) < unsigned(Reason::Count) ? unsigned(region.reason) : 0u];
    ++counts.prefix_lookup[bound.prefix_refusal < unsigned(prefix::Lookup::Count) ? bound.prefix_refusal : 0u];
    if (region.bound) {
        ++counts.prefix_bound; counts.prefix_permille_sum += permille; if (region.clipped) ++counts.prefix_clipped;
        counts.prefix_hull_px += hull_px; counts.prefix_aabb_px += aabb_px; counts.prefix_vertices += vertex_count;
    } else ++counts.prefix_refused;
    if (capture_ || locked_prefix_log_)
        log("locked_prefix device=%llu frame=%llu index=%lu bound=%u reason=%u clipped=%u pad=%u status=%s lookup=%s vb=%llu rev=%llu vertices=%lu scanned=%lu box=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f rect=%ld,%ld,%ld,%ld f_permille=%u f_of=%s hull_px=%llu aabb_px=%llu ticks=%llu",
            id_, frame_, static_cast<unsigned long>(counters_.draws), region.bound, unsigned(region.reason), region.clipped, region.pad, status_name(bound.status), prefix::lookup_name(prefix::Lookup(bound.prefix_refusal)),
            shadow_.stream0, bound.vb_revision, static_cast<unsigned long>(vertex_count), static_cast<unsigned long>(bound.scanned),
            bound.box.centre[0], bound.box.centre[1], bound.box.centre[2], bound.box.half[0], bound.box.half[1], bound.box.half[2],
            long(region.rect.left), long(region.rect.top), long(region.rect.right), long(region.rect.bottom), permille, of_viewport ? "viewport" : "target",
            static_cast<unsigned long long>(hull_px), static_cast<unsigned long long>(aabb_px), static_cast<unsigned long long>(ticks));
}

// Capture frames only (zero cost otherwise: one bool test per admitted
// packed draw). The "dimmer bullets" diagnostic of screen-emission-region.md,
// step B: the composed target A sampled at the centre of the bound rectangle
// before the source draw (after prepare: A|R is copied to B|R, A itself is
// untouched until finish composes) and after the composite, through one
// documented GetRenderTargetData (whole surface: the destination must match
// the source's size) into two retained system-memory surfaces of the
// target's size and format (one pre, one post) and a 1x1 LockRect. At most
// packed_sample_cap admitted draws per capture frame are sampled (the first
// ones; the rest are counted in packed_sample_skipped on the frame line), so
// a frame with hundreds of bullet draws costs at most 2 * cap copies. The
// centre of a bracket around a thin beam is usually not a bullet pixel, so
// the post sample also scans the whole rectangle out of the two copies
// (scan_packed_rect): changed_px, the pre/post luminance maxima and sums and
// the location of the post maximum with its colours. The retained surfaces
// are allocated once per size/format, counted by device_references() and
// released with the witness copy at Reset/teardown.
static inline float packed_sample_half(std::uint16_t h) noexcept {
    // Bit-exact half decode for the per-pixel scan (no ldexp in the loop).
    const unsigned sign = unsigned(h) & 0x8000u, exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    unsigned bits;
    if (!exponent) {
        if (!mantissa) bits = sign << 16;
        else { unsigned e = 113, m = mantissa; while (!(m & 1024u)) { m <<= 1; --e; } bits = (sign << 16) | (e << 23) | ((m & 1023u) << 13); }
    } else if (exponent == 31) bits = (sign << 16) | 0x7f800000u | (mantissa << 13);
    else bits = (sign << 16) | ((exponent + 112u) << 23) | (mantissa << 13);
    float value; std::memcpy(&value, &bits, 4); return value;
}
static unsigned packed_sample_pixel_bytes(std::uint32_t format) noexcept {
    switch (D3DFORMAT(format)) {
    case D3DFMT_A16B16G16R16F: return 8;
    case D3DFMT_A32B32G32R32F: return 16;
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
    default: return 0; // fail closed: no decode for other formats
    }
}
HRESULT MotionOutput::sample_target_pixel(IDirect3DSurface9* surface, const renderer::Surface& description, bool pre, std::int32_t x, std::int32_t y, float out[4]) noexcept {
    out[0] = out[1] = out[2] = out[3] = 0.f;
    if (!surface || !description.known || !description.width || !description.height) return D3DERR_NOTFOUND;
    if (x < 0 || y < 0 || std::uint32_t(x) >= description.width || std::uint32_t(y) >= description.height) return D3DERR_INVALIDCALL;
    const auto format = D3DFORMAT(description.format);
    const unsigned bytes = packed_sample_pixel_bytes(description.format);
    if (!bytes) return D3DERR_NOTAVAILABLE;
    auto& s = packed_sample_;
    if ((s.copy || s.pre_copy) && (s.copy_width != description.width || s.copy_height != description.height || s.copy_format != description.format)) release_packed_sample();
    IDirect3DSurface9*& slot = pre ? s.pre_copy : s.copy;
    HRESULT hr = S_OK;
    if (!slot) {
        hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, description.width, description.height, format, D3DPOOL_SYSTEMMEM, &slot, nullptr);
        if (SUCCEEDED(hr)) { s.copy_width = description.width; s.copy_height = description.height; s.copy_format = description.format; }
        else slot = nullptr;
    }
    IDirect3DSurface9* copy = slot;
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, surface, copy);
    if (SUCCEEDED(hr)) {
        const RECT pixel{x, y, x + 1, y + 1};
        D3DLOCKED_RECT lock{};
        hr = copy->LockRect(&lock, &pixel, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            const auto* bits = static_cast<const unsigned char*>(lock.pBits);
            if (bytes == 16) std::memcpy(out, bits, 16);
            else if (bytes == 8) {
                // The same decoder the rectangle scan uses (packed_sample_half).
                for (unsigned c = 0; c < 4; ++c) {
                    std::uint16_t h = 0; std::memcpy(&h, bits + 2 * c, 2);
                    out[c] = packed_sample_half(h);
                }
            } else {
                DWORD v = 0; std::memcpy(&v, bits, 4);
                out[0] = float((v >> 16) & 255u) / 255.f; out[1] = float((v >> 8) & 255u) / 255.f; out[2] = float(v & 255u) / 255.f;
                out[3] = format == D3DFMT_X8R8G8B8 ? 1.f : float((v >> 24) & 255u) / 255.f;
            }
            copy->UnlockRect();
        }
    }
    return hr;
}
void MotionOutput::release_packed_sample() noexcept {
    auto& s = packed_sample_;
    release(s.copy); s.copy = nullptr;
    release(s.pre_copy); s.pre_copy = nullptr;
    s.copy_width = s.copy_height = s.copy_format = 0; s.valid = false;
}
// Capture frames only, at most packed_sample_cap rectangles per frame: one
// pass over the bound rectangle (clipped to the copies' extent) comparing the
// two retained system-memory images. RGB is compared on the raw bits (exact),
// luminance is Rec.709 on the decoded channels; no allocation and no D3D call
// beyond the two LockRects.
HRESULT MotionOutput::scan_packed_rect(const fade_region::Rect& rect, PackedScan& scan) noexcept {
    auto& s = packed_sample_;
    const unsigned bytes = packed_sample_pixel_bytes(s.copy_format);
    if (!s.copy || !s.pre_copy || !bytes) return D3DERR_NOTAVAILABLE;
    RECT box{rect.left > 0 ? LONG(rect.left) : 0, rect.top > 0 ? LONG(rect.top) : 0,
             rect.right < std::int32_t(s.copy_width) ? LONG(rect.right) : LONG(s.copy_width),
             rect.bottom < std::int32_t(s.copy_height) ? LONG(rect.bottom) : LONG(s.copy_height)};
    if (box.right <= box.left || box.bottom <= box.top) return D3DERR_INVALIDCALL;
    D3DLOCKED_RECT pre_lock{}, post_lock{};
    HRESULT hr = s.pre_copy->LockRect(&pre_lock, &box, D3DLOCK_READONLY);
    if (FAILED(hr)) return hr;
    hr = s.copy->LockRect(&post_lock, &box, D3DLOCK_READONLY);
    if (FAILED(hr)) { s.pre_copy->UnlockRect(); return hr; }
    const unsigned width = unsigned(box.right - box.left), height = unsigned(box.bottom - box.top);
    const unsigned rgb_bytes = bytes == 4 ? 3u : bytes / 4u * 3u;
    double max_pre = -1.0, max_post = -1.0;
    for (unsigned row = 0; row < height; ++row) {
        const auto* a = static_cast<const unsigned char*>(pre_lock.pBits) + std::size_t(row) * std::size_t(pre_lock.Pitch);
        const auto* b = static_cast<const unsigned char*>(post_lock.pBits) + std::size_t(row) * std::size_t(post_lock.Pitch);
        for (unsigned column = 0; column < width; ++column, a += bytes, b += bytes) {
            float pre_rgb[3], post_rgb[3];
            if (bytes == 8) for (unsigned c = 0; c < 3; ++c) {
                std::uint16_t ha = 0, hb = 0; std::memcpy(&ha, a + 2 * c, 2); std::memcpy(&hb, b + 2 * c, 2);
                pre_rgb[c] = packed_sample_half(ha); post_rgb[c] = packed_sample_half(hb);
            } else if (bytes == 16) { std::memcpy(pre_rgb, a, 12); std::memcpy(post_rgb, b, 12); }
            else {
                std::uint32_t va = 0, vb = 0; std::memcpy(&va, a, 4); std::memcpy(&vb, b, 4);
                pre_rgb[0] = float((va >> 16) & 255u) / 255.f; pre_rgb[1] = float((va >> 8) & 255u) / 255.f; pre_rgb[2] = float(va & 255u) / 255.f;
                post_rgb[0] = float((vb >> 16) & 255u) / 255.f; post_rgb[1] = float((vb >> 8) & 255u) / 255.f; post_rgb[2] = float(vb & 255u) / 255.f;
            }
            const bool changed = std::memcmp(a, b, rgb_bytes) != 0; // A8R8G8B8 stores B,G,R,A: the first three bytes are the colour
            if (changed) ++scan.changed;
            const double pre_y = 0.2126 * pre_rgb[0] + 0.7152 * pre_rgb[1] + 0.0722 * pre_rgb[2];
            const double post_y = 0.2126 * post_rgb[0] + 0.7152 * post_rgb[1] + 0.0722 * post_rgb[2];
            scan.sum_pre += pre_y; scan.sum_post += post_y;
            if (pre_y > max_pre) max_pre = pre_y;
            if (post_y > max_post) {
                max_post = post_y;
                scan.argmax_x = std::int32_t(box.left) + std::int32_t(column); scan.argmax_y = std::int32_t(box.top) + std::int32_t(row);
                for (unsigned c = 0; c < 3; ++c) { scan.argmax_pre[c] = pre_rgb[c]; scan.argmax_post[c] = post_rgb[c]; }
            }
        }
    }
    scan.max_pre = max_pre < 0.0 ? 0.0 : max_pre; scan.max_post = max_post < 0.0 ? 0.0 : max_post;
    scan.pixels = static_cast<unsigned long>(width) * height;
    s.copy->UnlockRect(); s.pre_copy->UnlockRect();
    return S_OK;
}
void MotionOutput::sample_packed_pre(const MotionRoute& route) noexcept {
    auto& s = packed_sample_;
    if (s.sampled >= packed_sample_cap) { ++composition_counts_.packed_sample_skipped; return; }
    ++s.sampled;
    s.rect = route.prefix_region.rect;
    s.clipped = route.prefix_region.clipped;
    s.index = counters_.draws;
    s.x = s.rect.left + (s.rect.right - s.rect.left) / 2; s.y = s.rect.top + (s.rect.bottom - s.rect.top) / 2;
    // A target size/format change releases both copies inside the readback and
    // clears the pending pre, so the slot is armed after it: every admitted
    // sampled draw keeps its packed_sample line (with the readback's result),
    // and packed_sample_skipped stays the cap's counter alone.
    s.pre_result = sample_target_pixel(hdr_ ? hdr_->target() : nullptr, hdr_target_, true, s.x, s.y, s.pre);
    s.valid = true;
}
void MotionOutput::sample_packed_post(const RECT& composed) noexcept {
    auto& s = packed_sample_;
    if (!s.valid) return;
    s.valid = false;
    float post[4];
    const HRESULT post_result = sample_target_pixel(hdr_ ? hdr_->target() : nullptr, hdr_target_, false, s.x, s.y, post);
    PackedScan scan{};
    // Both readbacks must have landed for the rectangle comparison to mean
    // anything; a failed one leaves the scan fields at zero with its result.
    scan.result = SUCCEEDED(s.pre_result) && SUCCEEDED(post_result) ? scan_packed_rect(s.rect, scan) : post_result;
    auto luminance = [](const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
    log("packed_sample device=%llu frame=%llu index=%lu rect=%ld,%ld,%ld,%ld clipped=%u composed=%ld,%ld,%ld,%ld centre=%ld,%ld format=%u pre=%.6g,%.6g,%.6g,%.6g pre_y=%.6g post=%.6g,%.6g,%.6g,%.6g post_y=%.6g pre_result=%08lx post_result=%08lx"
        " scan_result=%08lx scan_px=%lu changed_px=%lu max_pre_y=%.6g max_post_y=%.6g sum_pre_y=%.6g sum_post_y=%.6g argmax=%ld,%ld argmax_pre=%.6g,%.6g,%.6g argmax_post=%.6g,%.6g,%.6g",
        id_, frame_, static_cast<unsigned long>(s.index), long(s.rect.left), long(s.rect.top), long(s.rect.right), long(s.rect.bottom), s.clipped,
        long(composed.left), long(composed.top), long(composed.right), long(composed.bottom), long(s.x), long(s.y), unsigned(hdr_target_.format),
        double(s.pre[0]), double(s.pre[1]), double(s.pre[2]), double(s.pre[3]), luminance(s.pre),
        double(post[0]), double(post[1]), double(post[2]), double(post[3]), luminance(post), static_cast<unsigned long>(s.pre_result), static_cast<unsigned long>(post_result),
        static_cast<unsigned long>(scan.result), scan.pixels, scan.changed, scan.max_pre, scan.max_post, scan.sum_pre, scan.sum_post,
        long(scan.argmax_x), long(scan.argmax_y),
        double(scan.argmax_pre[0]), double(scan.argmax_pre[1]), double(scan.argmax_pre[2]),
        double(scan.argmax_post[0]), double(scan.argmax_post[1]), double(scan.argmax_post[2]));
}

// Capture frames only. The draw was recognised (fade pair in the exact
// source-over state) and refused by admission (1-5); the step-1 rectangle is
// derived as for an admitted draw but through the read-only bound lookup
// (no insert, stamp, poison or eviction) and kept as integers. Nothing is
// composed and no composition counter, table counter, witness slot or route
// field changes.
void MotionOutput::record_fade_refused(const MotionRoute& route, unsigned refusal) noexcept {
    const unsigned slot = fade_refused_count_++;
    if (slot >= fade_refused_capacity) return;
    fade_region::Result bound{};
    bool of_viewport = false;
    unsigned permille = 0;
    const auto region = fade_rectangle(route, bound, of_viewport, permille, true);
    auto& r = fade_refused_[slot];
    r.vs = shadow_.vs_hash; r.ps = shadow_.ps_hash;
    r.index = std::uint32_t(counters_.draws);
    r.refusal = std::uint8_t(refusal);
    // Gate 4 refused the draw before scope sampling filled the key: read the
    // object context once, without the lifetime lookup (identity only).
    r.node = 0; r.model = 0; r.lod = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if (fixture_configured_) {
        if (fixture_.scope.known) { r.node = fixture_.scope.node; r.model = std::uint32_t(fixture_.scope.model); r.lod = std::uint32_t(fixture_.scope.lod); }
    } else
#endif
    {
        object_trace::Snapshot scope{};
        if (object_trace::current(&scope, false) && (scope.valid & object_trace::Node)) { r.node = scope.node; r.model = scope.model; r.lod = scope.lod; }
    }
    r.rect[0] = region.rect.left; r.rect[1] = region.rect.top; r.rect[2] = region.rect.right; r.rect[3] = region.rect.bottom;
    r.permille = permille; r.reason = std::uint8_t(region.reason); r.status = std::uint8_t(bound.status);
    r.bound = region.bound; r.of_viewport = of_viewport;
}
void MotionOutput::log_fade_refused() noexcept {
    const unsigned logged = fade_refused_count_ < fade_refused_capacity ? fade_refused_count_ : fade_refused_capacity;
    for (unsigned i = 0; i < logged; ++i) {
        const auto& r = fade_refused_[i];
        log("fade_refused_rect device=%llu frame=%llu index=%lu refusal=%u vs=%016llx ps=%016llx node=%llu model=%08lx lod=%08lx"
            " bound=%u reason=%u status=%s rect=%ld,%ld,%ld,%ld f_permille=%lu f_of=%s refused_total=%u",
            id_, frame_, static_cast<unsigned long>(r.index), unsigned(r.refusal), r.vs, r.ps, r.node,
            static_cast<unsigned long>(r.model), static_cast<unsigned long>(r.lod), unsigned(r.bound), unsigned(r.reason),
            fade_region::status_name(fade_region::Status(r.status)), long(r.rect[0]), long(r.rect[1]), long(r.rect[2]), long(r.rect[3]),
            static_cast<unsigned long>(r.permille), r.of_viewport ? "viewport" : "target", fade_refused_count_);
    }
    fade_refused_count_ = 0;
}

// Tested-opaque-arm admission of the two cutout pairs, observable per frame
// (linear_material_frame cutout_opaque_*). Reached only from after_draw's one
// shadow_.cutout_pair branch, which is false whenever linear materials are off
// (the flag is set from linear_material_requested_ at pair latch), and only
// while the exact cutout arm is inactive, so route.cutout is false here and
// these draws are otherwise indistinguishable from ordinary routed ones.
// Count-only: no allocation, no device call, no getter. A routed draw counts on
// success like cutout_routed; the lane bucket is the draws that bound the sun
// lane variant with the share extraction (route.sun_receiver), meaning oC2.g
// carried this draw's share. A refused draw counts its route reason: the
// route's own SunUntrackedReason when it recorded one (the lane's diagnostics
// only fill it for a colour writer), otherwise the gate it stopped at.
void MotionOutput::note_cutout_opaque(const MotionRoute& route, HRESULT result) noexcept {
    if (route.routed) {
        if (FAILED(result)) return;
        ++counters_.cutout_opaque_routed;
        if (route.sun_receiver) ++counters_.cutout_opaque_lane;
        return;
    }
    ++counters_.cutout_opaque_refused;
    unsigned reason = route.sun_refusal;
    if (!reason) switch (route.gate) { // no reason recorded: the gate's own bucket
        case MotionGate::Feature: reason = unsigned(renderer::SunUntrackedReason::Feature); break;
        case MotionGate::Scene: reason = unsigned(renderer::SunUntrackedReason::Scene); break;
        case MotionGate::Pair: reason = unsigned(renderer::SunUntrackedReason::Pair); break;
        case MotionGate::DrawState: reason = unsigned(renderer::SunUntrackedReason::State); break;
        case MotionGate::Scope: reason = unsigned(renderer::SunUntrackedReason::Scope); break;
        case MotionGate::History: reason = unsigned(renderer::SunUntrackedReason::History); break;
        default: break; // MotionGate::None without a reason: apply rollback before it was recorded
    }
    ++counters_.cutout_opaque_reasons[reason < renderer::sun_untracked_reason_count ? reason : 0u];
}

void MotionOutput::after_draw(MotionRoute& route, HRESULT result) noexcept {
    if (!enabled_ || !route.evaluated) return;
    const bool jittered = route.jittered;
    if (route.composition) finish_composition(result, route.composition_policy);
    if (route.screen_additive) finish_screen_additive(route);
    if(sun_lane_active_&&route.sun_color_writer){
        const bool coverage=route.composition&&sun_composition_completed_&&sun_coverage_current_&&composition_&&composition_->coverage_valid()&&
            !composition_frame_stopped_&&!composition_state_lost_&&!composition_quarantined_;
        // Diagnostics only: the refusal reason travels with the draw; the
        // bookkeeping decides untracked exactly as before.
        const auto reason=route.routed
            ?(route.fade_arm?renderer::SunUntrackedReason::FadeArm:!route.depth?renderer::SunUntrackedReason::NoDepth:renderer::SunUntrackedReason(route.sun_refusal))
            :renderer::SunUntrackedReason(route.sun_refusal);
        // Only an actual depth writer (z test and z write on, both read) can
        // veto: a color-only draw leaves the tracked depth intact.
        if(sun_frame_.draw(route.submit&&SUCCEEDED(result),route.routed&&route.depth&&!route.fade_arm&&route.sun_receiver,coverage,route.routed&&route.depth&&!route.fade_arm,reason,(route.sun_z_state&7u)==7u))
            note_sun_untracked_writer(route,reason);
    }
    if (cutout::missed(route.cutout_candidate, route.submit, SUCCEEDED(result), route.routed || route.composition,
            route.cutout_test_known, route.cutout_test, route.cutout_color_known, route.cutout_color,
            route.cutout_alpha_known, route.cutout_alpha, route.cutout_z_known, route.cutout_z,
            route.cutout_zfunc_known, route.cutout_zfunc, route.cutout_source_over)) {
        cutout_coverage_missed_ = true; ++counters_.cutout_missed; invalidate_taa(TaaInvalidateSite::CutoutMissed);
    }
    if (route.routed && route.cutout && SUCCEEDED(result)) ++counters_.cutout_routed;
    // One branch with linear materials off (shadow_.cutout_pair is false then).
    if (shadow_.cutout_pair && !cutout_arm_active_) note_cutout_opaque(route, result);
    if (route.routed && route.original_fill && SUCCEEDED(result)) ++original_fill_draws_;
    if (route.source_gain) finish_source_gain(route);
    if (candidates_requested_ && route.routed && SUCCEEDED(result)) note_candidate_draw(route);
    if (route.routed) {
        // route_draw: the apply (before_draw) plus this undo, without the
        // native draw between them and without the jitter writes.
        const std::uint64_t begin = draw_stamp();
        undo(route);
        route.ticks += draw_stamp() - begin;
        counters_.route_draw_ticks += route.ticks;
        record(unsigned(telemetry::Metric::RouteDraw), route.ticks);
    }
    // Hooks off: no SetSamplerState hook can put the bias back ahead of an
    // application LODBIAS write, so it comes back here, one native
    // SetSamplerState per biased stage (the mask is empty otherwise).
    if (!state_hooks_ && sampler_biased_mask_) restore_mip_bias();
    if (route.jittered && !composition_state_lost_) restore_jitter(route);
    if (pending_valid_) { pending_valid_ = false; observe(pending_, result); }
    if (shimmer_trace_ && route.scene && shadow_.asteroid_pair) record_shimmer_draw(route);
    if (capture_ && route.scene) {
        const auto& k = route.key;
        log("motion_route device=%llu frame=%llu index=%lu gate=%u routed=%u matched=%u depth=%u jittered=%u vs=%016llx ps=%016llx node=%p camera=%p node_handle=%lu camera_handle=%lu node_serial=%llu camera_serial=%llu load_epoch=%llu registry_epoch=%llu model=%08lx lod=%08lx vb=%llu ib=%llu declaration=%016llx offset=%u stride=%u position_offset=%u position_type=%u topology=%u first=%u primitives=%u base_vertex=%d min_vertex=%u vertex_count=%u indexed=%u pass=%lu rows_hash=%016llx result=%08lx"
            " zwrite=%ld blend=%ld src=%ld dst=%ld atest=%ld mask=%ld sepalpha=%ld fog=%ld fade_arm=%u fade_permille=%u fade_held=%u",
            id_, frame_, counters_.draws, unsigned(route.gate), route.routed, route.matched, route.routed && route.depth, jittered, shadow_.vs_hash, shadow_.ps_hash,
            reinterpret_cast<void*>(k.node), reinterpret_cast<void*>(k.camera), static_cast<unsigned long>(k.node_handle),
            static_cast<unsigned long>(k.camera_handle), k.object_lifetime, k.camera_lifetime, route.load_epoch, route.registry_epoch,
            static_cast<unsigned long>(k.model), static_cast<unsigned long>(k.lod), k.vertex_buffer, k.index_buffer, k.declaration,
            k.stream_offset, k.stride, k.position_offset, k.position_type, k.topology, k.first, k.primitives, k.base_vertex,
            k.min_vertex, k.vertex_count, k.indexed, static_cast<unsigned long>(k.pass), route.rows_hash, result,
            shadow_state_field(D3DRS_ZWRITEENABLE), shadow_state_field(D3DRS_ALPHABLENDENABLE),
            composition_blend_field(0), composition_blend_field(1),
            shadow_state_field(D3DRS_ALPHATESTENABLE), shadow_state_field(D3DRS_COLORWRITEENABLE),
            composition_blend_field(3), shadow_state_field(D3DRS_FOGENABLE), unsigned(route.fade_arm), route.fade_permille, unsigned(route.fade_held));
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
    if (composition_state_lost_ || motion_state_lost_) return surface;
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
    if (composition_state_lost_ || motion_state_lost_) return;
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
    probe_cutout_caps(); // a transient verdict retries at this boundary, never a draw
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
    if (write && composition_enhanced_ && !composition_terminal_export_ && !composition_diagnostic_export_) composition_export();
    if (write && composition_enhanced_) composition_published_ = true;
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
    if (composition_state_lost_ || motion_state_lost_ || hdr_state_ != HdrState::Active || !hdr_dirty_ || !hdr_ || !hdr_main_) return;
    ++counters_.hdr.flushes;
    hdr_writeback(hdr_->target(), true);
}
void MotionOutput::end_redirect(HdrEnd reason, MotionHdrSceneCallback callback, void* context) noexcept {
    if (composition_state_lost_ || motion_state_lost_ || hdr_state_ == HdrState::Off) return;
    composition_terminal_export_ = reason == HdrEnd::Hook || reason == HdrEnd::BloomCopy || reason == HdrEnd::Present;
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
    release_composition_identity();
    release(hdr_main_);
    hdr_state_ = HdrState::Off; hdr_dirty_ = false; hdr_latch_pending_ = false; hdr_pending_state_ = 0; hdr_resolved_ = nullptr;
    counters_.hdr.end = std::uint32_t(reason);
    composition_terminal_export_ = false;
}
void MotionOutput::drop_redirect() noexcept {
    hdr_resolved_ = nullptr;
    if (hdr_state_ == HdrState::Off) { release_composition_identity(); release(hdr_main_); return; }
    release_composition_identity();
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
        readback_surface(depth_surface_, sun_lane_active_?D3DFMT_G32R32F:D3DFMT_R32F, sun_lane_active_?8:4, L"depth", sun_lane_active_?L"rg32f":L"r32f", "motion_output_depth_readback", sun_lane_active_?"rg32f_row_major":"r32f_row_major", target_width_, target_height_);
    if(sun_lane_active_&&sun_frame_.available&&sun_frame_.coverage_required&&sun_coverage_current_&&composition_)
        readback_surface(composition_->coverage_target(),D3DFMT_A16B16G16R16F,8,L"sun_coverage",L"rgba16f","sun_shadow_lane_coverage_readback","rgba16f_row_major",target_width_,target_height_);
}

// One read of the engine's projection and view buffers (camera_state.cpp:
// two validated 64-byte copies) into the scene or the background slot.
void MotionOutput::read_camera(bool scene) noexcept {
    // The resolve's consumer, or the caster-candidate counter (its slice-0 test
    // and the depth replay's cascade-0 projection need the scene latch, W1).
    if (!(taa_enabled_ || candidates_requested_) || !camera_state::available()) return;
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
        " history_view_valid=%u history_view_frame=%llu rotation_deg=%.4f policy=%lu reason=%lu camera_cut=%u mode=%u cut_deg=%.2f prev_valid_at_policy=%u",
        id_, frame_, camera_state::status(), static_cast<unsigned long>(counters_.camera_reads), c.valid,
        static_cast<unsigned long>(counters_.camera_read_failure), static_cast<unsigned long>(counters_.camera_failure),
        reinterpret_cast<void*>(camera_projection_address_), reinterpret_cast<void*>(camera_view_address_),
        c.m00, c.m11, c.m20, c.m21, c.r[0], c.r[1], c.r[2], c.r[3], c.r[4], c.r[5], c.r[6], c.r[7], c.r[8], c.t[0], c.t[1], c.t[2],
        b.valid, b.m00, b.m11, background_rotation,
        camera_previous_.valid, camera_previous_frame_, t.camera_rotation_deg, static_cast<unsigned long>(t.camera_policy),
        static_cast<unsigned long>(t.camera_reason), t.camera_cut, unsigned(sentinel_mode_), camera_cut_degrees_, t.camera_previous_valid);
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
        if (!t.resolved) invalidate_taa(TaaInvalidateSite::NotResolved);
    } else t.skip = unsigned(TaaSkip::Disabled);
    witness_readback();
    if (capture_) readback();
}
// Fade-region witness (X3M_FADE_WITNESS=k): on every k-th frame the M
// coverage target (A16B16G16R16F, cleared once per frame by begin_frame and
// written only by the admitted sources' raster) is copied once to system
// memory through GetRenderTargetData and every covered pixel (any of x, y, z
// positive) is tested against the union of this frame's derived rectangles
// of prepared draws (an overflowing frame counts on and takes the whole
// target as its union). Sampled only when the frame admitted fade draws and no emission draw, and
// the coverage is valid (not stopped, not quarantined). The copy surface and
// the row of union flags are retained between samples and dropped at Reset
// and retirement; nothing here runs while the witness is off. Integers only
// in the log line; the readback time goes into the frame's readback counters.
void MotionOutput::witness_readback() noexcept {
    if (!fade_witness_interval_ || frame_ % fade_witness_interval_ != 0) return;
    auto& w = fade_witness_;
    const auto& cc = composition_counts_;
    // Exchange (emission) brackets rewrite M whole; in-place fade and packed
    // brackets add their rectangles to the union instead.
    const unsigned emission_prepared = cc.prepared - cc.prepared_fade - cc.packed_admitted;
    IDirect3DSurface9* mask = composition_ ? composition_->coverage_target() : nullptr;
    const char* reason = "sampled";
    if ((!distance_fade_requested_ && !screen_emission_requested_) || !composition_ || !mask) reason = "no_pass";
    else if (!w.count) reason = "no_fade";
    else if (emission_prepared) reason = "emission";
    else if (composition_frame_stopped_ || composition_quarantined_ || composition_state_lost_ || !composition_->coverage_valid()) reason = "mask_invalid";
    HRESULT hr = S_FALSE;
    UINT width = 0, height = 0;
    unsigned covered = 0, outside = 0;
    std::uint64_t union_area = 0;
    const bool sample = reason[0] == 's';
    if (sample) {
        const std::uint64_t begin = stamp();
        D3DSURFACE_DESC desc{};
        hr = mask->GetDesc(&desc);
        if (SUCCEEDED(hr) && (desc.Format != D3DFMT_A16B16G16R16F || !desc.Width || !desc.Height)) hr = D3DERR_INVALIDCALL;
        if (SUCCEEDED(hr)) {
            width = desc.Width; height = desc.Height;
            if (w.copy && (w.copy_width != width || w.copy_height != height)) release_fade_witness();
            if (!w.copy) {
                hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, width, height, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &w.copy, nullptr);
                if (SUCCEEDED(hr)) {
                    w.copy_width = width; w.copy_height = height;
                    w.row = new (std::nothrow) unsigned char[width]; w.row_width = w.row ? width : 0u;
                    if (!w.row) { release_fade_witness(); hr = E_OUTOFMEMORY; }
                } else { w.copy = nullptr; }
            }
        }
        if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, mask, w.copy);
        D3DLOCKED_RECT lock{};
        if (SUCCEEDED(hr)) hr = w.copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) {
            const unsigned stored = w.overflow ? FadeWitness::rect_capacity : w.count;
            for (UINT y = 0; y < height; ++y) {
                std::memset(w.row, w.overflow ? 1 : 0, width); // overflow: the whole target is the union
                for (unsigned i = 0; i < stored && !w.overflow; ++i) {
                    if (!w.prepared[i]) continue; // an unprepared draw wrote nothing through the pass
                    const auto& r = w.rects[i];
                    if (std::int32_t(y) < r.top || std::int32_t(y) >= r.bottom) continue;
                    const std::int32_t left = r.left < 0 ? 0 : r.left, right = r.right > std::int32_t(width) ? std::int32_t(width) : r.right;
                    if (left < right) std::memset(w.row + left, 1, std::size_t(right - left));
                }
                const auto* pixels = reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits) + std::size_t(y) * lock.Pitch);
                for (UINT x = 0; x < width; ++x) {
                    union_area += w.row[x];
                    const std::uint16_t* p = pixels + std::size_t(x) * 4;
                    bool hit = false;
                    for (unsigned c = 0; c < 3; ++c) hit = hit || (!(p[c] & 0x8000u) && (p[c] & 0x7fffu));
                    if (!hit) continue;
                    ++covered;
                    if (!w.row[x]) ++outside;
                }
            }
            w.copy->UnlockRect();
        }
        const std::uint64_t ticks = stamp() - begin;
        ++counters_.readbacks; counters_.readback_ticks += ticks;
        record(unsigned(telemetry::Metric::RouteReadback), ticks, FAILED(hr), std::uint64_t(width) * height * 8u);
    }
    log("fade_witness device=%llu frame=%llu k=%u sampled=%u reason=%s result=%08lx width=%u height=%u rects=%u rects_prepared=%u rects_unprepared=%u overflow=%u lines_truncated=%u covered=%u outside=%u union=%llu fade_prepared=%u emission_prepared=%u packed_prepared=%u f_hist=%u,%u,%u,%u,%u,%u,%u,%u",
        id_, frame_, fade_witness_interval_, unsigned(sample), reason, hr, unsigned(width), unsigned(height), w.count, w.prepared_count,
        w.count - w.prepared_count, unsigned(w.overflow), w.count > w.logged ? w.count - w.logged : 0u, covered, outside,
        static_cast<unsigned long long>(union_area), cc.prepared_fade, emission_prepared, cc.packed_admitted,
        w.f_hist[0], w.f_hist[1], w.f_hist[2], w.f_hist[3], w.f_hist[4], w.f_hist[5], w.f_hist[6], w.f_hist[7]);
}
void MotionOutput::release_fade_witness() noexcept {
    auto& w = fade_witness_;
    release(w.copy); w.copy = nullptr; w.copy_width = w.copy_height = 0;
    delete[] w.row; w.row = nullptr; w.row_width = 0;
}
// Distant-shimmer trace (X3M_SHIMMER_TRACE=1, docs/architecture/
// linear-distance-fade-region.md, "Shimmer trace (diagnostic)"): the draw
// hook copies integers into a fixed per-frame array (no formatting, no
// allocation, no locking, no floating point); the whole frame is formatted
// once after Present, where the full CPU boundary already holds.
void MotionOutput::record_shimmer_draw(const MotionRoute& route) noexcept {
    const unsigned slot = shimmer_count_++;
    if (slot >= shimmer_draw_capacity) return;   // beyond the capacity the frame only counts
    auto& r = shimmer_draws_[slot];
    const auto& k = route.key;
    r.node = k.node;
    r.index = std::uint32_t(counters_.draws);
    r.model = std::uint32_t(k.model); r.lod = std::uint32_t(k.lod);
    r.vertex_count = std::uint32_t(k.vertex_count); r.primitives = std::uint32_t(k.primitives);
    r.topology = std::uint32_t(k.topology);
    r.vertex_buffer = k.vertex_buffer; r.index_buffer = k.index_buffer;
    r.gate = std::uint8_t(route.gate);
    r.routed = route.routed; r.composition = route.composition; r.indexed = k.indexed != 0;
    r.f_permille = route.fade_region_evaluated ? std::int32_t(route.fade_region_permille) : -1;
    r.region_known = route.fade_region_evaluated && route.fade_region.bound;
    r.rect[0] = route.fade_region.rect.left; r.rect[1] = route.fade_region.rect.top;
    r.rect[2] = route.fade_region.rect.right; r.rect[3] = route.fade_region.rect.bottom;
}
namespace {
// Indices a D3D9 primitive count consumes; 0 for a non-indexed draw.
unsigned shimmer_index_count(unsigned topology, unsigned primitives) noexcept {
    switch (topology) {
    case D3DPT_POINTLIST: return primitives;
    case D3DPT_LINELIST: return primitives * 2u;
    case D3DPT_LINESTRIP: return primitives + 1u;
    case D3DPT_TRIANGLELIST: return primitives * 3u;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return primitives + 2u;
    default: return 0u;
    }
}
} // namespace
void MotionOutput::log_shimmer_frame(unsigned history_previous, unsigned history_current, unsigned committed) noexcept {
    const auto& t = counters_.taa;
    const auto& c = camera_scene_;
    // Scaled integers, never a formatted float. float*float and the 32-bit
    // truncation are SSE (cvttss2si); a 64-bit truncation would be x87 here.
    const long p00 = c.valid ? long(std::int32_t(c.m00 * 10000.f)) : 0l;
    const long p11 = c.valid ? long(std::int32_t(c.m11 * 10000.f)) : 0l;
    const unsigned logged = shimmer_count_ < shimmer_draw_capacity ? shimmer_count_ : shimmer_draw_capacity;
    log("shimmer_frame device=%llu frame=%llu draws=%lu asteroid=%u logged=%u truncated=%u taa=%u taa_attempted=%u"
        " taa_resolved=%u taa_history=%u taa_skip=%lu cut=%u camera_cut=%u jitter=%u jitter_index=%u"
        " history_previous=%u history_current=%u committed=%u camera_valid=%u p00_e4=%ld p11_e4=%ld",
        id_, frame_, static_cast<unsigned long>(counters_.draws), shimmer_count_, logged, shimmer_count_ - logged,
        taa_enabled_, t.attempted, t.resolved, t.used_history, static_cast<unsigned long>(t.skip),
        counters_.cut, t.camera_cut, counters_.jitter_active, counters_.jitter_index,
        history_previous, history_current, committed, c.valid, p00, p11);
    for (unsigned i = 0; i < logged; ++i) {
        const auto& r = shimmer_draws_[i];
        log("shimmer_draw device=%llu frame=%llu index=%lu gate=%u routed=%u composition=%u node=%llu model=%08lx lod=%08lx"
            " vb=%llu ib=%llu topology=%u indexed=%u vertex_count=%lu index_count=%lu primitives=%lu f_permille=%ld"
            " region=%u rect=%ld,%ld,%ld,%ld",
            id_, frame_, static_cast<unsigned long>(r.index), unsigned(r.gate), unsigned(r.routed), unsigned(r.composition),
            r.node, static_cast<unsigned long>(r.model), static_cast<unsigned long>(r.lod),
            r.vertex_buffer, r.index_buffer, r.topology, unsigned(r.indexed),
            static_cast<unsigned long>(r.vertex_count),
            static_cast<unsigned long>(r.indexed ? shimmer_index_count(r.topology, r.primitives) : 0u),
            static_cast<unsigned long>(r.primitives), static_cast<long>(r.f_permille), unsigned(r.region_known),
            long(r.rect[0]), long(r.rect[1]), long(r.rect[2]), long(r.rect[3]));
    }
}
// One diagnostic line per Present with X3M_SCREEN_EMISSION_TIMING=1 (the
// screen-emission option's opt-in timing): this frame's packed admissions and
// bracket pixels with the wall-clock time since the previous Present. One
// QueryPerformanceCounter and one log call; the frequency is read once.
void MotionOutput::log_screen_emission_frame() noexcept {
    LARGE_INTEGER now{};
    const std::uint64_t stamp = QueryPerformanceCounter(&now) ? std::uint64_t(now.QuadPart) : 0u;
    if (!qpc_frequency_) { LARGE_INTEGER f{}; if (QueryPerformanceFrequency(&f) && f.QuadPart > 0) qpc_frequency_ = std::uint64_t(f.QuadPart); }
    const std::uint64_t ticks = stamp > present_qpc_ && present_qpc_ ? stamp - present_qpc_ : 0u;
    present_qpc_ = stamp;
    log("screen_emission_frame device=%llu frame=%llu packed_admitted=%u brackets_px=%llu cpu_us=%llu",
        id_, frame_, composition_counts_.packed_admitted, composition_counts_.packed_region_pixels,
        qpc_frequency_ ? ticks * 1000000u / qpc_frequency_ : 0u);
}
// One line per Present with telemetry on (the option itself stays free of
// per-frame logging): which of the nine pairs actually drew additively this
// frame, how many draws were refused, and whether the runtime key has the
// option on. `pairs` is a hex bit mask over screen_emission::pairs indices.
void MotionOutput::log_screen_additive_frame() noexcept {
    log("screen_emission_additive_frame device=%llu frame=%llu admitted=%u refused=%u pairs=%03x toggled=%u",
        id_, frame_, screen_additive_frame_admitted_, screen_additive_frame_refused_,
        screen_additive_frame_pairs_, unsigned(screen_additive_enabled_));
}
void MotionOutput::after_present(HRESULT result) noexcept {
    report_xt_default_unavailable();
    report_mip_bias_game_write_failure();
    release_mip_bias_retry_bound();
    if (!enabled_) return;
    if (FAILED(result)) invalidate_taa(TaaInvalidateSite::PresentFailed);
    const bool committed = history_.commit(SUCCEEDED(result) && counters_.filled);
    const auto stats = history_.stats();
    if (shimmer_trace_) log_shimmer_frame(unsigned(stats.previous), unsigned(stats.current), unsigned(committed));
    if (screen_emission_timing_) log_screen_emission_frame();
    if (screen_additive_requested_) {
        if (telemetry_) log_screen_additive_frame();
        screen_additive_frame_admitted_ = screen_additive_frame_refused_ = screen_additive_frame_pairs_ = 0; // this frame only
    }
    if (fade_refused_count_) log_fade_refused();
    if (emission_source_gain_requested_) {
        // One line per frame that saw at least one candidate draw (an eligible
        // pair reaching prepare_source_gain), capture or not: run 26's 16-line
        // sample cap hid the totals. refused_other = unknown + state + bind.
        const auto& g = source_gain_counts_;
        const std::uint32_t other = g.refused_unknown + g.refused_state + g.bind_failures;
        if (g.admitted || g.refused_blend || g.refused_screen || other)
            log("emission_source_gain_frame device=%llu frame=%llu gain=%g admitted=%u admitted_screen=%u refused_blend=%u refused_screen=%u refused_other=%u refused_unknown=%u refused_state=%u bind_failures=%u",
                id_, frame_, double(emission_source_gain_), g.admitted, g.admitted_screen, g.refused_blend, g.refused_screen, other, g.refused_unknown, g.refused_state, g.bind_failures);
        source_gain_counts_ = {}; // this frame only, logged or not
    }
    if (original_fill_requested_ && original_fill_draws_) {
        log("original_fill_frame device=%llu frame=%llu fill=%g admitted=%u", id_, frame_, double(original_fill_), original_fill_draws_);
        original_fill_draws_ = 0;
    }
    if (capture_ || (telemetry_ && frame_ % frame_log_interval_ == 0)) {
        // Appended cost fields (totals for this frame; docs/verification/telemetry.md):
        // counts are exact, the *_us totals are CPU-side QPC wall clock with
        // telemetry on (timing=cpu_qpc) and zero otherwise (timing=off).
        const auto& c = counters_;
        if (composition_requested())
            log("%s_frame device=%llu frame=%llu prepared=%u linear=%u native=%u incomplete=%u refused=%u suppressed=%u exports=%u quarantine=%u state_lost=%u mask_valid=%u fade_eligible=%u fade_prepared=%u fade_linear=%u pool_traffic_estimate_bytes=%llu in_place=%u in_place_linear=%u in_place_incomplete=%u region_pixels=%llu"
                " packed_eligible=%u packed_admitted=%u packed_linear=%u packed_incomplete=%u packed_unbounded_refused=%u packed_caps_refused=%u packed_region_pixels=%llu packed_sample_skipped=%u",
                distance_fade_requested_ || screen_emission_requested_ ? "linear_composition" : "linear_emission", id_, frame_, composition_counts_.prepared, composition_counts_.linear, composition_counts_.native, composition_counts_.incomplete,
                composition_counts_.refused, composition_counts_.suppressed, composition_counts_.exports, composition_quarantined_, composition_state_lost_,
                composition_ && composition_->coverage_valid() && !composition_frame_stopped_ && !composition_quarantined_,
                composition_counts_.eligible_fade, composition_counts_.prepared_fade, composition_counts_.linear_fade, composition_counts_.pool_traffic_bytes,
                composition_counts_.in_place, composition_counts_.in_place_linear, composition_counts_.in_place_incomplete, composition_counts_.region_pixels,
                composition_counts_.packed_eligible, composition_counts_.packed_admitted, composition_counts_.packed_linear, composition_counts_.packed_incomplete,
                composition_counts_.packed_unbounded_refused, composition_counts_.packed_caps_refused, composition_counts_.packed_region_pixels, composition_counts_.packed_sample_skipped);
        if (distance_fade_requested_ && (composition_counts_.region_bound || composition_counts_.region_full))
            log("fade_region_frame device=%llu frame=%llu bound=%u full=%u hit=%u miss=%u poisoned=%u evicted=%u f_mean=%.4f reason_viewport=%u reason_rows=%u reason_unknown=%u reason_w=%u reason_nonfinite=%u reason_fill=%u status_no_table=%u status_no_scope=%u status_content=%u status_poisoned=%u status_read=%u status_back_link=%u status_no_record=%u status_invalid=%u table_used=%u table_poisoned=%u table_evictions=%u",
                id_, frame_, composition_counts_.region_bound, composition_counts_.region_full, composition_counts_.region_hit, composition_counts_.region_miss, composition_counts_.region_poisoned, composition_counts_.region_evicted,
                double(composition_counts_.region_permille_sum) / (1000.0 * double(composition_counts_.region_bound + composition_counts_.region_full)),
                composition_counts_.region_reason[1], composition_counts_.region_reason[2], composition_counts_.region_reason[3], composition_counts_.region_reason[4],
                composition_counts_.region_reason[5], composition_counts_.region_reason[6], composition_counts_.region_status[1], composition_counts_.region_status[2], composition_counts_.region_status[3],
                composition_counts_.region_status[4], composition_counts_.region_status[5], composition_counts_.region_status[6], composition_counts_.region_status[7], composition_counts_.region_status[8],
                fade_bounds_.used(), fade_bounds_.poisoned(), fade_bounds_.evictions());
        if (screen_emission_bound_ && composition_counts_.prefix_draws) {
            const auto& pc = composition_counts_;
            ownership::LockedPrefixStatistics s{};
            ownership::get_locked_prefix_statistics(&s);
            log("locked_prefix_frame device=%llu frame=%llu draws=%u bound=%u refused=%u instanced=%u clipped=%u rechecks=%u f_mean=%.4f hull_px=%llu aabb_px=%llu vertices=%llu derive_us=%.1f reason_viewport=%u reason_rows=%u reason_unknown=%u reason_w=%u reason_nonfinite=%u reason_fill=%u reason_near=%u lookup_unknown=%u lookup_pending=%u lookup_invalid=%u lookup_empty=%u lookup_beyond=%u lookup_nonfinite=%u locks=%llu scans=%llu scanned_vertices=%llu scan_us=%.1f sentinel_bytes=%llu sentinel_us=%.1f window_end_scans=%llu lookups=%llu bounds=%llu marks=%llu table_used=%u table_evictions=%llu",
                id_, frame_, pc.prefix_draws, pc.prefix_bound, pc.prefix_refused, pc.prefix_instanced, pc.prefix_clipped, pc.prefix_rechecks, pc.prefix_bound ? double(pc.prefix_permille_sum) / (1000.0 * double(pc.prefix_bound)) : 0.0,
                static_cast<unsigned long long>(pc.prefix_hull_px), static_cast<unsigned long long>(pc.prefix_aabb_px), static_cast<unsigned long long>(pc.prefix_vertices), telemetry::microseconds(pc.prefix_ticks),
                pc.prefix_reason[1], pc.prefix_reason[2], pc.prefix_reason[3], pc.prefix_reason[4], pc.prefix_reason[5], pc.prefix_reason[6], pc.prefix_reason[7],
                pc.prefix_lookup[1], pc.prefix_lookup[2], pc.prefix_lookup[3], pc.prefix_lookup[4], pc.prefix_lookup[5], pc.prefix_lookup[6],
                static_cast<unsigned long long>(s.locks), static_cast<unsigned long long>(s.scans), static_cast<unsigned long long>(s.scanned_vertices),
                s.qpc_frequency ? double(s.scan_ticks) * 1e6 / double(s.qpc_frequency) : 0.0,
                static_cast<unsigned long long>(s.sentinel_bytes), s.qpc_frequency ? double(s.sentinel_ticks) * 1e6 / double(s.qpc_frequency) : 0.0, static_cast<unsigned long long>(s.window_end_scans),
                static_cast<unsigned long long>(s.lookups), static_cast<unsigned long long>(s.bounds), static_cast<unsigned long long>(s.marks), s.used, static_cast<unsigned long long>(s.evictions));
        }
        if (composition_requested())
            log("%s_refusals device=%llu frame=%llu pair=%u permission_scene=%u readiness=%u readers=%u frame_stop=%u preparation=%u prepare_failures=%u composition_failures=%u restore_failures=%u exchange_failures=%u ack_failures=%u recovery_failures=%u last_prepare=%08lx last_prepare_restore=%08lx last_source=%08lx last_composition=%08lx last_restore=%08lx last_exchange=%08lx last_ack=%08lx last_recovery=%08lx",
                distance_fade_requested_ || screen_emission_requested_ ? "linear_composition" : "linear_emission", id_, frame_, composition_counts_.refusal[0], composition_counts_.refusal[1], composition_counts_.refusal[2], composition_counts_.refusal[3], composition_counts_.refusal[4], composition_counts_.refusal[5],
                composition_counts_.prepare_failures, composition_counts_.composition_failures, composition_counts_.restore_failures, composition_counts_.exchange_failures, composition_counts_.ack_failures, composition_counts_.recovery_failures,
                composition_counts_.prepare, composition_counts_.prepare_restore, composition_counts_.source, composition_counts_.composition, composition_counts_.restore, composition_counts_.exchange, composition_counts_.ack, composition_counts_.recovery);
        if (linear_material_requested_) {
            // Top three refusal buckets of the tested-opaque arm's cutout
            // pairs, formatted into a fixed stack buffer (no allocation, once
            // per frame line): name:count, descending, "none" when empty.
            char cutout_top[64] = "none"; unsigned written = 0;
            std::uint32_t taken = 0;
            for (unsigned slot = 0; slot < 3; ++slot) {
                unsigned best = renderer::sun_untracked_reason_count, best_count = 0;
                for (unsigned reason = 0; reason < renderer::sun_untracked_reason_count; ++reason)
                    if (!(taken & (1u << reason)) && c.cutout_opaque_reasons[reason] > best_count) { best = reason; best_count = c.cutout_opaque_reasons[reason]; }
                if (best == renderer::sun_untracked_reason_count) break;
                taken |= 1u << best;
                const int length = std::snprintf(cutout_top + written, sizeof cutout_top - written, "%s%s:%lu",
                    written ? "," : "", renderer::sun_untracked_reason_name(best), static_cast<unsigned long>(best_count));
                if (length <= 0 || unsigned(length) >= sizeof cutout_top - written) break;
                written += unsigned(length);
            }
            log("linear_material_frame device=%llu frame=%llu routed=%lu bump_routed=%lu refused=%lu bind_failures=%lu cutout_routed=%lu cutout_missed=%lu cutout_unavailable=%u cutout_caps=%u fade_routed=%lu fade_refused=%lu fade_held=%lu fade_route=%u"
                " cutout_opaque_routed=%lu cutout_opaque_refused=%lu cutout_opaque_lane=%lu cutout_opaque_refused_top=%s",
                id_, frame_, static_cast<unsigned long>(c.material_routed), static_cast<unsigned long>(c.material_bump_routed), static_cast<unsigned long>(c.material_refused),
                static_cast<unsigned long>(c.material_bind_failures), static_cast<unsigned long>(c.cutout_routed),
                static_cast<unsigned long>(c.cutout_missed), unsigned(cutout_coverage_missed_), unsigned(cutout_caps_),
                static_cast<unsigned long>(c.fade_routed), static_cast<unsigned long>(c.fade_refused), static_cast<unsigned long>(c.fade_held), fade_route_threshold_,
                static_cast<unsigned long>(c.cutout_opaque_routed), static_cast<unsigned long>(c.cutout_opaque_refused),
                static_cast<unsigned long>(c.cutout_opaque_lane), cutout_top);
        }
        const auto us = [](std::uint64_t ticks) { return telemetry::microseconds(ticks); };
        log("motion_output_frame device=%llu frame=%llu latched=%u msaa=%lu filled=%u fill_result=%08lx fill_restore=%08lx draws=%lu routed=%lu matched=%lu gate1=%lu gate2=%lu gate3=%lu gate4=%lu gate5=%lu gate6=%lu apply_failures=%lu restore_failures=%lu history_previous=%u history_current=%u committed=%u selector_state=%u present=%08lx depth=%u depth_routed=%lu jitter=%u jitter_index=%u jitter_x=%.6f jitter_y=%.6f jitter_previous_x=%.6f jitter_previous_y=%.6f jittered=%lu unjittered_depth_writers=%lu cut=%u cut_median_px=%.4f cut_missing=%.4f cut_samples=%lu taa=%u taa_attempted=%u taa_resolved=%u taa_history=%u taa_skip=%lu taa_result=%08lx taa_restore=%08lx taa_copy=%08lx taa_hdr=%u taa_k=%.5f taa_sharpen=%u scene_open=%u active_queries=%lu taa_references=%u"
            " camera_valid=%u camera_background_valid=%u camera_reads=%lu camera_policy=%lu camera_reason=%lu camera_cut=%u camera_rotation_deg=%.4f"
            " rt_mode=%s timing=%s set_rt=%lu lazy_flushes=%lu jitter_writes=%lu readbacks=%lu gate_us=%.1f route_draw_us=%.1f set_rt_us=%.1f lazy_flush_us=%.1f jitter_us=%.1f fill_us=%.1f taa_run_us=%.1f taa_capture_us=%.1f taa_copy_color_us=%.1f taa_copy_depth_us=%.1f taa_draw_us=%.1f taa_apply_us=%.1f taa_copy_back_us=%.1f readback_us=%.1f"
            " state_shadow=%u rs_mode=%s rs_queries=%lu rs_hits=%lu rs_gets=%lu rs_resyncs=%lu rs_invalidations=%lu sb_resyncs=%lu scene_hook=%u scene_end_source=%s scene_end_check=%lu hook_signals=%lu hook_outside_scene=%lu hook_state=%lu draws_after_hook=%lu bloom_copy_seen=%u"
            " mip_bias=%g mip_bias_sets=%lu mip_bias_restores=%lu mip_bias_draws=%lu mip_bias_stages=%04lx mip_bias_reads=%lu mip_bias_game_writes=%lu mip_bias_game_writes_total=%lu mip_bias_failures=%lu mip_bias_biased_now=%04lx",
            id_, frame_, counters_.latched, static_cast<unsigned long>(main_msaa_ ? main_msaa_samples_ : 0u), counters_.filled, counters_.fill_result, counters_.fill_restore,
            static_cast<unsigned long>(counters_.draws), static_cast<unsigned long>(counters_.routed), static_cast<unsigned long>(counters_.matched),
            static_cast<unsigned long>(counters_.gates[1]), static_cast<unsigned long>(counters_.gates[2]), static_cast<unsigned long>(counters_.gates[3]),
            static_cast<unsigned long>(counters_.gates[4]), static_cast<unsigned long>(counters_.gates[5]), static_cast<unsigned long>(counters_.gates[6]),
            static_cast<unsigned long>(counters_.apply_failures), static_cast<unsigned long>(counters_.restore_failures),
            unsigned(stats.previous), unsigned(stats.current), committed, unsigned(selector_.state()), result,
            depth_enabled_, static_cast<unsigned long>(counters_.depth_routed), counters_.jitter_active, counters_.jitter_index,
            counters_.jitter[0], counters_.jitter[1], counters_.jitter_previous[0], counters_.jitter_previous[1],
            static_cast<unsigned long>(counters_.jittered), static_cast<unsigned long>(counters_.unjittered_depth_writers),
            counters_.cut, counters_.cut_median_px, counters_.cut_missing_fraction,
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
            state_shadow_, render_state_mode(), static_cast<unsigned long>(c.rs_queries), static_cast<unsigned long>(c.rs_hits), static_cast<unsigned long>(c.rs_gets),
            static_cast<unsigned long>(c.rs_resyncs), static_cast<unsigned long>(c.rs_invalidations), static_cast<unsigned long>(c.sb_resyncs), scene_hook_installed_, scene_end_source_name(c.taa.source), static_cast<unsigned long>(c.scene_end_check),
            static_cast<unsigned long>(c.hook_signals), static_cast<unsigned long>(c.hook_outside_scene), static_cast<unsigned long>(c.hook_state),
            static_cast<unsigned long>(c.draws_after_hook), c.bloom_copy_seen,
            double(mip_bias_), static_cast<unsigned long>(c.mip_bias_sets), static_cast<unsigned long>(c.mip_bias_restores),
            static_cast<unsigned long>(c.mip_bias_draws), static_cast<unsigned long>(c.mip_bias_stages), static_cast<unsigned long>(c.mip_bias_reads),
            static_cast<unsigned long>(c.mip_bias_game_writes), static_cast<unsigned long>(mip_bias_total_game_writes_),
            static_cast<unsigned long>(c.mip_bias_failures), static_cast<unsigned long>(sampler_biased_mask_));
    }
    if (taa_enabled_ && (capture_ || frame_ % camera_log_interval_ == 0)) log_camera_state();
    if (hdr_enabled_ && (capture_ || (telemetry_ && frame_ % frame_log_interval_ == 0))) log_hdr_frame();
    flush_taa_invalidate_log();
}

#ifdef X3M_MOTION_OUTPUT_FIXTURE
void MotionOutput::fixture_configure(const MotionOutputFixtureConfig& config) noexcept {
    fixture_ = config; fixture_configured_ = true;
}
void MotionOutput::fixture_emission_fault(unsigned kind, unsigned count) noexcept {
    if (kind == 200) { fixture_cutout_cap_fault_ = count; cutout_caps_ = cutout::Capability::Pending;
        cutout_cap_result_ = S_FALSE; cutout_probe_frame_known_ = false; return; }
    if (kind == 210) { fixture_cutout_vs_fault_ = count; return; }
    if (kind == 211) { fixture_cutout_ps_fault_ = count; return; }
    if (kind == 220) { fixture_cutout_rs_fault_ = count; return; }
    if (kind == 221) { fixture_cutout_sampler_fault_ = count; return; }
    if (kind == 101) { fixture_emission_exchange_fault_ = count; return; }
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
    if (composition_ && kind <= unsigned(renderer::LinearEmissionPassFault::FrameClear)) composition_->inject(static_cast<renderer::LinearEmissionPassFault>(kind), count);
#else
    (void)kind; (void)count;
#endif
}
unsigned MotionOutput::fixture_emission_status(unsigned key) const noexcept {
    // 400 + SunUntrackedReason: the tested-opaque arm's refusal buckets.
    if (key >= 400 && key < 400 + renderer::sun_untracked_reason_count) return counters_.cutout_opaque_reasons[key - 400];
    switch (key) {
    case 0: return composition_effective_;
    case 1: return composition_ && composition_->coverage_valid() && !composition_frame_stopped_ && !composition_quarantined_ && !composition_state_lost_;
    case 2: return composition_state_lost_;
    case 3: return composition_quarantined_;
    case 4: return composition_counts_.prepared;
    case 5: return composition_counts_.linear;
    case 6: return composition_counts_.native;
    case 7: return composition_counts_.incomplete;
    case 8: return composition_counts_.refused;
    case 9: return composition_counts_.suppressed;
    case 10: return composition_counts_.exchanged;
    case 11: return unsigned(composition_counts_.source);
    case 13: return composition_counts_.eligible_fade;
    case 14: return composition_counts_.prepared_fade;
    case 15: return composition_counts_.linear_fade;
    case 16: return composition_required_producers_;
    case 17: return composition_frame_stopped_;
    case 18: return composition_ ? composition_->caps().available_policies : 0;
    case 19: return composition_ ? composition_->caps().supported_policies : 0;
    case 20: return composition_ ? composition_->references() : 0;
    case 21: return composition_ ? composition_->allocations() : 0;
    case 22: return composition_counts_.region_bound;
    case 23: return composition_counts_.region_full;
    case 24: return composition_counts_.region_hit;
    case 25: return composition_counts_.region_poisoned;
    case 26: return fade_bounds_.reserved();
    case 27: return composition_counts_.in_place;
    case 28: return composition_counts_.in_place_linear;
    case 29: return unsigned(composition_counts_.region_pixels);
    case 90: return sun_lane_qualified_;
    case 91: return sun_lane_active_;
    case 92: return sun_frame_.available;
    case 93: return sun_frame_.receivers;
    case 94: return sun_frame_.untracked;
    case 95: return sun_lane_failed_;
    case 96: return sun_frame_.coverage_required;
    case 97: return counters_.taa.resolved;
    case 98: return counters_.taa.used_history;
    case 30: return unsigned(cutout_caps_);
    case 31: return cutout_cap_queries_;
    case 32: return cutout_coverage_missed_;
    case 33: return counters_.cutout_routed;
    case 34: return counters_.cutout_missed;
    case 35: return static_cast<unsigned>(cutout_cap_result_);
    case 37: return counters_.cutout_opaque_routed;  // fixture: tested-opaque arm cutout pairs this frame
    case 38: return counters_.cutout_opaque_refused;
    case 39: return counters_.cutout_opaque_lane;
    case 89: return counters_.routed;   // fixture: routed draws this frame
    case 99: return counters_.gates[4]; // fixture: gate-4 (draw state) refusals this frame
    case 40: return composition_counts_.packed_eligible;
    case 41: return composition_counts_.packed_admitted;
    case 42: return composition_counts_.packed_linear;
    case 43: return composition_counts_.packed_incomplete;
    case 44: return composition_counts_.packed_unbounded_refused;
    case 45: return composition_counts_.packed_caps_refused;
    case 60: return screen_additive_admitted_;
    case 61: return screen_additive_refused_;
    case 62: return screen_additive_failures_;
    case 46: return unsigned(composition_counts_.packed_region_pixels);
    case 47: return composition_counts_.prefix_bound;
    case 48: return composition_counts_.prefix_refused;
    case 50: return counters_.fade_routed;
    case 51: return counters_.fade_refused;
    case 52: return fade_route_threshold_;
    case 53: return counters_.fade_held;
    case 36: { static_assert(motion_shadow_state_count <= 32); unsigned mask=0;
        for (unsigned i=0;i<motion_shadow_state_count;++i) if (shadow_.states_known[i]) mask |= std::uint32_t{1} << i;
        return mask; }
    default: return 0;
    }
}
HRESULT MotionOutput::fixture_setter_result(HRESULT result,unsigned slot,unsigned selector) noexcept {
    if (FAILED(result)) return result;
    if (slot==57 && fixture_cutout_rs_fault_ && fixture_cutout_rs_fault_==selector) {
        fixture_cutout_rs_fault_=0; return E_FAIL;
    }
    if (slot==69 && fixture_cutout_sampler_fault_ && fixture_cutout_sampler_fault_-1==selector) {
        fixture_cutout_sampler_fault_=0; return E_FAIL;
    }
    return result;
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
    if (target != 1 && target != 2 && target != 3) return D3DERR_INVALIDCALL;
    IDirect3DSurface9* surface = target == 1 ? target_surface_ : target == 2 ? depth_surface_ : composition_ ? composition_->coverage_target() : nullptr;
    const unsigned components = target == 2 ? (sun_lane_active_?2u:1u) : 4u;
    if (!surface) return D3DERR_NOTFOUND;
    if (!out || floats < std::size_t(target_width_) * target_height_ * components) return D3DERR_MOREDATA;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_, target_width_, target_height_,
        target == 1 ? D3DFMT_A32B32G32R32F : target == 2 ? (sun_lane_active_?D3DFMT_G32R32F:D3DFMT_R32F) : D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr)) hr = native<GetRtDataFn>(GetRenderTargetData)(device_, surface, copy);
    D3DLOCKED_RECT lock{};
    if (SUCCEEDED(hr)) hr = copy->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (UINT y = 0; y < target_height_; ++y) {
            const auto* row = static_cast<const char*>(lock.pBits) + y * lock.Pitch;
            if (target != 3) std::memcpy(out + std::size_t(y) * target_width_ * components, row, std::size_t(target_width_) * components * 4);
            else for (UINT x = 0; x < target_width_ * 4; ++x) {
                const auto h = reinterpret_cast<const std::uint16_t*>(row)[x];
                const unsigned exponent = (h >> 10) & 31u, mantissa = h & 1023u;
                float value = exponent == 31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity())
                    : std::ldexp(float(exponent ? 1024u + mantissa : mantissa), int(exponent ? exponent : 1) - 25);
                out[std::size_t(y) * target_width_ * 4 + x] = h & 0x8000u ? -value : value;
            }
        }
        copy->UnlockRect();
    }
    release(copy);
    return hr;
}
#endif

// ---- caster-candidate counter (shadow_replay_candidates.h) ----------------
//
// docs/architecture/shadow-replay-gates.md section 3, X3M_SHADOW_REPLAY_CANDIDATES=1.
// Off: no code runs (every site tests candidates_requested_). On: per routed
// successful draw, integer classification from route fields the draw already
// established plus, for a managed slice-0 candidate, two registry snapshots of
// the buffer-lock bookends (CPU only); at scene end the same snapshots again,
// one log line, at most 16 witness lines per device. No allocation.
shadow_replay::PoolClass MotionOutput::candidate_pool_of(std::uint64_t id, IDirect3DResource9* buffer, bool vertex) noexcept {
    using shadow_replay::PoolClass;
    if (!id || !buffer) return PoolClass::Unknown;
    const PoolClass cached = candidate_pools_.find(id);
    if (cached != PoolClass::Unknown) return cached;
    // The route has no pool/usage shadow: one documented GetDesc of the
    // application's own live SetStreamSource/SetIndices argument, cached per
    // allocation id (never reused), inside the setter's CpuCallBoundary hook.
    // LastError is kept as the application left it.
    const DWORD error = GetLastError();
    PoolClass value = PoolClass::Unknown;
    if (vertex) {
        D3DVERTEXBUFFER_DESC desc{};
        if (SUCCEEDED(static_cast<IDirect3DVertexBuffer9*>(buffer)->GetDesc(&desc))) value = shadow_replay::classify_pool(desc.Pool, desc.Usage);
    } else {
        D3DINDEXBUFFER_DESC desc{};
        if (SUCCEEDED(static_cast<IDirect3DIndexBuffer9*>(buffer)->GetDesc(&desc))) value = shadow_replay::classify_pool(desc.Pool, desc.Usage);
    }
    SetLastError(error);
    if (value != PoolClass::Unknown) candidate_pools_.store(id, value);
    return value;
}
void MotionOutput::note_candidate_distance(MotionRoute& route, const float* rows) noexcept {
    float distance = 0.f;
    route.candidate_distance = fade_route::origin_distance(rows, camera_scene_.valid, camera_scene_.m00, camera_scene_.m11,
                                                           camera_scene_.m20, camera_scene_.m21, distance) ? distance : -1.f;
}
void MotionOutput::note_candidate_draw(const MotionRoute& route) noexcept {
    // Gate 4 required ZENABLE=1 and ZWRITEENABLE=1 for every routed draw except
    // the fade-band arm (fade_route::state); alpha-tested draws (the exact
    // cutout arm and the tested-opaque arm) are excluded by W3. Slice 0 is the own-ship distance window of the rows'
    // origin (W1: no camera latch, no slice); pools come from the setter cache.
    const bool zwrite = !route.fade_arm;
    const bool in_slice = route.candidate_distance >= candidate_slice_near_ && route.candidate_distance <= shadow_replay::slice0_far;
    // The pool class and the bookend identities are the shadow's; they are
    // attributed only when the shadowed binding ids are the route key's.
    const bool shadow_ok = shadow_.stream0 == route.key.vertex_buffer && (!route.key.indexed || shadow_.indices == route.key.index_buffer);
    if (!candidates_.draw(zwrite, in_slice, route.alpha_tested, shadow_ok, shadow_.stream0_pool, shadow_.indices_pool, route.key.indexed)) return;
    // Bookend view at the draw: registry snapshot keyed by the wrapper identity
    // (never dereferenced). A managed candidate without a known view for
    // every buffer it uses is counted managed but not leased.
    ownership::BufferLockView vb{}, ib{};
    const auto view = [](std::uintptr_t identity, ownership::BufferLockView& out) noexcept {
        return identity && SUCCEEDED(ownership::get_buffer_lock_view(reinterpret_cast<IDirect3DResource9*>(identity), &out)) && out.known;
    };
    if (!view(shadow_.stream0_identity, vb)) return;
    if (route.key.indexed && !view(shadow_.indices_identity, ib)) return;
    auto& r = candidates_.record();
    r.vb = route.key.vertex_buffer; r.vb_identity = shadow_.stream0_identity;
    r.ib = route.key.indexed ? route.key.index_buffer : 0; r.ib_identity = route.key.indexed ? shadow_.indices_identity : 0;
    r.vb_view = static_cast<const ownership::BufferLockObservation&>(vb);
    r.ib_view = static_cast<const ownership::BufferLockObservation&>(ib);
    r.vb_generation = vb.generation; r.ib_generation = ib.generation;
    ++candidates_.counts.leased;
    if (depth_replay_requested_) note_depth_geometry(route, candidates_.record_count - 1);
}
void MotionOutput::publish_shadow_replay_candidates() noexcept {
    using shadow_replay::BufferVerdict;
    // Once per frame: the hook and the bloom-copy sites both qualify, and a
    // second qualifying copy must not emit a second (all-zero) line.
    if (candidates_published_frame_ == frame_) return;
    candidates_published_frame_ = frame_;
    auto& c = candidates_.counts;
    const std::uint32_t presenting = GetCurrentThreadId();
    bool quiet_records[shadow_replay::record_capacity]{}; // per record: compared, not stale, every buffer quiet (depth replay admission)
    for (unsigned i = 0; i < candidates_.record_count; ++i) {
        const auto& r = candidates_.records[i];
        // Scene-end view of each recorded buffer. A buffer whose registry
        // lookup fails (wrapper gone) is not quiet and yields no witness; a
        // view of another allocation or generation (identity reused by a new
        // wrapper, or a Reset in between) is stale: counted, never compared.
        ownership::BufferLockView end{};
        BufferVerdict verdicts[2]{}; bool present[2]{};
        const std::uintptr_t identities[2] = {r.vb_identity, r.ib_identity};
        const std::uint64_t generations[2] = {r.vb_generation, r.ib_generation};
        const ownership::BufferLockObservation* at_draw[2] = {&r.vb_view, &r.ib_view};
        const unsigned buffers = r.ib_identity ? 2u : 1u;
        bool stale = false;
        for (unsigned b = 0; b < buffers; ++b) {
            end = {};
            present[b] = SUCCEEDED(ownership::get_buffer_lock_view(reinterpret_cast<IDirect3DResource9*>(identities[b]), &end)) && end.requested;
            if (!present[b]) continue;
            if (end.allocation_id != at_draw[b]->allocation_id || end.generation != generations[b]) { stale = true; break; }
            verdicts[b] = shadow_replay::compare(*at_draw[b], static_cast<const ownership::BufferLockObservation&>(end), presenting);
        }
        if (stale) { ++c.stale; continue; }
        bool serial = false, readonly = false, writable = false, pending = false, in_flight = false, cold = false, quiet = true;
        for (unsigned b = 0; b < buffers; ++b) {
            if (!present[b]) { quiet = false; continue; }
            const auto& v = verdicts[b];
            serial |= v.serial_changed; readonly |= v.readonly_after; writable |= v.writable_after;
            pending |= v.pending; in_flight |= v.in_flight; cold |= v.cold_thread; quiet &= v.quiet;
        }
        c.serial_changed += serial; c.readonly_after += readonly && !writable; c.writable_after += writable;
        c.pending += pending; c.in_flight += in_flight; c.cold_thread += cold; c.quiet += quiet;
        quiet_records[i] = quiet;
        for (unsigned b = 0; b < buffers && candidate_witnesses_ < shadow_replay::witness_capacity; ++b) {
            if (!present[b] || !verdicts[b].changed) continue;
            ++candidate_witnesses_;
            const auto& w = verdicts[b].witness;
            log("shadow_replay_lock_witness device=%llu frame=%llu allocation=%llu flags=%08x offset=%u size=%u thread=%u serial_delta=%llu revision_delta=%llu",
                id_, frame_, static_cast<unsigned long long>(w.allocation), unsigned(w.flags), unsigned(w.offset), unsigned(w.size), unsigned(w.thread),
                static_cast<unsigned long long>(w.serial_delta), static_cast<unsigned long long>(w.revision_delta));
        }
    }
    // Admission roots from the process monitor when X3M_ADMISSION=1 (zero
    // otherwise); nested = scene-end signals this frame that found no open
    // boundary (a second or out-of-phase signal), the observable C1 refusal.
    if (candidates_monitor_) {
        const auto s = ownership::admission_snapshot(candidates_monitor_);
        c.roots = s.active_roots; c.waiting = s.waiting_roots;
    }
    c.nested = counters_.hook_outside_scene;
    log("shadow_replay_candidates device=%llu frame=%llu routed=%u zwrite=%u slice0=%u managed=%u dynamic=%u default_pool=%u excluded=%u unknown=%u shadow_mismatch=%u"
        " leased=%u serial_changed=%u readonly_after=%u writable_after=%u pending=%u in_flight=%u quiet=%u cold_thread=%u stale=%u roots=%llu waiting=%llu nested=%u overflow=%u",
        id_, frame_, c.routed, c.zwrite, c.slice0, c.managed, c.dynamic, c.default_pool, c.excluded, c.unknown, c.shadow_mismatch,
        c.leased, c.serial_changed, c.readonly_after, c.writable_after, c.pending, c.in_flight, c.quiet, c.cold_thread, c.stale,
        static_cast<unsigned long long>(c.roots), static_cast<unsigned long long>(c.waiting), c.nested, c.overflow);
    if (depth_replay_requested_) run_shadow_replay_depth(quiet_records);
    candidates_.reset();
}
#include "motion_output_shadow_replay_inc.h"
} // namespace x3m
