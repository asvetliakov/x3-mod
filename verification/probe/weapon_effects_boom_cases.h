#pragma once
// Step 3, the death explosion (docs/architecture/explosion-look.md sections 2, 3, 5, 7 and 9; the stage
// src/proxy/weapon_effects_boom.h, the shock src/effects/weapon_shock_ps.hlsl): the production builder and pass on the
// tiers the first flight draws (fighter, missile), at the vanilla vertical FOV (73.74 deg: 960 px per metre at 1 m at
// 1440 rows, 720 at 1080), so the frames have the note's pixel sizes. Included by weapon_effects_fixture.cpp after the
// class cases (it shares g, dev, report, base_frame, make_target, read_rgb, draw_lane, fence, stage_ring and the burst
// gates' body analysis). Rows: `EXPLOSION case=...`, `EXPLOSION_TIMING ...`, `EXPLOSION_DUMP ...`; checks death_*.
namespace boom {
namespace bm = wc::boom;
constexpr float vanilla_m11 = 1.3333333f; // 1 / tan(73.74 / 2 deg)
ws::BoomState state;
rr::WeaponSpriteVertex sprites[bm::max_quads * 4];
rr::WeaponGlowVertex glows[rr::weapon_glow_boxes * 4], darks[rr::weapon_scorch_boxes * 4];
rr::WeaponFireballVertex bodies[bm::max_bodies * 4], smokes[bm::max_bodies * 4], shocks[rr::weapon_shock_boxes * 4];
rr::WeaponGlowVertex halos[rr::weapon_halo_boxes * 4];
struct Out {
    ws::BoomCounts n;
    unsigned quads = 0, glows = 0, bodies = 0, smokes = 0, darks = 0, shocks = 0, halos = 0;
};
// Parts (the dump's and the gates' isolation): bit per element.
enum Part : unsigned { p_flash = 1, p_body = 2, p_shock = 4, p_ember = 8, p_fragment = 16, p_glow = 32, p_smoke = 64,
                       p_halo = 128, p_all = 255 };
const char* const part_names[8] = {"flash", "body", "shock", "ember", "fragment", "glow", "smoke", "halo"};
constexpr unsigned part_count = 8;
// The body's white-hot core in the builds (the geometry gates, sizes and erosion, measure the law's outline with it off:
// the core is a radiance term inside 0.5 R that would raise the 10 %-of-peak threshold).
bool core_on = true;
// The missile's shell for its own gate cases and budget row (boom::missile_shock is off in production).
bool force_shock = false;
ws::BoomTuning tuning(unsigned parts) {
    ws::BoomTuning t;
    t.star = parts & p_flash;
    t.body = parts & p_body;
    t.shock = parts & p_shock;
    t.embers = parts & p_ember;
    t.fragments = parts & p_fragment;
    t.glow = parts & p_glow;
    t.smoke = parts & p_smoke;
    t.halo = parts & p_halo;
    t.core = core_on;
    t.force_shock = force_shock;
    return t;
}
ws::HullView view(const Target& t, std::uint32_t now, std::uint32_t step) {
    ws::HullView v;
    v.cam = identity_camera();
    v.cam.m11 = vanilla_m11;
    v.cam.m00 = vanilla_m11 * float(t.h) / float(t.w);
    v.width = float(t.w);
    v.height = float(t.h);
    v.near_z = base_frame(g).near_z;
    v.now = now;
    v.step_ms = step;
    v.budget_left = wc::screen_budget;
    return v;
}
void clear() {
    wc::ring_clear(stage_ring);
    ws::boom_clear(state);
}
// One stage-owned death of `tier` at metres (x, y, z) from the eye (the identity camera), radius r_m, game ms t0;
// seq != 0 re-seeds it (another event of the same case).
wc::Event* add(unsigned tier, std::uint32_t t0, double x_m, double y_m, double z_m, double r_m, std::uint32_t seq = 0) {
    wc::Event* e = wc::ring_acquire(stage_ring);
    if (!e) return nullptr;
    // a fixed seed when none is given (the ring's running sequence depends on the fixture's history): every gate reproducible
    const std::uint32_t s = seq ? seq : 0x6a001u;
    std::memset(e, 0, sizeof *e);
    e->sequence = s;
    e->role = wc::role_explosion;
    e->style = std::uint8_t(tier);
    e->flags = wc::flag_stage;
    e->table = tier == bm::missile ? 10 : 7;
    e->patch_tier = tier == bm::corvette ? wc::patch_corvette : tier == bm::capital ? wc::patch_capital : wc::patch_fighter;
    e->t0_game_ms = t0;
    e->lifetime_ms = tier < bm::drawn_tiers ? bm::record_life_ms[tier] : 0;
    e->owner = 0x7000000u + s;
    e->node = 0x7100000u + s;
    e->pos[0] = std::int32_t(std::lround(x_m * 500.0));
    e->pos[1] = std::int32_t(std::lround(y_m * 500.0));
    e->pos[2] = std::int32_t(std::lround(z_m * 500.0));
    e->radius = std::int32_t(std::lround(r_m * 500.0));
    return e;
}
Out build(const ws::HullView& v, unsigned parts = p_all) {
    Out o;
    ws::BoomBuffers b;
    b.sprites = sprites;
    b.sprite_cap = bm::max_quads;
    b.glows = glows;
    b.glow_cap = rr::weapon_glow_boxes;
    b.fireballs = bodies;
    b.fireball_cap = bm::max_bodies;
    b.smokes = smokes;
    b.smoke_cap = bm::max_bodies;
    b.scorches = darks;
    b.scorch_cap = rr::weapon_scorch_boxes;
    b.shocks = shocks;
    b.halos = halos;
    b.halo_cap = rr::weapon_halo_boxes;
    b.shock_cap = rr::weapon_shock_boxes;
    o.n = ws::build_boom(stage_ring, state, v, tuning(parts), b);
    o.quads = b.sprite_quads;
    o.glows = b.glow_boxes;
    o.bodies = b.fireball_boxes;
    o.smokes = b.smoke_boxes;
    o.darks = b.scorch_boxes;
    o.shocks = b.shock_boxes;
    o.halos = b.halo_boxes;
    return o;
}
rr::WeaponEffectsFrame frame(Target& t, const Out& o, const ws::HullView& v) {
    rr::WeaponEffectsFrame f = base_frame(t);
    f.m00 = v.cam.m00;
    f.m11 = v.cam.m11;
    f.sprites = sprites;
    f.sprite_quads = o.quads;
    f.glows = glows;
    f.glow_boxes = o.glows;
    f.fireballs = bodies;
    f.fireball_boxes = o.bodies;
    f.fireball_octaves = o.n.octaves;
    f.smokes = smokes;
    f.smoke_boxes = o.smokes;
    f.scorches = darks;
    f.scorch_boxes = o.darks;
    f.shocks = shocks;
    f.shock_boxes = o.shocks;
    f.halos = halos;
    f.halo_boxes = o.halos;
    return f;
}
std::vector<float> render(rr::WeaponEffectsPass& pass, Target& t, const Out& o, const ws::HullView& v,
                          D3DCOLOR clear_colour = D3DCOLOR_ARGB(0, 0, 0, 0), HRESULT* result = nullptr) {
    dev->BeginScene();
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, clear_colour, 1.f, 0);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(frame(t, o, v), &r);
    dev->EndScene();
    if (result) *result = hr;
    return read_rgb(t, t.scene_s.p);
}
std::vector<float> luma(const std::vector<float>& rgb) {
    std::vector<float> l(rgb.size() / 3);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = std::max(rgb[3 * i], std::max(rgb[3 * i + 1], rgb[3 * i + 2]));
    return l;
}
float peak(const std::vector<float>& l) {
    float p = 0.f;
    for (float v : l) p = std::max(p, v);
    return p;
}
// The projected centre (px) and the law's radius (px) of a death at view point c with radius R (view units).
classes::burst::Disc disc(const Target& t, const ws::HullView& v, const float c[3], double R) {
    classes::burst::Disc d;
    d.x = 0.5 * t.w * (1.0 + double(v.cam.m00) * c[0] / c[2]);
    d.y = 0.5 * t.h * (1.0 - double(v.cam.m11) * c[1] / c[2]);
    d.R = R * 0.5 * t.w * double(v.cam.m00) / c[2];
    return d;
}
// The outermost pixel above 10 % of the image's peak in each of 36 angular bins about (x, y): the mean, and the
// farthest of all.
struct Outline {
    double mean = 0, max = 0, lit = 0;
};
Outline outline(const std::vector<float>& l, unsigned W, double x, double y) {
    Outline o;
    const float p = peak(l);
    if (!(p > 0.f)) return o;
    double outer[36] = {};
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (!(l[i] > 0.1f * p)) continue;
        const double dx = double(i % W) + 0.5 - x, dy = double(i / W) + 0.5 - y, r = std::hypot(dx, dy);
        const unsigned bin = std::min(35u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 36.0));
        outer[bin] = std::max(outer[bin], r);
        o.max = std::max(o.max, r);
        ++o.lit;
    }
    for (double v : outer) o.mean += v / 36.0;
    return o;
}
// The body's view-space centre of the newest body box (the biased point the program draws about).
void body_centre(const Out& o, float c[3]) {
    for (unsigned q = 0; q < 3; ++q) c[q] = o.bodies ? bodies[0].sphere[q] : 0.f;
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : (v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]));
}
bool write_pfm(const char* path, const std::vector<float>& rgb, unsigned w, unsigned h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "PF\n%u %u\n-1.0\n", w, h);
    for (unsigned y = h; y-- > 0;) std::fwrite(rgb.data() + std::size_t(y) * w * 3, sizeof(float), std::size_t(w) * 3, f);
    return std::fclose(f) == 0;
}
bool write_ppm(const char* path, const std::vector<float>& rgb, unsigned w, unsigned h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    std::vector<unsigned char> row(std::size_t(w) * 3);
    for (unsigned y = 0; y < h; ++y) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            const float v = rgb[std::size_t(y) * w * 3 + i];
            row[i] = (unsigned char)(v > 0.f ? (v < 1.f ? std::lround(v * 255.f) : 255) : 0);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    return std::fclose(f) == 0;
}
} // namespace boom

void boom_cases(rr::WeaponEffectsPass& pass, D3DPRESENT_PARAMETERS& pp) {
    namespace bm = wc::boom;
    using boom::Out;
    char detail[256];
    const D3DCOLOR black = D3DCOLOR_ARGB(0, 0, 0, 0), grey = D3DCOLOR_ARGB(0, 128, 128, 128);
    report("death_programs_attached", pass.caps().boom_enabled && pass.caps().shock_enabled &&
                                           pass.caps().shock_ps_slots > 0 && pass.caps().halo_ps_slots > 0,
           pass.caps().boom_reason);
    std::printf("EXPLOSION case=attach boom=%u reason=%s shock=%u shock_reason=%s shock_ps_slots=%u halo_ps_slots=%u "
                "max_ps_slots=%lu\n",
                unsigned(pass.caps().boom_enabled), pass.caps().boom_reason, unsigned(pass.caps().shock_enabled),
                pass.caps().shock_reason, pass.caps().shock_ps_slots, pass.caps().halo_ps_slots,
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
    const float m = ws::metres_to_view(1.f);
    // One death latched at t0 1000 (16 ms steps), then the frame at game age `age` (ms; the stage's age from its first
    // frame). The record's own builds advance the stage state (the first frame latches).
    auto latch = [&](Target& t, std::uint32_t step = 16) { boom::build(boom::view(t, 1000, step)); };
    auto at = [&](Target& t, float age, unsigned parts = boom::p_all, std::uint32_t step = 16) {
        return boom::build(boom::view(t, 1000 + std::uint32_t(age + 0.5f), step), parts);
    };
    auto fighter = [&](double z_m, std::uint32_t seq = 0, double x_m = 0.0) {
        return boom::add(bm::fighter, 1000, x_m, 0.0, z_m, 14.0, seq);
    };
    const float Lf = bm::tier_life_ms[bm::fighter], Lm = bm::tier_life_ms[bm::missile];
    draw_lane(g, 0.f, -1.f); // sky
    // Sections 1-4 measure the law's geometry and ramp: the white-hot core (a radiance term inside 0.5 R) off.
    boom::core_on = false;
    // ---- 1. death_tier_sizes: the body's mean outline radius (36 bins, 10 % of its peak) against R(t) f / z at five
    // ages, fighter (R 14 m) at 600 m and missile (R 3 m) at 300 m; the floor on a fighter at 2 km (12 px at 1440 rows).
    {
        bool ok = true;
        struct Tier {
            unsigned tier;
            double z, r;
            const char* name;
        } tiers[2] = {{bm::fighter, 600.0, 14.0, "fighter_600m"}, {bm::missile, 300.0, 3.0, "missile_300m"}};
        for (const Tier& tr : tiers) {
            const float L = bm::tier_life_ms[tr.tier];
            for (float frac : {0.1f, 0.3f, 0.6f, 0.8f, 1.0f}) {
                std::vector<double> ratios;
                double expected_px = 0;
                for (unsigned j = 0; j < 8; ++j) {
                    boom::clear();
                    wc::Event* e = boom::add(tr.tier, 1000, 0, 0, tr.z, tr.r, 0x6b000u + 977u * (j + 1));
                    latch(g);
                    const Out o = at(g, frac * L, boom::p_body);
                    const float R_f = ws::boom_r_f_m(*e) * m;
                    const float R0 = bm::tier_r0_obj[tr.tier] * float(e->radius) * wc::world_scale + bm::tier_r0_f[tr.tier] * R_f;
                    float c[3];
                    boom::body_centre(o, c);
                    if (!o.bodies) {
                        ratios.push_back(0.0);
                        continue;
                    }
                    const double R = ws::boom_body_radius(tr.tier, R_f, R0, frac * L);
                    const classes::burst::Disc d = boom::disc(g, boom::view(g, 0, 16), c, R);
                    expected_px = d.R;
                    const boom::Outline ol = boom::outline(boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16))), g.w, d.x, d.y);
                    ratios.push_back(ol.mean / d.R);
                }
                const double med = boom::median(ratios);
                std::printf("EXPLOSION case=size tier=%s age_frac=%.1f expected_px=%.1f outline_over_law=%.3f\n", tr.name,
                            double(frac), expected_px, med);
                if (frac <= 0.31f) ok = ok && std::fabs(med - 1.0) <= 0.05;
                if (frac >= 0.99f) ok = ok && med == 0.0;
            }
        }
        // the floor: a fighter at 2 km on 1440 rows draws at least 12 px of radius (its law gives 13.4: an M5 at 3 km)
        Target t1440;
        double floor_px = 0, law_px = 0;
        if (make_target(t1440, 2560, 1440)) {
            draw_lane(t1440, 0.f, -1.f);
            boom::clear();
            wc::Event* e = boom::add(bm::fighter, 1000, 0, 0, 3000.0, 12.0);
            boom::build(boom::view(t1440, 1000, 16));
            const Out o = boom::build(boom::view(t1440, 1000 + std::uint32_t(0.3f * Lf), 16), boom::p_body);
            float c[3];
            boom::body_centre(o, c);
            const ws::HullView v = boom::view(t1440, 0, 16);
            floor_px = o.bodies ? double(boom::bodies[0].sphere[3]) / bm::body_r_sphere * 0.5 * t1440.w * v.cam.m00 / c[2] : 0;
            law_px = double(ws::boom_r_f_m(*e)) * m * 0.5 * t1440.w * v.cam.m00 / (3000.0 * m);
            t1440.release();
        }
        std::printf("EXPLOSION case=floor tier=fighter_3km_1440 law_px=%.1f drawn_px=%.1f floor_px=%.1f\n", law_px, floor_px,
                    double(bm::body_floor_px));
        report("death_tier_sizes", ok && floor_px >= double(bm::body_floor_px) - 0.05, "");
    }
    // ---- 2-4. The burst's body gates on the fighter body (median of 16 fixed seeds): contrast and silhouette at 0.3 L, the
    // erosion outside-in at 0.1 / 0.5 / 0.9 / 1.0 L, core and rim colour at 0.2 L (16 fixed seeds).
    {
        std::vector<double> dark, hp, lr, lit[4], core_t, rim_t;
        const float ages[4] = {0.1f, 0.5f, 0.9f, 1.0f};
        for (unsigned j = 0; j < 16; ++j) {
            boom::clear();
            wc::Event* e = fighter(600.0, 0x6c000u + 977u * (j + 1));
            latch(g);
            const float R_f = ws::boom_r_f_m(*e) * m, R0 = 0.5f * float(e->radius) * wc::world_scale;
            auto body_at = [&](float frac, classes::burst::Disc* d) {
                const Out o = at(g, frac * Lf, boom::p_body);
                float c[3];
                boom::body_centre(o, c);
                *d = boom::disc(g, boom::view(g, 0, 16), c, ws::boom_body_radius(bm::fighter, R_f, R0, frac * Lf));
                return o.bodies ? boom::render(pass, g, o, boom::view(g, 0, 16)) : std::vector<float>(std::size_t(g.w) * g.h * 3, 0.f);
            };
            classes::burst::Disc d;
            const std::vector<float> l3 = boom::luma(body_at(0.3f, &d));
            const classes::burst::Body b = classes::burst::body_of(l3, d);
            dark.push_back(b.dark_share);
            hp.push_back(b.hp_std);
            lr.push_back(std::min(b.lobe_ratio, 100.0));
            for (unsigned k = 0; k < 4; ++k) {
                classes::burst::Disc dk;
                const std::vector<float> lk = boom::luma(body_at(ages[k], &dk));
                // the lit share inside R(t): above 10 % of the 0.1 L body's peak (one threshold over the life)
                double n = 0, on = 0;
                const float p0 = b.peak > 0 ? float(b.peak) : 1.f;
                for (unsigned y = 0; y < g.h; ++y)
                    for (unsigned x = 0; x < g.w; ++x)
                        if (std::hypot(x + 0.5 - dk.x, y + 0.5 - dk.y) <= dk.R) {
                            ++n;
                            on += lk[std::size_t(y) * g.w + x] > 0.1f * p0;
                        }
                lit[k].push_back(n > 0 ? on / n : 0.0);
            }
            const std::vector<float> rgb2 = body_at(0.2f, &d);
            double cc[3] = {}, rc[3] = {};
            for (unsigned y = 0; y < g.h; ++y)
                for (unsigned x = 0; x < g.w; ++x) {
                    const double r = std::hypot(x + 0.5 - d.x, y + 0.5 - d.y) / d.R;
                    const float* px = &rgb2[(std::size_t(y) * g.w + x) * 3];
                    for (unsigned q = 0; q < 3; ++q) {
                        if (r <= 0.3) cc[q] += px[q];
                        if (r >= 0.7 && r <= 1.0) rc[q] += px[q];
                    }
                }
            core_t.push_back(classes::burst::nearest_stop(cc));
            rim_t.push_back(classes::burst::nearest_stop(rc));
        }
        std::printf("EXPLOSION case=body tier=fighter_600m dark_share=%.4f hp_std=%.4f lobe_max_over_min=%.3f lit_01=%.3f "
                    "lit_05=%.3f lit_09=%.3f lit_10=%.3f core_T=%.2f rim_T=%.2f\n",
                    boom::median(dark), boom::median(hp), boom::median(lr), boom::median(lit[0]), boom::median(lit[1]),
                    boom::median(lit[2]), boom::median(lit[3]), boom::median(core_t), boom::median(rim_t));
        report("death_body_contrast", boom::median(dark) >= 0.15 && boom::median(hp) >= 0.10, "");
        report("death_body_silhouette_lobed", boom::median(lr) >= 1.25, "");
        // the bound at 0.9 L is 0.20 (coordinator decision 2026-11-07: body_r_sphere 1.33 meets the derived size; the
        // burst's 0.15 and the size gate cannot both hold, 0.156 measured)
        report("death_body_erodes_outside_in", boom::median(lit[0]) >= 0.8 && boom::median(lit[1]) >= 0.35 &&
                                                   boom::median(lit[1]) <= 0.75 && boom::median(lit[2]) <= 0.20 &&
                                                   boom::median(lit[3]) == 0.0, "");
        report("death_body_colour_core_rim", boom::median(core_t) >= 0.75 && boom::median(rim_t) <= 0.5, "");
    }
    boom::core_on = true;
    // ---- 4b. death_body_white_hot_core (decision 2026-11-07): at 0.1 L the body itself carries the white heat: the median
    // radiance of the lit pixels inside 0.3 R(t) >= 3 (8 fixed seeds); none of it by 0.4 L (the core's x < core_x).
    {
        std::vector<double> med_core, late;
        for (unsigned j = 0; j < 8; ++j) {
            boom::clear();
            wc::Event* e = fighter(600.0, 0x6c400u + 977u * (j + 1));
            latch(g);
            const float R_f = ws::boom_r_f_m(*e) * m, R0 = 0.5f * float(e->radius) * wc::world_scale;
            for (float frac : {0.1f, 0.4f}) {
                const Out o = at(g, frac * Lf, boom::p_body);
                float c[3];
                boom::body_centre(o, c);
                const classes::burst::Disc d =
                    boom::disc(g, boom::view(g, 0, 16), c, ws::boom_body_radius(bm::fighter, R_f, R0, frac * Lf));
                const std::vector<float> l = boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16)));
                std::vector<double> in;
                for (unsigned y = 0; y < g.h; ++y)
                    for (unsigned x = 0; x < g.w; ++x)
                        if (std::hypot(x + 0.5 - d.x, y + 0.5 - d.y) <= 0.3 * d.R && l[std::size_t(y) * g.w + x] > 0.f)
                            in.push_back(l[std::size_t(y) * g.w + x]);
                (frac < 0.2f ? med_core : late).push_back(boom::median(in));
            }
        }
        std::printf("EXPLOSION case=core tier=fighter_600m median_core_01=%.3f median_core_04=%.3f body_e=%.2f\n",
                    boom::median(med_core), boom::median(late), double(bm::body_e));
        report("death_body_white_hot_core", boom::median(med_core) >= 3.0 && boom::median(late) <= bm::body_e + 1e-3, "");
    }
    // ---- 5. death_no_pop_at_birth: the fighter's first frames (16 ms steps): the body's first frame at least R_0 f / z,
    // the star's arm under 0.25 H, no part's peak more than 2x the previous frame's except the star's birth.
    {
        boom::clear();
        wc::Event* e = fighter(600.0);
        float prev_peak = 0.f;
        bool rise_ok = true;
        double first_px = 0, r0_px = 0, arm_px = 0;
        for (unsigned fi = 0; fi < 6; ++fi) {
            const ws::HullView v = boom::view(g, 1000 + 16 * fi, 16);
            const Out o = boom::build(v, boom::p_all & ~unsigned(boom::p_flash));
            const float p = boom::peak(boom::luma(boom::render(pass, g, o, v)));
            if (fi && prev_peak > 0.f && p > 2.f * prev_peak) rise_ok = false;
            prev_peak = p;
            if (!fi && o.bodies) {
                float c[3];
                boom::body_centre(o, c);
                first_px = double(boom::bodies[0].sphere[3]) / bm::body_r_sphere * 0.5 * g.w * v.cam.m00 / c[2];
                r0_px = 0.5 * double(e->radius) * wc::world_scale * 0.5 * g.w * v.cam.m00 / c[2];
            }
        }
        boom::clear();
        fighter(60.0); // a near death: the star's arm against the cap
        const Out o = boom::build(boom::view(g, 1000, 16), boom::p_flash);
        if (o.quads) arm_px = double(boom::sprites[0].b[3]) * 0.5 * g.w * boom::view(g, 0, 16).cam.m00 / double(boom::sprites[0].a[3]);
        std::printf("EXPLOSION case=no_pop tier=fighter_600m first_px=%.2f r0_px=%.2f rise_ok=%u star_half_px_60m=%.1f cap_px=%.1f\n",
                    first_px, r0_px, unsigned(rise_ok), arm_px, double(bm::star_cap_h) * g.h);
        report("death_no_pop_at_birth", first_px >= 0.95 * r0_px && rise_ok && arm_px <= double(bm::star_cap_h) * g.h + 3.0, "");
    }
    // ---- 6. death_flash_star_3_frames (and 14. the SETA 6 floor): the star alone at 16 and 100 ms steps: lit on exactly
    // max(3, ceil(40 / step)) frames; elongated (principal second-moment ratio >= 2); <= 0.02 of its peak at 0.5 A off
    // the arms (no halo).
    {
        bool ok = true;
        for (std::uint32_t step : {16u, 100u}) {
            boom::clear();
            wc::Event* e = fighter(600.0);
            unsigned lit_frames = 0;
            double moment = 0, halo = 0;
            for (unsigned fi = 0; fi < 8; ++fi) {
                const ws::HullView v = boom::view(g, 1000 + step * fi, step);
                const Out o = boom::build(v, boom::p_flash);
                const std::vector<float> l = boom::luma(boom::render(pass, g, o, v));
                const float p = boom::peak(l);
                if (p > 0.f) ++lit_frames;
                if (fi == 0 && p > 0.f) {
                    // the hull star gate's method: the principal second-moment ratio of the lit set (unequal arms: a disc
                    // 1), and the largest value at least 0.5 A off both arm axes (no halo) over the peak
                    const float R_f = ws::boom_r_f_m(*e) * m;
                    const double A_px = std::max(double(wc::hull::star_floor_px),
                                                 double(bm::star_arm * R_f) * 0.5 * g.w * v.cam.m00 / (600.0 * m));
                    const double th = 1.5707963 * double(ws::unit_of(ws::element_hash(e->sequence, wc::hull::star_element)));
                    const double cs = std::cos(th), sn = std::sin(th), cx = 0.5 * g.w, cy = 0.5 * g.h;
                    double mx = 0, my = 0, n = 0;
                    for (std::size_t i = 0; i < l.size(); ++i) {
                        const double dx = double(i % g.w) + 0.5 - cx, up = -(double(i / g.w) + 0.5 - cy);
                        if (std::min(std::fabs(-sn * dx + cs * up), std::fabs(cs * dx + sn * up)) >= 0.5 * A_px)
                            halo = std::max(halo, double(l[i]) / p);
                        if (l[i] > 0.1f * p) {
                            mx += dx;
                            my += up;
                            ++n;
                        }
                    }
                    double sxx = 0, syy = 0, sxy = 0;
                    mx /= n;
                    my /= n;
                    for (std::size_t i = 0; i < l.size(); ++i)
                        if (l[i] > 0.1f * p) {
                            const double dx = double(i % g.w) + 0.5 - cx - mx, up = -(double(i / g.w) + 0.5 - cy) - my;
                            sxx += dx * dx;
                            syy += up * up;
                            sxy += dx * up;
                        }
                    const double tr = sxx + syy, det = sxx * syy - sxy * sxy, q = std::sqrt(std::max(0.0, tr * tr / 4 - det));
                    moment = tr / 2 - q > 0 ? (tr / 2 + q) / (tr / 2 - q) : 0.0;
                }
            }
            const unsigned want = std::max(3u, unsigned((40 + step - 1) / step));
            std::printf("EXPLOSION case=star step_ms=%u lit_frames=%u want=%u moment_ratio=%.2f off_arm=%.4f\n", step, lit_frames,
                        want, moment, halo);
            ok = ok && lit_frames == want && moment >= 2.0 && halo <= 0.02;
        }
        report("death_flash_star_3_frames", ok, "");
        // the body's game life is the law's at either step (SETA shortens the wall time, never the game life)
        bool seta_ok = true;
        for (std::uint32_t step : {16u, 100u}) {
            boom::clear();
            fighter(600.0);
            unsigned last = 0;
            for (unsigned fi = 0; fi * step <= 2 * unsigned(Lf); ++fi)
                if (boom::build(boom::view(g, 1000 + step * fi, step), boom::p_body).bodies) last = fi * step;
            std::printf("EXPLOSION case=seta step_ms=%u body_last_age_ms=%u life_ms=%.0f\n", step, last, double(Lf));
            seta_ok = seta_ok && last < unsigned(Lf) && last + step >= unsigned(Lf);
        }
        report("death_seta6_three_frame_floor", seta_ok, "");
    }
    // ---- 7. death_shock_is_a_shell (the limb only, decision 2026-11-07): the missile at 300 m at 0.1 / 0.2 tau: the
    // farthest lit pixel within 5 % of the law's r; the interior (the centre) <= 0.02 of the limb's peak; the peak <= 0.7
    // E_b; gone at shock_end L; not drawn once the eye is inside it. The shell forced on (BoomTuning::force_shock): the
    // missile's is off in production (boom::missile_shock), its law kept for when it returns.
    {
        boom::force_shock = true;
        bool ok = true;
        const float tau = bm::tier_shock_tau[bm::missile] * Lm;
        for (float u : {0.1f, 0.2f}) {
            boom::clear();
            wc::Event* e = boom::add(bm::missile, 1000, 0, 0, 300.0, 3.0);
            latch(g);
            const Out so = at(g, u * tau, boom::p_shock);
            const ws::HullView v = boom::view(g, 0, 16);
            const std::vector<float> l = boom::luma(boom::render(pass, g, so, v));
            const float R_f = ws::boom_r_f_m(*e) * m;
            const float c[3] = {0.f, 0.f, 300.f * m};
            const classes::burst::Disc d = boom::disc(g, v, c, bm::shock_r_x * R_f * std::sqrt(u));
            const boom::Outline ol = boom::outline(l, g.w, d.x, d.y);
            const float sp = boom::peak(l);
            const float inner = l[std::size_t(int(d.y)) * g.w + std::size_t(int(d.x))];
            std::printf("EXPLOSION case=shock tier=missile_300m u=%.1f law_px=%.1f outline_max_px=%.1f interior_over_limb=%.3f "
                        "shock_peak=%.3f body_e=%.2f boxes=%u\n",
                        double(u), d.R, ol.max, sp > 0 ? double(inner / sp) : 0.0, double(sp), double(bm::body_e), so.shocks);
            ok = ok && so.shocks == 1 && std::fabs(ol.max / d.R - 1.0) <= 0.05 && sp > 0 && inner <= 0.02f * sp &&
                 sp <= 0.7f * bm::body_e;
        }
        boom::clear();
        boom::add(bm::missile, 1000, 0, 0, 300.0, 3.0);
        latch(g);
        const Out gone = at(g, (bm::shock_end + 0.01f) * Lm, boom::p_shock);
        boom::clear();
        boom::add(bm::missile, 1000, 0, 0, 10.0, 3.0); // the eye 10 m from the centre: inside the shell after ~0.03 tau
        latch(g);
        const Out in = at(g, 0.5f * tau, boom::p_shock);
        std::printf("EXPLOSION case=shock_end boxes_after_end=%u boxes_eye_inside=%u missile_shock=%u\n", gone.shocks, in.shocks,
                    unsigned(bm::missile_shock));
        report("death_shock_is_a_shell", ok && in.shocks == 0 && gone.shocks == 0, "");
        boom::force_shock = false;
        // production: the missile's shell off (no box at 0.1 tau)
        boom::clear();
        boom::add(bm::missile, 1000, 0, 0, 300.0, 3.0);
        latch(g);
        const Out off = at(g, 0.1f * tau, boom::p_shock);
        std::printf("EXPLOSION case=shock_production missile_shock=%u boxes=%u\n", unsigned(bm::missile_shock), off.shocks);
        report("death_missile_shock_off", !bm::missile_shock && off.shocks == 0, "");
    }
    // ---- 8. death_embers (decision 2026-11-07): two seeds' angular histograms (36 bins of the lit pixels about the
    // centre at 0.6 L) correlate < 0.5; the mean ember colour at 0.3 / 0.7 / 1.1 L on the ramp T >= 0.75 / 0.5 / <= 0.3;
    // the farthest lit ember at 1.2 L within the speeds' reach (0.9 x 0.5 v_e .. 1.1 x 1.5 v_e, x 1.2 L, plus a length);
    // hidden inside the body: their lit share over the body's disc at 0.1 L <= 0.15, at most half drawn at birth;
    // lengths not uniform (std >= 0.2 of the mean over the drawn quads at 0.6 L); widths 1.5-2.5 px at 1080.
    {
        double hist[2][36] = {};
        double t_at[3] = {};
        double far_px = 0, reach_px = 0, cap_px = 0, over_body = 0, len_mean = 0, len_std = 0, w_lo = 1e9, w_hi = 0;
        unsigned at_birth = 0, drawn_06 = 0;
        for (unsigned j = 0; j < 2; ++j) {
            boom::clear();
            wc::Event* e = fighter(600.0, j ? 0x6d001u : 0x6d000u);
            latch(g);
            const ws::HullView v = boom::view(g, 0, 16);
            const Out o6 = at(g, 0.6f * Lf, boom::p_ember);
            const std::vector<float> l = boom::luma(boom::render(pass, g, o6, v));
            const float p = boom::peak(l);
            for (std::size_t i = 0; i < l.size(); ++i)
                if (p > 0.f && l[i] > 0.1f * p) {
                    const double dx = double(i % g.w) + 0.5 - 0.5 * g.w, dy = double(i / g.w) + 0.5 - 0.5 * g.h;
                    hist[j][std::min(35u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 36.0))] += 1;
                }
            if (j) continue;
            // the lengths and widths of the drawn quads (the pixel mode: -b.w the width, |c.x| the length)
            std::vector<double> lens;
            for (unsigned q = 0; q < o6.quads; ++q) {
                const rr::WeaponSpriteVertex& sv = boom::sprites[4 * q];
                if (!(sv.b[3] < 0.f)) continue;
                lens.push_back(std::fabs(sv.c[0]));
                w_lo = std::min(w_lo, double(-sv.b[3]));
                w_hi = std::max(w_hi, double(-sv.b[3]));
            }
            drawn_06 = unsigned(lens.size());
            for (double x : lens) len_mean += x / lens.size();
            for (double x : lens) len_std += (x - len_mean) * (x - len_mean) / lens.size();
            len_std = std::sqrt(len_std);
            const float fr[3] = {0.3f, 0.7f, 1.1f};
            for (unsigned k = 0; k < 3; ++k) {
                const std::vector<float> rgb = boom::render(pass, g, at(g, fr[k] * Lf, boom::p_ember), v);
                double s[3] = {};
                for (std::size_t i = 0; i < rgb.size() / 3; ++i)
                    for (unsigned q = 0; q < 3; ++q) s[q] += rgb[3 * i + q];
                t_at[k] = classes::burst::nearest_stop(s);
                if (!(s[0] + s[1] + s[2] > 0)) t_at[k] = 0;
            }
            const std::vector<float> l12 = boom::luma(boom::render(pass, g, at(g, 1.19f * Lf, boom::p_ember), v));
            for (std::size_t i = 0; i < l12.size(); ++i)
                if (l12[i] > 0.02f)
                    far_px = std::max(far_px, std::hypot(double(i % g.w) + 0.5 - 0.5 * g.w, double(i / g.w) + 0.5 - 0.5 * g.h));
            const float R_f_m = ws::boom_r_f_m(*e);
            reach_px = ws::boom_ember_speed_m(R_f_m) * 1.19 * Lf * 1e-3 * 0.5 * g.w * v.cam.m00 / 600.0;
            cap_px = bm::ember_len_max * R_f_m * 0.5 * g.w * v.cam.m00 / 600.0;
            // at 0.1 L: the embers' lit share over the body's disc (R(t)), and how many draw at birth
            const float R0 = 0.5f * float(e->radius) * wc::world_scale;
            const double Rpx = ws::boom_body_radius(bm::fighter, R_f_m * m, R0, 0.1f * Lf) * 0.5 * g.w * v.cam.m00 / (600.0 * m);
            const std::vector<float> l1 = boom::luma(boom::render(pass, g, at(g, 0.1f * Lf, boom::p_ember), v));
            double n = 0, on = 0;
            for (unsigned y = 0; y < g.h; ++y)
                for (unsigned x = 0; x < g.w; ++x)
                    if (std::hypot(x + 0.5 - 0.5 * g.w, y + 0.5 - 0.5 * g.h) <= Rpx) {
                        ++n;
                        on += l1[std::size_t(y) * g.w + x] > 0.05f;
                    }
            over_body = n > 0 ? on / n : 0.0;
            boom::clear();
            fighter(600.0, 0x6d000u);
            at_birth = boom::build(boom::view(g, 1000, 16), boom::p_ember).n.embers;
        }
        double ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
        for (unsigned b = 0; b < 36; ++b) {
            ma += hist[0][b] / 36;
            mb += hist[1][b] / 36;
        }
        for (unsigned b = 0; b < 36; ++b) {
            sab += (hist[0][b] - ma) * (hist[1][b] - mb);
            saa += (hist[0][b] - ma) * (hist[0][b] - ma);
            sbb += (hist[1][b] - mb) * (hist[1][b] - mb);
        }
        const double corr = saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 1.0;
        const unsigned count = unsigned(bm::tier_embers[bm::fighter]);
        std::printf("EXPLOSION case=embers tier=fighter_600m seed_correlation=%.3f T_03=%.2f T_07=%.2f T_11=%.2f far_px=%.1f "
                    "reach_px=%.1f lit_over_body_01=%.3f at_birth=%u count=%u drawn_06=%u len_mean_px=%.1f len_std_px=%.1f "
                    "cap_px=%.1f width_px=%.2f..%.2f\n",
                    corr, t_at[0], t_at[1], t_at[2], far_px, reach_px, over_body, at_birth, count, drawn_06, len_mean, len_std,
                    cap_px, w_lo, w_hi);
        report("death_embers_random_and_cooling", corr < 0.5 && t_at[0] >= 0.75 && t_at[1] >= 0.5 && t_at[2] <= 0.3 &&
                                                       far_px <= 1.1 * bm::ember_speed_hi * reach_px + cap_px &&
                                                       far_px >= 0.9 * bm::ember_speed_lo * reach_px, "");
        report("death_embers_hidden_in_body", over_body <= 0.15 && 2 * at_birth <= count, "");
        report("death_embers_lengths_and_widths", drawn_06 > 0 && len_std >= 0.2 * len_mean && len_mean <= cap_px &&
                                                      w_lo >= bm::ember_w_px_lo - 1e-3 && w_hi <= 3.0 + 1e-3, "");
    }
    // ---- 9. death_glow_is_the_bodys_light (over a hull plane 20 m behind the fighter; the burst's law, decision
    // 2026-11-07): the glow's peak <= 0.4 E_b; its 10 % equivalent radius <= min(glow_r_x R(t), glow_r_max R_f) f / z;
    // nothing over sky; nothing after L. death_halo_in_air: the halo alone over sky, its farthest pixel above 10 % of its
    // peak <= 1.8 R(t) at 0.1 / 0.3 / 0.6 / 0.8 L (a lamp fails), none after L, none with the eye inside the body.
    // 10. death_smoke_darkens_surfaces_only: the fighter over a 0.5 grey hull at its smoke's peak and at 1.6 L.
    {
        boom::clear();
        wc::Event* e = fighter(600.0);
        latch(g);
        const ws::HullView v = boom::view(g, 0, 16);
        const float R_f = ws::boom_r_f_m(*e) * m, R0 = 0.5f * float(e->radius) * wc::world_scale;
        const float c[3] = {0.f, 0.f, 600.f * m};
        draw_lane(g, 620.f * m, 1.f);
        double gp[2] = {}, gr = 0, glow_R_px = 0;
        const float fr[2] = {0.1f, 0.3f};
        for (unsigned k = 0; k < 2; ++k) {
            const std::vector<float> gl = boom::luma(boom::render(pass, g, at(g, fr[k] * Lf, boom::p_glow), v));
            gp[k] = boom::peak(gl);
            if (!k) {
                gr = std::sqrt(boom::outline(gl, g.w, 0.5 * g.w, 0.5 * g.h).lit / 3.14159265);
                const double Rt = ws::boom_body_radius(bm::fighter, R_f, R0, 0.1f * Lf);
                glow_R_px = boom::disc(g, v, c, std::min(double(bm::glow_r_x) * Rt, double(bm::glow_r_max * R_f))).R;
            }
        }
        const float after = boom::peak(boom::luma(boom::render(pass, g, at(g, 1.01f * Lf, boom::p_glow), v)));
        draw_lane(g, 0.f, -1.f);
        const float sky = boom::peak(boom::luma(boom::render(pass, g, at(g, 0.1f * Lf, boom::p_glow), v)));
        std::printf("EXPLOSION case=glow tier=fighter_600m glow_peak_01=%.3f glow_peak_03=%.3f body_e=%.2f glow_eq_r_px=%.1f "
                    "glow_R_px=%.1f after_L=%.4f sky=%.4f\n",
                    gp[0], gp[1], double(bm::body_e), gr, glow_R_px, double(after), double(sky));
        report("death_glow_is_the_bodys_light", gp[0] <= 0.4 * bm::body_e && gp[1] <= 0.4 * bm::body_e && gr <= glow_R_px &&
                                                    after == 0.f && sky == 0.f, "");
        bool halo_ok = true;
        for (float frac : {0.1f, 0.3f, 0.6f, 0.75f}) { // the halo fades out with the erosion by halo_fade_x1 L
            const std::vector<float> hl = boom::luma(boom::render(pass, g, at(g, frac * Lf, boom::p_halo), v));
            const double Rt_px = boom::disc(g, v, c, ws::boom_body_radius(bm::fighter, R_f, R0, frac * Lf)).R;
            const boom::Outline ol = boom::outline(hl, g.w, 0.5 * g.w, 0.5 * g.h);
            std::printf("EXPLOSION case=halo tier=fighter_600m age_frac=%.1f peak=%.3f max_r_px=%.1f R_t_px=%.1f ratio=%.3f\n",
                        double(frac), double(boom::peak(hl)), ol.max, Rt_px, Rt_px > 0 ? ol.max / Rt_px : 0.0);
            halo_ok = halo_ok && boom::peak(hl) > 0.f && ol.max <= 1.8 * Rt_px + 1.0;
        }
        const Out after_h = at(g, 1.01f * Lf, boom::p_halo);
        boom::clear();
        fighter(10.0);
        latch(g);
        const Out inside_h = at(g, 0.25f * Lf, boom::p_halo);
        std::printf("EXPLOSION case=halo_end after_L=%u eye_inside=%u\n", after_h.halos, inside_h.halos);
        report("death_halo_in_air", halo_ok && after_h.halos == 0 && inside_h.halos == 0, "");
        boom::clear();
        fighter(600.0);
        latch(g);
        // the smoke over a 0.5 grey hull plane just behind the body's centre (it lies on the surface): the darkest pixel
        draw_lane(g, 605.f * m, 1.f);
        const float s0 = bm::smoke_s0;
        auto darkest = [&](float frac) {
            const std::vector<float> l = boom::luma(boom::render(pass, g, at(g, frac * Lf, boom::p_smoke), v, grey));
            float mn = 1.f;
            for (float x : l) mn = std::min(mn, x);
            return mn;
        };
        const float peak_age = bm::smoke_t0 + bm::smoke_rise * bm::smoke_span; // y = smoke_rise: full strength
        const float d_peak = darkest(peak_age), d_end = darkest(1.61f);
        draw_lane(g, 0.f, -1.f);
        const float sky_s = darkest(peak_age);
        const float base = 128.f / 255.f;
        std::printf("EXPLOSION case=smoke tier=fighter_600m darkest_peak=%.3f bound=%.3f darkest_end=%.3f sky=%.3f base=%.3f\n",
                    double(d_peak), double(base * (1.f - 0.6f * s0)), double(d_end), double(sky_s), double(base));
        report("death_smoke_darkens_surfaces_only", d_peak <= base * (1.f - 0.6f * s0) + 1e-3f &&
                                                        std::fabs(d_end - base) < 2e-3f && std::fabs(sky_s - base) < 2e-3f, "");
    }
    // ---- 11. death_cools_through_ramp / death_fades_to_zero_at_life: the fighter's body at 0.2 / 0.6 / 1.0 L on the
    // ramp T >= 0.75 / >= 0.5 / <= 0.25 (1.0 L: nothing, T 0); every part 0 at its life (smoke 1.6 L).
    {
        // the mean hue's nearest ramp stop, the median over 8 fixed seeds (one seed's clumps decide its own figure)
        const ws::HullView v = boom::view(g, 0, 16);
        double T[3] = {};
        const float fr[3] = {0.2f, 0.6f, 1.0f};
        std::vector<double> Ts[3];
        for (unsigned j = 0; j < 8; ++j) {
            boom::clear();
            fighter(600.0, 0x6c800u + 977u * (j + 1));
            latch(g);
            for (unsigned k = 0; k < 3; ++k) {
                const std::vector<float> rgb = boom::render(pass, g, at(g, fr[k] * Lf, boom::p_body), v);
                double s[3] = {};
                for (std::size_t i = 0; i < rgb.size() / 3; ++i)
                    for (unsigned q = 0; q < 3; ++q) s[q] += rgb[3 * i + q];
                Ts[k].push_back(s[0] + s[1] + s[2] > 0 ? classes::burst::nearest_stop(s) : 0.0);
            }
        }
        for (unsigned k = 0; k < 3; ++k) T[k] = boom::median(Ts[k]);
        boom::clear();
        fighter(600.0);
        latch(g);
        draw_lane(g, 605.f * m, 1.f);
        bool zero = true;
        const float lives[8] = {bm::star_ms / Lf + 0.03f, 1.0f, bm::shock_end + 0.01f,
                                bm::ember_life[2] * (1.f + 0.5f * bm::ember_stagger) + 0.01f,
                                bm::fragment_life * (1.f + 0.5f * bm::ember_stagger) + 0.01f, 1.0f,
                                bm::smoke_t0 + bm::smoke_span + 0.01f, 1.0f};
        for (unsigned p = 0; p < boom::part_count; ++p) {
            const Out o = at(g, lives[p] * Lf, 1u << p);
            const unsigned n = o.quads + o.glows + o.bodies + o.smokes + o.darks + o.shocks + o.halos;
            zero = zero && n == 0;
        }
        draw_lane(g, 0.f, -1.f);
        std::printf("EXPLOSION case=cooling tier=fighter_600m T_02=%.2f T_06=%.2f T_10=%.2f zero_at_life=%u\n", T[0], T[1],
                    T[2], unsigned(zero));
        report("death_cools_through_ramp", T[0] >= 0.75 && T[1] >= 0.5 && T[2] <= 0.25, "");
        report("death_fades_to_zero_at_life", zero, "");
    }
    // ---- 13. death_nested_three_drawn: three fighters killed in one frame 12 m apart at 600 m (their bodies overlap):
    // three bodies with their own lobes (pairwise silhouette correlation < 0.8 over 36 bins, each body rendered alone),
    // the cluster rule saturating them (max N > 1; the three's peak of the bodies, light and halo at 0.1 L <= 1.4 x one
    // death's: never a sun).
    {
        boom::clear();
        for (int k = -1; k <= 1; ++k) fighter(600.0, 0x6e000u + 31u * unsigned(k + 1), 12.0 * k);
        latch(g);
        const ws::HullView v = boom::view(g, 0, 16);
        const Out o = at(g, 0.3f * Lf, boom::p_body);
        double prof[3][36] = {};
        for (unsigned b = 0; b < 3 && b < o.bodies; ++b) {
            Out one = o;
            one.bodies = 1;
            rr::WeaponEffectsFrame f1 = boom::frame(g, one, v);
            f1.fireballs = boom::bodies + 4 * b;
            dev->BeginScene();
            dev->SetRenderTarget(0, g.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, black, 1.f, 0);
            rr::WeaponEffectsReport r1{};
            pass.run(f1, &r1);
            dev->EndScene();
            const std::vector<float> l = boom::luma(read_rgb(g, g.scene_s.p));
            const float p1 = boom::peak(l);
            const float* sc = boom::bodies[4 * b].sphere;
            const float c[3] = {sc[0], sc[1], sc[2]};
            const classes::burst::Disc d = boom::disc(g, v, c, double(sc[3]));
            for (std::size_t i = 0; i < l.size(); ++i) {
                if (!(l[i] > 0.1f * p1)) continue;
                const double dx = double(i % g.w) + 0.5 - d.x, dy = double(i / g.w) + 0.5 - d.y, r = std::hypot(dx, dy);
                const unsigned bin = std::min(35u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 36.0));
                prof[b][bin] = std::max(prof[b][bin], r);
            }
        }
        double worst = -1;
        for (unsigned a2 = 0; a2 < 3; ++a2)
            for (unsigned b = a2 + 1; b < 3; ++b) {
                double ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
                for (unsigned q = 0; q < 36; ++q) {
                    ma += prof[a2][q] / 36;
                    mb += prof[b][q] / 36;
                }
                for (unsigned q = 0; q < 36; ++q) {
                    sab += (prof[a2][q] - ma) * (prof[b][q] - mb);
                    saa += (prof[a2][q] - ma) * (prof[a2][q] - ma);
                    sbb += (prof[b][q] - mb) * (prof[b][q] - mb);
                }
                worst = std::max(worst, saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 1.0);
            }
        const unsigned lit_parts = boom::p_body | boom::p_glow | boom::p_shock | boom::p_smoke | boom::p_halo;
        const Out o1 = at(g, 0.1f * Lf, lit_parts);
        const float p_all = boom::peak(boom::luma(boom::render(pass, g, o1, v)));
        const float p_embers = boom::peak(boom::luma(boom::render(pass, g, at(g, 0.1f * Lf, boom::p_ember | boom::p_fragment), v)));
        boom::clear();
        fighter(600.0, 0x6e000u + 31u);
        latch(g);
        const float p_one = boom::peak(boom::luma(boom::render(pass, g, at(g, 0.1f * Lf, lit_parts), v)));
        std::printf("EXPLOSION case=nested tier=fighter_600m_x3_12m bodies=%u max_cluster=%.2f worst_correlation=%.3f "
                    "peak_lit_01=%.3f single_lit_01=%.3f ratio=%.3f peak_embers_01=%.3f\n",
                    o.bodies, double(o1.n.max_cluster), worst, double(p_all), double(p_one),
                    p_one > 0.f ? double(p_all / p_one) : 0.0, double(p_embers));
        report("death_nested_three_drawn", o.bodies == 3 && worst < 0.8 && o1.n.max_cluster > 1.f && p_all <= 1.4f * p_one, "");
    }
    // ---- 12. death_eye_inside_bounded (decision 2026-11-07: a hot wash is the picture inside a fireball): the own death
    // in the chase view (the eye 24 m behind the fighter, inside its 28 m body): at 0.25 L lit over >= 90 % of the frame
    // and not uniform (the brightness's std over the frame >= 0.05 of its mean); at 0.6 L a dark share >= 0.15 (the
    // eroded clumps); the deeper eye (10 m) recorded; its cost below in the timing.
    {
        bool ok = true;
        for (double eye : {24.0, 10.0})
            for (float frac : {0.25f, 0.6f}) {
                boom::clear();
                fighter(eye);
                latch(g);
                const ws::HullView v = boom::view(g, 0, 16);
                const std::vector<float> l = boom::luma(boom::render(pass, g, at(g, frac * Lf, boom::p_body), v));
                const float p = boom::peak(l);
                double lit = 0, dark = 0, mean = 0, var = 0;
                for (float x : l) {
                    lit += x > 0.f;
                    dark += x < 0.1f * p;
                    mean += x;
                }
                lit /= double(l.size());
                dark /= double(l.size());
                mean /= double(l.size());
                for (float x : l) var += (x - mean) * (x - mean);
                const double cv = mean > 0 ? std::sqrt(var / double(l.size())) / mean : 0.0;
                std::printf("EXPLOSION case=eye_inside tier=fighter eye_m=%.0f age_frac=%.2f lit_share=%.3f dark_share=%.3f "
                            "std_over_mean=%.3f peak=%.3f\n", eye, double(frac), lit, dark, cv, double(p));
                if (eye > 20.0 && frac < 0.3f) ok = ok && lit >= 0.9 && cv >= 0.05;
                if (eye > 20.0 && frac > 0.5f) ok = ok && dark >= 0.15;
            }
        report("death_eye_inside_bounded", ok, "");
    }
    // ---- 15. Budget rows and the cases' cost, EVENT-fenced (best of 60), 1080p and 5120x1440: each element alone
    // filling the frame (their weights against the decal's 177.5 us), the fighter at 600 m, 20 fighters at 600 m, the
    // missile at 300 m, the nested kill, the eye inside a fighter.
    {
        for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
            Target tt;
            if (!make_target(tt, size.first, size.second)) {
                report("death_timing_target", false, "");
                continue;
            }
            draw_lane(tt, 0.f, -1.f);
            Com<IDirect3DQuery9> ev;
            dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
            LARGE_INTEGER freq, a, b;
            QueryPerformanceFrequency(&freq);
            auto time_frame = [&](const Out& o, const ws::HullView& v) {
                double best = 1e30;
                const rr::WeaponEffectsFrame f = boom::frame(tt, o, v);
                for (int i = -60; i < 60; ++i) {
                    dev->BeginScene();
                    dev->SetRenderTarget(0, tt.scene_s.p);
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, grey, 1.f, 0);
                    fence(ev.p);
                    QueryPerformanceCounter(&a);
                    rr::WeaponEffectsReport r{};
                    pass.run(f, &r);
                    fence(ev.p);
                    QueryPerformanceCounter(&b);
                    dev->EndScene();
                    if (i >= 0) best = std::min(best, ticks_us(a, b, freq));
                }
                return best;
            };
            const auto run_case = [&](const char* name, unsigned parts, float frac, auto&& place) {
                boom::clear();
                place();
                boom::build(boom::view(tt, 1000, 16));
                ws::HullView v = boom::view(tt, 1000 + std::uint32_t(frac * Lf), 16);
                const Out o = boom::build(v, parts);
                const double us = time_frame(o, v);
                std::printf("EXPLOSION_TIMING width=%u height=%u case=%s bodies=%u quads=%u glows=%u smokes=%u shocks=%u "
                            "octaves=%u screens=%.3f gpu_us=%.1f weight=%.3f\n",
                            size.first, size.second, name, o.bodies, o.quads, o.glows, o.smokes, o.shocks, o.n.octaves,
                            double(o.n.screens), us, us / 177.5);
                return us;
            };
            draw_lane(tt, 5.f * m, 1.f); // a hull everywhere just past the eye: the glows and smoke draw where they can
            const double fence_us = run_case("fence_empty", boom::p_all, 0.1f, [&] {}); // no death: the fence's own cost
            // the alone rows: an element whose box is the whole screen (the eye inside a body, a shell or a smoke)
            run_case("body_alone_fills_frame", boom::p_body, 0.3f, [&] { fighter(10.0); });
            run_case("smoke_alone_fills_frame", boom::p_smoke, 0.8f, [&] { fighter(10.0); });
            // the missile's shell (forced on) 24 m in radius at 0.1 L about a point 30 m out: most of the screen; its weight
            // per screen against the decal's 177.5 us, the fence removed (budget_weight_shock)
            boom::force_shock = true;
            {
                const double us = run_case("shock_alone_fills_frame", boom::p_shock, 0.1f * Lm / Lf, [&] {
                    boom::add(bm::missile, 1000, 0, 0, 30.0, 3.0);
                });
                boom::clear();
                boom::add(bm::missile, 1000, 0, 0, 30.0, 3.0);
                boom::build(boom::view(tt, 1000, 16));
                const Out so = boom::build(boom::view(tt, 1000 + std::uint32_t(0.1f * Lm), 16), boom::p_shock);
                const double area = double(so.n.screens) / double(bm::budget_weight_shock);
                std::printf("EXPLOSION_TIMING width=%u height=%u case=shock_weight shocks=%u area_screens=%.3f gpu_us=%.1f "
                            "fence_us=%.1f per_screen_weight=%.3f pinned=%.2f\n",
                            size.first, size.second, so.shocks, area, us, fence_us,
                            area > 0 ? (us - fence_us) / (area * 177.5) : 0.0, double(bm::budget_weight_shock));
            }
            boom::force_shock = false;
            run_case("star_alone", boom::p_flash, 0.f, [&] { fighter(60.0); });
            run_case("halo_alone", boom::p_halo, 0.3f, [&] { fighter(45.0); }); // the eye just outside the body, inside its halo
            draw_lane(tt, 0.f, -1.f);
            const double f600 = run_case("fighter_600m", boom::p_all, 0.1f, [&] { fighter(600.0); });
            const double f20 = run_case("fighters_20_600m", boom::p_all, 0.1f, [&] {
                for (unsigned k = 0; k < 20; ++k)
                    fighter(600.0 + 20.0 * (k % 5), 0x6f000u + 7u * k, -200.0 + 100.0 * (k / 5) + 13.0 * (k % 5));
            });
            const double mis = run_case("missile_300m", boom::p_all, 0.1f, [&] { boom::add(bm::missile, 1000, 0, 0, 300.0, 3.0); });
            const double nest = run_case("nested_3_fighters", boom::p_all, 0.1f, [&] {
                for (int k = -1; k <= 1; ++k) fighter(600.0, 0x6e000u + 31u * unsigned(k + 1), 60.0 * k);
            });
            const double eye = run_case("eye_inside_fighter", boom::p_all, 0.25f, [&] { fighter(24.0); });
            // its parts (where the cost sits) and the deeper eye
            run_case("eye_inside_fighter_body", boom::p_body, 0.25f, [&] { fighter(24.0); });
            run_case("eye_inside_fighter_glow", boom::p_glow, 0.25f, [&] { fighter(24.0); });
            run_case("eye_inside_fighter_sprites", boom::p_flash | boom::p_ember | boom::p_fragment, 0.25f, [&] { fighter(24.0); });
            run_case("eye_inside_fighter_10m", boom::p_all, 0.25f, [&] { fighter(10.0); });
            if (size.first == 5120) {
                std::snprintf(detail, sizeof detail, "fighter=%.1f twenty=%.1f missile=%.1f nested=%.1f eye=%.1f", f600, f20, mis,
                              nest, eye);
                // section 9: the eye inside <= 300 us; the far cases within the shared budget's fence (8 x 177.5 us)
                report("death_budget_5120", eye <= 300.0 && f20 <= double(wc::screen_budget) * 177.5 &&
                                                 f600 <= 100.0 && mis <= 100.0 && nest <= 150.0, detail);
            }
            tt.release();
        }
        // the hysteresis: 20 deaths inside the eye's reach exceed a small budget: two octaves for at least
        // fireball_octave_hold frames, back to three only under fireball_octave_return of it
        boom::clear();
        for (unsigned k = 0; k < 6; ++k) fighter(12.0 + 2.0 * k, 0x70000u + k);
        unsigned entered = 0, left = 0, frames_coarse = 0;
        for (unsigned fi = 0; fi < 12; ++fi) {
            ws::HullView v = boom::view(g, 1000 + 16 * fi, 16);
            v.budget_left = fi < 6 ? 0.5f : 1e6f;
            const Out o = boom::build(v, boom::p_body);
            if (o.n.octaves == 2) {
                ++frames_coarse;
                if (!entered) entered = fi + 1;
            } else if (entered && !left)
                left = fi + 1;
        }
        std::printf("EXPLOSION case=octave_lod entered_frame=%u left_frame=%u coarse_frames=%u hold=%u\n", entered, left,
                    frames_coarse, wc::look::fireball_octave_hold);
        report("death_octave_lod_hysteresis", entered == 1 && frames_coarse >= wc::look::fireball_octave_hold && left > 6, "");
    }
    // ---- The stage's CPU (the performance pass): 32 fighter deaths at 0.3 L in view (32 bodies, 1,024 ember and
    // fragment streaks, their glows), median of 201 builds, against a bound of 50 us (the hull's mixed-hit bound).
    {
        boom::clear();
        for (unsigned k = 0; k < 32; ++k)
            fighter(600.0 + 15.0 * (k % 4), 0x71000u + 13u * k, -300.0 + 20.0 * k);
        latch(g);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        std::vector<double> us;
        Out o;
        for (unsigned r = 0; r < 201; ++r) {
            const ws::HullView v = boom::view(g, 1000 + std::uint32_t(0.3f * Lf) + (r & 1u), 16);
            QueryPerformanceCounter(&a);
            o = boom::build(v);
            QueryPerformanceCounter(&b);
            us.push_back(ticks_us(a, b, freq));
        }
        const double med = boom::median(us);
        std::printf("EXPLOSION case=cpu deaths=32 bodies=%u quads=%u glows=%u median_us=%.1f bound_us=50\n", o.bodies, o.quads,
                    o.glows, med);
        report("death_stage_cpu_32_deaths", o.bodies == 32 && med <= 50.0, "");
    }
    // ---- Reset and refusal: the death programs survive a Reset (ensure_resources recreates them) and draw; a pass whose
    // death programs are refused (fault bit 8) keeps the hull (boom_enabled false, boom_reason) and draws no shock.
    {
        rr::WeaponEffectsPass refused;
        refused.set_faults(256);
        const HRESULT hr = refused.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, true,
                                          false, true);
        // a fighter's halo (a death program) and body at 0.1 L: the refused pass draws the body (the hull's program) only
        boom::clear();
        fighter(600.0);
        latch(g);
        const ws::HullView v = boom::view(g, 0, 16);
        const Out o = at(g, 0.1f * Lf, boom::p_body | boom::p_halo);
        dev->BeginScene();
        dev->SetRenderTarget(0, g.scene_s.p);
        rr::WeaponEffectsReport rep{};
        refused.run(boom::frame(g, o, v), &rep);
        dev->EndScene();
        std::printf("EXPLOSION case=refused attach=%08lx hull=%u boom=%u reason=%s halos_drawn=%u bodies_drawn=%u\n", hr,
                    unsigned(refused.caps().hull_enabled), unsigned(refused.caps().boom_enabled), refused.caps().boom_reason,
                    rep.halo_boxes, rep.fireball_boxes);
        report("death_programs_refused_keep_hull", SUCCEEDED(hr) && refused.caps().hull_enabled && !refused.caps().boom_enabled &&
                                                       !std::strcmp(refused.caps().boom_reason, "boom_program_create") &&
                                                       rep.halo_boxes == 0 && rep.fireball_boxes == o.bodies, "");
        refused.detach();
        // the shock program alone refused (fault bit 9): the set enabled, a fighter's death drawn (body, halo), the forced
        // missile shell not drawn
        rr::WeaponEffectsPass no_shock;
        no_shock.set_faults(512);
        const HRESULT hs = no_shock.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, true,
                                           false, true);
        boom::clear();
        fighter(600.0);
        boom::force_shock = true;
        boom::add(bm::missile, 1000, 40.0, 0, 300.0, 3.0, 0x6a777u);
        latch(g);
        const Out ns = at(g, 0.1f * Lm, boom::p_all);
        boom::force_shock = false;
        dev->BeginScene();
        dev->SetRenderTarget(0, g.scene_s.p);
        rr::WeaponEffectsReport rs{};
        no_shock.run(boom::frame(g, ns, v), &rs);
        dev->EndScene();
        std::printf("EXPLOSION case=shock_refused attach=%08lx boom=%u shock=%u shock_reason=%s bodies_drawn=%u halos_drawn=%u "
                    "shocks_built=%u shocks_drawn=%u\n",
                    hs, unsigned(no_shock.caps().boom_enabled), unsigned(no_shock.caps().shock_enabled),
                    no_shock.caps().shock_reason, rs.fireball_boxes, rs.halo_boxes, ns.shocks, rs.shock_boxes);
        report("death_shock_refused_deaths_drawn", SUCCEEDED(hs) && no_shock.caps().boom_enabled &&
                                                        !no_shock.caps().shock_enabled &&
                                                        !std::strcmp(no_shock.caps().shock_reason, "shock_program_create") &&
                                                        rs.fireball_boxes >= 2 && rs.halo_boxes >= 1 && ns.shocks == 1 &&
                                                        rs.shock_boxes == 0, "");
        no_shock.detach();
        pass.before_reset();
        g.release();
        D3DPRESENT_PARAMETERS pp2 = pp;
        const HRESULT reset = dev->Reset(&pp2);
        pass.after_reset(reset);
        const bool mt = SUCCEEDED(reset) && make_target(g, 1920, 1080);
        const HRESULT er = pass.ensure_resources();
        bool drew = false;
        if (mt && SUCCEEDED(er)) {
            draw_lane(g, 0.f, -1.f);
            HRESULT run = E_FAIL;
            const std::vector<float> l = boom::luma(boom::render(pass, g, o, v, black, &run));
            rr::WeaponEffectsReport after{};
            draw_lane(g, 0.f, -1.f);
            dev->BeginScene();
            dev->SetRenderTarget(0, g.scene_s.p);
            pass.run(boom::frame(g, o, v), &after);
            dev->EndScene();
            drew = SUCCEEDED(run) && boom::peak(l) > 0.f && pass.caps().boom_enabled && after.halo_boxes == o.halos &&
                   o.halos == 1;
        }
        std::printf("EXPLOSION case=reset reset=%08lx ensure=%08lx drew=%u\n", reset, er, unsigned(drew));
        report("death_reset_recovers", mt && SUCCEEDED(er) && pass.resources_ready() && drew, "");
    }
    // ---- The dump (X3M_WEAPON_EFFECTS_DUMP=<dir>, X3M_WEAPON_EFFECTS_DUMP_CASES selects base names): the cases the look
    // is judged on at 1920x1080 and 5120x1440, at 0.1 / 0.3 / 0.6 / 0.8 / 1.0 L (the fighter's ages for the nested and
    // eye cases), over black sky, and the fighter over a hull plane 20 m behind it; <case>_<W>x<H>-<ms>.ppm, and at
    // 1920x1080 each part alone as PFM (-flash, -body, -shock, -ember, -fragment, -glow, -smoke).
    const char* dir = std::getenv("X3M_WEAPON_EFFECTS_DUMP");
    if (dir && *dir) {
        const char* list = std::getenv("X3M_WEAPON_EFFECTS_DUMP_CASES");
        struct Case {
            const char* name;
            unsigned kind; // 0 fighter 600 m, 1 missile 300 m, 2 nested three, 3 eye inside, 4 fighter over a hull
        } cases[] = {{"boom_fighter_600m", 0}, {"boom_missile_300m", 1}, {"boom_nested_3_fighters", 2},
                     {"boom_eye_inside_fighter", 3}, {"boom_fighter_600m_hull", 4}};
        unsigned written = 0, failed = 0;
        char path[1024];
        for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
            Target tt;
            if (!make_target(tt, size.first, size.second)) continue;
            for (const Case& k : cases) {
                if (!dump::listed(list, k.name)) continue;
                const float L = k.kind == 1 ? Lm : Lf;
                for (float frac : {0.1f, 0.3f, 0.6f, 0.8f, 1.0f}) {
                    boom::clear();
                    if (k.kind == 0 || k.kind == 4) fighter(600.0);
                    if (k.kind == 1) boom::add(bm::missile, 1000, 0, 0, 300.0, 3.0);
                    if (k.kind == 2)
                        for (int q = -1; q <= 1; ++q) fighter(600.0, 0x6e000u + 31u * unsigned(q + 1), 60.0 * q);
                    if (k.kind == 3) fighter(24.0); // the own death in the chase view
                    draw_lane(tt, k.kind == 4 ? 620.f * m : 0.f, k.kind == 4 ? 1.f : -1.f);
                    // every frame from the first (the embers' streaks and the octave state), 16 ms steps
                    const unsigned age = unsigned(frac * L + 0.5f);
                    Out o;
                    for (unsigned t_ms = 0;; t_ms += 16) {
                        const unsigned now_age = t_ms < age ? t_ms : age;
                        o = boom::build(boom::view(tt, 1000 + now_age, 16));
                        if (now_age == age) break;
                    }
                    const ws::HullView v = boom::view(tt, 1000 + age, 16);
                    HRESULT hr = E_FAIL;
                    const std::vector<float> rgb = boom::render(pass, tt, o, v, k.kind == 4 ? grey : black, &hr);
                    std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u.ppm", dir, k.name, size.first, size.second, age);
                    const bool ok = SUCCEEDED(hr) && !rgb.empty() && boom::write_ppm(path, rgb, tt.w, tt.h);
                    ok ? ++written : ++failed;
                    std::printf("EXPLOSION_DUMP case=%s width=%u height=%u age_ms=%u bodies=%u quads=%u glows=%u smokes=%u "
                                "shocks=%u darks=%u screens=%.3f written=%u\n",
                                k.name, size.first, size.second, age, o.bodies, o.quads, o.glows, o.smokes, o.shocks, o.darks,
                                double(o.n.screens), unsigned(ok));
                    if (size.first != 1920) continue;
                    for (unsigned p = 0; p < boom::part_count; ++p) {
                        // the part's own build at the same state (the switches select what the frame draws)
                        static ws::BoomState saved;
                        saved = boom::state;
                        const Out po = boom::build(v, 1u << p);
                        boom::state = saved;
                        HRESULT ph = E_FAIL;
                        // glow, smoke, fragment: over the hull plane (the smoke on a 0.5 grey: it darkens; the others on black)
                        const bool surface = p == 5 || p == 6 || p == 4;
                        if (surface) draw_lane(tt, 620.f * m, 1.f);
                        const std::vector<float> prgb = boom::render(pass, tt, po, v, p == 6 ? grey : black, &ph);
                        draw_lane(tt, k.kind == 4 ? 620.f * m : 0.f, k.kind == 4 ? 1.f : -1.f);
                        std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u-%s.pfm", dir, k.name, size.first, size.second, age,
                                      boom::part_names[p]);
                        (SUCCEEDED(ph) && !prgb.empty() && boom::write_pfm(path, prgb, tt.w, tt.h)) ? ++written : ++failed;
                    }
                }
            }
            tt.release();
        }
        std::printf("EXPLOSION_DUMP done written=%u failed=%u\n", written, failed);
    }
    boom::clear();
    draw_lane(g, 0.f, -1.f);
}
