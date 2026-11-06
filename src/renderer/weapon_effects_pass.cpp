#include "weapon_effects_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include "../proxy/sse_scalar.h"
#include "../proxy/weapon_effects_core.h"
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
namespace wcore = x3m::weapon_effects::core;
template <class T> void drop(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}
bool lost(HRESULT hr) noexcept {
    return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
}
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, SetRenderState = 57, SetTexture = 65, SetSamplerState = 69,
    DrawPrimitiveUP = 83, DrawIndexedPrimitiveUP = 84, CreateVertexDeclaration = 86, SetVertexDeclaration = 87,
    CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/weapon-{shell,decal}-{vertex,pixel}-program.json).
constexpr std::uint32_t shell_vs_words[] = {
#include "weapon_shell_vertex_program_inc.h"
};
constexpr std::uint32_t shell_ps_words[] = {
#include "weapon_shell_pixel_program_inc.h"
};
constexpr std::uint32_t decal_vs_words[] = {
#include "weapon_decal_vertex_program_inc.h"
};
constexpr std::uint32_t decal_ps_words[] = {
#include "weapon_decal_pixel_program_inc.h"
};
template <std::size_t N> std::uint32_t vs3_slots(const std::uint32_t (&words)[N]) noexcept {
    std::uint32_t copy[N];
    std::memcpy(copy, words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, N);
}
constexpr D3DVERTEXELEMENT9 shell_elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                                D3DDECL_END()};
static_assert(sizeof(WeaponEffectsPass::CellVertex) == 32, "the patch declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 decal_elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                                D3DDECL_END()};
struct DecalVertex {
    float p[4];      // view-space centre, radius
    float shade[4];  // corner x, y, soft radius, alpha
};
static_assert(sizeof(DecalVertex) == 32, "the decal declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
bool finite_all(const float* v, unsigned n) noexcept {
    for (unsigned i = 0; i < n; ++i)
        if (!finite(v[i])) return false;
    return true;
}
// The patch index list (attach time): per cell its six corners (CellVertex, run() writes them per draw) as a fan of
// four triangles, cell c at vertices 6 c .. 6 c + 5. Both faces draw (CULLMODE NONE), so the winding is free.
bool build_indices(std::uint16_t* idx) noexcept {
    constexpr unsigned V = WeaponEffectsPass::cell_vertices;
    static_assert(WeaponEffectsPass::patch_vertices <= 65536, "16-bit indices");
    unsigned out = 0;
    for (unsigned c = 0; c < weapon_patch_cells; ++c)
        for (unsigned t = 0; t < WeaponEffectsPass::cell_triangles; ++t) {
            idx[out++] = std::uint16_t(c * V);
            idx[out++] = std::uint16_t(c * V + t + 1);
            idx[out++] = std::uint16_t(c * V + t + 2);
        }
    return out == WeaponEffectsPass::patch_triangles * 3;
}
} // namespace

WeaponLook default_weapon_look() noexcept {
    WeaponLook l{};
    l.ripple_speed = wcore::ripple_speed;
    l.ring_width = wcore::ring_width;
    l.ring_gain = wcore::ring_gain;
    l.centre_gain = wcore::centre_gain;
    l.centre_fill = wcore::centre_fill;
    l.fade_s = float(wcore::fade_ms) * 1e-3f;
    l.stagger = wcore::stagger;
    l.edge_width = wcore::edge_width;
    l.fill_ratio = wcore::fill_ratio;
    l.cell_gain = wcore::cell_gain;
    return l;
}
WeaponEffectsPass::~WeaponEffectsPass() {
    detach();
}
void WeaponEffectsPass::release_objects() noexcept {
    drop(shell_vs_);
    drop(shell_ps_);
    drop(decal_vs_);
    drop(decal_ps_);
    drop(shell_declaration_);
    drop(decal_declaration_);
}
void WeaponEffectsPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = false;
}
void WeaponEffectsPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_objects();
    reset_pending_ = device_ != nullptr;
}
void WeaponEffectsPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
bool WeaponEffectsPass::resources_ready() const noexcept {
    return device_ && caps_.enabled && !reset_pending_ && shell_vs_ && shell_ps_ && decal_vs_ && decal_ps_ &&
           shell_declaration_ && decal_declaration_;
}
unsigned WeaponEffectsPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(shell_vs_), static_cast<const void*>(shell_ps_),
                          static_cast<const void*>(decal_vs_), static_cast<const void*>(decal_ps_),
                          static_cast<const void*>(shell_declaration_), static_cast<const void*>(decal_declaration_)})
        n += p != nullptr;
    return n;
}
HRESULT WeaponEffectsPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
    PreserveCpuState guard;
    detach();
    auto refuse = [&](const char* reason, HRESULT hr = D3DERR_NOTAVAILABLE) {
        release_objects();
        device_ = nullptr;
        vtable_ = nullptr;
        caps_.enabled = false;
        caps_.reason = reason;
        return hr;
    };
    if (!d || !native) return refuse("device_native_table", E_INVALIDARG);
    device_ = d;
    vtable_ = native;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        return refuse("shader_model3");
    caps_.shell_vs_slots = vs3_slots(shell_vs_words);
    caps_.decal_vs_slots = vs3_slots(decal_vs_words);
    caps_.shell_ps_slots = ps3_program_slots(shell_ps_words, std::size(shell_ps_words));
    caps_.decal_ps_slots = ps3_program_slots(decal_ps_words, std::size(decal_ps_words));
    if (!caps_.shell_vs_slots || !caps_.decal_vs_slots || !caps_.shell_ps_slots || !caps_.decal_ps_slots)
        return refuse("compiled_program");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_caps");
    if (caps.MaxPrimitiveCount < patch_triangles || caps.MaxVertexIndex < patch_vertices || caps.MaxStreams < 1u)
        return refuse("limits");
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr))
        hr = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                                    D3DFMT_A16B16G16R16F);
    drop(api);
    caps_.fp16_blending = hr;
    if (faults_ & 1u) {
        caps_.fp16_blending = D3DERR_NOTAVAILABLE;
        hr = D3DERR_NOTAVAILABLE;
    }
    if (hr != D3D_OK) return refuse("fp16_blending", D3DERR_NOTAVAILABLE);
    if (!indices_built_) {
        indices_built_ = build_indices(indices_);
        if (!indices_built_) return refuse("cell_mesh", E_FAIL);
    }
    hr = create_objects();
    if (FAILED(hr)) return refuse("program_create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
HRESULT WeaponEffectsPass::create_objects() noexcept {
    D d = device_;
    HRESULT hr = (faults_ & 8u) ? E_FAIL : S_OK; // fixture fault bit 3: program creation refused
    if (SUCCEEDED(hr) && !shell_vs_) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(shell_vs_words), &shell_vs_);
    if (SUCCEEDED(hr) && !shell_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(shell_ps_words), &shell_ps_);
    if (SUCCEEDED(hr) && !decal_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(decal_vs_words), &decal_vs_);
    if (SUCCEEDED(hr) && !decal_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(decal_ps_words), &decal_ps_);
    if (SUCCEEDED(hr) && !shell_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, shell_elements, &shell_declaration_);
    if (SUCCEEDED(hr) && !decal_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, decal_elements, &decal_declaration_);
    if (SUCCEEDED(hr) && !(shell_vs_ && shell_ps_ && decal_vs_ && decal_ps_ && shell_declaration_ && decal_declaration_))
        hr = E_FAIL;
    if (FAILED(hr)) release_objects();
    return hr;
}
HRESULT WeaponEffectsPass::ensure_resources() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return E_FAIL;
    if (resources_ready()) return S_OK;
    PreserveCpuState guard;
    return create_objects();
}
namespace {
// The pixel constants of one draw (src/effects/weapon_hex_patch.hlsl): c2..c18 lattice, colour, patch, timing, gains,
// shape, anchor frame, the ring hits and their points (hex_block, the decals' whole set); patches add c19 (the soft
// radius, 1 / cell) and c20..c23 (the seam planes, weapon_shell_ps.hlsl). Returns false for a degenerate draw (skipped, never drawn
// wrong).
constexpr unsigned hex_head = 17, shell_tail = 5;
bool hex_block(const WeaponLook& l, float cell, float radius, float reach, float seed, float surface2,
               const float colour[3], const float* anchor, const float* anchor_u, const float* anchor_v, unsigned hits,
               const float (*hit_cell)[2], const float (*point)[2], const float* ages, float* out) noexcept {
    if (!(cell > 0.f) || !(radius > 0.f) || !(reach >= 0.f) || !finite(cell) || !finite(radius) || !finite(reach) ||
        !finite(seed) || !finite(surface2) || !finite_all(colour, 3) || hits > wcore::hit_slots)
        return false;
    const float v = l.ripple_speed * radius;
    const float block[9 * 4] = {
        1.f / cell, cell, seed, surface2,                                        // c2 lattice
        colour[0], colour[1], colour[2], 0.f,                                    // c3 colour
        radius, 1.f / radius, 1.f / v, v,                                        // c4 patch
        1.f / l.fade_s, l.stagger, 0.f, l.edge_width,                            // c5 timing
        l.cell_gain, l.fill_ratio, l.ring_gain, l.centre_gain,                   // c6 gains
        // c7 shape: the reach with a tolerance of 1e-3 cell (a footprint centre lies exactly on it)
        0.5f - l.edge_width, 1.f / (l.ring_width * radius), reach + 1e-3f * cell, l.centre_fill,
        anchor[0], anchor[1], anchor[2], 0.f,                                    // c8..c10 the anchor frame
        anchor_u[0], anchor_u[1], anchor_u[2], 0.f,
        anchor_v[0], anchor_v[1], anchor_v[2], 0.f};
    std::memcpy(out, block, sizeof block);
    for (unsigned k = 0; k < 4; ++k) {
        float* h = out + 36 + 4 * k;     // c11..c14
        float* p = out + 52 + 4 * k;     // c15..c18
        const bool on = k < hits;
        h[0] = on ? hit_cell[k][0] : 0.f;
        h[1] = on ? hit_cell[k][1] : 0.f;
        h[2] = on ? ages[k] : 0.f;
        h[3] = on ? 1.f : 0.f;
        p[0] = on ? point[k][0] : 0.f;
        p[1] = on ? point[k][1] : 0.f;
        p[2] = p[3] = 0.f;
        if (on && (!finite(h[0]) || !finite(h[1]) || !finite(h[2]) || !finite(p[0]) || !finite(p[1]))) return false;
    }
    return finite_all(out, hex_head * 4);
}
// A patch's cells as hexagons (CellVertex): per cell its six lattice corners (integers: the centre (kx, 3 ky) in half
// cells and sqrt 3 / 6 cells, the corners (0, +-2), (+-1, +-1) about it), its depth and its clock (game seconds since
// lit + 1, negative for a hit's centre cell, 0 not lit yet); false for a cell the programs cannot carry.
bool write_cells(const WeaponPatch& p, WeaponEffectsPass::CellVertex* v) noexcept {
    if (p.cells > weapon_patch_cells) return false;
    static constexpr float corner[WeaponEffectsPass::cell_vertices][2] = {{0.f, 2.f}, {-1.f, 1.f}, {-1.f, -1.f},
                                                                         {0.f, -2.f}, {1.f, -1.f}, {1.f, 1.f}};
    constexpr float key_max = float((2 * weapon_cell_key_bias) * 4096);
    for (unsigned i = 0; i < p.cells; ++i) {
        const float key = p.cell_key[i], since = p.cell_since[i], depth = p.cell_depth[i];
        if (!(key >= 0.f) || !(key < key_max) || !finite(since) || !finite(depth)) return false;
        const std::int32_t packed = std::int32_t(key); // exact: 0 <= key < 2^24
        const float kx = float((packed >> 12) - weapon_cell_key_bias);
        const float ky = float((packed & 0xfff) - weapon_cell_key_bias);
        const float clock = since >= 0.f ? (p.cell_centre[i] ? -1.f : 1.f) * (since + 1.f) : 0.f;
        for (unsigned k = 0; k < WeaponEffectsPass::cell_vertices; ++k) {
            WeaponEffectsPass::CellVertex& c = v[i * WeaponEffectsPass::cell_vertices + k];
            c.corner[0] = kx + corner[k][0];
            c.corner[1] = 3.f * ky + corner[k][1];
            c.corner[2] = depth;
            c.corner[3] = clock;
            c.key[0] = kx;
            c.key[1] = ky;
            c.key[2] = c.key[3] = 0.f;
        }
    }
    return true;
}
// The seam registers c20..c23 (weapon_shell_ps.hlsl): per seam neighbour j (WeaponPatch::seam_bound) the radical plane
// of the own bound (C, r) and the neighbour's (Cj, rj), as the signed distance s(X) = X . (C - Cj) / D + (|Cj|^2 -
// |C|^2 - rj^2 + r^2) / (2 D), D = |C - Cj| (positive on the own side; within the own bound and outside the
// neighbour's always positive, so the share bites only where both bounds cover the point); unused: s = 1e6. Double
// precision on the CPU (owner-frame view units up to a station's 6500).
void seam_block(const WeaponPatch& p, float out[16]) noexcept {
    for (unsigned k = 0; k < 4; ++k) {
        float* o = out + 4 * k;
        o[0] = o[1] = o[2] = 0.f;
        o[3] = 1e6f;
        if (k >= p.seam_count) continue;
        const float* q = p.seam_bound[k];
        const double c[3] = {p.bound[0], p.bound[1], p.bound[2]}, cj[3] = {q[0], q[1], q[2]};
        const double e[3] = {c[0] - cj[0], c[1] - cj[1], c[2] - cj[2]};
        const double d2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
        if (!(d2 > 1e-6)) { // coincident bounds: half each
            o[3] = 0.f;
            continue;
        }
        const double d = double(scalar::sqrt(float(d2)));
        const double cc = c[0] * c[0] + c[1] * c[1] + c[2] * c[2], jj = cj[0] * cj[0] + cj[1] * cj[1] + cj[2] * cj[2];
        for (unsigned i = 0; i < 3; ++i) o[i] = float(e[i] / d);
        o[3] = float((jj - cc - double(q[3]) * double(q[3]) + double(p.bound[3]) * double(p.bound[3])) / (2.0 * d));
    }
}
} // namespace
HRESULT WeaponEffectsPass::run(const WeaponEffectsFrame& f, WeaponEffectsReport* output) noexcept {
    PreserveCpuState guard;
    WeaponEffectsReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](WeaponEffectsStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    if (!device_ || !caps_.enabled) return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(WeaponEffectsStep::Validate, E_FAIL);
    if (!f.width || !f.height || !f.lane || (f.patch_count && !f.patches) || (f.decal_count && !f.decals) ||
        f.patch_count > wcore::max_patches || f.decal_count > wcore::max_decals || !(f.m00 > 0.f) || !(f.m11 > 0.f) ||
        !finite(f.m20) || !finite(f.m21) || !(f.near_z > 0.f))
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    if (!f.patch_count && !f.decal_count) return finish(S_FALSE);
    const WeaponLook& l = f.look;
    if (!(l.ripple_speed > 0.f) || !(l.ring_width > 0.f) || !finite(l.centre_gain) || !finite(l.centre_fill) ||
        !(l.fade_s > 0.f) || !(l.edge_width > 0.f) || !(l.edge_width < 0.5f) ||
        !(l.stagger >= 0.f) || !(l.stagger < 2.f) || !finite(l.ring_gain) ||
        !finite(l.cell_gain) || !finite(l.fill_ratio))
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(WeaponEffectsStep::Resources, hr);
    D d = device_;
    auto step = [&](WeaponEffectsStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    auto draw_fault = [&](HRESULT draw) {
        if (faults_ & 6u) {
            faults_ &= ~2u;
            return HRESULT(E_FAIL);
        }
        return draw;
    };
    const float W = float(f.width), H = float(f.height);
    const float lane[8] = {W, H, 1.f / W, 1.f / H, f.four_channel ? 1.f : 0.f, f.m22, f.m32, 0.f};
    const float projection[4] = {f.m00, f.m11, f.m20, f.m21};
    const float viewport[4] = {W * .5f, H * .5f, 2.f / W, 2.f / H};
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, lane, 2));
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 0, projection, 1));
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 1, viewport, 1));
    step(WeaponEffectsStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                   std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                   std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(WeaponEffectsStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)},
                   std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(WeaponEffectsStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    if (SUCCEEDED(hr) && f.patch_count) {
        step(WeaponEffectsStep::Patches, call<SetDeclarationFn>(SetVertexDeclaration)(d, shell_declaration_));
        step(WeaponEffectsStep::Patches, call<SetVsFn>(SetVertexShader)(d, shell_vs_));
        step(WeaponEffectsStep::Patches, call<SetPsFn>(SetPixelShader)(d, shell_ps_));
        for (unsigned i = 0; i < f.patch_count && SUCCEEDED(hr); ++i) {
            const WeaponPatch& p = f.patches[i];
            bool ok = finite(p.soft) && p.soft > 0.f && finite_all(p.centre, 3) && finite_all(p.view_u, 3) &&
                      finite_all(p.view_v, 3) && finite_all(p.view_a, 3) && finite_all(p.anchor, 3) &&
                      finite_all(p.anchor_u, 3) && finite_all(p.anchor_v, 3) && p.cells > 0;
            float ps[(hex_head + shell_tail) * 4];
            ok = ok && hex_block(l, p.cell, p.radius, p.reach, p.seed, 0.f, p.colour, p.anchor, p.anchor_u, p.anchor_v,
                                 p.hits, p.hit_cell, p.hit, p.age_s, ps) &&
                 write_cells(p, vertices_);
            if (!ok) continue; // a degenerate patch is skipped, never drawn wrong
            {
                float* x = ps + hex_head * 4; // c19 the soft radius and 1 / cell, c20..c23 the seam planes
                x[0] = p.soft;
                x[1] = 1.f / p.cell;
                x[2] = x[3] = 0.f;
                seam_block(p, x + 4);
                if (!finite_all(x, shell_tail * 4)) continue;
            }
            const float vs[7 * 4] = {p.centre[0], p.centre[1], p.centre[2], f.near_z,
                                     p.view_u[0], p.view_u[1], p.view_u[2], p.cell,
                                     p.view_v[0], p.view_v[1], p.view_v[2], 0.f,
                                     p.view_a[0], p.view_a[1], p.view_a[2], 0.f,
                                     p.anchor[0], p.anchor[1], p.anchor[2], 0.f,
                                     p.anchor_u[0], p.anchor_u[1], p.anchor_u[2], 0.f,
                                     p.anchor_v[0], p.anchor_v[1], p.anchor_v[2], 0.f};
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, vs, 7));
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, hex_head + shell_tail));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, p.cells * cell_vertices, p.cells * cell_triangles, indices_,
                    D3DFMT_INDEX16, vertices_, UINT(sizeof vertices_[0])));
                if (step(WeaponEffectsStep::Patches, draw)) {
                    ++r.patches;
                    r.drew = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && f.decal_count) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Decals, call<SetDeclarationFn>(SetVertexDeclaration)(d, decal_declaration_));
        step(WeaponEffectsStep::Decals, call<SetVsFn>(SetVertexShader)(d, decal_vs_));
        step(WeaponEffectsStep::Decals, call<SetPsFn>(SetPixelShader)(d, decal_ps_));
        step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
        static constexpr float origin[1][2] = {{0.f, 0.f}};
        static constexpr float frame_a[3] = {0.f, 0.f, 1.f}, frame_u[3] = {1.f, 0.f, 0.f}, frame_v[3] = {0.f, 1.f, 0.f};
        for (unsigned i = 0; i < f.decal_count && SUCCEEDED(hr); ++i) {
            const WeaponDecal& c = f.decals[i];
            float ps[hex_head * 4];
            bool ok = finite(c.radius) && c.radius > 0.f && finite(c.age_s) && finite(c.soft) && c.soft > 0.f &&
                      finite_all(c.centre, 3) &&
                      hex_block(l, c.cell, c.radius, c.reach, float(12), 0.f, c.colour, frame_a, frame_u, frame_v, 1,
                                origin, origin, &c.age_s, ps);
            if (!ok) continue;
            // The quad's half size: the footprint's outermost cell centres plus a cell's circumradius (1 / sqrt 3).
            const float half = c.reach + 0.5773503f * c.cell;
            ps[52 + 2] = half; // c15.z: the decal program's corner scale
            DecalVertex v[4];
            for (unsigned k = 0; k < 4; ++k)
                v[k] = DecalVertex{{c.centre[0], c.centre[1], c.centre[2], half},
                                   {corners[k][0], corners[k][1], c.soft, 1.f}};
            step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, hex_head));
            if (SUCCEEDED(hr)) {
                const HRESULT draw =
                    draw_fault(call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, v, UINT(sizeof(DecalVertex))));
                if (step(WeaponEffectsStep::Decals, draw)) {
                    ++r.decals;
                    r.drew = true;
                }
            }
        }
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
