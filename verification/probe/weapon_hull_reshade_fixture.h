#pragma once
// The hull hit re-shade's fixture parts (docs/architecture/hull-hit-look.md section 4), shared by weapon_effects_fixture.cpp
// (weapon_hull_reshade_cases.h: the checks and the cost) and weapon_effects_projectile_dump.cpp (weapon_hull_reshade_dump.h:
// the look frames). The standard_lighting DEFAULT pair is the game's own: run_weapon_effects.py extracts its VS and both
// PS from the FL catalogues into the untracked working directory (classic_vs.bin 494fe349b8bc12ec, classic_ps.bin
// ced7edef48059e84, classic_ps2s.bin 0d5848c8489b6630; each FNV-1a 64 checked), with the material submit's constants as
// weapon_lone_classic_cases.h sets them (g_TexMatrix c37..c38 the cell offset, g_AlphaValue c39.x the node fade, the
// emissive lit, no point light, fog off). The effects DEFAULT pair is reshade_fx's stand-in of its interface (vs_2_0:
// g_TexMatrix c10..c11, g_AlphaValue c12.x into oD0.x; ps_2_0: the colour matrix c0..c2 x oD0.x, alpha the texel's).
// Our programs are the production embedded bytecode, swapped in through the production device sequence
// (muzzle_reshade_route.h begin / end with the hull registers) exactly as MotionOutput::begin_hull_reshade /
// finish_hull_reshade do. Never launches the game; no game bytes are tracked.
#include "weapon_bullet_reshade_fixture.h" // reshade_fx: make_texture, clip_rows, the lone stand-in pair
#include "../../src/proxy/hull_reshade_core.h"
#include "../../src/proxy/muzzle_reshade_route.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace hull_fx {
namespace hh = x3m::weapon_effects::hull_reshade;
namespace mz = x3m::weapon_effects::muzzle_reshade;
constexpr DWORD effects_program[] = {
#include "../../src/renderer/weapon_hull_reshade_effects_pixel_program_inc.h"
};
constexpr DWORD classic_program[] = {
#include "../../src/renderer/weapon_hull_reshade_classic_pixel_program_inc.h"
};
// The classic body vertex the game VS declares: POSITION, TEXCOORD0, NORMAL (stride 32).
struct ClassicVertex {
    float x, y, z, u, v, nx, ny, nz;
};
static_assert(sizeof(ClassicVertex) == 32, "the classic body vertex");
inline const D3DVERTEXELEMENT9* classic_declaration() {
    static const D3DVERTEXELEMENT9 d[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                          D3DDECL_END()};
    return d;
}
inline bool load(const char* path, std::vector<DWORD>& out) {
    out.clear();
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    DWORD w = 0;
    while (std::fread(&w, 4, 1, f) == 1) out.push_back(w);
    std::fclose(f);
    return out.size() > 2 && out.back() == 0x0000ffffu;
}
inline std::uint64_t hash(const std::vector<DWORD>& code) {
    return x3m::weapon_effects::reshade::fnv1a64(reinterpret_cast<const unsigned char*>(code.data()), code.size() * 4);
}
// The vanilla look of an impact body under the standard_lighting pair for the dump's frames: the texel times the
// saturated vertex colour (the game VS's COLOR0.rgb, the white emissive c40 plus the point lights: the texel at the
// fixture's constants; muzzle_fx's emissive law). The game's own PS draws black under the fixture's material constants
// (they are not the impact materials'), so the frames' vanilla panel uses this; the checks use the game PS.
constexpr char vanilla_classic_ps_source[] =
    "sampler2D DiffuseTexSampler : register(s0);\n"
    "sampler2D LightMapTexSampler : register(s2);\n"
    "float4 g_EnableGlow : register(c4);\n"
    "float4 main(float4 c : COLOR0, float2 uv : TEXCOORD0_centroid) : COLOR0 {\n"
    "  float4 t = tex2D(DiffuseTexSampler, uv);\n"
    "  return float4(saturate(c.rgb) * t.rgb, lerp(t.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * c.a); }\n";
// The game pair and the stand-ins, created on one device. classic_ps[0] one-sided, [1] two-sided; ours[pair].
struct Programs {
    IDirect3DVertexShader9* classic_vs = nullptr;
    IDirect3DPixelShader9* classic_ps[2] = {nullptr, nullptr};
    IDirect3DVertexShader9* effects_vs = nullptr;
    IDirect3DPixelShader9* effects_ps = nullptr;
    IDirect3DPixelShader9* ours[2] = {nullptr, nullptr};
    IDirect3DPixelShader9* classic_vanilla = nullptr; // vanilla_classic_ps_source (the dump's vanilla panel)
    IDirect3DVertexDeclaration9 *classic_decl = nullptr, *effects_decl = nullptr;
    bool loaded = false, hashes = false;
    void release() {
        IUnknown* all[] = {classic_vs, classic_ps[0], classic_ps[1], effects_vs, effects_ps, ours[0], ours[1],
                           classic_vanilla, classic_decl, effects_decl};
        for (IUnknown* p : all)
            if (p) p->Release();
        *this = Programs{};
    }
};
// effects_vs_code / effects_ps_code: reshade_fx::lone_vs_source / lone_ps_source compiled by the caller (vs_2_0, ps_2_0);
// vanilla_code (optional): vanilla_classic_ps_source compiled (ps_3_0).
inline bool create(IDirect3DDevice9* dev, const DWORD* effects_vs_code, const DWORD* effects_ps_code, Programs& p,
                   const DWORD* vanilla_code = nullptr) {
    namespace rs = x3m::weapon_effects::reshade;
    std::vector<DWORD> vs, ps, ps2s;
    p.loaded = load("classic_vs.bin", vs) && load("classic_ps.bin", ps) && load("classic_ps2s.bin", ps2s);
    const std::uint64_t* fl = rs::classic_pixel_shader_pairs[rs::pair_fl];
    p.hashes = p.loaded && hash(vs) == hh::classic_vertex_shader && hash(ps) == fl[0] && hash(ps2s) == fl[1];
    return p.hashes && effects_vs_code && effects_ps_code && SUCCEEDED(dev->CreateVertexShader(vs.data(), &p.classic_vs)) &&
           SUCCEEDED(dev->CreatePixelShader(ps.data(), &p.classic_ps[0])) &&
           SUCCEEDED(dev->CreatePixelShader(ps2s.data(), &p.classic_ps[1])) &&
           SUCCEEDED(dev->CreateVertexShader(effects_vs_code, &p.effects_vs)) &&
           SUCCEEDED(dev->CreatePixelShader(effects_ps_code, &p.effects_ps)) &&
           SUCCEEDED(dev->CreatePixelShader(effects_program, &p.ours[hh::pair_effects])) &&
           SUCCEEDED(dev->CreatePixelShader(classic_program, &p.ours[hh::pair_classic])) &&
           SUCCEEDED(dev->CreateVertexDeclaration(classic_declaration(), &p.classic_decl)) &&
           SUCCEEDED(dev->CreateVertexDeclaration(reshade_fx::lone_declaration(), &p.effects_decl)) &&
           (!vanilla_code || SUCCEEDED(dev->CreatePixelShader(vanilla_code, &p.classic_vanilla)));
}
// The impact pass: the game's material constants for the pair (clip rows c0..c3 / c24..c27, g_TexMatrix, g_AlphaValue,
// the classic emissive and glow), ADD ONE / dst, Z off, no cull, alpha test off, linear wrap samplers, the texture on
// s0 and a light map on s2 (classic). tex_matrix: (u', v') = (u, v, 1) . row0 / row1 (the flipbook's cell offset).
inline void bind(IDirect3DDevice9* dev, const Programs& p, unsigned pair, bool two_sided, const float clip[16],
                 const float tex_matrix[8], float fade, float glow, IDirect3DBaseTexture9* tex,
                 IDirect3DBaseTexture9* light, DWORD dst) {
    if (pair == hh::pair_classic) {
        float c[42 * 4]{};
        for (unsigned i = 0; i < 16; ++i) c[24 * 4 + i] = clip[i];
        for (unsigned r = 0; r < 3; ++r) c[(28 + r) * 4 + r] = c[(31 + r) * 4 + r] = c[(34 + r) * 4 + r] = 1.f;
        for (unsigned i = 0; i < 8; ++i) c[37 * 4 + i] = tex_matrix[i];
        c[39 * 4] = fade;
        c[40 * 4] = c[40 * 4 + 1] = c[40 * 4 + 2] = c[40 * 4 + 3] = 1.f;
        dev->SetVertexShaderConstantF(0, c, 42);
        const int lights[4] = {0, 0, 1, 0};
        dev->SetVertexShaderConstantI(0, lights, 1);
        const BOOL fog = FALSE;
        dev->SetVertexShaderConstantB(0, &fog, 1);
        float q[10 * 4]{};
        q[0] = glow;                                             // c4 g_EnableGlow
        q[8 * 4] = q[8 * 4 + 1] = q[8 * 4 + 2] = q[8 * 4 + 3] = 1.f; // c12 g_MatQualityColor
        dev->SetPixelShaderConstantF(4, q, 10);
        dev->SetVertexShader(p.classic_vs);
        dev->SetVertexDeclaration(p.classic_decl);
        dev->SetPixelShader(p.classic_ps[two_sided ? 1 : 0]);
    } else {
        float c[13 * 4]{};
        for (unsigned i = 0; i < 16; ++i) c[i] = clip[i];
        for (unsigned i = 0; i < 3; ++i) c[40 + i] = tex_matrix[i], c[44 + i] = tex_matrix[4 + i];
        c[48] = fade;
        dev->SetVertexShaderConstantF(0, c, 13);
        const float colour_matrix[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        dev->SetPixelShaderConstantF(0, colour_matrix, 3);
        dev->SetVertexShader(p.effects_vs);
        dev->SetVertexDeclaration(p.effects_decl);
        dev->SetPixelShader(p.effects_ps);
    }
    dev->SetTexture(0, tex);
    dev->SetTexture(1, nullptr);
    dev->SetTexture(2, light);
    dev->SetTexture(3, nullptr);
    for (DWORD s = 0; s < 4; ++s) {
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, dst);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}
// The production device sequence (muzzle_reshade_route.h, the hull registers per pair) on this device.
inline auto device_ops(IDirect3DDevice9* dev, unsigned pair) {
    const UINT reg = pair == hh::pair_classic ? hh::classic_register : hh::effects_register;
    return mz::calls([dev, reg](const float* v) { return dev->SetPixelShaderConstantF(reg, v, hh::constant_count); },
                     [dev](IDirect3DPixelShader9* q) { return dev->SetPixelShader(q); });
}
// One substituted draw: begin, draw(), end. *restored the begin's and end's success.
template <class Draw>
inline HRESULT reshaded_draw(IDirect3DDevice9* dev, unsigned pair, IDirect3DPixelShader9* ours, IDirect3DPixelShader9* game,
                             const float block[hh::constant_floats], const float app[hh::constant_floats], Draw draw,
                             bool* restored) {
    auto ops = device_ops(dev, pair);
    mz::Applied applied;
    const HRESULT hr = mz::begin(ops, ours, block, app, applied);
    const HRESULT drawn = SUCCEEDED(hr) ? draw() : hr;
    const HRESULT back = mz::end(ops, applied, game, app);
    *restored = SUCCEEDED(hr) && SUCCEEDED(back);
    return drawn;
}
// A camera-facing quad in camera space (the fixture's world): centre c (metres), half extent h, rolled by `roll`, UV
// over [u0, u1] x [v0, v1] (the cell; the game's quad maps 0..0.25 and g_TexMatrix offsets it). Six vertices of both
// layouts (the effects stand-in reads the first 20 bytes).
inline void quad(const float c[3], float h, float roll, float u0, float v0, float u1, float v1, ClassicVertex out[6]) {
    const float cr = std::cos(roll) * h, sr = std::sin(roll) * h;
    const float px[4] = {-cr + sr, cr + sr, -cr - sr, cr - sr}, py[4] = {-sr - cr, sr - cr, -sr + cr, sr + cr};
    const float us[4] = {u0, u1, u0, u1}, vs[4] = {v1, v1, v0, v0};
    const unsigned idx[6] = {0, 2, 1, 1, 2, 3};
    for (unsigned i = 0; i < 6; ++i) {
        const unsigned k = idx[i];
        out[i] = ClassicVertex{c[0] + px[k], c[1] + py[k], c[2], us[k], vs[k], 0.f, 0.f, -1.f};
    }
}
// The effects stand-in's 20-byte vertex from a classic one.
inline void effects_vertices(const ClassicVertex* in, unsigned n, reshade_fx::LoneVertex* out) {
    for (unsigned i = 0; i < n; ++i) out[i] = reshade_fx::LoneVertex{in[i].x, in[i].y, in[i].z, in[i].u, in[i].v};
}
// A flat 1 x 1 MANAGED A8R8G8B8 texture.
inline IDirect3DTexture9* flat(IDirect3DDevice9* dev, const unsigned char rgba[4]) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) {
        t->Release();
        return nullptr;
    }
    auto* b = static_cast<unsigned char*>(lr.pBits);
    b[0] = rgba[2], b[1] = rgba[1], b[2] = rgba[0], b[3] = rgba[3];
    t->UnlockRect(0);
    return t;
}
// The flipbook cell offset of frame k on a 4 x 4 page (g_TexMatrix rows: u' = u + du, v' = v + dv).
inline void cell_matrix(unsigned k, float out[8]) {
    const unsigned i = k % 16;
    const float m[8] = {1.f, 0.f, 0.25f * float(i % 4), 0.f, 0.f, 1.f, 0.25f * float(i / 4), 0.f};
    for (unsigned j = 0; j < 8; ++j) out[j] = m[j];
}
// Body units to metres: the node scale times the engine's 500 units per metre (the muzzle dump's inference); the impact
// quads and the v/00587 mesh span +-100000 units.
constexpr float engine_units_per_metre = 500.f, body_half_units = 100000.f;
inline float half_metres(float size) { return size * body_half_units / engine_units_per_metre; }
// The node fade of a FADEOUT element at age t of life T: node +0x13c ramps 254 -> 1 (hull-hit-draw.md section 2).
inline float fadeout(float t, float life) {
    const float k = t < 0.f ? 0.f : t > life ? 1.f : t / life;
    return (254.f - 253.f * k) / 255.f;
}
} // namespace hull_fx
