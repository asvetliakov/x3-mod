#pragma once
// The hull hit re-shade's cases (docs/architecture/hull-hit-look.md section 4; HULL_RESHADE rows, hull_reshade_* checks),
// included by weapon_effects_fixture.cpp after its helpers (dev, report, compile, compile_shader, half_to_float, ticks_us,
// Com, device_guard). The parts are weapon_hull_reshade_fixture.h's: the game's standard_lighting DEFAULT pair (bytes
// extracted at run time, untracked) and the effects DEFAULT pair's stand-in, our two production programs through the
// production sequence, recognised by the production match (hull_reshade_core.h match, FL's table named explicitly: the
// fixture builds without an edition define). (a) programs; (b) both programs against the CPU model at 11 points each;
// (c) recognition and one claimant per draw against the lone classic, lone effects and muzzle decisions; (d) the
// sequence restores the game's program and the application's registers, and a failed step rolls back; (e) fade visible,
// cell edge and hot core on rendered quads; (f) cost.
#include "weapon_hull_reshade_fixture.h"

void hull_reshade_cases() {
    namespace hf = hull_fx;
    namespace hh = x3m::weapon_effects::hull_reshade;
    namespace rs = x3m::weapon_effects::reshade;
    namespace mz = x3m::weapon_effects::muzzle_reshade;
    namespace rf = reshade_fx;
    // (a)
    LPD3DXBUFFER vs_hold = nullptr, ps_hold = nullptr;
    const DWORD *vs_code = nullptr, *ps_code = nullptr;
    const bool compiled = compile_shader && compile(rf::lone_vs_source, "vs_2_0", &vs_code, &vs_hold) &&
                          compile(rf::lone_ps_source, "ps_2_0", &ps_code, &ps_hold);
    hf::Programs p;
    const bool created = compiled && hf::create(dev, vs_code, ps_code, p);
    if (vs_hold) vs_hold->Release();
    if (ps_hold) ps_hold->Release();
    std::printf("HULL_RESHADE case=programs loaded=%u game_hashes=%u created=%u effects_words=%u classic_words=%u "
                "effects_major=%u classic_major=%u bodies=%u\n",
                unsigned(p.loaded), unsigned(p.hashes), unsigned(created),
                unsigned(sizeof hf::effects_program / sizeof hf::effects_program[0]),
                unsigned(sizeof hf::classic_program / sizeof hf::classic_program[0]),
                unsigned((hf::effects_program[0] >> 8) & 0xffu), unsigned((hf::classic_program[0] >> 8) & 0xffu),
                hh::body_count(hh::edition_fl));
    report("hull_reshade_programs_create", created, "");
    if (!created) {
        p.release();
        return;
    }
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    const unsigned sparks = hh::body_index(10663u, hh::edition_fl), glow = hh::body_index(10658u, hh::edition_fl),
                   blue = hh::body_index(12000u, hh::edition_fl);
    float sparks_block[8], glow_block[8], blue_block[8];
    hh::constants(hh::bodies[sparks], tune, sparks_block);
    hh::constants(hh::bodies[glow], tune, glow_block);
    hh::constants(hh::bodies[blue], tune, blue_block);
    const float app[8] = {3.f, -1.f, 0.25f, 9.f, -4.f, 0.5f, 2.f, -0.75f};
    Com<IDirect3DTexture9> rt16, light;
    Com<IDirect3DSurface9> rt16_s, sys16;
    const unsigned char light_px[4] = {0, 0, 0, 64};
    light.p = hf::flat(dev, light_px);
    bool ok = light.p &&
              SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt16.p,
                                           nullptr)) &&
              SUCCEEDED(rt16.p->GetSurfaceLevel(0, &rt16_s.p)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys16.p, nullptr));
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    float identity_clip[16]{};
    identity_clip[0] = identity_clip[5] = identity_clip[10] = identity_clip[15] = 1.f;
    const float tex_identity[8] = {1, 0, 0, 0, 0, 1, 0, 0};
    // (b) the program's output itself (blend off) on a full-target quad of one UV, a flat texel: the model at 11 points
    // per variant, the cell envelope's centre, rim and edge, f 1 / 0.5 / 0.1, the glow lane (classic)
    struct Point {
        unsigned char t[4];
        float u, v, f, glow;
        const float* block;
    };
    const Point points[11] = {
        {{240, 235, 200, 255}, 0.125f, 0.125f, 1.f, 0.f, sparks_block}, // hot texel, cell centre
        {{110, 42, 5, 255}, 0.125f, 0.125f, 0.5f, 0.f, sparks_block},   // a spark point, half life (cooling)
        {{12, 5, 1, 255}, 0.13f, 0.11f, 1.f, 0.f, sparks_block},        // the dim dust: the fringe
        {{240, 235, 200, 255}, 0.2475f, 0.125f, 1.f, 0.f, sparks_block}, // the cell's edge (u)
        {{240, 235, 200, 255}, 0.23f, 0.235f, 0.5f, 0.f, sparks_block},  // the rim (u, v), half life
        {{240, 235, 200, 255}, 0.5025f, 0.75f, 0.1f, 0.f, sparks_block}, // just past a cell boundary, late
        {{248, 248, 248, 200}, 0.4f, 0.6f, 1.f, 0.f, glow_block},        // the glow's plateau: no envelope
        {{60, 60, 60, 120}, 0.99f, 0.01f, 0.5f, 1.f, glow_block},        // a haze texel, glow lane on
        {{0, 0, 0, 255}, 0.3f, 0.3f, 1.f, 0.f, glow_block},              // black
        {{200, 210, 255, 255}, 0.125f, 0.125f, 0.1f, 0.f, blue_block},   // the blue flipbook, late
        {{90, 100, 150, 180}, 0.0f, 0.24f, 0.5f, 0.f, blue_block},       // at a cell corner
    };
    float worst[2] = {0.f, 0.f}, worst_alpha[2] = {0.f, 0.f};
    if (ok) dev->SetRenderTarget(0, rt16_s.p);
    for (unsigned pair = 0; pair < 2 && ok; ++pair)
        for (unsigned k = 0; k < 11 && ok; ++k) {
            const Point& q = points[k];
            Com<IDirect3DTexture9> tex;
            tex.p = hf::flat(dev, q.t);
            hf::ClassicVertex v[6];
            const float c[3] = {0.f, 0.f, 0.5f};
            hf::quad(c, 1.f, 0.f, q.u, q.v, q.u, q.v, v);
            for (auto& x : v) x.z = 0.5f;
            rf::LoneVertex e[6];
            hf::effects_vertices(v, 6, e);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->BeginScene();
            hf::bind(dev, p, pair, false, identity_clip, tex_identity, q.f, q.glow, tex.p, light.p, D3DBLEND_ONE);
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            const UINT reg = pair == hh::pair_classic ? hh::classic_register : hh::effects_register;
            dev->SetPixelShaderConstantF(reg, q.block, hh::constant_count);
            dev->SetPixelShader(p.ours[pair]);
            ok = tex.p && (pair == hh::pair_classic
                               ? SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof v[0]))
                               : SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, e, sizeof e[0])));
            dev->EndScene();
            ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt16_s.p, sys16.p));
            D3DLOCKED_RECT o{};
            ok = ok && SUCCEEDED(sys16.p->LockRect(&o, nullptr, D3DLOCK_READONLY));
            if (!ok) break;
            const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(o.pBits) + 8 * o.Pitch) + 8 * 4;
            const float got[4] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]), half_to_float(px[3])};
            sys16.p->UnlockRect();
            const float t[4] = {q.t[0] / 255.f, q.t[1] / 255.f, q.t[2] / 255.f, q.t[3] / 255.f};
            float want[4];
            hh::model(q.block, t, q.u, q.v, q.f, pair == hh::pair_classic, light_px[3] / 255.f, q.glow, want);
            for (unsigned i = 0; i < 3; ++i)
                worst[pair] = std::max(worst[pair], std::fabs(got[i] - want[i]) / (0.004f + 0.004f * std::fabs(want[i])));
            worst_alpha[pair] = std::max(worst_alpha[pair], std::fabs(got[3] - want[3]) / (0.004f + 0.004f * want[3]));
            std::printf("HULL_RESHADE case=texel pair=%s index=%u uv=%.4f,%.4f f=%.2f got=%.4f,%.4f,%.4f,%.4f "
                        "model=%.4f,%.4f,%.4f,%.4f\n",
                        pair ? "classic" : "effects", k, double(q.u), double(q.v), double(q.f), double(got[0]),
                        double(got[1]), double(got[2]), double(got[3]), double(want[0]), double(want[1]), double(want[2]),
                        double(want[3]));
        }
    std::printf("HULL_RESHADE case=model effects_worst_rel=%.3f effects_alpha_rel=%.3f classic_worst_rel=%.3f "
                "classic_alpha_rel=%.3f\n",
                double(worst[0]), double(worst_alpha[0]), double(worst[1]), double(worst_alpha[1]));
    report("hull_reshade_program_matches_model", ok && worst[0] <= 1.f && worst[1] <= 1.f && worst_alpha[0] <= 1.f &&
                                                     worst_alpha[1] <= 1.f,
           "");
    // (c) recognition (FL's table) and one claimant: every listed body on its pair and blend is taken, refused on the
    // other pair, another blend, the wrong classic PS, blend off, unready or on AP; the muzzle bodies, a bullet-map body
    // and the death bodies are refused; for each draw the three routes' decisions (lone classic / lone effects, muzzle,
    // hull) admit at most one, exactly one for a body any of them lists.
    {
        constexpr std::uint64_t effects_vs = hh::effects_vertex_shader, classic_vs = hh::classic_vertex_shader;
        const std::uint64_t sl = rs::classic_pixel_shader_pairs[rs::pair_fl][0],
                            sl2s = rs::classic_pixel_shader_pairs[rs::pair_fl][1];
        constexpr std::uint32_t bullet_body = 1207u; // a TBullets body (the IRE bolt's id in the lone cases)
        std::vector<std::uint32_t> node(0x160 / 4, 0u);
        const auto body_id = [&](std::uint32_t* out) {
            return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + rs::node_body_offset, out,
                                            sizeof *out);
        };
        const auto mapped = [&](std::uint32_t id) { return id == bullet_body; };
        x3m::engine_memory::next_frame();
        const auto hull = [&](std::uint64_t vs, bool on, DWORD dst, std::uint64_t ps, std::uint32_t id, bool ready,
                              unsigned e) {
            node[rs::node_body_offset / 4] = id;
            return hh::match(vs, on, D3DBLEND_ONE, dst, D3DBLENDOP_ADD, ps, ready, ready, body_id, mapped, e);
        };
        unsigned taken = 0, refused_ok = 0, refused_rows = 0, multi = 0, none = 0;
        for (unsigned b = 0; b < hh::table_count; ++b) {
            const hh::Body& body = hh::bodies[b];
            const std::uint64_t vs = body.pair == hh::pair_classic ? classic_vs : effects_vs;
            const std::uint64_t other = body.pair == hh::pair_classic ? effects_vs : classic_vs;
            const DWORD dst = body.blend == hh::blend_screen ? D3DBLEND_INVSRCCOLOR : D3DBLEND_ONE;
            const DWORD wrong_dst = body.blend == hh::blend_screen ? D3DBLEND_ONE : D3DBLEND_INVSRCCOLOR;
            taken += hull(vs, true, dst, b % 2 ? sl : sl2s, body.id, true, hh::edition_fl).body == b;
            const bool refusals[6] = {
                hull(other, true, dst, sl, body.id, true, hh::edition_fl).body >= hh::table_count,
                hull(vs, true, wrong_dst, sl, body.id, true, hh::edition_fl).body >= hh::table_count,
                hull(vs, false, dst, sl, body.id, true, hh::edition_fl).body >= hh::table_count,
                hull(vs, true, dst, sl, body.id, false, hh::edition_fl).body >= hh::table_count,
                hull(vs, true, dst, sl, body.id, true, hh::edition_ap).body >= hh::table_count,
                body.pair != hh::pair_classic ||
                    hull(vs, true, dst, 0x7c83ed50c9894e44ull /* AP's PS */, body.id, true, hh::edition_fl).body >=
                        hh::table_count};
            for (const bool r : refusals) refused_ok += r, ++refused_rows;
        }
        std::vector<std::uint32_t> others = {bullet_body, 10656u, 10664u, 10670u, 333u};
        for (unsigned i = 0; i < mz::body_count; ++i) others.push_back(mz::bodies[i].id);
        for (const std::uint32_t id : others)
            for (const std::uint64_t vs : {effects_vs, classic_vs})
                for (const DWORD dst : {DWORD(D3DBLEND_ONE), DWORD(D3DBLEND_INVSRCCOLOR)}) {
                    refused_ok += hull(vs, true, dst, sl, id, true, hh::edition_fl).body >= hh::table_count;
                    ++refused_rows;
                }
        // one claimant: the classic VS under ONE / ONE (lone classic, muzzle, hull) and the effects VS under either blend
        // (lone effects, hull), for every listed id of the three routes and the death bodies
        std::vector<std::uint32_t> ids = others;
        for (unsigned b = 0; b < hh::table_count; ++b) ids.push_back(hh::bodies[b].id);
        for (const std::uint32_t id : ids) {
            node[rs::node_body_offset / 4] = id;
            const rs::ClassicMatch lone_classic = rs::classic_match(classic_vs, true, D3DBLEND_ONE, D3DBLEND_ONE,
                                                                    D3DBLENDOP_ADD, sl, true, body_id, mapped, rs::pair_fl);
            // the classic muzzle route claims its classic-pair rows only (an effects-pair row, 12009, is the muzzle
            // effects route's: weapon_muzzle_effects_cases.h checks that pair's claimants)
            const unsigned mzi = mz::body_index(id);
            const bool muzzle = mzi < mz::body_count && mz::bodies[mzi].pair == mz::pair_classic;
            const unsigned classic_claims = unsigned(lone_classic.blend != rs::lone_none) + unsigned(muzzle) +
                                            unsigned(hull(classic_vs, true, D3DBLEND_ONE, sl, id, true, hh::edition_fl).body <
                                                     hh::table_count);
            const bool lone_effects = mapped(id); // the lone effects route admits a mapped body under either blend
            const unsigned effects_claims =
                unsigned(lone_effects) + unsigned(hull(effects_vs, true, D3DBLEND_ONE, sl, id, true, hh::edition_fl).body <
                                                  hh::table_count);
            const bool listed = hh::body_index(id, hh::edition_fl) < hh::table_count;
            const bool classic_listed = listed && hh::bodies[hh::body_index(id, hh::edition_fl)].pair == hh::pair_classic &&
                                        hh::bodies[hh::body_index(id, hh::edition_fl)].blend == hh::blend_additive;
            const bool effects_listed = listed && hh::bodies[hh::body_index(id, hh::edition_fl)].pair == hh::pair_effects &&
                                        hh::bodies[hh::body_index(id, hh::edition_fl)].blend == hh::blend_additive;
            multi += classic_claims > 1 || effects_claims > 1;
            const bool want_classic = classic_listed || muzzle || id == bullet_body,
                       want_effects = effects_listed || id == bullet_body;
            none += (want_classic && classic_claims != 1) || (!want_classic && classic_claims != 0) ||
                    (want_effects && effects_claims != 1) || (!want_effects && effects_claims != 0);
        }
        std::printf("HULL_RESHADE case=recognition listed=%u taken=%u refused=%u/%u claim_ids=%u multiple_claimants=%u "
                    "claim_mismatches=%u\n",
                    hh::table_count, taken, refused_ok, refused_rows, unsigned(ids.size()), multi, none);
        report("hull_reshade_recognition_one_claimant",
               taken == hh::table_count && refused_ok == refused_rows && multi == 0 && none == 0, "");
    }
    // (d) the sequence on both pairs: during the draw our program and block are bound; after it the game's program and
    // the application's two registers (readback); a failed program step rolls the registers back
    bool restored_all = ok, during_all = ok, rollback_ok = ok;
    for (unsigned pair = 0; pair < 2 && ok; ++pair) {
        const UINT reg = pair == hh::pair_classic ? hh::classic_register : hh::effects_register;
        IDirect3DPixelShader9* game = pair == hh::pair_classic ? p.classic_ps[0] : p.effects_ps;
        dev->SetPixelShader(game);
        dev->SetPixelShaderConstantF(reg, app, hh::constant_count);
        bool restored = false, during = false;
        hf::reshaded_draw(
            dev, pair, p.ours[pair], game, sparks_block, app,
            [&] {
                IDirect3DPixelShader9* bound = nullptr;
                float now[8]{};
                dev->GetPixelShader(&bound);
                dev->GetPixelShaderConstantF(reg, now, hh::constant_count);
                during = bound == p.ours[pair] && !std::memcmp(now, sparks_block, sizeof now);
                if (bound) bound->Release();
                return S_OK;
            },
            &restored);
        IDirect3DPixelShader9* bound = nullptr;
        float now[8]{};
        dev->GetPixelShader(&bound);
        dev->GetPixelShaderConstantF(reg, now, hh::constant_count);
        restored_all = restored_all && restored && bound == game && !std::memcmp(now, app, sizeof now);
        during_all = during_all && during;
        if (bound) bound->Release();
        // a failing program step: begin fails and the registers are the application's again
        auto failing = mz::calls([this_dev = dev, reg](const float* v) {
                                     return this_dev->SetPixelShaderConstantF(reg, v, hh::constant_count);
                                 },
                                 [](IDirect3DPixelShader9*) { return E_FAIL; });
        mz::Applied applied;
        const HRESULT hr = mz::begin(failing, p.ours[pair], sparks_block, app, applied);
        dev->GetPixelShaderConstantF(reg, now, hh::constant_count);
        rollback_ok = rollback_ok && FAILED(hr) && !applied.program && !applied.constants && !applied.rollback_failed &&
                      !std::memcmp(now, app, sizeof now);
    }
    std::printf("HULL_RESHADE case=sequence restored=%u during=%u rollback=%u\n", unsigned(restored_all),
                unsigned(during_all), unsigned(rollback_ok));
    report("hull_reshade_sequence_restores", restored_all && during_all && rollback_ok, "");
    // (e) rendered quads (128 x 128 FP16, the game's ONE / ONE over black): the fade (the glow at f 1 and 0.1, standard
    // lighting pair: dark sum ratio <= 0.15, vanilla's reported), the cell edge (a uniform hot cell of the sparks' block:
    // the outermost pixel ring <= 0.05 of the cell's centre: D3D9's pixel centres sample q = 0 on one side and
    // q = 127 / 128 on the other, where the law's envelope is 0.030, hull_reshade_core.h smooth01) and the hot core (the sparks' hot texel at f 0.94 and the
    // glow's plateau at f 1 reach scene white, peak >= 1)
    Com<IDirect3DTexture9> rt, hot, plateau;
    Com<IDirect3DSurface9> rt_s, sys;
    const unsigned char hot_px[4] = {240, 235, 200, 255}, plateau_px[4] = {248, 248, 248, 255};
    hot.p = hf::flat(dev, hot_px);
    plateau.p = hf::flat(dev, plateau_px);
    ok = ok && hot.p && plateau.p &&
         SUCCEEDED(dev->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p,
                                      nullptr)) &&
         SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
         SUCCEEDED(dev->CreateOffscreenPlainSurface(128, 128, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
    std::vector<float> lum(128u * 128u);
    const auto render = [&](bool reshade, const float* block, IDirect3DBaseTexture9* tex, float f) {
        hf::ClassicVertex v[6];
        const float c[3] = {0.f, 0.f, 0.5f};
        hf::quad(c, 1.f, 0.f, 0.f, 0.f, 0.25f, 0.25f, v); // one cell, the full target
        for (auto& x : v) x.z = 0.5f;
        dev->SetRenderTarget(0, rt_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->BeginScene();
        hf::bind(dev, p, hh::pair_classic, false, identity_clip, tex_identity, f, 0.f, tex, light.p, D3DBLEND_ONE);
        bool restored = true;
        const HRESULT hr =
            reshade ? hf::reshaded_draw(
                          dev, hh::pair_classic, p.ours[hh::pair_classic], p.classic_ps[0], block, app,
                          [&] { return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof v[0]); }, &restored)
                    : dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof v[0]);
        dev->EndScene();
        if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(rt_s.p, sys.p))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        for (UINT y = 0; y < 128; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < 128; ++x)
                lum[y * 128 + x] = 0.2126f * half_to_float(row[x * 4]) + 0.7152f * half_to_float(row[x * 4 + 1]) +
                                   0.0722f * half_to_float(row[x * 4 + 2]);
        }
        sys.p->UnlockRect();
        return true;
    };
    const auto sum = [&] {
        double s = 0.0;
        for (const float l : lum) s += l;
        return s;
    };
    const auto peak = [&] {
        float m = 0.f;
        for (const float l : lum) m = std::max(m, l);
        return m;
    };
    double fade_sum[2][2] = {}; // [vanilla, reshade][f 1, f 0.1]
    for (unsigned shade = 0; shade < 2 && ok; ++shade)
        for (unsigned k = 0; k < 2 && ok; ++k) {
            ok = render(shade != 0, glow_block, plateau.p, k ? 0.1f : 1.f);
            fade_sum[shade][k] = sum();
        }
    const double fade_ratio = fade_sum[1][0] > 0.0 ? fade_sum[1][1] / fade_sum[1][0] : 1.0,
                 vanilla_fade_ratio = fade_sum[0][0] > 0.0 ? fade_sum[0][1] / fade_sum[0][0] : 1.0;
    float ring = 0.f, centre = 0.f, plain_ring = 0.f;
    if (ok) {
        ok = render(true, sparks_block, hot.p, 1.f);
        centre = lum[64 * 128 + 64];
        for (unsigned i = 0; i < 128; ++i)
            ring = std::max({ring, lum[i], lum[127 * 128 + i], lum[i * 128], lum[i * 128 + 127]});
        float no_cells[8];
        std::memcpy(no_cells, sparks_block, sizeof no_cells);
        no_cells[5] = 0.f; // the control: no envelope keeps the edge lit
        ok = ok && render(true, no_cells, hot.p, 1.f);
        for (unsigned i = 0; i < 128; ++i) plain_ring = std::max(plain_ring, lum[i * 128]);
    }
    float hot_sparks = 0.f, hot_glow = 0.f;
    if (ok) {
        ok = render(true, sparks_block, hot.p, 0.94f);
        hot_sparks = peak();
        ok = ok && render(true, glow_block, plateau.p, 1.f);
        hot_glow = peak();
    }
    std::printf("HULL_RESHADE case=frames fade_ratio=%.4f vanilla_fade_ratio=%.4f edge_ring=%.4f cell_centre=%.4f "
                "edge_ratio=%.4f law_edge_ratio=%.4f no_envelope_edge=%.4f hot_sparks_peak=%.4f hot_glow_peak=%.4f\n",
                fade_ratio, vanilla_fade_ratio, double(ring), double(centre), centre > 0.f ? double(ring / centre) : 1.0,
                double(1.f - hh::smooth01(0.85f, 1.f, 1.f - 2.f / 128.f)), double(plain_ring), double(hot_sparks),
                double(hot_glow));
    report("hull_reshade_fade_visible", ok && fade_ratio <= 0.15, "");
    report("hull_reshade_no_cell_edge", ok && centre > 0.f && ring <= 0.05f * centre && plain_ring > 0.5f * centre, "");
    report("hull_reshade_hot_core_scene_white", ok && hot_sparks >= 1.f && hot_glow >= 1.f, "");
    // (f) cost: the substitution per draw (2000 one-quad draws, fenced, best of 7) before and after, and the match per
    // blended candidate: the last listed body (the longest hit) and an unlisted one (every other candidate pays it)
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
    double best_vanilla = 1e30, best_reshaded = 1e30, best_hit = 1e30, best_miss = 1e30;
    bool restored = true;
    hf::ClassicVertex v[6];
    const float small_centre[3] = {0.f, 0.f, 0.5f};
    hf::quad(small_centre, 0.05f, 0.3f, 0.f, 0.f, 0.25f, 0.25f, v);
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->SetRenderTarget(0, rt_s.p);
        dev->BeginScene();
        hf::bind(dev, p, hh::pair_classic, false, identity_clip, tex_identity, 0.8f, 0.f, hot.p, light.p, D3DBLEND_ONE);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof v[0]);
        finish();
        QueryPerformanceCounter(&b);
        best_vanilla = std::min(best_vanilla, ticks_us(a, b, freq) / draws);
        auto ops = hf::device_ops(dev, hh::pair_classic);
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            mz::Applied applied;
            const HRESULT hr = mz::begin(ops, p.ours[hh::pair_classic], sparks_block, app, applied);
            if (SUCCEEDED(hr)) dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof v[0]);
            restored = restored && SUCCEEDED(hr) && SUCCEEDED(mz::end(ops, applied, p.classic_ps[0], app));
        }
        finish();
        QueryPerformanceCounter(&b);
        best_reshaded = std::min(best_reshaded, ticks_us(a, b, freq) / draws);
        dev->EndScene();
    }
    {
        std::vector<std::uint32_t> node(0x160 / 4, 0u);
        const std::uint64_t sl = rs::classic_pixel_shader_pairs[rs::pair_fl][0];
        unsigned hits = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            node[rs::node_body_offset / 4] = pass ? 0x9999u : hh::bodies[hh::table_count - 1].id;
            const std::uint64_t vs = hh::bodies[hh::table_count - 1].pair == hh::pair_classic ? hh::classic_vertex_shader
                                                                                              : hh::effects_vertex_shader;
            for (unsigned run = 0; run < runs; ++run) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < draws; ++i) {
                    const hh::Match m = hh::match(
                        vs, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, sl, true, true,
                        [&](std::uint32_t* out) {
                            return x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) +
                                                                rs::node_body_offset,
                                                            out, sizeof *out);
                        },
                        [](std::uint32_t) { return false; }, hh::edition_fl);
                    hits += m.body < hh::table_count;
                }
                QueryPerformanceCounter(&b);
                double& best = pass ? best_miss : best_hit;
                best = std::min(best, ticks_us(a, b, freq) / draws);
            }
        }
        ok = ok && hits == draws * runs;
    }
    device_guard("hull_reshade_cost");
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    for (DWORD s = 0; s < 4; ++s) dev->SetTexture(s, nullptr);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    std::printf("HULL_RESHADE case=cost draws=%u runs=%u draw_us_vanilla=%.3f draw_us_reshaded=%.3f "
                "substitution_us_per_draw=%.3f match_us_hit_last=%.4f match_us_miss=%.4f restored=%u fenced=%u\n",
                draws, runs, best_vanilla, best_reshaded, best_reshaded - best_vanilla, best_hit, best_miss,
                unsigned(restored), unsigned(fence.p != nullptr));
    report("hull_reshade_cost_measured", ok && restored && fence.p && best_reshaded - best_vanilla < 10.0 &&
                                             best_hit < 10.0 && best_miss < 10.0,
           "");
    p.release();
}
