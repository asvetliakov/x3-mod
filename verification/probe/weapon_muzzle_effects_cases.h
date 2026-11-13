#pragma once
// The muzzle flash re-shade's effects pair (body 12009, PRG; docs/architecture/muzzle-flash-look.md "The effects-pair
// body"; MUZZLE_EFFECTS rows, muzzle_effects_* checks), included by weapon_effects_fixture.cpp after its helpers (dev,
// report, compile, compile_shader, half_to_float, ticks_us, Com, device_guard). The game's effects DEFAULT pair is its
// own: run_weapon_effects.py extracts the FL 3_0 effects.fb DEFAULT VS d5e1c75351ed3f04 and PS 8360f422de08b5bd into the
// untracked working directory (effects_vs.bin, effects_ps.bin; each FNV-1a 64 checked here; without them the cases
// fail). Our programs are the production embedded vs_2_0 and ps_2_0 bytecode, bound through the production sequence
// (muzzle_reshade_route.h begin_pair / end_pair) and recognised by the production match (muzzle_reshade_core.h
// effects_match), as MotionOutput::begin_muzzle_effects / finish_muzzle_effects / muzzle_effects_match do.
// (a) programs; (b) our VS's oPos, oT0 and oD0 against the game VS's on the same draw, fog off and on; (c) our ps_2_0
// against effects_model at seven points, and the game PS against its law (texel x fade); (d) the sequence binds and
// restores (readback) and a failed step rolls back; (e) recognition and one claimant on the effects pair (lone effects,
// muzzle, hull hit); (f) the stand-in flash (body 12009's three cones, a red streak with its cell's profile; within 10 %
// of the real body's figures, verification/results/muzzle-reshade/standin_12009.py) at three poses, 30 m, 1920 x 1080,
// under the game's ONE / INVSRCCOLOR: the muzzle gates (energy <= mz_gain x vanilla's, lit (peak >= mz_gain / 4, axis-on
// mz_gain / 8: the fan's own axis-on dimness), the extent within
// mz_size x vanilla's + 1 px and within vanilla's at a tenth of the peak); (g) the cost. Never launches the game; no game
// bytes are tracked.
#include "weapon_hull_reshade_fixture.h" // hull_fx::load, hash; reshade_fx; muzzle_reshade_route.h
#include "weapon_muzzle_reshade_fixture.h"

namespace muzzle_effects_fx {
namespace mz = x3m::weapon_effects::muzzle_reshade;
constexpr DWORD vertex_program[] = {
#include "../../src/renderer/weapon_muzzle_reshade_effects_vertex_program_inc.h"
};
constexpr DWORD pixel_program[] = {
#include "../../src/renderer/weapon_muzzle_reshade_effects_pixel_program_inc.h"
};
constexpr std::uint64_t game_pixel_shader = 0x8360f422de08b5bdull;
// The effects body vertex the declaration gives the VS (reshade_fx::lone_declaration: POSITION FLOAT3, TEXCOORD0
// FLOAT2), in body units.
struct Vertex {
    float x, y, z, u, v;
};
static_assert(sizeof(Vertex) == 20, "position, UV");
// The cell 12009's quads map (muzzle_bodies_out.txt: fx_bullets2_diff u 0.406..0.570, v 0.241..0.447; v 0.447 the gun
// edge).
constexpr float cell_u0 = 0.406f, cell_u1 = 0.570f, cell_v0 = 0.241f, cell_v1 = 0.447f;
// Body 12009's stand-in: 50 quads in its three cones (standin_12009.py CONES, the same numbers): tilt from +z, length,
// azimuths, the gun edge's middle in (radial, tangential, z) and the across vectors in the cone's frame (d the axis,
// a = (-sin az, cos az, 0), b = d x a). Into muzzle_fx::Mesh (x, y, z, u, v per vertex; 300 = 50 x 6).
inline muzzle_fx::Mesh standin_mesh_12009() {
    muzzle_fx::Mesh m;
    struct Cone {
        float tilt, length;
        unsigned azimuths;
        float az0, az_step;
        unsigned mids;
        float mid[2][3];
        unsigned acrosses;
        float across[4][3];
    };
    const Cone cones[3] = {
        {0.f, 98800.f, 1, 0.f, 0.f, 2, {{250.f, 1800.f, 1200.f}, {250.f, -4200.f, 1200.f}}, 4,
         {{0.f, 0.f, -35940.f}, {0.f, -25415.f, -25415.f}, {0.f, 25415.f, -25415.f}, {0.f, -35940.f, 0.f}}},
        {41.6f, 54390.f, 6, 30.f, 60.f, 1, {{-2725.f, -250.f, 6784.f}, {}}, 3,
         {{0.f, -35940.f, 0.f}, {-7217.f, -25414.f, 22410.f}, {7217.f, -25414.f, -22410.f}, {}}},
        {77.9f, 40640.f, 8, 0.f, 45.f, 1, {{-4663.f, -250.f, 8852.f}, {}}, 3,
         {{0.f, -35940.f, 0.f}, {-9015.f, -25414.f, 44988.f}, {9015.f, -25414.f, -44988.f}, {}}}};
    const auto put = [&](const float q[3], float u, float v) {
        float* o = m.p[m.n++];
        o[0] = q[0], o[1] = q[1], o[2] = q[2], o[3] = u, o[4] = v;
    };
    for (const Cone& c : cones) {
        const float t = c.tilt * 0.0174532925f;
        for (unsigned k = 0; k < c.azimuths; ++k) {
            const float p = (c.az0 + c.az_step * float(k)) * 0.0174532925f;
            const float d[3] = {std::sin(t) * std::cos(p), std::sin(t) * std::sin(p), std::cos(t)};
            const float a[3] = {-std::sin(p), std::cos(p), 0.f};
            const float b[3] = {d[1] * a[2] - d[2] * a[1], d[2] * a[0] - d[0] * a[2], d[0] * a[1] - d[1] * a[0]};
            const float r[3] = {std::cos(p), std::sin(p), 0.f};
            for (unsigned i = 0; i < c.mids; ++i)
                for (unsigned j = 0; j < c.acrosses; ++j) {
                    float g0[3], g1[3], f0[3], f1[3];
                    for (unsigned e = 0; e < 3; ++e) {
                        const float gun = c.mid[i][0] * r[e] + c.mid[i][1] * a[e] + (e == 2 ? c.mid[i][2] : 0.f);
                        const float w = c.across[j][0] * d[e] + c.across[j][1] * a[e] + c.across[j][2] * b[e];
                        g0[e] = gun - 0.5f * w, g1[e] = gun + 0.5f * w;
                        f0[e] = g0[e] + c.length * d[e], f1[e] = g1[e] + c.length * d[e];
                    }
                    put(f0, cell_u0, cell_v0), put(f1, cell_u1, cell_v0), put(g0, cell_u0, cell_v1);
                    put(g1, cell_u1, cell_v1), put(g0, cell_u0, cell_v1), put(f1, cell_u1, cell_v0);
                }
        }
    }
    return m;
}
// Its texture stand-in (standin_12009.py texture, the same law): 512 x 512 RGBA bytes, inside the cell a red streak
// with the real cell's profiles (along v: 0.1 at the far edge rising to 1 at 70 %, flat to 86 %, falling to 0 at 99 %;
// across u: flat over 0.4..0.6 of the cell, a Gaussian edge of 0.07 beyond), amplitude 0.15, a deterministic texel noise
// (x 0.3 + 2.1 n^2: the real cell's p99 0.268 and band spread), the cell's hue (1, 0.128, 0.137); black outside; alpha 255.
// Cell mean luminance 0.0299 (real 0.0292),
// glow-weighted luminance 0.0273 (real 0.0264), 90 % energy radius 50333 (real 48104), ray sum 10.37 (real 9.88).
inline std::vector<unsigned char> standin_texture_12009(unsigned* side) {
    const unsigned n = 512;
    *side = n;
    std::vector<unsigned char> t(std::size_t(n) * n * 4, 0);
    std::uint32_t seed = 12009u;
    const float hue[3] = {1.f, 0.128f, 0.137f}, lum_hue = 0.2126f + 0.7152f * 0.128f + 0.0722f * 0.137f;
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = float(seed >> 8) / 16777216.f;
            const float u = (float(x) + 0.5f) / float(n), v = (float(y) + 0.5f) / float(n);
            unsigned char* p = &t[(std::size_t(y) * n + x) * 4];
            p[3] = 255;
            if (u < cell_u0 || u > cell_u1 || v < cell_v0 || v > cell_v1) continue;
            const float s = (v - cell_v0) / (cell_v1 - cell_v0), q = (u - cell_u0) / (cell_u1 - cell_u0);
            const float along = s < 0.86f ? 0.1f + 0.9f * (s / 0.7f < 1.f ? s / 0.7f : 1.f)
                                          : (1.f - (s - 0.86f) / 0.13f > 0.f ? 1.f - (s - 0.86f) / 0.13f : 0.f);
            const float edge = std::fabs(q - 0.5f) - 0.1f > 0.f ? (std::fabs(q - 0.5f) - 0.1f) / 0.07f : 0.f;
            const float l = 0.15f * along * std::exp(-edge * edge) * (0.3f + 2.1f * noise * noise);
            for (unsigned k = 0; k < 3; ++k) {
                const float c = l / lum_hue * hue[k];
                p[k] = (unsigned char)(c >= 1.f ? 255 : int(c * 255.f + 0.5f));
            }
        }
    return t;
}
// The game's material constants for the effects DEFAULT pair: g_mWorldViewProjection c0..c3, g_mWorld c4..c6,
// g_mViewInverse c7..c9, g_TexMatrix c10..c11, g_AlphaValue c12.x, g_FogClip c13 (VS); g_EnableFog b0; the colour
// matrix c0..c2 (PS, the identity: the preshader's output at Brightness / contrast / saturation 1, hue 0).
struct Material {
    float wvp[16], world[12], view_inverse[12], tex[8], alpha, fog_clip[2];
    bool fog;
};
inline Material identity_material() {
    Material m{};
    for (unsigned i = 0; i < 4; ++i) m.wvp[i * 5] = 1.f;
    for (unsigned i = 0; i < 3; ++i) m.world[i * 5] = m.view_inverse[i * 5] = 1.f;
    m.tex[0] = m.tex[5] = 1.f;
    m.alpha = 1.f;
    m.fog_clip[0] = 1.f;
    return m;
}
inline void bind_constants(IDirect3DDevice9* dev, const Material& m) {
    float c[14 * 4]{};
    for (unsigned i = 0; i < 16; ++i) c[i] = m.wvp[i];
    for (unsigned i = 0; i < 12; ++i) c[16 + i] = m.world[i], c[28 + i] = m.view_inverse[i];
    for (unsigned i = 0; i < 8; ++i) c[40 + i] = m.tex[i];
    c[48] = m.alpha;
    c[52] = m.fog_clip[0], c[53] = m.fog_clip[1];
    dev->SetVertexShaderConstantF(0, c, 14);
    const BOOL fog = m.fog ? TRUE : FALSE;
    dev->SetVertexShaderConstantB(0, &fog, 1);
    const float colour_matrix[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    dev->SetPixelShaderConstantF(0, colour_matrix, 3);
}
// The flash's pass state as the material sets it (g_CullMode 1 none, ADD ONE / dst, Z test as asked, Z write off, alpha
// test off), the texture on s0 (linear, wrap).
inline void bind_state(IDirect3DDevice9* dev, IDirect3DBaseTexture9* tex, bool blend, DWORD dst) {
    dev->SetTexture(0, tex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, dst);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}
// The flash placed as muzzle_fx::place places it (its box centre at `centre`, scale, yaw, roll; the camera at 0): the
// world rows (g_mWorld, the gun in .w), the identity view inverse, and g_mWorldViewProjection = clip x world.
inline void place(const muzzle_fx::Mesh& mesh, const float centre[3], float scale, float yaw, float roll,
                  const float clip[16], Material& m) {
    static muzzle_fx::Vertex scratch[muzzle_fx::max_vertices];
    float rows[36];
    muzzle_fx::place(mesh, centre, scale, yaw, roll, scratch, rows);
    for (unsigned i = 0; i < 12; ++i) m.world[i] = rows[i], m.view_inverse[i] = rows[24 + i];
    const float w4[16] = {rows[0], rows[1], rows[2], rows[3], rows[4], rows[5], rows[6], rows[7],
                          rows[8], rows[9], rows[10], rows[11], 0.f, 0.f, 0.f, 1.f};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < 4; ++j) {
            float s = 0.f;
            for (unsigned k = 0; k < 4; ++k) s += clip[i * 4 + k] * w4[k * 4 + j];
            m.wvp[i * 4 + j] = s;
        }
}
// The effects VS rows c4..c9 as MotionOutput shadows them (effects_draw_constants' input).
inline void effects_rows(const Material& m, float rows[24]) {
    for (unsigned i = 0; i < 12; ++i) rows[i] = m.world[i], rows[12 + i] = m.view_inverse[i];
}
// The production sequence on this device.
inline auto device_ops(IDirect3DDevice9* dev) {
    return mz::pair_calls(
        [dev](const float* v) { return dev->SetPixelShaderConstantF(mz::effects_register, v, mz::effects_count); },
        [dev](IDirect3DVertexShader9* v) { return dev->SetVertexShader(v); },
        [dev](IDirect3DPixelShader9* p) { return dev->SetPixelShader(p); });
}
} // namespace muzzle_effects_fx

void muzzle_effects_cases() {
    namespace ef = muzzle_effects_fx;
    namespace mf = muzzle_fx;
    namespace rf = reshade_fx;
    namespace mz = x3m::weapon_effects::muzzle_reshade;
    namespace hh = x3m::weapon_effects::hull_reshade;
    namespace pf = x3m::weapon_effects::projectile_family;
    // (a) the game's pair (extracted, hash-checked), ours (production), the declaration
    std::vector<DWORD> game_vs_code, game_ps_code;
    const bool loaded = hull_fx::load("effects_vs.bin", game_vs_code) && hull_fx::load("effects_ps.bin", game_ps_code);
    const bool hashes = loaded && hull_fx::hash(game_vs_code) == mz::effects_vertex_shader &&
                        hull_fx::hash(game_ps_code) == ef::game_pixel_shader;
    Com<IDirect3DVertexShader9> game_vs, our_vs;
    Com<IDirect3DPixelShader9> game_ps, our_ps, probe_ps;
    Com<IDirect3DVertexDeclaration9> decl;
    LPD3DXBUFFER probe_hold = nullptr;
    const DWORD* probe_code = nullptr;
    // (b)'s probe: the VS outputs the pixel program sees (COLOR0.x, TEXCOORD0.xy), written as they arrive
    static const char probe_source[] = "float4 main(float4 c : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {\n"
                                       "  return float4(c.x, uv.x, uv.y, 1.0); }\n";
    const bool created = hashes && compile_shader && compile(probe_source, "ps_2_0", &probe_code, &probe_hold) &&
                         SUCCEEDED(dev->CreateVertexShader(game_vs_code.data(), &game_vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(game_ps_code.data(), &game_ps.p)) &&
                         SUCCEEDED(dev->CreateVertexShader(ef::vertex_program, &our_vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ef::pixel_program, &our_ps.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(probe_code, &probe_ps.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::lone_declaration(), &decl.p));
    if (probe_hold) probe_hold->Release();
    const unsigned body = mz::body_index(12009u);
    std::printf("MUZZLE_EFFECTS case=programs loaded=%u game_hashes=%u created=%u vs_words=%u ps_words=%u "
                "vs_major=%u ps_major=%u body_row=%u pair=%u dest_blend=%u\n",
                unsigned(loaded), unsigned(hashes), unsigned(created),
                unsigned(sizeof ef::vertex_program / sizeof ef::vertex_program[0]),
                unsigned(sizeof ef::pixel_program / sizeof ef::pixel_program[0]), unsigned((ef::vertex_program[0] >> 8) & 0xffu),
                unsigned((ef::pixel_program[0] >> 8) & 0xffu), body,
                body < mz::body_count ? unsigned(mz::bodies[body].pair) : 255u,
                body < mz::body_count ? unsigned(mz::bodies[body].dest_blend) : 255u);
    report("muzzle_effects_programs_create",
           created && body < mz::body_count && mz::bodies[body].pair == mz::pair_effects &&
               mz::bodies[body].dest_blend == D3DBLEND_INVSRCCOLOR,
           "");
    if (!created || body >= mz::body_count) return;
    const mz::Body& b12009 = mz::bodies[body];
    const auto tune = pf::baked;
    unsigned side = 0;
    const std::vector<unsigned char> texels = ef::standin_texture_12009(&side);
    Com<IDirect3DTexture9> tex;
    tex.p = rf::make_texture(dev, texels.data(), side);
    const mf::Mesh mesh = ef::standin_mesh_12009();
    Com<IDirect3DVertexBuffer9> vb;
    bool ok = tex.p && mesh.n == 300 &&
              SUCCEEDED(dev->CreateVertexBuffer(mf::max_vertices * sizeof(ef::Vertex), D3DUSAGE_WRITEONLY, 0,
                                                D3DPOOL_DEFAULT, &vb.p, nullptr));
    if (ok) {
        void* p = nullptr;
        ok = SUCCEEDED(vb.p->Lock(0, 0, &p, 0));
        if (ok) {
            auto* v = static_cast<ef::Vertex*>(p);
            for (unsigned i = 0; i < mesh.n; ++i)
                v[i] = ef::Vertex{mesh.p[i][0], mesh.p[i][1], mesh.p[i][2], mesh.p[i][3], mesh.p[i][4]};
            vb.p->Unlock();
        }
    }
    report("muzzle_effects_targets", ok, "");
    if (!ok) return;
    const UINT prims = mesh.n / 3;
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    const float size = mz::clamp_key(tune.mz_size, pf::key_mz_size), gain = mz::clamp_key(tune.mz_gain, pf::key_mz_gain);
    // (b) our VS against the game's: the stand-in at the tilt pose with a non-identity g_TexMatrix and g_AlphaValue,
    // fog off and on (g_FogClip: the fade 1 at 20 m to 0 at 40 m: 0.5 at the flash), blend off, Z off, into an FP32
    // target cleared to 0 (the probe writes alpha 1): per pixel the coverage and COLOR0.x, TEXCOORD0.xy
    {
        Com<IDirect3DTexture9> rt;
        Com<IDirect3DSurface9> rt_s, sys;
        constexpr UINT w = 480, h = 270;
        bool fine = SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT,
                                                 &rt.p, nullptr)) &&
                    SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
                    SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &sys.p,
                                                               nullptr));
        float clip[16];
        rf::clip_rows(1.6f, 1.6f * 16.f / 9.f, clip);
        ef::Material m = ef::identity_material();
        const float centre[3] = {0.f, 0.f, 30.f};
        ef::place(mesh, centre, mf::peak_scale, 0.4363f, 0.3491f, clip, m);
        m.tex[0] = 1.1f, m.tex[1] = 0.05f, m.tex[2] = 0.02f, m.tex[4] = -0.03f, m.tex[5] = 0.9f, m.tex[6] = 0.04f;
        m.alpha = 0.8f;
        m.fog_clip[0] = 2.f, m.fog_clip[1] = 0.05f;
        std::vector<float> images[2];
        unsigned covered[2][2] = {}, coverage_mismatch[2] = {};
        float worst[2] = {0.f, 0.f}, fade_lo[2] = {1e9f, 1e9f}, fade_hi[2] = {-1e9f, -1e9f};
        if (fine) dev->SetRenderTarget(0, rt_s.p);
        for (unsigned fog = 0; fog < 2 && fine; ++fog) {
            m.fog = fog != 0;
            for (unsigned which = 0; which < 2 && fine; ++which) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(0.f, 0.f, 0.f, 0.f), 1.f, 0);
                dev->BeginScene();
                ef::bind_constants(dev, m);
                ef::bind_state(dev, tex.p, false, D3DBLEND_ONE);
                dev->SetVertexDeclaration(decl.p);
                dev->SetStreamSource(0, vb.p, 0, sizeof(ef::Vertex));
                dev->SetVertexShader(which ? our_vs.p : game_vs.p);
                dev->SetPixelShader(probe_ps.p);
                fine = SUCCEEDED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims));
                dev->EndScene();
                D3DLOCKED_RECT lr{};
                fine = fine && SUCCEEDED(dev->GetRenderTargetData(rt_s.p, sys.p)) &&
                       SUCCEEDED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                if (!fine) break;
                images[which].assign(std::size_t(w) * h * 4, 0.f);
                for (UINT y = 0; y < h; ++y)
                    std::memcpy(&images[which][std::size_t(y) * w * 4], static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch,
                                w * 16);
                sys.p->UnlockRect();
            }
            for (std::size_t i = 0; fine && i < std::size_t(w) * h; ++i) {
                const float* a = &images[0][i * 4];
                const float* c = &images[1][i * 4];
                const bool in_a = a[3] > 0.5f, in_c = c[3] > 0.5f;
                covered[fog][0] += in_a, covered[fog][1] += in_c;
                coverage_mismatch[fog] += in_a != in_c;
                if (!in_a || !in_c) continue;
                for (unsigned k = 0; k < 3; ++k) worst[fog] = std::max(worst[fog], std::fabs(a[k] - c[k]));
                fade_lo[fog] = std::min(fade_lo[fog], a[0]), fade_hi[fog] = std::max(fade_hi[fog], a[0]);
            }
            std::printf("MUZZLE_EFFECTS case=vs_parity fog=%u covered_game=%u covered_ours=%u coverage_mismatch=%u "
                        "worst_abs=%.3g fade_min=%.4f fade_max=%.4f\n",
                        fog, covered[fog][0], covered[fog][1], coverage_mismatch[fog], double(worst[fog]),
                        double(fade_lo[fog]), double(fade_hi[fog]));
        }
        report("muzzle_effects_vs_matches_game",
               fine && covered[0][0] > 1000 && covered[1][0] > 1000 && coverage_mismatch[0] == 0 &&
                   coverage_mismatch[1] == 0 && worst[0] <= 1e-6f && worst[1] <= 1e-6f && fade_lo[1] < fade_hi[1] &&
                   fade_hi[1] < 0.8f,
               "");
    }
    // (c) our ps_2_0 against effects_model, and the game PS against texel x fade: a 16 x 16 target, blend off, the quad
    // drawn through the given VS with g_mWorldViewProjection mapping the model point (mx, my, mz) to the centre pixel
    // (8, 8: NDC 0 in D3D9), so the interpolated TEXCOORD1 there is that point. The block is 12009's with gain / N and
    // core / N set to exercise both terms (the baked mz_core is 0).
    {
        float block[mz::effects_floats];
        mz::effects_constants(b12009, tune, block);
        block[4] = 0.05f, block[5] = 0.3f;
        const float radius = 1.f / block[3];
        Com<IDirect3DTexture9> rt;
        Com<IDirect3DSurface9> rt_s, sys;
        bool fine = SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT,
                                                 &rt.p, nullptr)) &&
                    SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
                    SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p,
                                                               nullptr));
        struct Case {
            float off; // distance from the gun over the radius, along (0.6, 0, 0.8)
            unsigned char t[4];
            float fade;
        };
        const Case cases[7] = {
            {0.f, {200, 26, 27, 255}, 1.f},     // at the gun: body + core, saturated
            {0.15f, {40, 5, 5, 255}, 1.f},      // the core's range
            {0.5f, {6, 1, 1, 255}, 0.5f},       // the glow, a dark texel, half fade
            {0.8f, {255, 33, 35, 200}, 1.f},    // the glow's rim, a bright texel
            {1.2f, {200, 26, 27, 255}, 1.f},    // past the radius: black, the texel's alpha
            {0.3f, {120, 15, 16, 128}, 0.25f},  // a quarter fade
            {0.05f, {0, 0, 0, 255}, 1.f},       // a black texel at the gun: the core alone
        };
        float worst = 0.f, worst_vanilla = 0.f;
        if (fine) dev->SetRenderTarget(0, rt_s.p);
        for (unsigned c = 0; fine && c < 7; ++c) {
            const Case& k = cases[c];
            Com<IDirect3DTexture9> flat;
            flat.p = mf::flat_texture(dev, k.t);
            fine = flat.p != nullptr;
            if (!fine) break;
            const float model[3] = {0.6f * k.off * radius, 0.f, 0.8f * k.off * radius};
            const float s = 0.01f * radius;
            ef::Material m = ef::identity_material();
            for (float& v : m.wvp) v = 0.f;
            m.wvp[0] = 1.f / s, m.wvp[3] = -model[0] / s;
            m.wvp[5] = 1.f / s, m.wvp[7] = -model[1] / s;
            m.wvp[11] = 0.5f, m.wvp[15] = 1.f;
            m.alpha = k.fade;
            const ef::Vertex quad[6] = {{model[0] - s, model[1] - s, model[2], 0.2f, 0.3f},
                                        {model[0] - s, model[1] + s, model[2], 0.2f, 0.3f},
                                        {model[0] + s, model[1] + s, model[2], 0.2f, 0.3f},
                                        {model[0] - s, model[1] - s, model[2], 0.2f, 0.3f},
                                        {model[0] + s, model[1] + s, model[2], 0.2f, 0.3f},
                                        {model[0] + s, model[1] - s, model[2], 0.2f, 0.3f}};
            for (unsigned which = 0; which < 2 && fine; ++which) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                dev->BeginScene();
                ef::bind_constants(dev, m);
                ef::bind_state(dev, flat.p, false, D3DBLEND_ONE);
                dev->SetVertexDeclaration(decl.p);
                dev->SetVertexShader(which ? our_vs.p : game_vs.p);
                dev->SetPixelShader(which ? our_ps.p : game_ps.p);
                if (which) dev->SetPixelShaderConstantF(mz::effects_register, block, mz::effects_count);
                fine = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(ef::Vertex)));
                dev->EndScene();
                D3DLOCKED_RECT out{};
                fine = fine && SUCCEEDED(dev->GetRenderTargetData(rt_s.p, sys.p)) &&
                       SUCCEEDED(sys.p->LockRect(&out, nullptr, D3DLOCK_READONLY));
                if (!fine) break;
                const auto* px =
                    reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(out.pBits) + 8 * out.Pitch) + 8 * 4;
                const float got[4] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]), half_to_float(px[3])};
                sys.p->UnlockRect();
                const float t[4] = {k.t[0] / 255.f, k.t[1] / 255.f, k.t[2] / 255.f, k.t[3] / 255.f};
                float want[4];
                if (which)
                    mz::effects_model(block, t, model, k.fade, want);
                else {
                    for (unsigned i = 0; i < 3; ++i) want[i] = t[i] * k.fade;
                    want[3] = t[3];
                }
                for (unsigned i = 0; i < 4; ++i) {
                    const float err = std::fabs(got[i] - want[i]) / (0.004f + 0.004f * std::fabs(want[i]));
                    float& wv = which ? worst : worst_vanilla;
                    wv = std::max(wv, err);
                }
                if (which)
                    std::printf("MUZZLE_EFFECTS case=texel index=%u off=%.2f got=%.4f,%.4f,%.4f,%.4f "
                                "model=%.4f,%.4f,%.4f,%.4f\n",
                                c, double(k.off), double(got[0]), double(got[1]), double(got[2]), double(got[3]),
                                double(want[0]), double(want[1]), double(want[2]), double(want[3]));
            }
        }
        std::printf("MUZZLE_EFFECTS case=model radius_model=%.1f worst_rel=%.3f vanilla_worst_rel=%.3f\n", double(radius),
                    double(worst), double(worst_vanilla));
        report("muzzle_effects_program_matches_model", fine && worst <= 1.f, "");
        report("muzzle_effects_game_ps_is_texel_times_fade", fine && worst_vanilla <= 1.f, "");
    }
    // (d) a failed step rolls back, latest first, and the end tries every step (CPU, recorded calls)
    {
        struct Log {
            char step[8];
            unsigned n = 0;
            int fail_at = -1, fail_undo = -1; // the call index that fails
            HRESULT call(char s) {
                const int i = int(n);
                if (n < 8) step[n++] = s;
                return i == fail_at || i == fail_undo ? E_FAIL : S_OK;
            }
        };
        IDirect3DVertexShader9* const vs_a = reinterpret_cast<IDirect3DVertexShader9*>(std::uintptr_t(16));
        IDirect3DPixelShader9* const ps_a = reinterpret_cast<IDirect3DPixelShader9*>(std::uintptr_t(32));
        const float ours_c[12] = {1.f}, app_c[12] = {2.f};
        const auto run = [&](int fail_at, int fail_undo, mz::PairApplied& a, Log& log) {
            log = Log{};
            log.fail_at = fail_at, log.fail_undo = fail_undo;
            auto ops = mz::pair_calls([&](const float* v) { return log.call(v == ours_c ? 'C' : 'c'); },
                                      [&](IDirect3DVertexShader9* v) { return log.call(v == vs_a ? 'V' : 'v'); },
                                      [&](IDirect3DPixelShader9* p) { return log.call(p == ps_a ? 'P' : 'p'); });
            return mz::begin_pair(ops, vs_a, ps_a, ours_c, nullptr, app_c, a);
        };
        mz::PairApplied a;
        Log log;
        bool fine = SUCCEEDED(run(-1, -1, a, log)) && log.n == 3 && !std::memcmp(log.step, "CVP", 3) && a.constants &&
                    a.vertex && a.program;
        fine = fine && FAILED(run(2, -1, a, log)) && log.n == 5 && !std::memcmp(log.step, "CVPvc", 5) && !a.constants &&
               !a.vertex && !a.program && !a.rollback_failed;
        fine = fine && FAILED(run(1, -1, a, log)) && log.n == 3 && !std::memcmp(log.step, "CVc", 3) && !a.rollback_failed;
        fine = fine && FAILED(run(2, 3, a, log)) && log.n == 5 && a.rollback_failed;
        {
            Log end_log;
            end_log.fail_at = 0;
            auto ops = mz::pair_calls([&](const float*) { return end_log.call('c'); },
                                      [&](IDirect3DVertexShader9*) { return end_log.call('v'); },
                                      [&](IDirect3DPixelShader9*) { return end_log.call('p'); });
            mz::PairApplied all;
            all.constants = all.vertex = all.program = true;
            fine = fine && FAILED(mz::end_pair(ops, all, nullptr, nullptr, app_c)) && end_log.n == 3 &&
                   !std::memcmp(end_log.step, "pvc", 3) && !all.constants && !all.vertex && !all.program;
        }
        report("muzzle_effects_sequence_rollback", fine, "");
    }
    // (e) recognition and one claimant on the effects pair: for every listed muzzle and hull body, a mapped bullet body
    // and two others, under the effects VS with ONE / ONE and ONE / INVSRCCOLOR, the lone effects route (a mapped body),
    // the muzzle effects match and the hull hit match admit at most one, exactly the route that lists the body for that
    // blend; and 12009 is refused off its blend, VS, readiness, blend-on and when mapped.
    {
        constexpr std::uint32_t bullet_body = 1207u;
        std::vector<std::uint32_t> node(0x160 / 4, 0u);
        const auto body_id = [&](std::uint32_t* out) {
            return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + mz::node_body_offset, out,
                                            sizeof *out);
        };
        const auto mapped = [&](std::uint32_t id) { return id == bullet_body; };
        const auto unmapped = [](std::uint32_t) { return false; };
        x3m::engine_memory::next_frame();
        bool candidate = false;
        const auto muzzle = [&](std::uint64_t vs, bool ready, bool on, DWORD dst, std::uint32_t id, bool map) {
            node[mz::node_body_offset / 4] = id;
            return map ? mz::effects_match(vs, ready, on, D3DBLEND_ONE, dst, D3DBLENDOP_ADD, body_id, mapped, &candidate)
                       : mz::effects_match(vs, ready, on, D3DBLEND_ONE, dst, D3DBLENDOP_ADD, body_id, unmapped, &candidate);
        };
        const std::uint64_t evs = mz::effects_vertex_shader;
        unsigned refused = 0;
        const bool taken = muzzle(evs, true, true, D3DBLEND_INVSRCCOLOR, 12009u, true) == body;
        refused += muzzle(evs, true, true, D3DBLEND_ONE, 12009u, true) >= mz::body_count && !candidate;
        refused += muzzle(mz::vertex_shader, true, true, D3DBLEND_INVSRCCOLOR, 12009u, true) >= mz::body_count && !candidate;
        refused += muzzle(evs, false, true, D3DBLEND_INVSRCCOLOR, 12009u, true) >= mz::body_count && !candidate;
        refused += muzzle(evs, true, false, D3DBLEND_INVSRCCOLOR, 12009u, true) >= mz::body_count && !candidate;
        refused += muzzle(evs, true, true, D3DBLEND_INVSRCCOLOR, 445u, true) >= mz::body_count && candidate;
        {
            // 12009 in the bullet map: left to the lone effects route
            node[mz::node_body_offset / 4] = 12009u;
            refused += mz::effects_match(evs, true, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, body_id,
                                         [](std::uint32_t) { return true; }, &candidate) >= mz::body_count;
        }
        std::vector<std::uint32_t> ids = {bullet_body, 333u, 10656u};
        for (unsigned i = 0; i < mz::body_count; ++i) ids.push_back(mz::bodies[i].id);
        for (unsigned i = 0; i < hh::table_count; ++i) ids.push_back(hh::bodies[i].id);
        unsigned multi = 0, mismatch = 0;
        for (const std::uint32_t id : ids)
            for (const DWORD dst : {DWORD(D3DBLEND_ONE), DWORD(D3DBLEND_INVSRCCOLOR)}) {
                const unsigned mzi = mz::body_index(id);
                const unsigned hhi = hh::body_index(id, hh::edition_fl);
                const bool lone = mapped(id);
                const bool mz_claim = muzzle(evs, true, true, dst, id, true) < mz::body_count;
                node[mz::node_body_offset / 4] = id;
                const bool hh_claim = hh::match(evs, true, D3DBLEND_ONE, dst, D3DBLENDOP_ADD, 0, true, true, body_id, mapped,
                                                hh::edition_fl)
                                          .body < hh::table_count;
                const unsigned claims = unsigned(lone) + unsigned(mz_claim) + unsigned(hh_claim);
                multi += claims > 1;
                const bool mz_want = mzi < mz::body_count && mz::bodies[mzi].pair == mz::pair_effects &&
                                     mz::bodies[mzi].dest_blend == dst;
                const bool hh_want = hhi < hh::table_count && hh::bodies[hhi].pair == hh::pair_effects &&
                                     (hh::bodies[hhi].blend == hh::blend_screen) == (dst == D3DBLEND_INVSRCCOLOR);
                mismatch += lone != (id == bullet_body) || mz_claim != mz_want || hh_claim != hh_want;
            }
        std::printf("MUZZLE_EFFECTS case=recognition taken=%u refused=%u/6 claim_ids=%u multiple_claimants=%u "
                    "claim_mismatches=%u\n",
                    unsigned(taken), refused, unsigned(ids.size()), multi, mismatch);
        report("muzzle_effects_match_one_claimant", taken && refused == 6 && multi == 0 && mismatch == 0, "");
    }
    // (f) the stand-in at three poses (as the classic muzzle case's), 30 m, 1920 x 1080 FP16, under ONE / INVSRCCOLOR
    // over black; vanilla: the game pair; re-shaded: the production sequence with 12009's block and the draw's c30.xy
    Com<IDirect3DTexture9> rt;
    Com<IDirect3DSurface9> rt_s, sys;
    ok = SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p,
                                      nullptr)) &&
         SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
         SUCCEEDED(dev->CreateOffscreenPlainSurface(1920, 1080, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
    device_guard("muzzle_effects_targets");
    std::vector<float> image(std::size_t(1920) * 1080 * 3);
    const auto read_image = [&] {
        if (FAILED(dev->GetRenderTargetData(rt_s.p, sys.p))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        for (UINT y = 0; y < 1080; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < 1920; ++x)
                for (unsigned k = 0; k < 3; ++k) image[(std::size_t(y) * 1920 + x) * 3 + k] = half_to_float(row[x * 4 + k]);
        }
        sys.p->UnlockRect();
        return true;
    };
    float clip[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, clip);
    const struct {
        const char* name;
        float yaw;
    } poses[3] = {{"tilt", 0.4363f}, {"axis", 1.5707963f}, {"side", 0.f}};
    float app[12];
    for (unsigned i = 0; i < 12; ++i) app[i] = 0.125f * float(i) - 0.75f;
    float base[mz::effects_floats], plain[mz::effects_floats], rows[24];
    mz::effects_constants(b12009, tune, base);
    bool during_ok = true, restored_all = true, getters_ok = true, energy_ok = true, extent_ok = true;
    ef::Material tilt{};
    if (ok) dev->SetRenderTarget(0, rt_s.p);
    for (unsigned pose = 0; pose < 3 && ok; ++pose) {
        ef::Material m = ef::identity_material();
        const float centre[3] = {0.f, 0.f, 30.f};
        ef::place(mesh, centre, mf::peak_scale, poses[pose].yaw, 0.3491f, clip, m);
        if (!pose) tilt = m;
        ef::effects_rows(m, rows);
        std::memcpy(plain, base, sizeof plain);
        ok = mz::effects_draw_constants(b12009, rows, plain);
        mf::Figures figs[2];
        // the frame: four crops of 320 px about the screen centre (the flash's box centre), side by side: vanilla on
        // black, vanilla on a linear 0.6 grey, re-shaded on black, re-shaded on grey (the hull frames' layout)
        constexpr UINT crop = 320, fw = crop * 4;
        std::vector<float> frame(std::size_t(fw) * crop * 3, 0.f);
        for (unsigned pass = 0; pass < 4 && ok; ++pass) {
            const unsigned which = pass / 2, grey = pass % 2;
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, grey ? D3DCOLOR_ARGB(255, 153, 153, 153) : D3DCOLOR_ARGB(0, 0, 0, 0),
                       1.f, 0);
            dev->BeginScene();
            ef::bind_constants(dev, m);
            ef::bind_state(dev, tex.p, true, D3DBLEND_INVSRCCOLOR);
            dev->SetVertexDeclaration(decl.p);
            dev->SetStreamSource(0, vb.p, 0, sizeof(ef::Vertex));
            dev->SetVertexShader(game_vs.p);
            dev->SetPixelShader(game_ps.p);
            dev->SetPixelShaderConstantF(mz::effects_register, app, mz::effects_count);
            HRESULT hr = S_OK;
            if (which) {
                auto ops = ef::device_ops(dev);
                mz::PairApplied applied;
                hr = mz::begin_pair(ops, our_vs.p, our_ps.p, plain, game_vs.p, app, applied);
                IDirect3DVertexShader9* bound_vs = nullptr;
                IDirect3DPixelShader9* bound_ps = nullptr;
                float got_c[12]{};
                during_ok = during_ok && SUCCEEDED(hr) && SUCCEEDED(dev->GetVertexShader(&bound_vs)) &&
                            bound_vs == our_vs.p && SUCCEEDED(dev->GetPixelShader(&bound_ps)) && bound_ps == our_ps.p &&
                            SUCCEEDED(dev->GetPixelShaderConstantF(mz::effects_register, got_c, mz::effects_count)) &&
                            !std::memcmp(got_c, plain, sizeof got_c);
                if (bound_vs) bound_vs->Release();
                if (bound_ps) bound_ps->Release();
                if (SUCCEEDED(hr)) hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims);
                restored_all = restored_all && SUCCEEDED(mz::end_pair(ops, applied, game_ps.p, game_vs.p, app)) &&
                               SUCCEEDED(hr);
                bound_vs = nullptr;
                bound_ps = nullptr;
                getters_ok = getters_ok && SUCCEEDED(dev->GetVertexShader(&bound_vs)) && bound_vs == game_vs.p &&
                             SUCCEEDED(dev->GetPixelShader(&bound_ps)) && bound_ps == game_ps.p &&
                             SUCCEEDED(dev->GetPixelShaderConstantF(mz::effects_register, got_c, mz::effects_count)) &&
                             !std::memcmp(got_c, app, sizeof got_c);
                if (bound_vs) bound_vs->Release();
                if (bound_ps) bound_ps->Release();
            } else
                hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims);
            dev->EndScene();
            ok = SUCCEEDED(hr) && read_image();
            if (ok && !grey) figs[which] = mf::figures(image.data(), 1920, 1080, 0.f);
            for (UINT y = 0; ok && y < crop; ++y)
                for (UINT x = 0; x < crop; ++x)
                    for (unsigned k = 0; k < 3; ++k)
                        frame[(std::size_t(y) * fw + pass * crop + x) * 3 + k] =
                            image[(std::size_t(540 - crop / 2 + y) * 1920 + 960 - crop / 2 + x) * 3 + k];
        }
        if (ok) { // fixture-muzzle-effects-<pose>.pfm in the working directory's dump-muzzle-effects (run_weapon_effects.py
                  // writes the PNG beside the muzzle results)
            CreateDirectoryA("dump-muzzle-effects", nullptr);
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "dump-muzzle-effects\\fixture-muzzle-effects-%s.pfm", poses[pose].name);
            if (std::FILE* f = std::fopen(path, "wb")) {
                std::fprintf(f, "PF\n%u %u\n-1.0\n", fw, crop);
                for (UINT y = crop; y-- > 0;) std::fwrite(&frame[std::size_t(y) * fw * 3], sizeof(float), std::size_t(fw) * 3, f);
                std::fclose(f);
            }
        }
        const double energy = figs[0].sum > 0 ? figs[1].sum / figs[0].sum : 0.0;
        energy_ok = energy_ok && ok && figs[0].sum > 0.0 && energy <= double(gain);
        // lit: peak >= mz_gain / 4 at tilt and side, mz_gain / 8 axis-on. The bound is lower axis-on because of the
        // fan's own geometry, not because the tolerance was loosened: viewed down the axis, the eight axial quads are
        // edge-on, and the gun's summed luminance spreads over the cones at 42 and 78 degrees. The real 445 mesh does
        // the same: its re-shade peaks at 0.025 axis-on at the baked keys (muzzle-flash-look.md section 4).
        const float lit = pose == 1 ? 0.125f * gain : 0.25f * gain;
        extent_ok = extent_ok && ok && figs[1].peak >= lit && figs[1].tenth_r <= size * figs[0].tenth_r + 1.f &&
                    figs[1].peak_tenth_r <= figs[0].peak_tenth_r;
        const double overlap = plain[4] > 0.f ? double(plain[6]) / double(plain[4]) : 0.0;
        std::printf("MUZZLE_EFFECTS case=flash12009_%s body=12009 distance_m=30 yaw_deg=%.1f scale=%.3f overlap=%.3f "
                    "vanilla_sum=%.2f reshade_sum=%.2f energy_ratio=%.3f vanilla_peak=%.4f reshade_peak=%.4f "
                    "vanilla_tenth_r=%.1f reshade_tenth_r=%.1f vanilla_peak_tenth_r=%.1f reshade_peak_tenth_r=%.1f "
                    "lit_bound=%.4f size=%.3g gain=%.3g core=%.3g smooth=%.3g\n",
                    poses[pose].name, double(poses[pose].yaw) * 57.29578, double(mf::peak_scale), overlap, figs[0].sum,
                    figs[1].sum, energy, double(figs[0].peak), double(figs[1].peak), double(figs[0].tenth_r),
                    double(figs[1].tenth_r), double(figs[0].peak_tenth_r), double(figs[1].peak_tenth_r), double(lit),
                    double(size), double(gain), double(tune.mz_core), double(tune.mz_smooth));
    }
    report("muzzle_effects_route_binds_and_restores", ok && during_ok && restored_all && getters_ok, "");
    report("muzzle_effects_energy_within_gain", ok && energy_ok, "");
    report("muzzle_effects_extent_within_size", ok && extent_ok, "");
    // (g) the cost per draw: the six calls alone, the 100-triangle draw with and without the sequence (best of 7 runs of
    // 2000, fenced), the match (TLS lookup, the 4-byte validated node read through engine_memory::read, the list) and the
    // draw's c30.xy, for 12009 and for an unlisted id (12011: every compare, no block)
    LARGE_INTEGER freq{}, a{}, bq{};
    QueryPerformanceFrequency(&freq);
    Com<IDirect3DQuery9> fence;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p);
    const auto finish = [&] {
        if (!fence.p) return;
        fence.p->Issue(D3DISSUE_END);
        while (fence.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
        }
    };
    constexpr unsigned draws = 2000, runs = 7;
    double best_calls = 1e30, best_vanilla = 1e30, best_reshaded = 1e30, best_match = 1e30, best_miss = 1e30;
    ef::effects_rows(tilt, rows);
    std::memcpy(plain, base, sizeof plain);
    ok = ok && mz::effects_draw_constants(b12009, rows, plain);
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->BeginScene();
        ef::bind_constants(dev, tilt);
        ef::bind_state(dev, tex.p, true, D3DBLEND_INVSRCCOLOR);
        dev->SetVertexDeclaration(decl.p);
        dev->SetStreamSource(0, vb.p, 0, sizeof(ef::Vertex));
        dev->SetVertexShader(game_vs.p);
        dev->SetPixelShader(game_ps.p);
        auto ops = ef::device_ops(dev);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            mz::PairApplied applied;
            mz::begin_pair(ops, our_vs.p, our_ps.p, plain, game_vs.p, app, applied);
            mz::end_pair(ops, applied, game_ps.p, game_vs.p, app);
        }
        QueryPerformanceCounter(&bq);
        best_calls = std::min(best_calls, ticks_us(a, bq, freq) / draws);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims);
        finish();
        QueryPerformanceCounter(&bq);
        best_vanilla = std::min(best_vanilla, ticks_us(a, bq, freq) / draws);
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            mz::PairApplied applied;
            mz::begin_pair(ops, our_vs.p, our_ps.p, plain, game_vs.p, app, applied);
            dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims);
            mz::end_pair(ops, applied, game_ps.p, game_vs.p, app);
        }
        finish();
        QueryPerformanceCounter(&bq);
        best_reshaded = std::min(best_reshaded, ticks_us(a, bq, freq) / draws);
        dev->EndScene();
    }
    device_guard("muzzle_effects_cost");
    {
        std::vector<std::uint32_t> node(0x160 / 4, 0u);
        const DWORD slot = TlsAlloc();
        TlsSetValue(slot, node.data());
        x3m::engine_memory::next_frame();
        float block[mz::effects_floats];
        const std::uint32_t ids[2] = {12009u, 12011u};
        double* best[2] = {&best_match, &best_miss};
        for (unsigned which = 0; which < 2; ++which) {
            node[mz::node_body_offset / 4] = ids[which];
            unsigned hits = 0;
            for (unsigned run = 0; run < runs; ++run) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < draws; ++i) {
                    bool candidate = false;
                    const unsigned row = mz::effects_match(
                        mz::effects_vertex_shader, true, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD,
                        [&](std::uint32_t* id) {
                            const auto at = reinterpret_cast<std::uintptr_t>(TlsGetValue(slot));
                            return at && x3m::engine_memory::read(at + mz::node_body_offset, id, sizeof *id);
                        },
                        [](std::uint32_t) { return false; }, &candidate);
                    if (row < mz::body_count) {
                        std::memcpy(block, base, sizeof block);
                        if (mz::effects_draw_constants(mz::bodies[row], rows, block)) ++hits;
                    }
                }
                QueryPerformanceCounter(&bq);
                *best[which] = std::min(*best[which], ticks_us(a, bq, freq) / draws);
            }
            ok = ok && hits == (which ? 0u : draws * runs);
        }
        TlsFree(slot);
    }
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("MUZZLE_EFFECTS case=cost draws=%u runs=%u primitives=%u calls_us_per_draw=%.3f draw_us_vanilla=%.3f "
                "draw_us_reshaded=%.3f substitution_us_per_draw=%.3f match_us_per_draw=%.3f match_miss_us_per_draw=%.3f "
                "fenced=%u\n",
                draws, runs, prims, best_calls, best_vanilla, best_reshaded, best_reshaded - best_vanilla, best_match,
                best_miss, unsigned(fence.p != nullptr));
    report("muzzle_effects_cost_measured", ok && fence.p && best_calls < 50.0 && best_match < 10.0 && best_miss < 10.0,
           "");
}
