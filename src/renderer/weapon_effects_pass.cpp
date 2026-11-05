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
    float shade[4];  // corner x, y, age, alpha
};
static_assert(sizeof(DecalVertex) == 32, "the decal declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
void normalize3(float* p) noexcept {
    const float n = scalar::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (n > 0.f)
        for (unsigned k = 0; k < 3; ++k) p[k] /= n;
}
// The unit icosphere: an icosahedron subdivided twice (12 -> 42 -> 162 vertices, 20 -> 80 -> 320 triangles), midpoints
// shared through a linear edge search (attach time only). Winding: every triangle (a, b, c) has (b - a) x (c - a)
// pointing outward (the base faces are counter-clockwise seen from outside in a right-handed frame and the 1:4 split
// keeps each child's order), so a shell drawn through view axes A shows its outward faces clockwise on screen when
// det A > 0 and counter-clockwise when det A < 0; run() culls the other winding per shell (review F4), and the pixel
// program still rejects a fragment whose interpolated normal faces away.
bool build_sphere(float (*v)[3], std::uint16_t* idx) noexcept {
    const float t = 1.6180339887f;
    const float base[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                               {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    const std::uint16_t faces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                                        {3, 8, 9},  {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    constexpr unsigned vmax = WeaponEffectsPass::sphere_vertices, imax = WeaponEffectsPass::sphere_triangles * 3;
    unsigned vertices = 12, triangles = 20;
    for (unsigned k = 0; k < 12; ++k) {
        std::memcpy(v[k], base[k], 12);
        normalize3(v[k]);
    }
    std::memcpy(idx, faces, sizeof faces);
    std::uint16_t edge_a[480], edge_b[480], edge_m[480], next[imax];
    for (unsigned level = 0; level < 2; ++level) {
        unsigned edges = 0, out = 0;
        auto midpoint = [&](std::uint16_t a, std::uint16_t b) -> std::uint16_t {
            const std::uint16_t lo = a < b ? a : b, hi = a < b ? b : a;
            for (unsigned e = 0; e < edges; ++e)
                if (edge_a[e] == lo && edge_b[e] == hi) return edge_m[e];
            if (edges >= 480 || vertices >= vmax) return 0xffffu;
            float* m = v[vertices];
            for (unsigned k = 0; k < 3; ++k) m[k] = 0.5f * (v[lo][k] + v[hi][k]);
            normalize3(m);
            edge_a[edges] = lo;
            edge_b[edges] = hi;
            edge_m[edges] = std::uint16_t(vertices);
            ++edges;
            return std::uint16_t(vertices++);
        };
        for (unsigned f = 0; f < triangles; ++f) {
            const std::uint16_t a = idx[f * 3], b = idx[f * 3 + 1], c = idx[f * 3 + 2];
            const std::uint16_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            if (ab == 0xffffu || bc == 0xffffu || ca == 0xffffu || out + 12 > imax) return false;
            const std::uint16_t tris[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            std::memcpy(next + out, tris, sizeof tris);
            out += 12;
        }
        std::memcpy(idx, next, out * sizeof(std::uint16_t));
        triangles = out / 3;
    }
    return vertices == vmax && triangles == WeaponEffectsPass::sphere_triangles;
}
} // namespace

WeaponLook default_weapon_look() noexcept {
    WeaponLook l{};
    l.rim_gain = wcore::rim_gain;
    l.rim_power = wcore::rim_power;
    l.hex_scale = wcore::hex_scale;
    l.hex_gain = wcore::hex_gain;
    l.ripple_s = float(wcore::ripple_ms) * 1e-3f;
    l.ring_speed = wcore::ring_speed;
    l.ring_width = wcore::ring_width;
    l.flash_s = float(wcore::flash_ms) * 1e-3f;
    l.flash_gain = wcore::flash_gain;
    l.flash_sharpness = wcore::flash_sharpness;
    l.decal_ring_speed = wcore::decal_ring_speed;
    l.decal_ring_width = wcore::decal_ring_width;
    l.decal_hex_scale = wcore::decal_hex_scale;
    l.soft = wcore::soft;
    l.ring_count = wcore::ring_count;
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
    if (caps.MaxPrimitiveCount < sphere_triangles || caps.MaxVertexIndex < sphere_vertices || caps.MaxStreams < 1u)
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
    if (!sphere_built_) {
        sphere_built_ = build_sphere(sphere_, indices_);
        if (!sphere_built_) return refuse("sphere", E_FAIL);
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
    if (!f.width || !f.height || !f.lane || (f.shell_count && !f.shells) || (f.decal_count && !f.decals) ||
        f.shell_count > wcore::max_shells || f.decal_count > wcore::max_decals || !(f.m00 > 0.f) || !(f.m11 > 0.f) ||
        !finite(f.m20) || !finite(f.m21) || !(f.near_z > 0.f))
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    if (!f.shell_count && !f.decal_count) return finish(S_FALSE);
    const WeaponLook& l = f.look;
    if (!(l.ripple_s > 0.f) || !(l.flash_s > 0.f) || !(l.ring_width > 0.f) || !(l.decal_ring_width > 0.f))
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
    if (SUCCEEDED(hr) && f.shell_count) {
        const float look[4] = {l.rim_gain, 0.f, l.rim_power, l.hex_scale};
        const float flash[4] = {l.flash_s, l.flash_gain, l.flash_sharpness, l.hex_gain};
        step(WeaponEffectsStep::Shells, call<SetDeclarationFn>(SetVertexDeclaration)(d, shell_declaration_));
        step(WeaponEffectsStep::Shells, call<SetVsFn>(SetVertexShader)(d, shell_vs_));
        step(WeaponEffectsStep::Shells, call<SetPsFn>(SetPixelShader)(d, shell_ps_));
        step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, look, 1));
        step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 9, flash, 1));
        const unsigned slots = l.ring_count < 1 ? 1 : l.ring_count > 4 ? 4 : l.ring_count;
        DWORD cull = D3DCULL_NONE; // the state list's value
        for (unsigned i = 0; i < f.shell_count && SUCCEEDED(hr); ++i) {
            const WeaponShell& s = f.shells[i];
            bool ok = s.hits >= 1 && finite(s.soft);
            for (unsigned k = 0; k < 3 && ok; ++k) ok = finite(s.centre[k]) && finite(s.tint[k]);
            for (unsigned k = 0; k < 9 && ok; ++k) ok = finite(s.axes[k]);
            // The winding of the outward faces on screen (see build_sphere): det A0 . (A1 x A2).
            const float* a = s.axes;
            const float det = ok ? a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                                       a[2] * (a[3] * a[7] - a[4] * a[6])
                                 : 0.f;
            if (!ok || !finite(det) || det == 0.f) continue; // a degenerate shell is skipped, never drawn wrong
            DWORD want = det > 0.f ? DWORD(D3DCULL_CCW) : DWORD(D3DCULL_CW); // keep the front (outward) faces
            if (faults_ & 16u) want = D3DCULL_NONE; // fixture bit 4: the former CULLMODE NONE draw (the cull's A/B)
            if (want != cull && step(WeaponEffectsStep::Shells, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, want)))
                cull = want;
            const unsigned hits = s.hits < slots ? s.hits : slots;
            const float centre[4] = {s.centre[0], s.centre[1], s.centre[2], f.near_z};
            const float axes[12] = {s.axes[0], s.axes[1], s.axes[2], s.soft, s.axes[3], s.axes[4], s.axes[5], 0.f,
                                    s.axes[6], s.axes[7], s.axes[8], 0.f};
            const float tint[4] = {s.tint[0], s.tint[1], s.tint[2], 1.f};
            const float timing[4] = {float(hits), l.ripple_s, l.ring_speed, l.ring_width};
            float slot[16]{}, amp[4]{};
            for (unsigned k = 0; k < hits; ++k) {
                std::memcpy(slot + 4 * k, s.dir[k], 12);
                slot[4 * k + 3] = s.age_s[k];
                amp[k] = s.amp[k];
            }
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, centre, 1));
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 3, axes, 3));
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 3, tint, 1));
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 4, timing, 1));
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 5, slot, 4));
            step(WeaponEffectsStep::Shells, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 10, amp, 1));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, sphere_vertices, sphere_triangles, indices_, D3DFMT_INDEX16, sphere_,
                    UINT(sizeof sphere_[0])));
                if (step(WeaponEffectsStep::Shells, draw)) {
                    ++r.shells;
                    r.drew = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && f.decal_count) {
        const float limits[4] = {f.near_z, l.soft, 0.f, 0.f};
        const float flash[4] = {l.flash_s, l.flash_gain, l.flash_sharpness, l.hex_gain};
        if (f.shell_count) // the shells may have left a winding cull; the decal quads are not wound
            step(WeaponEffectsStep::Decals, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Decals, call<SetDeclarationFn>(SetVertexDeclaration)(d, decal_declaration_));
        step(WeaponEffectsStep::Decals, call<SetVsFn>(SetVertexShader)(d, decal_vs_));
        step(WeaponEffectsStep::Decals, call<SetPsFn>(SetPixelShader)(d, decal_ps_));
        step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 4, flash, 1));
        static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
        for (unsigned i = 0; i < f.decal_count && SUCCEEDED(hr); ++i) {
            const WeaponDecal& c = f.decals[i];
            bool ok = finite(c.radius) && c.radius > 0.f && finite(c.age_s) && finite(c.amp);
            for (unsigned k = 0; k < 3 && ok; ++k) ok = finite(c.centre[k]) && finite(c.tint[k]);
            if (!ok) continue;
            const float look[4] = {c.amp, l.decal_hex_scale, l.decal_ring_speed, l.decal_ring_width};
            const float tint[4] = {c.tint[0], c.tint[1], c.tint[2], l.ripple_s};
            DecalVertex v[4];
            for (unsigned k = 0; k < 4; ++k) {
                v[k] = DecalVertex{{c.centre[0], c.centre[1], c.centre[2], c.radius},
                                   {corners[k][0], corners[k][1], c.age_s, 1.f}};
            }
            step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, look, 1));
            step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 3, tint, 1));
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
