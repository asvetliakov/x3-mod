// The hull hit's D3D cases (docs/architecture/weapon-look.md 3.1 "Fixture gates", step 2), included by
// weapon_effects_fixture.cpp inside its helper namespace (its device, targets, lanes, fake owner and ring). Every case
// builds through the production stage::build_hull over the fixture's fake engine memory and draws through the
// production WeaponEffectsPass (the sprite and glow programs). Rows: `HULL case=... ` and `HULL_TIMING ...`.
// Never launches the game.
rr::WeaponSpriteVertex hq_sprites[rr::weapon_sprite_quads * 4];
rr::WeaponGlowVertex hq_glows[rr::weapon_glow_boxes * 4];
rr::WeaponFireballVertex hq_fireballs[rr::weapon_fireball_boxes * 4];
rr::WeaponShapeVertex hq_rings[rr::weapon_ring_quads * 4];
rr::WeaponLightningVertex hq_filaments[rr::weapon_filament_boxes * 4];
rr::WeaponGlowVertex hq_scorches[rr::weapon_scorch_boxes * 4];
ws::ShimmerRect hq_shimmer[wc::hull::shimmer_max];
ws::HullState hq_state;
// The 1024-record ring's CPU bounds (case 19b: build_hull and the overflow stamp, best of 200): set from the native
// measurement of 2026-11-07 (docs/verification/weapon-effects.md, "Ring 1024").
// 64 visible: 144.7 and 136.9 measured before the out-of-reach cull (2026-11-07), the bound 100 after it (its owners
// behind the camera build nothing); 256 visible: 214.0 and 209.1 before.
constexpr double ring_1024_cpu_64_us = 100.0, ring_1024_cpu_256_us = 300.0;
// The laws of the records the shared machinery's gates stage (the class nibble 0: the energy class, hull-impact-
// classes.md 4.1; its sparks one set, the slow "spatter", so a spark's index is its set index), and of any anatomy.
namespace law {
namespace hl = wc::hull;
constexpr unsigned E = hl::an_energy;
inline float flash_ms(unsigned a = E) { return hl::class_flash[a][2]; }
inline float flash_halo_r(unsigned a = E) { return hl::class_flash[a][1]; }
inline float glow_r(unsigned a = E) { return hl::class_glow[a][0]; }
inline float glow_ms(unsigned a = E) { return hl::class_glow[a][1]; }
inline float glow_t(unsigned a = E) { return hl::class_glow[a][2]; }
inline float glow_e(unsigned a = E) { return hl::class_glow[a][3]; }
inline float ember_r(unsigned a = E) { return hl::class_ember[a][0]; }
inline float ember_ms(float k, unsigned a = E) { return hl::class_ember[a][1] + hl::class_ember[a][2] * k; }
inline float ember_t(unsigned a = E) { return hl::class_ember[a][3]; }
inline float ember_e(unsigned a = E) { return hl::class_ember[a][4]; }
inline float puff_r(unsigned a = E) { return hl::class_puff[a][0]; }
inline float puff_ms(float k, unsigned a = E) { return hl::class_puff[a][3] + hl::class_puff[a][4] * k; }
inline float puff_e(unsigned a = E) { return hl::class_puff[a][6]; }
inline float puff_rise(unsigned a = E) { return hl::class_puff[a][7]; }
inline ws::SparkSet set(unsigned a = E, unsigned which = 1) { return ws::spark_set(a, which); }
inline float spark_life(float k, unsigned a = E, unsigned which = 1) {
    const ws::SparkSet ss = ws::spark_set(a, which);
    return ss.life + ss.life_k * k;
}
// Every set's sparks of a record (the stage's counts).
inline unsigned sparks_of(const wc::Event& e, float k, const ws::HullTuning& t = ws::HullTuning{}) {
    unsigned n = 0;
    for (unsigned q = 0; q < 3; ++q) n += ws::spark_count(e, k, t, ws::anatomy_of(e), q);
    return n;
}
inline ws::SparkFactors factors(std::uint32_t seq, unsigned g, unsigned a = E, unsigned which = 1) {
    return ws::spark_factors(seq, g, ws::spark_set(a, which).speed_spread);
}
// The sparks of a record alive and lit at `age` by the stage's own law (each set's count, factors, floor and flicker duty).
inline unsigned sparks_lit(const wc::Event& e, float k, float age, float floor_ms) {
    const unsigned a = ws::anatomy_of(e);
    unsigned g0 = 0, n = 0;
    for (unsigned set = 0; set < 3; ++set) {
        const unsigned count = ws::spark_count(e, k, ws::HullTuning{}, a, set);
        if (!count) continue;
        const ws::SparkSet ss = ws::spark_set(a, set);
        const unsigned slot = unsigned(age) / wc::hull::flicker_ms;
        for (unsigned sp = 0; sp < count; ++sp) {
            const unsigned g = g0 + sp;
            const float law = (ss.life + ss.life_k * k) * ws::spark_factors(e.sequence, g, ss.speed_spread).life_x;
            if (!(age < (law > floor_ms ? law : floor_ms))) continue;
            if (ss.duty > 0.f &&
                !(ws::unit_of(ws::element_hash(e.sequence ^ (slot * 0x9e3779b9u), 8192u + g)) < ss.duty))
                continue;
            ++n;
        }
        g0 += (count + 1) & ~1u;
    }
    return n;
}
} // namespace law
// The staged owner at view (0, 0, z) with radius r (identity camera and rows) and an empty ring and hull state.
void hull_owner(float z, float r) {
    stage_owner(z, r, 0);
    wc::ring_clear(stage_ring);
    ws::hull_clear(hq_state);
}
// One stage-owned hull hit on the staged owner (local engine position, damage, tier, variant bits, table, the hull-hit
// class of the nibble).
wc::Event* add_hull_hit(std::uint32_t t0, const std::int32_t pos[3], std::int32_t damage,
                        std::uint8_t tier = wc::patch_fighter, std::uint8_t detail = 0,
                        std::uint8_t table = wc::table_bullets, std::uint8_t cls = wc::class_energy) {
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
    e->detail = std::uint8_t(detail | (cls << wc::detail_class_shift));
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
    unsigned quads = 0, boxes = 0, bursts = 0, rings = 0, filaments = 0, scorches = 0, shimmer = 0;
};
HullOut hull_build(const ws::HullView& v, const ws::HullTuning& t = ws::HullTuning{}, ws::HullState* st = &hq_state,
                   wc::Ring* ring = &stage_ring, ws::Reader rd = &fake_read, wc::VisibleOwners* visible = nullptr) {
    HullOut o;
    ws::HullBuffers b;
    b.sprites = hq_sprites;
    b.sprite_cap = rr::weapon_sprite_quads;
    b.glows = hq_glows;
    b.glow_cap = rr::weapon_glow_boxes;
    b.fireballs = hq_fireballs;
    b.fireball_cap = rr::weapon_fireball_boxes;
    b.rings = hq_rings;
    b.ring_cap = rr::weapon_ring_quads;
    b.filaments = hq_filaments;
    b.filament_cap = rr::weapon_filament_boxes;
    b.scorches = hq_scorches;
    b.scorch_cap = rr::weapon_scorch_boxes;
    b.shimmer = hq_shimmer;
    b.shimmer_cap = wc::hull::shimmer_max;
    o.n = ws::build_hull(*ring, *st, rd, v, t, b, visible);
    o.quads = b.sprite_quads;
    o.boxes = b.glow_boxes;
    o.bursts = b.fireball_boxes;
    o.rings = b.ring_quads;
    o.filaments = b.filament_boxes;
    o.scorches = b.scorch_boxes;
    o.shimmer = b.shimmer_count;
    return o;
}
rr::WeaponEffectsFrame hull_frame(Target& t, const HullOut& o) {
    rr::WeaponEffectsFrame f = base_frame(t);
    f.sprites = hq_sprites;
    f.sprite_quads = o.quads;
    f.glows = hq_glows;
    f.glow_boxes = o.boxes;
    f.fireballs = hq_fireballs;
    f.fireball_boxes = o.bursts;
    f.fireball_octaves = o.n.octaves;
    f.rings = hq_rings;
    f.ring_quads = o.rings;
    f.filaments = hq_filaments;
    f.filament_boxes = o.filaments;
    f.scorches = hq_scorches;
    f.scorch_boxes = o.scorches;
    return f;
}
// One element kind alone of a build (the others' counts zeroed): 0 sprites, 1 glows, 2 bursts, 3 rings, 4 filaments,
// 5 scorches.
HullOut hull_only(HullOut o, unsigned kind) {
    if (kind != 0) o.quads = 0;
    if (kind != 1) o.boxes = 0;
    if (kind != 2) o.bursts = 0;
    if (kind != 3) o.rings = 0;
    if (kind != 4) o.filaments = 0;
    if (kind != 5) o.scorches = 0;
    return o;
}
// A spark's displacement from its hit point at game age t_s (world = view under the fixture's identity camera, the owner
// still, no shot heading): the stage's own law of the energy class's set, d speed tau (1 - e^(-t / tau)) + w t
// (spark_factors, pair_direction, curl_drift).
void spark_displacement(std::uint32_t seq, unsigned sp, float U, double t_s, double out[3]) {
    const ws::SparkSet ss = law::set();
    const ws::SparkFactors sf = law::factors(seq, sp, ss.speed_spread);
    float d[3];
    ws::pair_direction(seq, sp >> 1, d);
    const float sgn = (sp & 1u) ? -1.f : 1.f;
    for (float& v : d) v *= sgn;
    const double speed = double(ss.speed * U * sf.speed_x);
    float w[3] = {0.f, 0.f, 0.f};
    if (sf.curl) ws::curl_drift(sf.curl_hash, d, w);
    const double F = double(ss.tau) * (1.0 - std::exp(-t_s / double(ss.tau)));
    for (unsigned k = 0; k < 3; ++k) out[k] = double(d[k]) * speed * F + double(w[k]) * speed * t_s;
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
// With `burst` the frame draws that fireball (still, four vertices) instead of the streak.
double resolve_streak(rr::WeaponEffectsPass& pass, Com<IDirect3DTexture9>& motion, Com<IDirect3DSurface9>& motion_s,
                      float speed_px, unsigned after, float* peak_out, double* residual,
                      const rr::WeaponFireballVertex* burst = nullptr) {
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
            ctx.frame.sprite_quads = drawn && !burst ? 1 : 0;
            ctx.frame.fireballs = burst;
            ctx.frame.fireball_boxes = drawn && burst ? 1 : 0;
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
    std::printf("HULL_SLOTS sprite_vs=%u sprite_ps=%u glow_vs=%u glow_ps=%u fireball_vs=%u fireball_ps=%u "
                "sprite_vertex_bytes=%u glow_vertex_bytes=%u fireball_vertex_bytes=%u hull_state_bytes=%u\n",
                pass.caps().sprite_vs_slots, pass.caps().sprite_ps_slots, pass.caps().glow_vs_slots,
                pass.caps().glow_ps_slots, pass.caps().fireball_vs_slots, pass.caps().fireball_ps_slots,
                unsigned(sizeof(rr::WeaponSpriteVertex)), unsigned(sizeof(rr::WeaponGlowVertex)),
                unsigned(sizeof(rr::WeaponFireballVertex)), unsigned(sizeof(ws::HullState)));
    report("hull_programs_attached", pass.caps().hull_enabled && !std::strcmp(pass.caps().hull_reason, "ok") &&
                                         pass.caps().sprite_vs_slots && pass.caps().sprite_ps_slots &&
                                         pass.caps().glow_vs_slots && pass.caps().glow_ps_slots &&
                                         pass.caps().fireball_vs_slots && pass.caps().fireball_ps_slots, "");
    const std::int32_t front[3] = {0, 0, -10000}; // the staged owner's front (r 100 view units), at view z 300
    const float z_hit = 300.f;
    const double ppu = px_per_unit(g, z_hit);
    const double cx = 0.5 * g.w, cy = 0.5 * g.h;
    ws::HullTuning only_flash, only_sparks, only_glow, only_ember;
    only_flash.sparks = only_flash.glow = only_flash.ember = false;
    only_sparks.flash = only_sparks.glow = only_sparks.ember = false;
    only_glow.flash = only_glow.sparks = only_glow.ember = false;
    only_ember.flash = only_ember.sparks = only_ember.glow = false;
    for (ws::HullTuning* t : {&only_flash, &only_sparks, &only_glow, &only_ember})
        t->burst = t->ring = t->filament = t->scorch = t->streak = t->shimmer = false;
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
                const std::uint32_t sq = add_hull_hit(1000, at, damage_of(ks[c]), wc::patch_fighter, 0, wc::table_bullets,
                                                      wc::class_ballistic)->sequence;
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
        const float R = law::glow_r() * hl::r_scale * U * (hl::r_base + hl::r_k * 0.33f);
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
        const float life = ws::element_life_ms(law::glow_ms(), ws::element_hash(e->sequence, 1), wc::stagger, 48.f, &fl);
        draw_lane(g, z_hit, 1.f);
        float at[3];
        const std::uint32_t ages[3] = {0, std::uint32_t(life * 0.5f), std::uint32_t(life) + 1};
        for (unsigned a = 0; a < 3; ++a) {
            const HullOut o = hull_build(hull_view(g, 1000 + ages[a], 16), only_glow);
            at[a] = centre_luma(hull_render(pass, o));
        }
        float heat[3];
        wc::heat_colour(law::glow_t(), heat);
        const float peak = std::max(heat[0], std::max(heat[1], heat[2])) * law::glow_e() * law::glow_t();
        const float half_expect = peak * (1.f - float(ages[1]) / life);
        std::printf("HULL case=glow_decay life_ms=%.1f age0=%.4f expected0=%.4f age_half=%.4f expected_half=%.4f age_end=%.5f\n",
                    double(life), double(at[0]), double(peak), double(at[1]), double(half_expect), double(at[2]));
        report("hull_light_peak_decay_150ms", std::fabs(at[0] - peak) <= 0.01f * peak + 2e-3f &&
                                                   std::fabs(at[1] - half_expect) <= 0.02f * peak + 2e-3f && at[2] == 0.f &&
                                                   life >= 0.8f * law::glow_ms() && life <= 1.2f * law::glow_ms(), "");
        draw_lane(g, 0.f, -1.f);
    }
    // 5. The sparks' cooling: their colour on the heat ramp at 0 / 0.5 / ~1 of a life (vertex colours, 8-bit), and the
    // drawn hue at half life (g / r of the brightest pixel).
    {
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(0.0));
        const float life = std::max(law::spark_life(0.f) * law::factors(e->sequence, 0).life_x, 48.f);
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
    // 6. Sizes, counts, reach and lives per tier at k 0 / 0.5 / 1 against the law (5 %): the spark count (round(8 + 16 k)
    // x (1 +- 0.4) by the record's hash: within the spread and equal to spark_count), the flash's halo flash_halo_r
    // r_hit, glow R 3 r_hit, ember R 0.6 r_hit, each spark's width spark_width_u U x its own factor (0.8..1.2); the
    // sparks' mean displacement at 200 ms against d speed tau (1 - e^(-t / tau)) + w t over their own speeds, directions
    // and curls; the reach law's asymptote 2.7 U (20 %); the sparks all live at 0.7 x their law and none past 1.3 x.
    {
        bool sizes_ok = true, reach_ok = true, lives_ok = true;
        const double asymptote_u = double(law::set().speed * law::set().tau);
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
                const unsigned want_n = law::sparks_of(*e, kf);
                const float law_n = law::set().n0 + law::set().nk * kf;
                const bool n_in_spread = float(want_n) >= law_n * (1.f - hl::spark_count_spread) - 0.5f &&
                                         float(want_n) <= law_n * (1.f + hl::spark_count_spread) + 0.5f;
                float flash_r = 0, glow_R = 0, ember_R = 0, width = 0, width_err = 0;
                unsigned spark_q = 0;
                for (unsigned q = 0; q < o.quads; ++q)
                    if (is_flash(q))
                        flash_r = hq_sprites[4 * q].b[3];
                    else {
                        width = hq_sprites[4 * q].b[3];
                        const float want_w = law::set().width * Ut * law::factors(seq, spark_q++).width_x;
                        width_err = std::max(width_err, std::fabs(width - want_w) / want_w);
                    }
                for (unsigned b = 0; b < o.boxes; ++b) {
                    const float R = hq_glows[4 * b].light[3];
                    if (R > 2.f * r_hit) glow_R = R;
                    else ember_R = R;
                }
                const bool s_ok = o.n.sparks == want_n && n_in_spread && o.n.flashes == 1 &&
                                  std::fabs(flash_r - law::flash_halo_r() * r_hit) <= 0.05f * law::flash_halo_r() * r_hit &&
                                  std::fabs(glow_R - law::glow_r() * r_hit) <= 0.01f * glow_R &&
                                  std::fabs(ember_R - law::ember_r() * r_hit) <= 0.01f * ember_R && width_err <= 0.01f;
                // The reach at 200 ms: each spark's head against its law.
                const HullOut o2 = hull_build(hull_view(g, 1100, 16), only_sparks);
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
                unsigned counted = 0;
                for (unsigned sp = 0; sp < want_n; ++sp) {
                    const float life = std::max((law::spark_life(kf)) *
                                                    law::factors(seq, sp).life_x, 48.f);
                    if (!(100.f < life)) continue;
                    double dsp[3];
                    spark_displacement(seq, sp, Ut, 0.1, dsp);
                    want += std::sqrt(dsp[0] * dsp[0] + dsp[1] * dsp[1] + dsp[2] * dsp[2]);
                    ++counted;
                }
                const bool r_ok = m == counted && m > 0 && std::fabs(got - want) <= 0.05 * want;
                // Lives: every spark at 0.7 x its law, none past 1.3 x.
                const float law = law::spark_life(kf);
                const HullOut early = hull_build(hull_view(g, 1000 + std::uint32_t(hl::spark_life_lo * law) - 1, 16), only_sparks);
                const HullOut late = hull_build(hull_view(g, 1000 + std::uint32_t(hl::spark_life_hi * law) + 1, 16), only_sparks);
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
        report("hull_spark_reach_law_100ms", reach_ok, "");
        // the energy class's spatter (hull-impact-classes.md 4.1: 3 U/s, tau 0.45 s): v0 tau 1.35 U
        report("hull_spark_reach_asymptote_energy_1_35u", std::fabs(asymptote_u - 1.35) <= 0.27, "");
        report("hull_spark_lives_law", lives_ok, "");
    }
    // 7. The pixel floors at 2 km: every spark quad at least 3 px wide and 24 px long, the flash's core 3 px across.
    {
        hull_owner(10100.f, 100.f); // the hit 2 km away
        add_hull_hit(1000, front, damage_of(0.33));
        hull_build(hull_view(g, 1000, 16));
        const HullOut o = hull_build(hull_view(g, 1050, 16));
        double min_w = 1e9, min_l = 1e9, core_px = 0, min_l_over_x = 1e9;
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
            min_l_over_x = std::min(min_l_over_x, p.length / double(std::fabs(hq_sprites[4 * q].c[0])));
        }
        std::printf("HULL case=floors_2km sparks=%u min_width_px=%.3f min_length_px=%.3f min_length_over_len_x_px=%.3f "
                    "flash_core_px=%.3f\n", o.n.sparks, min_w, min_l, min_l_over_x, core_px);
        // The floors before each spark's length factor (0.6..1.6): every streak at least floor_len_px x its factor.
        report("hull_streak_min_footprint_3px_24px", o.n.sparks > 0 && min_w >= 3.0 - 1e-3 && min_l_over_x >= 24.0 - 1e-2 &&
                                                         min_l >= 24.0 * double(hl::spark_len_lo) - 1e-2 &&
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
        const float ember_life = ws::element_life_ms(law::ember_ms(wc::calibre_of(e->damage)), ws::element_hash(e->sequence, 2),
                                                     wc::stagger, 300.f, &fl);
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
            static ws::HullState before; // static: 529 KB at 1024 records, off the fixture stack
            before = hq_state;
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
        bool gone[4] = {false, false, false, false}, seen[4] = {false, false, false, false}; // flash, glow, ember, burst
        std::uint32_t now = 1000, step = 16;
        while (now <= 3000) {
            const HullOut o = hull_build(hull_view(g, now, step));
            const unsigned counts[4] = {o.n.flashes, o.n.glows, o.n.embers, o.n.bursts};
            for (unsigned k = 0; k < 4; ++k) { // the burst's first frame has R 0 (nothing drawn): tracked once seen
                if (gone[k] && counts[k]) ++reappeared;
                if (counts[k]) seen[k] = true;
                else if (seen[k] || k < 3) gone[k] = true;
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
        report("hull_floor_latched_seta_engage_nothing_reappears",
               reappeared == 0 && gone[0] && gone[1] && gone[2] && seen[3] && gone[3], "");
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
                // a record leaves the ring at its anatomy's end (core::hull_lives): its latched first frame and floor
                // are kept here, so the law still says "nothing" after it went
                static std::uint32_t first_of[6];
                static float floor_of[6];
                const ws::HullSlot* hs = nullptr;
                for (unsigned i = 0; i < wc::ring_capacity; ++i)
                    if (hq_state.slots[i].sequence == seqs[r]) hs = &hq_state.slots[i];
                if (hs) {
                    first_of[r] = hs->first_ms;
                    floor_of[r] = hs->floor_ms;
                }
                const float age = float(now - first_of[r]);
                for (unsigned el = 0; el < 3; ++el) { // 0 flash, 1 glow, 2 ember
                    bool fl = false;
                    const float law = el == 0 ? law::flash_ms() : el == 1 ? law::glow_ms()
                                                                          : law::ember_ms(wc::calibre_of(damage_of(0.33)));
                    const float life = ws::element_life_ms(law, ws::element_hash(seqs[r], el), wc::stagger, floor_of[r], &fl);
                    const float fade = 1.f - age / life;
                    float want = -1.f, got = -1.f;
                    if (age < life) {
                        if (el == 0) want = wc::look::core_e * fade;
                        else if (el == 1) {
                            float c[3];
                            wc::heat_colour(law::glow_t(), c);
                            want = c[0] * law::glow_e() * law::glow_t() * fade;
                        } else {
                            const float T = law::ember_t() * fade;
                            float c[3];
                            wc::heat_colour(T, c);
                            want = c[0] * law::ember_e() * T;
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
                        const float R = (el == 1 ? law::glow_r() : law::ember_r()) * rr;
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
        hull_build(hull_view(g, 1000, 16));
        const HullOut o = hull_build(hull_view(g, 1030, 16));
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
                   drawn.second.patches == 1 && drawn.second.sprite_quads == 0 && drawn.second.glow_boxes == 0 &&
                   drawn.second.fireball_boxes == 0 && drawn.second.ring_quads == 0 && o.bursts > 0 && sum > 1.0,
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
        const double ext[3] = {20.0, 8.0, 60.0};
        const HullLane hull = staged_hull(ext);
        draw_hull_lane(g, hull);
        unsigned outside_of[2] = {0, 0}, lit_of[2] = {0, 0}, sparks_of[2] = {0, 0}, puffs = 0;
        for (unsigned which = 0; which < 2; ++which) { // 0 energy: the puff and the spatter; 1 ballistic: its sparks
            for (unsigned r = 0; r < wc::ring_capacity; ++r) stage_ring.slots[r].sequence = 0;
            wc::ring_clear(stage_ring);
            ws::hull_clear(hq_state);
            add_hull_hit(1000, nose, damage_of(0.33), wc::patch_fighter, 0, wc::table_bullets,
                         which ? wc::class_ballistic : wc::class_energy);
            ws::HullTuning tune = only_sparks;
            tune.burst = which == 0;
            hull_build(hull_view(g, 1000, 16), tune);
            const HullOut o = hull_build(hull_view(g, 1200, 16), tune);
            const std::vector<float> l = hull_render(pass, o);
            for (std::size_t i = 0; i < l.size(); i += 1) {
                if (!(l[i] > 1e-3f)) continue;
                ++lit_of[which];
                double p[3];
                if (!hull_point(g, hull, unsigned(i % g.w), unsigned(i / g.w), p)) ++outside_of[which];
            }
            sparks_of[which] = o.n.sparks;
            if (!which) puffs = o.bursts;
        }
        std::printf("HULL case=chase_nose age_ms=200 energy_sparks=%u energy_puffs=%u energy_lit=%u energy_outside_lit=%u "
                    "ballistic_sparks=%u ballistic_lit=%u ballistic_outside_lit=%u\n",
                    sparks_of[0], puffs, lit_of[0], outside_of[0], sparks_of[1], lit_of[1], outside_of[1]);
        report("hull_chase_nose_lit_outside_outline", outside_of[0] >= 20 && sparks_of[1] > 0 && outside_of[1] >= 20, "");
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
            static ws::HullState st; // static: 529 KB at 1024 records, off the fixture stack
            st = hq_state;
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
                            std::fabs(tug_R - law::glow_r(wc::hull::an_tug) * hl::r_scale * U * (hl::r_base + hl::r_k * 0.1f)) <=
                                1e-3f * tug_R &&
                            hq_glows[0].colour[2] > hq_glows[0].colour[0];
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.5), wc::patch_corvette, 0, wc::table_missiles);
        const HullOut mo = hull_build(hull_view(g, 1000, 16));
        double halo[3] = {0, 0, 0};
        for (unsigned q = 0; q < mo.quads; ++q)
            if (is_flash(q)) colour_of(hq_sprites[4 * q].colour, halo);
        float mt[3];
        wc::tint_linear(wc::missile_tint_rgb, mt);
        const wc::Event* missile_e = nullptr;
        for (unsigned i = 0; i < wc::ring_capacity; ++i)
            if (stage_ring.slots[i].sequence) missile_e = &stage_ring.slots[i];
        const unsigned want_m = missile_e ? law::sparks_lit(*missile_e, wc::calibre_of(missile_e->damage), 0.f, 48.f) : 0;
        const bool missile_ok = mo.n.sparks == want_m && mo.n.flashes == 1 && std::fabs(halo[1] - double(mt[1])) <= 0.01;
        hull_owner(400.f, 100.f);
        add_hull_hit(1000, front, damage_of(0.5), wc::patch_fighter, wc::detail_asteroid);
        const HullOut ao = hull_build(hull_view(g, 1000, 16));
        double dust[3] = {0, 0, 0};
        if (ao.quads) colour_of(hq_sprites[0].colour, dust);
        const wc::Event* rock_e = nullptr;
        for (unsigned i = 0; i < wc::ring_capacity; ++i)
            if (stage_ring.slots[i].sequence) rock_e = &stage_ring.slots[i];
        const unsigned want_a = rock_e ? law::sparks_of(*rock_e, wc::calibre_of(rock_e->damage)) : 0;
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
            double spray_x = 0;
            unsigned alive = 0;
            const float law_life = law::spark_life(wc::calibre_of(first->damage));
            for (unsigned sp = 0; sp < law::sparks_of(*first, wc::calibre_of(first->damage)); ++sp) {
                if (!(100.f < std::max(law_life * law::factors(first->sequence, sp).life_x, 48.f))) continue;
                double dsp[3];
                spark_displacement(first->sequence, sp, U, 0.1, dsp);
                spray_x += dsp[0];
                ++alive;
            }
            spray_x = alive ? spray_x / alive : 0.0;
            const double expect_x = 20.0 + double(want) * double(ws::spark_reach_factor(0.1f, law::set().tau)) + spray_x;
            const unsigned want_sparks = alive;
            std::printf("HULL case=velocity_first_hit v=%.3f want=%.3f sparks=%u mean_head_x=%.3f expected_x=%.3f "
                        "owner_x=%.3f\n", double(v0), double(want), o.n.sparks, mean_x, expect_x, 20.0 + 125.0);
            report("velocity_first_hit_from_previous_frame_rows", std::fabs(v0 - want) <= 1e-3f * want &&
                                                                  o.n.sparks == want_sparks &&
                                                                  std::fabs(mean_x - expect_x) <= 0.02 * (expect_x - 20.0), "");
        }
    }
    // 15. The shared budget: past the screens the patches left, other owners' glows shed oldest first, the own ship's kept,
    // sparks and flashes never.
    {
        hull_owner(400.f, 100.f);
        for (unsigned i = 0; i < 12; ++i) add_hull_hit(1000 + 5 * i, front, damage_of(1.0), wc::patch_capital);
        hull_build(hull_view(g, 1060, 16)); // every record latched (a burst's first frame has R 0)
        static ws::HullState st; // static: 529 KB at 1024 records, off the fixture stack
        st = hq_state;
        ws::HullView full = hull_view(g, 1090, 16), tight = full;
        full.budget_left = 1e6f; // nothing shed, the bursts at two octaves
        tight.budget_left = 0.5f;
        const HullOut a = hull_build(full, ws::HullTuning{}, &st);
        const unsigned sparks_full = a.n.sparks, boxes_full = a.boxes;
        st = hq_state;
        const HullOut b = hull_build(tight, ws::HullTuning{}, &st);
        st = hq_state;
        tight.own_node = addr(fake_node);
        const HullOut c = hull_build(tight, ws::HullTuning{}, &st);
        std::printf("HULL case=budget boxes=%u screens=%.3f tight_boxes=%u tight_shed=%u tight_screens=%.3f tight_flash_screens=%.3f "
                    "sparks=%u tight_sparks=%u own_boxes=%u own_shed=%u\n",
                    boxes_full, double(a.n.screens), b.boxes, b.n.shed, double(b.n.screens), double(b.n.flash_screens),
                    sparks_full, b.n.sparks, c.boxes, c.n.shed);
        std::printf("HULL case=budget_bursts bursts=%u burst_screens=%.3f octaves=%u tight_bursts=%u tight_bursts_shed=%u "
                    "tight_octaves=%u own_bursts=%u own_bursts_shed=%u rings=%u tight_rings=%u\n",
                    a.n.bursts, double(a.n.burst_screens), a.n.octaves, b.n.bursts, b.n.bursts_shed, b.n.octaves,
                    c.n.bursts, c.n.bursts_shed, a.n.rings, b.n.rings);
        // the flashes are charged and never shed (hull-impact-classes.md section 6): the sheddable screens within the
        // budget, or nothing sheddable left
        report("hull_budget_shed", b.n.shed > 0 && (b.n.screens - b.n.flash_screens <= 0.5f || (b.boxes == 0 && b.bursts == 0)) &&
                                       b.n.sparks == sparks_full && b.n.flashes == a.n.flashes && c.n.shed == 0 &&
                                       c.boxes == boxes_full, "");
        report("hull_budget_burst_lod_then_shed", a.bursts > 0 && a.n.octaves == 2 && b.n.octaves == 1 && b.n.bursts_shed > 0 &&
                                                      b.bursts + b.n.bursts_shed == a.bursts && b.rings == a.rings &&
                                                      c.bursts == a.bursts && c.n.bursts_shed == 0, "");
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
        hull_build(hull_view(g, 1000, 16)); // latched: the burst drawn at 50 ms
        const HullOut o = hull_build(hull_view(g, 1050, 16));
        draw_lane(g, z_hit, 1.f);
        pass.set_faults(2);
        const auto bad = draw_on(pass, g, hull_frame(g, o));
        pass.set_faults(0);
        const auto good = draw_on(pass, g, hull_frame(g, o));
        report("hull_fault_reported", FAILED(bad.first) && (bad.second.failed == rr::WeaponEffectsStep::Scorches ||
                                                            bad.second.failed == rr::WeaponEffectsStep::Glows ||
                                                            bad.second.failed == rr::WeaponEffectsStep::Sprites), "");
        report("hull_recovers_after_fault", SUCCEEDED(good.first) && good.second.drew && good.second.sprite_quads == o.quads &&
                                                good.second.glow_boxes == o.boxes && o.bursts > 0 &&
                                                good.second.fireball_boxes == o.bursts, "");
        // A fault in the fireball draw alone (the glows absent): reported at its step, the next frame draws it.
        {
            const HullOut fo = hull_only(o, 2);
            pass.set_faults(2);
            const auto fbad = draw_on(pass, g, hull_frame(g, fo));
            pass.set_faults(0);
            const auto fgood = draw_on(pass, g, hull_frame(g, fo));
            report("hull_fireball_fault_reported_and_recovers",
                   FAILED(fbad.first) && fbad.second.failed == rr::WeaponEffectsStep::Fireballs && SUCCEEDED(fgood.first) &&
                       fgood.second.fireball_boxes == fo.bursts && fo.bursts > 0, "");
        }
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
            // the scorch store is CPU state: its mark draws again after the Reset (the multiply blend recreated)
            drew = SUCCEEDED(after.first) && after.second.drew && after.second.sprite_quads == o.quads &&
                   after.second.fireball_boxes == o.bursts && o.scorches > 0 && after.second.scorch_boxes == o.scorches &&
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
    // 21. The hot-gas burst (2026-11-06, look 3.1): one IRE hit (k 0.33) on a fighter's front at view z 300, the burst
    // alone. (a) Its radius against R(t) = burst_r r_hit (1 - e^(-t / (burst_rise L))) at 0.1 / 0.3 / 0.6 of its life
    // (the vertex's sphere, and the drawn lit extent on the sky against the projected radius); (b) its colour on the
    // heat ramp: the brightest pixel white at its first drawn frame, dark red at 0.9 of its life; (c) the lane: a plane
    // through the hit hides the half inside the hull, a plane in front of the whole sphere hides it all, one behind it
    // nothing.
    {
        ws::HullTuning only_burst = only_flash;
        only_burst.flash = false;
        only_burst.burst = true;
        draw_lane(g, 0.f, -1.f);
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(0.33));
        const float kf = wc::calibre_of(e->damage);
        const float r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * kf);
        bool fl = false;
        const float life = ws::element_life_ms(law::puff_ms(kf), ws::element_hash(e->sequence, 3), wc::stagger, 48.f, &fl);
        hull_build(hull_view(g, 1000, 16), only_burst); // the first frame: R 0, nothing drawn
        const double fracs[3] = {0.1, 0.3, 0.6};
        double worst_r = 0, worst_extent = 0, best_extent = 1e9, worst_t = 0;
        bool law_ok = true;
        for (unsigned a = 0; a < 3; ++a) {
            const std::uint32_t age = std::uint32_t(fracs[a] * double(life) + 0.5);
            const HullOut o = hull_build(hull_view(g, 1000 + age, 16), only_burst);
            const double want = double(law::puff_r()) * double(r_hit) *
                                (1.0 - std::exp(-double(age) / (double(law::puff_rise()) * double(life))));
            const double got = o.bursts ? double(hq_fireballs[0].sphere[3]) : 0.0;
            const double t_err = o.bursts ? std::fabs(double(hq_fireballs[0].look[0]) - (1.0 - double(age) / double(life))) : 1.0;
            const std::vector<float> l = hull_render(pass, o);
            double extent = 0;
            for (std::size_t i = 0; i < l.size(); ++i)
                if (l[i] > 1e-4f) extent = std::max(extent, std::hypot(double(i % g.w) + 0.5 - cx, double(i / g.w) + 0.5 - cy));
            const double r_px = want * ppu;
            std::printf("HULL case=burst_radius age_frac=%.1f age_ms=%u life_ms=%.1f R_view=%.4f law_view=%.4f r_hit=%.3f "
                        "R_px=%.1f lit_extent_px=%.1f extent_over_R=%.4f T=%.4f lit=%u\n",
                        fracs[a], age, double(life), got, want, double(r_hit), r_px, extent, r_px > 0 ? extent / r_px : 0.0,
                        o.bursts ? double(hq_fireballs[0].look[0]) : -1.0, lit_count(l));
            law_ok = law_ok && o.bursts == 1 && std::fabs(got - want) <= 1e-3 * want;
            worst_r = std::max(worst_r, std::fabs(got - want) / want);
            worst_t = std::max(worst_t, t_err);
            worst_extent = std::max(worst_extent, extent / r_px);
            best_extent = std::min(best_extent, extent / r_px);
        }
        report("hull_burst_radius_law_3_ages", law_ok && worst_t <= 1e-5 && best_extent >= 0.75 && worst_extent <= 1.03, "");
        // (b) the colour: the brightest pixel at the first drawn frame (16 ms) and at 0.9 of the life.
        double gr[2] = {0, 0}, br[2] = {0, 0}, peak[2] = {0, 0};
        const std::uint32_t ages[2] = {16u, std::uint32_t(0.9 * double(life))};
        for (unsigned a = 0; a < 2; ++a) {
            const HullOut o = hull_build(hull_view(g, 1000 + ages[a], 16), only_burst);
            std::vector<float> rgb;
            hull_render(pass, o, &rgb);
            std::size_t best = 0;
            for (std::size_t i = 0; i < rgb.size() / 3; ++i)
                if (rgb[i * 3] > rgb[best * 3]) best = i;
            peak[a] = rgb.empty() ? 0.0 : double(rgb[best * 3]);
            gr[a] = peak[a] > 0 ? double(rgb[best * 3 + 1]) / peak[a] : -1;
            br[a] = peak[a] > 0 ? double(rgb[best * 3 + 2]) / peak[a] : -1;
        }
        std::printf("HULL case=burst_colour birth_age_ms=%u birth_r=%.4f birth_g_over_r=%.3f birth_b_over_r=%.3f "
                    "end_age_ms=%u end_r=%.5f end_g_over_r=%.3f end_b_over_r=%.3f\n",
                    ages[0], peak[0], gr[0], br[0], ages[1], peak[1], gr[1], br[1]);
        report("hull_burst_colour_white_core_at_birth", peak[0] > 0.1 && gr[0] >= 0.75 && br[0] >= 0.45, "");
        report("hull_burst_colour_dark_red_at_end", peak[1] > 0 && peak[1] <= 0.1 * peak[0] && gr[1] <= 0.3 && br[1] <= 0.2, "");
        // (c) the lane.
        const std::uint32_t age = std::uint32_t(0.3 * double(life));
        const HullOut o = hull_build(hull_view(g, 1000 + age, 16), only_burst);
        const float R = o.bursts ? hq_fireballs[0].sphere[3] : 0.f;
        draw_lane(g, 0.f, -1.f);
        const double sky = total(hull_render(pass, o));
        draw_lane(g, z_hit, 1.f);
        const double at_hit = total(hull_render(pass, o));
        draw_lane(g, z_hit - R - 2.f, 1.f);
        const double in_front = total(hull_render(pass, o));
        draw_lane(g, z_hit + R + 50.f, 1.f);
        const double behind = total(hull_render(pass, o));
        draw_lane(g, 0.f, -1.f);
        std::printf("HULL case=burst_lane sky=%.3f at_hit=%.3f in_front=%.4f behind=%.3f hit_share=%.4f front_share=%.5f "
                    "behind_share=%.4f\n", sky, at_hit, in_front, behind, sky > 0 ? at_hit / sky : 0.0,
                    sky > 0 ? in_front / sky : 0.0, sky > 0 ? behind / sky : 0.0);
        // at the hit: the front half's chord (its noise sampled at that half's middle: a half within the noise's spread)
        report("hull_burst_half_hidden_by_lane", sky > 0 && at_hit / sky >= 0.35 && at_hit / sky <= 0.65 &&
                                                     in_front <= 1e-3 * sky && behind >= 0.99 * sky, "");
    }
    // 22. Randomised sparks: two hits of the same calibre at the same point (different sequences) differ in count and in
    // their direction sets (each drawn spark's head less the hit point, 100 ms in); the same hit's quads are identical
    // when built twice for one frame, and its uncurled sparks keep their directions from one frame to the next; the
    // curled share and the curl's bend (100 to 300 ms) are reported.
    {
        draw_lane(g, 0.f, -1.f);
        const double k = 0.5;
        auto directions = [&](std::uint32_t* seq_out, unsigned* count_out, std::vector<std::array<double, 3>>* dirs,
                              unsigned extra) {
            hull_owner(400.f, 100.f);
            for (unsigned i = 0; i < extra; ++i) wc::ring_release(stage_ring, unsigned(add_hull_hit(1000, front, damage_of(k)) - stage_ring.slots));
            const wc::Event* e = add_hull_hit(1000, front, damage_of(k));
            *seq_out = e->sequence;
            *count_out = law::sparks_of(*e, wc::calibre_of(e->damage));
            hull_build(hull_view(g, 1000, 16), only_sparks);
            const HullOut o = hull_build(hull_view(g, 1100, 16), only_sparks);
            dirs->clear();
            for (unsigned q = 0; q < o.quads; ++q) {
                const QuadPx p = quad_px(g, q);
                const double v[3] = {(p.hx / double(g.w) * 2.0 - 1.0) / 1.6 * p.hz,
                                     (1.0 - p.hy / double(g.h) * 2.0) / (1.6 * double(g.w) / double(g.h)) * p.hz, p.hz - z_hit};
                const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                dirs->push_back({v[0] / n, v[1] / n, v[2] / n});
            }
            return o;
        };
        std::uint32_t seq_a = 0, seq_b = 0;
        unsigned n_a = 0, n_b = 0, extra = 0;
        std::vector<std::array<double, 3>> da, db;
        directions(&seq_a, &n_a, &da, 0);
        do directions(&seq_b, &n_b, &db, ++extra);
        while (n_b == n_a && extra < 8);
        double closest_worst = 1; // over A's directions, the best match in B; the set differs when some has none
        for (const auto& a : da) {
            double best = -1;
            for (const auto& b : db) best = std::max(best, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
            closest_worst = std::min(closest_worst, best);
        }
        // The same hit: twice for one frame (identical bytes), and its directions one frame later.
        hull_owner(400.f, 100.f);
        const wc::Event* e = add_hull_hit(1000, front, damage_of(k));
        const std::uint32_t seq = e->sequence;
        const unsigned n = law::sparks_of(*e, wc::calibre_of(e->damage));
        hull_build(hull_view(g, 1000, 16), only_sparks);
        static ws::HullState s1, s2; // static: 529 KB each at 1024 records
        s1 = hq_state;
        s2 = hq_state;
        const HullOut o1 = hull_build(hull_view(g, 1100, 16), only_sparks, &s1);
        std::vector<rr::WeaponSpriteVertex> copy(hq_sprites, hq_sprites + 4 * o1.quads);
        const HullOut o2 = hull_build(hull_view(g, 1100, 16), only_sparks, &s2);
        const bool identical = o1.quads == o2.quads && o1.quads > 0 &&
                               !std::memcmp(copy.data(), hq_sprites, sizeof(rr::WeaponSpriteVertex) * 4 * o1.quads);
        double straight_worst = 1, curl_min_deg = 1e9;
        unsigned curled = 0, total_sparks = 0;
        {
            std::vector<std::array<double, 3>> d1, d2;
            for (std::uint32_t t : {1100u, 1116u, 1250u}) {
                static ws::HullState st; // static: 529 KB at 1024 records, off the fixture stack
                st = s1;
                const HullOut o = hull_build(hull_view(g, t, 16), only_sparks, &st);
                std::vector<std::array<double, 3>>& d = t == 1100u ? d1 : d2;
                if (t == 1250u) { // the curl's bend: 100 -> 250 ms, curled sparks only (every spark alive at 250 ms, k 0.5)
                    for (unsigned q = 0; q < o.quads && q < d1.size(); ++q) {
                        const QuadPx p = quad_px(g, q);
                        const double v[3] = {(p.hx / double(g.w) * 2.0 - 1.0) / 1.6 * p.hz,
                                             (1.0 - p.hy / double(g.h) * 2.0) / (1.6 * double(g.w) / double(g.h)) * p.hz,
                                             p.hz - z_hit};
                        const double m = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                        if (o.quads != o1.quads || !law::factors(seq, q).curl) continue;
                        const double c = (v[0] * d1[q][0] + v[1] * d1[q][1] + v[2] * d1[q][2]) / m;
                        curl_min_deg = std::min(curl_min_deg, std::acos(std::min(1.0, c)) * 57.29578);
                    }
                    continue;
                }
                d.clear();
                for (unsigned q = 0; q < o.quads; ++q) {
                    const QuadPx p = quad_px(g, q);
                    const double v[3] = {(p.hx / double(g.w) * 2.0 - 1.0) / 1.6 * p.hz,
                                         (1.0 - p.hy / double(g.h) * 2.0) / (1.6 * double(g.w) / double(g.h)) * p.hz,
                                         p.hz - z_hit};
                    const double m = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    d.push_back({v[0] / m, v[1] / m, v[2] / m});
                }
            }
            for (unsigned q = 0; q < n && q < d1.size() && q < d2.size(); ++q) {
                const bool c = law::factors(seq, q).curl;
                curled += c;
                ++total_sparks;
                if (!c) straight_worst = std::min(straight_worst, d1[q][0] * d2[q][0] + d1[q][1] * d2[q][1] + d1[q][2] * d2[q][2]);
            }
        }
        // The curled share over many records (the hash's spread).
        unsigned curl_all = 0, sparks_all = 0;
        for (std::uint32_t q = 1; q <= 400; ++q)
            for (unsigned sp = 0; sp < 16; ++sp) {
                curl_all += law::factors(q, sp).curl;
                ++sparks_all;
            }
        std::printf("HULL case=spark_random seq_a=%u count_a=%u drawn_a=%u seq_b=%u count_b=%u drawn_b=%u "
                    "a_in_b_worst_cos=%.4f identical_same_frame=%u count=%u drawn=%u straight_worst_cos=%.6f curled=%u/%u "
                    "curl_min_bend_deg_100_250ms=%.2f curl_share_6400=%.4f\n",
                    seq_a, n_a, unsigned(da.size()), seq_b, n_b, unsigned(db.size()), closest_worst, unsigned(identical), n,
                    o1.quads, straight_worst, curled, total_sparks, curl_min_deg < 1e8 ? curl_min_deg : -1.0,
                    double(curl_all) / double(sparks_all));
        report("hull_spark_random_hits_differ", n_a != n_b && da.size() == n_a && db.size() == n_b && closest_worst < 0.99, "");
        report("hull_spark_same_hit_stable", identical && o1.quads == n && straight_worst >= 0.9999, "");
        report("hull_spark_curl_share", std::fabs(double(curl_all) / double(sparks_all) - double(hl::spark_curl_share)) <= 0.02,
               "");
    }
    // 23, 24 (the tangent-plane ring, the composition): superseded by the classes' surface ring and their per-class
    // composition (weapon_effects_class_cases.h).
    // 25. The burst through the production TemporalPass (resolve_core's configuration): a still burst (T 0.7) over the
    // whole screen quad, 16 frames: the core's energy survives (>= 0.9), and 8 frames after it went no residual above
    // one code.
    {
        Com<IDirect3DTexture9> motion;
        Com<IDirect3DSurface9> motion_s;
        const bool made = SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                       D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                          SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
        rr::WeaponFireballVertex fb[4];
        const ws::HullView hv = hull_view(g, 0, 16);
        const float c[3] = {0.f, 0.f, 300.f}, rect[4] = {-1.f, -1.f, 1.f, 1.f};
        const float look[4] = {0.7f, law::puff_e(), 0.3f, 3.f};
        const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
        ws::fireball_quad(hv, c, 25.f, rect, true, look, no_tint, fb);
        float peak = 0.f;
        double residual = 1;
        const double ratio = made ? resolve_streak(pass, motion, motion_s, 0.f, 8, &peak, &residual, fb) : -1.0;
        std::printf("HULL case=resolve_burst frames=16 core=%.4f peak=%.3f residual_after_8=%.5f\n", ratio, double(peak),
                    residual);
        report("hull_resolve_burst_survival", ratio >= 0.9, "");
        report("hull_resolve_burst_no_scar", ratio >= 0 && residual <= 1.0 / 255.0, "");
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
                static ws::HullState st; // static: 529 KB at 1024 records, off the fixture stack
                st = hq_state;
                QueryPerformanceCounter(&a);
                o = hull_build(hull_view(g, 1000 + 4 * hits + 16, 16), ws::HullTuning{}, &st);
                QueryPerformanceCounter(&b);
                const double us = ticks_us(a, b, freq);
                best = std::min(best, us);
                sum += us;
            }
            std::printf("HULL_CPU case=%s hits=%u sparks=%u flashes=%u boxes=%u bursts=%u rings=%u best_us=%.2f mean_us=%.2f "
                        "target_us=40\n", c ? "fighter_20_hits" : "capital_64_hits", hits, o.n.sparks, o.n.flashes, o.boxes,
                        o.bursts, o.rings, best, sum / 200.0);
            // the same frame by element (report only): the sparks alone, everything but the sparks
            for (unsigned part = 0; part < 2 && !c; ++part) {
                ws::HullTuning pt;
                if (part == 0) pt.flash = pt.glow = pt.ember = pt.burst = pt.ring = false;
                else pt.sparks = false;
                double pbest = 1e30;
                for (unsigned i = 0; i < 200; ++i) {
                    static ws::HullState st; // static: 529 KB at 1024 records, off the fixture stack
                    st = hq_state;
                    QueryPerformanceCounter(&a);
                    hull_build(hull_view(g, 1000 + 4 * hits + 16, 16), pt, &st);
                    QueryPerformanceCounter(&b);
                    pbest = std::min(pbest, ticks_us(a, b, freq));
                }
                std::printf("HULL_CPU case=capital_64_hits_%s best_us=%.2f\n", part ? "no_sparks" : "sparks_only", pbest);
            }
            // 30 us for the flash, sparks, glow and ember (26.6 measured before the burst, 2026-11-06); 64 bursts and 64
            // rings (a heavy PPC) add their element law, the sparks' hashes their factors (33.6 measured after the
            // performance pass)
            if (!c) report("hull_cpu_64_hits", best <= 40.0, "");
        }
        // 19b. The 1024-record ring (2026-11-07): the full ring of stage-owned hull hits, the visible ones on capitals in
        // view (64 hits each, t0 1000 + 4 i) and the rest on capitals behind the camera (16 hits each, older newest hits,
        // so the visible capitals are among hull::max_owners kept and 31 or 28 hidden ones are re-posed for nothing):
        // build_hull with the drawn-owner set and the overflow stamp (core::ring_stamp_hidden), best and mean of 200
        // from the latched state. 64 visible hits: one capital; 256: four, the shed order past the old ring's count.
        {
            struct FarOwner {
                unsigned char obj[0x100], node[0x200];
            };
            static FarOwner far_owners[wc::ring_capacity]; // 768 KB: up to one owner per record
            static wc::Ring full_ring, timed_ring;
            static ws::HullState full_state, timed_state;
            const ws::Reader rd = [](std::uintptr_t at, void* out, std::size_t n) {
                const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&far_owners[0]), size = sizeof far_owners;
                if (at >= base && n <= size && at - base <= size - n) {
                    std::memcpy(out, reinterpret_cast<const void*>(at), n);
                    return true;
                }
                return fake_read(at, out, n);
            };
            const auto engine = [](float v) { return std::uint32_t(std::int32_t(v / wc::world_scale)); };
            auto make = [&](unsigned k, float x, float z) {
                FarOwner& o = far_owners[k];
                std::memset(&o, 0, sizeof o);
                put32(o.obj, wc::layout::obj_node, addr(o.node));
                put32(o.obj, wc::layout::obj_radius, engine(2000.f));
                put32(o.node, wc::layout::node_handle, 0xf000u + k);
                put32(o.node, wc::layout::node_pos + 0, engine(x));
                put32(o.node, wc::layout::node_pos + 8, engine(z));
                for (unsigned i = 0; i < 3; ++i) put32(o.node, wc::layout::node_rot + 16 * i + 4 * i, 0x10000u);
            };
            auto hit = [&](unsigned k, unsigned i) {
                wc::Event* e = wc::ring_acquire(full_ring);
                const std::uint32_t seq = e->sequence;
                std::memset(e, 0, sizeof *e);
                e->sequence = seq;
                e->role = wc::role_hull_hit;
                e->style = wc::shape_slug;
                e->flags = std::uint8_t(wc::flag_stage);
                e->table = wc::table_bullets;
                e->tint_rgb = 0xffffffu | wc::rgb_present;
                e->patch_tier = wc::patch_capital;
                e->tier_source = wc::tier_by_radius;
                e->t0_game_ms = 1000 + 4 * i;
                e->lifetime_ms = std::uint16_t(wc::hull_lifetime_ms);
                e->damage = damage_of(0.8);
                e->detail = std::uint8_t(wc::class_energy << wc::detail_class_shift);
                e->owner = addr(far_owners[k].obj);
                e->node = addr(far_owners[k].node);
                e->handle = 0xf000u + k;
                const double r = 60000.0 * std::sqrt(halton(i + 1, 2)), ang = 6.2831853 * halton(i + 1, 3);
                e->pos[0] = std::int32_t(r * std::cos(ang));
                e->pos[1] = std::int32_t(r * std::sin(ang));
                e->pos[2] = -200000;
                e->radius = std::int32_t(engine(2000.f));
            };
            for (unsigned visible : {1u, 4u}) {
                const unsigned hidden_owners = (wc::ring_capacity - 64 * visible) / 16;
                wc::ring_clear(full_ring);
                ws::hull_clear(full_state);
                // the visible capital at 4 km (capital_64_hits' distance), or four across the view at 2.6 km (x -1.2 ..
                // 1.2 km: the capital-close budget case's distance, so the shed order engages); the hidden ones 20 km
                // behind the camera
                for (unsigned k = 0; k < visible; ++k)
                    make(k, visible == 1 ? 0.f : -1200.f + 800.f * float(k), visible == 1 ? 4000.f : 2600.f);
                for (unsigned k = visible; k < visible + hidden_owners; ++k)
                    make(k, (float(k) - 32.f) * 5000.f, -20000.f);
                for (unsigned i = 0; i < 64; ++i)
                    for (unsigned k = 0; k < visible + hidden_owners; ++k)
                        if (k < visible || i < 16) hit(k, i);
                const std::uint32_t now = 1000 + 4 * 64;
                { // the first frame latches and stamps (the timed frames order their owners by it: visible first)
                    static wc::VisibleOwners first;
                    first.reset(0.f);
                    hull_build(hull_view(g, now, 16), ws::HullTuning{}, &full_state, &full_ring, rd, &first);
                    wc::ring_stamp_hidden(full_ring, first);
                }
                double best = 1e30, sum = 0;
                HullOut o;
                unsigned drawn_owners = 0, hidden = 0, live = 0;
                for (unsigned rep = 0; rep < 200; ++rep) {
                    timed_ring = full_ring;
                    timed_state = full_state;
                    static wc::VisibleOwners vis;
                    vis.reset(0.f);
                    QueryPerformanceCounter(&a);
                    o = hull_build(hull_view(g, now + 16, 16), ws::HullTuning{}, &timed_state, &timed_ring, rd, &vis);
                    wc::ring_stamp_hidden(timed_ring, vis);
                    QueryPerformanceCounter(&b);
                    const double us = ticks_us(a, b, freq);
                    best = std::min(best, us);
                    sum += us;
                    drawn_owners = vis.count;
                }
                for (unsigned i = 0; i < wc::ring_capacity; ++i) {
                    live += timed_ring.slots[i].sequence != 0;
                    hidden += timed_ring.slots[i].sequence && (timed_ring.hidden[i] & wc::hidden_undrawn);
                }
                const bool v64 = visible == 1;
                const float budget = hull_view(g, now + 16, 16).budget_left;
                const unsigned shed = o.n.shed + o.n.bursts_shed + o.n.flashes_shed + o.n.rings_shed + o.n.scorches_shed +
                                      o.n.filaments_shed + o.n.capped + (o.n.octaves < wc::look::fireball_octaves ? 1u : 0u);
                std::printf("HULL_CPU case=%s live=%u visible_hits=%u hidden=%u drawn_owners=%u hits=%u owners_dropped=%u "
                            "sparks=%u flashes=%u boxes=%u bursts=%u rings=%u shed=%u bursts_shed=%u flashes_shed=%u "
                            "capped=%u octaves=%u screens=%.3f budget=%.3f out_of_reach=%u owners_out_of_reach=%u "
                            "best_us=%.2f mean_us=%.2f target_us=%.0f\n",
                            v64 ? "ring_1024_visible_64" : "ring_1024_visible_256", live, 64 * visible, hidden,
                            drawn_owners, o.n.hits, o.n.owners_dropped, o.n.sparks, o.n.flashes, o.boxes, o.bursts,
                            o.rings, o.n.shed, o.n.bursts_shed, o.n.flashes_shed, o.n.capped, o.n.octaves,
                            double(o.n.screens), double(budget), o.n.out_of_reach, o.n.owners_out_of_reach, best,
                            sum / 200.0,
                            v64 ? ring_1024_cpu_64_us : ring_1024_cpu_256_us);
                report(v64 ? "hull_ring_1024_visible_64_stamp" : "hull_ring_1024_visible_256_stamp",
                       live == wc::ring_capacity && hidden == wc::ring_capacity - 64 * visible && drawn_owners == visible, "");
                report(v64 ? "hull_cpu_ring_1024_visible_64" : "hull_cpu_ring_1024_visible_256",
                       best <= (v64 ? ring_1024_cpu_64_us : ring_1024_cpu_256_us), "");
                if (!v64) report("hull_ring_1024_visible_256_shed_within_budget", shed > 0 && o.n.screens <= budget + 1e-3f, "");
            }
            // Visible first (2026-11-07): 40 owners, 20 on screen (one hit each at t0 1000) and 20 behind the camera with
            // newer hits (t0 1040). The first frame (nothing stamped yet: every owner counts as visible) keeps the 32
            // newest, so 12 on-screen owners; after its stamp the next frame keeps all 20 on-screen owners and fills the
            // other 12 slots behind the camera.
            {
                wc::ring_clear(full_ring);
                ws::hull_clear(full_state);
                for (unsigned k = 0; k < 20; ++k) make(k, -3600.f + 378.9f * float(k), 6000.f);
                for (unsigned k = 20; k < 40; ++k) make(k, (float(k) - 30.f) * 5000.f, -20000.f);
                for (unsigned k = 0; k < 40; ++k) hit(k, k < 20 ? 0 : 10);
                static wc::VisibleOwners vis;
                unsigned drawn[2] = {}, in_view[2] = {};
                for (unsigned frame = 0; frame < 2; ++frame) {
                    vis.reset(0.f);
                    hull_build(hull_view(g, 1100 + 16 * frame, 16), ws::HullTuning{}, &full_state, &full_ring, rd, &vis);
                    wc::ring_stamp_hidden(full_ring, vis);
                    drawn[frame] = vis.count;
                    for (unsigned j = 0; j < vis.in_view_count; ++j) in_view[frame] += vis.in_view[j] != 0;
                }
                unsigned kept_on_screen = 0;
                for (unsigned j = 0; j < vis.count; ++j)
                    for (unsigned k = 0; k < 20; ++k) kept_on_screen += vis.owner[j] == addr(far_owners[k].obj);
                std::printf("HULL case=visible_first owners=40 off_screen=20 cap=%u frame1_drawn=%u frame2_drawn=%u "
                            "frame2_on_screen_kept=%u in_view=%u\n",
                            wc::hull::max_owners, drawn[0], drawn[1], kept_on_screen, in_view[1]);
                report("hull_visible_first_keeps_all_on_screen",
                       drawn[0] == 12 && drawn[1] == 20 && kept_on_screen == 20 && in_view[1] == 20, "");
            }
            // The owners past the caps through the production reader (review 5, 2026-11-07): one frame of both stages
            // (stage::build, stage::build_hull, the stamp) on a full ring of distinct owners, each located once a frame
            // for both stages (core::VisibleOwners::located). 512 owners with a shield hit and a hull hit each (16 on
            // screen at 6 km, the rest behind the camera), and 1024 owners with one hull hit each. Timed through the
            // fixture's reader and through engine_memory::read (src/proxy/engine_memory.cpp as production builds it;
            // next_frame before each frame, so every region is revalidated once per frame as in flight); the reads one
            // frame makes counted through a wrapper.
            {
                static unsigned reads = 0;
                static ws::Reader inner = nullptr;
                const ws::Reader counted = [](std::uintptr_t at, void* out, std::size_t n) {
                    ++reads;
                    return inner(at, out, n);
                };
                const ws::Reader production = [](std::uintptr_t at, void* out, std::size_t n) {
                    return x3m::engine_memory::read(at, out, n);
                };
                static rr::WeaponPatch patches[wc::max_patches];
                static rr::WeaponDecal decals[wc::max_decals];
                ws::Tuning st_tuning{};
                st_tuning.near_z = hull_view(g, 1000, 16).near_z;
                for (unsigned owners : {512u, 1024u}) {
                    const bool both = owners == 512;
                    wc::ring_clear(full_ring);
                    ws::hull_clear(full_state);
                    for (unsigned k = 0; k < owners; ++k) {
                        if (k < 16)
                            make(k, -3600.f + 480.f * float(k), 6000.f);
                        else
                            make(k, (float(k % 64) - 32.f) * 5000.f, -20000.f - 5000.f * float(k / 64));
                        hit(k, k);
                        if (both) { // its shield hit (a stage-owned shield record of the same owner)
                            wc::Event* e = wc::ring_acquire(full_ring);
                            const std::uint32_t seq = e->sequence;
                            std::memset(e, 0, sizeof *e);
                            e->sequence = seq;
                            e->role = wc::role_shield_hit;
                            e->style = wc::shape_slug;
                            e->flags = std::uint8_t(wc::flag_stage | wc::flag_forwarded | wc::flag_shield_read);
                            e->shield_q = 0xffff;
                            e->patch_tier = wc::patch_capital;
                            e->tier_source = wc::tier_by_radius;
                            e->t0_game_ms = 1000 + 4 * k;
                            e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
                            e->damage = 1200;
                            e->tint_rgb = 0xffffffu | wc::rgb_present;
                            e->owner = addr(far_owners[k].obj);
                            e->node = addr(far_owners[k].node);
                            e->handle = 0xf000u + k;
                            e->pos[2] = -200000;
                            e->radius = std::int32_t(engine(2000.f));
                        }
                    }
                    const std::uint32_t now = 1000 + 4 * owners;
                    static wc::VisibleOwners vis;
                    auto frame = [&](ws::Reader r, std::uint32_t t) {
                        vis.reset(0.f);
                        ws::build(timed_ring, t, r, hull_view(g, t, 16).cam, st_tuning, patches, wc::max_patches, decals,
                                  wc::max_decals, nullptr, &vis);
                        hull_build(hull_view(g, t, 16), ws::HullTuning{}, &timed_state, &timed_ring, r, &vis);
                        wc::ring_stamp_hidden(timed_ring, vis);
                    };
                    timed_ring = full_ring;
                    timed_state = full_state;
                    frame(rd, now); // latch and stamp (the timed frames order their owners by it)
                    full_ring = timed_ring;
                    full_state = timed_state;
                    double best[2] = {1e30, 1e30}, sum[2] = {0, 0};
                    for (unsigned path = 0; path < 2; ++path)
                        for (unsigned rep = 0; rep < 200; ++rep) {
                            timed_ring = full_ring;
                            timed_state = full_state;
                            if (path) x3m::engine_memory::next_frame();
                            QueryPerformanceCounter(&a);
                            frame(path ? production : rd, now + 16);
                            QueryPerformanceCounter(&b);
                            best[path] = std::min(best[path], ticks_us(a, b, freq));
                            sum[path] += ticks_us(a, b, freq);
                        }
                    timed_ring = full_ring;
                    timed_state = full_state;
                    reads = 0;
                    inner = rd;
                    frame(counted, now + 16);
                    const unsigned frame_reads = reads, located = vis.located_reads, past_cap = owners - wc::hull::max_owners;
                    const double per_read_ns = frame_reads ? (best[1] - best[0]) * 1e3 / double(frame_reads) : 0.0;
                    std::printf("STAGE_OWNERS case=%s records=%u owners=%u located_reads=%u located=%u reads=%u "
                                "fake_best_us=%.1f fake_mean_us=%.1f production_best_us=%.1f production_mean_us=%.1f "
                                "production_ns_per_read=%.1f\n",
                                both ? "owners_512_shield_hull" : "owners_1024_hull", ring_live(full_ring), owners, located,
                                vis.located_count, frame_reads, best[0], sum[0] / 200.0, best[1], sum[1] / 200.0,
                                per_read_ns);
                    // each owner located at most once a frame for both stages: the hull stage's owners past its cap
                    // (owners - 32) are all located, those the shield stage located or drew are not read again
                    report(both ? "stage_owners_512_located_once" : "stage_owners_1024_located_once",
                           located <= past_cap + (both ? 16u : 0u) && located >= past_cap - (both ? 16u : 0u) &&
                               vis.located_count <= owners,
                           "");
                }
            }
            // Out of reach (2026-11-07; review 6): eight fighter-tier hits at calibre 1 on an owner (r 5 view units, z
            // 1000) moved out past the right edge (x = z / m00 at its centre), the contact point on its side towards the
            // screen; each position is built with the cull and without it (the pre-change image), rendered on the sky
            // lane and compared pixel for pixel. Two classes: ballistic at 100 ms (the many-spark class) and heavy at 30
            // ms with its entry streak (the shot heading +x, so the streak's tail points into the screen). The lit range
            // is scanned first (margin / 48 steps, the unculled image); six steps across it (2 r .. the last lit
            // position) must each be non-empty and identical, and three past the margin must build nothing when culled.
            // The shared ring's sequence counter is restored afterwards (later cases hash their sequences).
            {
                draw_lane(g, 0.f, -1.f);
                const std::uint32_t sequence0 = stage_ring.next_sequence;
                const float r_view = 5.f, z_owner = 1000.f; // a small owner: its sparks outrun its 2 r
                const std::int32_t side[3] = {-std::int32_t(r_view / wc::world_scale), 0, 0};
                struct Sweep {
                    const char* name;
                    std::uint8_t cls;
                    std::int8_t dir_x;
                    std::uint32_t age_ms;
                };
                for (const Sweep sw : {Sweep{"ballistic", wc::class_ballistic, 0, 100}, Sweep{"heavy", wc::class_heavy, 127, 30}}) {
                    const bool heavy = sw.cls == wc::class_heavy;
                    ws::HullTuning culled, unculled;
                    unculled.reach_cull = false;
                    const ws::HullView v0 = hull_view(g, 1000, 16), v1 = hull_view(g, 1000 + sw.age_ms, 16);
                    float reach_att = 0.f, reach_spark = 0.f;
                    auto run = [&](float x, const ws::HullTuning& tt, std::vector<float>* luma) {
                        hull_owner(z_owner, r_view);
                        put32(fake_node, wc::layout::node_pos, std::uint32_t(std::int32_t(x / wc::world_scale)));
                        for (unsigned h = 0; h < 8; ++h) {
                            wc::Event* e = add_hull_hit(1000, side, damage_of(1.0), wc::patch_fighter, 0, wc::table_bullets,
                                                        sw.cls);
                            e->sequence = 0x5eed01u + h; // the same hashes in both builds
                            e->dir[0] = sw.dir_x;
                            ws::record_reach(*e, tt, float(wc::look::min_frames) * 16.f, &reach_att, &reach_spark);
                        }
                        hull_build(v0, tt);
                        const HullOut o = hull_build(v1, tt);
                        *luma = hull_render(pass, o);
                        return o;
                    };
                    auto lit_of = [](const std::vector<float>& l) {
                        unsigned n = 0;
                        for (const float v : l) n += v > 1e-3f;
                        return n;
                    };
                    const float m00 = v1.cam.m00, x_edge = z_owner / m00, norm = std::sqrt(m00 * m00 + 1.f);
                    const float px_scale = std::min(0.5f * v1.width * v1.cam.m00, 0.5f * v1.height * v1.cam.m11);
                    {
                        std::vector<float> l;
                        run(x_edge, culled, &l); // the reach of this record (both figures)
                    }
                    const float reach = reach_att + reach_spark;
                    const float floor_view = ws::reach_floor_px() * (z_owner + 2.f * r_view + reach) / px_scale;
                    const float margin = 2.f * r_view + reach + floor_view; // the owner test's radius
                    auto x_at = [&](float d) { return x_edge + d * norm / m00; }; // centre d past the right plane
                    float d_lit = 2.f * r_view;
                    for (unsigned j = 0; j <= 48; ++j) { // the lit range (unculled)
                        const float d = 2.f * r_view + float(j) * margin / 48.f;
                        std::vector<float> l;
                        run(x_at(d), unculled, &l);
                        if (!lit_of(l)) break;
                        d_lit = d;
                    }
                    unsigned identical = 0, steps = 0, lit_steps = 0, min_lit = ~0u, max_edge_px = 0, streaks = 0,
                             beyond_elements = 0, beyond_unculled = ~0u, beyond_skipped = ~0u;
                    for (unsigned j = 0; j <= 8; ++j) {
                        const bool beyond = j > 5;
                        const float d = beyond ? 2.f * r_view + float(j) * margin / 6.f
                                               : 2.f * r_view + float(j) * (d_lit - 2.f * r_view) / 5.f;
                        std::vector<float> lc, lu;
                        const HullOut oc = run(x_at(d), culled, &lc), ou = run(x_at(d), unculled, &lu);
                        unsigned edge = 0;
                        for (unsigned yy = 0; yy < g.h; ++yy)
                            for (unsigned xx = g.w - 16; xx < g.w; ++xx) edge += lc[std::size_t(yy) * g.w + xx] > 1e-3f;
                        const unsigned lit = lit_of(lc);
                        const unsigned elements = oc.quads + oc.boxes + oc.bursts + oc.rings + oc.filaments + oc.shimmer;
                        ++steps;
                        identical += lc == lu;
                        if (!beyond) {
                            lit_steps += lit > 0;
                            min_lit = std::min(min_lit, lit);
                            max_edge_px = std::max(max_edge_px, edge);
                            streaks += oc.n.streaks;
                        } else {
                            beyond_elements = std::max(beyond_elements, elements);
                            beyond_unculled = std::min(beyond_unculled, ou.quads);
                            beyond_skipped = std::min(beyond_skipped, oc.n.out_of_reach);
                        }
                        std::printf("HULL case=reach class=%s step=%u d=%.1f margin=%.1f lit_px=%u edge_px=%u identical=%u "
                                    "elements=%u streaks=%u unculled_quads=%u out_of_reach=%u\n",
                                    sw.name, j, double(d), double(margin), lit, edge, unsigned(lc == lu), elements,
                                    oc.n.streaks, ou.quads, oc.n.out_of_reach);
                    }
                    std::printf("HULL case=reach_summary class=%s r=%.1f reach_attached=%.1f reach_spark=%.1f floor_px=%.1f "
                                "floor_view=%.1f d_lit=%.1f steps=%u identical=%u lit_steps=%u min_lit_px=%u max_edge_px=%u "
                                "streaks=%u beyond_elements=%u beyond_unculled_quads=%u beyond_out_of_reach=%u\n",
                                sw.name, double(r_view), double(reach_att), double(reach_spark), double(ws::reach_floor_px()),
                                double(floor_view), double(d_lit), steps, identical, lit_steps, min_lit, max_edge_px,
                                streaks, beyond_elements, beyond_unculled, beyond_skipped);
                    const bool edge_ok = identical == steps && lit_steps == 6 && min_lit > 0 && max_edge_px > 0 &&
                                         d_lit > 2.f * r_view && (!heavy || streaks > 0);
                    const bool beyond_ok = beyond_elements == 0 && beyond_skipped == 8 && beyond_unculled > 0;
                    report(heavy ? "hull_reach_heavy_edge_unchanged" : "hull_reach_edge_sparks_unchanged", edge_ok, "");
                    report(heavy ? "hull_reach_heavy_beyond_margin_builds_nothing" : "hull_reach_beyond_margin_builds_nothing",
                           beyond_ok, "");
                }
                // The spark sphere of a moving owner (stage::build_hull (C)): an owner at z 1000 closing on the camera at
                // 15,000 view units/s (frame 968 at z 1240, 984 at z 1000: the hits there latch that velocity) is 100
                // ms later at z -500, behind the camera, so its own reach sphere is out of reach; the hits' sparks ride
                // p0 + v tau (1 - e^(-t / tau)) on the segment p0 .. p0 + v t, which is in view. With the cull its records
                // still build (out_of_reach 0, the owner counted out of reach) and the image is the unculled one, not
                // empty; the stamp counts the owner drawn and in view (its spark sphere, not its 2 r sphere).
                {
                    const float r_m = 5.f, speed = 15000.f;
                    const std::int32_t front[3] = {0, 0, -std::int32_t(r_m / wc::world_scale)};
                    auto at_z = [&](float z) {
                        put32(fake_node, wc::layout::node_pos + 8, std::uint32_t(std::int32_t(z / wc::world_scale)));
                    };
                    unsigned drawn = 0, in_view = 0;
                    auto run = [&](const ws::HullTuning& tt, std::vector<float>* luma) {
                        hull_owner(1000.f + speed * 0.016f, r_m);
                        add_hull_hit(968, front, damage_of(1.0), wc::patch_fighter, 0, wc::table_bullets, wc::class_ballistic)
                            ->sequence = 0x5eed11u;
                        static wc::VisibleOwners vis;
                        vis.reset(0.f);
                        hull_build(hull_view(g, 968, 16), tt, &hq_state, &stage_ring, &fake_read, &vis);
                        at_z(1000.f);
                        for (unsigned h = 0; h < 8; ++h)
                            add_hull_hit(984, front, damage_of(1.0), wc::patch_fighter, 0, wc::table_bullets, wc::class_ballistic)
                                ->sequence = 0x5eed21u + h;
                        vis.reset(0.f);
                        hull_build(hull_view(g, 984, 16), tt, &hq_state, &stage_ring, &fake_read, &vis);
                        at_z(1000.f - speed * 0.1f);
                        vis.reset(0.f);
                        const HullOut o = hull_build(hull_view(g, 1084, 16), tt, &hq_state, &stage_ring, &fake_read, &vis);
                        drawn = vis.count;
                        in_view = vis.in_view_count;
                        *luma = hull_render(pass, o);
                        return o;
                    };
                    ws::HullTuning culled, unculled;
                    unculled.reach_cull = false;
                    std::vector<float> lc, lu;
                    const HullOut oc = run(culled, &lc);
                    const unsigned drawn_c = drawn, in_view_c = in_view;
                    const HullOut ou = run(unculled, &lu);
                    unsigned lit = 0;
                    for (const float v : lc) lit += v > 1e-3f;
                    std::printf("HULL case=reach_moving speed=%.0f hits=%u sparks=%u unculled_sparks=%u out_of_reach=%u "
                                "owners_out_of_reach=%u lit_px=%u identical=%u drawn=%u in_view=%u\n",
                                double(speed), oc.n.hits, oc.n.sparks, ou.n.sparks, oc.n.out_of_reach,
                                oc.n.owners_out_of_reach, lit, unsigned(lc == lu), drawn_c, in_view_c);
                    report("hull_reach_moving_owner_sparks_kept",
                           oc.n.owners_out_of_reach == 1 && oc.n.out_of_reach == 0 && oc.n.hits == 9 && lit > 0 &&
                               lc == lu && oc.n.sparks == ou.n.sparks && drawn_c == 1 && in_view_c >= 1,
                           "");
                }
                stage_ring.next_sequence = sequence0;
            }
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
        ws::HullTuning only_burst = only_flash;
        only_burst.flash = false;
        only_burst.burst = true;
        for (unsigned c = 0; c < 6; ++c) {
            // 0: 20 hits on a fighter 60 m away (its hull a fifth of the frame: the stress row); 1: the same 200 m away
            // (the 3.1 budget's case); 2: 64 hits on a capital at close range; 3: one glow alone whose sphere holds the
            // eye (the whole screen once: the glow's full-screen cost, its budget weight against the decal's 177.5 us);
            // 4, 5: one burst alone holding the eye at two octaves and at one (the LOD): the fireball's weights.
            const unsigned hits = c == 2 ? 64 : c >= 3 ? 1 : 20;
            const float owner_z = c == 0 ? 400.f : c == 1 ? 1100.f : c == 2 ? 2600.f : 120.f;
            hull_owner(owner_z, c == 2 ? 2000.f : 100.f);
            for (unsigned i = 0; i < hits; ++i) {
                const double r = (c == 2 ? 60000.0 : 3000.0) * std::sqrt(halton(i + 1, 2)), ang = 6.2831853 * halton(i + 1, 3);
                const std::int32_t pos[3] = {c >= 3 ? 0 : std::int32_t(r * std::cos(ang)),
                                             c >= 3 ? 0 : std::int32_t(r * std::sin(ang)), c == 2 ? -200000 : -10000};
                add_hull_hit(1000 + 4 * i, pos, damage_of(c == 2 ? 0.8 : c >= 3 ? 1.0 : 0.33),
                             c >= 2 ? wc::patch_capital : wc::patch_fighter);
            }
            const ws::HullTuning& tune = c == 3 ? only_glow : c >= 4 ? only_burst : ws::HullTuning{};
            ws::HullView v = hull_view(tt, 1000, 16);
            hull_build(v, tune);
            v.now = 1000 + 4 * hits + 16;
            const HullOut o = hull_build(v, tune);
            const float lane_z = c == 2 ? 600.f : c == 3 ? 20.f : c >= 4 ? 600.f : owner_z - 100.f;
            draw_lane(tt, lane_z, 1.f);
            rr::WeaponEffectsFrame f = hull_frame(tt, o);
            if (c == 5) f.fireball_octaves = 1;
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
                               : c == 3 ? "glow_alone_fills_frame"
                               : c == 4 ? "burst_alone_fills_frame"
                                        : "burst_alone_fills_frame_lod";
            std::printf("HULL_TIMING width=%u height=%u case=%s hits=%u sparks=%u flashes=%u boxes=%u bursts=%u rings=%u "
                        "octaves=%u burst_screens=%.3f lit_share=%.4f raster_share=%.4f gpu_us=%.1f\n",
                        size.first, size.second, name, hits, o.n.sparks, o.n.flashes, o.boxes, o.bursts, o.rings,
                        f.fireball_octaves, double(o.n.burst_screens),
                        l.empty() ? 0.0 : double(lit_count(l)) / double(l.size()),
                        double(rasterized) / (double(size.first) * double(size.second)), best);
            // 50 us before the burst (45.6 measured, 2026-11-06); the twenty bursts' impostors add their rectangles
            if (c == 1 && size.first == 5120) report("hull_budget_20_hits_5120", best <= 70.0, "");
            if (c >= 4 && size.first == 5120)
                std::printf("HULL_TIMING case=%s weight=%.3f (gpu_us / 177.5)\n", name, best / 177.5);
        }
        tt.release();
    }
    hull_owner(400.f, 100.f);
    draw_lane(g, 0.f, -1.f);
    class_cases(pass);
}
