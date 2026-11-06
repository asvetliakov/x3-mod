// The hull hit's D3D cases (docs/architecture/weapon-look.md 3.1 "Fixture gates", step 2), included by
// weapon_effects_fixture.cpp inside its helper namespace (its device, targets, lanes, fake owner and ring). Every case
// builds through the production stage::build_hull over the fixture's fake engine memory and draws through the
// production WeaponEffectsPass (the sprite and glow programs). Rows: `HULL case=... ` and `HULL_TIMING ...`.
// Never launches the game.
rr::WeaponSpriteVertex hq_sprites[rr::weapon_sprite_quads * 4];
rr::WeaponGlowVertex hq_glows[rr::weapon_glow_boxes * 4];
ws::HullState hq_state;
// The staged owner at view (0, 0, z) with radius r (identity camera and rows) and an empty ring and hull state.
void hull_owner(float z, float r) {
    stage_owner(z, r, 0);
    wc::ring_clear(stage_ring);
    ws::hull_clear(hq_state);
}
// One stage-owned hull hit on the staged owner (local engine position, damage, tier, variant bits, table).
wc::Event* add_hull_hit(std::uint32_t t0, const std::int32_t pos[3], std::int32_t damage,
                        std::uint8_t tier = wc::patch_fighter, std::uint8_t detail = 0,
                        std::uint8_t table = wc::table_bullets) {
    wc::Event* e = wc::ring_acquire(stage_ring);
    if (!e) return nullptr;
    const std::uint32_t seq = e->sequence;
    std::memset(e, 0, sizeof *e);
    e->sequence = seq;
    e->role = wc::role_hull_hit;
    e->style = table == wc::table_missiles ? std::uint8_t(wc::shape_missile_hit) : std::uint8_t(wc::shape_slug);
    e->flags = std::uint8_t(wc::flag_stage);
    e->table = table;
    e->tint_rgb = table == wc::table_missiles ? wc::missile_tint_rgb : (0xffffffu | wc::rgb_present);
    e->patch_tier = tier;
    e->tier_source = wc::tier_by_radius;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::hull_lifetime_ms);
    e->damage = damage;
    e->detail = detail;
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    for (unsigned k = 0; k < 3; ++k) e->pos[k] = pos[k];
    std::int32_t r = 0;
    std::memcpy(&r, fake_obj + wc::layout::obj_radius, 4);
    e->radius = r;
    return e;
}
// The calibre's damage: k = log10(d / 300) / 2.
std::int32_t damage_of(double k) {
    return std::int32_t(std::lround(300.0 * std::pow(10.0, 2.0 * k)));
}
ws::HullView hull_view(const Target& t, std::uint32_t now, std::uint32_t step) {
    ws::HullView v;
    v.cam = identity_camera();
    v.cam.m11 = 1.6f * float(t.w) / float(t.h);
    v.width = float(t.w);
    v.height = float(t.h);
    v.near_z = base_frame(g).near_z;
    v.now = now;
    v.step_ms = step;
    return v;
}
struct HullOut {
    ws::HullCounts n;
    unsigned quads = 0, boxes = 0;
};
HullOut hull_build(const ws::HullView& v, const ws::HullTuning& t = ws::HullTuning{}, ws::HullState* st = &hq_state,
                   wc::Ring* ring = &stage_ring) {
    HullOut o;
    o.n = ws::build_hull(*ring, *st, &fake_read, v, t, hq_sprites, rr::weapon_sprite_quads, hq_glows, rr::weapon_glow_boxes,
                         &o.quads, &o.boxes);
    return o;
}
rr::WeaponEffectsFrame hull_frame(Target& t, const HullOut& o) {
    rr::WeaponEffectsFrame f = base_frame(t);
    f.sprites = hq_sprites;
    f.sprite_quads = o.quads;
    f.glows = hq_glows;
    f.glow_boxes = o.boxes;
    return f;
}
std::pair<HRESULT, rr::WeaponEffectsReport> draw_on(rr::WeaponEffectsPass& pass, Target& t, const rr::WeaponEffectsFrame& f) {
    dev->BeginScene();
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(f, &r);
    dev->EndScene();
    return {hr, r};
}
std::vector<float> hull_render(rr::WeaponEffectsPass& pass, const HullOut& o, std::vector<float>* rgb = nullptr) {
    draw_on(pass, g, hull_frame(g, o));
    if (rgb) *rgb = read_rgb(g, g.scene_s.p);
    return read_luma(g, g.scene_s.p);
}
struct Centroid {
    double x = 0, y = 0, sum = 0;
    unsigned lit = 0;
};
Centroid centroid(const std::vector<float>& l, unsigned w, float threshold = 1e-3f) {
    Centroid c;
    for (std::size_t i = 0; i < l.size(); ++i)
        if (l[i] > threshold) {
            c.x += double(l[i]) * double(i % w);
            c.y += double(l[i]) * double(i / w);
            c.sum += double(l[i]);
            ++c.lit;
        }
    if (c.sum > 0) {
        c.x /= c.sum;
        c.y /= c.sum;
    }
    return c;
}
// Pixels per view unit at view depth z (x; the fixture's m00 1.6 at the target's width).
double px_per_unit(const Target& t, double z) {
    return 0.5 * double(t.w) * 1.6 / z;
}
// The head of spark quad q (pixels, view depth): its head corners' midpoint less the cap of half its width, through the
// vertex program's CPU twin (stage::streak_corners).
struct QuadPx {
    double hx, hy, tx, ty, hz, tz, width, length;
};
QuadPx quad_px(const Target& t, unsigned q) {
    const ws::StreakCorners sc = ws::streak_corners(hull_view(t, 0, 16), hq_sprites + 4 * q);
    double x[4], y[4];
    for (unsigned k = 0; k < 4; ++k) {
        x[k] = sc.x[k];
        y[k] = sc.y[k];
    }
    QuadPx o{};
    const double mhx = 0.5 * (x[2] + x[3]), mhy = 0.5 * (y[2] + y[3]), mtx = 0.5 * (x[0] + x[1]), mty = 0.5 * (y[0] + y[1]);
    o.width = std::hypot(x[2] - x[3], y[2] - y[3]);
    const double full = std::hypot(mhx - mtx, mhy - mty);
    const double dx = full > 0 ? (mhx - mtx) / full : 1.0, dy = full > 0 ? (mhy - mty) / full : 0.0;
    o.hx = mhx - dx * 0.5 * o.width;
    o.hy = mhy - dy * 0.5 * o.width;
    o.tx = mtx + dx * 0.5 * o.width;
    o.ty = mty + dy * 0.5 * o.width;
    o.length = full - o.width;
    o.hz = sc.z[2];
    o.tz = sc.z[0];
    return o;
}
bool is_flash(unsigned q) {
    return (hq_sprites[4 * q].colour >> 24) != 0;
}
void colour_of(std::uint32_t c, double out[3]) {
    out[0] = double((c >> 16) & 0xff) / 255.0;
    out[1] = double((c >> 8) & 0xff) / 255.0;
    out[2] = double(c & 0xff) / 255.0;
}
// An R32F lane (device z / w, the fog's law z = m32 / (d - m22)): a plane at view depth z over the screen.
IDirect3DPixelShader9* r32f_ps = nullptr;
bool r32f_lane(Target& t, Com<IDirect3DTexture9>& tex, Com<IDirect3DSurface9>& surf, float z, float m22, float m32) {
    if (!tex.p) {
        if (FAILED(dev->CreateTexture(t.w, t.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &tex.p, nullptr)) ||
            FAILED(tex.p->GetSurfaceLevel(0, &surf.p)))
            return false;
    }
    if (!r32f_ps) {
        const DWORD* w = nullptr;
        static Com<ID3DXBuffer> hold;
        if (!compile("float4 c0:register(c0); float4 main(float2 vp:VPOS):COLOR0{ return float4(c0.x,0,0,0); }", "ps_3_0",
                     &w, &hold.p) ||
            FAILED(dev->CreatePixelShader(w, &r32f_ps)))
            return false;
    }
    const float c0[4] = {m22 + m32 / z, 0.f, 0.f, 0.f};
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, surf.p);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(r32f_ps);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
    return true;
}
// One streak through the production resolve (resolve_core's configuration): a spark of rest length floor_len_px and width
// 3 px moving +x at speed_px per frame (drawn from the previous frame's head to the current one: the streak rule),
// radiance 3.6 (a spark at T 0.9), the lane 100 view units behind it; 16 frames on, then `after` frames without it.
// Returns the core ratio (the current frame's streak pixels at half its peak or more: resolved over current share, on -
// off) and *residual the largest resolved difference on - off `after` frames after the spark left.
double resolve_streak(rr::WeaponEffectsPass& pass, Com<IDirect3DTexture9>& motion, Com<IDirect3DSurface9>& motion_s,
                      float speed_px, unsigned after, float* peak_out, double* residual) {
    constexpr unsigned frames = 16;
    const float z = 300.f;
    std::vector<float> cur[2], res[2], late[2];
    for (unsigned on = 0; on < 2; ++on) {
        rr::TemporalPass taa;
        HRESULT hr = taa.initialize(dev, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr,
                                    nullptr, reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
        if (SUCCEEDED(hr)) hr = taa.configure_far();
        if (FAILED(hr)) return -1;
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames + after; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            draw_lane(g, z + 100.f, 1.f);
            const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
            dev->BeginScene();
            dev->SetRenderTarget(0, motion_s.p);
            dev->SetVertexDeclaration(quad_decl);
            dev->SetVertexShader(quad_vs);
            dev->SetPixelShader(motion_ps);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
            dev->SetRenderTarget(0, g.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
            dev->EndScene();
            StageContext ctx{&pass, base_frame(g), S_FALSE};
            ctx.frame.m20 = 2.f * jx / float(g.w);
            ctx.frame.m21 = -2.f * jy / float(g.h);
            ws::HullView hv = hull_view(g, 0, 16);
            hv.cam.m20 = ctx.frame.m20;
            hv.cam.m21 = ctx.frame.m21;
            // the head this frame, the tail where it was the previous frame (the vertex program draws the rest length
            // of floor_len_px when it moved less); 3 px wide at view z 300 (5.12 px per view unit at 1080p)
            const float head_x = float(g.w) * 0.5f + (float(n) - float(frames - 1)) * speed_px, tail_x = head_x - speed_px;
            const float head[3] = {(head_x * 2.f / float(g.w) - 1.f) / 1.6f * z, 0.f, z};
            const float tail[3] = {(tail_x * 2.f / float(g.w) - 1.f) / 1.6f * z, 0.f, z};
            const bool drawn = n < frames;
            ws::streak_vertices(head, tail, 3.6f, 1.2f, 3.f / 5.12f, ws::pack_colour(wc::look::heat_ramp[4], 0.f), hq_sprites);
            (void)hv;
            ctx.frame.sprites = hq_sprites;
            ctx.frame.sprite_quads = drawn ? 1 : 0;
            rr::FrameInputs in;
            in.color = g.scene_tex.p;
            in.current_depth = g.lane_tex.p;
            in.width = g.w;
            in.height = g.h;
            in.epoch = 1;
            const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            std::copy(identity, identity + 16, in.clip_to_previous);
            in.current_jitter[0] = jx;
            in.current_jitter[1] = jy;
            in.previous_jitter[0] = pjx;
            in.previous_jitter[1] = pjy;
            in.history_allowed = true;
            in.caller_scene_open = true;
            in.caller_queries_idle = true;
            in.motion = motion.p;
            in.motion_policy = rr::MotionPolicy::PerPixel;
            in.reactive_policy = rr::ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera = true;
            in.sentinel_strict_sky = true;
            in.sky_history_exit_px = .25f;
            in.far_weight = .985f;
            in.far_d0 = .9995f;
            in.far_inv = 1.f / (.9999f - .9995f);
            in.far_speed_lo = x3::temporal::kFarSpeedLo;
            in.far_speed_hi = x3::temporal::kFarSpeedHi;
            in.thin_region_weight = .97f;
            in.thin_region_relax = 1;
            in.thin_region_camera_gate = true;
            if (on) {
                in.stage_callback = &stage_callback;
                in.stage_context = &ctx;
            }
            dev->BeginScene();
            hr = taa.run(in, &o);
            dev->EndScene();
            if (FAILED(hr) || (on && (!taa.diagnostics().stage_ran || FAILED(ctx.result)))) return -1;
            pjx = jx;
            pjy = jy;
            if (n + 1 == frames || n + 1 == frames + after) {
                Com<IDirect3DSurface9> resolved;
                if (!o.color || FAILED(o.color->GetSurfaceLevel(0, &resolved.p))) return -1;
                if (n + 1 == frames) {
                    cur[on] = read_luma(g, g.scene_s.p);
                    res[on] = read_luma(g, resolved.p);
                } else
                    late[on] = read_luma(g, resolved.p);
            }
        }
        taa.shutdown();
    }
    if (cur[0].empty() || cur[1].empty() || res[0].empty() || res[1].empty()) return -1;
    float peak = 0.f;
    for (std::size_t i = 0; i < cur[1].size(); ++i) peak = std::max(peak, cur[1][i] - cur[0][i]);
    double num = 0, den = 0;
    for (std::size_t i = 0; i < cur[1].size(); ++i) {
        const double c = double(cur[1][i]) - double(cur[0][i]);
        if (c >= .5 * double(peak)) {
            num += double(res[1][i]) - double(res[0][i]);
            den += c;
        }
    }
    *residual = 0;
    if (!late[0].empty() && !late[1].empty())
        for (std::size_t i = 0; i < late[1].size(); ++i) *residual = std::max(*residual, double(late[1][i]) - double(late[0][i]));
    *peak_out = peak;
    return den > 0 ? num / den : 0;
}

void hull_cases(rr::WeaponEffectsPass& pass, D3DPRESENT_PARAMETERS& pp) {
    namespace hl = wc::hull;
    namespace lk = wc::look;
    char detail[256];
    std::printf("HULL_SLOTS sprite_vs=%u sprite_ps=%u glow_vs=%u glow_ps=%u sprite_vertex_bytes=%u glow_vertex_bytes=%u "
                "hull_state_bytes=%u\n",
                pass.caps().sprite_vs_slots, pass.caps().sprite_ps_slots, pass.caps().glow_vs_slots,
                pass.caps().glow_ps_slots, unsigned(sizeof(rr::WeaponSpriteVertex)), unsigned(sizeof(rr::WeaponGlowVertex)),
                unsigned(sizeof(ws::HullState)));
    report("hull_programs_attached", pass.caps().hull_enabled && !std::strcmp(pass.caps().hull_reason, "ok") &&
                                         pass.caps().sprite_vs_slots && pass.caps().sprite_ps_slots &&
                                         pass.caps().glow_vs_slots && pass.caps().glow_ps_slots, "");
    const std::int32_t front[3] = {0, 0, -10000}; // the staged owner's front (r 100 view units), at view z 300
    const float z_hit = 300.f;
    const double ppu = px_per_unit(g, z_hit);
    const double cx = 0.5 * g.w, cy = 0.5 * g.h;
    ws::HullTuning only_flash, only_sparks, only_glow, only_ember;
    only_flash.sparks = only_flash.glow = only_flash.ember = false;
    only_sparks.flash = only_sparks.glow = only_sparks.ember = false;
    only_glow.flash = only_glow.sparks = only_glow.ember = false;
    only_ember.flash = only_ember.sparks = only_ember.glow = false;
    const float U = ws::metres_to_view(wc::cell_m[wc::patch_fighter]);

    // 1. Placement: the sparks' first-frame centroid at the recorded hit point (within 0.25 U) and the flash's lit
    // centroid at the projected hit (within 0.1 r_hit), on the sky lane.
    {
        draw_lane(g, 0.f, -1.f);
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        const float r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * 0.33f);
        const HullOut sp = hull_build(hull_view(g, 1000, 16), only_sparks);
        const Centroid cs = centroid(hull_render(pass, sp), g.w);
        double qx = 0, qy = 0;
        for (unsigned q = 0; q < sp.quads; ++q) {
            const QuadPx p = quad_px(g, q);
            qx += 0.5 * (p.hx + p.tx);
            qy += 0.5 * (p.hy + p.ty);
        }
        if (sp.quads) {
            qx /= sp.quads;
            qy /= sp.quads;
        }
        ws::hull_clear(hq_state);
        const HullOut fl = hull_build(hull_view(g, 1000, 16), only_flash);
        const Centroid cf = centroid(hull_render(pass, fl), g.w);
        const double spark_err_u = std::hypot(cs.x - cx, cs.y - cy) / ppu / U;
        const double quad_err_u = std::hypot(qx - cx, qy - cy) / ppu / U;
        const double flash_err = std::hypot(cf.x - cx, cf.y - cy) / ppu / r_hit;
        std::printf("HULL case=placement sparks=%u spark_lit=%u spark_centroid_err_u=%.4f quad_centroid_err_u=%.4f flashes=%u "
                    "flash_lit=%u flash_centroid_err_rhit=%.4f\n",
                    sp.n.sparks, cs.lit, spark_err_u, quad_err_u, fl.n.flashes, cf.lit, flash_err);
        report("hull_sparks_born_at_recorded_pos", sp.n.sparks > 0 && cs.lit > 0 && spark_err_u <= 0.25 && quad_err_u <= 0.25, "");
        report("hull_flash_centroid_at_projected_hit", fl.n.flashes == 1 && cf.lit > 0 && flash_err <= 0.1, "");
    }
    // 2. The lane hides the inward half: sparks 100 ms in on a flat hull facing the eye against the sky lane, by radiance
    // (the sum of the streaks' pixels; the lit-pixel ratio is reported beside it: on the sky a pair whose two sparks run
    // near the view axis overlaps on screen and counts its pixels once, so that ratio reads above one half), over eight
    // records per calibre (eight rotations: a pair that grazes the plane is hidden on both sides by the soft term, so
    // one record's share depends on its rotation; seq 438 at k 0.5 has two such pairs and reads 0.425).
    {
        double share[3] = {0, 0, 0}, px_share[3] = {0, 0, 0}, cpu_share[3] = {0, 0, 0};
        std::uint32_t seqs[3] = {0, 0, 0};
        unsigned crossing[3] = {0, 0, 0};
        unsigned lit_sky[3] = {0, 0, 0}, lit_hull[3] = {0, 0, 0};
        const double ks[3] = {0.0, 0.5, 1.0};
        for (unsigned c = 0; c < 3; ++c) {
            hull_owner(400.f, 100.f);
            for (unsigned r = 0; r < 8; ++r) {
                const std::int32_t at[3] = {std::int32_t(4000.0 * std::cos(0.785398 * r)),
                                            std::int32_t(2500.0 * std::sin(0.785398 * r)), -10000};
                const std::uint32_t sq = add_hull_hit(1000, at, damage_of(ks[c]))->sequence;
                if (!r) seqs[c] = sq;
            }
            hull_build(hull_view(g, 1000, 16), only_sparks);
            const HullOut o = hull_build(hull_view(g, 1100, 16), only_sparks);
            // The CPU twin's prediction: each streak's energy (radiance x length x width) in front of the plane or behind.
            double in_front = 0, behind = 0;
            for (unsigned q = 0; q < o.quads; ++q) {
                const ws::StreakCorners sk = ws::streak_corners(hull_view(g, 1100, 16), hq_sprites + 4 * q);
                const double energy = double(sk.radiance) * std::hypot(sk.x[2] - sk.x[0], sk.y[2] - sk.y[0]) * double(sk.lit);
                if (sk.z[0] < z_hit && sk.z[2] < z_hit) in_front += energy;
                else if (sk.z[0] > z_hit && sk.z[2] > z_hit) behind += energy;
                else ++crossing[c];
            }
            cpu_share[c] = in_front + behind > 0 ? in_front / (in_front + behind) : 0.0;
            draw_lane(g, 0.f, -1.f);
            const std::vector<float> sky = hull_render(pass, o);
            draw_lane(g, z_hit, 1.f);
            const std::vector<float> on = hull_render(pass, o);
            lit_sky[c] = lit_count(sky);
            lit_hull[c] = lit_count(on);
            px_share[c] = lit_sky[c] ? double(lit_hull[c]) / double(lit_sky[c]) : 0.0;
            share[c] = total(sky) > 0 ? total(on) / total(sky) : 0.0;
        }
        std::printf("HULL case=half_hidden k0_share=%.4f k05_share=%.4f k1_share=%.4f k0_px=%u/%u k0_px_share=%.4f "
                    "k05_px=%u/%u k05_px_share=%.4f k1_px=%u/%u k1_px_share=%.4f cpu_share=%.4f,%.4f,%.4f crossing=%u,%u,%u "
                    "seq=%u,%u,%u\n",
                    share[0], share[1], share[2], lit_hull[0], lit_sky[0], px_share[0], lit_hull[1], lit_sky[1], px_share[1],
                    lit_hull[2], lit_sky[2], px_share[2], cpu_share[0], cpu_share[1], cpu_share[2], crossing[0], crossing[1],
                    crossing[2], seqs[0], seqs[1], seqs[2]);
        bool ok = true;
        for (double s : share) ok = ok && s >= 0.45 && s <= 0.55;
        report("hull_half_of_sparks_hidden_by_flat_lane", ok, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 3. The glow on the hull, never on sky: half the screen hull (x < W / 2) with the light at its edge; then the full
    // plane: its centroid at the projected light (within 0.3 R).
    {
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        const HullOut o = hull_build(hull_view(g, 1000, 16), only_glow);
        draw_lane(g, z_hit, 0.5f);
        const std::vector<float> half = hull_render(pass, o);
        unsigned on_sky = 0, on_lane = 0;
        for (std::size_t i = 0; i < half.size(); ++i)
            if (half[i] > 1e-4f) ++((i % g.w) < g.w / 2 ? on_lane : on_sky);
        draw_lane(g, z_hit, 1.f);
        const Centroid c = centroid(hull_render(pass, o), g.w, 1e-4f);
        const float R = hl::glow_r * hl::r_scale * U * (hl::r_base + hl::r_k * 0.33f);
        const double err = std::hypot(c.x - cx, c.y - cy) / ppu / R;
        std::printf("HULL case=glow_surface boxes=%u lit_sky=%u lit_lane=%u on_lane_share=%.4f full_lit=%u centroid_err_R=%.4f "
                    "R_view=%.3f\n",
                    o.boxes, on_sky, on_lane, on_lane + on_sky ? double(on_lane) / double(on_lane + on_sky) : 0.0, c.lit, err,
                    double(R));
        report("hull_light_on_hull_not_sky", o.boxes == 1 && on_sky == 0 && on_lane > 0 && err <= 0.3, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 4. The glow's peak and decay: at its light point heat(glow_t) x glow_e x glow_t at age 0, half at half its life,
    // nothing at its end (its life from the production hash, 150 ms x (1 +- stagger / 2)).
    {
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(0.33));
        bool fl = false;
        const float life = ws::element_life_ms(float(hl::glow_ms), ws::element_hash(e->sequence, 1), wc::stagger, 48.f, &fl);
        draw_lane(g, z_hit, 1.f);
        float at[3];
        const std::uint32_t ages[3] = {0, std::uint32_t(life * 0.5f), std::uint32_t(life) + 1};
        for (unsigned a = 0; a < 3; ++a) {
            const HullOut o = hull_build(hull_view(g, 1000 + ages[a], 16), only_glow);
            at[a] = centre_luma(hull_render(pass, o));
        }
        float heat[3];
        wc::heat_colour(hl::glow_t, heat);
        const float peak = std::max(heat[0], std::max(heat[1], heat[2])) * hl::glow_e * hl::glow_t;
        const float half_expect = peak * (1.f - float(ages[1]) / life);
        std::printf("HULL case=glow_decay life_ms=%.1f age0=%.4f expected0=%.4f age_half=%.4f expected_half=%.4f age_end=%.5f\n",
                    double(life), double(at[0]), double(peak), double(at[1]), double(half_expect), double(at[2]));
        report("hull_light_peak_decay_150ms", std::fabs(at[0] - peak) <= 0.01f * peak + 2e-3f &&
                                                   std::fabs(at[1] - half_expect) <= 0.02f * peak + 2e-3f && at[2] == 0.f &&
                                                   life >= 0.8f * hl::glow_ms && life <= 1.2f * hl::glow_ms, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 5. The sparks' cooling: their colour on the heat ramp at 0 / 0.5 / ~1 of a life (vertex colours, 8-bit), and the
    // drawn hue at half life (g / r of the brightest pixel).
    {
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(0.0));
        const std::uint32_t h = ws::element_hash(e->sequence, 16u);
        bool fl = false;
        const float life = ws::element_life_ms(float(hl::spark_life_ms), ws::mix32(h), wc::stagger, 48.f, &fl);
        double worst = 0;
        double hue_ratio = -1;
        const float fracs[3] = {0.f, 0.5f, 0.95f};
        for (unsigned a = 0; a < 3; ++a) {
            const std::uint32_t age = std::uint32_t(life * fracs[a]);
            const HullOut o = hull_build(hull_view(g, 1000 + age, 16), only_sparks);
            if (!o.quads) {
                worst = 1;
                continue;
            }
            float want[3];
            wc::heat_colour(1.f - float(age) / life, want);
            double got[3];
            colour_of(hq_sprites[0].colour, got);
            for (unsigned k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(got[k] - double(want[k])));
            if (a == 1) {
                draw_lane(g, 0.f, -1.f);
                std::vector<float> rgb;
                HullOut one = o; // the first spark alone (each pair cools on its own life)
                one.quads = 1;
                one.boxes = 0;
                hull_render(pass, one, &rgb);
                std::size_t best = 0;
                for (std::size_t i = 0; i < rgb.size() / 3; ++i)
                    if (rgb[i * 3] > rgb[best * 3]) best = i;
                hue_ratio = rgb[best * 3] > 0 ? double(rgb[best * 3 + 1]) / double(rgb[best * 3]) : -1;
                const float T = 1.f - float(age) / life;
                wc::heat_colour(T, want);
                std::snprintf(detail, sizeof detail, "want_g_over_r=%.4f", double(want[1] / want[0]));
                std::printf("HULL case=cooling life_ms=%.1f half_T=%.3f drawn_g_over_r=%.4f %s\n", double(life), double(T),
                            hue_ratio, detail);
                report("hull_spark_cooling_hue_half_life", std::fabs(hue_ratio - double(want[1] / want[0])) <= 0.03, detail);
            }
        }
        std::printf("HULL case=cooling_ramp worst_channel_error=%.4f\n", worst);
        report("hull_spark_cooling_ramp", worst <= 2.5 / 255.0, "");
    }
    // 6. Sizes, counts, reach and lives per tier at k 0 / 0.5 / 1 against the law (5 %): spark count round(8 + 16 k),
    // flash radius r_hit, glow R 3 r_hit, ember R 0.6 r_hit, spark width 0.04 U; the sparks' mean displacement at 200 ms
    // against v0 tau (1 - e^(-t / tau)) over their own speeds; the reach law's asymptote 2.7 U (20 %); the sparks all live at
    // 0.8 x their law and none past 1.2 x.
    {
        bool sizes_ok = true, reach_ok = true, lives_ok = true;
        const double asymptote_u = double(hl::spark_speed_u * hl::spark_tau_s);
        for (unsigned tier = 0; tier < 3; ++tier)
            for (double k : {0.0, 0.5, 1.0}) {
                const float Ut = ws::metres_to_view(wc::cell_m[tier]);
                const float z_owner = 400.f + 40.f * Ut; // the whole spray in front of the eye
                hull_owner(z_owner, 100.f);
                const wc::Event* e = add_hull_hit(1000, front, damage_of(k), std::uint8_t(tier));
                const std::uint32_t seq = e->sequence;
                const float kf = wc::calibre_of(e->damage);
                const float r_hit = hl::r_scale * Ut * (hl::r_base + hl::r_k * kf);
                const HullOut o = hull_build(hull_view(g, 1000, 16));
                const unsigned want_n = unsigned(float(hl::spark_n0) + float(hl::spark_nk) * kf + 0.5f);
                float flash_r = 0, glow_R = 0, ember_R = 0, width = 0;
                for (unsigned q = 0; q < o.quads; ++q)
                    if (is_flash(q))
                        flash_r = hq_sprites[4 * q].b[3];
                    else
                        width = hq_sprites[4 * q].b[3];
                for (unsigned b = 0; b < o.boxes; ++b) {
                    const float R = hq_glows[4 * b].light[3];
                    if (R > 2.f * r_hit) glow_R = R;
                    else ember_R = R;
                }
                const bool s_ok = o.n.sparks == want_n && o.n.flashes == 1 && std::fabs(flash_r - r_hit) <= 0.05f * r_hit &&
                                  std::fabs(glow_R - hl::glow_r * r_hit) <= 0.01f * glow_R &&
                                  std::fabs(ember_R - hl::ember_r * r_hit) <= 0.01f * ember_R &&
                                  std::fabs(width - hl::spark_width_u * Ut) <= 0.01f * width;
                // The reach at 200 ms: each spark's head against its law.
                const HullOut o2 = hull_build(hull_view(g, 1200, 16), only_sparks);
                const double z_hit_t = double(z_owner) - 100.0;
                double got = 0, want = 0;
                unsigned m = 0;
                for (unsigned q = 0; q < o2.quads; ++q) {
                    const QuadPx p = quad_px(g, q);
                    const double vx = (p.hx / double(g.w) * 2.0 - 1.0) / 1.6 * p.hz;
                    const double vy = (1.0 - p.hy / double(g.h) * 2.0) / (1.6 * double(g.w) / double(g.h)) * p.hz;
                    got += std::sqrt(vx * vx + vy * vy + (p.hz - z_hit_t) * (p.hz - z_hit_t));
                    ++m;
                }
                const unsigned pairs = (want_n + 1) / 2;
                unsigned counted = 0;
                for (unsigned j = 0; j < pairs; ++j) {
                    const std::uint32_t h = ws::element_hash(seq, 16u + j);
                    const double speed = double(hl::spark_speed_u * Ut) *
                                         (1.0 + double(hl::spark_speed_spread) * (2.0 * double(ws::unit_of(h)) - 1.0));
                    bool fl = false;
                    const float life = ws::element_life_ms(float(hl::spark_life_ms) + float(hl::spark_life_k_ms) * kf,
                                                           ws::mix32(h), wc::stagger, 48.f, &fl);
                    if (!(200.f < life)) continue;
                    const unsigned sides = std::min(2u, want_n - 2 * j);
                    want += sides * speed * double(hl::spark_tau_s) * (1.0 - std::exp(-0.2 / double(hl::spark_tau_s)));
                    counted += sides;
                }
                const bool r_ok = m == counted && m > 0 && std::fabs(got - want) <= 0.05 * want;
                // Lives: every spark at 0.8 x its law, none past 1.2 x.
                const float law = float(hl::spark_life_ms) + float(hl::spark_life_k_ms) * kf;
                const HullOut early = hull_build(hull_view(g, 1000 + std::uint32_t(0.8f * law) - 1, 16), only_sparks);
                const HullOut late = hull_build(hull_view(g, 1000 + std::uint32_t(1.2f * law) + 1, 16), only_sparks);
                const bool l_ok = early.n.sparks == want_n && late.n.sparks == 0;
                std::printf("HULL case=law tier=%s k=%.2f calibre=%.4f sparks=%u want=%u flash_r=%.3f r_hit=%.3f glow_R=%.3f "
                            "ember_R=%.3f width=%.4f reach_mean_u=%.4f law_mean_u=%.4f early=%u late=%u\n",
                            wc::patch_tier_name(tier), k, double(kf), o.n.sparks, want_n, double(flash_r), double(r_hit),
                            double(glow_R), double(ember_R), double(width), m ? got / m / Ut : 0.0,
                            counted ? want / counted / Ut : 0.0, early.n.sparks, late.n.sparks);
                sizes_ok = sizes_ok && s_ok;
                reach_ok = reach_ok && r_ok;
                lives_ok = lives_ok && l_ok;
            }
        std::printf("HULL case=reach_asymptote v0_tau_u=%.3f\n", asymptote_u);
        report("hull_sizes_counts_per_tier_law", sizes_ok, "");
        report("hull_spark_reach_law_200ms", reach_ok, "");
        report("hull_spark_reach_asymptote_2_7u", std::fabs(asymptote_u - 2.7) <= 0.54, "");
        report("hull_spark_lives_law", lives_ok, "");
    }
    // 7. The pixel floors at 2 km: every spark quad at least 3 px wide and 24 px long, the flash's core 3 px across.
    {
        hull_owner(10100.f, 100.f); // the hit 2 km away
        add_hull_hit(1000, front, damage_of(0.33));
        hull_build(hull_view(g, 1000, 16));
        const HullOut o = hull_build(hull_view(g, 1050, 16));
        double min_w = 1e9, min_l = 1e9, core_px = 0;
        for (unsigned q = 0; q < o.quads; ++q) {
            if (is_flash(q)) {
                const QuadPx p = quad_px(g, q);
                (void)p;
                const double halo = std::fabs(double(hq_sprites[4 * q + 1].a[0] - hq_sprites[4 * q].a[0]) /
                                              double(hq_sprites[4 * q].a[3])) * 0.25 * double(g.w);
                core_px = 2.0 * halo * double(hq_sprites[4 * q].colour >> 24) / 255.0;
                continue;
            }
            const QuadPx p = quad_px(g, q);
            min_w = std::min(min_w, p.width);
            min_l = std::min(min_l, p.length);
        }
        std::printf("HULL case=floors_2km sparks=%u min_width_px=%.3f min_length_px=%.3f flash_core_px=%.3f\n", o.n.sparks,
                    min_w, min_l, core_px);
        report("hull_streak_min_footprint_3px_24px", o.n.sparks > 0 && min_w >= 3.0 - 1e-3 && min_l >= 24.0 - 1e-2 &&
                                                         core_px >= 3.0 - 0.02, "");
    }
    // 8. The three-frame floor: a 60 ms flash at SETA 6 (game step 100 ms) and SETA 10 (167 ms) drawn on 3 frames from
    // its first; the ember's frames at SETA 6 against its life (the wall-time law: life / 6).
    {
        unsigned frames[2] = {0, 0};
        const std::uint32_t steps[2] = {100, 167};
        for (unsigned s = 0; s < 2; ++s) {
            hull_owner(400.f, 100.f);
            add_hull_hit(1000, front, damage_of(0.33));
            for (unsigned f = 0; f < 12; ++f) frames[s] += hull_build(hull_view(g, 1000 + f * steps[s], steps[s]), only_flash).n.flashes;
        }
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(0.33));
        bool fl = false;
        const float ember_life = ws::element_life_ms(float(hl::ember_ms), ws::element_hash(e->sequence, 2), wc::stagger, 300.f, &fl);
        unsigned ember_frames = 0, last = 0;
        for (unsigned f = 0; f < 40; ++f)
            if (hull_build(hull_view(g, 1000 + f * 100, 100), only_ember).n.embers) {
                ++ember_frames;
                last = f;
            }
        const double wall_ms = double(ember_frames) * 100.0 / 6.0, expected_wall = double(ember_life) / 6.0;
        std::printf("HULL case=min_frames seta6_flash_frames=%u seta10_flash_frames=%u ember_life_ms=%.1f ember_frames=%u "
                    "ember_last_age_ms=%u seta6_wall_ms=%.2f expected_wall_ms=%.2f\n",
                    frames[0], frames[1], double(ember_life), ember_frames, last * 100, wall_ms, expected_wall);
        report("hull_min_frames_under_seta6", frames[0] == 3, "");
        report("hull_min_frames_under_seta10", frames[1] == 3, "");
        report("hull_seta6_wall_time_law", float(last * 100) < ember_life && float(last * 100 + 100) >= ember_life &&
                                               std::fabs(wall_ms - expected_wall) <= 100.0 / 6.0, "");
    }
    // 9. No pop at 12 hits per game second on a flat face (60 frames of 16 ms): each frame's image never darker than the
    // previous frame's elements aged one step (the same build without the records born since: nothing vanishes early).
    {
        hull_owner(400.f, 100.f);
        draw_lane(g, z_hit, 1.f);
        unsigned popped = 0, worst_frame = 0, max_hits = 0;
        double worst = 0;
        std::uint32_t last_hit = 0;
        unsigned hits = 0;
        for (unsigned f = 0; f < 60; ++f) {
            const std::uint32_t now = 1000 + 16 * f;
            ws::HullState before = hq_state;
            wc::Ring aged_ring = stage_ring;
            while (now >= last_hit + 83) {
                last_hit = last_hit ? last_hit + 83 : now;
                const double r = 3000.0 * std::sqrt(halton(hits + 1, 2)), a = 6.2831853 * halton(hits + 1, 3);
                const std::int32_t pos[3] = {std::int32_t(r * std::cos(a)), std::int32_t(r * std::sin(a)), -10000};
                add_hull_hit(now, pos, damage_of(0.33));
                ++hits;
            }
            const HullOut cur = hull_build(hull_view(g, now, 16));
            const std::vector<float> lc = hull_render(pass, cur);
            unsigned live = 0;
            for (unsigned i = 0; i < wc::ring_capacity; ++i) live += stage_ring.slots[i].sequence != 0;
            max_hits = std::max(max_hits, live);
            if (f) {
                const HullOut aged = hull_build(hull_view(g, now, 16), ws::HullTuning{}, &before, &aged_ring);
                const std::vector<float> la = hull_render(pass, aged);
                for (std::size_t i = 0; i < lc.size(); ++i) {
                    const double d = double(la[i]) - double(lc[i]);
                    if (d > 2e-3 + 2e-3 * double(la[i])) {
                        ++popped;
                        if (d > worst) {
                            worst = d;
                            worst_frame = f;
                        }
                    }
                }
            }
        }
        std::printf("HULL case=no_pop_12hps frames=60 hits=%u max_live=%u popped_pixels=%u worst=%.5f worst_frame=%u\n", hits,
                    max_hits, popped, worst, worst_frame);
        report("hull_no_pop_12hps", popped == 0 && hits >= 10, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 9b. Review 1: the floor is latched at an element's first drawn frame. A hit drawn at SETA 1 (16 ms steps) to 96 ms,
    // then SETA engages (167 ms steps): no element whose life ended comes back (the floor of the new step, 501 ms, would
    // have re-lit the 60 ms flash and the 150 ms glow), and the sparks only ever drop.
    {
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        unsigned reappeared = 0, frames = 0, prev_sparks = 1000;
        bool gone[3] = {false, false, false}; // flash, glow, ember
        std::uint32_t now = 1000, step = 16;
        while (now <= 3000) {
            const HullOut o = hull_build(hull_view(g, now, step));
            const unsigned counts[3] = {o.n.flashes, o.n.glows, o.n.embers};
            for (unsigned k = 0; k < 3; ++k) {
                if (gone[k] && counts[k]) ++reappeared;
                if (!counts[k]) gone[k] = true;
            }
            if (o.n.sparks > prev_sparks) ++reappeared;
            prev_sparks = o.n.sparks;
            ++frames;
            if (now - 1000 >= 96) step = 167;
            now += step;
        }
        std::printf("HULL case=seta_engage frames=%u reappeared=%u latched_floor_ms=%.0f\n", frames, reappeared,
                    double(hq_state.slots[unsigned(std::find_if(stage_ring.slots, stage_ring.slots + wc::ring_capacity,
                                                                [](const wc::Event& e) { return e.sequence != 0; }) -
                                                   stage_ring.slots)].floor_ms));
        report("hull_floor_latched_seta_engage_nothing_reappears", reappeared == 0 && gone[0] && gone[1] && gone[2], "");
    }
    // 9c. Review 6: consecutive frames of the same records across a step change (note 3.1 no-pop: no pixel drops by more
    // than one frame's fade step before an element's end): six records at fixed points on a flat face, 20 frames at 16 ms
    // then 20 at 100 ms (SETA 6), every flash, glow and ember of frame k checked against its own law at its age (its life
    // with the floor its record latched): present while its age is under its life, its radiance the law's, so each
    // element and pixel falls by exactly its own fade step; nothing goes early, nothing comes back.
    {
        hull_owner(400.f, 100.f);
        std::uint32_t seqs[6];
        float px[6], py[6];
        for (unsigned r = 0; r < 6; ++r) {
            const std::int32_t at[3] = {std::int32_t(-5000 + 2000 * std::int32_t(r)), 0, -10000};
            seqs[r] = add_hull_hit(1000 + 30 * r, at, damage_of(0.33))->sequence;
            px[r] = float(at[0]) * wc::world_scale;
            py[r] = 0.f;
        }
        ws::HullTuning statics;
        statics.sparks = false;
        unsigned checked = 0, violations = 0, frames = 0;
        double worst = 0;
        std::uint32_t now = 1150, step = 16; // every record drawn from its own first frame below
        hull_build(hull_view(g, 1000, 16), statics);
        for (std::uint32_t t = 1016; t < 1150; t += 16) hull_build(hull_view(g, t, 16), statics);
        const float rr = hl::r_scale * U * (hl::r_base + hl::r_k * wc::calibre_of(damage_of(0.33)));
        while (frames < 40) {
            const HullOut o = hull_build(hull_view(g, now, step), statics);
            for (unsigned r = 0; r < 6; ++r) {
                const ws::HullSlot* hs = nullptr;
                for (unsigned i = 0; i < wc::ring_capacity; ++i)
                    if (hq_state.slots[i].sequence == seqs[r]) hs = &hq_state.slots[i];
                if (!hs) continue;
                const float age = float(now - hs->first_ms);
                for (unsigned el = 0; el < 3; ++el) { // 0 flash, 1 glow, 2 ember
                    bool fl = false;
                    const float law = el == 0 ? float(hl::flash_ms) : el == 1 ? float(hl::glow_ms) : float(hl::ember_ms);
                    const float life = ws::element_life_ms(law, ws::element_hash(seqs[r], el), wc::stagger, hs->floor_ms, &fl);
                    const float fade = 1.f - age / life;
                    float want = -1.f, got = -1.f;
                    if (age < life) {
                        if (el == 0) want = wc::look::core_e * fade;
                        else if (el == 1) {
                            float c[3];
                            wc::heat_colour(hl::glow_t, c);
                            want = c[0] * hl::glow_e * hl::glow_t * fade;
                        } else {
                            const float T = hl::ember_t * fade;
                            float c[3];
                            wc::heat_colour(T, c);
                            want = c[0] * hl::ember_e * T;
                        }
                    }
                    if (el == 0) {
                        for (unsigned q = 0; q < o.quads; ++q) {
                            if (!is_flash(q)) continue;
                            const ws::HullView hv = hull_view(g, now, step);
                            const float cx = 0.5f * (hq_sprites[4 * q].a[0] / hq_sprites[4 * q].a[3] +
                                                     hq_sprites[4 * q + 3].a[0] / hq_sprites[4 * q + 3].a[3]);
                            const float x = cx / hv.cam.m00 * hq_sprites[4 * q].a[3] / (1.f - hl::bias * rr /
                                                                                            hq_sprites[4 * q].a[3]);
                            if (std::fabs(x - px[r]) < 2.f) got = hq_sprites[4 * q].b[2];
                        }
                    } else {
                        const float R = (el == 1 ? hl::glow_r : hl::ember_r) * rr;
                        for (unsigned b = 0; b < o.boxes; ++b)
                            if (std::fabs(hq_glows[4 * b].light[0] - px[r]) < 0.5f &&
                                std::fabs(hq_glows[4 * b].light[1] - py[r]) < 0.5f &&
                                std::fabs(hq_glows[4 * b].light[3] - R) < 1e-3f * R)
                                got = std::max(hq_glows[4 * b].colour[0], 0.f);
                    }
                    ++checked;
                    const double err = want < 0.f || got < 0.f ? (want < 0.f && got < 0.f ? 0.0 : 1.0)
                                                               : std::fabs(double(got) - double(want));
                    if (err > 1e-4 + 1e-4 * double(want)) ++violations;
                    worst = std::max(worst, err);
                }
            }
            ++frames;
            if (frames == 20) step = 100;
            now += step;
        }
        std::printf("HULL case=no_pop_step_change frames=40 records=6 checked=%u violations=%u worst=%.6f\n", checked,
                    violations, worst);
        report("hull_no_pop_step_change_law", violations == 0 && checked == 40 * 18, "");
    }
    // 9d. Review 5: a pass whose hull programs are refused (fault bit 4) stays enabled for the shields: a patch draws,
    // the hull's sprites and glows draw nothing.
    {
        rr::WeaponEffectsPass shields_only;
        shields_only.set_faults(16);
        const HRESULT hr = shields_only.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, true);
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        const HullOut o = hull_build(hull_view(g, 1000, 16));
        const rr::WeaponPatch patch = make_patch(tier_scene[0], {{0.f, 0.f, 0.05f}});
        rr::WeaponEffectsFrame f = hull_frame(g, o);
        f.patches = &patch;
        f.patch_count = 1;
        plane_lane(g, patch);
        const auto drawn = draw_on(shields_only, g, f);
        const double sum = total(read_luma(g, g.scene_s.p));
        std::printf("HULL case=hull_refused result=%08lx enabled=%u hull=%u reason=%s run=%08lx patches=%u sprites=%u glows=%u "
                    "luma=%.3f\n", hr, unsigned(shields_only.caps().enabled), unsigned(shields_only.caps().hull_enabled),
                    shields_only.caps().hull_reason, drawn.first, drawn.second.patches, drawn.second.sprite_quads,
                    drawn.second.glow_boxes, sum);
        report("hull_programs_refused_shields_still_draw",
               SUCCEEDED(hr) && shields_only.caps().enabled && !shields_only.caps().hull_enabled &&
                   !std::strcmp(shields_only.caps().hull_reason, "hull_program_create") && SUCCEEDED(drawn.first) &&
                   drawn.second.patches == 1 && drawn.second.sprite_quads == 0 && drawn.second.glow_boxes == 0 && sum > 1.0,
               "");
        rr::WeaponEffectsPass shields_mode; // shields mode never asks for them
        shields_mode.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, false);
        report("shields_mode_loads_no_hull_programs", shields_mode.caps().enabled && !shields_mode.caps().hull_enabled &&
                                                          !std::strcmp(shields_mode.caps().hull_reason, "not_requested") &&
                                                          shields_mode.references() == 7, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 10. The far side: a hit on the back of a sphere hull (the lane its front) draws no flash or glow (the hull hides it,
    // the glow's surface lies past R); the flash on the near side not halved by its own surface (0.9 of the sky's).
    {
        const double sphere[3] = {100.0, 100.0, 100.0};
        hull_owner(400.f, 100.f);
        const HullLane hull = staged_hull(sphere);
        const std::int32_t back[3] = {0, 0, 10000};
        add_hull_hit(1000, back, damage_of(0.33));
        ws::HullTuning no_sparks;
        no_sparks.sparks = false;
        const HullOut far_out = hull_build(hull_view(g, 1000, 16), no_sparks);
        draw_hull_lane(g, hull);
        const double far_sum = total(hull_render(pass, far_out));
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        const HullOut near_out = hull_build(hull_view(g, 1000, 16), only_flash);
        draw_hull_lane(g, hull);
        const double near_hull = total(hull_render(pass, near_out));
        draw_lane(g, 0.f, -1.f);
        const double near_sky = total(hull_render(pass, near_out));
        std::printf("HULL case=far_side far_sum=%.5f near_flash_on_hull=%.4f near_flash_on_sky=%.4f ratio=%.4f\n", far_sum,
                    near_hull, near_sky, near_sky > 0 ? near_hull / near_sky : 0.0);
        report("hull_far_side_hit_hidden", far_out.boxes >= 1 && far_out.n.flashes == 1 && far_sum <= 1e-3 * near_sky, "");
        report("hull_flash_not_halved_by_its_surface", near_sky > 0 && near_hull >= 0.9 * near_sky, "");
    }
    // 11. The chase-nose gate (look 2.9): the bubble case's camera behind and above a 4 x 1.6 x 12 m hull 24 m ahead,
    // pitched 0.175 rad, a nose hit: the sparks' lit pixels outside the hull's outline 200 ms in (their reach, not the
    // first frame's flash; review 6), the flash, glow and ember off.
    {
        hull_owner(120.f, 66.f);
        const double pc = std::cos(0.175), ps = std::sin(0.175);
        const std::int32_t rows[3][3] = {{65536, 0, 0},
                                         {0, std::int32_t(pc * 65536.0), std::int32_t(ps * 65536.0)},
                                         {0, -std::int32_t(ps * 65536.0), std::int32_t(pc * 65536.0)}};
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(rows[r][j]));
        const std::int32_t nose[3] = {0, 0, 6000};
        add_hull_hit(1000, nose, damage_of(0.33));
        const double ext[3] = {20.0, 8.0, 60.0};
        const HullLane hull = staged_hull(ext);
        draw_hull_lane(g, hull);
        hull_build(hull_view(g, 1000, 16), only_sparks);
        const HullOut o = hull_build(hull_view(g, 1200, 16), only_sparks);
        const std::vector<float> l = hull_render(pass, o);
        unsigned outside = 0, lit = 0;
        for (std::size_t i = 0; i < l.size(); i += 1) {
            if (!(l[i] > 1e-3f)) continue;
            ++lit;
            double p[3];
            if (!hull_point(g, hull, unsigned(i % g.w), unsigned(i / g.w), p)) ++outside;
        }
        std::printf("HULL case=chase_nose age_ms=200 sparks=%u lit=%u outside_lit=%u\n", o.n.sparks, lit, outside);
        report("hull_chase_nose_lit_outside_outline", o.n.sparks > 0 && outside >= 20, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 11b. The sparks' visibility (the lab's "no sparks" report, 2026-11-06): sparks alone at 100 and 200 ms, (a) the
    // chase-nose camera of case 11 on a fighter (IRE, k 0.33), (b) a PPC (k 0.8) on a capital's flat face 300 m from
    // the eye (view z 1500, the lane at the hit: the inward half hidden). Lit pixels, the mean quad width and rest
    // length in pixels through the vertex program's CPU twin, the peak and summed luma. Report rows only.
    for (unsigned subject = 0; subject < 2; ++subject)
        for (std::uint32_t age : {100u, 200u}) {
            const bool capital = subject == 1;
            HullLane hull{};
            if (capital) {
                hull_owner(2500.f, 1000.f);
                const std::int32_t at[3] = {0, 0, -100000};
                add_hull_hit(1000, at, damage_of(0.8), wc::patch_capital);
                draw_lane(g, 1500.f, 1.f);
            } else {
                hull_owner(120.f, 66.f);
                const double pc = std::cos(0.175), ps = std::sin(0.175);
                const std::int32_t rows[3][3] = {{65536, 0, 0},
                                                 {0, std::int32_t(pc * 65536.0), std::int32_t(ps * 65536.0)},
                                                 {0, -std::int32_t(ps * 65536.0), std::int32_t(pc * 65536.0)}};
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned j = 0; j < 3; ++j)
                        put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(rows[r][j]));
                const std::int32_t nose[3] = {0, 0, 6000};
                add_hull_hit(1000, nose, damage_of(0.33));
                const double ext[3] = {20.0, 8.0, 60.0};
                hull = staged_hull(ext);
                draw_hull_lane(g, hull);
            }
            hull_build(hull_view(g, 1000, 16), only_sparks);
            const HullOut o = hull_build(hull_view(g, 1000 + age, 16), only_sparks);
            double w = 0, len = 0;
            for (unsigned q = 0; q < o.quads; ++q) {
                const ws::StreakCorners sk = ws::streak_corners(hull_view(g, 1000 + age, 16), hq_sprites + 4 * q);
                w += sk.lit;
                len += sk.rest;
            }
            const std::vector<float> l = hull_render(pass, o);
            float peak = 0.f;
            for (float v : l) peak = std::max(peak, v);
            std::printf("HULL case=spark_visibility subject=%s age_ms=%u sparks=%u lit=%u width_px=%.2f rest_px=%.2f "
                        "peak_luma=%.3f sum_luma=%.1f\n",
                        capital ? "capital_ppc_300m" : "fighter_chase_nose", age, o.n.sparks, lit_count(l),
                        o.quads ? w / o.quads : 0.0, o.quads ? len / o.quads : 0.0, double(peak), total(l));
            draw_lane(g, 0.f, -1.f);
        }
    // 12. The own ship in the cockpit: every element of a hit on it at own_cockpit_gain; another ship unchanged.
    {
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        draw_lane(g, z_hit, 1.f);
        double sums[3] = {0, 0, 0};
        for (unsigned c = 0; c < 3; ++c) {
            ws::HullState st = hq_state;
            ws::HullView v = hull_view(g, 1030, 16);
            v.own_node = c == 2 ? addr(fake_node) + 4 : addr(fake_node);
            v.cockpit = c != 0;
            const HullOut o = hull_build(v, ws::HullTuning{}, &st);
            sums[c] = total(hull_render(pass, o));
        }
        const double own = sums[0] > 0 ? sums[1] / sums[0] : 0, other = sums[0] > 0 ? sums[2] / sums[0] : 0;
        std::printf("HULL case=cockpit chase=%.4f own_cockpit=%.4f other_cockpit=%.4f own_ratio=%.4f other_ratio=%.4f gain=%.3f\n",
                    sums[0], sums[1], sums[2], own, other, double(wc::own_cockpit_gain));
        report("hull_cockpit_dim_own_ship", std::fabs(own - double(wc::own_cockpit_gain)) <= 0.005 &&
                                                std::fabs(other - 1.0) <= 1e-4, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 13. Variants: the repair laser (no flash, no sparks, the weapon tint on glow and ember), the tug (the glow only,
    // pale blue, calibre 0.1), a missile source (2 x the sparks, its tint on the flash halo), an asteroid (2 x the sparks
    // on the ramp's last stops, no flash).
    {
        hull_owner(400.f, 100.f);
        wc::Event* rep = add_hull_hit(1000, front, damage_of(0.33), wc::patch_corvette, wc::detail_repair);
        rep->tint_rgb = wc::pack_rgb(50, 255, 50);
        const HullOut ro = hull_build(hull_view(g, 1000, 16));
        double gtint[3] = {0, 0, 0};
        for (unsigned k = 0; k < 3; ++k) gtint[k] = hq_glows[0].colour[k];
        const bool repair_ok = ro.n.sparks == 0 && ro.n.flashes == 0 && ro.n.glows == 1 && ro.n.embers == 1 &&
                               gtint[1] > 2.0 * gtint[0] && gtint[1] > 2.0 * gtint[2];
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33), wc::patch_fighter, wc::detail_tug);
        const HullOut to = hull_build(hull_view(g, 1000, 16));
        const float tug_R = to.boxes ? hq_glows[0].light[3] : 0.f;
        const bool tug_ok = to.n.sparks == 0 && to.n.flashes == 0 && to.n.glows == 1 && to.n.embers == 0 &&
                            std::fabs(tug_R - hl::glow_r * hl::r_scale * U * (hl::r_base + hl::r_k * 0.1f)) <= 1e-3f * tug_R &&
                            hq_glows[0].colour[2] > hq_glows[0].colour[0];
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.5), wc::patch_corvette, 0, wc::table_missiles);
        const HullOut mo = hull_build(hull_view(g, 1000, 16));
        double halo[3] = {0, 0, 0};
        for (unsigned q = 0; q < mo.quads; ++q)
            if (is_flash(q)) colour_of(hq_sprites[4 * q].colour, halo);
        float mt[3];
        wc::tint_linear(wc::missile_tint_rgb, mt);
        const unsigned want_m = unsigned((float(hl::spark_n0) + float(hl::spark_nk) * 0.5f) * hl::missile_spark_x + 0.5f);
        const bool missile_ok = mo.n.sparks == want_m && mo.n.flashes == 1 && std::fabs(halo[1] - double(mt[1])) <= 0.01;
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.5), wc::patch_fighter, wc::detail_asteroid);
        const HullOut ao = hull_build(hull_view(g, 1000, 16));
        double dust[3] = {0, 0, 0};
        if (ao.quads) colour_of(hq_sprites[0].colour, dust);
        const unsigned want_a = unsigned((float(hl::spark_n0) + float(hl::spark_nk) * 0.5f) * hl::asteroid_dust_x + 0.5f);
        const bool asteroid_ok = ao.n.sparks == want_a && ao.n.flashes == 0 && dust[0] < 0.9 && dust[1] < 0.5 * dust[0];
        std::printf("HULL case=variants repair_sparks=%u repair_flashes=%u repair_glows=%u repair_embers=%u repair_glow_rgb=%.3f,%.3f,%.3f "
                    "tug_glows=%u tug_embers=%u tug_R=%.3f missile_sparks=%u missile_want=%u missile_halo_g=%.3f missile_tint_g=%.3f "
                    "asteroid_sparks=%u asteroid_want=%u asteroid_flashes=%u asteroid_rgb=%.3f,%.3f,%.3f\n",
                    ro.n.sparks, ro.n.flashes, ro.n.glows, ro.n.embers, gtint[0], gtint[1], gtint[2], to.n.glows, to.n.embers,
                    double(tug_R), mo.n.sparks, want_m, halo[1], double(mt[1]), ao.n.sparks, want_a, ao.n.flashes, dust[0],
                    dust[1], dust[2]);
        report("hull_repair_no_sparks", repair_ok, "");
        report("hull_tug_glow_only", tug_ok, "");
        report("hull_missile_tier", missile_ok, "");
        report("hull_asteroid_dust", asteroid_ok, "");
    }
    // 14. velocity_from_frame_delta: an owner moving 2000 engine units (20 view) in 16 ms: the first hit of the owner takes
    // 0 (no previous stage frame), a hit arriving after a frame that saw the owner takes the node's delta.
    {
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        hull_build(hull_view(g, 1000, 16));
        put32(fake_node, wc::layout::node_pos + 0, 2000u);
        const wc::Event* b = add_hull_hit(1016, front, damage_of(0.33));
        const unsigned slot_b = unsigned(b - stage_ring.slots);
        hull_build(hull_view(g, 1016, 16));
        const float want = 2000.f * wc::world_scale * 1000.f / 16.f;
        unsigned slot_a = 0;
        for (unsigned i = 0; i < wc::ring_capacity; ++i)
            if (stage_ring.slots[i].sequence && i != slot_b) slot_a = i;
        std::printf("HULL case=velocity first_v=%.3f,%.3f,%.3f second_v=%.3f,%.3f,%.3f want_vx=%.3f\n",
                    double(hq_state.slots[slot_a].v[0]), double(hq_state.slots[slot_a].v[1]),
                    double(hq_state.slots[slot_a].v[2]), double(hq_state.slots[slot_b].v[0]),
                    double(hq_state.slots[slot_b].v[1]), double(hq_state.slots[slot_b].v[2]), double(want));
        report("velocity_from_frame_delta", hq_state.slots[slot_a].v[0] == 0.f &&
                                                std::fabs(hq_state.slots[slot_b].v[0] - want) <= 1e-3f * want &&
                                                hq_state.slots[slot_b].v[1] == 0.f && hq_state.slots[slot_b].v[2] == 0.f, "");
        // A dead owner after its first frame: the sparks stay (orphan), flash / glow / ember go; one never seen leaves.
        put32(fake_obj, wc::layout::obj_flags, wc::layout::obj_dying);
        add_hull_hit(1030, front, damage_of(0.33)); // never drawn while live
        const HullOut d = hull_build(hull_view(g, 1032, 16));
        unsigned live = 0;
        for (unsigned i = 0; i < wc::ring_capacity; ++i) live += stage_ring.slots[i].sequence != 0;
        std::printf("HULL case=orphan orphans=%u retired=%u sparks=%u flashes=%u glows=%u embers=%u live=%u\n", d.n.orphans,
                    d.n.retired, d.n.sparks, d.n.flashes, d.n.glows, d.n.embers, live);
        report("hull_orphan_sparks_outlive_owner", d.n.orphans == 2 && d.n.retired == 1 && d.n.sparks > 0 &&
                                                       d.n.flashes == 0 && d.n.glows == 0 && d.n.embers == 0 && live == 2, "");
        // Review 4: the owner's first hull hit, the owner drawn in the previous stage frame for a shield hit: the velocity
        // from that frame's rows, and the sparks' mean head (the antipodal pairs cancel) 100 ms later at p0 + v F(0.1 s).
        {
            hull_owner(400.f, 100.f);
            add_stage_hit(1000, front);                       // a shield hit: the owner's rows tracked at 1000
            hull_build(hull_view(g, 1000, 16));
            put32(fake_node, wc::layout::node_pos + 0, 2000u); // 20 view units in 16 ms: 1250 view units per second
            const wc::Event* first = add_hull_hit(1016, front, damage_of(0.5));
            const unsigned slot = unsigned(first - stage_ring.slots);
            hull_build(hull_view(g, 1016, 16), only_sparks);
            const float v0 = hq_state.slots[slot].v[0];
            put32(fake_node, wc::layout::node_pos + 0, 2000u + 12500u); // the owner 0.1 s on
            const HullOut o = hull_build(hull_view(g, 1116, 16), only_sparks);
            double mean_x = 0;
            for (unsigned q = 0; q < o.quads; ++q) {
                const QuadPx p = quad_px(g, q);
                mean_x += (p.hx / double(g.w) * 2.0 - 1.0) / 1.6 * p.hz;
            }
            mean_x = o.quads ? mean_x / o.quads : 0.0;
            const double expect_x = 20.0 + double(want) * double(ws::spark_reach_factor(0.1f));
            std::printf("HULL case=velocity_first_hit v=%.3f want=%.3f sparks=%u mean_head_x=%.3f expected_x=%.3f "
                        "owner_x=%.3f\n", double(v0), double(want), o.n.sparks, mean_x, expect_x, 20.0 + 125.0);
            report("velocity_first_hit_from_previous_frame_rows", std::fabs(v0 - want) <= 1e-3f * want && o.n.sparks == 16 &&
                                                                  std::fabs(mean_x - expect_x) <= 0.02 * (expect_x - 20.0), "");
        }
    }
    // 15. The shared budget: past the screens the patches left, other owners' glows shed oldest first, the own ship's kept,
    // sparks and flashes never.
    {
        hull_owner(400.f, 100.f);
        for (unsigned i = 0; i < 12; ++i) add_hull_hit(1000 + 5 * i, front, damage_of(1.0), wc::patch_capital);
        ws::HullState st = hq_state;
        ws::HullView full = hull_view(g, 1060, 16), tight = full;
        tight.budget_left = 0.5f;
        const HullOut a = hull_build(full, ws::HullTuning{}, &st);
        const unsigned sparks_full = a.n.sparks, boxes_full = a.boxes;
        st = hq_state;
        const HullOut b = hull_build(tight, ws::HullTuning{}, &st);
        st = hq_state;
        tight.own_node = addr(fake_node);
        const HullOut c = hull_build(tight, ws::HullTuning{}, &st);
        std::printf("HULL case=budget boxes=%u screens=%.3f tight_boxes=%u tight_shed=%u tight_screens=%.3f sparks=%u tight_sparks=%u "
                    "own_boxes=%u own_shed=%u\n",
                    boxes_full, double(a.n.screens), b.boxes, b.n.shed, double(b.n.screens), sparks_full, b.n.sparks, c.boxes,
                    c.n.shed);
        report("hull_budget_shed", b.n.shed > 0 && b.n.screens <= 0.5f && b.n.sparks == sparks_full && b.n.flashes == a.n.flashes &&
                                       c.n.shed == 0 && c.boxes == boxes_full, "");
    }
    // 16. The R32F lane form (four_channel false, the step-1 open item): the glow and the hidden sparks on a plane drawn
    // with the same law as on the four-channel lane.
    {
        Com<IDirect3DTexture9> rt;
        Com<IDirect3DSurface9> rs;
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        hull_build(hull_view(g, 1000, 16));
        const HullOut o = hull_build(hull_view(g, 1080, 16));
        draw_lane(g, z_hit, 1.f);
        const double four = total(hull_render(pass, o));
        const float m22 = 1.0001f, m32 = -6.0006f;
        double r32 = -1;
        if (r32f_lane(g, rt, rs, z_hit, m22, m32)) {
            rr::WeaponEffectsFrame f = hull_frame(g, o);
            f.lane = rt.p;
            f.four_channel = false;
            f.m22 = m22;
            f.m32 = m32;
            draw_on(pass, g, f);
            r32 = total(read_luma(g, g.scene_s.p));
        }
        std::printf("HULL case=r32f_lane four_channel=%.4f r32f=%.4f ratio=%.5f\n", four, r32, four > 0 ? r32 / four : 0.0);
        report("hull_r32f_lane_form", r32 > 0 && std::fabs(r32 / four - 1.0) <= 0.01, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 17. The streak's core through the production TemporalPass at 0 / 4 / 8 px per frame, and 8 frames after the spark
    // left, no residual above one 8-bit code (1 / 255).
    {
        Com<IDirect3DTexture9> motion;
        Com<IDirect3DSurface9> motion_s;
        const bool made = SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                       D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                          SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
        for (const float speed : {0.f, 4.f, 8.f}) {
            float peak = 0.f;
            double residual = 1;
            const double ratio = made ? resolve_streak(pass, motion, motion_s, speed, 8, &peak, &residual) : -1.0;
            std::printf("HULL case=resolve speed_px=%.0f frames=16 core=%.4f peak=%.3f residual_after_8=%.5f\n", double(speed),
                        ratio, double(peak), residual);
            char label[64];
            std::snprintf(label, sizeof label, "hull_resolve_core_survival_%.0fpx", double(speed));
            report(label, ratio >= 0.95, "");
            std::snprintf(label, sizeof label, "hull_resolve_no_scar_%.0fpx", double(speed));
            report(label, ratio >= 0 && residual <= 1.0 / 255.0, "");
        }
        draw_lane(g, 0.f, -1.f);
    }
    // 18. Fault and Reset: a failed hull draw is reported and the next draws; after a Reset the programs come back and
    // the hull frame draws; a pass whose programs were refused draws nothing (no call).
    {
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.33));
        const HullOut o = hull_build(hull_view(g, 1050, 16));
        draw_lane(g, z_hit, 1.f);
        pass.set_faults(2);
        const auto bad = draw_on(pass, g, hull_frame(g, o));
        pass.set_faults(0);
        const auto good = draw_on(pass, g, hull_frame(g, o));
        report("hull_fault_reported", FAILED(bad.first) && (bad.second.failed == rr::WeaponEffectsStep::Glows ||
                                                            bad.second.failed == rr::WeaponEffectsStep::Sprites), "");
        report("hull_recovers_after_fault", SUCCEEDED(good.first) && good.second.drew && good.second.sprite_quads == o.quads &&
                                                good.second.glow_boxes == o.boxes, "");
        pass.before_reset();
        const unsigned refs_reset = pass.references();
        g.release();
        D3DPRESENT_PARAMETERS pp2 = pp;
        const HRESULT reset = dev->Reset(&pp2);
        pass.after_reset(reset);
        if (r32f_ps) {
            r32f_ps->Release();
            r32f_ps = nullptr;
        }
        const bool mt = SUCCEEDED(reset) && make_target(g, 1920, 1080);
        const HRESULT er = mt ? pass.ensure_resources() : E_FAIL;
        bool drew = false;
        if (mt && SUCCEEDED(er)) {
            draw_lane(g, z_hit, 1.f);
            const auto after = draw_on(pass, g, hull_frame(g, o));
            drew = SUCCEEDED(after.first) && after.second.drew && after.second.sprite_quads == o.quads &&
                   total(read_luma(g, g.scene_s.p)) > 1.0;
        }
        std::printf("HULL case=reset result=%08lx references_before_reset=%u ensure=%08lx drew=%u\n", reset, refs_reset, er,
                    unsigned(drew));
        report("hull_reset_recovers", refs_reset == 0 && mt && SUCCEEDED(er) && pass.resources_ready() && drew, "");
        rr::WeaponEffectsPass refused;
        refused.set_faults(8);
        refused.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, true);
        rr::WeaponEffectsReport rep{};
        const HRESULT run = refused.run(hull_frame(g, o), &rep);
        report("hull_program_refusal_draws_nothing", !refused.caps().enabled && refused.references() == 0 &&
                                                         run == E_INVALIDARG && rep.calls == 0, "");
        draw_lane(g, 0.f, -1.f);
    }
    // 19. The CPU cost of the hull build: 64 hits on one capital (best / mean of 200 builds) and 20 hits on a fighter.
    {
        LARGE_INTEGER freq{}, a{}, b{};
        QueryPerformanceFrequency(&freq);
        for (unsigned c = 0; c < 2; ++c) {
            const unsigned hits = c ? 20 : 64;
            hull_owner(c ? 400.f : 4000.f, c ? 100.f : 2000.f);
            for (unsigned i = 0; i < hits; ++i) {
                const double r = (c ? 3000.0 : 60000.0) * std::sqrt(halton(i + 1, 2)), ang = 6.2831853 * halton(i + 1, 3);
                const std::int32_t pos[3] = {std::int32_t(r * std::cos(ang)), std::int32_t(r * std::sin(ang)),
                                             c ? -10000 : -200000};
                add_hull_hit(1000 + 4 * i, pos, damage_of(c ? 0.33 : 0.8), c ? wc::patch_fighter : wc::patch_capital);
            }
            hull_build(hull_view(g, 1000 + 4 * hits, 16));
            double best = 1e30, sum = 0;
            HullOut o;
            for (unsigned i = 0; i < 200; ++i) {
                ws::HullState st = hq_state;
                QueryPerformanceCounter(&a);
                o = hull_build(hull_view(g, 1000 + 4 * hits + 16, 16), ws::HullTuning{}, &st);
                QueryPerformanceCounter(&b);
                const double us = ticks_us(a, b, freq);
                best = std::min(best, us);
                sum += us;
            }
            std::printf("HULL_CPU case=%s hits=%u sparks=%u flashes=%u boxes=%u best_us=%.2f mean_us=%.2f target_us=30\n",
                        c ? "fighter_20_hits" : "capital_64_hits", hits, o.n.sparks, o.n.flashes, o.boxes, best, sum / 200.0);
            if (!c) report("hull_cpu_64_hits", best <= 30.0, "");
        }
    }
    // 20. GPU cost, EVENT-fenced (best of 60) at 1080p and 5120x1440: 20 hits on a fighter at 60 m (the 3.1 budget: 50 us
    // at 5120x1440), 64 hits on a capital at close range, one glow filling the frame (the eye inside its box), each with
    // the lit and rasterized shares.
    for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
        Target tt;
        if (!make_target(tt, size.first, size.second)) {
            report("hull_timing_target", false, "");
            continue;
        }
        Com<IDirect3DQuery9> ev, occlusion;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        for (unsigned c = 0; c < 4; ++c) {
            // 0: 20 hits on a fighter 60 m away (its hull a fifth of the frame: the stress row); 1: the same 200 m away
            // (the 3.1 budget's case); 2: 64 hits on a capital at close range; 3: one glow alone whose sphere holds the
            // eye (the whole screen once: the glow's full-screen cost, its budget weight against the decal's 177.5 us).
            const unsigned hits = c == 2 ? 64 : c == 3 ? 1 : 20;
            const float owner_z = c == 0 ? 400.f : c == 1 ? 1100.f : c == 2 ? 2600.f : 120.f;
            hull_owner(owner_z, c == 2 ? 2000.f : 100.f);
            for (unsigned i = 0; i < hits; ++i) {
                const double r = (c == 2 ? 60000.0 : 3000.0) * std::sqrt(halton(i + 1, 2)), ang = 6.2831853 * halton(i + 1, 3);
                const std::int32_t pos[3] = {c == 3 ? 0 : std::int32_t(r * std::cos(ang)),
                                             c == 3 ? 0 : std::int32_t(r * std::sin(ang)), c == 2 ? -200000 : -10000};
                add_hull_hit(1000 + 4 * i, pos, damage_of(c == 2 ? 0.8 : c == 3 ? 1.0 : 0.33),
                             c >= 2 ? wc::patch_capital : wc::patch_fighter);
            }
            ws::HullView v = hull_view(tt, 1000, 16);
            hull_build(v, c == 3 ? only_glow : ws::HullTuning{});
            v.now = 1000 + 4 * hits + 16;
            const HullOut o = hull_build(v, c == 3 ? only_glow : ws::HullTuning{});
            const float lane_z = c == 2 ? 600.f : c == 3 ? 20.f : owner_z - 100.f;
            draw_lane(tt, lane_z, 1.f);
            const rr::WeaponEffectsFrame f = hull_frame(tt, o);
            double best = 1e30;
            // 120 untimed runs first: the CPU-only cases before these rows leave the GPU idle, and best-of-60 of a short
            // fenced draw did not always bring its clock back (the glow-alone row read 71 or 109 us run to run).
            for (int i = -120; i < 60; ++i) {
                dev->BeginScene();
                dev->SetRenderTarget(0, tt.scene_s.p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                fence(ev.p);
                QueryPerformanceCounter(&a);
                rr::WeaponEffectsReport r{};
                pass.run(f, &r);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                if (i >= 0) best = std::min(best, ticks_us(a, b, freq));
            }
            dev->BeginScene();
            dev->SetRenderTarget(0, tt.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            DWORD rasterized = 0;
            {
                rr::WeaponEffectsReport r{};
                if (occlusion.p) occlusion.p->Issue(D3DISSUE_BEGIN);
                pass.run(f, &r);
                if (occlusion.p) occlusion.p->Issue(D3DISSUE_END);
            }
            dev->EndScene();
            if (occlusion.p) {
                const DWORD start = GetTickCount();
                while (occlusion.p->GetData(&rasterized, sizeof rasterized, D3DGETDATA_FLUSH) == S_FALSE &&
                       GetTickCount() - start < 5000) {
                }
            }
            const std::vector<float> l = read_luma(tt, tt.scene_s.p);
            const char* name = c == 0   ? "fighter_60m_20_hits"
                               : c == 1 ? "fighter_200m_20_hits"
                               : c == 2 ? "capital_close_64_hits"
                                        : "glow_alone_fills_frame";
            std::printf("HULL_TIMING width=%u height=%u case=%s hits=%u sparks=%u flashes=%u boxes=%u lit_share=%.4f "
                        "raster_share=%.4f gpu_us=%.1f\n",
                        size.first, size.second, name, hits, o.n.sparks, o.n.flashes, o.boxes,
                        l.empty() ? 0.0 : double(lit_count(l)) / double(l.size()),
                        double(rasterized) / (double(size.first) * double(size.second)), best);
            if (c == 1 && size.first == 5120) report("hull_budget_20_hits_5120", best <= 50.0, "");
        }
        tt.release();
    }
    hull_owner(400.f, 100.f);
    draw_lane(g, 0.f, -1.f);
}
