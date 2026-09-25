#include "effects_stage_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateVertexBuffer = 26, CreateIndexBuffer = 27, SetRenderState = 57, SetTexture = 65,
    SetSamplerState = 69, DrawIndexedPrimitive = 82, CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91,
    SetVertexShader = 92, SetVertexShaderConstantF = 94, SetStreamSource = 100, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107,
    SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using CreateIbFn = HRESULT(WINAPI*)(D, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetVsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/effects-*-program.json).
constexpr std::uint32_t bolt_vs_words[] = {
#include "effects_bolt_vertex_program_inc.h"
};
constexpr std::uint32_t bolt_ps_words[] = {
#include "effects_bolt_pixel_program_inc.h"
};
constexpr std::uint32_t shell_vs_words[] = {
#include "effects_shell_vertex_program_inc.h"
};
constexpr std::uint32_t shell_ps_words[] = {
#include "effects_shell_pixel_program_inc.h"
};
constexpr std::uint32_t decal_vs_words[] = {
#include "effects_decal_vertex_program_inc.h"
};
constexpr std::uint32_t decal_ps_words[] = {
#include "effects_decal_pixel_program_inc.h"
};
// The vs_3_0 twin of ps3_program_slots (same costs; the version token differs).
std::uint32_t vs3_program_slots(const std::uint32_t* words, std::size_t count) noexcept {
    if (!words || count < 2 || words[0] != 0xfffe0300u || words[count - 1] != 0xffffu) return 0;
    std::uint32_t slots = 0; std::size_t i = 1;
    while (i < count - 1) {
        const std::uint32_t token = words[i];
        if ((token & 0xffffu) == 0xfffeu) { i += 1 + ((token >> 16) & 0x7fffu); continue; }
        const std::uint32_t op = token & 0xffffu, operands = (token >> 24) & 15u;
        std::uint32_t cost = 1;
        switch (op) { case 31: case 48: case 81: case 46: cost = 0; break; case 37: cost = 8; break; case 38: case 36: case 32: cost = 3; break;
                      case 33: case 90: case 18: case 91: case 92: case 95: cost = 2; break; default: break; }
        slots += cost; i += 1 + operands;
    }
    return i == count - 1 ? slots : 0;
}
constexpr D3DVERTEXELEMENT9 bolt_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
    D3DDECL_END()};
constexpr D3DVERTEXELEMENT9 shell_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END()};
constexpr D3DVERTEXELEMENT9 decal_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()};
bool finite_positive(float v) noexcept { return v == v && v > 0.f && v <= 3.4e38f; }
bool finite_nonneg(float v) noexcept { return v == v && v >= 0.f && v <= 3.4e38f; }
// The unit icosphere: an icosahedron subdivided twice (12 -> 42 -> 162 vertices, 20 -> 80 -> 320 triangles), midpoints
// shared through a linear edge search (at most 480 edges; attach time only). Outward winding is not relied upon: the
// shell draws with CULLMODE NONE and its pixel program rejects back faces by the view-space normal.
struct Sphere { float v[EffectsStagePass::sphere_vertices][3]; std::uint16_t i[EffectsStagePass::sphere_triangles * 3]; unsigned vertices = 0, triangles = 0; };
void normalize3(float* p) noexcept { const float n = scalar::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); if (n > 0.f) { p[0] /= n; p[1] /= n; p[2] /= n; } }
bool build_sphere(Sphere& s) noexcept {
    const float t = 1.6180339887f; // (1 + sqrt 5) / 2
    const float base[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    const std::uint16_t faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    s.vertices = 12; s.triangles = 20;
    for (unsigned k = 0; k < 12; ++k) { std::memcpy(s.v[k], base[k], 12); normalize3(s.v[k]); }
    std::memcpy(s.i, faces, sizeof faces);
    std::uint16_t edge_a[480], edge_b[480], edge_m[480]; unsigned edges = 0;
    std::uint16_t next[EffectsStagePass::sphere_triangles * 3];
    for (unsigned level = 0; level < 2; ++level) {
        edges = 0; unsigned out = 0;
        auto midpoint = [&](std::uint16_t a, std::uint16_t b) -> std::uint16_t {
            const std::uint16_t lo = a < b ? a : b, hi = a < b ? b : a;
            for (unsigned e = 0; e < edges; ++e) if (edge_a[e] == lo && edge_b[e] == hi) return edge_m[e];
            if (edges >= 480 || s.vertices >= EffectsStagePass::sphere_vertices) return 0xffffu;
            float* m = s.v[s.vertices];
            for (unsigned k = 0; k < 3; ++k) m[k] = 0.5f * (s.v[lo][k] + s.v[hi][k]);
            normalize3(m);
            edge_a[edges] = lo; edge_b[edges] = hi; edge_m[edges] = std::uint16_t(s.vertices); ++edges;
            return std::uint16_t(s.vertices++);
        };
        for (unsigned f = 0; f < s.triangles; ++f) {
            const std::uint16_t a = s.i[f * 3], b = s.i[f * 3 + 1], c = s.i[f * 3 + 2];
            const std::uint16_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            if (ab == 0xffffu || bc == 0xffffu || ca == 0xffffu || out + 12 > EffectsStagePass::sphere_triangles * 3) return false;
            const std::uint16_t tris[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            std::memcpy(next + out, tris, sizeof tris); out += 12;
        }
        std::memcpy(s.i, next, out * sizeof(std::uint16_t)); s.triangles = out / 3;
    }
    return s.vertices == EffectsStagePass::sphere_vertices && s.triangles == EffectsStagePass::sphere_triangles;
}
} // namespace

bool valid_tuning(const EffectsStageTuning& t) noexcept {
    return finite_positive(t.min_width_px) && t.min_width_px <= 64.f && finite_positive(t.min_length_px) && t.min_length_px <= 256.f && t.min_length_px >= t.min_width_px &&
           finite_positive(t.halo) && t.halo >= 1.f && t.halo <= 16.f && finite_nonneg(t.stretch) && t.stretch <= 8.f &&
           finite_nonneg(t.core_intensity) && t.core_intensity <= 64.f && finite_nonneg(t.halo_intensity) && t.halo_intensity <= 64.f &&
           finite_positive(t.halo_sigma) && t.halo_sigma <= 4.f && finite_positive(t.tint_floor) && t.tint_floor <= 1.f && finite_positive(t.soft) && t.soft <= 16.f &&
           finite_nonneg(t.rim_intensity) && t.rim_intensity <= 64.f && finite_nonneg(t.ring_intensity) && t.ring_intensity <= 64.f && finite_positive(t.rim_power) && t.rim_power <= 16.f &&
           finite_positive(t.hex_scale) && t.hex_scale <= 256.f && finite_positive(t.ripple_seconds) && t.ripple_seconds <= 10.f && finite_positive(t.ring_speed) && t.ring_speed <= 100.f &&
           finite_positive(t.ring_width) && t.ring_width <= 4.f && finite_positive(t.flash_seconds) && t.flash_seconds <= 10.f && finite_nonneg(t.flash_intensity) && t.flash_intensity <= 256.f &&
           finite_positive(t.flash_sharpness) && t.flash_sharpness <= 1e4f && finite_nonneg(t.decal_ring_intensity) && t.decal_ring_intensity <= 64.f && finite_positive(t.decal_hex_scale) &&
           t.decal_hex_scale <= 256.f && finite_positive(t.decal_ring_speed) && t.decal_ring_speed <= 100.f && finite_positive(t.decal_ring_width) && t.decal_ring_width <= 4.f;
}

EffectsStagePass::~EffectsStagePass() { detach(); }
void EffectsStagePass::detach() noexcept {
    PreserveCpuState guard;
    release_buffers();
    drop(bolt_vs_); drop(bolt_ps_); drop(shell_vs_); drop(shell_ps_); drop(decal_vs_); drop(decal_ps_);
    drop(bolt_declaration_); drop(shell_declaration_); drop(decal_declaration_);
    device_ = nullptr; vtable_ = nullptr; caps_ = {}; reset_pending_ = false;
}
void EffectsStagePass::release_buffers() noexcept { drop(dynamic_vb_); drop(sphere_vb_); drop(sphere_ib_); drop(quad_ib_); }
void EffectsStagePass::before_reset() noexcept { PreserveCpuState guard; release_buffers(); reset_pending_ = device_ != nullptr; }
void EffectsStagePass::after_reset(HRESULT hr) noexcept { PreserveCpuState guard; if (SUCCEEDED(hr)) reset_pending_ = false; }
unsigned EffectsStagePass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(bolt_vs_), static_cast<const void*>(bolt_ps_), static_cast<const void*>(shell_vs_), static_cast<const void*>(shell_ps_),
                          static_cast<const void*>(decal_vs_), static_cast<const void*>(decal_ps_), static_cast<const void*>(bolt_declaration_), static_cast<const void*>(shell_declaration_),
                          static_cast<const void*>(decal_declaration_), static_cast<const void*>(dynamic_vb_), static_cast<const void*>(sphere_vb_), static_cast<const void*>(sphere_ib_),
                          static_cast<const void*>(quad_ib_)}) n += p != nullptr;
    return n;
}
HRESULT EffectsStagePass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
    PreserveCpuState guard;
    detach();
    auto refuse = [&](const char* reason, HRESULT hr = D3DERR_NOTAVAILABLE) { device_ = nullptr; vtable_ = nullptr; caps_.enabled = false; caps_.reason = reason; return hr; };
    if (!d || !native) return refuse("device_native_table", E_INVALIDARG);
    device_ = d; vtable_ = native;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0)) return refuse("shader_model3");
    caps_.bolt_vs_slots = vs3_program_slots(bolt_vs_words, std::size(bolt_vs_words)); caps_.bolt_ps_slots = ps3_program_slots(bolt_ps_words, std::size(bolt_ps_words));
    caps_.shell_vs_slots = vs3_program_slots(shell_vs_words, std::size(shell_vs_words)); caps_.shell_ps_slots = ps3_program_slots(shell_ps_words, std::size(shell_ps_words));
    caps_.decal_vs_slots = vs3_program_slots(decal_vs_words, std::size(decal_vs_words)); caps_.decal_ps_slots = ps3_program_slots(decal_ps_words, std::size(decal_ps_words));
    for (unsigned s : {caps_.bolt_vs_slots, caps_.bolt_ps_slots, caps_.shell_vs_slots, caps_.shell_ps_slots, caps_.decal_vs_slots, caps_.decal_ps_slots}) {
        if (!s) return refuse("compiled_program");
        if (s > caps_.largest_program_slots) caps_.largest_program_slots = s;
    }
    for (unsigned s : {caps_.bolt_ps_slots, caps_.shell_ps_slots, caps_.decal_ps_slots}) if (s > caps.MaxPixelShader30InstructionSlots) return refuse("compiled_slots");
    for (unsigned s : {caps_.bolt_vs_slots, caps_.shell_vs_slots, caps_.decal_vs_slots}) if (s > caps.MaxVertexShader30InstructionSlots) return refuse("compiled_slots");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) || !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE)) return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (caps.MaxVertexShaderConst < 10 || caps.MaxStreams < 1 || caps.MaxVertexIndex < 4u * quad_capacity || caps.MaxPrimitiveCount < 2u * quad_capacity) return refuse("limits");
    if (!(caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) || !(caps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR)) return refuse("filter_caps");
    IDirect3D9* api = nullptr; D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr)) hr = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
    drop(api);
    caps_.fp16_blending = hr;
    if (faults_ & 1u) { caps_.fp16_blending = D3DERR_NOTAVAILABLE; hr = D3DERR_NOTAVAILABLE; }
    if (hr != D3D_OK) return refuse("fp16_blending", D3DERR_NOTAVAILABLE);
    hr = create_programs(caps);
    if (FAILED(hr)) { detach(); return refuse("program_create", hr); }
    hr = ensure_resources();
    if (FAILED(hr)) { detach(); return refuse("buffers", hr); }
    caps_.enabled = true; caps_.reason = "ok";
    return S_OK;
}
HRESULT EffectsStagePass::create_programs(const D3DCAPS9&) noexcept {
    D d = device_;
    HRESULT hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(bolt_vs_words), &bolt_vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(bolt_ps_words), &bolt_ps_);
    if (SUCCEEDED(hr)) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(shell_vs_words), &shell_vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(shell_ps_words), &shell_ps_);
    if (SUCCEEDED(hr)) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(decal_vs_words), &decal_vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(decal_ps_words), &decal_ps_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, bolt_elements, &bolt_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, shell_elements, &shell_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, decal_elements, &decal_declaration_);
    if (SUCCEEDED(hr) && !(bolt_vs_ && bolt_ps_ && shell_vs_ && shell_ps_ && decal_vs_ && decal_ps_ && bolt_declaration_ && shell_declaration_ && decal_declaration_)) hr = E_FAIL;
    return hr;
}
HRESULT EffectsStagePass::create_sphere() noexcept {
    Sphere s{};
    if (!build_sphere(s)) return E_FAIL;
    D d = device_;
    HRESULT hr = call<CreateVbFn>(CreateVertexBuffer)(d, sphere_vertices * 12u, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &sphere_vb_, nullptr);
    if (SUCCEEDED(hr)) hr = call<CreateIbFn>(CreateIndexBuffer)(d, sphere_triangles * 3u * 2u, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &sphere_ib_, nullptr);
    if (SUCCEEDED(hr) && (!sphere_vb_ || !sphere_ib_)) hr = E_FAIL;
    void* mapping = nullptr;
    if (SUCCEEDED(hr)) { hr = sphere_vb_->Lock(0, 0, &mapping, 0); if (SUCCEEDED(hr) && mapping) { std::memcpy(mapping, s.v, sizeof s.v); hr = sphere_vb_->Unlock(); } else if (SUCCEEDED(hr)) hr = E_FAIL; }
    if (SUCCEEDED(hr)) { hr = sphere_ib_->Lock(0, 0, &mapping, 0); if (SUCCEEDED(hr) && mapping) { std::memcpy(mapping, s.i, sizeof s.i); hr = sphere_ib_->Unlock(); } else if (SUCCEEDED(hr)) hr = E_FAIL; }
    if (FAILED(hr)) { drop(sphere_vb_); drop(sphere_ib_); }
    return hr;
}
HRESULT EffectsStagePass::create_quad_indices() noexcept {
    D d = device_;
    HRESULT hr = call<CreateIbFn>(CreateIndexBuffer)(d, quad_capacity * 6u * 2u, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &quad_ib_, nullptr);
    if (SUCCEEDED(hr) && !quad_ib_) hr = E_FAIL;
    void* mapping = nullptr;
    if (SUCCEEDED(hr)) {
        hr = quad_ib_->Lock(0, 0, &mapping, 0);
        if (SUCCEEDED(hr) && mapping) {
            auto* out = static_cast<std::uint16_t*>(mapping);
            for (unsigned q = 0; q < quad_capacity; ++q) {
                const std::uint16_t b = std::uint16_t(q * 4u);
                const std::uint16_t tris[6] = {b, std::uint16_t(b + 1), std::uint16_t(b + 2), std::uint16_t(b + 2), std::uint16_t(b + 1), std::uint16_t(b + 3)};
                std::memcpy(out + q * 6u, tris, sizeof tris);
            }
            hr = quad_ib_->Unlock();
        } else if (SUCCEEDED(hr)) hr = E_FAIL;
    }
    if (FAILED(hr)) drop(quad_ib_);
    return hr;
}
HRESULT EffectsStagePass::ensure_resources() noexcept {
    if (!device_ || reset_pending_) return D3DERR_DEVICENOTRESET;
    HRESULT hr = S_OK;
    if (!dynamic_vb_) { hr = call<CreateVbFn>(CreateVertexBuffer)(device_, dynamic_bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &dynamic_vb_, nullptr); if (SUCCEEDED(hr) && !dynamic_vb_) hr = E_FAIL; }
    if (SUCCEEDED(hr) && (!sphere_vb_ || !sphere_ib_)) { drop(sphere_vb_); drop(sphere_ib_); hr = create_sphere(); }
    if (SUCCEEDED(hr) && !quad_ib_) hr = create_quad_indices();
    if (FAILED(hr)) release_buffers();
    return hr;
}
HRESULT EffectsStagePass::run(const EffectsFrame& f, EffectsReport* output) noexcept {
    PreserveCpuState guard;
    EffectsReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) { r.operation = hr; r.calls = calls_ - initial; if (output) *output = r; return hr; };
    auto refuse = [&](EffectsStageStep step, HRESULT hr) { r.failed = step; if (lost(hr)) reset_pending_ = true; return finish(hr); };
    if (!device_ || !caps_.enabled) return refuse(EffectsStageStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(EffectsStageStep::Validate, D3DERR_DEVICENOTRESET);
    const EffectsStageTuning defaults{};
    const EffectsStageTuning& t = f.tuning ? *f.tuning : defaults;
    if (!f.width || !f.height || !f.lane || !valid_tuning(t) || !finite_positive(f.m00) || !finite_positive(f.m11) || !finite_positive(f.near_z) ||
        f.bolt_instances > effects_stage::max_bolts || f.shell_count > effects_stage::max_shells || f.decal_count > effects_stage::max_hits ||
        (f.bolt_instances && !f.bolt_vertices) || (f.shell_count && !f.shells) || (f.decal_count && !f.decal_vertices)) return refuse(EffectsStageStep::Validate, E_INVALIDARG);
    for (float v : f.view_rows) if (!(v == v) || v > 3.4e38f || v < -3.4e38f) return refuse(EffectsStageStep::Validate, E_INVALIDARG);
    if (!f.bolt_instances && !f.shell_count && !f.decal_count) return finish(S_FALSE);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(EffectsStageStep::Resources, hr);
    D d = device_;
    // The dynamic vertices: bolts first (stride 64), then decals (stride 32) at the next 32-byte boundary.
    const UINT bolt_bytes = f.bolt_instances * 4u * UINT(sizeof(effects_stage::BoltVertex));
    const UINT decal_offset = bolt_bytes, decal_bytes = f.decal_count * 4u * UINT(sizeof(effects_stage::DecalVertex));
    if (decal_offset + decal_bytes > dynamic_bytes) return refuse(EffectsStageStep::Validate, E_INVALIDARG);
    if (bolt_bytes || decal_bytes) {
        void* mapping = nullptr;
        hr = dynamic_vb_->Lock(0, decal_offset + decal_bytes, &mapping, D3DLOCK_DISCARD);
        if (FAILED(hr) || !mapping) { if (SUCCEEDED(hr)) { dynamic_vb_->Unlock(); hr = E_FAIL; } return refuse(EffectsStageStep::Lock, hr); }
        if (bolt_bytes) std::memcpy(mapping, f.bolt_vertices, bolt_bytes);
        if (decal_bytes) std::memcpy(static_cast<unsigned char*>(mapping) + decal_offset, f.decal_vertices, decal_bytes);
        hr = dynamic_vb_->Unlock();
        if (FAILED(hr)) return refuse(EffectsStageStep::Lock, hr);
    }
    // Shared state and constants.
    auto step = [&](EffectsStageStep at, HRESULT value) { if (FAILED(value) && SUCCEEDED(hr)) { hr = value; r.failed = at; } return SUCCEEDED(hr); };
    const float projection[4] = {f.m00, f.m11, f.m20, f.m21};
    const float viewport[4] = {float(f.width) * 0.5f, float(f.height) * 0.5f, 2.f / float(f.width), 2.f / float(f.height)};
    const float sizes[4] = {float(f.width), float(f.height), 1.f / float(f.width), 1.f / float(f.height)};
    const float lane_form[4] = {f.lane_four_channel ? 1.f : 0.f, f.m22, f.m32, 0.f};
    step(EffectsStageStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 0, f.view_rows, 3));
    step(EffectsStageStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 3, projection, 1));
    step(EffectsStageStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 4, viewport, 1));
    step(EffectsStageStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, sizes, 1));
    step(EffectsStageStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, lane_form, 1));
    step(EffectsStageStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)},
                   std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(EffectsStageStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)}, std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_FOGENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(EffectsStageStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    const float scale_1080 = float(f.height) / 1080.f;
    if (SUCCEEDED(hr) && f.bolt_instances) {
        const float footprint[4] = {t.min_width_px, t.min_length_px * scale_1080, t.halo, t.stretch};
        const float limits[4] = {f.near_z, t.soft, 0.f, 0.f};
        const float intensity[4] = {t.core_intensity, t.halo_intensity, t.halo_sigma, t.tint_floor};
        step(EffectsStageStep::Bolts, call<SetDeclarationFn>(SetVertexDeclaration)(d, bolt_declaration_));
        step(EffectsStageStep::Bolts, call<SetStreamFn>(SetStreamSource)(d, 0, dynamic_vb_, 0, sizeof(effects_stage::BoltVertex)));
        step(EffectsStageStep::Bolts, call<SetIndicesFn>(SetIndices)(d, quad_ib_));
        step(EffectsStageStep::Bolts, call<SetVsFn>(SetVertexShader)(d, bolt_vs_));
        step(EffectsStageStep::Bolts, call<SetPsFn>(SetPixelShader)(d, bolt_ps_));
        step(EffectsStageStep::Bolts, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 5, footprint, 1));
        step(EffectsStageStep::Bolts, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 6, limits, 1));
        step(EffectsStageStep::Bolts, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, intensity, 1));
        step(EffectsStageStep::Bolts, call<SetTextureFn>(SetTexture)(d, 1, f.bolt_atlas));
        for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_LINEAR)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_LINEAR)}, std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_LINEAR)},
                       std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}, std::pair{D3DSAMP_MAXMIPLEVEL, DWORD(0)}})
            step(EffectsStageStep::Bolts, call<SetSamplerFn>(SetSamplerState)(d, 1, s.first, s.second));
        if (SUCCEEDED(hr)) {
            HRESULT draw = call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0, f.bolt_instances * 4u, 0, f.bolt_instances * 2u);
            if (faults_ & 2u) { faults_ &= ~2u; draw = E_FAIL; }
            if (step(EffectsStageStep::Bolts, draw)) { r.bolts = f.bolt_instances; r.drew = true; }
        }
    }
    if (SUCCEEDED(hr) && f.shell_count) {
        const float look[4] = {t.rim_intensity, t.ring_intensity, t.rim_power, t.hex_scale};
        const float timing_base[4] = {0.f, t.ripple_seconds, t.ring_speed, t.ring_width};
        const float flash[4] = {t.flash_seconds, t.flash_intensity, t.flash_sharpness, 0.f};
        step(EffectsStageStep::Shells, call<SetDeclarationFn>(SetVertexDeclaration)(d, shell_declaration_));
        step(EffectsStageStep::Shells, call<SetStreamFn>(SetStreamSource)(d, 0, sphere_vb_, 0, 12));
        step(EffectsStageStep::Shells, call<SetIndicesFn>(SetIndices)(d, sphere_ib_));
        step(EffectsStageStep::Shells, call<SetVsFn>(SetVertexShader)(d, shell_vs_));
        step(EffectsStageStep::Shells, call<SetPsFn>(SetPixelShader)(d, shell_ps_));
        step(EffectsStageStep::Shells, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, look, 1));
        step(EffectsStageStep::Shells, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 9, flash, 1));
        for (unsigned i = 0; i < f.shell_count && SUCCEEDED(hr); ++i) {
            const effects_stage::ShellInstance& s = f.shells[i];
            float mean_axis = 0.f;
            for (unsigned k = 0; k < 3; ++k) mean_axis += effects_stage::length3(s.axes + k * 3);
            mean_axis *= (1.f / 3.f);
            const float centre[4] = {s.centre[0], s.centre[1], s.centre[2], f.near_z};
            const float axes[12] = {s.axes[0], s.axes[1], s.axes[2], mean_axis * t.soft, s.axes[3], s.axes[4], s.axes[5], 0.f, s.axes[6], s.axes[7], s.axes[8], 0.f};
            const float tint[4] = {s.tint[0], s.tint[1], s.tint[2], s.alpha};
            float timing[4]; std::memcpy(timing, timing_base, sizeof timing); timing[0] = float(s.hit_count < effects_stage::hit_slots ? s.hit_count : effects_stage::hit_slots);
            step(EffectsStageStep::Shells, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 5, centre, 1));
            step(EffectsStageStep::Shells, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 6, axes, 3));
            step(EffectsStageStep::Shells, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 3, tint, 1));
            step(EffectsStageStep::Shells, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 4, timing, 1));
            step(EffectsStageStep::Shells, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 5, &s.hits[0][0], effects_stage::hit_slots));
            if (SUCCEEDED(hr) && step(EffectsStageStep::Shells, call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0, sphere_vertices, 0, sphere_triangles))) { ++r.shells; r.drew = true; }
        }
    }
    if (SUCCEEDED(hr) && f.decal_count) {
        const float limits[4] = {f.near_z, t.soft, 0.f, 0.f};
        const float look[4] = {t.decal_ring_intensity, t.decal_hex_scale, t.decal_ring_speed, t.decal_ring_width};
        const float tint[4] = {1.f, 1.f, 1.f, t.ripple_seconds};
        const float flash[4] = {t.flash_seconds, t.flash_intensity, t.flash_sharpness, 0.f};
        // The decal vertices follow the bolt vertices in the one dynamic buffer: stream offset 0 with BaseVertexIndex in
        // decal strides (the bolt bytes are a whole number of them), so D3DDEVCAPS2_STREAMOFFSET is not required.
        static_assert(sizeof(effects_stage::BoltVertex) % sizeof(effects_stage::DecalVertex) == 0, "the decal base index is exact");
        const INT decal_base = INT(decal_offset / sizeof(effects_stage::DecalVertex));
        step(EffectsStageStep::Decals, call<SetDeclarationFn>(SetVertexDeclaration)(d, decal_declaration_));
        step(EffectsStageStep::Decals, call<SetStreamFn>(SetStreamSource)(d, 0, dynamic_vb_, 0, sizeof(effects_stage::DecalVertex)));
        step(EffectsStageStep::Decals, call<SetIndicesFn>(SetIndices)(d, quad_ib_));
        step(EffectsStageStep::Decals, call<SetVsFn>(SetVertexShader)(d, decal_vs_));
        step(EffectsStageStep::Decals, call<SetPsFn>(SetPixelShader)(d, decal_ps_));
        step(EffectsStageStep::Decals, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 5, limits, 1));
        step(EffectsStageStep::Decals, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, look, 1));
        step(EffectsStageStep::Decals, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 3, tint, 1));
        step(EffectsStageStep::Decals, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 4, flash, 1));
        if (SUCCEEDED(hr) && step(EffectsStageStep::Decals, call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, decal_base, 0, f.decal_count * 4u, 0, f.decal_count * 2u))) { r.decals = f.decal_count; r.drew = true; }
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
