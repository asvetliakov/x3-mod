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
constexpr D3DVERTEXELEMENT9 shell_elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                D3DDECL_END()};
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
// The unit polar cap: vertex 0 the pole (rho 0), then cap_rings rings of cap_segments vertices at rho = r / cap_rings,
// phi = 2 pi k / cap_segments, stored as (cos phi, sin phi, rho); the vertex program maps (x, y, rho) to cos(rho a) dir
// + sin(rho a) (x u + y v) with (u, v, dir) right-handed. The angles come from a rotation recurrence (attach time only,
// no libm), renormalized every step. Winding: every triangle is counter-clockwise seen from outside (from +dir), so
// (b - a) x (c - a) points outward like the icosphere it replaces; run() keeps the outward winding per patch from the
// sign of det A.
bool build_cap(float (*v)[3], std::uint16_t* idx) noexcept {
    constexpr unsigned R = WeaponEffectsPass::cap_rings, S = WeaponEffectsPass::cap_segments;
    static_assert(S == 32, "the recurrence step is 2 pi / 32");
    const float step_c = 0.98078528040323044f, step_s = 0.19509032201612826f; // cos, sin of pi / 16
    float c = 1.f, s = 0.f;
    float ring_c[S], ring_s[S];
    for (unsigned k = 0; k < S; ++k) {
        ring_c[k] = c;
        ring_s[k] = s;
        const float nc = c * step_c - s * step_s, ns = s * step_c + c * step_s;
        const float n = scalar::sqrt(nc * nc + ns * ns);
        c = nc / n;
        s = ns / n;
    }
    v[0][0] = v[0][1] = v[0][2] = 0.f;
    for (unsigned r = 1; r <= R; ++r)
        for (unsigned k = 0; k < S; ++k) {
            float* p = v[1 + (r - 1) * S + k];
            p[0] = ring_c[k];
            p[1] = ring_s[k];
            p[2] = float(r) / float(R);
        }
    unsigned out = 0;
    auto at = [](unsigned r, unsigned k) { return std::uint16_t(1 + (r - 1) * S + (k % S)); };
    for (unsigned k = 0; k < S; ++k) {
        idx[out++] = 0;
        idx[out++] = at(1, k);
        idx[out++] = at(1, k + 1);
    }
    for (unsigned r = 1; r < R; ++r)
        for (unsigned k = 0; k < S; ++k) {
            const std::uint16_t a = at(r, k), b = at(r + 1, k), cc = at(r + 1, k + 1), d = at(r, k + 1);
            const std::uint16_t tris[6] = {a, b, cc, a, cc, d};
            std::memcpy(idx + out, tris, sizeof tris);
            out += 6;
        }
    return out == WeaponEffectsPass::cap_triangles * 3;
}
} // namespace

WeaponLook default_weapon_look() noexcept {
    WeaponLook l{};
    l.ripple_speed = wcore::ripple_speed;
    l.ring_width = wcore::ring_width;
    l.ring_gain = wcore::ring_gain;
    l.core_s = float(wcore::core_ms) * 1e-3f;
    l.core_cells = wcore::core_cells;
    l.core_gain = wcore::core_gain;
    l.fade_s = float(wcore::fade_ms) * 1e-3f;
    l.stagger = wcore::stagger;
    l.edge_width = wcore::edge_width;
    l.fill_ratio = wcore::fill_ratio;
    l.cell_gain = wcore::cell_gain;
    l.feather_cells = wcore::feather_cells;
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
    constexpr DWORD cull_caps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    if ((caps.PrimitiveMiscCaps & cull_caps) != cull_caps) return refuse("cull_caps");
    if (caps.MaxPrimitiveCount < cap_triangles || caps.MaxVertexIndex < cap_vertices || caps.MaxStreams < 1u)
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
    if (!cap_built_) {
        cap_built_ = build_cap(cap_, indices_);
        if (!cap_built_) return refuse("cap_mesh", E_FAIL);
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
// The pixel constants c2..c14 of one draw (src/effects/weapon_hex_patch.hlsl): lattice, colour, patch, timing, gains,
// shape, anchor frame, hits. Returns false for a degenerate draw (skipped, never drawn wrong).
bool hex_block(const WeaponLook& l, float cell, float radius, float seed, float surface2, const float colour[3],
               const float* anchor, const float* anchor_u, const float* anchor_v, unsigned hits, const float (*hit)[2],
               const float* ages, float out[13 * 4]) noexcept {
    if (!(cell > 0.f) || !(radius > 0.f) || !finite(cell) || !finite(radius) || !finite(seed) || !finite(surface2) ||
        !finite_all(colour, 3) || hits < 1 || hits > wcore::hit_slots)
        return false;
    const float v = l.ripple_speed * radius, core_r = l.core_cells * cell, feather = l.feather_cells * cell;
    const float block[9 * 4] = {
        1.f / cell, cell, seed, surface2,                                        // c2 lattice
        colour[0], colour[1], colour[2], 0.f,                                    // c3 colour
        radius, 1.f / radius, 1.f / v, v,                                        // c4 patch
        1.f / l.fade_s, l.stagger, 1.f / l.core_s, l.edge_width,                 // c5 timing
        l.cell_gain, l.fill_ratio, l.ring_gain, l.core_gain,                     // c6 gains
        0.5f - l.edge_width, 1.f / (l.ring_width * radius), 1.f / feather, 1.f / (core_r * core_r), // c7 shape
        anchor[0], anchor[1], anchor[2], 0.f,                                    // c8..c10 the anchor frame
        anchor_u[0], anchor_u[1], anchor_u[2], 0.f,
        anchor_v[0], anchor_v[1], anchor_v[2], 0.f};
    std::memcpy(out, block, sizeof block);
    for (unsigned k = 0; k < 4; ++k) {
        float* h = out + 36 + 4 * k;
        const bool on = k < hits;
        h[0] = on ? hit[k][0] : 0.f;
        h[1] = on ? hit[k][1] : 0.f;
        h[2] = on ? ages[k] : 0.f;
        h[3] = on ? 1.f : 0.f;
        if (on && (!finite(h[0]) || !finite(h[1]) || !finite(h[2]))) return false;
    }
    return finite_all(out, 13 * 4);
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
    if (!(l.ripple_speed > 0.f) || !(l.ring_width > 0.f) || !(l.core_s > 0.f) || !(l.core_cells > 0.f) ||
        !(l.fade_s > 0.f) || !(l.edge_width > 0.f) || !(l.edge_width < 0.5f) || !(l.feather_cells > 0.f) ||
        !(l.stagger >= 0.f) || !(l.stagger < 2.f) || !finite(l.ring_gain) || !finite(l.core_gain) ||
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
        DWORD cull = D3DCULL_NONE; // the state list's value
        for (unsigned i = 0; i < f.patch_count && SUCCEEDED(hr); ++i) {
            const WeaponPatch& p = f.patches[i];
            bool ok = finite(p.soft) && finite_all(p.centre, 3) && finite_all(p.axes, 9) && finite_all(p.cap_dir, 3) &&
                      finite_all(p.cap_u, 3) && finite_all(p.cap_v, 3) && finite(p.cap_angle) && p.cap_angle > 0.f &&
                      finite(p.surface) && p.surface > 0.f;
            float ps[13 * 4];
            ok = ok && hex_block(l, p.cell, p.radius, p.seed, 2.f * p.surface, p.colour, p.anchor, p.anchor_u, p.anchor_v,
                                 p.hits, p.hit, p.age_s, ps);
            // The winding of the outward faces on screen (see build_cap): det A0 . (A1 x A2).
            const float* a = p.axes;
            const float det = ok ? a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                                       a[2] * (a[3] * a[7] - a[4] * a[6])
                                 : 0.f;
            if (!ok || !finite(det) || det == 0.f) continue; // a degenerate patch is skipped, never drawn wrong
            DWORD want = det > 0.f ? DWORD(D3DCULL_CCW) : DWORD(D3DCULL_CW); // keep the front (outward) faces
            if (faults_ & 16u) want = D3DCULL_NONE; // fixture bit 4: CULLMODE NONE (the cull's A/B)
            if (want != cull && step(WeaponEffectsStep::Patches, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, want)))
                cull = want;
            const float angle = p.cap_angle < 3.14159265f ? p.cap_angle : 3.14159265f;
            const float vs[7 * 4] = {p.centre[0], p.centre[1], p.centre[2], f.near_z,
                                     a[0], a[1], a[2], p.soft, a[3], a[4], a[5], 0.f, a[6], a[7], a[8], 0.f,
                                     p.cap_dir[0], p.cap_dir[1], p.cap_dir[2], angle,
                                     p.cap_u[0], p.cap_u[1], p.cap_u[2], 0.f, p.cap_v[0], p.cap_v[1], p.cap_v[2], 0.f};
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, vs, 7));
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, 13));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, cap_vertices, cap_triangles, indices_, D3DFMT_INDEX16, cap_,
                    UINT(sizeof cap_[0])));
                if (step(WeaponEffectsStep::Patches, draw)) {
                    ++r.patches;
                    r.drew = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && f.decal_count) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        if (f.patch_count) // the patches may have left a winding cull; the decal quads are not wound
            step(WeaponEffectsStep::Decals, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Decals, call<SetDeclarationFn>(SetVertexDeclaration)(d, decal_declaration_));
        step(WeaponEffectsStep::Decals, call<SetVsFn>(SetVertexShader)(d, decal_vs_));
        step(WeaponEffectsStep::Decals, call<SetPsFn>(SetPixelShader)(d, decal_ps_));
        step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
        static constexpr float origin[1][2] = {{0.f, 0.f}};
        static constexpr float frame_a[3] = {0.f, 0.f, 1.f}, frame_u[3] = {1.f, 0.f, 0.f}, frame_v[3] = {0.f, 1.f, 0.f};
        for (unsigned i = 0; i < f.decal_count && SUCCEEDED(hr); ++i) {
            const WeaponDecal& c = f.decals[i];
            float ps[13 * 4];
            bool ok = finite(c.radius) && c.radius > 0.f && finite(c.age_s) && finite(c.soft) && c.soft > 0.f &&
                      finite_all(c.centre, 3) &&
                      hex_block(l, c.cell, c.radius, float(12), 0.f, c.colour, frame_a, frame_u, frame_v, 1, origin,
                                &c.age_s, ps);
            if (!ok) continue;
            DecalVertex v[4];
            for (unsigned k = 0; k < 4; ++k)
                v[k] = DecalVertex{{c.centre[0], c.centre[1], c.centre[2], c.radius},
                                   {corners[k][0], corners[k][1], c.soft, 1.f}};
            step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, 13));
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
