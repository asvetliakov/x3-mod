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
struct BoomState {
    BoomSlot slots[core::ring_capacity];
    bool octaves_coarse = false; // the budget's step (1) with its hysteresis
    std::uint8_t coarse_frames = 0;
};
inline void boom_clear(BoomState& s) noexcept {
    for (auto& v : s.slots) v = BoomSlot{};
    s.octaves_coarse = false;
    s.coarse_frames = 0;
}
// The element switches (the fixture isolates one part; the header's look by default).
struct BoomTuning {
    bool body = true, star = true, shock = true, embers = true, fragments = true, glow = true, smoke = true;
    bool halo = true, core = true; // the body's halo in air and its white-hot core (the fixture's geometry gates: core off)
    bool secondaries = true, hull_light = true; // the big tiers' pre-roll: the secondaries (body, star, glow, embers) and the
                                                // dying hull's light
    bool force_shock = false; // the fixture's shell gates and budget row: the missile's shell drawn though missile_shock is off
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
// Sheds the largest boxes of other owners while the charged screens exceed the budget (the last box moves into the
// freed place). Returns the boxes shed.
template <class V>
inline unsigned shed_largest(V* verts, unsigned* count, BoxMeta* meta, float* total, float budget) noexcept {
    unsigned shed = 0;
    while (*total > budget && *count) {
        unsigned big = *count;
        for (unsigned g = 0; g < *count; ++g)
            if (!meta[g].own && (big == *count || meta[g].area > meta[big].area)) big = g;
        if (big == *count) break;
        *total -= meta[big].area;
        const unsigned last = --*count;
        if (big != last) {
            for (unsigned c = 0; c < 4; ++c) verts[4 * big + c] = verts[4 * last + c];
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
    out.halo_boxes = 0;
    auto cap_of = [](const void* p, unsigned cap, unsigned most) { return p ? (cap < most ? cap : most) : 0u; };
    const unsigned sprite_cap = cap_of(out.sprites, out.sprite_cap, bm::max_quads);
    const unsigned glow_cap = cap_of(out.glows, out.glow_cap, renderer::weapon_glow_boxes);
    const unsigned body_cap = cap_of(out.fireballs, out.fireball_cap, bm::max_bodies);
    const unsigned smoke_cap = cap_of(out.smokes, out.smoke_cap, bm::max_bodies);
    const unsigned dark_cap = cap_of(out.scorches, out.scorch_cap, renderer::weapon_scorch_boxes);
    const unsigned shock_cap = cap_of(out.shocks, out.shock_cap, renderer::weapon_shock_boxes);
    const unsigned halo_cap = cap_of(out.halos, out.halo_cap, renderer::weapon_halo_boxes);
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
    unsigned nq = 0, ng = 0, nb = 0, nm = 0, nd = 0, ns = 0, nh = 0;
    static BoxMeta body_meta[bm::max_bodies], smoke_meta[bm::max_bodies], shock_meta[renderer::weapon_shock_boxes];
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
                            float fine = 1.f;
                            if (!full) {
                                const float over = R * px_x - fine_px * cb[2], band = fine_band_px * cb[2];
                                fine = over <= 0.f ? 0.f : over >= band ? 1.f : over / band;
                            }
                            const float look[4] = {T, bm::body_e, x, sseed};
                            const float tint4[4] = {0.f, 0.f, 0.f, 0.f};
                            const float disc[4] = {6.2831853f * unit_of(h_rot), 64.f * unit_of(mix32(h_rot)),
                                                   core::look::fireball_advect * x, fine};
                            const float g = t.core && x < bm::core_x ? bm::core_gain * (1.f - x / bm::core_x) : 0.f;
                            fireball_quad(f, cb, Rsph, rect, full, look, tint4, disc, sec_verts + 4 * nsec, g);
                            constexpr float w2 = core::look::budget_weight_fireball_coarse,
                                            w3 = core::look::budget_weight_fireball;
                            sec_meta[nsec] = BoxMeta{age_ms, e.owner, rect_screens(rect) * (w2 + (w3 - w2) * fine),
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
            const float Rs = lv.R * (cb[2] > lv.R * bm::body_r_sphere && dot3(cb, cb) > lv.R * lv.R * bm::body_r_sphere * bm::body_r_sphere
                                         ? boom_sphere_factor(lv.R * px_x / cb[2] * (1440.f / f.height))
                                         : bm::body_r_sphere);
            float rect[4];
            bool full = false;
            if (cb[2] + Rs > f.near_z && sphere_rect(f, cb, Rs, rect, &full)) {
                if (nb < body_cap) {
                    float fine = 1.f;
                    if (!full) {
                        const float over = lv.R * px_x - fine_px * cb[2], band = fine_band_px * cb[2];
                        fine = over <= 0.f ? 0.f : over >= band ? 1.f : over / band;
                    }
                    const float look[4] = {lv.T, bm::body_e * sb, lv.x, seed};
                    const float tint4[4] = {0.f, 0.f, 0.f, 0.f};
                    const float disc[4] = {6.2831853f * unit_of(h_rot), 64.f * unit_of(h_lobe),
                                           core::look::fireball_advect * lv.x, fine};
                    // the white-hot core (decision 2026-11-07): g = core_gain (1 - x / core_x) while x < core_x
                    const float g = t.core && lv.x < bm::core_x ? bm::core_gain * (1.f - lv.x / bm::core_x) : 0.f;
                    fireball_quad(f, cb, Rs, rect, full, look, tint4, disc, out.fireballs + 4 * nb, g);
                    constexpr float w2 = core::look::budget_weight_fireball_coarse, w3 = core::look::budget_weight_fireball;
                    body_meta[nb] = BoxMeta{age_ms, e.owner, rect_screens(rect) * (w2 + (w3 - w2) * fine), lv.own,
                                            std::uint8_t(fine > 0.f ? 1 : 0)};
                    body_fine[nb] = fine;
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
    // (D) The budget (section 7): past budget, (1) the third octave (hysteresis), (2) smoke largest first,
    // (3) shocks largest first, (4) other owners' glows largest first, (5) the secondaries past the two newest live ones
    // of each death, largest first; the bodies, stars, embers and fragments stay.
    // the family's cap below the shared remainder (bm::budget_screens: the close capital's 450 us fence)
    const float budget = f.budget_left < t.budget_screens ? f.budget_left : t.budget_screens;
    float area = star_area, body_area = 0.f;
    for (unsigned g = 0; g < nb; ++g) body_area += body_meta[g].area;
    for (unsigned g = 0; g < nsec; ++g) area += sec_meta[g].area;
    for (unsigned g = 0; g < nm; ++g) area += smoke_meta[g].area;
    for (unsigned g = 0; g < ns; ++g) area += shock_meta[g].area;
    for (unsigned g = 0; g < ng; ++g) area += glow_meta[g].area;
    for (unsigned g = 0; g < nh; ++g) area += halo_meta[g].area;
    area += body_area;
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
        constexpr float w2 = core::look::budget_weight_fireball_coarse, w3 = core::look::budget_weight_fireball;
        for (unsigned g = 0; g < nb; ++g)
            if (body_meta[g].rank) {
                const float we = w2 + (w3 - w2) * body_fine[g];
                const float cut = body_meta[g].area * (we - w2) / we;
                area -= cut;
                body_area -= cut;
                body_meta[g].area -= cut;
            }
        for (unsigned g = 0; g < nsec; ++g)
            if (sec_meta[g].rank) {
                const float we = w2 + (w3 - w2) * sec_fine[g];
                const float cut = sec_meta[g].area * (we - w2) / we;
                area -= cut;
                sec_meta[g].area -= cut;
            }
        n.octaves = 2;
    }
    n.smokes_shed = shed_largest(out.smokes, &nm, smoke_meta, &area, budget);
    n.shocks_shed = shed_largest(out.shocks, &ns, shock_meta, &area, budget);
    n.glows_shed = shed_largest(out.glows, &ng, glow_meta, &area, budget);
    n.glows_shed += shed_largest(out.halos, &nh, halo_meta, &area, budget);
    n.secondaries_shed = shed_largest(sec_verts, &nsec, sec_meta, &area, budget);
    n.shed = n.smokes_shed + n.shocks_shed + n.glows_shed + n.secondaries_shed;
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
    n.halos = nh;
    return n;
}
} // namespace x3m::weapon_effects::stage
