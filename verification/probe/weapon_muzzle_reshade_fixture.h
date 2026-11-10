#pragma once
// The muzzle flash re-shade's fixture parts (docs/architecture/muzzle-flash-look.md), shared by weapon_effects_fixture.cpp
// (checks and the substitution's cost) and weapon_effects_projectile_dump.cpp (the look frames). The fixture cannot load
// the game's standard_lighting2s.fb, so the flash is drawn the way the game draws it with stand-ins built here: a
// vs_3_0 (compiled) with the game VS 494fe349b8bc12ec's output ABI (COLOR0 = the material's emissive and alpha from c4,
// TEXCOORD0 centroid = the UV, TEXCOORD1 = the camera minus the world position, passed per vertex: the fixture's world
// is camera space, so it is -P; the position through the c0..c3 rows) and a ps_3_0 (compiled) doing the part of the game
// PS 0d5848c8489b6630 the flash's emissive material exercises at the default quality colour (saturate(COLOR0.rgb) x
// texel; alpha lerp(texel.a, light.a, c4.x) x COLOR0.a; the lighting terms are zero at diffuse strength 0 and the
// specular, reflection and light-map colour terms are left out), stream 0 stride 32 (POSITION FLOAT3, TEXCOORD0 FLOAT2,
// TEXCOORD1 FLOAT3),
// ADD ONE / ONE, Z test on and Z write off, no cull. The re-shade program is the production embedded ps_3_0 bytecode,
// swapped in exactly as MotionOutput::begin_muzzle_reshade / finish_muzzle_reshade do (muzzle_reshade_route.h). Never
// launches the game; no game bytes: the stand-in mesh and texture are procedural (50 fanned quads mapping the body 445
// cell, a streak with the cell's radial profile and hue), and the dump loads the real mesh and texture only from
// untracked files run_weapon_effects.py --dump-case muzzle prepares from the FL catalogues and the local lab assets.
#include "weapon_bullet_reshade_fixture.h" // make_texture, clip_rows
#include "../../src/proxy/muzzle_reshade_core.h"
#include "../../src/proxy/muzzle_reshade_route.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace muzzle_fx {
namespace mz = x3m::weapon_effects::muzzle_reshade;
constexpr DWORD program[] = {
#include "../../src/renderer/weapon_muzzle_reshade_pixel_program_inc.h"
};
constexpr char vs_source[] =
    "float4 m[4] : register(c0);\n"
    "float4 emissive : register(c4);\n"
    "struct O { float4 p : POSITION; float4 c : COLOR0; float2 uv : TEXCOORD0_centroid; float3 v : TEXCOORD1; };\n"
    "O main(float4 p : POSITION, float2 uv : TEXCOORD0, float3 v : TEXCOORD1) {\n"
    "  O o; float4 q = float4(p.xyz, 1);\n"
    "  o.p = float4(dot(q, m[0]), dot(q, m[1]), dot(q, m[2]), dot(q, m[3])); o.c = emissive; o.uv = uv; o.v = v;\n"
    "  return o; }\n";
constexpr char vanilla_ps_source[] =
    "sampler2D DiffuseTexSampler : register(s0);\n"
    "sampler2D LightMapTexSampler : register(s2);\n"
    "float4 g_EnableGlow : register(c4);\n"
    "float4 main(float4 c : COLOR0, float2 uv : TEXCOORD0_centroid) : COLOR0 {\n"
    "  float4 t = tex2D(DiffuseTexSampler, uv);\n"
    "  return float4(saturate(c.rgb) * t.rgb, lerp(t.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * c.a); }\n";
struct Vertex {
    float x, y, z, u, v, vx, vy, vz;
};
static_assert(sizeof(Vertex) == 32, "position, UV, view vector");
inline const D3DVERTEXELEMENT9* declaration() {
    static const D3DVERTEXELEMENT9 d[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                                          D3DDECL_END()};
    return d;
}
// Body 445's 100 textured triangles in body units: x, y, z, u, v per vertex.
constexpr unsigned max_vertices = 300;
struct Mesh {
    float p[max_vertices][5];
    unsigned n = 0;
};
// The cell every quad maps (muzzle_inputs_out.txt).
constexpr float cell_u0 = 0.002853f, cell_u1 = 0.411944f, cell_v0 = -0.001845f, cell_v1 = 0.511156f;
// 50 quads fanned from the gun: base edge (v 0.511) near z 0, far edge (v 0) up to 100000 units along directions
// tilted 10..55 degrees from +z, each 25000 units wide (the real quads' extents: 25240 x 29176 to 60000 long).
inline Mesh standin_mesh() {
    Mesh m;
    std::uint32_t seed = 0x445u;
    const auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.f;
    };
    const auto put = [&](const float q[3], float u, float v) {
        float* o = m.p[m.n++];
        o[0] = q[0], o[1] = q[1], o[2] = q[2], o[3] = u, o[4] = v;
    };
    for (unsigned k = 0; k < 50; ++k) {
        const float tilt = (10.f + 45.f * rnd()) * 0.0174533f, phi = 6.2831853f * rnd(), psi = 6.2831853f * rnd();
        const float length = 29000.f + 50000.f * rnd(), half = 12500.f, base = 12000.f * (rnd() - 0.4f);
        const float d[3] = {std::sin(tilt) * std::cos(phi), std::sin(tilt) * std::sin(phi), std::cos(tilt)};
        // a unit vector across d: the cross product with an axis, then rotated by psi about d
        const float a[3] = {-std::sin(phi), std::cos(phi), 0.f};
        const float b[3] = {d[1] * a[2] - d[2] * a[1], d[2] * a[0] - d[0] * a[2], d[0] * a[1] - d[1] * a[0]};
        const float w[3] = {std::cos(psi) * a[0] + std::sin(psi) * b[0], std::cos(psi) * a[1] + std::sin(psi) * b[1],
                            std::cos(psi) * a[2] + std::sin(psi) * b[2]};
        float c00[3], c10[3], c01[3], c11[3]; // corner (u side, v side): v1 = base (gun), v0 = far
        for (unsigned i = 0; i < 3; ++i) {
            const float b0 = (i == 2 ? base : 0.f), f0 = b0 + length * d[i];
            c01[i] = b0 - half * w[i], c11[i] = b0 + half * w[i];
            c00[i] = f0 - half * w[i], c10[i] = f0 + half * w[i];
        }
        put(c00, cell_u0, cell_v0), put(c10, cell_u1, cell_v0), put(c01, cell_u0, cell_v1);
        put(c11, cell_u1, cell_v1), put(c01, cell_u0, cell_v1), put(c10, cell_u1, cell_v0);
    }
    return m;
}
// The stand-in atlas, 256 x 256 RGBA bytes: inside the cell a streak along v (brightest towards the gun edge, v 0.45),
// narrowing away from the centre line, with a deterministic texel noise of the real cell's order (sd ~0.1 mid-radius),
// the cell's hue (1, 0.485, 0.166); black outside; alpha 255 (the JPEG has none). The streak's amplitude 0.42 gives the
// real cell's mean luminance, 0.0539 over its 13624 texels (muzzle_inputs_out.txt; 0.36 gave 0.0462, a dimmer vanilla
// and an energy ratio at the 0.2 gate's edge: review F11, 2026-11-09).
inline std::vector<unsigned char> standin_texture(unsigned* side) {
    const unsigned n = 256;
    *side = n;
    std::vector<unsigned char> t(std::size_t(n) * n * 4, 0);
    std::uint32_t seed = 384u;
    const float hue[3] = {1.f, 0.485f, 0.166f}, lum_hue = 0.2126f + 0.7152f * 0.485f + 0.0722f * 0.166f;
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = float(seed >> 8) / 16777216.f;
            const float u = (float(x) + 0.5f) / float(n), v = (float(y) + 0.5f) / float(n);
            unsigned char* p = &t[(std::size_t(y) * n + x) * 4];
            p[3] = 255;
            if (u < cell_u0 || u > cell_u1 || v < 0.f || v > cell_v1) continue;
            const float across = std::fabs(u - 0.2074f) / 0.2045f, along = v / 0.511f;
            const float width = 0.25f + 0.55f * (1.f - along); // wider away from the gun
            float l = across < width ? 0.42f * (1.f - across / width) * (0.15f + 0.85f * along) : 0.f;
            if (along > 0.92f) l *= (1.f - along) / 0.08f;
            l *= 0.4f + 1.2f * noise;
            for (unsigned k = 0; k < 3; ++k) {
                const float c = l / lum_hue * hue[k];
                p[k] = (unsigned char)(c >= 1.f ? 255 : int(c * 255.f + 0.5f));
            }
        }
    return t;
}
// Body units to metres: the node scale times the engine's 500 units per metre (an inference: the scene part's own
// scale taken as 1; muzzle-flash-draw.md section 3 gives the scale law b - (b - a) t^2, peak b 0.031 at 75 ms).
constexpr float peak_scale = 0.031f, engine_units_per_metre = 500.f;
// The textured vertices' bounding-box centre in body units (the crop centre).
inline void mesh_centre(const Mesh& m, float c[3]) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (unsigned i = 0; i < m.n; ++i)
        for (unsigned k = 0; k < 3; ++k) {
            lo[k] = m.p[i][k] < lo[k] ? m.p[i][k] : lo[k];
            hi[k] = m.p[i][k] > hi[k] ? m.p[i][k] : hi[k];
        }
    for (unsigned k = 0; k < 3; ++k) c[k] = 0.5f * (lo[k] + hi[k]);
}
// The flash in camera space (the fixture's world; the camera at 0): its box centre at `centre` (metres), scale `scale`,
// its +z axis yawed `yaw` radians out of the screen plane (z -> (cos yaw, 0, sin yaw)) and rolled `roll` about itself.
// A body point to camera space:
inline void transform(const float mid[3], const float centre[3], float scale, float yaw, float roll, const float p[3],
                      float out[3]) {
    const float s = scale / engine_units_per_metre;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
    const float x0 = (p[0] - mid[0]) * s, y0 = (p[1] - mid[1]) * s, z0 = (p[2] - mid[2]) * s;
    const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0;
    out[0] = centre[0] + z0 * cy - x1 * sy;
    out[1] = centre[1] + y1;
    out[2] = centre[2] + z0 * sy + x1 * cy;
}
// Writes n vertices (TEXCOORD1 = -P) and the VS rows c28..c36 the game would have bound for this node: g_mWorld with the
// placement's rotation times the world scale in its 3x3 (column j the body's axis j in camera space, so column 2 is the
// flash axis the per-draw overlap reads) and the gun (body origin) in .w, g_mViewInverse with the camera (0) in .w.
inline unsigned place(const Mesh& m, const float centre[3], float scale, float yaw, float roll, Vertex* out,
                      float rows[36] = nullptr) {
    float mid[3];
    mesh_centre(m, mid);
    for (unsigned i = 0; i < m.n; ++i) {
        float q[3];
        transform(mid, centre, scale, yaw, roll, m.p[i], q);
        out[i] = Vertex{q[0], q[1], q[2], m.p[i][3], m.p[i][4], -q[0], -q[1], -q[2]};
    }
    if (rows) {
        const float zero[3] = {0.f, 0.f, 0.f};
        float gun[3];
        transform(mid, centre, scale, yaw, roll, zero, gun);
        const float s = scale / engine_units_per_metre;
        for (unsigned i = 0; i < 36; ++i) rows[i] = 0.f;
        // transform's rotation applied to the body axes x, y, z (the columns), times the scale
        const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
        const float axes[3][3] = {{-cr * sy, sr, cr * cy}, {sr * sy, cr, -sr * cy}, {cy, 0.f, sy}};
        for (unsigned j = 0; j < 3; ++j)
            for (unsigned i = 0; i < 3; ++i) rows[i * 4 + j] = s * axes[j][i];
        rows[3] = gun[0], rows[7] = gun[1], rows[11] = gun[2];
        rows[24] = rows[29] = rows[34] = 1.f; // g_mViewInverse: identity, the camera at 0
    }
    return m.n;
}
// The material's emissive (COLOR0.rgb): Materials row 384's diffuse 150 x self illumination 100 / 25500
// (classic-materials.md), alpha 1.
constexpr float emissive[4] = {150.f * 100.f / 25500.f, 150.f * 100.f / 25500.f, 150.f * 100.f / 25500.f, 1.f};
// The flash's pass state (ADD ONE / ONE, Z test on, no Z write, no cull, alpha test off), the stand-in VS with its rows
// and emissive, g_EnableGlow (PS c4), the atlas on s0 and the light map on s2 (linear, wrap).
inline void bind_pass(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexDeclaration9* decl,
                      IDirect3DBaseTexture9* tex, IDirect3DBaseTexture9* light, const float rows[16], bool depth,
                      const float colour[4] = emissive, float glow = 0.f) {
    dev->SetVertexShader(vs);
    dev->SetVertexShaderConstantF(0, rows, 4);
    dev->SetVertexShaderConstantF(4, colour, 1);
    const float g[4] = {glow, 0.f, 0.f, 0.f};
    dev->SetPixelShaderConstantF(4, g, 1);
    dev->SetVertexDeclaration(decl);
    dev->SetTexture(0, tex);
    dev->SetTexture(2, light);
    for (DWORD s : {DWORD(0), DWORD(2)}) {
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    dev->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}
// The production device sequence (src/proxy/muzzle_reshade_route.h, the code MotionOutput runs) on this device.
inline auto device_ops(IDirect3DDevice9* dev) {
    return mz::calls([dev](const float* v) { return dev->SetPixelShaderConstantF(mz::constant_register, v, mz::constant_count); },
                     [dev](IDirect3DPixelShader9* p) { return dev->SetPixelShader(p); });
}
// One substituted draw (begin_muzzle_reshade / finish_muzzle_reshade): begin, the draw, end. Returns the draw's
// result; *restored the sequence's begin and end success.
inline HRESULT reshaded_draw(IDirect3DDevice9* dev, IDirect3DPixelShader9* ours, IDirect3DPixelShader9* game,
                             const float constants[16], const float app[16], UINT primitives, bool* restored) {
    auto ops = device_ops(dev);
    mz::Applied applied;
    const HRESULT hr = mz::begin(ops, ours, constants, app, applied);
    const HRESULT drawn = SUCCEEDED(hr) ? dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitives) : hr;
    const HRESULT back = mz::end(ops, applied, game, app);
    *restored = SUCCEEDED(hr) && SUCCEEDED(back);
    return drawn;
}
// A 1 x 1 MANAGED A8R8G8B8 texture of one RGBA texel.
inline IDirect3DTexture9* flat_texture(IDirect3DDevice9* dev, const unsigned char rgba[4]) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) {
        t->Release();
        return nullptr;
    }
    unsigned char* b = static_cast<unsigned char*>(lr.pBits);
    b[0] = rgba[2], b[1] = rgba[1], b[2] = rgba[0], b[3] = rgba[3];
    t->UnlockRect(0);
    return t;
}
// Image figures over a linear RGB crop (w x h, RGB floats), the background luminance `base` subtracted: the energy (sum),
// the peak, and the equivalent radii (sqrt(area / pi), px) of the pixels at or above 1.0 (scene white), 0.1 over it, and
// a tenth of the image's own peak (peak_tenth_r: the extent gate's figure, independent of the brightness).
struct Figures {
    double sum = 0.0;
    float peak = 0.f, white_r = 0.f, tenth_r = 0.f, peak_tenth_r = 0.f;
};
inline Figures figures(const float* rgb, unsigned w, unsigned h, float base) {
    Figures f;
    unsigned white = 0, tenth = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        const float l = 0.2126f * rgb[i * 3] + 0.7152f * rgb[i * 3 + 1] + 0.0722f * rgb[i * 3 + 2] - base;
        f.sum += l;
        f.peak = l > f.peak ? l : f.peak;
        white += l >= 1.f;
        tenth += l >= 0.1f;
    }
    f.white_r = std::sqrt(float(white) / 3.14159265f);
    f.tenth_r = std::sqrt(float(tenth) / 3.14159265f);
    unsigned lit = 0;
    for (unsigned i = 0; f.peak > 0.f && i < w * h; ++i)
        lit += 0.2126f * rgb[i * 3] + 0.7152f * rgb[i * 3 + 1] + 0.0722f * rgb[i * 3 + 2] - base >= 0.1f * f.peak;
    f.peak_tenth_r = std::sqrt(float(lit) / 3.14159265f);
    return f;
}
} // namespace muzzle_fx
