#pragma once
// The bullet re-shade's lone classic cases (docs/reverse-engineering/fl/bullet-draw-batch.md section 7 "Classic bodies";
// RESHADE case=lone_classic_* rows), included by weapon_effects_fixture.cpp after its helpers (dev, caps, report,
// compile, half_to_float, ticks_us, Com, device_guard). Unlike the other re-shade cases the draws go through the game's
// own standard_lighting / standard_lighting2s DEFAULT programs: run_weapon_effects.py extracts their bytes from the FL
// catalogues into the untracked working directory (classic_vs.bin 494fe349b8bc12ec, classic_ps.bin ced7edef48059e84,
// classic_ps2s.bin 0d5848c8489b6630) and the cases check each file's FNV-1a 64; without them the cases fail. The game's
// VS constants are set as the material submit sets them for a classic bullet body (g_mWorldViewProjection c24..c27,
// g_mWorld / g_mWorldIT / g_mViewInverse identity, g_TexMatrix c37..c38, g_AlphaValue c39.x the node fade, the
// emissive c40, no point light, fog off); the PS's material constants c4..c13 are fixture values (glow off, emissive
// lit). The re-shade program is the production embedded ps_3_0 bytecode, swapped in through the production sequence
// (bullet_reshade_route.h) exactly as MotionOutput::begin_bullet_reshade / end_bullet_reshade do for a lone classic draw
// off the additive route (the game's ONE / ONE, Z write normalised, the far flag outside the route), and recognised by
// the production match (bullet_reshade_core.h classic_match over the body map: the gates, their order, the muzzle-body
// exclusion and the verdict MotionOutput::lone_bullet_match runs). Never launches the game; no game
// bytes are tracked (the mesh and texture are the procedural IRE stand-ins).
namespace classic_fx {
constexpr DWORD program[] = {
#include "../../src/renderer/weapon_bullet_reshade_classic_pixel_program_inc.h"
};
constexpr DWORD flag_program[] = {
#include "../../src/renderer/weapon_bullet_reshade_classic_flag_pixel_program_inc.h"
};
// The classic body vertex the VS declares: POSITION, TEXCOORD0, NORMAL (stride 32; no vertex colour).
struct Vertex {
    float x, y, z, u, v, nx, ny, nz;
};
static_assert(sizeof(Vertex) == 32, "the classic body vertex");
inline const D3DVERTEXELEMENT9* declaration() {
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
// The material submit's VS constants for a classic body: rows c24..c27 (the clip transform, rf::clip_rows' layout),
// identity world / world inverse transpose / view inverse, g_TexMatrix rows (u', v') = (u, v, 1) . c37 / c38,
// g_AlphaValue c39.x, g_MatEmissiveColor c40, g_FogClip c41 0; i0 = 0 point lights, b0 fog off. The PS: g_EnableGlow c4.x,
// both directional lights black, specular / reflection / diffuse strength 0, quality colour 1.
inline void bind_constants(IDirect3DDevice9* dev, const float clip[16], const float tex_matrix[8], float fade, float glow) {
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
    float p[10 * 4]{};
    p[0] = glow;                                          // c4 g_EnableGlow
    p[8 * 4] = p[8 * 4 + 1] = p[8 * 4 + 2] = p[8 * 4 + 3] = 1.f; // c12 g_MatQualityColor
    dev->SetPixelShaderConstantF(4, p, 10);
}
// A 1 x 1 or 2 x 1 MANAGED A8R8G8B8 texture from RGBA bytes.
inline IDirect3DTexture9* texels(IDirect3DDevice9* dev, const unsigned char* rgba, unsigned n) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(n, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) {
        t->Release();
        return nullptr;
    }
    auto* b = static_cast<unsigned char*>(lr.pBits);
    for (unsigned i = 0; i < n; ++i)
        b[4 * i] = rgba[4 * i + 2], b[4 * i + 1] = rgba[4 * i + 1], b[4 * i + 2] = rgba[4 * i], b[4 * i + 3] = rgba[4 * i + 3];
    t->UnlockRect(0);
    return t;
}
} // namespace classic_fx

void lone_classic_cases() {
    namespace rs = x3m::weapon_effects::reshade;
    namespace rf = reshade_fx;
    namespace cf = classic_fx;
    // (0) the game's programs and ours
    std::vector<DWORD> vs_code, ps_code, ps2s_code;
    const bool loaded = cf::load("classic_vs.bin", vs_code) && cf::load("classic_ps.bin", ps_code) &&
                        cf::load("classic_ps2s.bin", ps2s_code);
    // FL's pair by name: the fixture builds without an edition define, so the build's default row is AP's
    // (lone_classic_ap_case below)
    const std::uint64_t* fl_pair = rs::classic_pixel_shader_pairs[rs::pair_fl];
    const bool hashes = loaded && cf::hash(vs_code) == rs::classic_vertex_shader && cf::hash(ps_code) == fl_pair[0] &&
                        cf::hash(ps2s_code) == fl_pair[1];
    Com<IDirect3DVertexShader9> vs, batch_vs;
    Com<IDirect3DPixelShader9> ps1, ps2s, ours, ours_flag, batch_ours;
    Com<IDirect3DVertexDeclaration9> decl, batch_decl;
    LPD3DXBUFFER batch_hold = nullptr;
    const DWORD* batch_code = nullptr;
    const bool created = hashes && SUCCEEDED(dev->CreateVertexShader(vs_code.data(), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps_code.data(), &ps1.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps2s_code.data(), &ps2s.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(cf::program, &ours.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(cf::flag_program, &ours_flag.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(cf::declaration(), &decl.p)) && compile_shader &&
                         compile(rf::vs_source, "vs_1_1", &batch_code, &batch_hold) &&
                         SUCCEEDED(dev->CreateVertexShader(batch_code, &batch_vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::reshade_program, &batch_ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::declaration(), &batch_decl.p));
    if (batch_hold) batch_hold->Release();
    std::printf("RESHADE case=lone_classic_programs loaded=%u game_hashes=%u vs_dwords=%u ps_dwords=%u ps2s_dwords=%u "
                "created=%u words=%u flag_words=%u ps_major=%u\n",
                unsigned(loaded), unsigned(hashes), unsigned(vs_code.size()), unsigned(ps_code.size()),
                unsigned(ps2s_code.size()), unsigned(created), unsigned(sizeof cf::program / sizeof cf::program[0]),
                unsigned(sizeof cf::flag_program / sizeof cf::flag_program[0]), unsigned((cf::program[0] >> 8) & 0xffu));
    report("reshade_lone_classic_programs_create", created, "");
    if (!created) return;

    // (a) recognition: the production verdict over a body map built from a TBullets stand-in (classic PAL v/10385,
    // Kyon v/00527 and EMP v/00517, an effects IRE body, and a row naming the muzzle body 446, which no FL row does: the
    // muzzle route's draw even then), the scope node's body id read by the production reader
    constexpr std::uint32_t pal_body = 10385u, kyon_body = 527u, emp_body = 517u, ire_body = 1207u, muzzle_body = 445u,
                            muzzle_mapped = 446u;
    static wc::Table table{};
    table.rows = 5;
    const std::uint32_t names[5] = {0x10385u, 0x00527u, 0x00517u, 0x1e1e1e1eu, 0x00446u};
    const std::uint16_t row_ids[5] = {std::uint16_t(pal_body), std::uint16_t(kyon_body), std::uint16_t(emp_body),
                                      std::uint16_t(ire_body), std::uint16_t(muzzle_mapped)};
    for (unsigned r = 0; r < 5; ++r) {
        table.bits[r] = wc::row_present;
        table.body_hash[r] = table.bullet_body_hash[r] = names[r];
    }
    table.bullet_bodies = 5;
    // a head + fin layout (the IRE's words: the stand-in bolt's UV, so the geometric size is exercised)
    const std::int32_t pal_words[8] = {3, 57881, 56830, 6546, 23063, 18300, 858, 64010};
    std::memcpy(table.bullet_body_layout[0], pal_words, sizeof pal_words);
    std::vector<unsigned char> tbullets(5 * wc::layout::type_stride, 0);
    for (unsigned r = 0; r < 5; ++r) std::memcpy(&tbullets[r * wc::layout::type_stride + rs::tbullets_body], &row_ids[r], 2);
    const std::uint32_t table_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(tbullets.data())), count_slot = 5;
    const auto reader = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); };
    x3m::engine_memory::next_frame();
    rs::BodyMap map;
    const bool built = rs::build_body_map(map, &table, reinterpret_cast<std::uintptr_t>(&table_slot),
                                          reinterpret_cast<std::uintptr_t>(&count_slot), reader) &&
                       map.state == 1 && map.count == 5;
    std::vector<std::uint32_t> node(0x160 / 4, 0u);
    // MotionOutput::lone_bullet_match's classic decision, the production function: VS gate, blend on, the node's body
    // in the map and not a muzzle body, the verdict (ADD ONE / ONE only)
    const auto recognise = [&](std::uint64_t vs_hash, std::uint64_t ps_hash, bool blend, DWORD src, DWORD dst, DWORD op,
                               std::uint32_t id, bool ready, unsigned* why) -> unsigned {
        node[rs::node_body_offset / 4] = id;
        const rs::ClassicMatch m = rs::classic_match(
            vs_hash, blend, src, dst, op, ps_hash, ready,
            [&](std::uint32_t* out) {
                return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + rs::node_body_offset, out,
                                                sizeof *out);
            },
            [&](std::uint32_t b) { return rs::body_layout(map, &table, b) != nullptr; }, rs::pair_fl);
        *why = m.why;
        return m.blend;
    };
    constexpr std::uint64_t sl = 0xced7edef48059e84ull, sl2s = 0x0d5848c8489b6630ull, other_ps = 0x7c83ed50c9894e44ull;
    constexpr unsigned none = rs::classic_refused_count;
    const struct {
        const char* name;
        std::uint64_t vs, ps;
        bool blend;
        DWORD src, dst;
        std::uint32_t id;
        bool ready;
        unsigned want, why;
    } rows_a[] = {
        {"pal_two_sided_one_one", rs::classic_vertex_shader, sl2s, true, D3DBLEND_ONE, D3DBLEND_ONE, pal_body, true,
         rs::lone_additive, none},
        {"kyon_one_sided_one_one", rs::classic_vertex_shader, sl, true, D3DBLEND_ONE, D3DBLEND_ONE, kyon_body, true,
         rs::lone_additive, none},
        {"pal_one_invsrccolor", rs::classic_vertex_shader, sl2s, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, pal_body, true,
         rs::lone_none, rs::classic_blend},
        {"emp_alphablend", rs::classic_vertex_shader, sl, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, emp_body, true,
         rs::lone_none, rs::classic_blend},
        {"mass_null_part_srcalpha_one", rs::classic_vertex_shader, sl2s, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, pal_body,
         true, rs::lone_none, rs::classic_blend},
        {"wrong_ps", rs::classic_vertex_shader, other_ps, true, D3DBLEND_ONE, D3DBLEND_ONE, pal_body, true, rs::lone_none,
         rs::classic_program},
        {"unready", rs::classic_vertex_shader, sl, true, D3DBLEND_ONE, D3DBLEND_ONE, kyon_body, false, rs::lone_none,
         rs::classic_unready},
        {"muzzle_body_445", rs::classic_vertex_shader, sl2s, true, D3DBLEND_ONE, D3DBLEND_ONE, muzzle_body, true,
         rs::lone_none, none},
        {"muzzle_body_446_mapped", rs::classic_vertex_shader, sl2s, true, D3DBLEND_ONE, D3DBLEND_ONE, muzzle_mapped, true,
         rs::lone_none, none},
        {"blend_off", rs::classic_vertex_shader, sl, false, D3DBLEND_ONE, D3DBLEND_ZERO, pal_body, true, rs::lone_none, none},
        {"effects_vs", 0xd5e1c75351ed3f04ull, sl, true, D3DBLEND_ONE, D3DBLEND_ONE, pal_body, true, rs::lone_none, none},
    };
    bool recognition = built;
    for (const auto& r : rows_a) {
        unsigned why = 0;
        const unsigned got = recognise(r.vs, r.ps, r.blend, r.src, r.dst, D3DBLENDOP_ADD, r.id, r.ready, &why);
        const bool ok = got == r.want && why == r.why;
        recognition = recognition && ok;
        std::printf("RESHADE case=lone_classic_recognition row=%s body=%lu class=%u want=%u why=%s ok=%u\n", r.name,
                    static_cast<unsigned long>(r.id), got, r.want, why < none ? rs::classic_refused_names[why] : "none",
                    unsigned(ok));
    }
    report("reshade_lone_classic_recognised_by_body_id", recognition, "");

    // (b) our program behind the game's VS against the model (blend off, a full-target quad, 16 x 16 FP16): the fade
    // from c39.x through COLOR0.a, the UV through g_TexMatrix (a 2 x 1 point-sampled texture: identity -> texel 0,
    // u + 0.5 -> texel 1), vanilla's alpha lane (glow off: texel.a x fade; glow on: the light map's alpha x fade)
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    float constants[4];
    rs::constants(tune, constants);
    const float app_block[12] = {3.f, -1.f, 0.25f, 9.f, -4.f, 0.5f, 2.f, -0.75f, 0.125f, -2.5f, 7.f, 0.0625f};
    const unsigned char two[8] = {99, 77, 17, 200, 200, 150, 40, 120};
    const unsigned char light_px[4] = {0, 0, 0, 64};
    Com<IDirect3DTexture9> tex2, light, rt16;
    Com<IDirect3DSurface9> rt16_s, sys16;
    tex2.p = cf::texels(dev, two, 2);
    light.p = cf::texels(dev, light_px, 1);
    bool ok = tex2.p && light.p &&
              SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt16.p,
                                           nullptr)) &&
              SUCCEEDED(rt16.p->GetSurfaceLevel(0, &rt16_s.p)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys16.p, nullptr));
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    float identity_clip[16]{};
    identity_clip[0] = identity_clip[5] = identity_clip[10] = identity_clip[15] = 1.f;
    const float tex_identity[8] = {1, 0, 0, 0, 0, 1, 0, 0}, tex_shift[8] = {1, 0, 0.5f, 0, 0, 1, 0, 0};
    const cf::Vertex quad[6] = {{-1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1}, {-1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},
                                {1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},   {-1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1},
                                {1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},   {1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1}};
    const auto pass_state = [&](IDirect3DPixelShader9* game, DWORD cull) {
        dev->SetVertexShader(vs.p);
        dev->SetVertexDeclaration(decl.p);
        dev->SetPixelShader(game);
        for (DWORD s = 0; s < 4; ++s) {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
        }
        dev->SetTexture(1, nullptr);
        dev->SetTexture(2, light.p);
        dev->SetTexture(3, nullptr);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, cull);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        dev->SetPixelShaderConstantF(rs::block_register, app_block, rs::block_count);
    };
    struct ModelCase {
        const float* tex_matrix;
        unsigned texel;
        float fade, glow;
    };
    const ModelCase model_cases[4] = {{tex_identity, 0, 0.8f, 0.f}, {tex_shift, 1, 0.8f, 0.f}, {tex_identity, 0, 0.35f, 0.f},
                                      {tex_identity, 0, 0.8f, 1.f}};
    float worst_model = 0.f, worst_alpha = 0.f;
    bool vanilla_finite = true;
    if (ok) dev->SetRenderTarget(0, rt16_s.p);
    for (unsigned c = 0; c < 4 && ok; ++c) {
        const ModelCase& k = model_cases[c];
        for (unsigned which = 0; which < 2 && ok; ++which) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->BeginScene();
            pass_state(ps1.p, D3DCULL_NONE);
            dev->SetTexture(0, tex2.p);
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            cf::bind_constants(dev, identity_clip, k.tex_matrix, k.fade, k.glow);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); // the program's output itself
            float block[12];
            for (unsigned i = 0; i < 8; ++i) block[i] = rs::identity_layout[i];
            for (unsigned i = 0; i < 4; ++i) block[8 + i] = constants[i];
            if (which) {
                dev->SetPixelShaderConstantF(rs::block_register, block, rs::block_count);
                dev->SetPixelShader(ours.p);
            }
            ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(cf::Vertex)));
            dev->EndScene();
            ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt16_s.p, sys16.p));
            D3DLOCKED_RECT o{};
            ok = ok && SUCCEEDED(sys16.p->LockRect(&o, nullptr, D3DLOCK_READONLY));
            if (!ok) break;
            const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(o.pBits) + 8 * o.Pitch) + 8 * 4;
            const float got[4] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]), half_to_float(px[3])};
            sys16.p->UnlockRect();
            if (!which) {
                for (unsigned i = 0; i < 4; ++i) vanilla_finite = vanilla_finite && std::isfinite(got[i]);
                continue;
            }
            const unsigned char* tb = two + 4 * k.texel;
            const float t[3] = {tb[0] / 255.f, tb[1] / 255.f, tb[2] / 255.f};
            float want[3];
            rs::model(constants, t, k.fade, want);
            const float want_a = (k.glow > 0.f ? light_px[3] / 255.f : tb[3] / 255.f) * k.fade;
            for (unsigned i = 0; i < 3; ++i)
                worst_model = std::max(worst_model, std::fabs(got[i] - want[i]) / (0.004f + 0.004f * std::fabs(want[i])));
            worst_alpha = std::max(worst_alpha, std::fabs(got[3] - want_a) / (0.004f + 0.004f * want_a));
            std::printf("RESHADE case=lone_classic_texel index=%u texel=%u fade=%.2f glow=%.0f got=%.4f,%.4f,%.4f,%.4f "
                        "model=%.4f,%.4f,%.4f,%.4f\n",
                        c, k.texel, double(k.fade), double(k.glow), double(got[0]), double(got[1]), double(got[2]),
                        double(got[3]), double(want[0]), double(want[1]), double(want[2]), double(want_a));
        }
    }
    std::printf("RESHADE case=lone_classic_model worst_rel=%.3f worst_alpha_rel=%.3f vanilla_finite=%u\n",
                double(worst_model), double(worst_alpha), unsigned(vanilla_finite));
    report("reshade_lone_classic_program_matches_model", ok && worst_model <= 1.f && worst_alpha <= 1.f && vanilla_finite, "");

    // (c) the stand-in bolt (the IRE topology and strip texture, fade 204 / 255) at three depths on 1920 x 1080 FP16,
    // ONE / ONE: vanilla (the game's pair), the lone classic re-shade through the production sequence (from the
    // application's Z write on, the lone plain bits), and the batch reference (the batch's vs_1_1 stand-in and the
    // production ps_2_0 batch program, COLOR0.a the same fade, the same c29..c31): the lone classic frame equals the
    // batched one. Gates per depth: lit (peak over 0.002), the lit pixel count (over 0.001) equal to the batch's, the
    // bound program, c29..c31, DESTBLEND, Z write and the cull mode during and after, the game's VS constants and c4
    // untouched; at the far depth
    // the far flag outside the route (RT2's lane base 1.0, the classic flag variant, flag_bind / flag_unbind).
    Com<IDirect3DTexture9> rt, rt1, rt2, tex;
    Com<IDirect3DSurface9> rt_s, rt1_s, rt2_s, sys;
    Com<IDirect3DVertexBuffer9> vb, batch_vb;
    unsigned side = 0;
    const std::vector<unsigned char> texture = rf::standin_texture(&side);
    tex.p = rf::make_texture(dev, texture.data(), side);
    const rf::Mesh mesh = rf::standin_mesh();
    ok = ok && tex.p &&
         SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p, nullptr)) &&
         SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
         SUCCEEDED(dev->CreateOffscreenPlainSurface(1920, 1080, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
         SUCCEEDED(dev->CreateVertexBuffer(66 * sizeof(cf::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb.p, nullptr)) &&
         SUCCEEDED(dev->CreateVertexBuffer(66 * sizeof(rf::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &batch_vb.p, nullptr));
    const bool mrt = caps.NumSimultaneousRTs >= 3 && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
    if (ok && mrt)
        ok = SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt1.p,
                                          nullptr)) &&
             SUCCEEDED(rt1.p->GetSurfaceLevel(0, &rt1_s.p)) &&
             SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt2.p,
                                          nullptr)) &&
             SUCCEEDED(rt2.p->GetSurfaceLevel(0, &rt2_s.p));
    report("reshade_lone_classic_targets", ok, "");
    if (!ok) {
        dev->SetRenderTarget(0, old_rt);
        if (old_rt) old_rt->Release();
        return;
    }
    float clip[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, clip);
    float layout[8];
    rs::layout_constants(pal_words, tune, layout);
    constexpr BYTE fade_byte = 204;
    const float fade = fade_byte / 255.f;
    const auto fill = [&](float distance) {
        rf::Vertex v[66];
        const float centre[3] = {0.f, 0.f, distance};
        rf::place(mesh, centre, 0.44f, 0.35f, fade_byte, v);
        void* p = nullptr;
        if (FAILED(batch_vb.p->Lock(0, 0, &p, 0))) return false;
        std::memcpy(p, v, sizeof v);
        batch_vb.p->Unlock();
        cf::Vertex c[66];
        for (unsigned i = 0; i < 66; ++i) c[i] = cf::Vertex{v[i].x, v[i].y, v[i].z, v[i].u, v[i].v, 0.f, 0.f, -1.f};
        if (FAILED(vb.p->Lock(0, 0, &p, 0))) return false;
        std::memcpy(p, c, sizeof c);
        vb.p->Unlock();
        return true;
    };
    std::vector<float> img[3];
    const auto read_image = [&](IDirect3DSurface9* s, std::vector<float>& out) {
        if (FAILED(dev->GetRenderTargetData(s, sys.p))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        out.resize(std::size_t(1920) * 1080 * 4);
        for (UINT y = 0; y < 1080; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < 1920 * 4; ++x) out[std::size_t(y) * 1920 * 4 + x] = half_to_float(row[x]);
        }
        sys.p->UnlockRect();
        return true;
    };
    const auto stats = [](const std::vector<float>& im, float* peak, unsigned* lit) {
        *peak = 0.f;
        *lit = 0;
        for (std::size_t i = 0; i < im.size(); i += 4) {
            const float l = 0.2126f * im[i] + 0.7152f * im[i + 1] + 0.0722f * im[i + 2];
            *peak = std::max(*peak, l);
            if (l > 0.001f) ++*lit; // lit: above 0.001 linear (the far stand-in bolt peaks under 0.01)
        }
    };
    // the states the sequence must leave as the application set them (the cull mode of the program's draw included)
    const auto vs_untouched = [&](float fade_want) {
        float c[4 * 4]{}, p4[4]{};
        return SUCCEEDED(dev->GetVertexShaderConstantF(37, c, 3)) && c[0] == 1.f && c[5] == 1.f && c[8] == fade_want &&
               SUCCEEDED(dev->GetPixelShaderConstantF(4, p4, 1)) && p4[0] == 0.f;
    };
    struct Depth {
        float distance;
        bool two_sided;
    };
    const Depth depths[3] = {{50.f, true}, {300.f, false}, {1500.f, true}};
    bool during_all = true, after_all = true, lit_all = true, same_all = true;
    float worst_batch = 0.f;
    for (unsigned d = 0; d < 3 && ok; ++d) {
        const Depth& k = depths[d];
        IDirect3DPixelShader9* game = k.two_sided ? ps2s.p : ps1.p;
        const DWORD cull = k.two_sided ? D3DCULL_NONE : D3DCULL_CCW;
        ok = fill(k.distance);
        dev->SetRenderTarget(0, rt_s.p);
        for (unsigned which = 0; which < 3 && ok; ++which) { // 0 vanilla, 1 lone classic re-shade, 2 batch reference
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->BeginScene();
            if (which < 2) {
                pass_state(game, cull);
                dev->SetTexture(0, tex.p);
                cf::bind_constants(dev, clip, tex_identity, fade, 0.f);
                dev->SetStreamSource(0, vb.p, 0, sizeof(cf::Vertex));
                dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE); // the application's (normalised by the sequence)
            } else {
                rf::bind_pass(dev, batch_vs.p, batch_decl.p, tex.p, clip, false);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_CULLMODE, cull);
                dev->SetStreamSource(0, batch_vb.p, 0, sizeof(rf::Vertex));
            }
            if (which == 0) {
                ok = SUCCEEDED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22));
            } else {
                rs::ScreenState own;
                for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->GetRenderState(rs::lone_states[i], &own.lone[i]);
                auto ops = rf::device_ops(dev);
                rs::Plan plan;
                plan.program = which == 1 ? ours.p : batch_ours.p;
                plan.constants = constants;
                plan.layout = layout;
                plan.states = which == 1 ? rs::state_differs(own) & rs::lone_plain_bits : 0u;
                plan.state_app = own.lone;
                rs::Applied applied;
                ok = SUCCEEDED(rs::begin(ops, plan, app_block, applied));
                if (which == 1) {
                    DWORD z = ~0u, cm = ~0u, blend = 0;
                    IDirect3DPixelShader9* bound = nullptr;
                    float block[12]{}, want[12];
                    rs::plan_constants(plan, want);
                    const bool during = ok && applied.states == rs::state_zwrite &&
                                        SUCCEEDED(dev->GetRenderState(D3DRS_ZWRITEENABLE, &z)) && z == FALSE &&
                                        SUCCEEDED(dev->GetRenderState(D3DRS_CULLMODE, &cm)) && cm == cull &&
                                        SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) && blend == D3DBLEND_ONE &&
                                        SUCCEEDED(dev->GetPixelShader(&bound)) && bound == ours.p &&
                                        SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                                        !std::memcmp(block, want, sizeof block) && vs_untouched(fade);
                    if (bound) bound->Release();
                    during_all = during_all && during;
                }
                ok = ok && SUCCEEDED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22));
                const HRESULT back = rs::end(ops, applied, which == 1 ? game : nullptr, app_block);
                if (which == 1) {
                    DWORD z = ~0u, cm = ~0u, blend = 0;
                    IDirect3DPixelShader9* bound = nullptr;
                    float block[12]{};
                    const bool after = SUCCEEDED(back) && SUCCEEDED(dev->GetRenderState(D3DRS_ZWRITEENABLE, &z)) &&
                                       z == TRUE && SUCCEEDED(dev->GetRenderState(D3DRS_CULLMODE, &cm)) && cm == cull &&
                                       SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) && blend == D3DBLEND_ONE &&
                                       SUCCEEDED(dev->GetPixelShader(&bound)) && bound == game &&
                                       SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                                       !std::memcmp(block, app_block, sizeof block) && vs_untouched(fade);
                    if (bound) bound->Release();
                    after_all = after_all && after;
                }
            }
            dev->EndScene();
            ok = ok && read_image(rt_s.p, img[which]);
        }
        if (!ok) break;
        float peak[3]{};
        unsigned lit[3]{};
        for (unsigned i = 0; i < 3; ++i) stats(img[i], &peak[i], &lit[i]);
        float worst = 0.f;
        unsigned off = 0;
        for (std::size_t i = 0; i < img[1].size(); ++i) {
            if ((i & 3) == 3) continue; // colour lanes (the batch program's alpha is the texel's, ours vanilla's)
            const float e = std::fabs(img[1][i] - img[2][i]) / (0.004f + 0.004f * std::fabs(img[2][i]));
            worst = std::max(worst, e);
            off += e > 1.f;
        }
        worst_batch = std::max(worst_batch, worst);
        const bool lit_ok = peak[1] > 0.002f && lit[1] > 0;
        const bool same = off <= lit[2] / 200u && (lit[1] > lit[2] ? lit[1] - lit[2] : lit[2] - lit[1]) <= 1u + lit[2] / 100u;
        lit_all = lit_all && lit_ok;
        same_all = same_all && same;
        std::printf("RESHADE case=lone_classic_depth distance_m=%.0f two_sided=%u vanilla_peak=%.4f vanilla_lit_px=%u "
                    "reshade_peak=%.4f reshade_lit_px=%u batch_peak=%.4f batch_lit_px=%u worst_rel_vs_batch=%.3f "
                    "px_off_batch=%u\n",
                    double(k.distance), unsigned(k.two_sided), double(peak[0]), lit[0], double(peak[1]), lit[1],
                    double(peak[2]), lit[2], double(worst), off);
    }
    report("reshade_lone_classic_matches_batch_at_three_depths", ok && lit_all && same_all, "");
    report("reshade_lone_classic_sequence_restores", ok && during_all && after_all, "");
    // the far flag outside the route at the far depth (motion_output_bullet_reshade_inc.h lone_native_flag: ONE / ONE,
    // the classic flag variant), RT2's lane base 1.0: RT2.g = 1 + 32 x max(rgb) where the bolt is lit, RT0 = the plain
    // re-shade's frame; after flag_unbind RT1 / RT2 unbound and the application's masks back
    float flag_g = -1.f, flag_want = 0.f, rt0_worst = 0.f;
    bool flag_restored = false;
    if (mrt && ok) {
        constexpr DWORD app_write1 = 5u, app_write2 = 9u;
        dev->SetRenderTarget(0, rt2_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 255, 255), 1.f, 0);
        dev->SetRenderTarget(0, rt_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE1, app_write1);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE2, app_write2);
        dev->BeginScene();
        pass_state(ps2s.p, D3DCULL_NONE);
        dev->SetTexture(0, tex.p);
        cf::bind_constants(dev, clip, tex_identity, fade, 0.f);
        dev->SetStreamSource(0, vb.p, 0, sizeof(cf::Vertex));
        auto fops = rf::flag_ops(dev, rt1_s.p, rt2_s.p);
        rf::FlagTargets targets;
        HRESULT bind_back = S_OK;
        ok = SUCCEEDED(rs::flag_bind(fops, targets, &bind_back)) && targets.bolt_flag &&
             rs::lone_native_flag(rs::lone_additive);
        auto ops = rf::device_ops(dev);
        rs::Plan plan;
        plan.program = ours_flag.p;
        plan.constants = constants;
        plan.layout = layout;
        rs::Applied applied;
        ok = ok && SUCCEEDED(rs::begin(ops, plan, app_block, applied));
        ok = ok && SUCCEEDED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22));
        const HRESULT back = rs::end(ops, applied, ps2s.p, app_block);
        const HRESULT flag_back = rs::flag_unbind(fops, targets);
        DWORD w1 = 0, w2 = 0;
        IDirect3DSurface9 *s1 = nullptr, *s2 = nullptr;
        flag_restored = SUCCEEDED(back) && SUCCEEDED(flag_back) && dev->GetRenderTarget(1, &s1) == D3DERR_NOTFOUND &&
                        dev->GetRenderTarget(2, &s2) == D3DERR_NOTFOUND &&
                        SUCCEEDED(dev->GetRenderState(D3DRS_COLORWRITEENABLE1, &w1)) && w1 == app_write1 &&
                        SUCCEEDED(dev->GetRenderState(D3DRS_COLORWRITEENABLE2, &w2)) && w2 == app_write2;
        if (s1) s1->Release();
        if (s2) s2->Release();
        dev->EndScene();
        std::vector<float> rt0, lane;
        ok = ok && read_image(rt_s.p, rt0) && read_image(rt2_s.p, lane);
        // img[1] holds the far depth's plain re-shade (the last depth, two-sided): RT0 must equal it; the lane at the
        // brightest re-shaded pixel
        std::size_t at = 0;
        float best = -1.f;
        for (std::size_t i = 0; ok && i < rt0.size(); i += 4) {
            const float m = std::max(img[1][i], std::max(img[1][i + 1], img[1][i + 2]));
            if (m > best) best = m, at = i;
            for (unsigned c = 0; c < 3; ++c)
                rt0_worst = std::max(rt0_worst, std::fabs(rt0[i + c] - img[1][i + c]) / (0.004f + 0.004f * std::fabs(img[1][i + c])));
        }
        if (ok) {
            flag_g = lane[at + 1];
            flag_want = 1.f + 32.f * best;
        }
        dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 15);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE2, 15);
    }
    const bool flag_ok = !mrt || (flag_g > 1.f && std::fabs(flag_g - flag_want) <= 0.01f * flag_want + 0.01f &&
                                  rt0_worst <= 1.f && flag_restored);
    std::printf("RESHADE case=lone_classic_native_flag mrt=%u distance_m=1500 base=1.0 rt2_g=%.3f want=%.3f "
                "rt0_worst_rel=%.3f restored=%u\n",
                unsigned(mrt), double(flag_g), double(flag_want), double(rt0_worst), unsigned(flag_restored));
    report("reshade_lone_classic_native_flag_one_one", ok && flag_ok, "");

    // (d) refusals on the device: a draw the verdict refuses (another pixel program: `program`; a body not in the map:
    // no match) is drawn vanilla, bit for bit the vanilla frame, with nothing of the sequence applied
    bool refused_ok = ok;
    std::vector<float> refused_img;
    const float refuse_distance = depths[2].distance;
    ok = ok && fill(refuse_distance);
    for (unsigned pass = 0; pass < 2 && ok; ++pass) {
        unsigned why = 0;
        const unsigned cls = pass == 0 ? recognise(rs::classic_vertex_shader, other_ps, true, D3DBLEND_ONE, D3DBLEND_ONE,
                                                   D3DBLENDOP_ADD, pal_body, true, &why)
                                       : recognise(rs::classic_vertex_shader, sl2s, true, D3DBLEND_ONE, D3DBLEND_ONE,
                                                   D3DBLENDOP_ADD, 0x9999u, true, &why);
        dev->SetRenderTarget(0, rt_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->BeginScene();
        pass_state(ps2s.p, D3DCULL_NONE);
        dev->SetTexture(0, tex.p);
        cf::bind_constants(dev, clip, tex_identity, fade, 0.f);
        dev->SetStreamSource(0, vb.p, 0, sizeof(cf::Vertex));
        if (cls != rs::lone_none) {
            refused_ok = false; // the verdict must refuse both
            rf::reshaded_draw(dev, ours.p, ps2s.p, constants, app_block, 22, &refused_ok, layout);
        } else
            ok = SUCCEEDED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22));
        IDirect3DPixelShader9* bound = nullptr;
        float block[12]{};
        const bool untouched = SUCCEEDED(dev->GetPixelShader(&bound)) && bound == ps2s.p &&
                               SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                               !std::memcmp(block, app_block, sizeof block);
        if (bound) bound->Release();
        dev->EndScene();
        ok = ok && read_image(rt_s.p, refused_img);
        // img[0] is the far depth's vanilla frame (two-sided, the same pass)
        const bool same = ok && refused_img.size() == img[0].size() &&
                          !std::memcmp(refused_img.data(), img[0].data(), img[0].size() * sizeof(float));
        refused_ok = refused_ok && cls == rs::lone_none && untouched && same &&
                     why == (pass == 0 ? unsigned(rs::classic_program) : unsigned(rs::classic_refused_count));
        std::printf("RESHADE case=lone_classic_refusal row=%s class=%u why=%s untouched=%u vanilla_bit_exact=%u\n",
                    pass == 0 ? "wrong_ps" : "wrong_body", cls,
                    why < rs::classic_refused_count ? rs::classic_refused_names[why] : "none", unsigned(untouched),
                    unsigned(same));
    }
    report("reshade_lone_classic_refusals_stay_vanilla", ok && refused_ok, "");

    // (e) cost: the substitution per draw (2000 draws of the stand-in bolt, fenced, best of 7) before (vanilla) and after
    // (re-shaded through the sequence, Z write normalised and put back); the recognition per blended classic draw: a
    // mapped body (the last of a 30-body map: the longest hit) and an unmapped one (every other blended classic draw
    // pays it), and an opaque draw (the blend test alone)
    LARGE_INTEGER freq{}, a{}, b{};
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
    double best_vanilla = 1e30, best_reshaded = 1e30, best_hit = 1e30, best_miss = 1e30, best_opaque = 1e30;
    bool restored = true;
    const DWORD zwrite_on[rs::lone_state_count] = {TRUE, 15u, FALSE, FALSE};
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->BeginScene();
        pass_state(ps2s.p, D3DCULL_NONE);
        dev->SetTexture(0, tex.p);
        cf::bind_constants(dev, clip, tex_identity, fade, 0.f);
        dev->SetStreamSource(0, vb.p, 0, sizeof(cf::Vertex));
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
        finish();
        QueryPerformanceCounter(&b);
        best_vanilla = std::min(best_vanilla, ticks_us(a, b, freq) / draws);
        auto ops = rf::device_ops(dev);
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            rs::Plan plan;
            plan.program = ours.p;
            plan.constants = constants;
            plan.layout = layout;
            plan.states = rs::state_zwrite;
            plan.state_app = zwrite_on;
            rs::Applied applied;
            const HRESULT hr = rs::begin(ops, plan, app_block, applied);
            if (SUCCEEDED(hr)) dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
            restored = restored && SUCCEEDED(hr) && SUCCEEDED(rs::end(ops, applied, ps2s.p, app_block));
        }
        finish();
        QueryPerformanceCounter(&b);
        best_reshaded = std::min(best_reshaded, ticks_us(a, b, freq) / draws);
        dev->EndScene();
    }
    {
        rs::BodyMap big;
        big.state = 1;
        big.count = 30;
        for (unsigned i = 0; i < 30; ++i) big.id[i] = 0x100u + 7u * i, big.entry[i] = 0;
        unsigned hits = 0;
        for (unsigned pass = 0; pass < 3; ++pass) {
            node[rs::node_body_offset / 4] = pass == 1 ? 0x9999u : big.id[29];
            const BOOL blend_on = pass == 2 ? FALSE : TRUE;
            for (unsigned run = 0; run < runs; ++run) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < draws; ++i) { // the production match (an opaque draw ends at blend_on)
                    const rs::ClassicMatch m = rs::classic_match(
                        rs::classic_vertex_shader, blend_on != FALSE, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, sl2s, true,
                        [&](std::uint32_t* out) {
                            return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) +
                                                                rs::node_body_offset,
                                                            out, sizeof *out);
                        },
                        [&](std::uint32_t id) { return rs::body_layout(big, &table, id) != nullptr; }, rs::pair_fl);
                    hits += m.blend != rs::lone_none;
                }
                QueryPerformanceCounter(&b);
                double& best = pass == 0 ? best_hit : pass == 1 ? best_miss : best_opaque;
                best = std::min(best, ticks_us(a, b, freq) / draws);
            }
        }
        ok = ok && hits == draws * runs;
    }
    device_guard("reshade_lone_classic_cost");
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    for (DWORD s = 0; s < 4; ++s) dev->SetTexture(s, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    std::printf("RESHADE case=lone_classic_cost draws=%u runs=%u draw_us_vanilla=%.3f draw_us_reshaded=%.3f "
                "substitution_us_per_draw=%.3f match_us_hit_30=%.4f match_us_miss_30=%.4f match_us_opaque=%.4f "
                "restored=%u fenced=%u\n",
                draws, runs, best_vanilla, best_reshaded, best_reshaded - best_vanilla, best_hit, best_miss, best_opaque,
                unsigned(restored), unsigned(fence.p != nullptr));
    report("reshade_lone_classic_cost_measured", ok && restored && fence.p && best_hit < 10.0 && best_miss < 10.0, "");
}

// The AP edition's lone classic path (docs/reverse-engineering/ap-bullet-batch-site.md section 2; RESHADE
// case=lone_classic_ap rows): the same standard_lighting DEFAULT VS bytes, AP's own standard_lighting / 2s DEFAULT PS
// (7c83ed50c9894e44 / e70adc744a38ca59), extracted at run time from the AP catalogues (run_weapon_effects.py
// CLASSIC_PROGRAMS_AP: ap_classic_vs.bin, ap_classic_ps.bin, ap_classic_ps2s.bin; untracked, each hash checked). (a) The
// production match under the AP row (bullet_reshade_core.h classic_match, pair_ap; this fixture builds without an
// edition define, so its node body id is AP's +0x140): Mayhem's one classic bullet body (row 25 PBC, v/00525) under ONE /
// ONE is admitted with either AP program, refused `program` with an FL one, the AP programs refused under the FL row,
// SRCALPHA / INVSRCALPHA refused `blend`. (b) The AP pair drawn behind the shared VS (vanilla, finite; lit printed, not
// gated: the fixture's PS constants are FL's layout) and our classic
// program in its place against the model (blend off, 16 x 16 FP16, two texels, two fades): the program reads only the
// VS's interpolators, so the same bytes serve both editions.
void lone_classic_ap_case() {
    namespace rs = x3m::weapon_effects::reshade;
    namespace cf = classic_fx;
    std::vector<DWORD> vs_code, ps_code, ps2s_code;
    const bool loaded = cf::load("ap_classic_vs.bin", vs_code) && cf::load("ap_classic_ps.bin", ps_code) &&
                        cf::load("ap_classic_ps2s.bin", ps2s_code);
    const std::uint64_t* ap = rs::classic_pixel_shader_pairs[rs::pair_ap];
    const std::uint64_t* fl = rs::classic_pixel_shader_pairs[rs::pair_fl];
    const bool hashes = loaded && cf::hash(vs_code) == rs::classic_vertex_shader && cf::hash(ps_code) == ap[0] &&
                        cf::hash(ps2s_code) == ap[1];
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps1, ps2s, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = hashes && SUCCEEDED(dev->CreateVertexShader(vs_code.data(), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps_code.data(), &ps1.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps2s_code.data(), &ps2s.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(cf::program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(cf::declaration(), &decl.p));
    // (a) recognition: a one-row TBullets stand-in naming v/00525, the scope node's body id at the build's offset
    constexpr std::uint32_t pbc_body = 525u;
    static wc::Table table{}; // zero-initialised, this case's alone
    table.rows = 1;
    table.bits[0] = wc::row_present;
    table.body_hash[0] = table.bullet_body_hash[0] = 0x00525u;
    table.bullet_bodies = 1;
    std::vector<unsigned char> tbullets(wc::layout::type_stride, 0);
    const std::uint16_t row_id = std::uint16_t(pbc_body);
    std::memcpy(&tbullets[rs::tbullets_body], &row_id, 2);
    const std::uint32_t table_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(tbullets.data())), count_slot = 1;
    const auto reader = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); };
    x3m::engine_memory::next_frame();
    rs::BodyMap map;
    const bool built = rs::build_body_map(map, &table, reinterpret_cast<std::uintptr_t>(&table_slot),
                                          reinterpret_cast<std::uintptr_t>(&count_slot), reader) &&
                       map.state == 1 && map.count == 1;
    std::vector<std::uint32_t> node(0x160 / 4, 0u);
    node[rs::node_body_offset / 4] = pbc_body;
    const auto verdict = [&](std::uint64_t ps, DWORD src, DWORD dst, unsigned pair, unsigned* why) {
        const rs::ClassicMatch m = rs::classic_match(
            rs::classic_vertex_shader, true, src, dst, D3DBLENDOP_ADD, ps, true,
            [&](std::uint32_t* out) {
                return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + rs::node_body_offset, out,
                                                sizeof *out);
            },
            [&](std::uint32_t b) { return rs::body_layout(map, &table, b) != nullptr; }, pair);
        *why = m.why;
        return m.blend;
    };
    constexpr unsigned none = rs::classic_refused_count;
    const struct {
        const char* name;
        std::uint64_t ps;
        DWORD src, dst;
        unsigned pair, want, why;
    } rows[] = {
        {"ap_one_sided_one_one", ap[0], D3DBLEND_ONE, D3DBLEND_ONE, rs::pair_ap, rs::lone_additive, none},
        {"ap_two_sided_one_one", ap[1], D3DBLEND_ONE, D3DBLEND_ONE, rs::pair_ap, rs::lone_additive, none},
        {"fl_ps_under_ap", fl[0], D3DBLEND_ONE, D3DBLEND_ONE, rs::pair_ap, rs::lone_none, rs::classic_program},
        {"ap_ps_under_fl", ap[1], D3DBLEND_ONE, D3DBLEND_ONE, rs::pair_fl, rs::lone_none, rs::classic_program},
        {"ap_alphablend", ap[0], D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, rs::pair_ap, rs::lone_none, rs::classic_blend},
    };
    bool recognition = built && rs::node_body_offset == 0x140u && rs::build_pair == rs::pair_ap;
    for (const auto& r : rows) {
        unsigned why = 0;
        const unsigned got = verdict(r.ps, r.src, r.dst, r.pair, &why);
        const bool ok = got == r.want && why == r.why;
        recognition = recognition && ok;
        std::printf("RESHADE case=lone_classic_ap_recognition row=%s class=%u want=%u why=%s ok=%u\n", r.name, got, r.want,
                    why < none ? rs::classic_refused_names[why] : "none", unsigned(ok));
    }

    // (b) the AP pair behind the VS (vanilla) and our program in its place against the model
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    float constants[4];
    rs::constants(tune, constants);
    const unsigned char two[8] = {99, 77, 17, 200, 200, 150, 40, 120};
    const unsigned char light_px[4] = {0, 0, 0, 64};
    Com<IDirect3DTexture9> tex2, light, rt16;
    Com<IDirect3DSurface9> rt16_s, sys16;
    bool ok = created;
    if (ok) {
        tex2.p = cf::texels(dev, two, 2);
        light.p = cf::texels(dev, light_px, 1);
        ok = tex2.p && light.p &&
             SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt16.p,
                                          nullptr)) &&
             SUCCEEDED(rt16.p->GetSurfaceLevel(0, &rt16_s.p)) &&
             SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys16.p, nullptr));
    }
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    float identity_clip[16]{};
    identity_clip[0] = identity_clip[5] = identity_clip[10] = identity_clip[15] = 1.f;
    const float tex_identity[8] = {1, 0, 0, 0, 0, 1, 0, 0}, tex_shift[8] = {1, 0, 0.5f, 0, 0, 1, 0, 0};
    const cf::Vertex quad[6] = {{-1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1}, {-1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},
                                {1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},   {-1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1},
                                {1, 1, 0.5f, 0.25f, 0.5f, 0, 0, -1},   {1, -1, 0.5f, 0.25f, 0.5f, 0, 0, -1}};
    struct ModelCase {
        const float* tex_matrix;
        unsigned texel;
        float fade;
        IDirect3DPixelShader9* game;
    };
    const ModelCase cases[2] = {{tex_identity, 0, 0.8f, ps1.p}, {tex_shift, 1, 0.35f, ps2s.p}};
    float worst_model = 0.f;
    bool vanilla_finite = true, vanilla_lit = true;
    if (ok) dev->SetRenderTarget(0, rt16_s.p);
    for (unsigned c = 0; c < 2 && ok; ++c) {
        const ModelCase& k = cases[c];
        for (unsigned which = 0; which < 2 && ok; ++which) { // 0 the AP program, 1 ours
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->BeginScene();
            dev->SetVertexShader(vs.p);
            dev->SetVertexDeclaration(decl.p);
            dev->SetPixelShader(which ? ours.p : k.game);
            for (DWORD s = 0; s < 4; ++s) {
                dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
                dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
            }
            dev->SetTexture(0, tex2.p);
            dev->SetTexture(1, nullptr);
            dev->SetTexture(2, light.p);
            dev->SetTexture(3, nullptr);
            dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); // the program's output itself
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
            dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            cf::bind_constants(dev, identity_clip, k.tex_matrix, k.fade, 0.f);
            float block[12];
            for (unsigned i = 0; i < 8; ++i) block[i] = rs::identity_layout[i];
            for (unsigned i = 0; i < 4; ++i) block[8 + i] = constants[i];
            if (which) dev->SetPixelShaderConstantF(rs::block_register, block, rs::block_count);
            ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(cf::Vertex)));
            dev->EndScene();
            ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt16_s.p, sys16.p));
            D3DLOCKED_RECT o{};
            ok = ok && SUCCEEDED(sys16.p->LockRect(&o, nullptr, D3DLOCK_READONLY));
            if (!ok) break;
            const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(o.pBits) + 8 * o.Pitch) + 8 * 4;
            const float got[4] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]), half_to_float(px[3])};
            sys16.p->UnlockRect();
            if (!which) {
                for (unsigned i = 0; i < 4; ++i) vanilla_finite = vanilla_finite && std::isfinite(got[i]);
                vanilla_lit = vanilla_lit && got[0] + got[1] + got[2] > 0.f; // informational: AP's lighting terms
                std::printf("RESHADE case=lone_classic_ap_vanilla index=%u program=%s got=%.4f,%.4f,%.4f,%.4f\n", c,
                            c ? "standard_lighting2s" : "standard_lighting", double(got[0]), double(got[1]),
                            double(got[2]), double(got[3]));
                continue;
            }
            const unsigned char* tb = two + 4 * k.texel;
            const float t[3] = {tb[0] / 255.f, tb[1] / 255.f, tb[2] / 255.f};
            float want[3];
            rs::model(constants, t, k.fade, want);
            for (unsigned i = 0; i < 3; ++i)
                worst_model = std::max(worst_model, std::fabs(got[i] - want[i]) / (0.004f + 0.004f * std::fabs(want[i])));
            std::printf("RESHADE case=lone_classic_ap_texel index=%u texel=%u fade=%.2f got=%.4f,%.4f,%.4f model=%.4f,%.4f,"
                        "%.4f\n",
                        c, k.texel, double(k.fade), double(got[0]), double(got[1]), double(got[2]), double(want[0]),
                        double(want[1]), double(want[2]));
        }
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    for (DWORD s = 0; s < 4; ++s) dev->SetTexture(s, nullptr);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    std::printf("RESHADE case=lone_classic_ap loaded=%u game_hashes=%u ps_dwords=%u ps2s_dwords=%u created=%u "
                "node_body=0x%x recognition=%u vanilla_finite=%u vanilla_lit=%u worst_rel=%.3f\n",
                unsigned(loaded), unsigned(hashes), unsigned(ps_code.size()), unsigned(ps2s_code.size()), unsigned(created),
                unsigned(rs::node_body_offset), unsigned(recognition), unsigned(vanilla_finite), unsigned(vanilla_lit),
                double(worst_model));
    report("reshade_lone_classic_ap_pair", ok && recognition && vanilla_finite && worst_model <= 1.f, "");
}
