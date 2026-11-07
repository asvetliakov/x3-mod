#pragma once
// Step 3, the death explosion (docs/architecture/explosion-look.md sections 2, 3, 5, 7 and 9; the stage
// src/proxy/weapon_effects_boom.h, the shock src/effects/weapon_shock_ps.hlsl): the production builder and pass on every
// tier (fighter, missile; the big tiers corvette, capital, station with their pre-roll, section 16), at the vanilla vertical FOV (73.74 deg: 960 px per metre at 1 m at
// 1440 rows, 720 at 1080), so the frames have the note's pixel sizes. Included by weapon_effects_fixture.cpp after the
// class cases (it shares g, dev, report, base_frame, make_target, read_rgb, draw_lane, fence, stage_ring and the burst
// gates' body analysis). Rows: `EXPLOSION case=...`, `EXPLOSION_TIMING ...`, `EXPLOSION_DUMP ...`; checks death_*.
namespace boom {
namespace bm = wc::boom;
constexpr float vanilla_m11 = 1.3333333f; // 1 / tan(73.74 / 2 deg)
ws::BoomState state;
rr::WeaponSpriteVertex sprites[bm::max_quads * 4];
rr::WeaponGlowVertex glows[rr::weapon_glow_boxes * 4], darks[rr::weapon_scorch_boxes * 4];
// the bodies and, after them, the big tiers' secondaries (the stage places them past the bodies)
constexpr unsigned body_boxes = bm::max_bodies + bm::max_secondaries;
rr::WeaponFireballVertex bodies[body_boxes * 4], smokes[bm::max_bodies * 4], shocks[rr::weapon_shock_boxes * 4];
rr::WeaponGlowVertex halos[rr::weapon_halo_boxes * 4];
rr::WeaponFireballVertex waves[rr::weapon_wave_boxes * 4 * rr::weapon_wave_quads]; // section 13.1: the fire waves' quads
x3m::engine_shimmer::WaveRequest wave_req;                 // the newest wave's refraction request
rr::WeaponComposeConstants compose[bm::max_bodies];        // section 13.2: the composed bodies' tables
struct Out {
    ws::BoomCounts n;
    unsigned quads = 0, glows = 0, bodies = 0, smokes = 0, darks = 0, shocks = 0, halos = 0, waves = 0, wave_shimmer = 0;
    unsigned composed = 0;
    const rr::WeaponWaveConstants* wave_constants = nullptr;
};
// Parts (the dump's and the gates' isolation): bit per element.
// p_secondary: the big tiers' secondaries (each with its star, glow and embers); the hull light rides on p_glow.
// p_wave: section 13.1's fire wave (capital, station). p_ray, p_spike: section 14's starburst and halo spike.
enum Part : unsigned { p_flash = 1, p_body = 2, p_shock = 4, p_ember = 8, p_fragment = 16, p_glow = 32, p_smoke = 64,
                       p_halo = 128, p_secondary = 256, p_wave = 512, p_ray = 1024, p_spike = 2048, p_all = 4095 };
const char* const part_names[12] = {"flash", "body", "shock", "ember", "fragment", "glow", "smoke", "halo", "secondary",
                                    "wave", "ray", "spike"};
constexpr unsigned part_count = 12;
// death_sparks_1024_budget_5120's bound (us at 5120x1440): the first measurement + 25 % (2026-11-08 review)
constexpr double sparks_1024_bound_us = 48.1; // 38.5 us measured on the first run, + 25 %
// Section 13.2's composed body in the builds (off: the single sphere; the drift-law ring gate measures one sphere), the
// fixture's compose rows (force_n, force_shared, force_jets 0xff: the law's) and the box gate's growth.
bool compose_on = true, force_shared = false;
bool wave_octave2 = true; // the wave sheet's second octave (the one-octave weight row: false)
bool wave_off = false;     // the CPU rows' reference without the waves
unsigned octave_cap = 4;   // the frame's octaves at most (the fourth octave's timing row: 3)
unsigned force_n = 0, force_jets = 0xffu;
float box_scale = 1.f;
// The body's white-hot core in the builds (the geometry gates, sizes and erosion, measure the law's outline with it off:
// the core is a radiance term inside 0.5 R that would raise the 10 %-of-peak threshold).
bool core_on = true;
// The missile's shell for its own gate cases and budget row (boom::missile_shock is off in production).
bool force_shock = false;
// The hull light's switch in the builds (0 with p_glow, 1 alone with the body's glow off, 2 off): its own gates.
unsigned hull_light_mode = 0;
// The family's own screen cap (BoomTuning::budget_screens; the hysteresis gate lifts it to see the shared budget alone).
float cap_screens = bm::budget_screens;
ws::BoomTuning tuning(unsigned parts) {
    ws::BoomTuning t;
    t.star = parts & p_flash;
    t.spike = parts & p_spike;
    t.rays = parts & p_ray;
    t.body = parts & p_body;
    t.shock = parts & p_shock;
    t.embers = parts & p_ember;
    t.fragments = parts & p_fragment;
    t.glow = parts & p_glow;
    t.smoke = parts & p_smoke;
    t.halo = parts & p_halo;
    t.secondaries = parts & p_secondary; // its star also needs p_flash (every star is the flash part)
    t.hull_light = hull_light_mode == 2 ? false : hull_light_mode == 1 ? true : (parts & p_glow) != 0;
    if (hull_light_mode == 1) t.glow = false;
    t.core = core_on;
    t.force_shock = force_shock;
    t.budget_screens = cap_screens;
    t.wave = (parts & p_wave) && !wave_off;
    t.compose = compose_on;
    t.wave_octave2 = wave_octave2;
    t.force_n = force_n;
    t.force_jets = force_jets;
    t.force_shared = force_shared;
    t.box_scale = box_scale;
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
// The bubble cache the build looks the dying owner up in (nullptr: none; the big tiers take the axis capsule).
const ws::BubbleCache* bubbles = nullptr;
Out build(const ws::HullView& v, unsigned parts = p_all) {
    Out o;
    ws::BoomBuffers b;
    b.sprites = sprites;
    b.sprite_cap = bm::max_quads;
    b.glows = glows;
    b.glow_cap = rr::weapon_glow_boxes;
    b.fireballs = bodies;
    b.fireball_cap = body_boxes;
    b.smokes = smokes;
    b.smoke_cap = bm::max_bodies;
    b.scorches = darks;
    b.scorch_cap = rr::weapon_scorch_boxes;
    b.shocks = shocks;
    b.halos = halos;
    b.halo_cap = rr::weapon_halo_boxes;
    b.shock_cap = rr::weapon_shock_boxes;
    b.waves = waves;
    b.wave_cap = rr::weapon_wave_boxes;
    b.wave_shimmer = &wave_req;
    b.compose = compose;
    o.n = ws::build_boom(stage_ring, state, v, tuning(parts), b, nullptr, bubbles);
    o.quads = b.sprite_quads;
    o.glows = b.glow_boxes;
    o.bodies = b.fireball_boxes;
    o.smokes = b.smoke_boxes;
    o.darks = b.scorch_boxes;
    o.shocks = b.shock_boxes;
    o.halos = b.halo_boxes;
    o.waves = b.wave_boxes;
    o.wave_shimmer = b.wave_shimmer_count;
    o.composed = b.composed_boxes;
    o.wave_constants = b.wave_constants;
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
    f.fireball_octaves = o.n.octaves < octave_cap ? o.n.octaves : octave_cap;
    f.smokes = smokes;
    f.smoke_boxes = o.smokes;
    f.scorches = darks;
    f.scorch_boxes = o.darks;
    f.shocks = shocks;
    f.shock_boxes = o.shocks;
    f.halos = halos;
    f.halo_boxes = o.halos;
    f.waves = waves;
    f.wave_boxes = o.waves;
    f.wave_constants = o.wave_constants;
    f.compose = compose;
    f.composed_boxes = o.composed;
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
// The big tiers' scene model (section 8: the lab's box, 2 R_obj along the node's row 2 and 0.5 R_obj across): the dying
// hull drawn by the fixture into the scene target, grey with a key light and panel lines, at the engine's FADEOUTBODY
// alpha (1 to P, then to 0 at P + 1 s: the carrier's root faded by R'), and its depth into the lane while it is drawn
// (the glows and the smoke land on it, the lane's soft cut hides what is behind it). The camera is the identity, so the
// event's world rows are the view's.
IDirect3DPixelShader9* box_ps = nullptr;
bool compile_box() {
    if (box_ps) return true;
    // c0 (W, H, m00, m11), c1 the box centre (view) and the alpha, c2..c4 its axes (view), c5 its half-extents and the
    // mode (0 the lane: .r 0.5 hull, .b the view depth, else the sky sentinel; 1 the colour, clipped outside)
    const char* src =
        "float4 c0:register(c0); float4 c1:register(c1); float4 c2:register(c2); float4 c3:register(c3);"
        " float4 c4:register(c4); float4 c5:register(c5); float4 main(float2 vp:VPOS):COLOR0{"
        " float2 ndc = float2(2.0 * vp.x / c0.x - 1.0, 1.0 - 2.0 * vp.y / c0.y);"
        " float3 d = float3(ndc.x / c0.z, ndc.y / c0.w, 1.0), o = -c1.xyz;"
        " float3 ol = float3(dot(o, c2.xyz), dot(o, c3.xyz), dot(o, c4.xyz));"
        " float3 dl = float3(dot(d, c2.xyz), dot(d, c3.xyz), dot(d, c4.xyz));"
        " float3 sl = float3(dl.x >= 0.0 ? 1.0 : -1.0, dl.y >= 0.0 ? 1.0 : -1.0, dl.z >= 0.0 ? 1.0 : -1.0);"
        " float3 inv = sl / max(abs(dl), 1e-7);"
        " float3 ta = (-c5.xyz - ol) * inv, tb = (c5.xyz - ol) * inv, tn = min(ta, tb), tf = max(ta, tb);"
        " float t0 = max(max(tn.x, tn.y), tn.z), t1 = min(min(tf.x, tf.y), tf.z);"
        " bool hit = t1 >= max(t0, 0.0); float t = t0 > 0.0 ? t0 : t1;"
        " if (c5.w < 0.5) return hit ? float4(0.5, 0, t, 0) : float4(-1, 0, 0, 0);"
        " clip(hit ? 1.0 : -1.0);"
        " float3 n = tn.x >= t0 ? float3(-sl.x, 0, 0) : (tn.y >= t0 ? float3(0, -sl.y, 0) : float3(0, 0, -sl.z));"
        " float3 nv = n.x * c2.xyz + n.y * c3.xyz + n.z * c4.xyz;"
        " float k = 0.05 + 0.17 * saturate(dot(nv, normalize(float3(-0.45, 0.6, -0.66))));"
        " float3 u = (ol + dl * t) / c5.xyz, f = abs(frac(u * 4.0) - 0.5) * (1.0 - abs(n));"
        " float seam = max(max(f.x, f.y), f.z) > 0.47 ? 0.55 : 1.0;"
        " return float4(float3(0.85, 0.9, 1.0) * k * seam * c1.w, 1.0); }";
    const DWORD* w = nullptr;
    static Com<ID3DXBuffer> hold;
    return compile(src, "ps_3_0", &w, &hold.p) && SUCCEEDED(dev->CreatePixelShader(w, &box_ps));
}
struct Hull {
    float c[3], ax[3][3], e[3];
};
// The box of a big-tier record (its world point and rows; half-extents R_obj along row 2, 0.25 R_obj across).
Hull hull_of(const wc::Event& e) {
    Hull h;
    ws::boom_rows(e, h.ax);
    const float r = float(e.radius) * wc::world_scale;
    for (unsigned q = 0; q < 3; ++q) h.c[q] = float(e.pos[q]) * wc::world_scale;
    h.e[0] = h.e[1] = 0.25f * r;
    h.e[2] = r;
    return h;
}
// The carrier's alpha at game age `age` (ms from the death) of a tier: FADEOUTBODY retimed to P, 1000 ms.
float hull_alpha(unsigned tier, float age) {
    const float P = bm::tier_preroll_ms[tier < bm::drawn_tiers ? tier : 0];
    return age < P ? 1.f : age < P + 1000.f ? 1.f - (age - P) / 1000.f : 0.f;
}
void hull_constants(const Target& t, const ws::HullView& v, const Hull& h, float alpha, float mode) {
    const float c[6][4] = {{float(t.w), float(t.h), v.cam.m00, v.cam.m11},
                           {h.c[0], h.c[1], h.c[2], alpha},
                           {h.ax[0][0], h.ax[0][1], h.ax[0][2], 0.f},
                           {h.ax[1][0], h.ax[1][1], h.ax[1][2], 0.f},
                           {h.ax[2][0], h.ax[2][1], h.ax[2][2], 0.f},
                           {h.e[0], h.e[1], h.e[2], mode}};
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(box_ps);
    dev->SetPixelShaderConstantF(0, &c[0][0], 6);
}
// The lane for the frame: the hull's depth while it is drawn (alpha > 0), else sky.
void hull_lane(Target& t, const ws::HullView& v, const Hull* h, float alpha) {
    if (!h || !(alpha > 0.f) || !compile_box()) {
        draw_lane(t, 0.f, -1.f);
        return;
    }
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    hull_constants(t, v, *h, alpha, 0.f);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
// One frame over the scene model: the hull at its alpha drawn first, then the pass (the lane set by hull_lane).
std::vector<float> render_hull(rr::WeaponEffectsPass& pass, Target& t, const Out& o, const ws::HullView& v, const Hull* h,
                               float alpha, HRESULT* result = nullptr) {
    dev->BeginScene();
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    if (h && alpha > 0.f && compile_box()) {
        const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
        hull_constants(t, v, *h, alpha, 1.f);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    }
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(frame(t, o, v), &r);
    dev->EndScene();
    if (result) *result = hr;
    return read_rgb(t, t.scene_s.p);
}
// A big-tier death broadside to the identity camera: row 2 (the forward axis, the capsule's) along view +x, row 0 (the
// sideways axis) along +y, row 1 (up) = row 2 x row 0 (+z, along the view axis): the deck plane (rows 2 and 0, the
// wave's plane since 2026-11-08) is the view's x-y plane, face-on. Until 2026-11-08 the wave's plane was rows 2 and 1
// and the helpers put row 0 along +z: the rows are swapped here so every wave gate keeps its geometry.
void broadside(wc::Event* e) {
    e->dir[0] = 127;
    e->dir[1] = e->dir[2] = 0;
    e->alt_rgb332 = 0;
    e->detail = 127;
    e->calibre_q = 0;
}
// The same hull with the eye in the deck plane (section 13.1's -above frames keep their name): row 2 along +x, row 0
// along +z, row 1 = -y (the wave's plane, rows 2 and 0, the view's x-z plane: edge-on).
void from_above(wc::Event* e) {
    e->dir[0] = 127;
    e->dir[1] = e->dir[2] = 0;
    e->alt_rgb332 = 0;
    e->detail = 0;
    e->calibre_q = 127;
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
// cw / ch (non-zero): only the centred cw x ch window (the big tiers' dump: the 5120x1440 frames would be 22 MB each).
bool write_ppm(const char* path, const std::vector<float>& rgb, unsigned w, unsigned h, unsigned cw = 0, unsigned ch = 0) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const unsigned ow = cw && cw < w ? cw : w, oh = ch && ch < h ? ch : h, x0 = (w - ow) / 2, y0 = (h - oh) / 2;
    std::fprintf(f, "P6\n%u %u\n255\n", ow, oh);
    std::vector<unsigned char> row(std::size_t(ow) * 3);
    for (unsigned y = y0; y < y0 + oh; ++y) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            const float v = rgb[(std::size_t(y) * w + x0) * 3 + i];
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
                "wave=%u wave_reason=%s wave_ps_slots=%u fireball_ps_slots=%u max_ps_slots=%lu\n",
                unsigned(pass.caps().boom_enabled), pass.caps().boom_reason, unsigned(pass.caps().shock_enabled),
                pass.caps().shock_reason, pass.caps().shock_ps_slots, pass.caps().halo_ps_slots,
                unsigned(pass.caps().wave_enabled), pass.caps().wave_reason, pass.caps().wave_ps_slots,
                pass.caps().fireball_ps_slots, static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
    report("wave_program_attached", pass.caps().wave_enabled && pass.caps().wave_ps_slots > 0, pass.caps().wave_reason);
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
            floor_px = o.bodies ? double(o.n.body_R0) * 0.5 * t1440.w * v.cam.m00 / c[2] : 0;
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
        // 4a. death_body_no_centre_ring (the doughnut; docs/verification/weapon-effects.md "Fireball drift law"): the burst
        // gate's measure (classes::burst::ring_of about the drawn sphere's centre and R_s) on the same 16 fighter bodies at
        // 0.5 / 0.6 / 0.7 L: every body's prominence <= 0.15, the pooled annulus 0.3-0.45 R_s lit share <= 1.3 x its
        // neighbours'.
        boom::compose_on = false; // one sphere: the drift law's measure about the drawn sphere (13.2: each sub-burst the same)
        bool ring_ok = true;
        for (float frac : {0.5f, 0.6f, 0.7f}) {
            std::vector<double> prom;
            double rl[3] = {}, rn[3] = {};
            for (unsigned j = 0; j < 16; ++j) {
                boom::clear();
                fighter(600.0, 0x6c000u + 977u * (j + 1));
                latch(g);
                const Out o = at(g, frac * Lf, boom::p_body);
                if (!o.bodies) {
                    prom.push_back(0.0);
                    continue;
                }
                float c[3];
                boom::body_centre(o, c);
                const classes::burst::Disc d = boom::disc(g, boom::view(g, 0, 16), c, double(boom::bodies[0].sphere[3]));
                const std::vector<float> rgb = boom::render(pass, g, o, boom::view(g, 0, 16));
                const classes::burst::Ring r = classes::burst::ring_of(boom::luma(rgb), g.w, d.x, d.y, d.R);
                char name[64];
                std::snprintf(name, sizeof name, "centre_ring_death_hit%u_%02.0f", j, double(frac) * 10.0);
                classes::burst::dump_ring(name, rgb, g.w, g.h);
                prom.push_back(r.prominence);
                for (unsigned a = 0; a < 3; ++a) {
                    rl[a] += r.lit[a];
                    rn[a] += r.n[a];
                }
                std::printf("EXPLOSION case=body_centre_ring_hit hit=%u age_frac=%.1f R_s_px=%.1f prominence=%.4f "
                            "prominence_wide=%.4f\n", j, double(frac), d.R, r.prominence, r.prominence_wide);
            }
            double s[3];
            for (unsigned a = 0; a < 3; ++a) s[a] = rn[a] > 0 ? rl[a] / rn[a] : 0.0;
            const double nb = 0.5 * (s[0] + s[2]), ratio = nb > 0 ? s[1] / nb : (s[1] > 0 ? 1e9 : 0.0);
            const double worst = *std::max_element(prom.begin(), prom.end());
            std::printf("EXPLOSION case=body_centre_ring tier=fighter_600m age_frac=%.1f prominence=%.4f prominence_max=%.4f "
                        "annulus_lit=%.4f,%.4f,%.4f annulus_ratio=%.3f\n", double(frac), boom::median(prom), worst, s[0], s[1],
                        s[2], ratio);
            ring_ok = ring_ok && worst <= 0.15 && ratio <= 1.3;
        }
        report("death_body_no_centre_ring", ring_ok, "");
        boom::compose_on = true;
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
            const Out o = boom::build(v, boom::p_all & ~unsigned(boom::p_flash | boom::p_spike));
            const float p = boom::peak(boom::luma(boom::render(pass, g, o, v)));
            if (fi && prev_peak > 0.f && p > 2.f * prev_peak) rise_ok = false;
            prev_peak = p;
            if (!fi && o.bodies) {
                float c[3];
                boom::body_centre(o, c);
                first_px = double(o.n.body_R0) * 0.5 * g.w * v.cam.m00 / c[2];
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
        unsigned at_birth = 0, drawn_06 = 0, spread_drawn = 0;
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
            // section 14: the spray's births spread over spark_birth_x L: drawn once they are all out
            spread_drawn = boom::build(boom::view(g, 1000 + unsigned(bm::spark_birth_x * Lf) + 16u, 16), boom::p_ember).n.embers;
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
        const unsigned count = ws::boom_spark_count(0x6d000u, bm::fighter);
        std::printf("EXPLOSION case=embers tier=fighter_600m seed_correlation=%.3f T_03=%.2f T_07=%.2f T_11=%.2f far_px=%.1f "
                    "reach_px=%.1f lit_over_body_01=%.3f at_birth=%u count=%u drawn_06=%u len_mean_px=%.1f len_std_px=%.1f "
                    "cap_px=%.1f width_px=%.2f..%.2f\n",
                    corr, t_at[0], t_at[1], t_at[2], far_px, reach_px, over_body, at_birth, count, drawn_06, len_mean, len_std,
                    cap_px, w_lo, w_hi);
        report("death_embers_random_and_cooling", corr < 0.5 && t_at[0] >= 0.75 && t_at[1] >= 0.5 && t_at[2] <= 0.3 &&
                                                       far_px <= 1.1 * bm::ember_speed_hi * reach_px + cap_px &&
                                                       far_px >= 0.9 * bm::ember_speed_lo * reach_px, "");
        // section 14 (2026-11-07): the spray visible from birth (hidden only within ember_hide_r R(t)); the pixel lengths
        // and widths of the new law (the over-body share recorded, no longer bounded)
        std::printf("EXPLOSION case=sparks_birth tier=fighter_600m at_birth=%u after_spread=%u count=%u\n", at_birth,
                    spread_drawn, count);
        report("death_embers_hidden_in_body", at_birth > 0 && 10 * spread_drawn >= 8 * (count - count / 8u), "");
        report("death_embers_lengths_and_widths", drawn_06 > 0 && len_std >= 0.4 * len_mean &&
                                                      len_mean <= double(bm::spark_long_hi) &&
                                                      w_lo >= bm::ember_w_px_floor - 1e-3 && w_hi <= bm::ember_w_px_hi + 1e-3, "");
    }
    // ---- 14. Section 14 (2026-11-07, the vanilla frames): the radiant core, the starburst, the spark spray, the meter.
    // death_core_radiance: the body part's median radiance (the max channel) inside 0.2 R(t) at 0.05 L >= 6 (the fighter
    // at 600 m, median of 8 seeds). death_rays_starburst: the ray part alone about the screen centre (the fighter at
    // 300 m, 5120x1440, 4 seeds; soft rays 2026-11-07): the angular peaks of a circle (4096 bilinear samples, a peak above
    // 0.1 of the profile's maximum, >= its left and > its right neighbour) at 0.5 R(t), 0.1 L (every ray lit there: the
    // golden-ratio lengths end from 0.4 R_f) within the drawn count +- 1, at 1.5 R(t) >= 3 (the long rays out past the
    // body) and <= 2 at 0.6 L (600 m recorded); death_rays_soft: the median FWHM of the rays across the circle at 1.0
    // R(t) >= 4 px at 1080 rows and every ray's peak at 0.5 R(t) <= 0.4 of what the rest of the death (all but the rays,
    // sparks and fragments) puts under it; death_rays_differ: two seeds' 360-bin profiles at 0.1 L correlate < 0.3. death_sparks_dense: the fighter
    // at 600 m at 0.3 L draws >= 100 sparks with their head on screen (the ember part's local maxima recorded), its drawn
    // lengths' std >= 0.4 of their mean. death_exposure_meter_shift: the production meter (src/renderer/exposure.cpp,
    // Auto capped at +1.3 EV, gamma 2.2 decode) on the fighter at 300 m and at 120 m (enough bright tiles for the p99
    // tile maximum: the highlight limit acts) at 5120x1440 over a uniform dim scene (engine 0.15, the cap holds alone)
    // and a lit one (0.5): the fresh target moves <= 0.5 EV at 0.025 ... 0.5 L against the scene alone. Black sky is
    // recorded, not gated: the meter scores an empty frame 0 EV and climbs to its cap on any signal, so that row measures
    // the meter's black policy, not the death's highlight pull (coordinator decision 2026-11-07).
    {
        std::vector<double> core05;
        for (unsigned j = 0; j < 8; ++j) {
            boom::clear();
            wc::Event* e = fighter(600.0, 0x6c400u + 977u * (j + 1));
            latch(g);
            const float R_f = ws::boom_r_f_m(*e) * m, R0 = 0.5f * float(e->radius) * wc::world_scale;
            const Out o = at(g, 0.05f * Lf, boom::p_body);
            float c[3];
            boom::body_centre(o, c);
            const classes::burst::Disc d =
                boom::disc(g, boom::view(g, 0, 16), c, ws::boom_body_radius(bm::fighter, R_f, R0, 0.05f * Lf));
            const std::vector<float> l = boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16)));
            std::vector<double> in;
            for (unsigned y = 0; y < g.h; ++y)
                for (unsigned x = 0; x < g.w; ++x)
                    if (std::hypot(x + 0.5 - d.x, y + 0.5 - d.y) <= 0.2 * d.R) in.push_back(l[std::size_t(y) * g.w + x]);
            core05.push_back(boom::median(in));
        }
        std::printf("EXPLOSION case=core_radiance tier=fighter_600m median_02R_005=%.3f min=%.3f max=%.3f core_gain=%.2f\n",
                    boom::median(core05), *std::min_element(core05.begin(), core05.end()),
                    *std::max_element(core05.begin(), core05.end()), double(bm::core_gain));
        report("death_core_radiance", boom::median(core05) >= 6.0, "");
        // the sparks at 0.3 L (600 m, 1080p)
        {
            boom::clear();
            fighter(600.0, 0x6d000u);
            latch(g);
            // the sparks alone (2026-11-07: a fragment's head is as wide as a spark's, so the fragments are left out)
            const Out o = at(g, 0.3f * Lf, boom::p_ember);
            std::vector<double> lens;
            // the sparks whose head is on screen
            unsigned on_screen = 0;
            const ws::Camera cam = boom::view(g, 0, 16).cam;
            for (unsigned q = 0; q < o.quads; ++q) {
                const auto& v = boom::sprites[4 * q];
                if (!(v.b[3] < 0.f)) continue;
                lens.push_back(std::fabs(v.c[0]));
                if (!(v.a[2] > 0.f)) continue;
                const float xs = cam.m00 * v.a[0] / v.a[2] + cam.m20, ys = cam.m11 * v.a[1] / v.a[2] + cam.m21;
                on_screen += xs >= -1.f && xs <= 1.f && ys >= -1.f && ys <= 1.f;
            }
            double mean = 0, sd = 0;
            for (double x : lens) mean += x / double(lens.size());
            for (double x : lens) sd += (x - mean) * (x - mean) / double(lens.size());
            sd = std::sqrt(sd);
            const std::vector<float> l = boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16)));
            const float pk = boom::peak(l);
            unsigned heads = 0;
            // death_spark_heads: each head's FWHM, the narrowest of the full widths at half its value along the row, the
            // column and the two diagonals (the tail lies along one of them), pixels at 1080 rows
            std::vector<double> head_fw;
            const auto fwhm_at = [&](unsigned x, unsigned y, int dx, int dy) {
                const float v0 = l[std::size_t(y) * g.w + x], half = 0.5f * v0;
                const double step = (dx && dy) ? 1.41421356 : 1.0;
                double w = 0;
                for (int sgn : {-1, 1}) {
                    float prev = v0;
                    for (int k = 1; k < 16; ++k) {
                        const int xx = int(x) + sgn * k * dx, yy = int(y) + sgn * k * dy;
                        if (xx < 0 || yy < 0 || xx >= int(g.w) || yy >= int(g.h)) return 1e9;
                        const float v = l[std::size_t(yy) * g.w + unsigned(xx)];
                        if (v < half) {
                            w += (double(k - 1) + double(prev - half) / double(prev - v)) * step;
                            break;
                        }
                        if (k == 15) return 1e9;
                        prev = v;
                    }
                }
                return w;
            };
            for (unsigned y = 1; y + 1 < g.h; ++y)
                for (unsigned x = 1; x + 1 < g.w; ++x) {
                    const float v = l[std::size_t(y) * g.w + x];
                    if (!(v > 0.1f * pk)) continue;
                    bool top = true;
                    for (int dy = -1; dy <= 1 && top; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy) continue;
                            const float w = l[std::size_t(int(y) + dy) * g.w + unsigned(int(x) + dx)];
                            if (w > v || (w == v && (dy < 0 || (dy == 0 && dx < 0)))) {
                                top = false;
                                break;
                            }
                        }
                    heads += top;
                    if (top) {
                        const double fw = std::min(std::min(fwhm_at(x, y, 1, 0), fwhm_at(x, y, 0, 1)),
                                                   std::min(fwhm_at(x, y, 1, 1), fwhm_at(x, y, 1, -1)));
                        if (fw < 1e8) head_fw.push_back(fw * 1080.0 / g.h);
                    }
                }
            const double head_med = head_fw.empty() ? 0.0 : boom::median(head_fw);
            std::printf("EXPLOSION case=sparks tier=fighter_600m age_frac=0.3 count=%u drawn=%u on_screen=%u hidden=%u "
                        "image_heads=%u len_mean_px=%.1f len_std_px=%.1f std_over_mean=%.3f halvings=%u "
                        "head_fwhm_median_px1080=%.2f head_fwhm_n=%zu\n",
                        ws::boom_spark_count(0x6d000u, bm::fighter), o.n.embers, on_screen, o.n.embers_hidden,
                        heads, mean, sd, mean > 0 ? sd / mean : 0.0, o.n.spark_halvings, head_med, head_fw.size());
            report("death_sparks_dense", on_screen >= 100 && mean > 0 && sd >= 0.4 * mean, "");
            report("death_spark_heads", head_med >= 3.0, "");
        }
        Target big;
        if (make_target(big, 5120, 1440)) {
            draw_lane(big, 0.f, -1.f);
            const ws::HullView vb = boom::view(big, 0, 16);
            const double kx = 0.5 * big.w * double(vb.cam.m00);
            // a part's luma about the screen centre and R(t) there in pixels
            const auto render_parts = [&](std::uint32_t seq, double z, float frac, unsigned parts, unsigned* rays, double* R_px) {
                boom::clear();
                wc::Event* e = fighter(z, seq);
                latch(big);
                const Out o = at(big, frac * Lf, parts);
                if (rays) *rays = o.n.rays;
                const float R_f = ws::boom_r_f_m(*e) * m, R0 = 0.5f * float(e->radius) * wc::world_scale;
                *R_px = ws::boom_body_radius(bm::fighter, R_f, R0, frac * Lf) * kx / (z * m);
                return boom::luma(boom::render(pass, big, o, vb));
            };
            // the circle of radius r px about the screen centre: 4096 bilinear samples
            const auto sample = [&](const std::vector<float>& l, double r) {
                std::vector<double> s(4096);
                for (unsigned i = 0; i < 4096; ++i) {
                    const double a = 6.283185307 * (i + 0.5) / 4096.0;
                    const double x = 0.5 * big.w + r * std::cos(a) - 0.5, y = 0.5 * big.h + r * std::sin(a) - 0.5;
                    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
                    const double fx = x - x0, fy = y - y0;
                    const auto at_px = [&](int xx, int yy) {
                        return xx < 0 || yy < 0 || xx >= int(big.w) || yy >= int(big.h) ? 0.0
                                                                                         : double(l[std::size_t(yy) * big.w + xx]);
                    };
                    s[i] = (1 - fy) * ((1 - fx) * at_px(x0, y0) + fx * at_px(x0 + 1, y0)) +
                           fy * ((1 - fx) * at_px(x0, y0 + 1) + fx * at_px(x0 + 1, y0 + 1));
                }
                return s;
            };
            // the ray part's circle at 1.5 R(t)
            const auto circle = [&](std::uint32_t seq, double z, float frac, unsigned* rays) {
                double R_px = 0;
                const std::vector<float> l = render_parts(seq, z, frac, boom::p_ray, rays, &R_px);
                return sample(l, 1.5 * R_px);
            };
            const auto peak_at = [](const std::vector<double>& s, std::size_t i, double thr) {
                const std::size_t N = s.size();
                return s[i] > thr && s[i] >= s[(i + N - 1) % N] && s[i] > s[(i + 1) % N];
            };
            const auto peaks = [&](const std::vector<double>& s, double thr) {
                unsigned n = 0;
                for (std::size_t i = 0; i < s.size(); ++i) n += peak_at(s, i, thr);
                return n;
            };
            // a peak's full width at half its value along the circle, in samples (linear between samples)
            const auto fwhm_samples = [](const std::vector<double>& s, std::size_t i) {
                const std::size_t N = s.size();
                const double half = 0.5 * s[i];
                double w = 0;
                for (int dir : {-1, 1}) {
                    double prev = s[i];
                    for (std::size_t k = 1; k < N / 2; ++k) {
                        const double v = s[(i + N + std::size_t(dir) * k) % N];
                        if (v < half) {
                            w += double(k - 1) + (prev - half) / (prev - v);
                            break;
                        }
                        prev = v;
                    }
                }
                return w;
            };
            bool rays_ok = true, soft_ok = true;
            unsigned worst01 = 1000, worst06 = 0, worst05_lo = 1000, worst05_hi = 0;
            std::vector<double> fwhms;
            double worst_ratio = 0;
            for (double z : {300.0, 600.0})
                for (unsigned j = 0; j < 4; ++j) {
                    const std::uint32_t seq = 0x6f100u + 131u * j;
                    unsigned drawn = 0;
                    double R_px = 0, R_un = 0;
                    const std::vector<float> lr = render_parts(seq, z, 0.1f, boom::p_ray, &drawn, &R_px);
                    const std::vector<float> lu =
                        render_parts(seq, z, 0.1f, boom::p_all & ~(boom::p_ray | boom::p_ember | boom::p_fragment), nullptr, &R_un);
                    const std::vector<double> a = sample(lr, 1.5 * R_px), b = circle(seq, z, 0.6f, nullptr);
                    const std::vector<double> c5 = sample(lr, 0.5 * R_px), u5 = sample(lu, 0.5 * R_px), c10 = sample(lr, R_px);
                    const double mx = *std::max_element(a.begin(), a.end());
                    const double mx5 = *std::max_element(c5.begin(), c5.end()), mx10 = *std::max_element(c10.begin(), c10.end());
                    const unsigned pa = peaks(a, 0.1 * mx), pb = peaks(b, 0.1 * mx), p5 = peaks(c5, 0.1 * mx5);
                    // the softness: each ray's FWHM on the circle at 1.0 R(t) (arc px at 1080 rows) and its peak over what
                    // the rest of the death (body, core, halo, glow, spike, star) puts under it at 0.5 R(t)
                    std::vector<double> fw;
                    for (std::size_t i = 0; i < c10.size(); ++i)
                        if (peak_at(c10, i, 0.1 * mx10))
                            fw.push_back(fwhm_samples(c10, i) * 6.283185307 * R_px / 4096.0 * 1080.0 / big.h);
                    double ratio = 0;
                    for (std::size_t i = 0; i < c5.size(); ++i)
                        if (peak_at(c5, i, 0.1 * mx5)) ratio = std::max(ratio, u5[i] > 0 ? c5[i] / u5[i] : 1e9);
                    const double fmed = fw.empty() ? 0.0 : boom::median(fw);
                    float turn;
                    std::printf("EXPLOSION case=rays tier=fighter z_m=%.0f seed=%u rays=%u drawn=%u peaks_01=%u peaks_06=%u "
                                "max_01=%.3f peaks_05R=%u fwhm_10R_px1080=%.2f peak_over_under_05R=%.3f R_px=%.1f\n",
                                z, j, ws::boom_ray_count(seq, &turn), drawn, pa, pb, mx, p5, fmed, ratio, R_px);
                    if (z < 400.0) {
                        worst01 = std::min(worst01, pa);
                        worst06 = std::max(worst06, pb);
                        worst05_lo = std::min(worst05_lo, p5);
                        worst05_hi = std::max(worst05_hi, p5);
                        // every ray resolved where they are all lit (0.5 R(t)): the count's 9-15 +- 1 (a stratified pair
                        // merging or a peak split); the long ones out past the body (1.5 R(t)); gone by 0.6 L
                        rays_ok = rays_ok && p5 + 1 >= drawn && p5 <= drawn + 1 && pa >= 3 && pb <= 2;
                        fwhms.insert(fwhms.end(), fw.begin(), fw.end());
                        worst_ratio = std::max(worst_ratio, ratio);
                    }
                }
            const double fwhm_median = fwhms.empty() ? 0.0 : boom::median(fwhms);
            soft_ok = fwhm_median >= 4.0 && worst_ratio <= 0.4;
            std::printf("EXPLOSION case=rays_soft fwhm_10R_median_px1080=%.2f fwhm_count=%zu worst_peak_over_under_05R=%.3f\n",
                        fwhm_median, fwhms.size(), worst_ratio);
            // two deaths' starbursts: 360-bin profiles at 0.1 L (300 m)
            double corr = 1.0;
            {
                const std::vector<double> a = circle(0x6f100u, 300.0, 0.1f, nullptr), b = circle(0x6f283u, 300.0, 0.1f, nullptr);
                double ha[360] = {}, hb[360] = {}, ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
                for (unsigned i = 0; i < 4096; ++i) {
                    ha[i * 360 / 4096] += a[i];
                    hb[i * 360 / 4096] += b[i];
                }
                for (unsigned q = 0; q < 360; ++q) {
                    ma += ha[q] / 360;
                    mb += hb[q] / 360;
                }
                for (unsigned q = 0; q < 360; ++q) {
                    sab += (ha[q] - ma) * (hb[q] - mb);
                    saa += (ha[q] - ma) * (ha[q] - ma);
                    sbb += (hb[q] - mb) * (hb[q] - mb);
                }
                corr = saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 1.0;
            }
            std::printf("EXPLOSION case=rays_summary worst_peaks_01=%u worst_peaks_06=%u peaks_05R=%u..%u seed_correlation=%.3f\n",
                        worst01, worst06, worst05_lo, worst05_hi, corr);
            report("death_rays_starburst", rays_ok, "");
            report("death_rays_soft", soft_ok, "");
            report("death_rays_differ", corr < 0.3, "");
            // the meter: the production statistic and target on the frame plus a uniform scene in engine space
            const auto meter_ev = [&](const std::vector<float>& rgb, float bg) {
                rr::ExposureParams pm;
                pm.ev_max = 1.3f; // the production Auto cap (capture.cpp)
                const std::size_t n = std::size_t(big.w) * big.h;
                std::vector<float> mean(n), mx(n), scratch(std::size_t((big.w + 3) / 4) * ((big.h + 3) / 4));
                for (std::size_t i = 0; i < n; ++i) {
                    const float c[3] = {rgb[3 * i] + bg, rgb[3 * i + 1] + bg, rgb[3 * i + 2] + bg};
                    mean[i] = mx[i] = rr::meter_level0(c, rr::ExposureDecode::Gamma22, pm);
                }
                unsigned tw = big.w, th = big.h;
                rr::reduce_chain(mean.data(), mx.data(), scratch.data(), tw, th, 4, rr::kMeterTileMax);
                std::vector<float> wts(std::size_t(tw) * th);
                rr::tile_weights(wts.data(), tw, th, pm.meter_edge_weight);
                std::vector<rr::TileSample> ts(std::size_t(tw) * th);
                const rr::MeterStatistics st = rr::meter_statistics(mean.data(), mx.data(), wts.data(), tw * th, ts.data(), pm);
                return rr::exposure_target(st, pm).ev_target;
            };
            const std::vector<float> empty(std::size_t(big.w) * big.h * 3, 0.f);
            const float bgs[3] = {0.f, 0.15f, 0.5f};
            float base_ev[3];
            for (unsigned b = 0; b < 3; ++b) base_ev[b] = meter_ev(empty, bgs[b]);
            // the gate: dim and lit, per distance (coordinator decision 2026-11-08: the 300 m fighter <= 0.5 EV, sparks at
            // normal range never drive the meter; the 120 m fighter <= 1.3 EV, Auto's upper limit: a death that close may
            // dim the scene); black informational
            double worst = 0, worst_black = 0, worst_at[2] = {0, 0};
            for (const unsigned dist_m : {300u, 120u}) {
                boom::clear();
                fighter(double(dist_m), 0x6d000u);
                Out o;
                unsigned t_ms = 0;
                for (float frac : {0.025f, 0.05f, 0.1f, 0.2f, 0.3f, 0.5f}) {
                    const unsigned age = unsigned(frac * Lf + 0.5f);
                    for (;; t_ms += 16) {
                        const unsigned now_age = t_ms < age ? t_ms : age;
                        o = boom::build(boom::view(big, 1000 + now_age, 16));
                        if (now_age == age) break;
                    }
                    const std::vector<float> rgb = boom::render(pass, big, o, vb);
                    float ev[3];
                    for (unsigned b = 0; b < 3; ++b) {
                        ev[b] = meter_ev(rgb, bgs[b]);
                        double& w = b ? worst : worst_black;
                        w = std::max(w, std::fabs(double(ev[b] - base_ev[b])));
                        if (b) worst_at[dist_m < 200u] = std::max(worst_at[dist_m < 200u], std::fabs(double(ev[b] - base_ev[b])));
                    }
                    std::printf("EXPLOSION case=meter tier=fighter_%um_5120 age_ms=%u ev_black=%.3f ev_dim=%.3f ev_lit=%.3f "
                                "base=%.3f,%.3f,%.3f peak=%.2f\n",
                                dist_m, age, double(ev[0]), double(ev[1]), double(ev[2]), double(base_ev[0]), double(base_ev[1]),
                                double(base_ev[2]), double(boom::peak(boom::luma(rgb))));
                }
            }
            std::printf("EXPLOSION case=meter_summary worst_shift_ev=%.3f black_shift_ev=%.3f worst_300m_ev=%.3f worst_120m_ev=%.3f\n",
                        worst, worst_black, worst_at[0], worst_at[1]);
            report("death_exposure_meter_shift", worst_at[0] <= 0.5 && worst_at[1] <= 1.3, "");
            big.release();
        } else {
            report("death_rays_starburst", false, "target");
            report("death_rays_soft", false, "target");
            report("death_rays_differ", false, "target");
            report("death_exposure_meter_shift", false, "target");
        }
        draw_lane(g, 0.f, -1.f);
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
        // section 14: the rays end at ray_x L, the halo spike at spike_x L
        const float lives[boom::part_count] = {bm::star_ms / Lf + 0.03f, 1.0f,
                                               bm::shock_end + 0.01f,
                                               bm::ember_life[2] * (1.f + 0.5f * bm::ember_stagger) + bm::spark_birth_x + 0.01f,
                                               bm::fragment_life * (1.f + 0.5f * bm::ember_stagger) + bm::spark_birth_x + 0.01f,
                                               1.0f,
                                               bm::smoke_t0 + bm::smoke_span + 0.01f, 1.0f, 1.0f, bm::wave_life_x + 0.01f,
                                               bm::ray_x + 0.01f, bm::spike_x + 0.01f};
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
            one.composed = o.composed ? 1u : 0u; // section 13.2: the body's own table
            rr::WeaponEffectsFrame f1 = boom::frame(g, one, v);
            f1.fireballs = boom::bodies + 4 * b;
            f1.compose = boom::compose + b;
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
            Out last_case; // the last run_case's build (the wave's drawn screens)
            const auto run_case = [&](const char* name, unsigned parts, float frac, auto&& place) {
                boom::clear();
                place();
                boom::build(boom::view(tt, 1000, 16));
                ws::HullView v = boom::view(tt, 1000 + std::uint32_t(frac * Lf), 16);
                const Out o = boom::build(v, parts);
                last_case = o;
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
            // the fourth octave (2026-11-07 v3; review: seen from outside, an eye-inside body keeps three): the single body
            // with the eye at 2 R from its centre (R the drawn sphere's), four octaves against three; its weight per screen
            // (budget_weight_fireball_fine4) as budget_weight_fireball x the ratio of the costs, the fence removed
            {
                boom::compose_on = false;
                boom::clear();
                const wc::Event* ef = fighter(600.0);
                const float R_ff = ws::boom_r_f_m(*ef) * m, R0f = 0.5f * float(ef->radius) * wc::world_scale;
                const double z2 = 2.0 * double(ws::boom_body_radius(bm::fighter, R_ff, R0f, 0.3f * Lf)) *
                                  double(bm::body_r_sphere) / double(m);
                const double u4 = run_case("body_four_octaves_fills_frame", boom::p_body, 0.3f, [&] { fighter(z2); });
                const double scr = double(last_case.n.screens);
                boom::octave_cap = 3;
                const double u3 = run_case("body_three_octaves_fills_frame", boom::p_body, 0.3f, [&] { fighter(z2); });
                boom::octave_cap = 4;
                boom::compose_on = true;
                std::printf("EXPLOSION_TIMING width=%u height=%u case=fine4_weight four_us=%.1f three_us=%.1f charged=%.3f "
                            "weight4=%.3f pinned4=%.2f pinned3=%.2f\n",
                            size.first, size.second, u4, u3, scr,
                            u3 > fence_us ? double(wc::look::budget_weight_fireball) * (u4 - fence_us) / (u3 - fence_us) : 0.0,
                            double(wc::look::budget_weight_fireball_fine4), double(wc::look::budget_weight_fireball));
            }
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
            // Section 13 (over sky): the fire wave's front on every pixel (the eye inside the slab 60 m from the plane, the
            // front through the view's centre at 0.3 tau) and its box with no front in it (inside a station's ring at
            // birth: the early-out everywhere); the composed body N 2 .. 5 (no jets), the shared mode at N 4 and the
            // single body at one placement (a capital at 1.8 km, 0.3 L: the eye outside every sphere), the box's screens
            // from its corners; the own death's chase view at N 4 (the shared mode, the eye-inside fence)
            draw_lane(tt, 0.f, -1.f);
            {
                const float Lc = bm::tier_life_ms[bm::capital], Pc = bm::tier_preroll_ms[bm::capital];
                const float tw = 0.3f * bm::wave_tau_x * Lc;
                const float rw = ws::boom_wave_radius(0.5f * bm::secondary_capsule_len * 1288.f * m, bm::wave_r_x * 600.f * m,
                                                      bm::wave_tau_x * Lc, tw);
                const auto wave_at_front = [&] {
                    wc::Event* e = boom::add(bm::capital, 1000, -double(rw) / double(m), 0, 60.0, 1288.0);
                    e->table = 7;
                    boom::broadside(e);
                };
                // the weights per drawn screen (the strips' clipped quads, or the box), the fence removed
                const double wa = run_case("wave_alone_fills_frame", boom::p_wave, (Pc + tw) / Lf, wave_at_front);
                const double sa = double(last_case.n.wave_screens);
                const unsigned qa = last_case.n.wave_quads;
                boom::wave_octave2 = false;
                const double w1 = run_case("wave_one_octave_alone", boom::p_wave, (Pc + tw) / Lf, wave_at_front);
                const double s1 = double(last_case.n.wave_screens);
                boom::wave_octave2 = true;
                // the slab's box at a station's birth from 300 m inside its front (the annulus a disc: the box drawn, every
                // pixel inside the window since the sheet's 2026-11-07 widening; budget_weight_wave_miss stays the old
                // shader's early-out row, that path unchanged)
                const double wm = run_case("wave_box_alone", boom::p_wave, bm::tier_preroll_ms[bm::station] / Lf, [&] {
                    wc::Event* e = boom::add(bm::station, 1000, 0, 0, 300.0, 1800.0);
                    e->table = 6;
                    boom::broadside(e);
                });
                const double sm = double(last_case.n.wave_screens);
                const unsigned qm = last_case.n.wave_quads;
                const auto per = [&](double us, double scr) { return scr > 0 ? (us - fence_us) / 177.5 / scr : 0.0; };
                std::printf("EXPLOSION_TIMING width=%u height=%u case=wave_weight alone_weight=%.3f alone_screens=%.3f "
                            "alone_quads=%u one_octave_weight=%.3f box_weight=%.3f box_screens=%.3f box_quads=%u "
                            "pinned=%.2f pinned_one=%.2f pinned_miss=%.2f\n",
                            size.first, size.second, per(wa, sa), sa, qa, per(w1, s1), per(wm, sm), sm, qm,
                            double(bm::budget_weight_wave), double(bm::budget_weight_wave_one),
                            double(bm::budget_weight_wave_miss));
                if (bm::compose_enabled) { // the composed body's rows (v4: skipped with the switch off)
                const auto box_of = [&](const ws::HullView& v) {
                    const rr::WeaponFireballVertex& a = boom::bodies[0];
                    const rr::WeaponFireballVertex& b = boom::bodies[3];
                    const double x0 = v.cam.m00 * a.corner[0] / a.corner[2], y0 = v.cam.m11 * a.corner[1] / a.corner[2];
                    const double x1 = v.cam.m00 * b.corner[0] / b.corner[2], y1 = v.cam.m11 * b.corner[1] / b.corner[2];
                    return 0.25 * std::fabs(x1 - x0) * std::fabs(y1 - y0);
                };
                const float age = (Pc + 0.3f * Lc) / Lf;
                const auto place = [&] {
                    wc::Event* e = boom::add(bm::capital, 1000, 0, 0, 1800.0, 1288.0);
                    e->table = 7;
                    boom::broadside(e);
                };
                boom::compose_on = false;
                const double us1 = run_case("compose_single_fills_frame", boom::p_body, age, place);
                const double box1 = box_of(boom::view(tt, 0, 16));
                boom::compose_on = true;
                boom::force_jets = 0;
                const char* names[4] = {"compose_n2_fills_frame", "compose_n3_fills_frame", "compose_n4_fills_frame",
                                        "compose_n5_fills_frame"};
                for (unsigned nn = 2; nn <= 5; ++nn) {
                    boom::force_n = nn;
                    const double us = run_case(names[nn - 2], boom::p_body, age, place);
                    const double box = box_of(boom::view(tt, 0, 16));
                    std::printf("EXPLOSION_TIMING width=%u height=%u case=compose_factor n=%u shared=0 box_screens=%.3f "
                                "single_box_screens=%.3f factor=%.3f pinned=%.3f\n",
                                size.first, size.second, nn, box, box1,
                                box > 0 && us1 > fence_us ? (us - fence_us) / box / ((us1 - fence_us) / box1) : 0.0,
                                double(ws::boom_compose_factor(nn, false)));
                }
                boom::force_n = 4;
                boom::force_shared = true;
                {
                    const double us = run_case("compose_n4_shared_fills_frame", boom::p_body, age, place);
                    const double box = box_of(boom::view(tt, 0, 16));
                    std::printf("EXPLOSION_TIMING width=%u height=%u case=compose_factor n=4 shared=1 box_screens=%.3f "
                                "single_box_screens=%.3f factor=%.3f pinned=%.3f\n",
                                size.first, size.second, box, box1,
                                box > 0 && us1 > fence_us ? (us - fence_us) / box / ((us1 - fence_us) / box1) : 0.0,
                                double(ws::boom_compose_factor(4, true)));
                }
                boom::force_shared = false;
                }
                run_case("eye_inside_fighter_n4", boom::p_all, 0.25f, [&] { fighter(24.0); });
                boom::force_n = 0;
                boom::force_jets = 0xffu;
            }
            draw_lane(tt, 5.f * m, 1.f);
            draw_lane(tt, 0.f, -1.f);
            const double f600 = run_case("fighter_600m", boom::p_all, 0.1f, [&] { fighter(600.0); });
            const double f20 = run_case("fighters_20_600m", boom::p_all, 0.1f, [&] {
                for (unsigned k = 0; k < 20; ++k)
                    fighter(600.0 + 20.0 * (k % 5), 0x6f000u + 7u * k, -200.0 + 100.0 * (k / 5) + 13.0 * (k % 5));
            });
            // section 14: the rays' and the sparks' ages (0.05 L: the starburst, the spike and the core; 0.3 L: the spray), the
            // starburst alone (the per-ray quads) on the 300 m fighter and the close capital at its body's birth
            const double f600r = run_case("fighter_600m_rays", boom::p_all, 0.05f, [&] { fighter(600.0); });
            const double f600s = run_case("fighter_600m_sparks", boom::p_all, 0.3f, [&] { fighter(600.0); });
            const auto twenty = [&] {
                for (unsigned k = 0; k < 20; ++k)
                    fighter(600.0 + 20.0 * (k % 5), 0x6f000u + 7u * k, -200.0 + 100.0 * (k / 5) + 13.0 * (k % 5));
            };
            const double f20r = run_case("fighters_20_600m_rays", boom::p_all, 0.05f, twenty);
            const double f20s = run_case("fighters_20_600m_sparks", boom::p_all, 0.3f, twenty);
            std::printf("EXPLOSION_TIMING width=%u height=%u case=spark_shed twenty_halvings=%u twenty_sparks=%u twenty_shed=%u "
                        "twenty_rays=%u\n",
                        size.first, size.second, last_case.n.spark_halvings, last_case.n.embers, last_case.n.sparks_shed,
                        last_case.n.rays);
            // 2026-11-08 review: the sparks' quads (2 FWHM + 0.5 px about head and tail) at full count: fighters at 300 m
            // across the screen, as many as keep the frame's demand within spark_frame_cap (no halving), the sparks alone
            // at 0.3 L; bound the first measurement + 25 % (docs/verification/weapon-effects.md)
            {
                std::uint32_t seqs[32];
                unsigned nseq = 0, total = 0;
                for (unsigned k = 0; k < 32; ++k) {
                    const std::uint32_t sq = 0x71000u + 11u * k;
                    const unsigned c = ws::boom_spark_count(sq, bm::fighter);
                    if (total + c > bm::spark_frame_cap) break;
                    total += c;
                    seqs[nseq++] = sq;
                }
                const double us = run_case("sparks_1024_300m", boom::p_ember, 0.3f, [&] {
                    for (unsigned k = 0; k < nseq; ++k)
                        fighter(300.0 + 15.0 * (k / 4), seqs[k], -90.0 + 60.0 * (k % 4));
                });
                std::printf("EXPLOSION_TIMING width=%u height=%u case=sparks_1024 deaths=%u demand=%u drawn=%u halvings=%u "
                            "screens=%.3f gpu_us=%.1f bound_us=%.1f\n",
                            size.first, size.second, nseq, total, last_case.n.embers, last_case.n.spark_halvings,
                            double(last_case.n.screens), us, boom::sparks_1024_bound_us);
                if (size.first == 5120) report("death_sparks_1024_budget_5120", us <= boom::sparks_1024_bound_us, "");
            }
            run_case("rays_alone_fighter_300m", boom::p_ray, 0.1f, [&] { fighter(300.0); });
            const double rays_close = run_case("rays_alone_capital_close_720m", boom::p_ray,
                                               (bm::tier_preroll_ms[bm::capital] + 0.05f * bm::tier_life_ms[bm::capital]) / Lf,
                                               [&] {
                                                   wc::Event* e = boom::add(bm::capital, 1000, 0, 0, 720.0, 1288.0);
                                                   e->table = 7;
                                                   boom::broadside(e);
                                               });
            if (size.first == 5120) {
                std::snprintf(detail, sizeof detail, "fighter=%.1f,%.1f,%.1f twenty=%.1f,%.1f,%.1f rays_close=%.1f", f600, f600r,
                              f600s, f20, f20r, f20s, rays_close);
                report("death_rays_sparks_budget_5120", std::max(f600, std::max(f600r, f600s)) <= 60.0 &&
                                                            std::max(f20, std::max(f20r, f20s)) <= 150.0, detail);
            }
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
        boom::cap_screens = 1e6f; // the shared budget alone decides here
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
        boom::cap_screens = bm::budget_screens;
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
        Out o;
        // the median of 201 builds, repeated 5 times on the same inputs (2026-11-07 v3: the host's swing is the method's
        // problem): the gate takes the median of the five, all five recorded
        const auto rep_median = [&](bool compose) {
            boom::compose_on = compose;
            std::vector<double> reps;
            for (unsigned k = 0; k < 5; ++k) {
                std::vector<double> us;
                for (unsigned r = 0; r < 201; ++r) {
                    const ws::HullView v = boom::view(g, 1000 + std::uint32_t(0.3f * Lf) + (r & 1u), 16);
                    QueryPerformanceCounter(&a);
                    o = boom::build(v);
                    QueryPerformanceCounter(&b);
                    us.push_back(ticks_us(a, b, freq));
                }
                reps.push_back(boom::median(us));
            }
            boom::compose_on = true;
            return reps;
        };
        const std::vector<double> single = rep_median(false), reps = rep_median(true);
        const double med = boom::median(reps);
        std::printf("EXPLOSION case=cpu deaths=32 bodies=%u quads=%u glows=%u median_us=%.1f single_body_us=%.1f "
                    "repeats_us=%.1f,%.1f,%.1f,%.1f,%.1f bound_us=50\n",
                    o.bodies, o.quads, o.glows, med, boom::median(single), reps[0], reps[1], reps[2], reps[3], reps[4]);
        report("death_stage_cpu_32_deaths", o.bodies == 32 && med <= 50.0, "");
    }
    // ---- 16. The big tiers (sections 2.5, 2.6, 3, 4 and 9; the native spawn ran through R', so the engine's carrier is
    // the hull through the pre-roll: the fixture's scene model draws it). Corvette (R 87 m) at 1.5 km, capital (M2, R
    // 1288 m) at 1.5 km broadside, station (R 1800 m) at 3 km; the close capital (the eye 720 m from its centre).
    {
        struct BigTier {
            unsigned tier;
            double z, r;
            const char* name;
        } bigs[3] = {{bm::corvette, 1500.0, 87.0, "corvette_1500m"}, {bm::capital, 1500.0, 1288.0, "capital_1500m"},
                     {bm::station, 3000.0, 1800.0, "station_3km"}};
        auto big = [&](const BigTier& b, std::uint32_t seq = 0, double z = 0.0) {
            wc::Event* e = boom::add(b.tier, 1000, 0, 0, z > 0.0 ? z : b.z, b.r, seq);
            e->table = b.tier == bm::station ? 6 : 7;
            boom::broadside(e);
            return e;
        };
        // death_big_tier_sizes: the body's mean outline (core off, over sky) within 5 % of R(t) f / z at 0.1 / 0.3 L after
        // its birth P, median of 8 seeds; the clamps: an M7 (R 618 m: 0.9 R = 556 m, inside [80, 600]) and a shipyard
        // (R 4 km: 0.6 R past 1,800 m: clamped).
        boom::core_on = false;
        bool sizes = true;
        for (const BigTier& b : bigs) {
            const float L = bm::tier_life_ms[b.tier], P = bm::tier_preroll_ms[b.tier];
            for (float frac : {0.1f, 0.3f}) {
                std::vector<double> ratios;
                double expected_px = 0;
                for (unsigned j = 0; j < 8; ++j) {
                    boom::clear();
                    wc::Event* e = big(b, 0x6d000u + 977u * (j + 1));
                    latch(g);
                    const Out o = at(g, P + frac * L, boom::p_body);
                    const float R_f = ws::boom_r_f_m(*e) * m;
                    const float R0 = bm::tier_r0_obj[b.tier] * float(e->radius) * wc::world_scale + bm::tier_r0_f[b.tier] * R_f;
                    float c[3];
                    boom::body_centre(o, c);
                    if (!o.n.bodies) {
                        ratios.push_back(0.0);
                        continue;
                    }
                    const classes::burst::Disc d = boom::disc(g, boom::view(g, 0, 16), c, ws::boom_body_radius(b.tier, R_f, R0, frac * L));
                    expected_px = d.R;
                    const boom::Outline ol = boom::outline(boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16))), g.w, d.x, d.y);
                    ratios.push_back(ol.mean / d.R);
                }
                const double med = boom::median(ratios);
                std::printf("EXPLOSION case=size tier=%s age_frac=%.1f expected_px=%.1f outline_over_law=%.3f\n", b.name,
                            double(frac), expected_px, med);
                sizes = sizes && std::fabs(med - 1.0) <= 0.05;
            }
        }
        {
            boom::clear();
            wc::Event* m7 = boom::add(bm::capital, 1000, 0, 0, 5000.0, 618.0);
            wc::Event* yard = boom::add(bm::station, 1000, 0, 0, 9000.0, 4000.0, 0x6d900u);
            const double m7_rf = ws::boom_r_f_m(*m7), yard_rf = ws::boom_r_f_m(*yard);
            std::printf("EXPLOSION case=clamp m7_R_f=%.1f shipyard_R_f=%.1f\n", m7_rf, yard_rf);
            sizes = sizes && std::fabs(m7_rf - 556.2) < 0.2 && yard_rf == double(bm::tier_max_m[bm::station]);
        }
        report("death_big_tier_sizes", sizes, "");
        boom::core_on = true;
        // death_preroll_cadence_and_hull_light: the capital broadside at 1.5 km in 4 ms steps from its first frame to P +
        // 8 ms: 8 secondary births at the law's gaps (each within 20 % plus a step), each centre inside the axis capsule
        // (no box in the cache) and, with the owner's box in the bubble cache, inside that box; the hull light (alone,
        // over a lane plane through the hull's centre) at 0 and just before P at 0.4 / 0.8 x 0.33 E_b x heat(0.6) within
        // 10 %; no body before P; the body born at P at 0.5 R_f.
        {
            const BigTier& cap = bigs[1];
            const float P = bm::tier_preroll_ms[bm::capital], r = bm::tier_cadence[bm::capital];
            const unsigned N = bm::tier_secondaries[bm::capital];
            bool ok = true;
            unsigned births_box = 0, inside_box = 0;
            for (unsigned with_box = 0; with_box < 2; ++with_box) {
                boom::clear();
                wc::Event* e = big(cap);
                const float R = float(e->radius) * wc::world_scale, c0[3] = {0.f, 0.f, float(cap.z) * m};
                static ws::BubbleCache cache;
                cache.clear();
                if (with_box) { // the hull walk's box in the owner frame: 2 R long along row 2, offset 0.1 R along it
                    ws::BubbleCache::Entry& x = cache.entries[0];
                    x.owner = e->owner;
                    x.node = e->node;
                    x.handle = e->handle;
                    x.state = ws::BubbleCache::walked;
                    x.radius = e->radius;
                    x.bubble.c[0] = x.bubble.c[1] = 0.f;
                    x.bubble.c[2] = 0.1f * R;
                    x.bubble.e[0] = 0.2f * R * wc::bubble_scale;
                    x.bubble.e[1] = 0.15f * R * wc::bubble_scale;
                    x.bubble.e[2] = 0.9f * R * wc::bubble_scale;
                    boom::bubbles = &cache;
                }
                std::vector<float> births;
                std::vector<std::array<float, 3>> where;
                unsigned bodies_before = 0;
                double first_body_R = 0;
                for (unsigned a = 0; a <= unsigned(P) + 8; a += 4) {
                    const Out o = boom::build(boom::view(g, 1000 + a, 4), boom::p_all);
                    if (float(a) < P) bodies_before += o.n.bodies;
                    else if (!first_body_R && o.n.bodies)
                        first_body_R = double(o.n.body_R0);
                    for (unsigned k = 0; k < o.n.secondaries_born; ++k) {
                        births.push_back(float(a));
                        where.push_back({o.n.born_at[0], o.n.born_at[1], o.n.born_at[2]});
                    }
                }
                boom::bubbles = nullptr;
                const unsigned record_flags = e->flags;
                bool gaps = births.size() == N;
                for (unsigned k = 1; gaps && k < N; ++k) {
                    float g0 = P * (1.f - r), rn = 1.f;
                    for (unsigned j = 0; j + 1 < N; ++j) rn *= r;
                    g0 /= 1.f - rn;
                    float law = g0;
                    for (unsigned j = 1; j < k; ++j) law *= r;
                    const float gap = births[k] - births[k - 1];
                    gaps = gaps && std::fabs(gap - law) <= 0.2f * law + 4.f + 1e-3f;
                }
                // inside: the capsule (row 2 = +x, 0.7 R half-length, 0.25 R radius) or the box (centre +0.1 R along x)
                unsigned inside = 0;
                for (const auto& w : where) {
                    const float l[3] = {w[0] - c0[0], w[1] - c0[1], w[2] - c0[2]};
                    if (with_box) {
                        // local axes: row 0 = +z, row 1 = -y, row 2 = +x
                        const float lx = l[2], ly = -l[1], lz = l[0] - 0.1f * R;
                        inside += std::fabs(lx) <= 0.2f * R * 1.001f && std::fabs(ly) <= 0.15f * R * 1.001f &&
                                  std::fabs(lz) <= 0.9f * R * 1.001f;
                    } else {
                        const float rad = bm::secondary_capsule_r * R, hl = 0.5f * bm::secondary_capsule_len * R - rad;
                        const float s = std::max(-hl, std::min(hl, l[0]));
                        const float dx = l[0] - s;
                        inside += dx * dx + l[1] * l[1] + l[2] * l[2] <= rad * rad * 1.002f;
                    }
                }
                std::printf("EXPLOSION case=preroll tier=capital_1500m box=%u births=%zu first=%.0f last=%.0f inside=%u "
                            "bodies_before_P=%u first_body_R_over_R_f=%.3f bubble_flags=%u\n",
                            with_box, births.size(), births.empty() ? -1.0 : double(births.front()),
                            births.empty() ? -1.0 : double(births.back()), inside, bodies_before,
                            first_body_R / double(ws::boom_r_f_m(*e) * m), unsigned((record_flags >> 4) & 3u));
                ok = ok && gaps && inside == N && bodies_before == 0 &&
                     std::fabs(first_body_R / double(ws::boom_r_f_m(*e) * m) - double(bm::tier_r0_f[bm::capital])) <= 0.03 &&
                     (record_flags & wc::flag_bubble_looked) && (with_box ? (record_flags & wc::flag_bubble_cached) != 0
                                                                         : (record_flags & wc::flag_bubble_cached) == 0);
                if (with_box) {
                    births_box = unsigned(births.size());
                    inside_box = inside;
                }
            }
            // the hull light alone (p_glow; the secondaries off) on a lane plane through the hull's centre
            boom::clear();
            big(cap);
            latch(g);
            draw_lane(g, float(cap.z) * m, 1.f);
            float hc[3];
            wc::heat_colour(bm::hull_light_t, hc);
            const double maxc = std::max(hc[0], std::max(hc[1], hc[2]));
            const double want0 = double(bm::hull_light_e0) * bm::glow_e * bm::body_e * maxc;
            const ws::HullView v = boom::view(g, 0, 16);
            const double p0 = boom::peak(boom::luma(boom::render(pass, g, at(g, 0.f, boom::p_glow), v)));
            const float late = P - 16.f;
            const double want1 = (double(bm::hull_light_e0) + double(bm::hull_light_e1 - bm::hull_light_e0) * late / P) *
                                 bm::glow_e * bm::body_e * maxc;
            const double p1 = boom::peak(boom::luma(boom::render(pass, g, at(g, late, boom::p_glow), v)));
            // past P it falls with the hull copy's fade: half at P + 500 ms, gone at P + 1 s
            boom::hull_light_mode = 1;
            const double ph = boom::peak(boom::luma(boom::render(pass, g, at(g, P + 500.f, boom::p_glow), v)));
            boom::hull_light_mode = 0;
            const Out after = at(g, P + 1000.f + 16.f, boom::p_glow);
            draw_lane(g, 0.f, -1.f);
            const double want_h = 0.5 * double(bm::hull_light_e1) * bm::glow_e * bm::body_e * maxc;
            std::printf("EXPLOSION case=hull_light tier=capital_1500m peak_0=%.4f want_0=%.4f peak_P=%.4f want_P=%.4f "
                        "peak_P500=%.4f want_P500=%.4f hull_lights_after_fade=%u box_births=%u box_inside=%u\n",
                        p0, want0, p1, want1, ph, want_h, after.n.hull_lights, births_box, inside_box);
            ok = ok && std::fabs(p0 / want0 - 1.0) <= 0.10 && std::fabs(p1 / want1 - 1.0) <= 0.10 &&
                 std::fabs(ph / want_h - 1.0) <= 0.10 && after.n.hull_lights == 0;
            report("death_preroll_cadence_and_hull_light", ok, "");
        }
        // death_big_no_pop_at_birth: the capital and the corvette at their body's birth (16 ms steps, every part but the
        // flash, over sky): the body's first frame at least R_0 f / z (0.5 R_f; the corvette's 0.5 R_obj); no frame's peak
        // above twice the frame before over P - 32 .. P + 32 ms; the capital's star arm under 0.25 H.
        {
            bool ok = true;
            for (unsigned which : {0u, 1u}) {
                const BigTier& b = bigs[which];
                const float P = bm::tier_preroll_ms[b.tier];
                boom::clear();
                wc::Event* e = big(b);
                const float R_f = ws::boom_r_f_m(*e) * m;
                const float R0 = bm::tier_r0_obj[b.tier] * float(e->radius) * wc::world_scale + bm::tier_r0_f[b.tier] * R_f;
                float prev_peak = 0.f;
                bool rise_ok = true;
                double first = 0, arm_px = 0, worst_rise = 0;
                const unsigned start = P > 32.f ? unsigned(P) - 32u : 0u;
                for (unsigned a = 0; a <= unsigned(P) + 32; a += 16) {
                    const ws::HullView v = boom::view(g, 1000 + a, 16);
                    const Out o = boom::build(v, boom::p_all & ~unsigned(boom::p_flash | boom::p_spike));
                    if (a < start) continue;
                    const float pk = boom::peak(boom::luma(boom::render(pass, g, o, v)));
                    if (a > start && prev_peak > 0.f && pk > 2.f * prev_peak) rise_ok = false;
                    if (a > start && prev_peak > 0.f) worst_rise = std::max(worst_rise, double(pk / prev_peak));
                    prev_peak = pk;
                    if (!first && o.n.bodies) {
                        float c[3];
                        boom::body_centre(o, c);
                        first = double(o.n.body_R0) / double(R0);
                    }
                }
                if (b.tier == bm::capital) {
                    const ws::HullView v = boom::view(g, 1000 + unsigned(P) + 16, 16);
                    const Out o = boom::build(v, boom::p_flash);
                    for (unsigned q = 0; q < o.quads; ++q) // the body's star: the widest of the frame's stars
                        arm_px = std::max(arm_px, double(boom::sprites[4 * q].b[3]) * 0.5 * g.w * v.cam.m00 /
                                                      double(boom::sprites[4 * q].a[3]));
                }
                std::printf("EXPLOSION case=no_pop tier=%s first_over_r0=%.3f rise_ok=%u worst_rise=%.3f star_half_px=%.1f "
                            "cap_px=%.1f\n",
                            b.name, first, unsigned(rise_ok), worst_rise, arm_px, double(bm::star_cap_h) * g.h);
                ok = ok && first >= 0.95 && rise_ok && arm_px <= double(bm::star_cap_h) * g.h + 3.0;
            }
            report("death_big_no_pop_at_birth", ok, "");
        }
        // death_big_shock_is_a_shell: the corvette's shell at 0.1 / 0.2 tau (the limb only), forced on (D14: off for every
        // tier in production; the law kept as evidence): its farthest lit pixel within 5 % of the law's r, the interior
        // <= 0.02 of the limb's peak, the peak <= 0.7 E_b. death_shock_off_all_tiers: none drawn for any tier unforced.
        {
            bool ok = true;
            boom::force_shock = true;
            const float L = bm::tier_life_ms[bm::corvette], tau = bm::tier_shock_tau[bm::corvette] * L;
            for (float u : {0.1f, 0.2f}) {
                boom::clear();
                wc::Event* e = big(bigs[0]);
                latch(g);
                const Out so = at(g, u * tau, boom::p_shock);
                const ws::HullView v = boom::view(g, 0, 16);
                const std::vector<float> l = boom::luma(boom::render(pass, g, so, v));
                const float c[3] = {0.f, 0.f, 1500.f * m};
                const classes::burst::Disc d = boom::disc(g, v, c, bm::shock_r_x * ws::boom_r_f_m(*e) * m * std::sqrt(u));
                const boom::Outline ol = boom::outline(l, g.w, d.x, d.y);
                const float sp = boom::peak(l), inner = l[std::size_t(int(d.y)) * g.w + std::size_t(int(d.x))];
                std::printf("EXPLOSION case=shock tier=corvette_1500m u=%.1f law_px=%.1f outline_max_px=%.1f "
                            "interior_over_limb=%.3f shock_peak=%.3f body_e=%.2f boxes=%u\n",
                            double(u), d.R, ol.max, sp > 0 ? double(inner / sp) : 0.0, double(sp), double(bm::body_e), so.shocks);
                ok = ok && so.shocks == 1 && std::fabs(ol.max / d.R - 1.0) <= 0.05 && sp > 0 && inner <= 0.02f * sp &&
                     sp <= 0.7f * bm::body_e;
            }
            boom::force_shock = false;
            report("death_big_shock_is_a_shell", ok, "");
            unsigned shocks = 0;
            for (const BigTier& b : bigs) {
                boom::clear();
                big(b);
                latch(g);
                const float Lb = bm::tier_life_ms[b.tier], Pb = bm::tier_preroll_ms[b.tier];
                for (float fr : {0.02f, 0.1f, 0.2f}) shocks += at(g, Pb + fr * Lb).shocks;
            }
            std::printf("EXPLOSION case=shock_off shock_tiers=%u shocks=%u\n", unsigned(bm::shock_tiers), shocks);
            report("death_shock_off_all_tiers", bm::shock_tiers == 0 && shocks == 0, "");
            // death_big_halo_capped (D15): the capital's halo alone over sky: its farthest pixel above 10 % of its peak
            // <= 1.25 R(t) at 0.2 L, its peak <= 0.2 E_b, none at 0.6 L
            {
                const float Lc = bm::tier_life_ms[bm::capital], Pc = bm::tier_preroll_ms[bm::capital];
                boom::clear();
                wc::Event* e = big(bigs[1]);
                latch(g);
                const ws::HullView v = boom::view(g, 0, 16);
                const std::vector<float> hl = boom::luma(boom::render(pass, g, at(g, Pc + 0.2f * Lc, boom::p_halo), v));
                const float R_f = ws::boom_r_f_m(*e) * m, R0 = bm::tier_r0_f[bm::capital] * R_f;
                const float c[3] = {0.f, 0.f, 1500.f * m};
                const double Rt_px = boom::disc(g, v, c, ws::boom_body_radius(bm::capital, R_f, R0, 0.2f * Lc)).R;
                const boom::Outline ol = boom::outline(hl, g.w, 0.5 * g.w, 0.5 * g.h);
                const float hp = boom::peak(hl);
                const Out late = at(g, Pc + 0.6f * Lc, boom::p_halo);
                std::printf("EXPLOSION case=halo_big tier=capital_1500m age_frac=0.2 peak=%.3f max_r_px=%.1f R_t_px=%.1f "
                            "ratio=%.3f halos_06=%u\n", double(hp), ol.max, Rt_px, Rt_px > 0 ? ol.max / Rt_px : 0.0, late.halos);
                report("death_big_halo_capped", hp > 0.f && ol.max <= 1.25 * Rt_px + 1.0 &&
                                                    hp <= bm::halo_big_e * bm::body_e + 1e-3f && late.halos == 0, "");
            }
        }
        // death_big_light_and_fade: the capital's glow at 0.1 / 0.3 L (a lane plane through its centre) <= 0.4 E_b and
        // none over sky; every tier's elements gone at P + 1.6 L (+ the floor); the secondaries gone by P + 1.8 x 0.3 L.
        {
            bool ok = true;
            const float L = bm::tier_life_ms[bm::capital], P = bm::tier_preroll_ms[bm::capital];
            boom::clear();
            big(bigs[1]);
            latch(g);
            const ws::HullView v = boom::view(g, 0, 16);
            draw_lane(g, 1500.f * m, 1.f);
            boom::hull_light_mode = 2; // the body's light alone (the hull light has its own gate)
            const double g1 = boom::peak(boom::luma(boom::render(pass, g, at(g, P + 0.1f * L, boom::p_glow), v)));
            const double g3 = boom::peak(boom::luma(boom::render(pass, g, at(g, P + 0.3f * L, boom::p_glow), v)));
            boom::hull_light_mode = 0;
            draw_lane(g, 0.f, -1.f);
            const double sky = boom::peak(boom::luma(boom::render(pass, g, at(g, P + 0.1f * L, boom::p_glow), v)));
            ok = g1 <= 0.4 * bm::body_e && g3 <= 0.4 * bm::body_e && g1 > 0.0 && sky == 0.0;
            unsigned left = 0;
            for (const BigTier& b : bigs) {
                boom::clear();
                big(b);
                latch(g);
                const float Lb = bm::tier_life_ms[b.tier], Pb = bm::tier_preroll_ms[b.tier];
                const Out o = at(g, Pb + (bm::smoke_t0 + bm::smoke_span) * Lb + 3.f * 16.f + 1.f);
                left += o.quads + o.glows + o.bodies + o.smokes + o.darks + o.shocks + o.halos + o.n.secondaries;
                if (b.tier != bm::corvette) {
                    const float Ls = bm::secondary_life * Lb;
                    const Out s2 = at(g, Pb + (bm::fragment_life > bm::ember_life[2] ? bm::fragment_life : bm::ember_life[2]) *
                                                  Ls * (1.f + 0.5f * bm::ember_stagger) + 64.f,
                                      boom::p_secondary);
                    left += s2.quads + s2.glows + s2.bodies;
                }
            }
            std::printf("EXPLOSION case=big_light tier=capital_1500m glow_peak_01=%.3f glow_peak_03=%.3f sky=%.4f left_at_life=%u\n",
                        g1, g3, sky, left);
            report("death_big_light_and_fade", ok && left == 0, "");
        }
        // death_big_eye_inside_bounded (decision D2's law): the close capital (the eye 720 m from its centre, broadside):
        // at 0.25 L after P the frame lit >= 80 % and not uniform (std >= 0.05 of the mean); at 0.6 L a dark share >= 0.15.
        {
            bool ok = true;
            const float L = bm::tier_life_ms[bm::capital], P = bm::tier_preroll_ms[bm::capital];
            for (float frac : {0.25f, 0.6f}) {
                boom::clear();
                big(bigs[1], 0, 720.0);
                latch(g);
                const ws::HullView v = boom::view(g, 0, 16);
                const std::vector<float> l = boom::luma(boom::render(pass, g, at(g, P + frac * L, boom::p_body), v));
                const float pk = boom::peak(l);
                double lit = 0, dark = 0, mean = 0, var = 0;
                for (float x : l) {
                    lit += x > 0.f;
                    dark += x < 0.1f * pk;
                    mean += x;
                }
                lit /= double(l.size());
                dark /= double(l.size());
                mean /= double(l.size());
                for (float x : l) var += (x - mean) * (x - mean);
                const double cv = mean > 0 ? std::sqrt(var / double(l.size())) / mean : 0.0;
                std::printf("EXPLOSION case=eye_inside tier=capital_720m eye_m=720 age_frac=%.2f lit_share=%.3f dark_share=%.3f "
                            "std_over_mean=%.3f peak=%.3f\n", double(frac), lit, dark, cv, double(pk));
                // 0.9 -> 0.8 with the composed body (the coordinator, 2026-11-07 v3): its eye-inside mode draws two
                // sub-bursts set apart (D35), 0.84 of the view lit is a full wash; 0.9 for the single body (compose off)
                if (frac < 0.3f) ok = ok && lit >= (bm::compose_enabled ? 0.8 : 0.9) && cv >= 0.05;
                else ok = ok && dark >= 0.15;
            }
            report("death_big_eye_inside_bounded", ok, "");
        }
        // death_big_seta6: the capital at 100 ms steps (SETA 6 at 60 fps): every secondary born (8, several in a frame),
        // the body's game life unchanged (its last frame within a step of P + L), its star on 3 frames.
        {
            boom::clear();
            big(bigs[1]);
            const float L = bm::tier_life_ms[bm::capital], P = bm::tier_preroll_ms[bm::capital];
            unsigned born = 0, last = 0, star_frames = 0;
            for (unsigned a = 0; a <= unsigned(P + L) + 300; a += 100) {
                const Out o = boom::build(boom::view(g, 1000 + a, 100), boom::p_all);
                born += o.n.secondaries_born;
                if (o.n.bodies) last = a;
                if (float(a) >= P) {
                    const Out s = boom::build(boom::view(g, 1000 + a, 100), boom::p_flash);
                    star_frames += s.n.stars > 0;
                }
            }
            std::printf("EXPLOSION case=seta tier=capital step_ms=100 secondaries_born=%u body_last_age_ms=%u life_ms=%.0f "
                        "star_frames=%u\n", born, last, double(P + L), star_frames);
            report("death_big_seta6", born == bm::tier_secondaries[bm::capital] && float(last) < P + L &&
                                          float(last) + 100.f >= P + L && star_frames == 3, "");
        }
        // death_budget_capital_close_5120: the close capital over its own hull (the scene model's lane), fenced best of 60
        // every 100 ms from P - 100 to P + 2000: <= 450 us each, the shed state logged; with every step shed (a budget
        // of 0) <= 300 us at the body's birth.
        {
            Target tt;
            if (make_target(tt, 5120, 1440)) {
                Com<IDirect3DQuery9> ev;
                dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
                LARGE_INTEGER freq, a0, b0;
                QueryPerformanceFrequency(&freq);
                auto time_frame = [&](const Out& o, const ws::HullView& v) {
                    double best = 1e30;
                    const rr::WeaponEffectsFrame f = boom::frame(tt, o, v);
                    for (int i = -60; i < 60; ++i) {
                        dev->BeginScene();
                        dev->SetRenderTarget(0, tt.scene_s.p);
                        dev->Clear(0, nullptr, D3DCLEAR_TARGET, grey, 1.f, 0);
                        fence(ev.p);
                        QueryPerformanceCounter(&a0);
                        rr::WeaponEffectsReport rep{};
                        pass.run(f, &rep);
                        fence(ev.p);
                        QueryPerformanceCounter(&b0);
                        dev->EndScene();
                        if (i >= 0) best = std::min(best, ticks_us(a0, b0, freq));
                    }
                    return best;
                };
                const float P = bm::tier_preroll_ms[bm::capital];
                boom::clear();
                wc::Event* e = big(bigs[1], 0, 720.0);
                const boom::Hull hull = boom::hull_of(*e);
                double worst = 0, worst_at = 0;
                unsigned worst_shed = 0, wave_frames = 0, wave_span = 0;
                const float Lw = bm::wave_life_x * bm::tier_life_ms[bm::capital];
                for (unsigned a = 0; a <= unsigned(P) + 2000; a += 50) {
                    const ws::HullView v = boom::view(tt, 1000 + a, 16);
                    const Out o = boom::build(v); // every 50 ms (the octave hysteresis sees a stream of frames)
                    // the frames P .. P + L_w with the wave on screen (its strips reach the target: built, drawn or shed;
                    // face-on at 720 m the front leaves the frame within ~0.2 s, the strips then build nothing)
                    if (float(a) >= P && float(a) < P + Lw && o.waves + o.n.waves_shed > 0) {
                        ++wave_span;
                        wave_frames += o.waves > 0;
                    }
                    if (float(a) + 100.f < P || (a % 100)) continue;
                    boom::hull_lane(tt, v, &hull, boom::hull_alpha(bm::capital, float(a)));
                    const double us = time_frame(o, v);
                    std::printf("EXPLOSION_TIMING width=5120 height=1440 case=capital_close_%dms bodies=%u quads=%u glows=%u "
                                "smokes=%u shocks=%u octaves=%u screens=%.3f gpu_us=%.1f weight=%.3f secondaries=%u shed=%u "
                                "secondaries_shed=%u smokes_shed=%u shocks_shed=%u glows_shed=%u waves=%u waves_shed=%u "
                                "wave_quads=%u wave_screens=%.3f wave_octaves_shed=%u composed=%u compose_shared=%u "
                                "compose_dropped=%u\n",
                                int(a) - int(P), o.bodies, o.quads, o.glows, o.smokes, o.shocks, o.n.octaves,
                                double(o.n.screens), us, us / 177.5, o.n.secondaries, o.n.shed, o.n.secondaries_shed,
                                o.n.smokes_shed, o.n.shocks_shed, o.n.glows_shed, o.waves, o.n.waves_shed, o.n.wave_quads,
                                double(o.n.wave_screens), o.n.wave_octaves_shed, o.n.composed, o.n.compose_shared,
                                o.n.compose_dropped);
                    if (us > worst) {
                        worst = us;
                        worst_at = double(a) - double(P);
                        worst_shed = o.n.shed;
                    }
                }
                // every step shed: the budget 0 at the body's birth + 100 ms
                boom::clear();
                big(bigs[1], 0, 720.0);
                for (unsigned a = 0; a <= unsigned(P) + 100; a += 50) boom::build(boom::view(tt, 1000 + a, 16));
                ws::HullView v = boom::view(tt, 1000 + unsigned(P) + 100, 16);
                v.budget_left = 0.f;
                const Out all_shed = boom::build(v);
                boom::hull_lane(tt, v, &hull, boom::hull_alpha(bm::capital, P + 100.f));
                const double shed_us = time_frame(all_shed, v);
                std::printf("EXPLOSION_TIMING width=5120 height=1440 case=capital_close_all_shed bodies=%u quads=%u glows=%u "
                            "smokes=%u shocks=%u octaves=%u screens=%.3f gpu_us=%.1f weight=%.3f secondaries=%u shed=%u\n",
                            all_shed.bodies, all_shed.quads, all_shed.glows, all_shed.smokes, all_shed.shocks,
                            all_shed.n.octaves, double(all_shed.n.screens), shed_us, shed_us / 177.5, all_shed.n.secondaries,
                            all_shed.n.shed);
                std::snprintf(detail, sizeof detail, "worst=%.1f at=%.0f shed=%u all_shed=%.1f wave_frames=%u/%u", worst, worst_at,
                              worst_shed, shed_us, wave_frames, wave_span);
                std::printf("EXPLOSION case=close_capital_wave wave_frames=%u span=%u share=%.3f\n", wave_frames, wave_span,
                            wave_span ? double(wave_frames) / wave_span : 0.0);
                report("death_budget_capital_close_5120", worst <= 450.0 && shed_us <= 300.0, detail);
                // section 13.1: the wave drawn (not shed) on every frame P .. P + L_w it is on screen (2026-11-07 v3: the
                // wave sheds last), within that fence
                report("wave_drawn_close_capital", wave_span && wave_frames == wave_span, detail);
                draw_lane(tt, 0.f, -1.f);
                tt.release();
            } else
                report("death_budget_capital_close_5120", false, "target");
        }
        // death_big_stage_cpu (the performance pass): four capitals and two stations through their pre-roll's end (every
        // secondary live, the bodies born) plus 26 fighters, median of 201 builds, against the 50 us bound.
        {
            boom::clear();
            for (unsigned k = 0; k < 4; ++k) big(bigs[1], 0x72000u + 13u * k, 1500.0 + 400.0 * k);
            for (unsigned k = 0; k < 2; ++k) big(bigs[2], 0x72100u + 13u * k, 3000.0 + 600.0 * k);
            for (unsigned k = 0; k < 26; ++k) fighter(600.0 + 15.0 * (k % 4), 0x72200u + 13u * k, -300.0 + 20.0 * k);
            latch(g);
            LARGE_INTEGER freq, a0, b0;
            QueryPerformanceFrequency(&freq);
            Out o;
            // the median of 201 builds, 5 repeats on the same inputs, the gate on their median (all five recorded); the
            // single body's and the no-wave figures beside it (their shares; not gated)
            const auto rep_median = [&](unsigned mode) {
                boom::compose_on = mode != 1;
                boom::wave_off = mode == 2;
                std::vector<double> reps;
                for (unsigned k = 0; k < 5; ++k) {
                    std::vector<double> us;
                    for (unsigned r = 0; r < 201; ++r) {
                        const ws::HullView v = boom::view(g, 1000 + 1600 + (r & 1u), 16);
                        QueryPerformanceCounter(&a0);
                        const Out q = boom::build(v);
                        QueryPerformanceCounter(&b0);
                        if (!mode) o = q;
                        us.push_back(ticks_us(a0, b0, freq));
                    }
                    reps.push_back(boom::median(us));
                }
                boom::compose_on = true;
                boom::wave_off = false;
                return reps;
            };
            const std::vector<double> single = rep_median(1), no_wave = rep_median(2), reps = rep_median(0);
            const double med = boom::median(reps);
            // the stage's cost follows the quads it writes (the 32 fighters: 0.065 us a quad): bound 0.08 us a drawn quad
            // and 100 us in all
            std::printf("EXPLOSION case=cpu deaths=32 big=6 bodies=%u secondaries=%u quads=%u glows=%u waves=%u median_us=%.1f "
                        "single_body_us=%.1f no_wave_us=%.1f repeats_us=%.1f,%.1f,%.1f,%.1f,%.1f us_per_quad=%.4f "
                        "bound_us_per_quad=0.08 bound_us=100\n",
                        o.n.bodies, o.n.secondaries, o.quads, o.glows, o.waves, med, boom::median(single),
                        boom::median(no_wave), reps[0], reps[1], reps[2], reps[3], reps[4], o.quads ? med / o.quads : 0.0);
            report("death_big_stage_cpu", o.n.secondaries > 0 && o.quads && med <= 0.08 * o.quads && med <= 100.0, "");
        }
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
        // the wave program alone refused (fault bit 10; review 2026-11-07): the set enabled, a capital's death at P + 0.5
        // tau drawn (its body) without its wave, wave_enabled false with its reason
        const auto capital_wave = [&]() {
            boom::clear();
            wc::Event* ec = boom::add(bm::capital, 1000, 0, 0, 5000.0, 1288.0, 0x6b111u);
            ec->table = 7;
            boom::broadside(ec);
            latch(g);
            return at(g, bm::tier_preroll_ms[bm::capital] + 0.5f * bm::wave_tau_x * bm::tier_life_ms[bm::capital], boom::p_all);
        };
        {
            rr::WeaponEffectsPass no_wave;
            no_wave.set_faults(1024);
            const HRESULT hw = no_wave.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format, true,
                                              false, true);
            const Out ow = capital_wave();
            dev->BeginScene();
            dev->SetRenderTarget(0, g.scene_s.p);
            rr::WeaponEffectsReport rw{};
            no_wave.run(boom::frame(g, ow, v), &rw);
            dev->EndScene();
            std::printf("EXPLOSION case=wave_refused attach=%08lx boom=%u wave=%u wave_reason=%s bodies_drawn=%u waves_built=%u "
                        "waves_drawn=%u\n",
                        hw, unsigned(no_wave.caps().boom_enabled), unsigned(no_wave.caps().wave_enabled),
                        no_wave.caps().wave_reason, rw.fireball_boxes, ow.waves, rw.wave_boxes);
            report("wave_refused_body_drawn", SUCCEEDED(hw) && no_wave.caps().boom_enabled && !no_wave.caps().wave_enabled &&
                                                  !std::strcmp(no_wave.caps().wave_reason, "wave_program_create") &&
                                                  rw.fireball_boxes >= 1 && ow.waves >= 1 && rw.wave_boxes == 0, "");
            no_wave.detach();
        }
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
        // and a capital's wave after the Reset (review 2026-11-07): wave_enabled, its quads drawn
        bool wave_drew = false;
        if (mt && SUCCEEDED(er)) {
            const Out ow = capital_wave();
            rr::WeaponEffectsReport aw{};
            dev->BeginScene();
            dev->SetRenderTarget(0, g.scene_s.p);
            pass.run(boom::frame(g, ow, v), &aw);
            dev->EndScene();
            wave_drew = pass.caps().wave_enabled && ow.waves >= 1 && aw.wave_boxes == ow.waves;
        }
        std::printf("EXPLOSION case=reset reset=%08lx ensure=%08lx drew=%u wave_enabled=%u wave_drew=%u\n", reset, er,
                    unsigned(drew), unsigned(pass.caps().wave_enabled), unsigned(wave_drew));
        report("death_reset_recovers", mt && SUCCEEDED(er) && pass.resources_ready() && drew && wave_drew, "");
    }
    // ---- 17. Section 13 (explosion-look.md 13.1, 13.2): the fire wave of the capital (and station) deaths and the
    // composed body of every tier. The wave's geometry at 5120x1440 (the capital at 5 km face-on: the whole ring fits;
    // at 1.5 km from above: the bar), its colour, its broken circumference, the swept hulls' light; the refraction
    // request and its strips (the GPU half in engine_shimmer_fixture.cpp weapon_wave_case); the composed body's silhouette,
    // connectedness, radiance, births, jets, eye inside, LOD floor and box over 8 seeds (fighter 600 m, capital 1.5 km,
    // station 3 km at 1080p).
    {
        const float Lc = bm::tier_life_ms[bm::capital], Pc = bm::tier_preroll_ms[bm::capital];
        const float tau = bm::wave_tau_x * Lc, Lw = bm::wave_life_x * Lc;
        auto capital = [&](double z, bool above, std::uint32_t seq = 0, double x = 0.0) {
            wc::Event* e = boom::add(bm::capital, 1000, x, 0, z, 1288.0, seq);
            e->table = 7;
            above ? boom::from_above(e) : boom::broadside(e);
            return e;
        };
        // the wave alone at tb ms after the body's birth (the record latched at 0)
        auto wave_at = [&](Target& t, float tb, unsigned parts = boom::p_wave) {
            boom::build(boom::view(t, 1000, 16), 0u);
            return boom::build(boom::view(t, 1000 + std::uint32_t(Pc + tb + 0.5f), 16), parts);
        };
        // 64 bins of the stretched in-plane angle about (cx, cy): each bin's peak and its outer radius above 10 % of that
        // peak; over the bins whose peak is >= 10 % of the frame's (the lit arcs) the median outer radius, and (out) the
        // lit bins' outer radii
        auto wave_outer_median = [](const std::vector<float>& l, unsigned w, double cx, double cy, float pk,
                                    std::vector<double>* radii, double* peak_median = nullptr) {
            double bin_pk[64] = {}, bin_out[64] = {}, bin_at[64] = {};
            for (std::size_t i = 0; i < l.size(); ++i) {
                const double dx = (double(i % w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / w) + 0.5 - cy;
                const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                if (double(l[i]) > bin_pk[bin]) {
                    bin_pk[bin] = double(l[i]);
                    bin_at[bin] = std::hypot(dx, dy);
                }
            }
            if (peak_median) {
                std::vector<double> at;
                for (unsigned b = 0; b < 64; ++b)
                    if (bin_pk[b] >= 0.1 * double(pk) && bin_pk[b] > 0.0) at.push_back(bin_at[b]);
                *peak_median = at.empty() ? 0.0 : boom::median(at);
            }
            for (std::size_t i = 0; i < l.size(); ++i) {
                const double dx = (double(i % w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / w) + 0.5 - cy;
                const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                if (l[i] > 0.1 * bin_pk[bin]) bin_out[bin] = std::max(bin_out[bin], std::hypot(dx, dy));
            }
            std::vector<double> lit;
            for (unsigned b = 0; b < 64; ++b)
                if (bin_pk[b] >= 0.1 * double(pk) && bin_pk[b] > 0.0) lit.push_back(bin_out[b]);
            if (radii) *radii = lit;
            return lit.empty() ? 0.0 : boom::median(lit);
        };
        Target tt;
        if (make_target(tt, 5120, 1440)) {
            draw_lane(tt, 0.f, -1.f);
            const ws::HullView v0 = boom::view(tt, 0, 16);
            const double px_x = 0.5 * tt.w * double(v0.cam.m00);
            const double cx = 0.5 * tt.w, cy = 0.5 * tt.h;
            const float R_f = 600.f * m, we = bm::wave_w_x * R_f, r0w = 0.5f * bm::secondary_capsule_len * 1288.f * m;
            auto law_r = [&](float tb) { return ws::boom_wave_radius(r0w, bm::wave_r_x * R_f, tau, tb); };
            // (a) wave_radius_law, wave_never_brightest: the 10 %-of-peak outer radius in the stretched plane (the long
            // axis is the view's x) against the law's outer 10 % point r(t) + sqrt(ln 10) w_e at 0.1 / 0.5 / 1.0 tau
            bool radius_ok = true, bright_ok = true;
            for (float u : {0.1f, 0.5f, 1.0f}) {
                boom::clear();
                capital(5000.0, false);
                const Out o = wave_at(tt, u * tau);
                const std::vector<float> l = boom::luma(boom::render(pass, tt, o, v0));
                const float pk = boom::peak(l);
                // the flame tongues shift the front both ways about r(t) (2026-11-07): the median over the lit angular bins
                // of each bin's brightest pixel's radius (the edge's peak, z = 0) against r(t); the outer 10 % radius (each
                // bin's own peak, the median: the bins' widest tongue) printed beside it
                double front = 0;
                const double outer = wave_outer_median(l, tt.w, cx, cy, pk, nullptr, &front);
                const double law = double(law_r(u * tau)) * px_x / (5000.0 * m),
                             law_outer = (double(law_r(u * tau)) + 1.5174 * double(we)) * px_x / (5000.0 * m);
                boom::clear();
                capital(5000.0, false);
                const Out ob = wave_at(tt, u * tau, boom::p_body);
                const float pb = boom::peak(boom::luma(boom::render(pass, tt, ob, v0)));
                std::printf("EXPLOSION case=wave_radius tau_frac=%.1f waves=%u front_px=%.1f law_px=%.1f ratio=%.3f "
                            "outer_px=%.1f law_outer_px=%.1f peak=%.3f body_peak=%.3f\n",
                            double(u), o.waves, front, law, law > 0 ? front / law : 0.0, outer, law_outer, double(pk),
                            double(pb));
                radius_ok = radius_ok && o.waves == 1 && std::fabs(front / law - 1.0) <= 0.05;
                bright_ok = bright_ok && pk <= pb;
            }
            boom::clear();
            capital(5000.0, false);
            const unsigned after = wave_at(tt, Lw + 20.f).waves;
            unsigned others = 0;
            for (unsigned tier : {unsigned(bm::fighter), unsigned(bm::corvette), unsigned(bm::missile)}) {
                boom::clear();
                boom::add(tier, 1000, 0, 0, 600.0, tier == bm::corvette ? 87.0 : 14.0);
                boom::build(boom::view(tt, 1000, 16));
                others += boom::build(boom::view(tt, 1100, 16)).waves;
            }
            std::printf("EXPLOSION case=wave_life after_life=%u other_tiers=%u\n", after, others);
            report("wave_radius_law", radius_ok, "");
            report("wave_never_brightest", bright_ok && after == 0 && others == 0, "");
            // (b) wave_thick_not_a_line at 0.5 tau: face-on (5 km) the band's radial 10 % width along 16 rays (the median
            // of the lit ones); edge-on (1.5 km from above) the bar's vertical 10 % thickness over 8 columns
            double band_w = 0, bar_h = 0, edge_sheet = 0, sheet_07 = 0, tongue_depth = 0;
            const double rf5 = double(R_f) * px_x / (5000.0 * m); // R_f in pixels at 5 km
            {
                boom::clear();
                capital(5000.0, false);
                const Out o = wave_at(tt, 0.5f * tau);
                const std::vector<float> l = boom::luma(boom::render(pass, tt, o, v0));
                const float pk = boom::peak(l);
                std::vector<double> widths, ratios, at07;
                for (unsigned k = 0; k < 16; ++k) {
                    const double a = 6.2831853 * (k + 0.5) / 16.0, ux = std::cos(a) * double(bm::wave_stretch), uy = std::sin(a);
                    float best = 0.f;
                    std::vector<float> prof;
                    for (double s = 0; s < 2.0 * tt.w; s += 1.0) {
                        const double x = cx + s * ux, y = cy + s * uy;
                        if (x < 0 || y < 0 || x >= tt.w || y >= tt.h) break;
                        prof.push_back(l[std::size_t(y) * tt.w + std::size_t(x)]);
                        best = std::max(best, prof.back());
                    }
                    if (!(best > 0.1f * pk)) continue;
                    double lit = 0;
                    for (float p : prof) lit += p > 0.1f * best;
                    widths.push_back(lit);
                    // the radial profile behind this ray's edge (its peak): the sheet's mean 0.2 .. 0.6 R_f behind, its
                    // value 0.7 R_f behind
                    std::size_t at = 0;
                    for (std::size_t q = 0; q < prof.size(); ++q)
                        if (prof[q] == best) at = q;
                    double sum = 0, cnt = 0;
                    for (double d = 0.2 * rf5; d <= 0.6 * rf5; d += 1.0)
                        if (double(at) >= d) {
                            sum += prof[at - std::size_t(d)];
                            ++cnt;
                        }
                    if (cnt > 0 && sum > 0) ratios.push_back(double(best) / (sum / cnt));
                    if (double(at) >= 0.7 * rf5) at07.push_back(prof[at - std::size_t(0.7 * rf5)] / double(best));
                }
                band_w = boom::median(widths);
                edge_sheet = ratios.empty() ? 0.0 : boom::median(ratios);
                sheet_07 = at07.empty() ? 0.0 : boom::median(at07);
                // the flame tongues: the lit bins' outer radii (each above 10 % of its own peak), max - min over R_f
                std::vector<double> radii;
                wave_outer_median(l, tt.w, cx, cy, pk, &radii);
                if (!radii.empty())
                    tongue_depth = (*std::max_element(radii.begin(), radii.end()) - *std::min_element(radii.begin(), radii.end())) / rf5;
                boom::clear();
                capital(5000.0, true);
                const Out oa = wave_at(tt, 0.5f * tau);
                const std::vector<float> la = boom::luma(boom::render(pass, tt, oa, v0));
                std::vector<double> heights;
                for (unsigned k = 0; k < 8; ++k) {
                    const unsigned x = unsigned(cx + (double(k) - 3.5) * 0.1 * cx);
                    float best = 0.f;
                    for (unsigned y = 0; y < tt.h; ++y) best = std::max(best, la[std::size_t(y) * tt.w + x]);
                    if (!(best > 0.f)) continue;
                    double lit = 0;
                    for (unsigned y = 0; y < tt.h; ++y) lit += la[std::size_t(y) * tt.w + x] > 0.1f * best;
                    heights.push_back(lit);
                }
                bar_h = boom::median(heights);
            }
            // the bar's need at the near crossing's depth (5 km less the front's radius: the column through the centre)
            const double need5 = 0.2 * double(R_f) * px_x / (5000.0 * m),
                         need15 = 0.2 * double(R_f) * px_x / (5000.0 * m - double(law_r(0.5f * tau)));
            std::printf("EXPLOSION case=wave_thickness band_px=%.1f band_need_px=%.1f bar_px=%.1f bar_need_px=%.1f\n", band_w,
                        need5, bar_h, need15);
            report("wave_thick_not_a_line", band_w >= need5 && bar_h >= need15, "");
            // the trailing sheet and the tongues (2026-11-07, the hoop's fix): band and sheet >= 0.6 R_f at 10 % of each
            // ray's peak; the edge >= 1.4 x the sheet's mean 0.2 .. 0.6 R_f behind it and the sheet still >= 1 % of the
            // peak 0.7 R_f behind (medians of the lit rays); the lit bins' outer radii spread >= 0.2 R_f
            std::printf("EXPLOSION case=wave_sheet thick_rf=%.3f need_rf=0.60 edge_over_sheet=%.2f sheet_07=%.4f "
                        "tongue_depth_rf=%.3f\n",
                        band_w / rf5, edge_sheet, sheet_07, tongue_depth);
            report("wave_sheet_thick", band_w >= 0.6 * rf5, "");
            report("wave_hot_edge_trailing_sheet", edge_sheet >= 1.4 && sheet_07 >= 0.01, "");
            report("wave_flame_tongues", tongue_depth >= 0.2, "");
            // (c) wave_broken_circumference (64 bins of the stretched angle at 0.5 tau, 5 km), wave_fire_not_grey (the
            // edge over the band along the brightest bin, the band's R / G, the hue at 0.2 / 0.9 L_w, the chroma)
            {
                boom::clear();
                capital(5000.0, false);
                const Out o = wave_at(tt, 0.5f * tau);
                const std::vector<float> rgb = boom::render(pass, tt, o, v0);
                const std::vector<float> l = boom::luma(rgb);
                const float pk = boom::peak(l);
                const double r_px = double(law_r(0.5f * tau)) * px_x / (5000.0 * m), w_px = double(we) * px_x / (5000.0 * m);
                // per bin its peak and the peak's radius (the edge, shifted by the tongues), then the band's pixels about
                // its centre behind each bin's own edge (wave_trail_span / 2 w_e): the edge over the band, the median of
                // the lit bins; the band's colour summed over them
                double bins[64] = {}, bin_r[64] = {}, bin_a[64] = {}, bin_ray[64] = {}, bin_band[64] = {}, band[3] = {};
                (void)r_px;
                unsigned band_n = 0;
                for (std::size_t i = 0; i < l.size(); ++i) {
                    const double dx = (double(i % tt.w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / tt.w) + 0.5 - cy;
                    const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                    if (double(l[i]) > bins[bin]) {
                        bins[bin] = double(l[i]);
                        bin_r[bin] = std::hypot(dx, dy);
                        bin_a[bin] = std::atan2(dy, dx);
                    }
                }
                // the band behind each bin's edge along that edge pixel's own ray (review 2026-11-07: across a whole bin
                // the tongues put another part's edge into the window)
                for (unsigned b = 0; b < 64; ++b) {
                    if (!(bins[b] >= 0.1 * pk)) continue;
                    for (double z = -0.5 * double(bm::wave_trail_span) - 0.4; z <= -0.5 * double(bm::wave_trail_span) + 0.4;
                         z += 0.1) {
                        const double rr = bin_r[b] + z * w_px;
                        const double x = cx + double(bm::wave_stretch) * rr * std::cos(bin_a[b]), y = cy + rr * std::sin(bin_a[b]);
                        if (x < 0 || y < 0 || x >= tt.w || y >= tt.h) continue;
                        bin_ray[b] = std::max(bin_ray[b], double(l[std::size_t(y) * tt.w + std::size_t(x)]));
                    }
                }
                const double bc = 0.5 * double(bm::wave_trail_span);
                for (std::size_t i = 0; i < l.size(); ++i) {
                    const double dx = (double(i % tt.w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / tt.w) + 0.5 - cy;
                    const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                    const double z = (std::hypot(dx, dy) - bin_r[bin]) / w_px;
                    if (bins[bin] >= 0.1 * pk && z > -bc - 0.4 && z < -bc + 0.4) {
                        for (unsigned q = 0; q < 3; ++q) band[q] += rgb[3 * i + q];
                        bin_band[bin] = std::max(bin_band[bin], double(l[i]));
                        ++band_n;
                    }
                }
                std::vector<double> ebs;
                for (unsigned b = 0; b < 64; ++b)
                    if (bins[b] >= 0.1 * pk && bin_ray[b] > 0) ebs.push_back(bins[b] / bin_ray[b]);
                const double edge_v = ebs.empty() ? 0.0 : boom::median(ebs), band_v = 1.0;
                unsigned lit = 0, lit30 = 0, lit50 = 0;
                double mx = 0, mn = 1e30;
                for (double b : bins) {
                    lit += b >= 0.1 * pk;
                    lit30 += b >= 0.3 * pk;
                    lit50 += b >= 0.5 * pk;
                    mx = std::max(mx, b);
                    mn = std::min(mn, b);
                }
                const double share = lit / 64.0, ratio = mn > 0 ? mx / mn : 1e9;
                // v7 (D55): the band is never transparent (the dark-red floor), so every bin passes 10 % of the peak; the
                // break is read from each bin's band (its mean luminance 0 .. wave_trail_span w_e behind its own edge):
                // the arcs' max / min >= 2 and their coefficient of variation >= 0.25, and every bin's peak >= 5 % of the
                // ring's (no transparent arc)
                double bsum[64] = {}, bcnt[64] = {};
                for (std::size_t i = 0; i < l.size(); ++i) {
                    const double dx = (double(i % tt.w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / tt.w) + 0.5 - cy;
                    const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                    const double z = (std::hypot(dx, dy) - bin_r[bin]) / w_px;
                    if (z <= 0.0 && z > -double(bm::wave_trail_span)) {
                        bsum[bin] += l[i];
                        ++bcnt[bin];
                    }
                }
                double am = 0, amx = 0, amn = 1e30, av = 0;
                for (unsigned b = 0; b < 64; ++b) {
                    bsum[b] = bcnt[b] > 0 ? bsum[b] / bcnt[b] : 0.0;
                    am += bsum[b] / 64.0;
                    amx = std::max(amx, bsum[b]);
                    amn = std::min(amn, bsum[b]);
                }
                for (unsigned b = 0; b < 64; ++b) av += (bsum[b] - am) * (bsum[b] - am) / 64.0;
                const double arc_ratio = amn > 0 ? amx / amn : 1e9, arc_cv = am > 0 ? std::sqrt(av) / am : 0.0;
                std::printf("EXPLOSION case=wave_circumference lit_share=%.3f lit30=%.3f lit50=%.3f max_over_min=%.2f "
                            "min_bin_peak=%.3f arc_max_over_min=%.2f arc_cv=%.3f\n",
                            share, lit30 / 64.0, lit50 / 64.0, ratio, mn / double(pk), arc_ratio, arc_cv);
                report("wave_broken_circumference", arc_ratio >= 2.0 && arc_cv >= 0.25 && mn >= 0.05 * double(pk), "");
                // review 2026-11-07: the break follows the record's seeds: another sequence's 64-bin peaks correlate < 0.5
                {
                    boom::clear();
                    capital(5000.0, false, 0x5a3c1u);
                    const Out o2 = wave_at(tt, 0.5f * tau);
                    const std::vector<float> l2 = boom::luma(boom::render(pass, tt, o2, v0));
                    double b2[64] = {};
                    for (std::size_t i = 0; i < l2.size(); ++i) {
                        const double dx = (double(i % tt.w) + 0.5 - cx) / double(bm::wave_stretch), dy = double(i / tt.w) + 0.5 - cy;
                        const unsigned bin = std::min(63u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 64.0));
                        b2[bin] = std::max(b2[bin], double(l2[i]));
                    }
                    double m1 = 0, m2 = 0, sxy = 0, sxx = 0, syy = 0;
                    for (unsigned k = 0; k < 64; ++k) {
                        m1 += bins[k] / 64.0;
                        m2 += b2[k] / 64.0;
                    }
                    for (unsigned k = 0; k < 64; ++k) {
                        sxy += (bins[k] - m1) * (b2[k] - m2);
                        sxx += (bins[k] - m1) * (bins[k] - m1);
                        syy += (b2[k] - m2) * (b2[k] - m2);
                    }
                    const double corr = sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 1.0;
                    std::printf("EXPLOSION case=wave_seeds break_correlation=%.3f\n", corr);
                    report("wave_break_follows_seed", corr < 0.5, "");
                }
                // wave_no_strip_seam (review 2026-11-07): at the eight strip boundaries (k + 1/2) pi / 4 and across the
                // angle's wrap (pi), the band's mean luminance 2 px either side (its radial span r - band .. r) differs by
                // < 10 % of the frame's peak
                {
                    double worst = 0;
                    const double band_px = double(bm::wave_band_x) * double(R_f) * px_x / (5000.0 * m);
                    for (unsigned k = 0; k < 9; ++k) {
                        const double phi = k < 8 ? (double(k) + 0.5) * 0.78539816 : 3.14159265;
                        double side[2] = {};
                        for (unsigned sd = 0; sd < 2; ++sd) {
                            const double a2 = phi + (sd ? 2.0 : -2.0) / r_px;
                            double sum = 0, n = 0;
                            for (double rr = r_px - band_px; rr <= r_px; rr += 1.0) {
                                const double x = cx + double(bm::wave_stretch) * rr * std::cos(a2), y = cy + rr * std::sin(a2);
                                if (x < 0 || y < 0 || x >= tt.w || y >= tt.h) continue;
                                sum += l[std::size_t(y) * tt.w + std::size_t(x)];
                                ++n;
                            }
                            side[sd] = n > 0 ? sum / n : 0;
                        }
                        worst = std::max(worst, std::fabs(side[0] - side[1]) / double(pk));
                    }
                    std::printf("EXPLOSION case=wave_seam worst_over_peak=%.4f quads=%u\n", worst, o.n.wave_quads);
                    report("wave_no_strip_seam", worst < 0.1, "");
                }
                // wave_no_hard_edge (v7, 2026-11-07: the lab's ring ended in a vertical cut; v6's sheet ran at full gain
                // ahead of the front to the window and the strip's edge): (a) along the band at 36 angles (10 degrees
                // apart, the eight strips' boundaries among the 2-degree pairs' spans), the band's mean luminance (its
                // radial span r - band .. r) sampled every 0.25 degrees over 2 degrees: where it drops past 50 % between the
                // pair's ends (the brighter >= 10 % of the frame's peak), no single 0.25-degree step carries more than 35
                // % of the brighter end (a noise hole's edge is soft: the erosion's +-0.15 of the noise); (b) outward from
                // each lit ray's peak to r + (window_out + tongue + 2) w_e along 36 rays, no 1-px step rises, and none falls
                // by more than 30 % of that ray's peak (nothing but the rim's fall-off ahead of the front; at 5 km a w_e is
                // 11.5 px, v6's sheet fell by its whole value in one)
                {
                    const double band_px = double(bm::wave_band_x) * double(R_f) * px_x / (5000.0 * m);
                    const auto band_mean = [&](double a2) {
                        double sum = 0, n = 0;
                        for (double rr = r_px - band_px; rr <= r_px; rr += 1.0) {
                            const double x = cx + double(bm::wave_stretch) * rr * std::cos(a2), y = cy + rr * std::sin(a2);
                            if (x < 0 || y < 0 || x >= tt.w || y >= tt.h) continue;
                            sum += l[std::size_t(y) * tt.w + std::size_t(x)];
                            ++n;
                        }
                        return n > 0 ? sum / n : 0.0;
                    };
                    unsigned drops = 0, hard = 0;
                    double worst_step = 0;
                    for (unsigned k = 0; k < 36; ++k) {
                        double v[9];
                        for (unsigned j = 0; j < 9; ++j)
                            v[j] = band_mean((double(k) * 10.0 - 180.0 + 0.25 * double(j)) * 0.017453293);
                        const double hi = std::max(v[0], v[8]), lo = std::min(v[0], v[8]);
                        if (!(hi >= 0.1 * double(pk)) || lo >= 0.5 * hi) continue;
                        ++drops;
                        double step = 0;
                        for (unsigned j = 0; j < 8; ++j) step = std::max(step, std::fabs(v[j + 1] - v[j]) / hi);
                        worst_step = std::max(worst_step, step);
                        hard += step > 0.35;
                    }
                    unsigned rays = 0, rises = 0;
                    double worst_fall = 0, worst_at = 0, peak_at = 0;
                    const double reach = r_px + (double(bm::wave_window_out) + double(bm::wave_tongue * bm::wave_trail_span) + 2.0) * w_px;
                    for (unsigned k = 0; k < 36; ++k) {
                        const double a2 = (double(k) + 0.5) * 0.17453293 - 3.14159265;
                        std::vector<float> prof;
                        for (double rr = r_px - band_px; rr <= reach; rr += 1.0) {
                            const double x = cx + double(bm::wave_stretch) * rr * std::cos(a2), y = cy + rr * std::sin(a2);
                            if (x < 0 || y < 0 || x >= tt.w || y >= tt.h) break;
                            prof.push_back(l[std::size_t(y) * tt.w + std::size_t(x)]);
                        }
                        std::size_t at = 0;
                        for (std::size_t q = 0; q < prof.size(); ++q)
                            if (prof[q] > prof[at]) at = q;
                        if (prof.empty() || !(prof[at] >= 0.1f * pk)) continue;
                        ++rays;
                        for (std::size_t q = at; q + 1 < prof.size(); ++q) {
                            const double dv = double(prof[q + 1]) - double(prof[q]);
                            rises += dv > 0.02 * double(prof[at]);
                            if (-dv / double(prof[at]) > worst_fall) {
                                worst_fall = -dv / double(prof[at]);
                                worst_at = (double(q) - band_px) / w_px; // w_e from r(t)
                                peak_at = (double(at) - band_px) / w_px;
                            }
                        }
                    }
                    std::printf("EXPLOSION case=wave_hard_edge band_drops=%u hard_drops=%u worst_step=%.3f rays=%u "
                                "outer_rises=%u worst_outer_fall=%.3f fall_at_we=%.2f peak_at_we=%.2f\n",
                                drops, hard, worst_step, rays, rises, worst_fall, worst_at, peak_at);
                    report("wave_no_hard_edge", hard == 0 && rays >= 18 && rises == 0 && worst_fall <= 0.3, "");
                }
                // wave_fire_hue (v7, the user: beige / olive is not fire): over the lit pixels (10 % of the frame's peak) at
                // 0.5 tau, in the dump's display encoding (sRGB of the linear value clamped to [0, 1]), the mean chroma (max
                // - min) / max >= 0.5 and >= 80 % of them with a hue angle in 10 .. 40 degrees (orange-red, not yellow)
                {
                    const auto enc = [](double v) {
                        v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
                        return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
                    };
                    double csum = 0, n = 0, in_band = 0, hsum = 0;
                    for (std::size_t i = 0; i < l.size(); ++i) {
                        if (!(l[i] > 0.1f * pk)) continue;
                        const double R = enc(rgb[3 * i]), G = enc(rgb[3 * i + 1]), B = enc(rgb[3 * i + 2]);
                        const double mx = std::max(R, std::max(G, B)), mn = std::min(R, std::min(G, B));
                        csum += mx > 0 ? (mx - mn) / mx : 0.0;
                        double h = 0;
                        if (mx > mn) {
                            h = mx == R ? 60.0 * std::fmod((G - B) / (mx - mn) + 6.0, 6.0)
                                : mx == G ? 60.0 * ((B - R) / (mx - mn) + 2.0) : 60.0 * ((R - G) / (mx - mn) + 4.0);
                        }
                        in_band += h >= 10.0 && h <= 40.0;
                        hsum += h;
                        ++n;
                    }
                    const double chroma = n > 0 ? csum / n : 0, share = n > 0 ? in_band / n : 0;
                    std::printf("EXPLOSION case=wave_fire_hue lit_px=%.0f mean_chroma=%.3f hue_10_40_share=%.3f mean_hue=%.1f\n",
                                n, chroma, share, n > 0 ? hsum / n : 0.0);
                    report("wave_fire_hue", n > 0 && chroma >= 0.5 && share >= 0.8, "");
                }
                // 2026-11-07 v3 (the coordinator: beige / olive and soot read as no fire): over the lit pixels (10 % of the
                // frame's peak) the mean of each pixel's chroma, and the share whose colour (over its largest channel) lies
                // within 0.1 of the body's heat ramp's (over its largest) at some T: the 0.5 tau frame and both ages
                const auto ramp_stats = [](const std::vector<float>& c, const std::vector<float>& l, float p, double* ch,
                                           double* on) {
                    static float ramp[101][3];
                    static bool made = false;
                    if (!made) {
                        for (unsigned k = 0; k <= 100; ++k) {
                            wc::heat_colour(0.2f + 0.8f * float(k) / 100.f, ramp[k]);
                            const float m = std::max(ramp[k][0], std::max(ramp[k][1], ramp[k][2]));
                            for (unsigned q = 0; q < 3; ++q) ramp[k][q] /= m;
                        }
                        made = true;
                    }
                    double sc = 0, so = 0, n = 0;
                    for (std::size_t i = 0; i < l.size(); ++i) {
                        if (!(l[i] > 0.1f * p)) continue;
                        const float* px = &c[3 * i];
                        const float mx = std::max(px[0], std::max(px[1], px[2])), mn = std::min(px[0], std::min(px[1], px[2]));
                        sc += mx > 0.f ? (mx - mn) / mx : 0.0;
                        float best = 1e30f;
                        for (unsigned k = 0; k <= 100; ++k) {
                            float d = 0.f;
                            for (unsigned q = 0; q < 3; ++q) d += (px[q] / mx - ramp[k][q]) * (px[q] / mx - ramp[k][q]);
                            best = std::min(best, d);
                        }
                        so += best <= 0.01f;
                        ++n;
                    }
                    *ch = n > 0 ? sc / n : 0.0;
                    *on = n > 0 ? so / n : 0.0;
                };
                double pch[3] = {}, pon[3] = {};
                ramp_stats(rgb, l, pk, &pch[0], &pon[0]);
                double hue[2] = {}, chroma[2] = {};
                const float ages[2] = {0.2f * Lw, 0.9f * Lw};
                for (unsigned k = 0; k < 2; ++k) {
                    boom::clear();
                    capital(5000.0, false);
                    const Out ok = wave_at(tt, ages[k]);
                    const std::vector<float> ck = boom::render(pass, tt, ok, v0);
                    const std::vector<float> lk = boom::luma(ck);
                    const float pkk = boom::peak(lk);
                    double s[3] = {};
                    for (std::size_t i = 0; i < lk.size(); ++i)
                        if (lk[i] > 0.1f * pkk)
                            for (unsigned q = 0; q < 3; ++q) s[q] += ck[3 * i + q];
                    hue[k] = classes::burst::nearest_stop(s);
                    ramp_stats(ck, lk, pkk, &pch[k + 1], &pon[k + 1]);
                    const double smx = std::max(s[0], std::max(s[1], s[2])), smn = std::min(s[0], std::min(s[1], s[2]));
                    chroma[k] = smx > 0 ? (smx - smn) / smx : 0;
                }
                const double rg = band[1] > 0 ? band[0] / band[1] : 0, eb = band_v > 0 ? edge_v / band_v : 0;
                std::printf("EXPLOSION case=wave_colour edge_over_band=%.2f band_r_over_g=%.2f band_px=%u hue_02=%.2f "
                            "hue_09=%.2f chroma_02=%.3f chroma_09=%.3f\n",
                            eb, rg, band_n, hue[0], hue[1], chroma[0], chroma[1]);
                std::printf("EXPLOSION case=wave_ramp mean_chroma=%.3f,%.3f,%.3f on_ramp=%.3f,%.3f,%.3f\n", pch[0], pch[1],
                            pch[2], pon[0], pon[1], pon[2]);
                bool ramp_ok = true;
                for (unsigned k = 0; k < 3; ++k) ramp_ok = ramp_ok && pch[k] >= 0.35 && pon[k] >= 0.9;
                report("wave_fire_not_grey", eb >= 1.3 && rg >= 1.3 && hue[0] >= 0.5 && hue[1] <= 0.35 && chroma[0] >= 0.2 &&
                                                 chroma[1] >= 0.2 && ramp_ok, "");
            }
            // (d) wave_refraction_strips_present_and_capped: the newest wave's request at 1.5 km face-on, its eight
            // strips at the body's birth (r_0 592 px: 0.10 screens), none past the cap (drawn without a band) later; at
            // r 1,000 px the strips' sum against the single box (wave_shimmer_strips_timing: <= 0.6, the GPU half in
            // the engine shimmer fixture)
            {
                boom::clear();
                capital(1500.0, false);
                const Out early = wave_at(tt, 0.f);
                boom::clear();
                capital(1500.0, false);
                const Out late = wave_at(tt, 200.f);
                boom::clear();
                capital(1500.0, false);
                std::printf("EXPLOSION case=wave_strips early_strips=%u early_screens=%.3f early_request=%u late_waves=%u "
                            "late_strips=%u late_capped=%u late_screens=%.3f\n",
                            early.n.wave_strips, double(early.n.wave_strip_screens), early.wave_shimmer, late.waves,
                            late.n.wave_strips, late.n.wave_strips_capped, double(late.n.wave_strip_screens));
                x3m::engine_shimmer::Projection pj{};
                pj.m00 = v0.cam.m00;
                pj.m11 = v0.cam.m11;
                pj.width = float(tt.w);
                pj.height = float(tt.h);
                x3m::engine_shimmer::WaveRequest w{};
                const float z = 5000.f * m;
                w.centre[2] = z;
                w.normal[2] = 1.f;
                w.axis[0] = 1.f;
                w.r = float(1000.0 / px_x) * z;
                w.band = 0.2f * R_f;
                w.stretch = 1.f;
                w.px = 6.f;
                x3m::engine_shimmer::Rect strips[8];
                float ss = 0.f;
                const unsigned made = x3m::engine_shimmer::wave_strips(w, pj, v0.near_z, strips, &ss);
                const double band_px = double(w.band) * px_x / double(z);
                const double box = std::pow(2.0 * (1000.0 + band_px), 2.0) / (double(tt.w) * tt.h);
                std::printf("EXPLOSION case=wave_strip_area r_px=1000 strips=%u strip_screens=%.3f box_screens=%.3f ratio=%.3f\n",
                            made, double(ss), box, box > 0 ? double(ss) / box : 0.0);
                report("wave_refraction_strips_present_and_capped",
                       early.n.wave_strips == bm::wave_strips && early.wave_shimmer == 1 && late.waves == 1 &&
                           late.n.wave_strips == 0 && late.n.wave_strips_capped == 1 && late.wave_shimmer == 0, "");
                report("wave_shimmer_strips_area", made > 0 && ss > 0.f && double(ss) <= 0.6 * box, "");
            }
            // (e) wave_lights_swept_hulls: a hull plane through the wave's centre (5 km face-on): the light there (the
            // frame over the plane less half the sky frame: the plane halves the slab's chord) at the pixel 2 R_f above
            // the centre, while the front crosses it and at 0.5 tau
            {
                const float target = 2.f * R_f;
                float t_cross = 0.f;
                for (float t = 0.f; t < tau; t += 1.f)
                    if (law_r(t) >= target) {
                        t_cross = t;
                        break;
                    }
                const std::size_t px = std::size_t(cy - double(target) * px_x / (5000.0 * m)) * tt.w + std::size_t(cx);
                double light[2] = {}, sky_v[2] = {};
                const float ts[2] = {t_cross, 0.5f * tau};
                for (unsigned k = 0; k < 2; ++k) {
                    boom::clear();
                    capital(5000.0, false);
                    const Out o = wave_at(tt, ts[k]);
                    draw_lane(tt, 0.f, -1.f);
                    const std::vector<float> sky = boom::luma(boom::render(pass, tt, o, v0));
                    draw_lane(tt, 5000.f * m, 1.f);
                    const std::vector<float> lane = boom::luma(boom::render(pass, tt, o, v0));
                    draw_lane(tt, 0.f, -1.f);
                    sky_v[k] = sky[px];
                    light[k] = double(lane[px]) - 0.5 * double(sky[px]);
                }
                std::printf("EXPLOSION case=wave_swept_hull t_cross_ms=%.0f light_cross=%.3f light_late=%.3f sky_cross=%.3f "
                            "sky_late=%.3f\n",
                            double(t_cross), light[0], light[1], sky_v[0], sky_v[1]);
                report("wave_lights_swept_hulls", light[0] >= 0.1 && std::fabs(light[1]) <= 0.02, "");
            }
            // (f) wave_eye_in_plane_no_whiteout (2026-11-07 v3): the eye in the wave's plane (the capital from above at 1.5
            // km, every element, over sky), every 50 ms P .. P + L_w: the frame's mean of 1 - e^(-Y) (Y the linear Rec. 709
            // luminance) <= 0.35 and no frame with more than 40 % of its pixels above 0.9
            {
                boom::clear();
                capital(1500.0, true);
                double worst_mean = 0, worst_share = 0, worst_at = 0;
                for (unsigned a = 0; a <= unsigned(Pc + Lw); a += 50) {
                    const Out o = boom::build(boom::view(tt, 1000 + a, 16));
                    if (float(a) < Pc) continue;
                    const std::vector<float> rgb = boom::render(pass, tt, o, v0);
                    double sum = 0, hot = 0;
                    const std::size_t npx = rgb.size() / 3;
                    for (std::size_t i = 0; i < npx; ++i) {
                        const double Y = 0.2126 * rgb[3 * i] + 0.7152 * rgb[3 * i + 1] + 0.0722 * rgb[3 * i + 2];
                        const double tm = 1.0 - std::exp(-Y);
                        sum += tm;
                        hot += tm > 0.9;
                    }
                    const double mean = sum / double(npx), share = hot / double(npx);
                    if (mean > worst_mean) {
                        worst_mean = mean;
                        worst_at = double(a) - double(Pc);
                    }
                    worst_share = std::max(worst_share, share);
                }
                std::printf("EXPLOSION case=wave_eye_in_plane worst_mean=%.3f at_ms=%.0f worst_hot_share=%.3f\n", worst_mean,
                            worst_at, worst_share);
                report("wave_eye_in_plane_no_whiteout", worst_mean <= 0.35 && worst_share <= 0.4, "");
            }
            tt.release();
        } else
            report("wave_radius_law", false, "target");
        // ---- 13.2: the composed body. Silhouette (36-bin 10 %-of-peak outline about the box's centre) at 0.3 / 0.6 L,
        // connectedness and radiance at 0.3 / 0.1 L against the single body, 8 seeds per tier, 1080p, core off for the
        // geometry, on for the radiance. Only with boom::compose_enabled (2026-11-07 v4, the kill: off for the flight; the
        // program stays built), else one row with the reason.
        if (!bm::compose_enabled)
            std::printf("EXPLOSION case=compose_skipped reason=compose_enabled_false\n");
        else {
            struct Tier {
                unsigned tier;
                double z, r;
                const char* name;
            } tiers[3] = {{bm::fighter, 600.0, 14.0, "fighter_600m"}, {bm::capital, 1500.0, 1288.0, "capital_1500m"},
                          {bm::station, 3000.0, 1800.0, "station_3km"}};
            auto one = [&](const Tier& tr, std::uint32_t seq) {
                wc::Event* e = boom::add(tr.tier, 1000, 0, 0, tr.z, tr.r, seq);
                if (tr.tier == bm::capital || tr.tier == bm::station) {
                    e->table = tr.tier == bm::station ? 6 : 7;
                    boom::broadside(e);
                }
                return e;
            };
            auto profile = [&](const std::vector<float>& l, double x, double y, double out[36]) {
                const float p = boom::peak(l);
                for (unsigned b = 0; b < 36; ++b) out[b] = 0;
                for (std::size_t i = 0; i < l.size(); ++i) {
                    if (!(l[i] > 0.1f * p)) continue;
                    const double dx = double(i % g.w) + 0.5 - x, dy = double(i / g.w) + 0.5 - y, r = std::hypot(dx, dy);
                    const unsigned bin = std::min(35u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 36.0));
                    out[bin] = std::max(out[bin], r);
                }
            };
            auto cv_of = [](const double p[36]) {
                double mean = 0, var = 0;
                for (unsigned b = 0; b < 36; ++b) mean += p[b] / 36.0;
                for (unsigned b = 0; b < 36; ++b) var += (p[b] - mean) * (p[b] - mean) / 36.0;
                return mean > 0 ? std::sqrt(var) / mean : 0.0;
            };
            struct Blob {
                std::size_t n;
                unsigned x0, y0, x1, y1;
            };
            auto blobs = [](unsigned w, unsigned h, const auto& in) {
                std::vector<Blob> out;
                std::vector<unsigned char> seen(std::size_t(w) * h, 0);
                std::vector<std::size_t> stack;
                for (std::size_t i = 0; i < seen.size(); ++i) {
                    if (seen[i] || !in(i)) continue;
                    Blob b{0, unsigned(i % w), unsigned(i / w), unsigned(i % w), unsigned(i / w)};
                    stack.push_back(i);
                    seen[i] = 1;
                    while (!stack.empty()) {
                        const std::size_t q = stack.back();
                        stack.pop_back();
                        ++b.n;
                        const unsigned x = unsigned(q % w), y = unsigned(q / w);
                        b.x0 = std::min(b.x0, x);
                        b.x1 = std::max(b.x1, x);
                        b.y0 = std::min(b.y0, y);
                        b.y1 = std::max(b.y1, y);
                        const std::size_t nb[4] = {x > 0 ? q - 1 : q, x + 1 < w ? q + 1 : q, y > 0 ? q - w : q, y + 1 < h ? q + w : q};
                        for (std::size_t r : nb)
                            if (!seen[r] && in(r)) {
                                seen[r] = 1;
                                stack.push_back(r);
                            }
                    }
                    out.push_back(b);
                }
                return out;
            };
            bool cv_ok = true, change_ok = true, grapes_ok = true, sun_ok = true, clumps_ok = true, white_ok = true,
                 structure_ok = false;
            for (const Tier& tr : tiers) {
                const float L = bm::tier_life_ms[tr.tier], P = bm::tier_preroll_ms[tr.tier];
                std::vector<double> cv3, cv6, single3, corr, rms, blob, peak_ratio, mean_ratio, clumps, white, lit_in;
                for (unsigned j = 0; j < 8; ++j) {
                    const std::uint32_t seq = 0x6d000u + 977u * (j + 1);
                    double prof[2][36];
                    for (unsigned k = 0; k < 2; ++k) {
                        boom::core_on = false;
                        boom::clear();
                        one(tr, seq);
                        latch(g);
                        const Out o = at(g, P + (k ? 0.6f : 0.3f) * L, boom::p_body);
                        float c[3];
                        boom::body_centre(o, c);
                        const classes::burst::Disc d = boom::disc(g, boom::view(g, 0, 16), c, 1.0);
                        const std::vector<float> l = boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16)));
                        profile(l, d.x, d.y, prof[k]);
                        (k ? cv6 : cv3).push_back(cv_of(prof[k]));
                        // the 4-connected blobs of the lit pixels (10 % of the frame's peak): at 0.3 L one holds them,
                        // at 0.6 L the clumps survive (>= 2 blobs of >= 1 % of the lit pixels each)
                        const float p = boom::peak(l);
                        const std::vector<Blob> bl = blobs(g.w, g.h, [&](std::size_t i) { return l[i] > 0.1f * p; });
                        std::size_t total = 0, best = 0;
                        for (const Blob& b : bl) {
                            total += b.n;
                            best = std::max(best, b.n);
                        }
                        if (!k) blob.push_back(total ? double(best) / double(total) : 0.0);
                        else {
                            unsigned count = 0;
                            for (const Blob& b : bl) count += b.n * 100 >= total;
                            clumps.push_back(count);
                            // the holes: the lit share of the pixels inside the 36-bin outline
                            double in = 0, on = 0;
                            for (std::size_t i = 0; i < l.size(); ++i) {
                                const double dx = double(i % g.w) + 0.5 - d.x, dy = double(i / g.w) + 0.5 - d.y;
                                const unsigned bin = std::min(35u, unsigned((std::atan2(dy, dx) + 3.14159265) / 6.2831853 * 36.0));
                                if (std::hypot(dx, dy) <= prof[k][bin]) {
                                    ++in;
                                    on += l[i] > 0.1f * p;
                                }
                            }
                            lit_in.push_back(in > 0 ? on / in : 1.0);
                        }
                    }
                    double mean[2] = {}, sxy = 0, sxx = 0, syy = 0, d2 = 0;
                    for (unsigned k = 0; k < 2; ++k)
                        for (unsigned b = 0; b < 36; ++b) mean[k] += prof[k][b] / 36.0;
                    for (unsigned b = 0; b < 36; ++b) {
                        const double a = prof[0][b] - mean[0], c = prof[1][b] - mean[1];
                        sxy += a * c;
                        sxx += a * a;
                        syy += c * c;
                        const double na = mean[0] > 0 ? prof[0][b] / mean[0] : 0, nc = mean[1] > 0 ? prof[1][b] / mean[1] : 0;
                        d2 += (na - nc) * (na - nc) / 36.0;
                    }
                    corr.push_back(sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 1.0);
                    rms.push_back(std::sqrt(d2));
                    // the single body's silhouette (the lobed ball) for the record
                    boom::compose_on = false;
                    boom::clear();
                    one(tr, seq);
                    latch(g);
                    {
                        const Out o = at(g, P + 0.3f * L, boom::p_body);
                        float c[3];
                        boom::body_centre(o, c);
                        const classes::burst::Disc d = boom::disc(g, boom::view(g, 0, 16), c, 1.0);
                        double ps[36];
                        profile(boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16))), d.x, d.y, ps);
                        single3.push_back(cv_of(ps));
                    }
                    // the radiance at 0.1 L (core on): the composed against the single body of the same seed
                    boom::core_on = true;
                    double pk[2], mr[2];
                    for (unsigned k = 0; k < 2; ++k) {
                        boom::compose_on = k == 0;
                        boom::clear();
                        one(tr, seq);
                        latch(g);
                        const Out o = at(g, P + 0.1f * L, boom::p_body);
                        const std::vector<float> l = boom::luma(boom::render(pass, g, o, boom::view(g, 0, 16)));
                        pk[k] = boom::peak(l);
                        double s = 0, n = 0;
                        for (float v : l)
                            if (v > 0.1f * float(pk[k])) {
                                s += v;
                                ++n;
                            }
                        mr[k] = n > 0 ? s / n : 0;
                    }
                    boom::compose_on = true;
                    peak_ratio.push_back(pk[1] > 0 ? pk[0] / pk[1] : 0);
                    mean_ratio.push_back(mr[1] > 0 ? mr[0] / mr[1] : 0);
                    // no white region past 0.3 L (core on): the widest 4-connected run of white pixels (luma >= 1.2, the
                    // smallest channel >= 0.75 of the largest) at 0.3 / 0.6 L, over R_f on screen
                    double wmax = 0;
                    for (float frac : {0.3f, 0.6f}) {
                        boom::clear();
                        wc::Event* e = one(tr, seq);
                        latch(g);
                        const Out o = at(g, P + frac * L, boom::p_body);
                        const std::vector<float> rgb = boom::render(pass, g, o, boom::view(g, 0, 16));
                        const std::vector<float> l = boom::luma(rgb);
                        const double rf_px = double(ws::boom_r_f_m(*e)) * double(m) * 0.5 * g.w * double(boom::view(g, 0, 16).cam.m00) / (tr.z * double(m));
                        const std::vector<Blob> bl = blobs(g.w, g.h, [&](std::size_t i) {
                            const float* c = &rgb[3 * i];
                            const float mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
                            return l[i] >= 1.2f && mx > 0.f && mn >= 0.75f * mx;
                        });
                        for (const Blob& b : bl)
                            wmax = std::max(wmax, double(std::max(b.x1 - b.x0, b.y1 - b.y0) + 1) / rf_px);
                    }
                    boom::core_on = false;
                    white.push_back(wmax);
                }
                const double c3 = boom::median(cv3), c6 = boom::median(cv6), s3 = boom::median(single3);
                const double cr = boom::median(corr), rm = boom::median(rms), bl = boom::median(blob);
                const double pr = boom::median(peak_ratio), mrr = boom::median(mean_ratio);
                const double cl6 = boom::median(clumps), wh = boom::median(white), li6 = boom::median(lit_in);
                if (tr.tier == bm::capital) structure_ok = cl6 >= 3.0 && li6 <= 0.75;
                std::printf("EXPLOSION case=compose_silhouette tier=%s cv_03=%.3f cv_06=%.3f single_cv_03=%.3f corr=%.3f "
                            "rms=%.3f blob_share=%.3f peak_over_single=%.3f mean_over_single=%.3f clumps_06=%.1f "
                            "white_rf=%.3f lit_in_06=%.3f\n",
                            tr.name, c3, c6, s3, cr, rm, bl, pr, mrr, cl6, wh, li6);
                clumps_ok = clumps_ok && cl6 >= 2.0;
                white_ok = white_ok && wh <= 0.3;
                cv_ok = cv_ok && c3 >= 0.18 && c6 >= 0.18;
                change_ok = change_ok && (cr <= 0.7 || rm >= 0.15);
                grapes_ok = grapes_ok && bl >= 0.8;
                sun_ok = sun_ok && pr <= 1.4 && mrr >= 0.7 && mrr <= 1.3;
            }
            boom::core_on = false;
            report("compose_silhouette_cv", cv_ok, "");
            report("compose_silhouette_changes", change_ok, "");
            report("compose_not_grapes", grapes_ok, "");
            report("compose_under_one_sun", sun_ok, "");
            report("compose_clumps_survive", clumps_ok, "");
            report("compose_no_white_region", white_ok, "");
            // 2026-11-07 v3 (the fourth octave): the capital broadside at 0.6 L keeps >= 3 clumps and holes (the lit share
            // inside its outline <= 0.75)
            report("compose_capital_structure", structure_ok, "");
            // births and jets (the stage's table, mirrored by the program): sphere 0 at the body's birth, every one born
            // by compose_spread (jets by birth0 + span), the composed body gone at L; jets on the capital and station only,
            // their long over short axis
            bool births_ok = true, jets_ok = true;
            for (const Tier& tr : tiers) {
                for (unsigned j = 0; j < 8; ++j) {
                    boom::clear();
                    wc::Event* e = one(tr, 0x6d800u + 977u * (j + 1));
                    const float L = bm::tier_life_ms[tr.tier], P = bm::tier_preroll_ms[tr.tier];
                    latch(g);
                    const Out o = at(g, P + 0.2f * L, boom::p_body);
                    const std::uint32_t hc = ws::element_hash(e->sequence, bm::compose_element);
                    const unsigned full = bm::compose_n_lo[tr.tier] + (hc >> 4) % (bm::compose_n_hi[tr.tier] - bm::compose_n_lo[tr.tier] + 1u);
                    const unsigned jets = bm::compose_jets_lo[tr.tier] +
                                          (hc >> 12) % (bm::compose_jets_hi[tr.tier] - bm::compose_jets_lo[tr.tier] + 1u);
                    const unsigned n = full;
                    float ax[3][3];
                    ws::boom_rows(*e, ax);
                    ws::ComposeSphere sp[bm::compose_max];
                    const float zero[3] = {0.f, 0.f, 0.f};
                    const float seed = boom::bodies[0].look[3];
                    const unsigned at0 = ws::boom_compose_table(zero, 1.f, 0.f, seed, n, jets, full, true, 0.5f, 0.15f, ax, sp);
                    const unsigned atx = ws::boom_compose_table(zero, 1.f, bm::compose_spread + 1e-4f, seed, n, jets, full, true,
                                                                0.5f, 0.15f, ax, sp);
                    const Out gone = at(g, P + L + 20.f, boom::p_body);
                    births_ok = births_ok && o.n.composed == 1 && at0 >= 1 && atx == n + jets && gone.n.composed == 0;
                    const bool big = tr.tier == bm::capital || tr.tier == bm::station;
                    jets_ok = jets_ok && (big ? jets >= 1 && jets <= 2 : jets == 0);
                    if (!j)
                        std::printf("EXPLOSION case=compose_table tier=%s n=%u n_full=%u jets=%u born_at_0=%u born_by_spread=%u "
                                    "gone_at_L=%u jet_ratio=%.2f\n",
                                    tr.name, n, full, jets, at0, atx, gone.n.composed,
                                    double(bm::compose_jet_long / bm::compose_jet_short));
                }
            }
            report("compose_births_spread", births_ok, "");
            report("compose_jets_capital", jets_ok && std::fabs(bm::compose_jet_long / bm::compose_jet_short - 2.f) <= 0.3f, "");
            // the eye inside the cluster: the own death in the chase view (24 m) and the close capital (720 m, t_b + 0.3 s):
            // the shared-noise mode and N at most compose_eye_n
            {
                boom::clear();
                fighter(24.0);
                latch(g);
                const Out own = at(g, 0.25f * Lf, boom::p_body);
                const float own_n = boom::compose[0].head[0], own_s = boom::compose[0].head[1];
                boom::clear();
                capital(720.0, false);
                latch(g);
                const Out close = at(g, Pc + 0.25f * Lc, boom::p_body);
                const float cl_n = boom::compose[0].head[0], cl_s = boom::compose[0].head[1];
                std::printf("EXPLOSION case=compose_eye own_eye=%u own_drawn=%.0f own_shared=%.0f close_eye=%u close_drawn=%.0f "
                            "close_shared=%.0f\n",
                            own.n.compose_eye, double(own_n), double(own_s), close.n.compose_eye, double(cl_n), double(cl_s));
                report("compose_eye_inside_shared", own.n.compose_eye == 1 && own_s == 1.f && own_n >= 2.f && own_n <= 4.f &&
                                                        close.n.compose_eye == 1 && cl_s == 1.f && cl_n >= 2.f && cl_n <= 4.f, "");
            }
            // the LOD floor: a capital's body over a budget of nothing after the octave step's hold: the shared mode, the
            // spheres down to compose_n_min, then the jets, never under it
            {
                boom::clear();
                capital(1500.0, false);
                latch(g);
                const float keep_cap = boom::cap_screens;
                boom::cap_screens = 0.f;
                Out o;
                for (unsigned a = 0; a < 20; ++a)
                    o = boom::build(boom::view(g, 1000 + unsigned(Pc + 0.3f * Lc) + 16 * a, 16), boom::p_body);
                boom::cap_screens = keep_cap;
                const float drawn = boom::compose[0].head[0], shared = boom::compose[0].head[1];
                unsigned jets = 0;
                for (unsigned k = 0; k < unsigned(drawn); ++k) jets += boom::compose[0].sphere[k][1][3] > 0.f;
                std::printf("EXPLOSION case=compose_lod drawn=%.0f jets=%u shared=%.0f dropped=%u octaves=%u\n", double(drawn),
                            jets, double(shared), o.n.compose_dropped, o.n.octaves);
                report("compose_lod_floor", o.n.octaves == 2 && drawn == float(bm::compose_n_min) && jets == 0 && shared == 1.f &&
                                                o.n.compose_dropped > 0, "");
            }
            // the box holds the spheres: the body drawn in its box and in one 1.6 x larger, 8 seeds of each tier, at 0.6 L
            {
                double worst = 0;
                for (const Tier& tr : tiers)
                    for (unsigned j = 0; j < 8; ++j) {
                        // the normal box's pixels (its corners through the projection), then the body in a box 1.6 x
                        // larger: its lit pixels outside the first
                        double box[4] = {0, 0, 0, 0};
                        std::vector<float> big;
                        for (unsigned k = 0; k < 2; ++k) {
                            boom::box_scale = k ? 1.6f : 1.f;
                            boom::clear();
                            one(tr, 0x6e800u + 977u * (j + 1));
                            latch(g);
                            const float L = bm::tier_life_ms[tr.tier], P = bm::tier_preroll_ms[tr.tier];
                            const Out o = at(g, P + 0.6f * L, boom::p_body);
                            const ws::HullView v = boom::view(g, 0, 16);
                            if (!k) {
                                const rr::WeaponFireballVertex& a = boom::bodies[0];
                                const rr::WeaponFireballVertex& b = boom::bodies[3];
                                box[0] = 0.5 * g.w * (1.0 + v.cam.m00 * a.corner[0] / a.corner[2]);
                                box[2] = 0.5 * g.w * (1.0 + v.cam.m00 * b.corner[0] / b.corner[2]);
                                box[1] = 0.5 * g.h * (1.0 - v.cam.m11 * b.corner[1] / b.corner[2]);
                                box[3] = 0.5 * g.h * (1.0 - v.cam.m11 * a.corner[1] / a.corner[2]);
                            } else
                                big = boom::luma(boom::render(pass, g, o, v));
                        }
                        boom::box_scale = 1.f;
                        const float pk = boom::peak(big);
                        double outside = 0;
                        for (std::size_t i = 0; i < big.size(); ++i) {
                            const double x = double(i % g.w) + 0.5, y = double(i / g.w) + 0.5;
                            const bool in = x >= std::min(box[0], box[2]) - 1 && x <= std::max(box[0], box[2]) + 1 &&
                                            y >= std::min(box[1], box[3]) - 1 && y <= std::max(box[1], box[3]) + 1;
                            outside += !in && big[i] > 0.01f * pk;
                        }
                        worst = std::max(worst, outside);
                    }
                std::printf("EXPLOSION case=compose_box worst_clipped_px=%.0f\n", worst);
                report("compose_box_holds_spheres", worst == 0, "");
            }
        }
        boom::core_on = true;
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
            unsigned kind; // 0 fighter 600 m, 1 missile 300 m, 2 nested three, 3 eye inside, 4 fighter over a hull,
                           // 5 fighter 300 m (section 14: the vanilla frames' size)
        } cases[] = {{"boom_fighter_600m", 0}, {"boom_missile_300m", 1}, {"boom_nested_3_fighters", 2},
                     {"boom_eye_inside_fighter", 3}, {"boom_fighter_600m_hull", 4}, {"boom_fighter_300m", 5}};
        unsigned written = 0, failed = 0;
        char path[1024];
        // The big tiers over the scene model (the dying hull at the carrier's alpha, its depth in the lane), at the note's
        // ages from the death (the pre-roll's and the body's), each frame built in 16 ms steps from the first, the PPM a
        // centred window (cw x ch at 1440 rows, x 0.75 at 1080; 0: the whole frame); no part PFMs.
        struct BigCase {
            const char* name;
            unsigned tier;
            double z, r;
            unsigned cw, ch;
            std::initializer_list<unsigned> ages;
            bool above = false; // section 13.1: the hull seen from above (the wave edge-on)
        };
        // section 13 (2026-11-07): the whole frame for the wave's cases (the ring passes any window), the broadside at P +
        // 40 / 120 / 400 / 1,200 ms among its ages, the bar from above and the far capital; at 1920x1080 the -body and
        // -wave parts as PFM beside each frame
        const BigCase big_cases[] = {
            {"boom_corvette_1500m", bm::corvette, 1500.0, 87.0, 1000, 600, {16, 100, 300, 600, 900, 1800, 3000}},
            {"boom_capital_1500m_broadside", bm::capital, 1500.0, 1288.0, 0, 0,
             {100, 600, 1100, 1450, 1540, 1620, 1900, 2300, 2700, 3900, 5500}},
            {"boom_capital_1500m_above", bm::capital, 1500.0, 1288.0, 0, 0, {1540, 1620, 1900, 2700}, true},
            {"boom_capital_far_5km", bm::capital, 5000.0, 1288.0, 0, 0, {1540, 1620, 1900, 2700}},
            {"boom_capital_close_720m", bm::capital, 720.0, 1288.0, 0, 0, {600, 1450, 1540, 2500, 3900}},
            {"boom_station_3km", bm::station, 3000.0, 1800.0, 0, 0, {200, 1500, 2800, 3060, 3600, 4200, 6600, 9000}}};
        for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
            Target tt;
            if (!make_target(tt, size.first, size.second)) continue;
            for (const BigCase& k : big_cases) {
                if (!dump::listed(list, k.name)) continue;
                for (unsigned age : k.ages) {
                    boom::clear();
                    wc::Event* e = boom::add(k.tier, 1000, 0, 0, k.z, k.r);
                    e->table = k.tier == bm::station ? 6 : 7;
                    k.above ? boom::from_above(e) : boom::broadside(e);
                    const boom::Hull hull = boom::hull_of(*e);
                    Out o;
                    for (unsigned t_ms = 0;; t_ms += 16) {
                        const unsigned now_age = t_ms < age ? t_ms : age;
                        o = boom::build(boom::view(tt, 1000 + now_age, 16));
                        if (now_age == age) break;
                    }
                    const ws::HullView v = boom::view(tt, 1000 + age, 16);
                    const float alpha = boom::hull_alpha(k.tier, float(age));
                    boom::hull_lane(tt, v, &hull, alpha);
                    HRESULT hr = E_FAIL;
                    const std::vector<float> rgb = boom::render_hull(pass, tt, o, v, &hull, alpha, &hr);
                    const double s = size.second == 1440 ? 1.0 : 0.75;
                    std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u.ppm", dir, k.name, size.first, size.second, age);
                    const bool ok = SUCCEEDED(hr) && !rgb.empty() &&
                                    boom::write_ppm(path, rgb, tt.w, tt.h, unsigned(k.cw * s), unsigned(k.ch * s));
                    ok ? ++written : ++failed;
                    if (size.first == 1920 && SUCCEEDED(hr) && !rgb.empty()) { // section 14: the frame as PFM (AgX offline)
                        std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u.pfm", dir, k.name, size.first, size.second, age);
                        boom::write_pfm(path, rgb, tt.w, tt.h) ? ++written : ++failed;
                    }
                    std::printf("EXPLOSION_DUMP case=%s width=%u height=%u age_ms=%u bodies=%u secondaries=%u quads=%u "
                                "glows=%u smokes=%u shocks=%u darks=%u waves=%u wave_strips=%u composed=%u hull_alpha=%.2f "
                                "screens=%.3f written=%u\n",
                                k.name, size.first, size.second, age, o.n.bodies, o.n.secondaries, o.quads, o.glows, o.smokes,
                                o.shocks, o.darks, o.waves, o.n.wave_strips, o.n.composed, double(alpha), double(o.n.screens),
                                unsigned(ok));
                    if (size.first == 1920)
                        for (unsigned part : {unsigned(boom::p_body), unsigned(boom::p_wave)}) {
                            static ws::BoomState saved;
                            saved = boom::state;
                            const Out po = boom::build(v, part);
                            boom::state = saved;
                            HRESULT ph = E_FAIL;
                            const std::vector<float> prgb = boom::render_hull(pass, tt, po, v, &hull, 0.f, &ph);
                            std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u-%s.pfm", dir, k.name, size.first, size.second, age,
                                          part == boom::p_body ? "body" : "wave");
                            (SUCCEEDED(ph) && !prgb.empty() && boom::write_pfm(path, prgb, tt.w, tt.h)) ? ++written : ++failed;
                        }
                }
            }
            draw_lane(tt, 0.f, -1.f);
            for (const Case& k : cases) {
                if (!dump::listed(list, k.name)) continue;
                const float L = k.kind == 1 ? Lm : Lf;
                // section 14: the vanilla comparison's ages (50 / 150 / 400 / 900 / 1,500 ms of the fighter) as frames only,
                // the parts at the note's five
                for (float frac : {0.025f, 0.075f, 0.1f, 0.2f, 0.3f, 0.45f, 0.6f, 0.75f, 0.8f, 1.0f}) {
                    const bool parts_too = frac == 0.1f || frac == 0.3f || frac == 0.6f || frac == 0.8f || frac == 1.0f;
                    boom::clear();
                    if (k.kind == 0 || k.kind == 4) fighter(600.0);
                    if (k.kind == 5) fighter(300.0);
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
                    // section 14: the frame itself as PFM at 1080 rows (the HDR path's view: decode, exposure, AgX offline)
                    if (size.first == 1920 && SUCCEEDED(hr) && !rgb.empty()) {
                        std::snprintf(path, sizeof path, "%s\\%s_%ux%u-%u.pfm", dir, k.name, size.first, size.second, age);
                        boom::write_pfm(path, rgb, tt.w, tt.h) ? ++written : ++failed;
                    }
                    std::printf("EXPLOSION_DUMP case=%s width=%u height=%u age_ms=%u bodies=%u quads=%u glows=%u smokes=%u "
                                "shocks=%u darks=%u screens=%.3f written=%u\n",
                                k.name, size.first, size.second, age, o.bodies, o.quads, o.glows, o.smokes, o.shocks, o.darks,
                                double(o.n.screens), unsigned(ok));
                    if (size.first != 1920 || !parts_too) continue;
                    for (unsigned p = 0; p < boom::part_count; ++p) {
                        if ((1u << p) == boom::p_secondary || (1u << p) == boom::p_wave) continue; // none on these tiers
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
