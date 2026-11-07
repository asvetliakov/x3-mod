#pragma once
#include "weapon_effects_hull.h"

// The death explosion's scene-end step (step 3; docs/architecture/explosion-look.md is the spec, weapon-look.md 3.2
// where it is silent): the ring's stage-owned death records (role_explosion with flag_stage, the explosions modes; the
// hook skipped their native spawn: core::boom::decide said ours) become the frame's
// - body (2.1): the hull burst's fireball impostor unchanged (weapon_fireball_ps.hlsl, look::fireball_*), one box per
//   death at the world point the record holds (the node died with the object), R(t) = R_0 + (R_f - R_0) (1 -
//   e^(-t / (rho L))) + body_grow R_f t / L, T(t) = 1 - body_cool t / (body_hold L) then linear to 0 at L, x = t / L the
//   erosion's age, E_b = body_e scaled by the event frame's cluster (s(N) = 1 / (1 + (N - 1) / cluster_n0), N by the
//   other live bodies' remaining life within cluster_r_x R), biased body_bias R(t) towards the eye; the sphere drawn at
//   body_r_sphere R(t) (the lobes); the third octave weighted in over look::fireball_fine_px .. + fine_band_px;
// - star (2.2): the burst's 40 ms four-arm star (stage::star_quad) at A = star_arm R_f, core star_core R_f, white at
//   look::core_e, the arms heat(star_t), linear, its arm capped at star_cap_h of the rows, born on the body's frame;
// - shock (2.3, the tiers with one: missile now): a shell box (weapon_shock_ps.hlsl) of radius shock_r_x R_f (t /
//   tau)^0.5, width shock_w r, E shock_e (1 - t / tau)^2 x heat(shock_t); not drawn once the eye is inside it;
// - embers and fragments (2.4): per death tier_embers streaks from 4-6 seed cones (axes and members hashed per event:
//   random per death), each at the constant speed v_e = clamp(ember_v_x R_f / s, ember_v_min, ember_v_max) m/s from the
//   centre +- ember_p0 R_f, p(t) = p0 + v t (no drag: the linear branch), drawn from the previous frame's position to the
//   current one (the streak rule), lives ember_life by thirds x (1 +- ember_stagger / 2), E ember_e e^(-t / (ember_tau
//   L)) T, colour heat(T), T = 1 - t / life; one in fragment_every a fragment at fragment_v v_e to fragment_life L: a hot tip
//   (fragment_tip rest lengths, heat(fragment_tip_t) cooling) and a dark multiply box under it (the scorch pass:
//   surface-bound, nothing over sky);
// - glow (2.6, as amended 2026-11-07: the burst's law): the glow primitive at R = min(glow_r_x R(t), glow_r_max R_f),
//   E_g = glow_e E_b T(t) heat(T(t)), the cluster's saturation, while the body draws (surface-bound: the lane's hulls
//   only); and the same light in air about the body (the halo, weapon_halo_ps.hlsl, occlusion class 1) unless the eye is
//   inside the body; the body's white-hot core (boom::core_*: the program's gain g in the core, 0 from core_x L);
// - smoke (2.7): the body's impostor in the scorch pass at smoke_r R_f from smoke_t0 L over smoke_span L.
// Every life is game time from the record's first stage frame (age 0 on its first drawn frame), never shorter than the
// three-frame floor latched then (min_frames x that frame's step). The big tiers (corvette, capital, station: the native
// spawn ran through the clone row R', so the engine's carrier shows the hull through the pre-roll and fades it over
// P .. P + 1 s, note section 1) draw their body, star, shock, embers, glow and smoke from the pre-roll's end P; through
// the pre-roll (capital and station, section 4):
// - the hull light (2.6): the glow primitive at the hull shape's centre, R = hull_light_r R_obj, E = glow_e E_b
//   (hull_light_e0 .. e1 over the pre-roll, then to 0 with the hull copy's fade over P .. P + 1 s) heat(hull_light_t),
//   surface-bound (the carrier's hull is what it lights);
// - the secondaries (2.5): tier_secondaries burst bodies at the geometric cadence (boom_secondary_births), each at a
//   hashed point inside the hull shape, R_s = secondary_scale R_f, life secondary_life L, its own star, glow (the event's
//   cluster saturation over its live secondaries) and secondary_embers embers.
// The hull shape (latched on the record's first frame): the dying owner's box from the bubble cache (the hull hits'
// walk; the record's flag_bubble_cached, unknown 4) in the frame of the node's rows the record carries, else the axis
// capsule along row 2. The big tiers' embers at the body's birth leave from the shape's surface (the ray from its centre
// along the ember's direction), the rest of the tier's count after the secondaries' share.
// The budget (section 7, the family's items of the shared order): past the screens left, (1) every body's third octave
// with the burst's hysteresis (enter past the budget for fireball_octave_hold frames, leave under
// fireball_octave_return), (2) the smoke, largest box first, (3) the shocks, (4) the glows of owners other than the own
// ship, largest first; never shed: the body, the star, the embers and fragments, anything of the own ship. Count caps:
// boom::max_bodies bodies, boom::max_quads sprite quads, the buffers' capacities. Reach: a record whose reach sphere
// (the glow's, the smoke's, the embers' at their age, grown by the pixel floors) is outside the view volume builds
// nothing. State: BoomState holds per ring slot the record's first stage frame, its floor, its world point and its seed
// cones, keyed by the sequence (a reused slot is recognised), and its hull shape; CPU data naming no device resource: a
// Reset leaves it valid. No allocation, no libm, SSE2 floats and doubles; scratch in the TU's .bss. Shed order (section
// 7): after (4) the glows, (5) the secondaries past the two newest live ones of each death (all of them from P), largest
// first.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
constexpr unsigned boom_max_cones = 6;
struct BoomSlot {
    std::uint32_t sequence = 0, first_ms = 0;
    float floor_ms = 0.f;
    double p0[3]{};                    // the death point in world (view units)
    float cone[boom_max_cones][3]{};   // the seed cones' axes (world, unit)
    std::uint8_t cones = 0, latched = 0;
    // The hull shape (the big tiers): its centre from the death point and its axes (world; view units), half-extents
    // (the box's; the capsule's radius, radius, half-length); kind 0 none, 1 the bubble cache's box, 2 the axis capsule.
    float hull_c[3]{}, hull_ax[3][3]{}, hull_e[3]{};
    std::uint8_t hull_kind = 0;
};
// One sub-burst's hashed constants (per record: the stage caches them by sequence, BoomState::compose).
struct ComposeEntry {
    float cl[3], jl[3]; // its offset direction x compose_offset h1 and its jet axis (the hull frame; 0 for a sphere)
    float rl, reach, delta, pace, ti;
    std::uint8_t jet;
};
// The composed bodies' entries by record (the hashes taken once per record, not per frame; round robin over max_bodies).
// One sub-burst's placement at unit 1 at the age x_ref and its slopes in the age (the stage's per-frame advance): the
// rotated offset direction x compose_offset h1 (view), its jet axis, local radius and reach, rise, erosion compensation
// (the body's loss over its own), its clocks x_i, x_e and its heat's factor.
struct ComposeRef {
    float off[3], axis[3];
    float rl, reach, rise, drise, comp, dcomp, xi, dxi, xe, dxe, ti, cool;
};
struct ComposeCache {
    std::uint32_t sequence = 0;
    std::uint8_t spheres = 0, jets = 0, count = 0, valid = 0;
    ComposeEntry en[core::boom::compose_max]{};
    // the placement (refreshed on a compose_refresh_px move of the projected centre, every compose_refresh_frames
    // builds, past compose_refresh_dx of the age, at the next birth or with another set) and the outline's estimate
    ComposeRef ref[core::boom::compose_max]{};
    float outline = 0.f, x_ref = -1.f, next_birth = 2.f, px[2]{};
    std::uint8_t m = 0, set = 0xff, frames = 0;
};
struct BoomState {
    BoomSlot slots[core::ring_capacity];
    ComposeCache compose[core::boom::max_bodies];
    std::uint32_t compose_key[core::boom::max_bodies]{};
    unsigned compose_next = 0;
    bool octaves_coarse = false; // the budget's step (1) with its hysteresis
    std::uint8_t coarse_frames = 0;
};
inline void boom_clear(BoomState& s) noexcept {
    for (auto& v : s.slots) v = BoomSlot{};
    s.octaves_coarse = false;
    s.coarse_frames = 0;
    for (auto& c : s.compose) c = ComposeCache{};
    for (auto& k : s.compose_key) k = 0;
    s.compose_next = 0;
}
// The element switches (the fixture isolates one part; the header's look by default).
struct BoomTuning {
    bool body = true, star = true, shock = true, embers = true, fragments = true, glow = true, smoke = true;
    bool halo = true, core = true; // the body's halo in air and its white-hot core (the fixture's geometry gates: core off)
    bool secondaries = true, hull_light = true; // the big tiers' pre-roll: the secondaries (body, star, glow, embers) and the
                                                // dying hull's light
    bool force_shock = false; // the fixture's shell gates and budget row: the missile's shell drawn though missile_shock is off
    // Section 13: the fire wave (capital, station), its refraction request, and the composed body (off: the single
    // sphere, the lab's "single body" toggle); force_shared / force_n: the fixture's compose rows (0: the law's).
    bool wave = true, wave_shimmer = true, compose = true, force_shared = false;
    bool wave_octave2 = true; // the sheet's second octave (the fixture's one-octave weight row: false)
    unsigned force_n = 0, force_jets = 0xffu;
    float box_scale = 1.f; // the fixture's compose_box_holds_spheres: the composed body's box grown
    float budget_screens = core::boom::budget_screens; // the family's own cap (the fixture's hysteresis gate lifts it)
    bool cluster = true, reach_cull = true;
    unsigned min_frames = core::look::min_frames;
    float fine_band_px = core::look::fireball_fine_band_px;
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
};
struct BoomCounts {
    unsigned deaths = 0, bodies = 0, stars = 0, shocks = 0, embers = 0, fragments = 0, glows = 0, smokes = 0, darks = 0;
    unsigned halos = 0, embers_hidden = 0; // the bodies' halos; embers inside their body (not drawn this frame)
    unsigned out_of_reach = 0, capped = 0, floored = 0, shed = 0, smokes_shed = 0, shocks_shed = 0, glows_shed = 0;
    unsigned octaves = core::look::fireball_octaves; // 2: the budget's step (1) this frame
    unsigned fine = 0;      // bodies drawn without their full third octave by their size
    float screens = 0.f, body_screens = 0.f;
    float max_floor_ms = 0.f; // the largest floor a live record latched (Present's keep, HookState::boom_floor_ms)
    float max_cluster = 1.f;  // the largest cluster N this frame (the nested-kill check)
    // The big tiers: secondaries drawn (and shed: section 7 step 5), born this frame (the newest one's centre, view,
    // unbiased: the cadence gate), hull lights drawn; records latched with the bubble cache's box or the axis capsule.
    unsigned secondaries = 0, secondaries_shed = 0, secondaries_born = 0, hull_lights = 0, bubble_cached = 0, capsules = 0;
    float born_at[3]{};
    // Section 13: the fire waves drawn and shed, those in the view plane for unread rows (weapon_death wave_plane=), the
    // newest wave's refraction strips (0: none or capped past the cap) and their screens; the composed bodies, those in
    // the shared-noise mode, the spheres the LOD removed, the eye inside a cluster.
    unsigned waves = 0, waves_shed = 0, wave_view_plane = 0, wave_strips = 0, wave_strips_capped = 0, wave_strips_dropped = 0;
    float wave_strip_screens = 0.f;
    // the drawn waves' quads (8 strips or 1 box each) and their screens (the strips' clipped sum or the box)
    unsigned wave_quads = 0, wave_octaves_shed = 0;
    float wave_screens = 0.f;
    unsigned composed = 0, compose_shared = 0, compose_dropped = 0, compose_eye = 0;
    float body_R0 = 0.f; // the law's radius R(t) (with its floor) of the first body box (view units)
};
// The frame's outputs (four vertices an element; nullptr / 0: none built). The caller hands each buffer past whatever
// another family wrote into it.
struct BoomBuffers {
    renderer::WeaponSpriteVertex* sprites = nullptr;
    unsigned sprite_cap = 0, sprite_quads = 0;
    renderer::WeaponGlowVertex* glows = nullptr;
    unsigned glow_cap = 0, glow_boxes = 0;
    renderer::WeaponFireballVertex* fireballs = nullptr;
    unsigned fireball_cap = 0, fireball_boxes = 0;
    renderer::WeaponFireballVertex* smokes = nullptr; // nullptr: no smoke (no multiply blend)
    unsigned smoke_cap = 0, smoke_boxes = 0;
    renderer::WeaponGlowVertex* scorches = nullptr;   // the fragments' dark boxes (nullptr: none)
    unsigned scorch_cap = 0, scorch_boxes = 0;
    renderer::WeaponFireballVertex* shocks = nullptr;
    unsigned shock_cap = 0, shock_boxes = 0;
    renderer::WeaponGlowVertex* halos = nullptr; // the bodies' halos in air (weapon_halo_ps.hlsl)
    unsigned halo_cap = 0, halo_boxes = 0;
    // section 13.1: the fire waves (weapon_wave_ps.hlsl), 4 x renderer::weapon_wave_quads vertices a wave (its quads,
    // WeaponWaveConstants::quads of them drawn); wave_cap counts waves
    renderer::WeaponFireballVertex* waves = nullptr;
    unsigned wave_cap = 0, wave_boxes = 0;
    const renderer::WeaponWaveConstants* wave_constants = nullptr; // out: the waves' draw constants (the stage's storage)
    // Section 13.2: the composed bodies' tables (room for boom::max_bodies; nullptr: the single body), one per body box;
    // out: how many leading fireball boxes are composed.
    renderer::WeaponComposeConstants* compose = nullptr;
    unsigned composed_boxes = 0;
    x3m::engine_shimmer::WaveRequest* wave_shimmer = nullptr; // the newest wave's refraction (nullptr: none)
    unsigned wave_strip_room = x3m::engine_shimmer::max_rects; // the shimmer pass's rects left for the strips (the glue's)
    unsigned wave_shimmer_count = 0;
};
// The tier's R_f (metres) of a record: tier_f R_obj clamped.
inline float boom_r_f_m(const core::Event& e) noexcept {
    namespace bm = core::boom;
    const unsigned t = e.style < bm::drawn_tiers ? e.style : 0u;
    const float r = bm::tier_f[t] * float(e.radius) * (1.f / core::engine_units_per_metre);
    return r < bm::tier_min_m[t] ? bm::tier_min_m[t] : r > bm::tier_max_m[t] ? bm::tier_max_m[t] : r;
}
// The body's radius law (2.1) at t game ms after the body's birth (view units, any unit of R_f and R_0).
inline float boom_body_radius(unsigned tier, float R_f, float R_0, float t_ms) noexcept {
    namespace bm = core::boom;
    const float L = bm::tier_life_ms[tier];
    return R_0 + (R_f - R_0) * (1.f - core::exp_neg(-t_ms / (bm::tier_rise[tier] * L))) + bm::body_grow * R_f * t_ms / L;
}
// The body's heat law (2.1): 1 - body_cool t / (body_hold L) to body_hold L, then linear to 0 at L.
inline float boom_body_heat(unsigned tier, float t_ms) noexcept {
    namespace bm = core::boom;
    const float x = t_ms / bm::tier_life_ms[tier];
    if (x <= bm::body_hold) return 1.f - bm::body_cool * x / bm::body_hold;
    const float end = 1.f - bm::body_cool;
    return x >= 1.f ? 0.f : end * (1.f - x) / (1.f - bm::body_hold);
}
// The ember speed (m/s, 2.4).
inline float boom_ember_speed_m(float R_f_m) noexcept {
    namespace bm = core::boom;
    const float v = bm::ember_v_x * R_f_m;
    return v < bm::ember_v_min ? bm::ember_v_min : v > bm::ember_v_max ? bm::ember_v_max : v;
}
// A seed cone's axis (world, unit): an isotropic direction from the record's hashes (the hull's rejection draw).
inline void boom_cone_axis(std::uint32_t sequence, unsigned c, float d[3]) noexcept {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        float q[3];
        cube_point(element_hash(sequence, core::boom::cone_element + 8u * c + attempt), q);
        const float r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        if (r2 > 1e-4f && (r2 <= 1.f || attempt == 7)) {
            const float inv = rsqrt_nr(r2);
            for (unsigned k = 0; k < 3; ++k) d[k] = q[k] * inv;
            return;
        }
    }
    d[0] = d[1] = 0.f;
    d[2] = 1.f;
}
// An ember's direction inside its cone (world, unit): the axis plus a hashed offset of at most tan(ember_cone_deg)
// across it (the cube point's part across the axis, cut to the unit disc).
constexpr float boom_cone_tan = 0.46630766f; // tan 25 deg
static_assert(core::boom::ember_cone_deg == 25.f, "boom_cone_tan is tan(ember_cone_deg)");
inline void boom_ember_dir(const float a[3], std::uint32_t h, float d[3]) noexcept {
    float q[3];
    cube_point(h, q);
    const float qa = q[0] * a[0] + q[1] * a[1] + q[2] * a[2];
    for (unsigned k = 0; k < 3; ++k) q[k] -= qa * a[k];
    float q2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    const float s = q2 > 1.f ? rsqrt_nr(q2) : 1.f;
    for (unsigned k = 0; k < 3; ++k) d[k] = a[k] + boom_cone_tan * s * q[k];
    const float inv = rsqrt_nr(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (unsigned k = 0; k < 3; ++k) d[k] *= inv;
}
// The drawn sphere's factor (its radius over R(t)) at a projected radius of R_px pixels at 1440 rows.
inline float boom_sphere_factor(float R_px) noexcept {
    namespace bm = core::boom;
    const float u = (R_px - bm::sphere_px0) / (bm::sphere_px1 - bm::sphere_px0);
    return bm::body_r_sphere + (bm::body_r_sphere_big - bm::body_r_sphere) * (u < 0.f ? 0.f : u > 1.f ? 1.f : u);
}
// The radius and heat laws with an explicit life and rise (a secondary's: secondary_life L, secondary_rise).
inline float boom_radius_law(float R_f, float R_0, float t_ms, float L, float rho) noexcept {
    return R_0 + (R_f - R_0) * (1.f - core::exp_neg(-t_ms / (rho * L))) + core::boom::body_grow * R_f * t_ms / L;
}
inline float boom_heat_law(float t_ms, float L) noexcept {
    namespace bm = core::boom;
    const float x = t_ms / L;
    if (x <= bm::body_hold) return 1.f - bm::body_cool * x / bm::body_hold;
    return x >= 1.f ? 0.f : (1.f - bm::body_cool) * (1.f - x) / (1.f - bm::body_hold);
}
// The tier's secondaries' births (game ms from the record's first frame, ascending; returns N): the first at 0, then the
// geometric gaps g0 r^j (g0 = P (1 - r) / (1 - r^(N - 1)): they sum to P) each x (1 +- secondary_jitter) by the seed
// (element secondary_element + 4 j), none past P.
inline unsigned boom_secondary_births(std::uint32_t sequence, unsigned tier, float out[]) noexcept {
    namespace bm = core::boom;
    const unsigned N = tier < bm::drawn_tiers ? bm::tier_secondaries[tier] : 0u;
    if (!N) return 0;
    const float P = bm::tier_preroll_ms[tier], r = bm::tier_cadence[tier];
    float rn = 1.f;
    for (unsigned j = 0; j + 1 < N; ++j) rn *= r;
    float g = N > 1 && rn < 1.f ? P * (1.f - r) / (1.f - rn) : P, at = 0.f;
    out[0] = 0.f;
    for (unsigned k = 1; k < N; ++k) {
        const float u = unit_of(element_hash(sequence, bm::secondary_element + 4u * k));
        at += g * (1.f + bm::secondary_jitter * (2.f * u - 1.f));
        g *= r;
        out[k] = at < P ? at : P;
    }
    return N;
}
// The record's node rows (core::death: row 2 in dir, row 0 in alt_rgb332 / detail / calibre_q, x 127) as an orthonormal
// frame (row 1 = row 2 x row 0); world +z (and an axis across it) when unread.
inline void boom_rows(const core::Event& e, float ax[3][3]) noexcept {
    float z[3] = {float(e.dir[0]), float(e.dir[1]), float(e.dir[2])};
    float x[3] = {float(std::int8_t(e.alt_rgb332)), float(std::int8_t(e.detail)), float(std::int8_t(e.calibre_q))};
    const float zz = dot3(z, z);
    if (zz < 0.25f * 127.f * 127.f) {
        z[0] = z[1] = 0.f;
        z[2] = 1.f;
    } else {
        const float s = rsqrt_nr(zz);
        for (unsigned q = 0; q < 3; ++q) z[q] *= s;
    }
    float xz = dot3(x, z);
    for (unsigned q = 0; q < 3; ++q) x[q] -= xz * z[q];
    if (dot3(x, x) < 0.25f * 127.f * 127.f * 0.25f) { // unread or parallel: any axis across z
        x[0] = z[0] * z[0] < 0.81f ? 1.f : 0.f;
        x[1] = z[0] * z[0] < 0.81f ? 0.f : 1.f;
        x[2] = 0.f;
        xz = dot3(x, z);
        for (unsigned q = 0; q < 3; ++q) x[q] -= xz * z[q];
    }
    const float sx = rsqrt_nr(dot3(x, x));
    for (unsigned q = 0; q < 3; ++q) x[q] *= sx;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    for (unsigned q = 0; q < 3; ++q) {
        ax[0][q] = x[q];
        ax[1][q] = y[q];
        ax[2][q] = z[q];
    }
}
// The hull shape at the record's first frame: the owner's box from the bubble cache (no read: bubble_peek), its owner
// frame taken to world by the rows (centre sum c_k row_k, half-extents e_k / bubble_scale: the hull bounds), else the
// axis capsule along row 2 (secondary_capsule_len R_obj long, secondary_capsule_r R_obj in radius). Marks the record
// flag_bubble_looked and, from the cache, flag_bubble_cached (weapon_death bubble_cached=).
inline bool boom_hull_latch(BoomSlot& s, core::Event& e, const BubbleCache* cache) noexcept {
    namespace bm = core::boom;
    boom_rows(e, s.hull_ax);
    Bubble b;
    const bool cached = bubble_peek(cache, e.owner, e.node, e.handle, &b);
    e.flags = std::uint8_t(e.flags | core::flag_bubble_looked | (cached ? core::flag_bubble_cached : 0));
    const float r = float(e.radius) * core::world_scale;
    if (cached) {
        for (unsigned q = 0; q < 3; ++q)
            s.hull_c[q] = b.c[0] * s.hull_ax[0][q] + b.c[1] * s.hull_ax[1][q] + b.c[2] * s.hull_ax[2][q];
        for (unsigned k = 0; k < 3; ++k) s.hull_e[k] = b.e[k] * (1.f / core::bubble_scale);
        s.hull_kind = 1;
    } else {
        for (unsigned q = 0; q < 3; ++q) s.hull_c[q] = 0.f;
        s.hull_e[0] = s.hull_e[1] = bm::secondary_capsule_r * r;
        s.hull_e[2] = 0.5f * bm::secondary_capsule_len * r;
        s.hull_kind = 2;
    }
    return cached;
}
// A hashed point inside the hull shape (from the death point, world, view units), secondary_inset of the way out.
inline void boom_hull_point(const BoomSlot& s, std::uint32_t h, float out[3]) noexcept {
    const float in = core::boom::secondary_inset;
    float q[3], l[3];
    cube_point(h, q);
    if (s.hull_kind == 1) {
        for (unsigned k = 0; k < 3; ++k) l[k] = in * q[k] * s.hull_e[k];
    } else { // the capsule: a point of its segment plus one of the ball of its radius
        const float rad = s.hull_e[0], hl = s.hull_e[2] > rad ? s.hull_e[2] - rad : 0.f;
        float b[3];
        cube_point(mix32(h ^ 0x85ebca6bu), b);
        const float r2 = dot3(b, b);
        if (r2 > 1.f) {
            const float k = rsqrt_nr(r2);
            for (unsigned c = 0; c < 3; ++c) b[c] *= k;
        }
        l[0] = in * rad * b[0];
        l[1] = in * rad * b[1];
        l[2] = in * (q[2] * hl + rad * b[2]);
    }
    for (unsigned c = 0; c < 3; ++c)
        out[c] = s.hull_c[c] + l[0] * s.hull_ax[0][c] + l[1] * s.hull_ax[1][c] + l[2] * s.hull_ax[2][c];
}
// Where the ray from the hull shape's centre along d (world, unit) leaves it (from the death point, world, view units).
inline void boom_hull_exit(const BoomSlot& s, const float d[3], float out[3]) noexcept {
    const float dl[3] = {dot3(d, s.hull_ax[0]), dot3(d, s.hull_ax[1]), dot3(d, s.hull_ax[2])};
    float t = 0.f;
    if (s.hull_kind == 1) {
        t = 1e30f;
        for (unsigned k = 0; k < 3; ++k) {
            const float a = dl[k] < 0.f ? -dl[k] : dl[k];
            if (a > 1e-6f && s.hull_e[k] < t * a) t = s.hull_e[k] / a;
        }
        if (!(t < 1e29f)) t = 0.f;
    } else if (s.hull_kind == 2) {
        const float rad = s.hull_e[0], hl = s.hull_e[2] > rad ? s.hull_e[2] - rad : 0.f;
        const float da = dl[2] < 0.f ? -dl[2] : dl[2], dp2 = 1.f - da * da;
        const float dp = dp2 > 0.f ? scalar::sqrt(dp2) : 0.f;
        if (dp > 1e-4f && rad * da <= hl * dp) {
            t = rad / dp; // the cylinder
        } else {          // a cap: |t d - hl sign(d_a) axis| = rad
            const float b = hl * da, q = b * b - hl * hl + rad * rad;
            t = b + scalar::sqrt(q > 0.f ? q : 0.f);
        }
    }
    for (unsigned c = 0; c < 3; ++c) out[c] = s.hull_c[c] + d[c] * t;
}
// Section 13.2: the composed body's table, the program's composed() in float32 (weapon_fireball_ps.hlsl; its hash2,
// boom::compose_*): the spheres born at the body's age fraction x, view space about the cluster's centre C at `unit`
// (the program's sphere.w), the hull frame's rows in view (ax), n drawn spheres of the hashed n_full, `jets` jets.
inline float boom_frac(float x) noexcept {
    return x - float(scalar::floor(double(x)));
}
inline float boom_hash2(float x, float y) noexcept {
    float p0 = boom_frac(x * 0.1031f), p1 = boom_frac(y * 0.1031f), p2 = p0;
    const float d = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
    p0 += d;
    p1 += d;
    p2 += d;
    return boom_frac((p0 + p1) * p2);
}
// sin and cos of 2 pi h, h in [0, 1) (quadrant reduction of sincos_quarter).
inline void boom_sincos_turn(float h, float* s, float* c) noexcept {
    float q4 = 4.f * boom_frac(h);
    const float q = float(scalar::floor(double(q4)));
    float s0, c0;
    sincos_quarter((q4 - q) * 1.5707963f, &s0, &c0);
    const int k = int(q) & 3;
    *s = k == 0 ? s0 : k == 1 ? c0 : k == 2 ? -s0 : -c0;
    *c = k == 0 ? c0 : k == 1 ? -s0 : k == 2 ? -c0 : s0;
}
struct ComposeSphere {
    float c[3];    // centre (view)
    float R;       // radius (a jet's short semi-axis)
    float reach;   // its farthest extent along any axis (a jet's long semi-axis)
    float axis[3]; // a jet's axis (view, unit; 0 for a sphere)
    float xi, xe;  // its age clock x_i and its erosion and heat clock 1 - (1 - x_i)^k_i (its own pace)
    float ti;      // its table index (the seeds' step)
    float cool;    // its heat's factor (1 - compose_offset_cool |offset| / compose_offset)
};
// The table (the program draws it; the lab's composeTable mirrors it): sphere i < n of the hashed n_full has the local
// radius compose_r_lo + compose_r_span (1 - (i + h0) / n_full) (the largest first: the LOD drops the last), sphere 0 at
// the centre, the others offset compose_offset h1 along unit(lerp(u, +-z, compose_axis_blend)) (u from h3, h4; alone
// without rows), born compose_spread h2 L; a jet (index n_full + j) a compose_jet_long : compose_jet_short spheroid along
// (cos th cos ph, cos th sin ph, sin th), th = (2 h3 - 1) compose_jet_cone, ph = 2 pi h4, its centre (0, 0, (2 h1 - 1)
// compose_offset) + its axis x long / 2, born compose_jet_birth0 + compose_jet_birth_span h2; its erosion paced by k =
// compose_pace_lo + compose_pace_span h5 (2026-11-07: the outline changes between 0.3 and 0.6 L as the sub-bursts erode
// at their own pace). The hashes from the record's noise seed: hash2(seed + compose_seed_step i, 3.1 + 1.7 j).
inline unsigned boom_compose_entries(float seed, unsigned n, unsigned jets, unsigned n_full, bool valid,
                                     ComposeEntry* out) noexcept {
    namespace bm = core::boom;
    unsigned m = 0;
    for (unsigned i = 0; i < n + jets && i < bm::compose_max; ++i) {
        const bool jet = i >= n;
        const float ti = jet ? float(n_full + (i - n)) : float(i);
        const float hs = seed + bm::compose_seed_step * ti;
        const float h0 = boom_hash2(hs, 3.1f), h1 = boom_hash2(hs, 4.8f), h2 = boom_hash2(hs, 6.5f);
        const float h3 = boom_hash2(hs, 8.2f), h4 = boom_hash2(hs, 9.9f), h5 = boom_hash2(hs, 11.6f);
        ComposeEntry& o = out[m++];
        o.jet = jet;
        o.ti = ti;
        o.delta = jet ? bm::compose_jet_birth0 + bm::compose_jet_birth_span * h2 : (ti > 0.5f ? bm::compose_spread * h2 : 0.f);
        o.pace = bm::compose_pace_lo + bm::compose_pace_span * h5;
        float sp, cp;
        boom_sincos_turn(h4, &sp, &cp);
        for (unsigned q = 0; q < 3; ++q) o.jl[q] = 0.f;
        if (jet) {
            const float th = (2.f * h3 - 1.f) * bm::compose_jet_cone;
            float st, ct;
            sincos_quarter(th < 0.f ? -th : th, &st, &ct);
            if (th < 0.f) st = -st;
            o.jl[0] = ct * cp;
            o.jl[1] = ct * sp;
            o.jl[2] = st;
            for (unsigned q = 0; q < 3; ++q) o.cl[q] = o.jl[q] * (0.5f * bm::compose_jet_long);
            o.cl[2] += (2.f * h1 - 1.f) * bm::compose_offset;
            o.rl = bm::compose_jet_short;
            o.reach = bm::compose_jet_long;
        } else {
            const float z = 2.f * h3 - 1.f, rr2 = 1.f - z * z, rr = rr2 > 0.f ? scalar::sqrt(rr2) : 0.f;
            const float bl = valid ? bm::compose_axis_blend : 0.f;
            const float u[3] = {rr * cp * (1.f - bl), rr * sp * (1.f - bl), z * (1.f - bl) + (z >= 0.f ? bl : -bl)};
            const float uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
            const float k = ti > 0.5f ? bm::compose_offset * (bm::compose_offset_min + (1.f - bm::compose_offset_min) * h1) *
                                            rsqrt_nr(uu > 1e-8f ? uu : 1e-8f)
                                      : 0.f;
            for (unsigned q = 0; q < 3; ++q) o.cl[q] = u[q] * k;
            o.rl = bm::compose_r_lo + bm::compose_r_span * (1.f - (ti + h0) / float(n_full > 0 ? n_full : 1u));
            o.reach = o.rl;
        }
    }
    return m;
}
// The single body's outline over R(t) by age (compose_erode_*: the measured loss past compose_erode_x0).
inline float boom_outline_loss(float x) noexcept {
    namespace bm = core::boom;
    const float s = 1.f - bm::compose_erode_k * (x > bm::compose_erode_x0 ? x - bm::compose_erode_x0 : 0.f);
    return s > 0.05f ? s : 0.05f;
}
// The sub-bursts now: the first n spheres and the first jets jets of the entries that are born at the body's age
// fraction x, about C at `unit` in the hull frame's rows ax (view): radius and offset x unit x rise(x_i) (x_i = (x -
// delta) / (1 - delta), rise = r0 + (1 - r0)(1 - e^(-x_i / rho)) + body_grow x_i). outline_k > 0: the outline's estimate
// (R and reach x outline_k x boom_outline_loss(x_e) / boom_outline_loss(x)).
inline unsigned boom_compose_place(const ComposeEntry* en, unsigned count, unsigned n, unsigned jets, const float C[3],
                                   float unit, float x, float r0, float rho, const float ax[3][3], ComposeSphere* out,
                                   float outline_k = 0.f) noexcept {
    namespace bm = core::boom;
    unsigned m = 0, spheres = 0, js = 0;
    const float body_loss = outline_k > 0.f ? boom_outline_loss(x) : 1.f;
    for (unsigned i = 0; i < count; ++i) {
        const ComposeEntry& e = en[i];
        if (e.jet ? js >= jets : spheres >= n) continue;
        e.jet ? ++js : ++spheres;
        const float xi = (x - e.delta) / (1.f - e.delta);
        if (!(xi >= 0.f)) continue;
        const float rise = r0 + (1.f - r0) * (1.f - core::exp_neg(-xi / (rho > 1e-3f ? rho : 1e-3f))) + bm::body_grow * xi;
        const float left = 1.f - (xi < 1.f ? xi : 1.f);
        const float ln_left = left > 0.f ? core::log10f_sse(left) * 2.30258509f : -1e30f;
        const float xe = 1.f - core::exp_neg(e.pace * ln_left);
        const float sc = unit * rise;
        ComposeSphere& o = out[m++];
        for (unsigned q = 0; q < 3; ++q) {
            o.c[q] = C[q] + sc * (e.cl[0] * ax[0][q] + e.cl[1] * ax[1][q] + e.cl[2] * ax[2][q]);
            o.axis[q] = e.jl[0] * ax[0][q] + e.jl[1] * ax[1][q] + e.jl[2] * ax[2][q];
        }
        const float k = outline_k > 0.f ? outline_k * boom_outline_loss(xe) / body_loss : 1.f;
        o.R = sc * e.rl * k;
        o.reach = sc * e.reach * k;
        o.xi = xi;
        o.xe = xe;
        o.ti = e.ti;
        const float ol = scalar::sqrt(e.cl[0] * e.cl[0] + e.cl[1] * e.cl[1] + e.cl[2] * e.cl[2]);
        o.cool = 1.f - bm::compose_offset_cool * (ol < bm::compose_offset ? ol : bm::compose_offset) / bm::compose_offset;
    }
    return m;
}
// The stage's form (section 13.2, the CPU pass): the first n spheres and the first jets jets of the entries that are born
// at the age x, at unit 1 in the hull frame's rows ax (view), with each sub-burst's erosion compensation (2026-11-07:
// per sub-burst, not on the union): its radius x the body's loss over its own, comp = boom_outline_loss(x) /
// boom_outline_loss(x_e) (both capped at compose_erode_cap), so its eroded outline follows the single body's and the
// cluster's unit holds no erosion term; and the slopes of rise, comp and the clocks in x. *next_birth: the earliest
// birth past x (2 without one).
inline unsigned boom_compose_ref(const ComposeEntry* en, unsigned count, unsigned n, unsigned jets, float x, float r0,
                                 float rho, const float ax[3][3], ComposeRef* out, float* next_birth) noexcept {
    namespace bm = core::boom;
    unsigned m = 0, spheres = 0, js = 0;
    float nb = 2.f;
    const float cap = bm::compose_erode_cap, xc = x < cap ? x : cap;
    const float lb = boom_outline_loss(xc);
    const float dlb = x < cap && x > bm::compose_erode_x0 && lb > 0.05f ? -bm::compose_erode_k : 0.f;
    const float rh = rho > 1e-3f ? rho : 1e-3f;
    for (unsigned i = 0; i < count; ++i) {
        const ComposeEntry& e = en[i];
        if (e.jet ? js >= jets : spheres >= n) continue;
        e.jet ? ++js : ++spheres;
        if (x < e.delta) {
            nb = e.delta < nb ? e.delta : nb;
            continue;
        }
        const float dxi = 1.f / (1.f - e.delta), xi = (x - e.delta) * dxi;
        const float ex = core::exp_neg(-xi / rh);
        const float rise = r0 + (1.f - r0) * (1.f - ex) + bm::body_grow * xi;
        const float drise = ((1.f - r0) * ex / rh + bm::body_grow) * dxi;
        const float left = 1.f - (xi < 1.f ? xi : 1.f);
        const float ln_left = left > 0.f ? core::log10f_sse(left) * 2.30258509f : -1e30f;
        const float xe = 1.f - core::exp_neg(e.pace * ln_left);
        const float dxe = left > 1e-4f ? e.pace * (1.f - xe) / left * dxi : 0.f;
        const float xec = xe < cap ? xe : cap, le = boom_outline_loss(xec);
        const float dle = xe < cap && xe > bm::compose_erode_x0 && le > 0.05f ? -bm::compose_erode_k * dxe : 0.f;
        ComposeRef& o = out[m++];
        for (unsigned q = 0; q < 3; ++q) {
            o.off[q] = e.cl[0] * ax[0][q] + e.cl[1] * ax[1][q] + e.cl[2] * ax[2][q];
            o.axis[q] = e.jl[0] * ax[0][q] + e.jl[1] * ax[1][q] + e.jl[2] * ax[2][q];
        }
        o.rl = e.rl;
        o.reach = e.reach;
        o.rise = rise;
        o.drise = drise;
        o.comp = lb / le;
        o.dcomp = (dlb * le - lb * dle) / (le * le);
        o.xi = xi;
        o.dxi = dxi;
        o.xe = xe;
        o.dxe = dxe;
        o.ti = e.ti;
        const float ol = scalar::sqrt(e.cl[0] * e.cl[0] + e.cl[1] * e.cl[1] + e.cl[2] * e.cl[2]);
        o.cool = 1.f - bm::compose_offset_cool * (ol < bm::compose_offset ? ol : bm::compose_offset) / bm::compose_offset;
    }
    *next_birth = nb;
    return m;
}
// The placement now from the reference at x_ref advanced to x (linear), about C at `unit`: ComposeSphere's R and reach
// with the compensation (drawn), and est (nullable) the outline's estimate's spheres at unit 1 (R and reach without it,
// over the drawn-sphere factor).
inline void boom_compose_advance(const ComposeRef* ref, unsigned m, float x_ref, float x, const float C[3], float unit,
                                 ComposeSphere* out) noexcept {
    const float dx = x - x_ref;
    for (unsigned i = 0; i < m; ++i) {
        const ComposeRef& r = ref[i];
        const float rise = r.rise + r.drise * dx, comp = r.comp + r.dcomp * dx;
        const float sc = unit * (rise > 0.f ? rise : 0.f);
        ComposeSphere& o = out[i];
        for (unsigned q = 0; q < 3; ++q) {
            o.c[q] = C[q] + sc * r.off[q];
            o.axis[q] = r.axis[q];
        }
        o.R = sc * r.rl * comp;
        o.reach = sc * r.reach * comp;
        const float xi = r.xi + r.dxi * dx, xe = r.xe + r.dxe * dx;
        o.xi = xi < 0.f ? 0.f : xi > 1.f ? 1.f : xi;
        o.xe = xe < 0.f ? 0.f : xe > 1.f ? 1.f : xe;
        o.ti = r.ti;
        o.cool = r.cool;
    }
}
// The table at once (the fixture's and the lab's form): the entries of n spheres and `jets` jets, placed.
inline unsigned boom_compose_table(const float C[3], float unit, float x, float seed, unsigned n, unsigned jets,
                                   unsigned n_full, bool valid, float r0, float rho, const float ax[3][3],
                                   ComposeSphere* out, float outline_k = 0.f) noexcept {
    ComposeEntry en[core::boom::compose_max];
    const unsigned count = boom_compose_entries(seed, n, jets, n_full, valid, en);
    return boom_compose_place(en, count, n, jets, C, unit, x, r0, rho, ax, out, outline_k);
}
// The mean of the cluster's projected outline over compose_bins rays from its centre (the 10 %-of-peak outline the
// gates measure, a sphere's outline its drawn radius over the drawn-sphere factor: its R): the composed body's unit is
// R(t) / this at unit 1.
inline float boom_compose_outline(const float C[3], const ComposeSphere* sp, unsigned m) noexcept {
    namespace bm = core::boom;
    static constexpr float dirs[16][2] = {
        {1.f, 0.f}, {0.92387953f, 0.38268343f}, {0.70710678f, 0.70710678f}, {0.38268343f, 0.92387953f},
        {0.f, 1.f}, {-0.38268343f, 0.92387953f}, {-0.70710678f, 0.70710678f}, {-0.92387953f, 0.38268343f},
        {-1.f, 0.f}, {-0.92387953f, -0.38268343f}, {-0.70710678f, -0.70710678f}, {-0.38268343f, -0.92387953f},
        {0.f, -1.f}, {0.38268343f, -0.92387953f}, {0.70710678f, -0.70710678f}, {0.92387953f, -0.38268343f}};
    static_assert(bm::compose_bins == 16, "the outline's rays");
    float sum = 0.f;
    for (unsigned k = 0; k < 16; ++k) {
        float best = 0.f;
        for (unsigned i = 0; i < m; ++i) {
            const float ox = sp[i].c[0] - C[0], oy = sp[i].c[1] - C[1];
            const float r = 0.5f * (sp[i].R + sp[i].reach);
            const float b = dirs[k][0] * ox + dirs[k][1] * oy, q = r * r - (ox * ox + oy * oy) + b * b;
            if (q > 0.f) {
                const float t = b + scalar::sqrt(q);
                best = t > best ? t : best;
            }
        }
        sum += best;
    }
    return sum * (1.f / 16.f);
}
// The composed body's charge factor over the single body's (section 7, size_derivation.py; the *_alone rows pin it).
inline float boom_compose_factor(unsigned n, bool shared) noexcept {
    namespace bm = core::boom;
    if (n < 2) return 1.f;
    return shared ? bm::compose_cost_shared_base + bm::compose_cost_shared_per * float(n)
                  : bm::compose_cost_base + bm::compose_cost_per * float(n);
}
// A body's octave weight 0 .. 2 (disc.w: the third octave's 0 .. 1, then the fourth's; 2026-11-07) from its projected
// radius `over_px` (px at the target, the third's band start fine_px, the fourth's fine4_px, bands likewise), and its
// charge per box screen: w2 + (w3 - w2) min(fine, 1) + (w4 - w3) max(fine - 1, 0).
inline float boom_fine(float r_px, float fine_px, float band_px, float fine4_px, float band4_px) noexcept {
    const float a = r_px - fine_px, b = r_px - fine4_px;
    const float f3 = a <= 0.f ? 0.f : a >= band_px ? 1.f : a / band_px;
    const float f4 = b <= 0.f ? 0.f : b >= band4_px ? 1.f : b / band4_px;
    return f3 < 1.f ? f3 : 1.f + f4;
}
inline float boom_fine_weight(float fine) noexcept {
    namespace lk = core::look;
    const float f3 = fine < 1.f ? fine : 1.f, f4 = fine > 1.f ? fine - 1.f : 0.f;
    return lk::budget_weight_fireball_coarse + (lk::budget_weight_fireball - lk::budget_weight_fireball_coarse) * f3 +
           (lk::budget_weight_fireball_fine4 - lk::budget_weight_fireball) * f4;
}
// Whether the record carries the node's rows (row 2 in dir; boom_rows' fallback otherwise).
inline bool boom_rows_valid(const core::Event& e) noexcept {
    const float z[3] = {float(e.dir[0]), float(e.dir[1]), float(e.dir[2])};
    const float x[3] = {float(std::int8_t(e.alt_rgb332)), float(std::int8_t(e.detail)), float(std::int8_t(e.calibre_q))};
    return dot3(z, z) >= 0.25f * 127.f * 127.f && dot3(x, x) >= 0.25f * 127.f * 127.f * 0.25f;
}
// Section 13.1: the fire wave's front radius (view units) t game ms after the body's birth.
inline float boom_wave_radius(float r0, float rmax, float tau, float t) noexcept {
    namespace bm = core::boom;
    if (rmax < r0) rmax = r0;
    const float u = t / tau;
    if (u <= 1.f) {
        const float w = 1.f - (u > 0.f ? u : 0.f);
        return r0 + (rmax - r0) * (1.f - w * w * w);
    }
    return rmax + bm::wave_creep * (rmax - r0) * (u - 1.f);
}
// Section 13.1: the wave's drawn quads, its annulus [ri, ro] (stretched in-plane distance) as eight octagon strips: each
// the plane's rectangle about phi_k = k pi / 4 (radially ri cos(pi / 8) .. ro, tangentially +-ro sin(pi / 8): the sector
// lies inside it; the long axis av stretched by `stretch`), +-hh along the normal nv, cut at the near plane (the box's
// edges crossing it), projected, taken to the screen box oriented along its projected radial direction, one pixel
// grown each side, and its screens clipped to the target. quad[k][4] the corners in NDC (back-left, back-right,
// front-left, front-right), screens[k] each one's clipped screens; returns the strips made (those wholly behind the near
// plane or off the target dropped). The caller keeps the strips only while their sum is under the slab's box.
struct WaveQuad {
    float ndc[4][2];
    float zq;
    float sector; // its sector k (the program shades the plane's angles (k -+ 1/2) pi / 4 there); -1 the box
};
inline float boom_clipped_quad_screens(const float q[4][2]) noexcept {
    // the parallelogram (corners 0, 1, 3, 2 in order) clipped to [-1, 1]^2 (Sutherland-Hodgman), its area / 4
    float a[12][2], b[12][2];
    const unsigned order[4] = {0, 1, 3, 2};
    unsigned n = 4;
    for (unsigned i = 0; i < 4; ++i) {
        a[i][0] = q[order[i]][0];
        a[i][1] = q[order[i]][1];
    }
    for (unsigned e = 0; e < 4 && n; ++e) {
        const unsigned axis = e >> 1;
        const float sg = (e & 1u) ? -1.f : 1.f; // keep sg * v <= 1
        unsigned m = 0;
        for (unsigned i = 0; i < n; ++i) {
            const float* p0 = a[i];
            const float* p1 = a[(i + 1) % n];
            const float d0 = 1.f - sg * p0[axis], d1 = 1.f - sg * p1[axis];
            if (d0 >= 0.f && m < 12) {
                b[m][0] = p0[0];
                b[m][1] = p0[1];
                ++m;
            }
            if ((d0 >= 0.f) != (d1 >= 0.f) && m < 12) {
                const float t = d0 / (d0 - d1);
                b[m][0] = p0[0] + t * (p1[0] - p0[0]);
                b[m][1] = p0[1] + t * (p1[1] - p0[1]);
                ++m;
            }
        }
        n = m;
        for (unsigned i = 0; i < n; ++i) {
            a[i][0] = b[i][0];
            a[i][1] = b[i][1];
        }
    }
    float area = 0.f;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned j = (i + 1) % n;
        area += a[i][0] * a[j][1] - a[j][0] * a[i][1];
    }
    return 0.125f * (area < 0.f ? -area : area);
}
inline unsigned boom_wave_strips(const HullView& f, const float C[3], const float nv[3], const float av[3],
                                 const float bv[3], float stretch, float ri, float ro, float hh, WaveQuad out[8],
                                 float screens[8]) noexcept {
    constexpr float c8 = 0.92387953f, s8 = 0.38268343f;
    constexpr float cs[8][2] = {{1.f, 0.f}, {0.70710678f, 0.70710678f}, {0.f, 1.f}, {-0.70710678f, 0.70710678f},
                                {-1.f, 0.f}, {-0.70710678f, -0.70710678f}, {0.f, -1.f}, {0.70710678f, -0.70710678f}};
    const float u0 = (ri > 0.f ? ri : 0.f) * c8, u1 = ro, hv = ro * s8;
    const float nz = f.near_z * 1.001f;
    const float px_x = 2.f / (f.width > 1.f ? f.width : 1.f), px_y = 2.f / (f.height > 1.f ? f.height : 1.f);
    unsigned made = 0;
    for (unsigned k = 0; k < 8; ++k) {
        const float co = cs[k][0], si = cs[k][1];
        float p[8][3];
        unsigned front = 0;
        for (unsigned c = 0; c < 8; ++c) {
            const float u = (c & 1u) ? u1 : u0, v = (c & 2u) ? hv : -hv, y = (c & 4u) ? hh : -hh;
            const float ka = stretch * (u * co - v * si), kb = u * si + v * co;
            for (unsigned q = 0; q < 3; ++q) p[c][q] = C[q] + ka * av[q] + kb * bv[q] + y * nv[q];
            front += p[c][2] > nz;
        }
        if (!front) continue;
        // the projected points: the corners in front, and where the box's edges cross the near plane
        float pr[20][2];
        unsigned np = 0;
        float zmin = 1e30f;
        const auto put = [&](const float* q3) {
            pr[np][0] = f.cam.m00 * q3[0] / q3[2] + f.cam.m20;
            pr[np][1] = f.cam.m11 * q3[1] / q3[2] + f.cam.m21;
            ++np;
            zmin = q3[2] < zmin ? q3[2] : zmin;
        };
        for (unsigned c = 0; c < 8; ++c)
            if (p[c][2] > nz) put(p[c]);
        if (front < 8)
            for (unsigned c = 0; c < 8; ++c)
                for (unsigned bit = 1; bit < 8; bit <<= 1) {
                    const unsigned d = c | bit;
                    if (d == c) continue;
                    const bool fc = p[c][2] > nz, fd = p[d][2] > nz;
                    if (fc == fd) continue;
                    const float t = (nz - p[c][2]) / (p[d][2] - p[c][2]);
                    float q3[3];
                    for (unsigned q = 0; q < 3; ++q) q3[q] = p[c][q] + t * (p[d][q] - p[c][q]);
                    q3[2] = nz;
                    if (np < 20) put(q3);
                }
        // the strip's radial direction on screen (pixels): its mid-line's ends when in front, else the screen's x
        float ax = 1.f, ay = 0.f;
        {
            float m0[3], m1[3];
            for (unsigned q = 0; q < 3; ++q) {
                m0[q] = C[q] + stretch * u0 * co * av[q] + u0 * si * bv[q];
                m1[q] = C[q] + stretch * u1 * co * av[q] + u1 * si * bv[q];
            }
            if (m0[2] > nz && m1[2] > nz) {
                const float dx = (f.cam.m00 * m1[0] / m1[2] - f.cam.m00 * m0[0] / m0[2]) / px_x,
                            dy = (f.cam.m11 * m1[1] / m1[2] - f.cam.m11 * m0[1] / m0[2]) / px_y;
                const float l2 = dx * dx + dy * dy;
                if (l2 > 1e-6f) {
                    const float inv = rsqrt_nr(l2);
                    ax = dx * inv;
                    ay = dy * inv;
                }
            }
        }
        float lo_s = 1e30f, hi_s = -1e30f, lo_t = 1e30f, hi_t = -1e30f;
        for (unsigned i = 0; i < np; ++i) {
            const float x = pr[i][0] / px_x, y = pr[i][1] / px_y; // pixels (y up)
            const float sa = x * ax + y * ay, ta = -x * ay + y * ax;
            lo_s = sa < lo_s ? sa : lo_s;
            hi_s = sa > hi_s ? sa : hi_s;
            lo_t = ta < lo_t ? ta : lo_t;
            hi_t = ta > hi_t ? ta : hi_t;
        }
        lo_s -= 1.f;
        hi_s += 1.f;
        lo_t -= 1.f;
        hi_t += 1.f;
        WaveQuad& w = out[made];
        for (unsigned c = 0; c < 4; ++c) {
            const float sa = (c & 1u) ? hi_s : lo_s, ta = (c & 2u) ? hi_t : lo_t;
            w.ndc[c][0] = (sa * ax - ta * ay) * px_x;
            w.ndc[c][1] = (sa * ay + ta * ax) * px_y;
        }
        const float sc = boom_clipped_quad_screens(w.ndc);
        if (!(sc > 0.f)) continue;
        w.zq = zmin > 1.5f * f.near_z ? zmin : 1.5f * f.near_z;
        w.sector = float(k);
        screens[made++] = sc;
    }
    return made;
}
// Sheds the largest boxes of other owners while the charged screens exceed the budget (the last box moves into the
// freed place). Returns the boxes shed.
// stride: the vertices of one box (4; a wave's 4 x wave_quads). Past 16 boxes (2026-11-07 v3: the wave sheds last, so
// a dense frame sheds most of its 128 glows first) the order is taken once (heap sort by area) and the survivors
// compacted in their order, not one scan a shed box.
template <class V>
inline unsigned shed_largest(V* verts, unsigned* count, BoxMeta* meta, float* total, float budget,
                             unsigned stride = 4) noexcept {
    unsigned shed = 0;
    if (*count > 16u && *count <= 255u && *total > budget) {
        const unsigned n = *count;
        std::uint8_t order[255];
        float key[255];
        bool gone[255];
        for (unsigned g = 0; g < n; ++g) {
            order[g] = std::uint8_t(g);
            key[g] = meta[g].own ? 3.0e38f : -meta[g].area; // the largest first, the own ship's never
            gone[g] = false;
        }
        renderer::sort_by_key(order, n, key);
        for (unsigned k = 0; k < n && *total > budget; ++k) {
            const unsigned g = order[k];
            if (meta[g].own) break;
            *total -= meta[g].area;
            gone[g] = true;
            ++shed;
        }
        unsigned w = 0;
        for (unsigned g = 0; g < n; ++g) {
            if (gone[g]) continue;
            if (w != g) {
                for (unsigned c = 0; c < stride; ++c) verts[stride * w + c] = verts[stride * g + c];
                meta[w] = meta[g];
            }
            ++w;
        }
        *count = w;
        return shed;
    }
    while (*total > budget && *count) {
        unsigned big = *count;
        for (unsigned g = 0; g < *count; ++g)
            if (!meta[g].own && (big == *count || meta[g].area > meta[big].area)) big = g;
        if (big == *count) break;
        *total -= meta[big].area;
        const unsigned last = --*count;
        if (big != last) {
            for (unsigned c = 0; c < stride; ++c) verts[stride * big + c] = verts[stride * last + c];
            meta[big] = meta[last];
        }
        ++shed;
    }
    return shed;
}
// Builds the frame's death elements into the buffers (see the header). f.budget_left: the screens this family may
// charge before shedding. visible (nullable): every drawn death's owner is added as drawn and in view (the ring's
// overflow order, core::ring_stamp_hidden). bubbles (nullable): the hull hits' bubble cache, looked up without a read for
// the dying owner's box (the big tiers' hull shape). Internal linkage (static): the scratch is the TU's .bss.
static inline BoomCounts build_boom(core::Ring& ring, BoomState& state, const HullView& f, const BoomTuning& t,
                                    BoomBuffers& out, core::VisibleOwners* visible = nullptr,
                                    const BubbleCache* bubbles = nullptr) noexcept {
    namespace bm = core::boom;
    BoomCounts n{};
    out.sprite_quads = out.glow_boxes = out.fireball_boxes = out.smoke_boxes = out.scorch_boxes = out.shock_boxes = 0;
    out.halo_boxes = out.wave_boxes = out.wave_shimmer_count = out.composed_boxes = 0;
    auto cap_of = [](const void* p, unsigned cap, unsigned most) { return p ? (cap < most ? cap : most) : 0u; };
    const unsigned sprite_cap = cap_of(out.sprites, out.sprite_cap, bm::max_quads);
    const unsigned glow_cap = cap_of(out.glows, out.glow_cap, renderer::weapon_glow_boxes);
    const unsigned body_cap = cap_of(out.fireballs, out.fireball_cap, bm::max_bodies);
    const unsigned smoke_cap = cap_of(out.smokes, out.smoke_cap, bm::max_bodies);
    const unsigned dark_cap = cap_of(out.scorches, out.scorch_cap, renderer::weapon_scorch_boxes);
    const unsigned shock_cap = cap_of(out.shocks, out.shock_cap, renderer::weapon_shock_boxes);
    const unsigned halo_cap = cap_of(out.halos, out.halo_cap, renderer::weapon_halo_boxes);
    const unsigned wave_cap = cap_of(out.waves, out.wave_cap, bm::max_waves < renderer::weapon_wave_boxes ? bm::max_waves
                                                                                                           : renderer::weapon_wave_boxes);
    if (!(f.width > 0.f) || !(f.height > 0.f) || !(f.cam.m00 > 0.f) || !(f.cam.m11 > 0.f)) return n;
    const float frame_floor_ms = float(t.min_frames) * float(f.step_ms);
    const Screen sc = screen_of(f);
    const Camera& prev = f.prev_valid ? f.prev : f.cam;
    const float step_s = float(f.step_ms) * 1e-3f;
    const float px_x = 0.5f * f.width * f.cam.m00;
    const float px_per_z = 1.f / (px_x < 0.5f * f.height * f.cam.m11 ? px_x : 0.5f * f.height * f.cam.m11);
    const float far_z = visible ? visible->far_z : 0.f;
    const float fine_px = core::look::fireball_fine_px * f.height * (1.f / 1440.f);
    const float fine_band_px = t.fine_band_px * f.height * (1.f / 1440.f);
    const float fine4_px = core::look::fireball_fine4_px * f.height * (1.f / 1440.f),
                fine4_band_px = core::look::fireball_fine4_band_px * f.height * (1.f / 1440.f);
    const float view_per_m = metres_to_view(1.f);
    // (A) The live records this frame: latch the first frame, the age, the reach cull; gather the bodies for the cluster.
    struct Live {
        std::uint16_t slot;
        float c[3];      // the death point (view)
        float R_f, R_0;  // view units
        float age, tb;   // game ms since the first stage frame; the body's age (age - the pre-roll)
        float R, T, x;   // the body now (R 0: not drawn)
        float cn;        // the cluster's N
        std::uint8_t tier, own;
    };
    static Live live[bm::max_bodies];
    unsigned nl = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        const core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_explosion || !(e.flags & core::flag_stage) || e.style >= bm::drawn_tiers)
            continue;
        BoomSlot& s = state.slots[i];
        if (s.sequence != e.sequence) {
            s = BoomSlot{};
            s.sequence = e.sequence;
        }
        if (!s.latched) {
            s.latched = 1;
            s.first_ms = f.now;
            s.floor_ms = frame_floor_ms;
            for (unsigned q = 0; q < 3; ++q) s.p0[q] = double(e.pos[q]) * double(core::world_scale);
            const std::uint32_t hc = element_hash(e.sequence, bm::cone_element - 1u);
            const unsigned span = unsigned(bm::ember_cones_hi - bm::ember_cones_lo) + 1u;
            s.cones = std::uint8_t(unsigned(bm::ember_cones_lo) + (hc >> 8) % span);
            if (s.cones > boom_max_cones) s.cones = boom_max_cones;
            for (unsigned c = 0; c < s.cones; ++c) boom_cone_axis(e.sequence, c, s.cone[c]);
            const bool cached = boom_hull_latch(s, ring.slots[i], bubbles);
            if (e.style == bm::corvette || e.style == bm::capital || e.style == bm::station) {
                n.bubble_cached += cached;
                n.capsules += !cached;
            } else
                s.hull_kind = 0;
        }
        if (s.floor_ms > n.max_floor_ms) n.max_floor_ms = s.floor_ms;
        const float age = float(f.now - s.first_ms);
        const unsigned tier = e.style;
        const float L = bm::tier_life_ms[tier], pre = bm::tier_preroll_ms[tier];
        if (age > pre + (bm::smoke_t0 + bm::smoke_span) * L + s.floor_ms) continue; // every element past its life
        float c[3];
        to_view(f.cam, s.p0, c);
        const float R_f_m = boom_r_f_m(e);
        const float R_f = R_f_m * view_per_m;
        const float R_0 = bm::tier_r0_obj[tier] * float(e.radius) * core::world_scale + bm::tier_r0_f[tier] * R_f;
        const float tb = age - pre;
        // the reach: the glow's and the smoke's spheres, the embers' (and fragments') distance at their age plus the
        // launch offset, the shock's radius
        const float te = tb > 0.f ? tb * 1e-3f : 0.f;
        const float ember_reach = bm::ember_speed_hi * boom_ember_speed_m(R_f_m) * view_per_m * te + bm::ember_p0 * 1.8f * R_f;
        float reach = bm::glow_r_max * R_f;
        const float sm = bm::smoke_r * R_f * (1.f + core::look::fireball_lobe_amp);
        reach = reach > sm ? reach : sm;
        reach = reach > ember_reach ? reach : ember_reach;
        reach = reach > bm::shock_r_x * R_f ? reach : bm::shock_r_x * R_f;
        if (bm::wave_tiers & (1u << tier)) { // the fire wave's box at its creep past r_max
            const float rw = 1.1f * bm::wave_stretch * bm::wave_r_x * R_f * (1.f + bm::wave_creep);
            reach = reach > rw ? reach : rw;
        }
        if (s.hull_kind) { // the hull shape and what leaves it (the secondaries' embers through the pre-roll)
            const float ext = scalar::sqrt(dot3(s.hull_c, s.hull_c)) + scalar::sqrt(dot3(s.hull_e, s.hull_e));
            const float sec_t = age < pre + bm::secondary_life * L ? age : pre + bm::secondary_life * L;
            const float hr = ext + bm::ember_speed_hi * bm::ember_v_max * view_per_m * sec_t * 1e-3f + ember_reach;
            reach = reach > hr ? reach : hr;
        }
        if (t.reach_cull && out_of_reach(c, reach, f, far_z, px_per_z)) {
            ++n.out_of_reach;
            continue;
        }
        if (nl >= bm::max_bodies) {
            ++n.capped;
            continue;
        }
        Live& lv = live[nl++];
        lv.slot = std::uint16_t(i);
        for (unsigned q = 0; q < 3; ++q) lv.c[q] = c[q];
        lv.R_f = R_f;
        lv.R_0 = R_0;
        lv.age = age;
        lv.tb = tb;
        lv.tier = std::uint8_t(tier);
        lv.own = std::uint8_t(f.own_node && e.node == f.own_node);
        lv.cn = 1.f;
        lv.R = lv.T = lv.x = 0.f;
        if (tb >= 0.f && tb < L) {
            lv.R = boom_body_radius(tier, R_f, R_0, tb);
            // the pixel floor at the centre's depth (a body behind the near plane keeps its law)
            if (c[2] > f.near_z) {
                const float floor_R = bm::body_floor_px * f.height * (1.f / 1440.f) * c[2] / px_x;
                if (lv.R < floor_R) lv.R = floor_R;
            }
            lv.T = boom_body_heat(tier, tb);
            lv.x = tb / L;
        }
        ++n.deaths;
        if (visible) {
            visible->add(e.owner);
            visible->add_in_view(e.owner);
        }
    }
    // (B) The cluster (2.6 and section 5): per live body, N = 1 + the other live bodies within cluster_r_x R of it,
    // each by its remaining life fraction (the burst's rule over the event frame's death bodies).
    if (t.cluster)
        for (unsigned a = 0; a < nl; ++a) {
            if (!(live[a].R > 0.f)) continue;
            float sum = 1.f;
            const float reach2 = bm::cluster_r_x * live[a].R * bm::cluster_r_x * live[a].R;
            for (unsigned b = 0; b < nl; ++b) {
                if (b == a || !(live[b].R > 0.f)) continue;
                const float dx = live[a].c[0] - live[b].c[0], dy = live[a].c[1] - live[b].c[1], dz = live[a].c[2] - live[b].c[2];
                if (dx * dx + dy * dy + dz * dz <= reach2) sum += 1.f - live[b].x;
            }
            live[a].cn = sum;
            if (sum > n.max_cluster) n.max_cluster = sum;
        }
    // (C) The elements.
    // the composed bodies' LOD by count (2026-11-07, the CPU pass): above compose_count_3 live bodies at most 3 sub-bursts
    // a body, above compose_count_2 at most 2 (jets first)
    unsigned live_bodies = 0;
    for (unsigned a = 0; a < nl; ++a) live_bodies += live[a].R > 0.f;
    const unsigned compose_cap = live_bodies > bm::compose_count_2 ? 2u : live_bodies > bm::compose_count_3 ? 3u : bm::compose_max;
    unsigned nq = 0, ng = 0, nb = 0, nm = 0, nd = 0, ns = 0, nh = 0, nw = 0;
    static BoxMeta body_meta[bm::max_bodies], smoke_meta[bm::max_bodies], shock_meta[renderer::weapon_shock_boxes];
    static BoxMeta wave_meta[renderer::weapon_wave_boxes];
    // the composed bodies' LOD state (section 13.2): drawn spheres, jets, the hashed N, the shared mode, the charge
    // before the factor
    struct Comp {
        std::uint8_t n, jets, full, shared, valid, jets_full;
        float base;
    };
    static Comp body_comp[bm::max_bodies];
    static ComposeSphere body_sp[bm::max_bodies][bm::compose_max];
    static std::uint8_t body_m[bm::max_bodies];
    static float body_seed[bm::max_bodies][4]; // the noise seed, the lobe seed, L, the body's age fraction
    // The composed body g's draw constants (renderer::WeaponComposeConstants) from its table: the first c.n spheres and
    // the first c.jets jets of what is born, each its centre and radius, its jet axis and squeeze, its erosion clock,
    // seeds and heat, its core.
    const auto write_compose = [&](unsigned g) {
        renderer::WeaponComposeConstants& k = out.compose[g];
        const Comp& c = body_comp[g];
        unsigned spheres = 0, jets = 0, w = 0;
        for (unsigned i = 0; i < body_m[g] && w < renderer::weapon_compose_spheres; ++i) {
            const ComposeSphere& s = body_sp[g][i];
            const bool jet = s.axis[0] != 0.f || s.axis[1] != 0.f || s.axis[2] != 0.f;
            if (jet ? jets >= c.jets : spheres >= c.n) continue;
            jet ? ++jets : ++spheres;
            float(&q)[4][4] = k.sphere[w++];
            for (unsigned j = 0; j < 3; ++j) {
                q[0][j] = s.c[j];
                q[1][j] = s.axis[j];
            }
            q[0][3] = s.R;
            q[1][3] = jet ? 1.f - bm::compose_jet_short / bm::compose_jet_long : 0.f;
            q[2][0] = s.xe;
            q[2][1] = body_seed[g][0] + bm::compose_seed_step * s.ti;
            q[2][2] = body_seed[g][1] + bm::compose_seed_step * s.ti;
            q[2][3] = boom_heat_law(s.xi * body_seed[g][2], body_seed[g][2]) * s.cool; // the heat on the age clock
            // the core on the body's clock (a late sub-burst carries no new white-hot core: a lamp in the frames), gone
            // at compose_core_x (2026-11-07: N cores to 0.35 L drew white balloons)
            const float xb = body_seed[g][3];
            q[3][0] = t.core && xb < bm::compose_core_x ? bm::core_gain * (1.f - xb / bm::compose_core_x) : 0.f;
            q[3][1] = q[3][2] = q[3][3] = 0.f;
        }
        k.head[0] = float(w);
        k.head[1] = c.shared ? 1.f : 0.f;
        k.head[2] = k.head[3] = 0.f;
    };
    static renderer::WeaponWaveConstants wave_const[renderer::weapon_wave_boxes];
    static float wave_scr[renderer::weapon_wave_boxes]; // each wave's drawn screens
    static_assert(bm::wave_quads == renderer::weapon_wave_quads, "the stage's and the pass's quads a wave");
    float newest_wave = -1.f; // the newest wave's age (its refraction request)
    x3m::engine_shimmer::WaveRequest wave_req{};
    float strip_area = 0.f;
    static BoxMeta halo_meta[renderer::weapon_halo_boxes];
    static BoxMeta glow_meta[bm::max_glows];
    static float body_fine[bm::max_bodies];
    // the big tiers' secondaries (bodies built here, shed at step 5, then placed after the bodies)
    unsigned nsec = 0;
    static renderer::WeaponFireballVertex sec_verts[bm::max_secondaries * 4];
    static BoxMeta sec_meta[bm::max_secondaries];
    static float sec_fine[bm::max_secondaries];
    static_assert(bm::tier_secondaries[bm::capital] <= 12 && bm::tier_secondaries[bm::station] <= 12, "births[12]");
    float star_area = 0.f;
    float star_colour[3];
    core::heat_colour(bm::star_t, star_colour);
    const bool simple = bm::mode == bm::simple;
    for (unsigned k = 0; k < nl; ++k) {
        const Live& lv = live[k];
        const core::Event& e = ring.slots[lv.slot];
        const BoomSlot& s = state.slots[lv.slot];
        const unsigned tier = lv.tier;
        const float L = bm::tier_life_ms[tier];
        const std::uint32_t seq = e.sequence;
        const std::uint32_t age_ms = std::uint32_t(lv.age > 0.f ? lv.age : 0.f);
        const float U = metres_to_view(t.cell_m[e.patch_tier < core::patch_tier_count ? e.patch_tier : 0u]);
        // The ember sets (2.4): the death point now and a frame ago (view), the rows' scale.
        float p_now[3], p_prev[3];
        to_view(f.cam, s.p0, p_now);
        to_view(prev, s.p0, p_prev);
        const float rows = f.height * (1.f / 1080.f);
        const unsigned every = unsigned(bm::fragment_every);
        // One set: count embers from hash index index0 (the fragment rule and the life thirds by the index), launched
        // from cen (world offset from the death point, view units) plus the launch offset within the unit ball (at most
        // ember_p0 Rfb, never past ember_hide_r R0b), or from the hull shape's surface along their direction (surface);
        // hidden while inside the body about cen (Rb now, xb its age share), speeds from Rfb, lives from Lset, at the
        // set's age tset (game ms). Closed form from the hashes; never shed.
        const auto ember_set = [&](unsigned index0, unsigned count, const float cen[3], bool surface, float Rb, float R0b,
                                   float Rfb, float xb, float Lset, float tset, bool own_switch) {
            // own_switch: the set is a secondary's (drawn under its switch, not the embers' and fragments')
            const bool want_embers = own_switch || t.embers, want_fragments = own_switch || t.fragments;
            if ((!want_embers && !want_fragments) || !(tset >= 0.f)) return;
            const float v_e = boom_ember_speed_m(Rfb / view_per_m) * view_per_m; // view units per game s
            const float t_now = tset * 1e-3f, t_prev = t_now > step_s ? t_now - step_s : 0.f;
            const bool first = !(t_now > t_prev);
            float hide = (xb - bm::ember_hide_x0) / (bm::ember_hide_x1 - bm::ember_hide_x0);
            hide = hide < 0.f ? 0.f : hide > 1.f ? 1.f : hide;
            const float hide_a = Rb > 0.f ? 1.f - hide : 0.f; // the body's hiding share (1 young, 0 once eroded)
            const float o_max = bm::ember_hide_r * R0b, o = bm::ember_p0 * Rfb < o_max ? bm::ember_p0 * Rfb : o_max;
            for (unsigned j = 0; j < count; ++j) {
                const unsigned m = index0 + j;
                const bool fragment = every && (m % every) == every - 1u;
                if (fragment ? !want_fragments : !want_embers) continue;
                const std::uint32_t h = element_hash(seq, bm::ember_element + m), h2 = mix32(h + 0x9e3779b9u);
                const float law = fragment ? bm::fragment_life * Lset : bm::ember_life[m % 3u] * Lset;
                const float life_law = law * (1.f + bm::ember_stagger * (byte_unit(h2, 0) - 0.5f));
                const bool fl = s.floor_ms > life_law;
                const float life = fl ? s.floor_ms : life_law;
                if (!(tset < life)) continue;
                float d[3], off[3], launch[3];
                boom_ember_dir(s.cone[(h >> 8) % (s.cones ? s.cones : 1u)], mix32(h ^ 0x5bd1e995u), d);
                const float speed = (fragment ? bm::fragment_v : 1.f) * v_e *
                                    (bm::ember_speed_lo + (bm::ember_speed_hi - bm::ember_speed_lo) * byte_unit(h2, 8));
                if (surface) {
                    boom_hull_exit(s, d, launch);
                } else {
                    cube_point(mix32(h2 ^ 0x27d4eb2fu), off);
                    const float r2 = dot3(off, off), k = r2 > 1.f ? rsqrt_nr(r2) : 1.f;
                    for (unsigned q = 0; q < 3; ++q) launch[q] = cen[q] + off[q] * k * o;
                }
                // hidden while inside the body: fully within ember_hide_r R(t), visible past R(t), the share fading with
                // the body's erosion
                float rel[3];
                for (unsigned q = 0; q < 3; ++q) rel[q] = launch[q] - cen[q] + d[q] * speed * t_now;
                const float r_e = scalar::sqrt(dot3(rel, rel));
                float inside = Rb > 0.f ? (Rb - r_e) / ((1.f - bm::ember_hide_r) * Rb) : 0.f;
                inside = inside < 0.f ? 0.f : inside > 1.f ? 1.f : inside;
                const float vis = 1.f - hide_a * inside;
                if (!(vis > 0.01f)) {
                    ++n.embers_hidden;
                    continue;
                }
                float dn[3], dp[3], ln[3], lp[3];
                rotate_world_to_view(f.cam, d, dn);
                rotate_world_to_view(prev, d, dp);
                rotate_world_to_view(f.cam, launch, ln);
                rotate_world_to_view(prev, launch, lp);
                float head[3], tail[3];
                for (unsigned q = 0; q < 3; ++q) {
                    head[q] = p_now[q] + ln[q] + dn[q] * speed * t_now;
                    tail[q] = p_prev[q] + lp[q] + dp[q] * speed * t_prev;
                }
                if (!(head[2] > f.near_z) || !(tail[2] > f.near_z)) continue;
                if (first)
                    for (unsigned q = 0; q < 3; ++q) tail[q] = head[q] - dn[q] * speed * 1e-3f;
                const float fade = 1.f - tset / life;
                if (nq >= sprite_cap) {
                    ++n.capped;
                    continue;
                }
                // the pixel mode: a hashed width in pixels (scaled with the rows), a hashed length of the cap 0.35 R_f
                // on screen (half for a fragment), never under two widths; the motion's direction only
                const float w_px = (fragment ? bm::fragment_w_px
                                             : bm::ember_w_px_lo + (bm::ember_w_px_hi - bm::ember_w_px_lo) * byte_unit(h2, 16)) *
                                   rows;
                const float cap_px = bm::ember_len_max * Rfb * px_x / head[2];
                float len_px =
                    (bm::ember_len_lo + (1.f - bm::ember_len_lo) * byte_unit(h2, 24)) * cap_px * (fragment ? 0.5f : 1.f);
                len_px = len_px > 2.f * w_px ? len_px : 2.f * w_px;
                if (!fragment) {
                    const float e_raw = bm::ember_e * core::exp_neg(-tset / (bm::ember_tau * Lset)) * fade * vis; // E x T
                    streak_vertices(head, tail, e_raw, e_raw, -w_px, heat_packed(fade), out.sprites + 4 * nq, len_px);
                    ++nq;
                    ++n.embers;
                } else {
                    // the hot tip and the dark box under it (surface-bound)
                    const float e_raw = 0.5f * bm::ember_e * fade * vis;
                    streak_vertices(head, tail, e_raw, e_raw, -w_px, heat_packed(bm::fragment_tip_t * fade),
                                    out.sprites + 4 * nq, len_px);
                    ++nq;
                    ++n.fragments;
                    const float Rd = bm::fragment_dark_r * U * (1.f / 3.f); // 1 m on a fighter (U 3 m)
                    float rect[4];
                    bool full = false;
                    if (nd < dark_cap && head[2] + Rd > f.near_z && sphere_rect(f, head, Rd, rect, &full)) {
                        const float dk = bm::fragment_dark * fade * vis;
                        const float colour[3] = {dk, dk, dk};
                        glow_quad(f, head, Rd, rect, full, colour, out.scorches + 4 * nd);
                        ++nd;
                        ++n.darks;
                    }
                }
                n.floored += fl;
            }
        };
        // The embers: the tier's count, the secondaries' share (secondary_embers each) launched with them, the rest at
        // the body's birth (big tiers: from the hull shape's surface).
        const unsigned ember_total = unsigned(bm::tier_embers[tier] * (simple ? 0.5f : 1.f) + 0.5f);
        const unsigned sec_count = simple || !t.secondaries || !s.hull_kind ? 0u : bm::tier_secondaries[tier];
        const unsigned sec_embers = sec_count * bm::secondary_embers;
        const unsigned main_embers = ember_total > sec_embers ? ember_total - sec_embers : 0u;
        // The pre-roll (capital and station, section 4): the dying hull's light from t = 0 to P, E rising hull_light_e0 ..
        // e1, on whatever the lane holds (the carrier's hull); charged and shed as another owner's glow.
        const float pre_ms = bm::tier_preroll_ms[tier];
        const float hull_end = pre_ms + float(bm::clone_fade_ms);
        if (s.hull_kind && pre_ms > 0.f && lv.age < hull_end && t.hull_light && ng < glow_cap && ng < bm::max_glows) {
            float hv[3], hc[3];
            rotate_world_to_view(f.cam, s.hull_c, hv);
            for (unsigned q = 0; q < 3; ++q) hc[q] = lv.c[q] + hv[q];
            const float Rh = bm::hull_light_r * float(e.radius) * core::world_scale;
            float rect[4];
            bool full = false;
            if (Rh > 0.f && hc[2] + Rh > f.near_z && sphere_rect(f, hc, Rh, rect, &full)) {
                float colour[3];
                core::heat_colour(bm::hull_light_t, colour);
                const float k = bm::glow_e * bm::body_e *
                                (lv.age < pre_ms ? bm::hull_light_e0 + (bm::hull_light_e1 - bm::hull_light_e0) * lv.age / pre_ms
                                                 : bm::hull_light_e1 * (1.f - (lv.age - pre_ms) / float(bm::clone_fade_ms)));
                for (unsigned q = 0; q < 3; ++q) colour[q] *= k;
                glow_quad(f, hc, Rh, rect, full, colour, out.glows + 4 * ng);
                glow_meta[ng] = BoxMeta{age_ms, e.owner, rect_screens(rect) * core::look::budget_weight_glow, lv.own, 0};
                ++ng;
                ++n.hull_lights;
            }
        }
        // The secondaries (2.5): births at the cadence, each a burst body at a hashed point inside the hull shape with
        // its star, its glow (the event's live secondaries saturate) and its embers; the two newest live ones of each
        // death kept from the shed (section 7 step 5).
        if (sec_count) {
            float births[12];
            boom_secondary_births(seq, tier, births);
            const float Ls = bm::secondary_life * L;
            const float star_life = bm::star_ms > s.floor_ms ? bm::star_ms : s.floor_ms;
            const float blife = Ls > s.floor_ms ? Ls : s.floor_ms;
            const float ember_end = (bm::fragment_life > bm::ember_life[2] ? bm::fragment_life : bm::ember_life[2]) * Ls *
                                    (1.f + 0.5f * bm::ember_stagger);
            float cn = 0.f;
            unsigned live_n = 0;
            for (unsigned k = 0; k < sec_count; ++k) {
                const float a = lv.age - births[k];
                if (a >= 0.f && a < Ls) {
                    cn += 1.f - a / Ls;
                    ++live_n;
                }
            }
            const float sg = cn > 1.f ? 1.f / (1.f + (cn - 1.f) * (1.f / bm::secondary_cluster_n0)) : 1.f;
            unsigned live_rank = 0;
            for (unsigned k = 0; k < sec_count; ++k) {
                const float a = lv.age - births[k];
                if (a < 0.f || !(a < (ember_end > blife ? ember_end : blife) + s.floor_ms)) continue;
                const bool live = a < Ls;
                // the two newest live ones kept from the shed through the pre-roll (the body's prelude); from P the body
                // is the substance and every secondary may go (2026-11-07: kept past P the close capital met 448 us)
                const bool keep = live && live_rank++ + 2u >= live_n && lv.age < pre_ms;
                const float Rs = bm::secondary_scale(tier, k) * lv.R_f;
                float pw[3], pv[3], cv[3];
                boom_hull_point(s, element_hash(seq, bm::secondary_element + 4u * k + 1u), pw);
                rotate_world_to_view(f.cam, pw, pv);
                for (unsigned q = 0; q < 3; ++q) cv[q] = lv.c[q] + pv[q];
                float R = 0.f, T = 0.f, x = 0.f;
                if (a < blife) {
                    R = boom_radius_law(Rs, 0.f, a, Ls, bm::secondary_rise);
                    if (cv[2] > f.near_z) {
                        const float floor_R = bm::body_floor_px * f.height * (1.f / 1440.f) * cv[2] / px_x;
                        if (R < floor_R) R = floor_R;
                    }
                    T = boom_heat_law(a < Ls ? a : Ls, Ls);
                    x = a < Ls ? a / Ls : 1.f;
                }
                if (a < float(f.step_ms > 0 ? f.step_ms : 1u)) {
                    ++n.secondaries_born;
                    for (unsigned q = 0; q < 3; ++q) n.born_at[q] = cv[q];
                }
                const std::uint32_t h_rot = element_hash(seq, bm::secondary_element + 4u * k + 2u);
                const float sseed = 64.f * unit_of(element_hash(seq, bm::secondary_element + 4u * k + 3u));
                const float ds = scalar::sqrt(dot3(cv, cv));
                if (R > 0.f && T > 0.f) {
                    // the body, biased body_bias R towards the eye as the death's
                    float cb[3];
                    const float ease = ds < 2.f * R ? ds / (2.f * R) : 1.f;
                    const float bb = ds > 1e-6f ? 1.f - bm::body_bias * R * ease / ds : 1.f;
                    for (unsigned q = 0; q < 3; ++q) cb[q] = cv[q] * bb;
                    const float Rsph = R * (cb[2] > R * bm::body_r_sphere && dot3(cb, cb) > R * R * bm::body_r_sphere * bm::body_r_sphere
                                                ? boom_sphere_factor(R * px_x / cb[2] * (1440.f / f.height))
                                                : bm::body_r_sphere);
                    float rect[4];
                    bool full = false;
                    if (cb[2] + Rsph > f.near_z && sphere_rect(f, cb, Rsph, rect, &full)) {
                        if (nsec < bm::max_secondaries) {
                            const float fine = full ? 1.f : boom_fine(R * px_x / cb[2], fine_px, fine_band_px, fine4_px, fine4_band_px);
                            const float look[4] = {T, bm::body_e, x, sseed};
                            const float tint4[4] = {0.f, 0.f, 0.f, 0.f};
                            const float disc[4] = {6.2831853f * unit_of(h_rot), 64.f * unit_of(mix32(h_rot)),
                                                   core::look::fireball_advect * x, fine};
                            const float g = t.core && x < bm::core_x ? bm::core_gain * (1.f - x / bm::core_x) : 0.f;
                            fireball_quad(f, cb, Rsph, rect, full, look, tint4, disc, sec_verts + 4 * nsec, g);
                            sec_meta[nsec] = BoxMeta{age_ms, e.owner, rect_screens(rect) * boom_fine_weight(fine),
                                                     std::uint8_t(keep || lv.own), std::uint8_t(fine > 0.f ? 1 : 0)};
                            sec_fine[nsec] = fine;
                            ++nsec;
                        } else
                            ++n.capped;
                    }
                    // its glow on the surfaces about it (the event's saturation)
                    if (ng < glow_cap && ng < bm::max_glows) {
                        const float Rg_law = bm::glow_r_x * R, Rg_max = bm::glow_r_max * Rs;
                        const float Rg = Rg_law < Rg_max ? Rg_law : Rg_max;
                        float grect[4];
                        bool gfull = false;
                        if (cv[2] + Rg > f.near_z && sphere_rect(f, cv, Rg, grect, &gfull)) {
                            float colour[3];
                            core::heat_colour(T, colour);
                            const float k = bm::glow_e * bm::body_e * T * sg;
                            for (unsigned q = 0; q < 3; ++q) colour[q] *= k;
                            glow_quad(f, cv, Rg, grect, gfull, colour, out.glows + 4 * ng);
                            glow_meta[ng] =
                                BoxMeta{age_ms, e.owner, rect_screens(grect) * core::look::budget_weight_glow, lv.own, 0};
                            ++ng;
                            ++n.glows;
                        }
                    }
                }
                // its star (A = star_arm R_s, capped on screen as the death's; with the stars' switch too)
                if (t.star && a < star_life && cv[2] > f.near_z) {
                    if (nq < sprite_cap) {
                        float r_star = Rs;
                        const float arm_px = bm::star_arm * Rs * px_x / cv[2], cap_px = bm::star_cap_h * f.height;
                        if (arm_px > cap_px) r_star *= cap_px / arm_px;
                        const float angle = 1.5707963f * unit_of(mix32(h_rot ^ 0x7f4a7c15u));
                        float screens = 0.f;
                        if (star_quad(f, sc, cv, r_star, bm::star_core, bm::star_arm, angle, star_colour,
                                      core::look::core_e * (1.f - a / star_life), out.sprites + 4 * nq, &screens)) {
                            ++nq;
                            ++n.stars;
                            star_area += screens * core::hull::budget_weight_flash;
                        }
                    } else
                        ++n.capped;
                }
                // its embers
                ember_set(main_embers + bm::secondary_embers * k, bm::secondary_embers, pw, false, R, 0.5f * Rs, Rs, x, Ls, a,
                          true);
            }
        }
        if (lv.tb < 0.f) continue; // the rest from the body's birth P
        const float sb = 1.f / (1.f + (lv.cn - 1.f) * (1.f / bm::cluster_n0));
        // the body's centre biased towards the eye by body_bias R, the bias eased in over the first 2 R of the eye's
        // distance (an eye inside the body keeps the centre where it is: continuous, never past the eye); the smoke's by
        // body_bias of its own radius
        const float dist = scalar::sqrt(dot3(lv.c, lv.c));
        const auto biased = [&](float R, float out[3]) {
            for (unsigned q = 0; q < 3; ++q) out[q] = lv.c[q];
            if (!(R > 0.f) || !(dist > 1e-6f)) return;
            const float ease = dist < 2.f * R ? dist / (2.f * R) : 1.f;
            const float b = 1.f - bm::body_bias * R * ease / dist;
            for (unsigned q = 0; q < 3; ++q) out[q] = lv.c[q] * b;
        };
        float cb[3];
        biased(lv.R, cb);
        const std::uint32_t h_rot = element_hash(seq, core::hull::burst_rot_element);
        const std::uint32_t h_lobe = element_hash(seq, core::hull::burst_lobe_element);
        const float seed = 64.f * unit_of(element_hash(seq, 4));
        // Body.
        if (t.body && lv.R > 0.f && body_cap) {
            const float factor = cb[2] > lv.R * bm::body_r_sphere && dot3(cb, cb) > lv.R * lv.R * bm::body_r_sphere * bm::body_r_sphere
                                     ? boom_sphere_factor(lv.R * px_x / cb[2] * (1440.f / f.height))
                                     : bm::body_r_sphere;
            const float Rs = lv.R * factor;
            // Section 13.2: the composed body (N sub-bursts in this quad): the hashed N and jets, N compose_n_min under
            // compose_small_px of R_f; the unit so the cluster's outline follows R(t); the box the cluster's bounding
            // sphere; the eye inside the cluster: the shared-noise mode, N at most compose_eye_n (jets first).
            Comp cp{0, 0, 0, 0, 0, 0, 0.f};
            float Rbox = Rs;
            ComposeSphere sp[bm::compose_max];
            unsigned m = 0;
            if (bm::compose_enabled && t.compose && !simple && out.compose && nb < bm::max_bodies) {
                const std::uint32_t hc = element_hash(seq, bm::compose_element);
                const unsigned span_n = bm::compose_n_hi[tier] - bm::compose_n_lo[tier] + 1u;
                const unsigned span_j = bm::compose_jets_hi[tier] - bm::compose_jets_lo[tier] + 1u;
                cp.full = std::uint8_t(bm::compose_n_lo[tier] + (hc >> 4) % span_n);
                cp.n = t.force_n ? std::uint8_t(t.force_n) : cp.full;
                cp.jets = std::uint8_t(t.force_jets != 0xffu ? t.force_jets : bm::compose_jets_lo[tier] + (hc >> 12) % span_j);
                cp.jets_full = cp.jets;
                const float rf_px = lv.c[2] > f.near_z ? lv.R_f * px_x / lv.c[2] * (1440.f / f.height) : 1e9f;
                if (!t.force_n && rf_px < bm::compose_small_px) {
                    cp.n = std::uint8_t(bm::compose_n_min);
                    cp.jets = 0;
                }
                while (!t.force_n && cp.n + cp.jets > compose_cap) {
                    if (cp.jets) --cp.jets;
                    else --cp.n;
                }
                cp.valid = boom_rows_valid(e) ? 1 : 0;
                const float r0c = lv.R_f > 0.f ? lv.R_0 / lv.R_f : 0.f;
                // the record's entries (hashed once); its placement at unit 1 (rotated offsets, rise, compensation,
                // clocks and their slopes) and the outline's estimate refreshed per compose_refresh_* (the CPU pass), each
                // frame advanced linearly; the unit so the estimate (a sphere's outline its drawn radius over the drawn
                // sphere's factor; no erosion term: each sub-burst carries its own) follows R(t); the table scales by it
                // about the biased centre
                const unsigned sph_full = cp.n > cp.full ? cp.n : cp.full, jets_full = cp.jets_full;
                ComposeCache* cc = nullptr;
                for (unsigned k = 0; k < bm::max_bodies; ++k) // the keys first (contiguous), the entry's counts then
                    if (state.compose_key[k] == seq && state.compose[k].spheres == sph_full &&
                        state.compose[k].jets == jets_full && state.compose[k].valid == cp.valid) {
                        cc = &state.compose[k];
                        break;
                    }
                if (!cc) {
                    const unsigned at = state.compose_next++ % bm::max_bodies;
                    cc = &state.compose[at];
                    *cc = ComposeCache{};
                    state.compose_key[at] = seq;
                    cc->sequence = seq;
                    cc->spheres = std::uint8_t(sph_full);
                    cc->jets = std::uint8_t(jets_full);
                    cc->valid = cp.valid;
                    cc->count = std::uint8_t(boom_compose_entries(seed, sph_full, jets_full, cp.full, cp.valid, cc->en));
                }
                const std::uint8_t set = std::uint8_t(cp.n | (cp.jets << 3));
                const float pxc[2] = {cb[2] > f.near_z ? px_x * cb[0] / cb[2] : 0.f,
                                      cb[2] > f.near_z ? 0.5f * f.height * f.cam.m11 * cb[1] / cb[2] : 0.f};
                const float mx = pxc[0] - cc->px[0], my = pxc[1] - cc->px[1], dxa = lv.x - cc->x_ref;
                if (cc->set != set || cc->frames + 1u >= bm::compose_refresh_frames || lv.x >= cc->next_birth ||
                    !(dxa < bm::compose_refresh_dx && dxa > -bm::compose_refresh_dx) ||
                    !(mx * mx + my * my <= bm::compose_refresh_px * bm::compose_refresh_px)) {
                    float rows[3][3], vr[3][3];
                    boom_rows(e, rows);
                    for (unsigned k = 0; k < 3; ++k) rotate_world_to_view(f.cam, rows[k], vr[k]);
                    cc->m = std::uint8_t(boom_compose_ref(cc->en, cc->count, cp.n, cp.jets, lv.x, r0c, bm::tier_rise[tier], vr,
                                                          cc->ref, &cc->next_birth));
                    cc->x_ref = lv.x;
                    cc->px[0] = pxc[0];
                    cc->px[1] = pxc[1];
                    cc->set = set;
                    cc->frames = 0;
                    ComposeSphere so[bm::compose_max];
                    const float zero[3] = {0.f, 0.f, 0.f};
                    boom_compose_advance(cc->ref, cc->m, lv.x, lv.x, zero, 1.f, so);
                    const float k = 1.f / factor;
                    for (unsigned i = 0; i < cc->m; ++i) { // the estimate: the radii without the compensation
                        const float c = cc->ref[i].comp > 1e-6f ? k / cc->ref[i].comp : k;
                        so[i].R *= c;
                        so[i].reach *= c;
                    }
                    cc->outline = cc->m ? boom_compose_outline(zero, so, cc->m) : 0.f;
                } else
                    ++cc->frames;
                m = cc->m;
                const float mo = cc->outline;
                const float unit = mo > 1e-4f ? bm::compose_size[tier] * lv.R / mo : Rs;
                boom_compose_advance(cc->ref, m, cc->x_ref, lv.x, cb, unit, sp);
                // the eye's sphere: the one holding it deepest (1 - |c|^2 / R^2 largest; its chords the longest)
                float bound = 0.f, eye_depth = 0.f;
                unsigned eye_at = m;
                for (unsigned i = 0; i < m; ++i) {
                    float o[3];
                    for (unsigned q = 0; q < 3; ++q) o[q] = sp[i].c[q] - cb[q];
                    const float ext = scalar::sqrt(dot3(o, o)) + sp[i].reach;
                    bound = ext > bound ? ext : bound;
                    const float R2 = sp[i].R * sp[i].R, depth = R2 > 0.f ? 1.f - dot3(sp[i].c, sp[i].c) / R2 : 0.f;
                    if (depth > eye_depth) {
                        eye_depth = depth;
                        eye_at = i;
                    }
                }
                Rbox = bm::compose_box_margin * (bound > 0.f ? bound : Rs) * t.box_scale;
                cp.shared = std::uint8_t(eye_at < m || t.force_shared);
                if (eye_at < m) { // the eye's sphere first (the shared noise is sampled there), N at most compose_eye_n
                    ++n.compose_eye;
                    const ComposeSphere first = sp[eye_at];
                    for (unsigned i = eye_at; i > 0; --i) sp[i] = sp[i - 1];
                    sp[0] = first;
                    // the second: the sphere that fills most of the view from the eye (R^2 / |c|^2 largest; no jet)
                    unsigned best = m;
                    float best_k = -1.f;
                    for (unsigned i = 1; i < m; ++i) {
                        if (sp[i].axis[0] != 0.f || sp[i].axis[1] != 0.f || sp[i].axis[2] != 0.f) continue;
                        const float d2 = dot3(sp[i].c, sp[i].c), k = sp[i].R * sp[i].R / (d2 > 1e-12f ? d2 : 1e-12f);
                        if (k > best_k) {
                            best_k = k;
                            best = i;
                        }
                    }
                    if (best < m && best > 1) {
                        const ComposeSphere second = sp[best];
                        for (unsigned i = best; i > 1; --i) sp[i] = sp[i - 1];
                        sp[1] = second;
                    }
                    while (cp.n + cp.jets > bm::compose_eye_n) {
                        if (cp.jets) --cp.jets;
                        else --cp.n;
                    }
                }
                if (!m) cp.n = 0;
            }
            float rect[4];
            bool full = false;
            if (cb[2] + Rbox > f.near_z && sphere_rect(f, cb, Rbox, rect, &full)) {
                if (nb < body_cap) {
                    // the eye inside (the box the whole screen): three octaves (2026-11-07 v4: the fourth put the eye-inside
                    // fighter at 309.8 us against the 300 us fence [M])
                    const float fine = full ? 1.f : boom_fine(lv.R * px_x / cb[2], fine_px, fine_band_px, fine4_px, fine4_band_px);
                    const float look[4] = {lv.T, bm::body_e * sb, lv.x, seed};
                    const float tint4[4] = {0.f, 0.f, 0.f, 0.f};
                    const float disc[4] = {6.2831853f * unit_of(h_rot), 64.f * unit_of(h_lobe),
                                           core::look::fireball_advect * lv.x, fine};
                    // the white-hot core (decision 2026-11-07): g = core_gain (1 - x / core_x) while x < core_x
                    const float g = t.core && lv.x < bm::core_x ? bm::core_gain * (1.f - lv.x / bm::core_x) : 0.f;
                    fireball_quad(f, cb, Rbox, rect, full, look, tint4, disc, out.fireballs + 4 * nb, g);
                    if (cp.n >= bm::compose_n_min) {
                        renderer::WeaponFireballVertex* v4 = out.fireballs + 4 * nb;
                        for (unsigned k = 0; k < 4; ++k) v4[k].disc[2] = core::look::fireball_advect; // x each clock
                        body_m[nb] = std::uint8_t(m);
                        for (unsigned i = 0; i < m; ++i) body_sp[nb][i] = sp[i];
                        body_seed[nb][0] = seed;
                        body_seed[nb][1] = disc[1];
                        body_seed[nb][2] = L;
                        body_seed[nb][3] = lv.x;
                        ++n.composed;
                        n.compose_shared += cp.shared;
                    }
                    cp.base = rect_screens(rect) * boom_fine_weight(fine);
                    body_meta[nb] = BoxMeta{age_ms, e.owner, cp.base * boom_compose_factor(cp.n + cp.jets, cp.shared), lv.own,
                                            std::uint8_t(fine > 0.f ? 1 : 0)};
                    body_comp[nb] = cp;
                    body_fine[nb] = fine;
                    if (cp.n >= bm::compose_n_min)
                        write_compose(nb);
                    else if (bm::compose_enabled && out.compose) { // the N 1 table (the composed draw takes every body box)
                        renderer::WeaponComposeConstants& k1 = out.compose[nb];
                        k1 = renderer::WeaponComposeConstants{};
                        k1.head[0] = 1.f;
                        float(&q)[4][4] = k1.sphere[0];
                        for (unsigned j = 0; j < 3; ++j) q[0][j] = cb[j];
                        q[0][3] = Rs;
                        q[2][0] = lv.x;
                        q[2][1] = seed;
                        q[2][2] = disc[1];
                        q[2][3] = lv.T;
                        q[3][0] = g;
                    }
                    if (!nb) n.body_R0 = lv.R;
                    ++nb;
                    ++n.bodies;
                    n.fine += fine < 1.f;
                } else
                    ++n.capped;
            }
        }
        // Star: born on the body's first frame, star_ms (three frames at least), its arm capped on screen.
        if (t.star && lv.c[2] > f.near_z) {
            const float life = bm::star_ms > s.floor_ms ? bm::star_ms : s.floor_ms;
            if (lv.tb < life) {
                if (nq < sprite_cap) {
                    float r_star = lv.R_f;
                    const float arm_px = bm::star_arm * lv.R_f * px_x / lv.c[2], cap_px = bm::star_cap_h * f.height;
                    if (arm_px > cap_px) r_star *= cap_px / arm_px;
                    const float angle = 1.5707963f * unit_of(element_hash(seq, core::hull::star_element));
                    float screens = 0.f;
                    if (star_quad(f, sc, lv.c, r_star, bm::star_core, bm::star_arm, angle, star_colour,
                                  core::look::core_e * (1.f - lv.tb / life), out.sprites + 4 * nq, &screens)) {
                        ++nq;
                        ++n.stars;
                        star_area += screens * core::hull::budget_weight_flash;
                        n.floored += life > bm::star_ms;
                    }
                } else
                    ++n.capped;
            }
        }
        // Shock: a shell of the tier's law, the limb only, the body's heat, gone at shock_end L; not drawn once the eye
        // is inside it.
        if (t.shock && !simple && bm::tier_shock[tier] > 0.f &&
            ((bm::shock_tiers & (1u << tier)) || (tier == bm::missile && bm::missile_shock) || t.force_shock) && shock_cap) {
            const float tau = bm::tier_shock_tau[tier] * L, end = bm::shock_end * L;
            const float life = end > s.floor_ms ? end : s.floor_ms;
            if (lv.tb < life) {
                const float u = lv.tb / tau < 1.f ? lv.tb / tau : 1.f;
                const float r = bm::shock_r_x * lv.R_f * scalar::sqrt(u > 1e-4f ? u : 1e-4f);
                const float a = (1.f - lv.tb / life) * (1.f - lv.tb / life);
                float rect[4];
                bool full = false;
                if (dist > r && lv.c[2] + r > f.near_z && sphere_rect(f, lv.c, r, rect, &full)) {
                    if (ns < shock_cap) {
                        float colour[4];
                        core::heat_colour(boom_body_heat(tier, lv.tb), colour);
                        colour[3] = 0.f;
                        const float look[4] = {bm::shock_w * r, bm::shock_e * a, bm::shock_limb, 0.f};
                        const float disc[4] = {0.f, 0.f, 0.f, 0.f};
                        fireball_quad(f, lv.c, r, rect, full, look, colour, disc, out.shocks + 4 * ns);
                        shock_meta[ns] = BoxMeta{age_ms, e.owner, rect_screens(rect) * bm::budget_weight_shock, lv.own, 0};
                        ++ns;
                        ++n.shocks;
                    } else
                        ++n.capped;
                }
            }
        }
        // Section 13.1, the fire wave (capital, station): the slab's box from the body's birth to L_w, the plane of the
        // rows 2 and 1 (the view plane without rows); the newest wave's refraction request through the shimmer pass.
        if (t.wave && !simple && (bm::wave_tiers & (1u << tier)) && wave_cap) {
            const float Lw = bm::wave_life_x * L, life = Lw > s.floor_ms ? Lw : s.floor_ms;
            if (lv.tb < life) {
                const float r0w = s.hull_kind ? s.hull_e[2] : 0.5f * bm::secondary_capsule_len * float(e.radius) * core::world_scale;
                const float r = boom_wave_radius(r0w, bm::wave_r_x * lv.R_f, bm::wave_tau_x * L, lv.tb);
                const float we = bm::wave_w_x * lv.R_f, hh = 0.5f * bm::wave_slab_x * lv.R_f;
                const float fade = (1.f - lv.tb / life) * (1.f - lv.tb / life);
                const float Tw = 1.f - bm::wave_t_cool * lv.tb / life; // the age's cooling factor on every T
                float nv[3], av[3];
                const bool rows_ok = boom_rows_valid(e);
                if (rows_ok) {
                    float rows[3][3];
                    boom_rows(e, rows);
                    rotate_world_to_view(f.cam, rows[0], nv);
                    rotate_world_to_view(f.cam, rows[2], av);
                } else { // the view plane: the normal to the eye, the long axis the view's x across it
                    const float k = dist > 1e-6f ? -1.f / dist : 0.f;
                    for (unsigned q = 0; q < 3; ++q) nv[q] = lv.c[q] * k;
                    if (!(dist > 1e-6f)) nv[2] = -1.f;
                    const float xn = nv[0];
                    for (unsigned q = 0; q < 3; ++q) av[q] = (q == 0 ? 1.f : 0.f) - xn * nv[q];
                    const float al = dot3(av, av);
                    const float ka = al > 1e-8f ? rsqrt_nr(al) : 0.f;
                    for (unsigned q = 0; q < 3; ++q) av[q] *= ka;
                    if (!(al > 1e-8f)) av[1] = 1.f;
                    ++n.wave_view_plane;
                }
                const float bv[3] = {nv[1] * av[2] - nv[2] * av[1], nv[2] * av[0] - nv[0] * av[2], nv[0] * av[1] - nv[1] * av[0]};
                // the eye near the plane: the wave x smoothstep(0, wave_eye_fade_x R_f, the eye's in-plane distance from
                // the band), weighted in within wave_eye_plane_x R_f of the plane (2026-11-07 v3: no white wall)
                float eye_fade = 1.f;
                {
                    const float en = dot3(lv.c, nv), ea = dot3(lv.c, av) / bm::wave_stretch, eb = dot3(lv.c, bv);
                    const float ad = en < 0.f ? -en : en, de = scalar::sqrt(ea * ea + eb * eb);
                    const float near_w = (bm::wave_eye_plane_x * lv.R_f - ad) / (bm::wave_eye_plane_blend_x * lv.R_f);
                    if (near_w > 0.f) {
                        const float band = bm::wave_band_x * lv.R_f;
                        const float out_d = de > r ? de - r : de < r - band ? r - band - de : 0.f;
                        float u = out_d / (bm::wave_eye_fade_x * lv.R_f);
                        u = u < 1.f ? u : 1.f;
                        const float sm = u * u * (3.f - 2.f * u), w = near_w < 1.f ? near_w : 1.f;
                        eye_fade = 1.f - w * (1.f - sm);
                    }
                }
                // the box: the slab's eight corners; any at or behind the near plane: the whole screen
                const float ro = r + bm::wave_window_out * we, ri = r - bm::wave_window_in * we;
                float rect[4] = {1.f, 1.f, -1.f, -1.f}, zq = 1e30f;
                bool full = false;
                for (unsigned k = 0; k < 8 && !full; ++k) {
                    const float sa = (k & 1u ? 1.f : -1.f) * bm::wave_stretch * ro, sb = (k & 2u ? 1.f : -1.f) * ro,
                                sn = (k & 4u ? 1.f : -1.f) * hh;
                    float p[3];
                    for (unsigned q = 0; q < 3; ++q) p[q] = lv.c[q] + sa * av[q] + sb * bv[q] + sn * nv[q];
                    if (!(p[2] > f.near_z)) {
                        full = true;
                        break;
                    }
                    const float x = f.cam.m00 * p[0] / p[2] + f.cam.m20, y = f.cam.m11 * p[1] / p[2] + f.cam.m21;
                    rect[0] = x < rect[0] ? x : rect[0];
                    rect[2] = x > rect[2] ? x : rect[2];
                    rect[1] = y < rect[1] ? y : rect[1];
                    rect[3] = y > rect[3] ? y : rect[3];
                    zq = p[2] < zq ? p[2] : zq;
                }
                if (full) {
                    rect[0] = rect[1] = -1.f;
                    rect[2] = rect[3] = 1.f;
                    zq = 1.5f * f.near_z;
                } else {
                    rect[0] = rect[0] > -1.f ? rect[0] : -1.f;
                    rect[1] = rect[1] > -1.f ? rect[1] : -1.f;
                    rect[2] = rect[2] < 1.f ? rect[2] : 1.f;
                    rect[3] = rect[3] < 1.f ? rect[3] : 1.f;
                }
                // the drawn quads: the annulus's strips while their screens sum under the box's, else the box; none when no
                // strip reaches the target (the wave wholly off screen or behind the near plane: nothing of it is seen)
                WaveQuad strips[bm::wave_quads];
                float sscr[bm::wave_quads];
                const bool on_screen = rect[2] > rect[0] && rect[3] > rect[1];
                const unsigned ns_made = on_screen && nw < wave_cap
                                             ? boom_wave_strips(f, lv.c, nv, av, bv, bm::wave_stretch, ri, ro, hh, strips, sscr)
                                             : 0u;
                if (on_screen && nw >= wave_cap)
                    ++n.capped;
                else if (ns_made) {
                    {
                        float strip_sum = 0.f;
                        for (unsigned k = 0; k < ns_made; ++k) strip_sum += sscr[k];
                        const float box = rect_screens(rect);
                        const bool use_strips = ns_made && strip_sum < box;
                        unsigned quads = 1;
                        if (use_strips) {
                            quads = ns_made;
                        } else {
                            WaveQuad& b = strips[0];
                            for (unsigned k = 0; k < 4; ++k) {
                                b.ndc[k][0] = rect[k & 1u ? 2 : 0];
                                b.ndc[k][1] = rect[k & 2u ? 3 : 1];
                            }
                            b.zq = zq;
                            b.sector = -1.f;
                        }
                        renderer::WeaponFireballVertex* v4 = out.waves + 4 * bm::wave_quads * nw;
                        const float s1 = 64.f * unit_of(element_hash(seq, bm::wave_seed_element));
                        const float s2 = 64.f * unit_of(element_hash(seq, bm::wave_seed_element + 1u));
                        for (unsigned qd = 0; qd < quads; ++qd) {
                            const WaveQuad& wq = strips[qd];
                            const float kx = wq.zq / f.cam.m00, ky = wq.zq / f.cam.m11;
                            for (unsigned k = 0; k < 4; ++k) {
                                renderer::WeaponFireballVertex& v = v4[4 * qd + k];
                                v.corner[0] = (wq.ndc[k][0] - f.cam.m20) * kx;
                                v.corner[1] = (wq.ndc[k][1] - f.cam.m21) * ky;
                                v.corner[2] = wq.zq;
                                for (unsigned q = 0; q < 3; ++q) {
                                    v.sphere[q] = lv.c[q];
                                    v.tint[q] = av[q];
                                }
                                v.sphere[3] = r;
                                v.look[0] = we;
                                v.look[1] = hh;
                                v.look[2] = Tw;
                                v.look[3] = bm::wave_e * fade * eye_fade;
                                v.tint[3] = 1.f / bm::wave_stretch;
                                v.disc[0] = 0.f;
                                v.disc[1] = s1;
                                v.disc[2] = s2;
                                v.disc[3] = wq.sector;
                                v.core = 0.f;
                            }
                        }
                        renderer::WeaponWaveConstants& wc = wave_const[nw];
                        for (unsigned q = 0; q < 3; ++q) wc.normal[q] = nv[q];
                        wc.heat[0] = Tw;
                        // the sheet noise's frequency: 1 / (wave_sheet_cell R_f (r / R_f)^wave_sheet_advect)
                        {
                            const float q = r / lv.R_f > 1e-3f ? r / lv.R_f : 1e-3f;
                            const float lq = core::log10f_sse(q) * 2.30258509f * bm::wave_sheet_advect;
                            const float grow = lq >= 0.f ? 1.f / core::exp_neg(-lq) : core::exp_neg(lq);
                            wc.normal[3] = 1.f / (bm::wave_sheet_cell * lv.R_f * grow);
                        }
                        wc.heat[1] = t.wave_octave2 ? bm::wave_sheet_n_w[1] : 0.f; // the sheet's second octave (0: shed)
                        wc.heat[2] = bm::wave_hole_cells;
                        wc.heat[3] = 1.f / bm::wave_hole_stretch;
                        for (unsigned q = 0; q < 4; ++q) wc.spare[q] = 0.f;
                        wc.quads = quads;
                        // the charge: the strips' screens x budget_weight_wave; the box: the annulus's projected screens x
                        // budget_weight_wave, the rest of it x _miss
                        float charge = 0.f;
                        if (use_strips) {
                            charge = strip_sum * (t.wave_octave2 ? bm::budget_weight_wave : bm::budget_weight_wave_one);
                        } else {
                            float ann = 0.f;
                            const float eye_n = -dot3(lv.c, nv), eye_a = -dot3(lv.c, av) / bm::wave_stretch, eye_b = -dot3(lv.c, bv);
                            const float d_eye = scalar::sqrt(eye_a * eye_a + eye_b * eye_b);
                            if (d_eye < r && (eye_n < 0.f ? -eye_n : eye_n) < 3.f * hh) { // the eye in the ring's plane: the wall
                                const float bar = 0.5f * f.cam.m11 * (2.f * hh + bm::wave_band_x * lv.R_f) / (r - d_eye > we ? r - d_eye : we);
                                ann = bar < 1.f ? bar : 1.f;
                            } else {
                                const float cz = dist > f.near_z ? dist : f.near_z, cosv = (eye_n < 0.f ? -eye_n : eye_n) / (dist > 1e-6f ? dist : 1.f);
                                const float rin = ri > 0.f ? ri : 0.f;
                                const float area_w = 3.14159265f * bm::wave_stretch * (ro * ro - rin * rin) * cosv +
                                                     4.f * bm::wave_stretch * ro * hh * scalar::sqrt(1.f - (cosv < 1.f ? cosv * cosv : 1.f));
                                ann = 0.25f * area_w * f.cam.m00 * f.cam.m11 / (cz * cz);
                            }
                            ann = ann < box ? ann : box;
                            charge = ann * bm::budget_weight_wave + (box - ann) * bm::budget_weight_wave_miss;
                        }
                        wave_scr[nw] = use_strips ? strip_sum : box;
                        wave_meta[nw] = BoxMeta{age_ms, e.owner, charge, lv.own, 0};
                        ++nw;
                        ++n.waves;
                        if (t.wave_shimmer && out.wave_shimmer && (newest_wave < 0.f || lv.tb < newest_wave)) {
                            newest_wave = lv.tb;
                            for (unsigned q = 0; q < 3; ++q) {
                                wave_req.centre[q] = lv.c[q];
                                wave_req.normal[q] = nv[q];
                                wave_req.axis[q] = av[q];
                            }
                            wave_req.r = r;
                            wave_req.band = bm::wave_refract_band * we;
                            wave_req.stretch = bm::wave_stretch;
                            wave_req.px = bm::wave_refract_px * f.height * (1.f / 1440.f) * fade;
                        }
                    }
                }
            }
        }
        // Glow: the body's light on whatever surface the lane holds (none over sky), while the body draws: R =
        // min(glow_r_x R(t), glow_r_max R_f) (the burst's law); and the same light in air about the body (the halo), not
        // drawn with the eye inside the body.
        float light_colour[3] = {0.f, 0.f, 0.f};
        if (lv.R > 0.f) {
            core::heat_colour(lv.T, light_colour);
            const float e0 = bm::glow_e * bm::body_e * lv.T * sb;
            for (unsigned q = 0; q < 3; ++q) light_colour[q] *= e0;
        }
        if (t.glow && lv.R > 0.f && ng < glow_cap) {
            const float Rg_law = bm::glow_r_x * lv.R, Rg_max = bm::glow_r_max * lv.R_f;
            const float Rg = Rg_law < Rg_max ? Rg_law : Rg_max;
            float rect[4];
            bool full = false;
            if (lv.c[2] + Rg > f.near_z && sphere_rect(f, lv.c, Rg, rect, &full) && ng < bm::max_glows) {
                glow_quad(f, lv.c, Rg, rect, full, light_colour, out.glows + 4 * ng);
                glow_meta[ng] = BoxMeta{age_ms, e.owner, rect_screens(rect) * core::look::budget_weight_glow, lv.own, 0};
                ++ng;
                ++n.glows;
            }
        }
        // the halo: the burst's law; corvettes and up (D15) at halo_big_r R(t), at most halo_big_e E_b, faded out
        // over halo_big_x0 .. x1 of L
        const bool big_halo = tier == bm::corvette || tier == bm::capital || tier == bm::station;
        const float hx0 = big_halo ? bm::halo_big_x0 : bm::halo_fade_x0, hx1 = big_halo ? bm::halo_big_x1 : bm::halo_fade_x1;
        float halo_fade = (lv.x - hx0) / (hx1 - hx0);
        halo_fade = halo_fade < 0.f ? 1.f : halo_fade > 1.f ? 0.f : 1.f - halo_fade * halo_fade * (3.f - 2.f * halo_fade);
        if (big_halo && lv.R > 0.f) { // the radiance cap: E_g = glow_e E_b T sb above halo_big_e E_b scaled down to it
            const float e_g = bm::glow_e * bm::body_e * lv.T * sb, cap = bm::halo_big_e * bm::body_e;
            if (e_g > cap) halo_fade *= cap / e_g;
        }
        if (t.halo && lv.R > 0.f && halo_cap && dist > lv.R * bm::body_r_sphere && halo_fade > 0.f) {
            const float Rh = (big_halo ? bm::halo_big_r : bm::glow_r_x) * lv.R;
            const float halo_colour[3] = {light_colour[0] * halo_fade, light_colour[1] * halo_fade,
                                          light_colour[2] * halo_fade};
            float rect[4];
            bool full = false;
            if (lv.c[2] + Rh > f.near_z && sphere_rect(f, lv.c, Rh, rect, &full)) {
                if (nh < halo_cap) {
                    glow_quad(f, lv.c, Rh, rect, full, halo_colour, out.halos + 4 * nh);
                    halo_meta[nh] = BoxMeta{age_ms, e.owner, rect_screens(rect) * bm::budget_weight_halo, lv.own, 0};
                    ++nh;
                    ++n.halos;
                } else
                    ++n.capped;
            }
        }
        // Smoke: the body's impostor in the multiply pass (surface-bound), smoke_t0 L .. (smoke_t0 + smoke_span) L.
        if (t.smoke && !simple && smoke_cap) {
            const float y = (lv.tb - bm::smoke_t0 * L) / (bm::smoke_span * L);
            if (y > 0.f && y < 1.f) {
                const float sv = bm::smoke_s0 * (y < bm::smoke_rise ? y / bm::smoke_rise : 1.f) * (1.f - y) *
                                 (1.f / (1.f - bm::smoke_rise));
                const float Rm = bm::smoke_r * lv.R_f, Rs = Rm * (1.f + core::look::fireball_lobe_amp);
                float cm[3];
                biased(Rm, cm);
                float rect[4];
                bool full = false;
                if (cm[2] + Rs > f.near_z && sphere_rect(f, cm, Rs, rect, &full)) {
                    if (nm < smoke_cap) {
                        const float look[4] = {0.f, 0.f, y, seed};
                        const float tint4[4] = {sv * core::hull::scorch_colour[0], sv * core::hull::scorch_colour[1],
                                                sv * core::hull::scorch_colour[2], -1.f};
                        const float disc[4] = {6.2831853f * unit_of(h_rot), 64.f * unit_of(h_lobe),
                                               core::look::fireball_advect * (lv.tb / L), 1.f};
                        fireball_quad(f, cm, Rs, rect, full, look, tint4, disc, out.smokes + 4 * nm);
                        smoke_meta[nm] = BoxMeta{age_ms, e.owner, rect_screens(rect) * core::look::budget_weight_smoke,
                                                 lv.own, 0};
                        ++nm;
                        ++n.smokes;
                    } else
                        ++n.capped;
                }
            }
        }
        // The body's embers (the big tiers' from the hull shape's surface).
        {
            const float zero[3] = {0.f, 0.f, 0.f};
            ember_set(0u, main_embers, zero, s.hull_kind != 0, lv.R, lv.R_0, lv.R_f, lv.x, L, lv.tb, false);
        }
    }
    // (D) The budget (section 7; explosion-look.md D42): past budget, (0) the wave's refraction strips, (0a) the waves'
    // sheet's second octave, (0b) the fourth octave, (1) the third octave (hysteresis), (1b, 1c) the composed bodies' LOD;
    // then with a wave in the frame (2) other owners' glows and halos, (3) the secondaries past the two newest live ones of
    // each death, (4) smoke, (5) shocks, (6) the waves; without one (2) smoke, (3) shocks, (4) glows and halos, (5) the
    // secondaries; largest first each; the bodies, stars, embers and fragments stay.
    // the family's cap below the shared remainder (bm::budget_screens: the close capital's 450 us fence)
    const float budget = f.budget_left < t.budget_screens ? f.budget_left : t.budget_screens;
    float area = star_area, body_area = 0.f;
    for (unsigned g = 0; g < nb; ++g) body_area += body_meta[g].area;
    for (unsigned g = 0; g < nsec; ++g) area += sec_meta[g].area;
    for (unsigned g = 0; g < nm; ++g) area += smoke_meta[g].area;
    for (unsigned g = 0; g < ns; ++g) area += shock_meta[g].area;
    for (unsigned g = 0; g < nw; ++g) area += wave_meta[g].area;
    // the newest wave's refraction strips (charged x the shimmer's weight, shed first: step 0)
    if (newest_wave >= 0.f) {
        x3m::engine_shimmer::Projection pj{};
        pj.m00 = f.cam.m00;
        pj.m11 = f.cam.m11;
        pj.m20 = f.cam.m20;
        pj.m21 = f.cam.m21;
        pj.width = f.width;
        pj.height = f.height;
        x3m::engine_shimmer::Rect strips[x3m::engine_shimmer::wave_strip_count];
        float ss = 0.f;
        n.wave_strips = x3m::engine_shimmer::wave_strips(wave_req, pj, f.near_z, strips, &ss);
        n.wave_strip_screens = ss;
        // no room left in the shimmer pass for the eight strips (its rects after the engines' and the hull's, the glue's
        // last count): dropped, logged, not charged and no request (review 2026-11-07)
        if (n.wave_strips && out.wave_strip_room < x3m::engine_shimmer::wave_strip_count) {
            n.wave_strips = 0;
            ++n.wave_strips_dropped;
        } else if (n.wave_strips) {
            strip_area = ss * core::hull::budget_weight_shimmer;
            area += strip_area;
        } else if (!n.wave_strips_dropped)
            ++n.wave_strips_capped;
    }
    for (unsigned g = 0; g < ng; ++g) area += glow_meta[g].area;
    for (unsigned g = 0; g < nh; ++g) area += halo_meta[g].area;
    area += body_area;
    // (0) the wave's refraction strips past the budget
    if (strip_area > 0.f && area > budget) {
        area -= strip_area;
        strip_area = 0.f;
        n.wave_strips = 0;
    }
    // (0a) the waves' sheet loses its second octave (the charge x budget_weight_wave_one / budget_weight_wave), (0b) the
    // bodies their fourth octave (the frame's octaves 3), before the third (2026-11-07)
    for (unsigned g = 0; g < nw && area > budget; ++g)
        if (wave_const[g].heat[1] > 0.f) {
            const float now = wave_meta[g].area * (bm::budget_weight_wave_one / bm::budget_weight_wave);
            area -= wave_meta[g].area - now;
            wave_meta[g].area = now;
            wave_const[g].heat[1] = 0.f;
            ++n.wave_octaves_shed;
        }
    if (area > budget && n.octaves > 3u) {
        const auto drop4 = [&](BoxMeta& mt, float& fine, bool body) {
            if (!(fine > 1.f)) return;
            const float now = mt.area * boom_fine_weight(1.f) / boom_fine_weight(fine);
            area -= mt.area - now;
            if (body) body_area -= mt.area - now;
            mt.area = now;
            fine = 1.f;
        };
        for (unsigned g = 0; g < nb; ++g) drop4(body_meta[g], body_fine[g], true);
        for (unsigned g = 0; g < nsec; ++g) drop4(sec_meta[g], sec_fine[g], false);
        n.octaves = 3;
    }
    if (state.octaves_coarse) {
        if (state.coarse_frames >= core::look::fireball_octave_hold && area < core::look::fireball_octave_return * budget)
            state.octaves_coarse = false;
        else if (state.coarse_frames < 255)
            ++state.coarse_frames;
    } else if (area > budget && (nb || nsec)) {
        state.octaves_coarse = true;
        state.coarse_frames = 1;
    }
    if (state.octaves_coarse) {
        constexpr float w2 = core::look::budget_weight_fireball_coarse;
        for (unsigned g = 0; g < nb; ++g)
            if (body_meta[g].rank) {
                const float we = boom_fine_weight(body_fine[g]);
                const float cut = body_meta[g].area * (we - w2) / we;
                area -= cut;
                body_area -= cut;
                body_meta[g].area -= cut;
            }
        for (unsigned g = 0; g < nsec; ++g)
            if (sec_meta[g].rank) {
                const float we = boom_fine_weight(sec_fine[g]);
                const float cut = sec_meta[g].area * (we - w2) / we;
                area -= cut;
                sec_meta[g].area -= cut;
            }
        n.octaves = 2;
    }
    // (1b) the composed bodies' shared-noise mode, (1c) their spheres down one at a time (the largest body first, its last
    // sphere, then its jets), never under compose_n_min; while the third octave's step holds and the budget is passed
    if (state.octaves_coarse && area > budget) {
        const auto recharge = [&](unsigned g) {
            const Comp& c = body_comp[g];
            const float now = c.base * boom_compose_factor(c.n + c.jets, c.shared);
            const float we = boom_fine_weight(body_fine[g]);
            // the octave cut already applied leaves the coarse share of the charge
            const float coarse = body_meta[g].rank ? core::look::budget_weight_fireball_coarse / we : 1.f;
            area += now * coarse - body_meta[g].area;
            body_area += now * coarse - body_meta[g].area;
            body_meta[g].area = now * coarse;
            write_compose(g);
        };
        for (unsigned g = 0; g < nb; ++g)
            if (body_comp[g].n >= bm::compose_n_min && !body_comp[g].shared) {
                body_comp[g].shared = 1;
                ++n.compose_shared;
                recharge(g);
            }
        while (area > budget) {
            unsigned big = nb;
            for (unsigned g = 0; g < nb; ++g)
                if (body_comp[g].n >= bm::compose_n_min && body_comp[g].n + body_comp[g].jets > bm::compose_n_min &&
                    (big == nb || body_meta[g].area > body_meta[big].area))
                    big = g;
            if (big == nb) break;
            Comp& c = body_comp[big];
            if (c.n > bm::compose_n_min) --c.n;
            else --c.jets;
            ++n.compose_dropped;
            recharge(big);
        }
    }
    // With a wave in the frame (2026-11-07 v3: the wave is the feature): (2) other owners' glows and halos, (3) the
    // secondaries past the two newest live ones, (4) the smoke, (5) the shocks, (6) the waves last. Without one the
    // previous order: (2) the smoke, (3) the shocks, (4) glows and halos, (5) the secondaries.
    if (nw) {
        n.glows_shed = shed_largest(out.glows, &ng, glow_meta, &area, budget);
        n.glows_shed += shed_largest(out.halos, &nh, halo_meta, &area, budget);
        n.secondaries_shed = shed_largest(sec_verts, &nsec, sec_meta, &area, budget);
    }
    n.smokes_shed = shed_largest(out.smokes, &nm, smoke_meta, &area, budget);
    n.shocks_shed = shed_largest(out.shocks, &ns, shock_meta, &area, budget);
    {   // the waves with their constants (the shed moves the last box into the freed place: the constants follow)
        unsigned before = nw;
        BoxMeta tmp[renderer::weapon_wave_boxes];
        for (unsigned g = 0; g < nw; ++g) tmp[g] = wave_meta[g];
        n.waves_shed = shed_largest(out.waves, &nw, wave_meta, &area, budget, 4u * bm::wave_quads);
        // the survivors keep their own constants: match by the box's age and owner
        renderer::WeaponWaveConstants kept[renderer::weapon_wave_boxes];
        float kept_scr[renderer::weapon_wave_boxes];
        for (unsigned g = 0; g < nw; ++g)
            for (unsigned h = 0; h < before; ++h)
                if (tmp[h].age == wave_meta[g].age && tmp[h].owner == wave_meta[g].owner) {
                    kept[g] = wave_const[h];
                    kept_scr[g] = wave_scr[h];
                    break;
                }
        for (unsigned g = 0; g < nw; ++g) {
            wave_const[g] = kept[g];
            wave_scr[g] = kept_scr[g];
            n.wave_quads += kept[g].quads;
            n.wave_screens += kept_scr[g];
        }
    }
    if (newest_wave >= 0.f && out.wave_shimmer && n.wave_strips) { // the request only while a wave is drawn
        bool drawn = false;
        for (unsigned g = 0; g < nw; ++g) drawn = drawn || out.waves[4 * bm::wave_quads * g].sphere[3] == wave_req.r;
        if (drawn) {
            *out.wave_shimmer = wave_req;
            out.wave_shimmer_count = 1;
        } else
            n.wave_strips = 0;
    }
    if (!nw) {
        n.glows_shed = shed_largest(out.glows, &ng, glow_meta, &area, budget);
        n.glows_shed += shed_largest(out.halos, &nh, halo_meta, &area, budget);
        n.secondaries_shed = shed_largest(sec_verts, &nsec, sec_meta, &area, budget);
    }
    n.shed = n.smokes_shed + n.shocks_shed + n.waves_shed + n.glows_shed + n.secondaries_shed;
    // the secondaries after the bodies in the fireball buffer
    {
        const unsigned room = out.fireballs && out.fireball_cap > nb ? out.fireball_cap - nb : 0u;
        const unsigned placed = nsec < room ? nsec : room;
        for (unsigned g = 0; g < 4 * placed; ++g) out.fireballs[4 * nb + g] = sec_verts[g];
        n.capped += nsec - placed;
        n.secondaries = placed;
        nsec = placed;
    }
    n.smokes = nm;
    n.shocks = ns;
    n.glows = ng;
    n.screens = area;
    n.body_screens = body_area;
    out.sprite_quads = nq;
    out.glow_boxes = ng;
    out.fireball_boxes = nb + nsec;
    out.smoke_boxes = nm;
    out.scorch_boxes = nd;
    out.shock_boxes = ns;
    out.halo_boxes = nh;
    out.wave_boxes = nw;
    out.wave_constants = wave_const;
    out.composed_boxes = n.composed ? nb : 0u;
    n.halos = nh;
    n.waves = nw;
    return n;
}
} // namespace x3m::weapon_effects::stage
