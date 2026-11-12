#pragma once
// The bullet re-shade's fixture parts (docs/reverse-engineering/fl/bullet-draw-batch.md section 6), shared by
// weapon_effects_fixture.cpp (checks and the substitution's cost) and weapon_effects_projectile_dump.cpp (the look
// frames). The fixture cannot load the game's effects.fb, so the bolt is drawn the way the game's INSTANCE_BULLETS pass
// draws it with stand-ins built here: a vs_1_1 (compiled) doing what the game's VS does (dp4 of the world position against
// c0..c3, the UV and the packed colour passed through) and a ps_1_1 (assembled) doing what the game's B1 PS does
// (`tex t0; mul r0.rgb, t0, v0.a; +mov r0.a, t0.a`), stream 0 stride 24 (POSITION FLOAT3, TEXCOORD0 FLOAT2, COLOR0
// D3DCOLOR = alpha << 24), ONE / INVSRCCOLOR, Z test on and Z write off. The re-shade program is the production
// embedded ps_2_0 bytecode, swapped in exactly as MotionOutput::prepare_bullet_reshade / finish_bullet_reshade do
// (c29..c31, SetPixelShader; SetPixelShader, c29..c31 back). Never launches the game; no game bytes: the stand-in mesh and
// texture are procedural (the IRE's topology and UV extents, its strip's luminance profile), and the dump loads the
// real mesh and texture only from untracked files run_weapon_effects.py --dump prepares from the local lab assets.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/proxy/bullet_reshade_core.h"
#include "../../src/proxy/bullet_reshade_route.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace reshade_fx {
constexpr DWORD reshade_program[] = {
#include "../../src/renderer/weapon_bullet_reshade_pixel_program_inc.h"
};
constexpr DWORD reshade_flag_program[] = {
#include "../../src/renderer/weapon_bullet_reshade_flag_pixel_program_inc.h"
};
// ps_1_x is assembled (D3DXCompileShader of d3dx9_37 refuses ps_1_x HLSL): the B1 body as disassembled (section 6).
constexpr char vanilla_ps_asm[] = "ps_1_1\ntex t0\nmul r0.rgb, t0, v0.a\n+mov r0.a, t0.a\n";
using AssembleFn = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, void*, DWORD, ID3DXBuffer**, ID3DXBuffer**);
inline bool assemble(HMODULE d3dx, const char* text, ID3DXBuffer** out) {
    const auto fn = d3dx ? reinterpret_cast<AssembleFn>(GetProcAddress(d3dx, "D3DXAssembleShader")) : nullptr;
    ID3DXBuffer* err = nullptr;
    const HRESULT hr = fn ? fn(text, UINT(std::strlen(text)), nullptr, nullptr, 0, out, &err) : E_FAIL;
    if (err) {
        std::printf("ASSEMBLE_ERR %.200s\n", static_cast<const char*>(err->GetBufferPointer()));
        err->Release();
    }
    return SUCCEEDED(hr) && *out;
}
constexpr char vs_source[] =
    "float4 m[4] : register(c0);\n"
    "struct O { float4 p : POSITION; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 uv : TEXCOORD0, float4 c : COLOR0) {\n"
    "  O o; float4 q = float4(p.xyz, 1);\n"
    "  o.p = float4(dot(q, m[0]), dot(q, m[1]), dot(q, m[2]), dot(q, m[3])); o.c = c; o.uv = uv; return o; }\n";
struct Vertex {
    float x, y, z, u, v;
    DWORD colour;
};
static_assert(sizeof(Vertex) == 24, "the game's bullet vertex stride");
inline const D3DVERTEXELEMENT9* declaration() {
    static const D3DVERTEXELEMENT9 d[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                          D3DDECL_END()};
    return d;
}
// A bolt in body units: x, y, z, u, v per vertex, 66 vertices (22 triangles).
struct Mesh {
    float p[66][5];
    unsigned n = 0;
};
// The IRE topology (section 6): eight fins, each a quad from the axis (v 0.3519) to the rim (radius 0.01337, v 0.2792)
// running tail (z -0.2473, u 0.0131) to head (z +0.2473, u 0.9767), and three crossed head quads (z 0.148..0.202,
// half-width 0.0044..0.0056) over the round glow sprite (u 0.7833..0.9831, v 0.7673..0.967).
inline Mesh standin_mesh() {
    Mesh m;
    const auto put = [&](float x, float y, float z, float u, float v) {
        float* q = m.p[m.n++];
        q[0] = x, q[1] = y, q[2] = z, q[3] = u, q[4] = v;
    };
    const float r = 0.01337f, h = 0.2473f, u0 = 0.0131f, u1 = 0.9767f, va = 0.3519f, vr = 0.2792f;
    for (unsigned k = 0; k < 8; ++k) {
        const float a = float(k) * 0.785398163f, x = r * std::cos(a), y = r * std::sin(a);
        put(0, 0, -h, u0, va), put(x, y, h, u1, vr), put(0, 0, h, u1, va);
        put(x, y, h, u1, vr), put(0, 0, -h, u0, va), put(x, y, -h, u0, vr);
    }
    const float hu0 = 0.7833f, hu1 = 0.9831f, hv0 = 0.7673f, hv1 = 0.967f, z0 = 0.1482f, z1 = 0.2019f, zm = 0.1796f;
    const float c = 0.0044f, e = 0.0056f;
    // two diagonal quads and one across the axis, each two triangles
    put(c, c, z0, hu0, hv1), put(-c, -c, z0, hu1, hv1), put(-c, -c, z1, hu1, hv0);
    put(-c, -c, z1, hu1, hv0), put(c, c, z1, hu0, hv0), put(c, c, z0, hu0, hv1);
    put(c, -c, z0, hu0, hv1), put(-c, c, z0, hu1, hv1), put(-c, c, z1, hu1, hv0);
    put(-c, c, z1, hu1, hv0), put(c, -c, z1, hu0, hv0), put(c, -c, z0, hu0, hv1);
    put(e, -e, zm, hu0, hv1), put(-e, e, zm, hu1, hv0), put(-e, -e, zm, hu1, hv1);
    put(-e, e, zm, hu1, hv0), put(e, -e, zm, hu0, hv1), put(e, e, zm, hu0, hv0);
    return m;
}
// The stand-in texture, 128 x 128 A8R8G8B8 (RGBA bytes): the body strip (rows v 0.2792..0.3519 rising towards the axis
// edge and the head to luminance 0.36, hue (1, 0.78, 0.18)) and the head's round glow (peak 0.38, hue (1, 0.75, 0.42)),
// alpha following the luminance; black elsewhere.
inline std::vector<unsigned char> standin_texture(unsigned* side) {
    const unsigned n = 128;
    *side = n;
    std::vector<unsigned char> t(std::size_t(n) * n * 4, 0);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            const float u = (float(x) + 0.5f) / float(n), v = (float(y) + 0.5f) / float(n);
            float l = 0.f, hue[3] = {1.f, 0.78f, 0.18f};
            if (v >= 0.2792f && v <= 0.3643f) {
                const float across = (v - 0.2792f) / (0.3643f - 0.2792f), along = u;
                l = 0.36f * across * across * (0.3f + 0.7f * along);
            } else if (u >= 0.7833f && u <= 0.9831f && v >= 0.7673f && v <= 0.967f) {
                const float du = (u - 0.8832f) / 0.1f, dv = (v - 0.8672f) / 0.1f, d = std::sqrt(du * du + dv * dv);
                l = d < 1.f ? 0.38f * (1.f - d) * (1.f - d) : 0.f;
                hue[1] = 0.75f, hue[2] = 0.42f;
            }
            const float lum_of_hue = 0.2126f * hue[0] + 0.7152f * hue[1] + 0.0722f * hue[2];
            unsigned char* p = &t[(std::size_t(y) * n + x) * 4];
            for (unsigned k = 0; k < 3; ++k) {
                const float c = l / lum_of_hue * hue[k];
                p[k] = (unsigned char)(c >= 1.f ? 255 : int(c * 255.f + 0.5f));
            }
            p[3] = (unsigned char)(int(l / 0.38f * 250.f + 0.5f));
        }
    return t;
}
// A MANAGED A8R8G8B8 texture with a box-filtered mip chain from RGBA bytes (side a power of two).
inline IDirect3DTexture9* make_texture(IDirect3DDevice9* dev, const unsigned char* rgba, unsigned side) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(side, side, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) return nullptr;
    std::vector<float> level(std::size_t(side) * side * 4);
    for (std::size_t i = 0; i < level.size(); ++i) level[i] = float(rgba[i]);
    unsigned s = side;
    for (DWORD lv = 0; lv < tex->GetLevelCount(); ++lv) {
        D3DLOCKED_RECT lr{};
        if (FAILED(tex->LockRect(lv, &lr, nullptr, 0))) {
            tex->Release();
            return nullptr;
        }
        for (unsigned y = 0; y < s; ++y) {
            unsigned char* row = static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch;
            for (unsigned x = 0; x < s; ++x) {
                const float* q = &level[(std::size_t(y) * s + x) * 4];
                const auto b = [](float v) { return (unsigned char)(v >= 255.f ? 255 : int(v + 0.5f)); };
                row[x * 4 + 0] = b(q[2]), row[x * 4 + 1] = b(q[1]), row[x * 4 + 2] = b(q[0]), row[x * 4 + 3] = b(q[3]);
            }
        }
        tex->UnlockRect(lv);
        if (s == 1) break;
        const unsigned h = s / 2;
        std::vector<float> next(std::size_t(h) * h * 4);
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < h; ++x)
                for (unsigned k = 0; k < 4; ++k)
                    next[(std::size_t(y) * h + x) * 4 + k] =
                        0.25f * (level[((std::size_t(2 * y) * s) + 2 * x) * 4 + k] + level[((std::size_t(2 * y) * s) + 2 * x + 1) * 4 + k] +
                                 level[((std::size_t(2 * y + 1) * s) + 2 * x) * 4 + k] +
                                 level[((std::size_t(2 * y + 1) * s) + 2 * x + 1) * 4 + k]);
        level.swap(next);
        s = h;
    }
    return tex;
}
// The bolt's world placement: body units to metres (an assumption: the IRE's drawn length 10 m; the note leaves the
// runtime node scales open), its centre at `centre` (camera space, metres), its axis yawed `yaw` radians out of the
// screen plane and its fins rolled `roll` radians. Writes n vertices (the game's world-space VB layout).
constexpr float metres_per_body_unit = 10.f / (2.f * 0.2473f);
inline unsigned place(const Mesh& m, const float centre[3], float yaw, float roll, BYTE alpha, Vertex* out) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
    for (unsigned i = 0; i < m.n; ++i) {
        const float s = metres_per_body_unit;
        const float x0 = m.p[i][0] * s, y0 = m.p[i][1] * s, z0 = m.p[i][2] * s;
        const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0; // roll about the axis (z)
        // axis z -> camera (cos yaw, 0, sin yaw); the fin x -> (-sin yaw, 0, cos yaw); y stays up
        out[i] = Vertex{centre[0] + z0 * cy - x1 * sy, centre[1] + y1, centre[2] + z0 * sy + x1 * cy, m.p[i][3], m.p[i][4],
                        DWORD(alpha) << 24};
    }
    return m.n;
}
// c0..c3 for the stand-in VS: rows of the clip transform (x' = m00 x, y' = m11 y, z' = m22 z + m32, w' = z).
inline void clip_rows(float m00, float m11, float out[16]) {
    const float n = 1.f, f = 1e5f;
    for (unsigned i = 0; i < 16; ++i) out[i] = 0.f;
    out[0] = m00;
    out[5] = m11;
    out[10] = f / (f - n);
    out[11] = -n * f / (f - n);
    out[14] = 1.f;
}
// The INSTANCE_BULLETS pass state (blend ONE / INVSRCCOLOR, Z test on, no Z write, no cull, alpha test off), the
// stand-in VS with its rows, the declaration, the texture on stage 0 with linear filtering.
inline void bind_pass(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexDeclaration9* decl,
                      IDirect3DBaseTexture9* tex, const float rows[16], bool depth) {
    dev->SetVertexShader(vs);
    dev->SetVertexShaderConstantF(0, rows, 4);
    dev->SetVertexDeclaration(decl);
    dev->SetTexture(0, tex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}
// The production device sequence (src/proxy/bullet_reshade_route.h, the code MotionOutput runs) on this device.
inline auto device_ops(IDirect3DDevice9* dev) {
    namespace rs = x3m::weapon_effects::reshade;
    return rs::calls([dev](DWORD v) { return dev->SetRenderState(D3DRS_DESTBLEND, v); },
                     [dev](const float* v) {
                         return dev->SetPixelShaderConstantF(rs::block_register, v, rs::block_count);
                     },
                     [dev](IDirect3DPixelShader9* p) { return dev->SetPixelShader(p); });
}
// One substituted draw on the plain path (the game's ONE / INVSRCCOLOR; motion_output_bullet_reshade_inc.h
// prepare_bullet_reshade / finish_bullet_reshade): begin, the draw, end. Returns the draw's result; *restored the
// sequence's begin and end success. app_block: the application's c29..c31 (12 floats); layout: the body's c29..c30
// (layout_constants; nullptr the identity).
inline HRESULT reshaded_draw(IDirect3DDevice9* dev, IDirect3DPixelShader9* ours, IDirect3DPixelShader9* game,
                             const float constants[4], const float app_block[12], UINT primitives, bool* restored,
                             const float* layout = nullptr) {
    namespace rs = x3m::weapon_effects::reshade;
    auto ops = device_ops(dev);
    rs::Plan plan;
    plan.program = ours;
    plan.constants = constants;
    plan.layout = layout;
    rs::Applied applied;
    const HRESULT hr = rs::begin(ops, plan, app_block, applied);
    const HRESULT drawn = SUCCEEDED(hr) ? dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitives) : hr;
    const HRESULT back = rs::end(ops, applied, game, app_block);
    *restored = SUCCEEDED(hr) && SUCCEEDED(back);
    return drawn;
}
// FNV-1a 32 of a program's instruction tokens (comments skipped): the stand-in's body against the game's.
inline std::uint32_t body_hash(const DWORD* code) {
    std::uint32_t h = 2166136261u;
    for (unsigned i = 1; code[i] != 0x0000ffffu && i < 4096;) {
        if ((code[i] & 0xffffu) == 0xfffeu) {
            i += 1 + (code[i] >> 16);
            continue;
        }
        for (unsigned k = 0; k < 4; ++k) h = (h ^ ((code[i] >> (8 * k)) & 0xffu)) * 16777619u;
        ++i;
    }
    return h;
}
// The lone path (bullet-draw-batch.md section 7): a bullet body the batch does not take is drawn through the material
// submit with its effects.fx material, the effects DEFAULT pair (VS d5e1c75351ed3f04, PS 8360f422de08b5bd in the SM2+
// directories), indexed from the body's own buffers, under the material's ONE / INVSRCCOLOR. Stand-ins of that pair's
// interface (verification/results/projectile-reshade/lone_inputs_out.txt): a vs_2_0 with g_mWorldViewProjection c0..c3,
// g_TexMatrix c10..c11 (u' = (u, v, 1) . c10.xyz, v' = (u, v, 1) . c11.xyz) and g_AlphaValue c12.x into oD0.x (oD0.yz
// 0; the game leaves oD0.w unwritten, the stand-in writes 0 so a program reading .a would draw black), and a ps_2_0
// sampling s0, applying the affine colour matrix c0..c2 to (rgb, 1), times v0.x, alpha the texel's. Stream 0: POSITION
// FLOAT3, TEXCOORD0 FLOAT2 (stride 20; the body vertex carries no colour the pair reads).
constexpr DWORD lone_program[] = {
#include "../../src/renderer/weapon_bullet_reshade_lone_pixel_program_inc.h"
};
constexpr DWORD lone_flag_program[] = {
#include "../../src/renderer/weapon_bullet_reshade_lone_flag_pixel_program_inc.h"
};
constexpr char lone_vs_source[] =
    "float4 m[4] : register(c0);\n"
    "float4 tex_matrix[2] : register(c10);\n"
    "float4 alpha_value : register(c12);\n"
    "struct O { float4 p : POSITION; float4 c : COLOR0; float2 uv : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 uv : TEXCOORD0) {\n"
    "  O o; float4 q = float4(p.xyz, 1); float3 t = float3(uv, 1);\n"
    "  o.p = float4(dot(q, m[0]), dot(q, m[1]), dot(q, m[2]), dot(q, m[3]));\n"
    "  o.uv = float2(dot(t, tex_matrix[0].xyz), dot(t, tex_matrix[1].xyz));\n"
    "  o.c = float4(alpha_value.x, 0, 0, 0); return o; }\n";
constexpr char lone_ps_source[] =
    "sampler2D s : register(s0);\n"
    "float4 colour_matrix[3] : register(c0);\n"
    "float4 main(float4 c : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float4 t = tex2D(s, uv); float4 h = float4(t.rgb, 1);\n"
    "  return float4(float3(dot(h, colour_matrix[0]), dot(h, colour_matrix[1]), dot(h, colour_matrix[2])) * c.x, t.a); }\n";
struct LoneVertex {
    float x, y, z, u, v;
};
static_assert(sizeof(LoneVertex) == 20, "the stand-in body vertex");
inline const D3DVERTEXELEMENT9* lone_declaration() {
    static const D3DVERTEXELEMENT9 d[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    return d;
}
// The material's constants the stand-in pair reads: the identity g_TexMatrix (no texture animation on a bullet body),
// g_AlphaValue = fade, the identity colour matrix.
inline void bind_lone_constants(IDirect3DDevice9* dev, float fade) {
    const float tex_matrix[8] = {1, 0, 0, 0, 0, 1, 0, 0}, alpha[4] = {fade, 0, 0, 0};
    const float colour_matrix[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    dev->SetVertexShaderConstantF(10, tex_matrix, 2);
    dev->SetVertexShaderConstantF(12, alpha, 1);
    dev->SetPixelShaderConstantF(0, colour_matrix, 3);
}
// The lone draw's pass: bind_pass's states (the material's ONE / INVSRCCOLOR, Z write off, no cull) with the lone
// stand-in VS, its declaration and constants.
inline void bind_lone_pass(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, IDirect3DVertexDeclaration9* decl,
                           IDirect3DBaseTexture9* tex, const float rows[16], bool depth, float fade) {
    bind_pass(dev, vs, decl, tex, rows, depth);
    bind_lone_constants(dev, fade);
}
// One lone draw re-shaded on the plain path (motion_output_bullet_reshade_inc.h prepare_lone_bullet: the game's blend
// kept): begin, the indexed draw, end, through the production sequence.
inline HRESULT lone_reshaded_draw(IDirect3DDevice9* dev, IDirect3DPixelShader9* ours, IDirect3DPixelShader9* game,
                                  const float constants[4], const float app_block[12], UINT vertices, UINT primitives,
                                  bool* restored, const float* layout = nullptr) {
    namespace rs = x3m::weapon_effects::reshade;
    auto ops = device_ops(dev);
    rs::Plan plan;
    plan.program = ours;
    plan.constants = constants;
    plan.layout = layout;
    rs::Applied applied;
    const HRESULT hr = rs::begin(ops, plan, app_block, applied);
    const HRESULT drawn = SUCCEEDED(hr) ? dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, 0, primitives) : hr;
    const HRESULT back = rs::end(ops, applied, game, app_block);
    *restored = SUCCEEDED(hr) && SUCCEEDED(back);
    return drawn;
}
} // namespace reshade_fx
