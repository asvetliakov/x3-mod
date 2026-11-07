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
// three-frame floor latched then (min_frames x that frame's step). The big tiers' pre-roll is the masked forward's
// (note section 1): a record of a tier with tier_preroll_ms draws its body, star, shock, embers, glow and smoke from the
// pre-roll's end; its secondaries and hull light come with the mask (they are not built here).
// The budget (section 7, the family's items of the shared order): past the screens left, (1) every body's third octave
// with the burst's hysteresis (enter past the budget for fireball_octave_hold frames, leave under
// fireball_octave_return), (2) the smoke, largest box first, (3) the shocks, (4) the glows of owners other than the own
// ship, largest first; never shed: the body, the star, the embers and fragments, anything of the own ship. Count caps:
// boom::max_bodies bodies, boom::max_quads sprite quads, the buffers' capacities. Reach: a record whose reach sphere
// (the glow's, the smoke's, the embers' at their age, grown by the pixel floors) is outside the view volume builds
// nothing. State: BoomState holds per ring slot the record's first stage frame, its floor, its world point and its seed
// cones, keyed by the sequence (a reused slot is recognised); CPU data naming no device resource: a Reset leaves it
// valid. No allocation, no libm, SSE2 floats and doubles; scratch in the TU's .bss.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
constexpr unsigned boom_max_cones = 6;
struct BoomSlot {
    std::uint32_t sequence = 0, first_ms = 0;
    float floor_ms = 0.f;
    double p0[3]{};                    // the death point in world (view units)
    float cone[boom_max_cones][3]{};   // the seed cones' axes (world, unit)
    std::uint8_t cones = 0, latched = 0;
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
    bool force_shock = false; // the fixture's shell gates and budget row: the missile's shell drawn though missile_shock is off
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
// overflow order, core::ring_stamp_hidden). Internal linkage (static): the scratch is the TU's .bss.
static inline BoomCounts build_boom(core::Ring& ring, BoomState& state, const HullView& f, const BoomTuning& t,
                                    BoomBuffers& out, core::VisibleOwners* visible = nullptr) noexcept {
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
    static BoxMeta glow_meta[bm::max_bodies];
    static float body_fine[bm::max_bodies];
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
        if (lv.tb < 0.f) continue; // the pre-roll (the masked forward's secondaries and hull light: not built yet)
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
            const float Rs = lv.R * bm::body_r_sphere;
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
        if (t.shock && !simple && bm::tier_shock[tier] > 0.f && (tier != bm::missile || bm::missile_shock || t.force_shock) && shock_cap) {
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
            if (lv.c[2] + Rg > f.near_z && sphere_rect(f, lv.c, Rg, rect, &full) && ng < bm::max_bodies) {
                glow_quad(f, lv.c, Rg, rect, full, light_colour, out.glows + 4 * ng);
                glow_meta[ng] = BoxMeta{age_ms, e.owner, rect_screens(rect) * core::look::budget_weight_glow, lv.own, 0};
                ++ng;
                ++n.glows;
            }
        }
        float halo_fade = (lv.x - bm::halo_fade_x0) / (bm::halo_fade_x1 - bm::halo_fade_x0);
        halo_fade = halo_fade < 0.f ? 1.f : halo_fade > 1.f ? 0.f : 1.f - halo_fade * halo_fade * (3.f - 2.f * halo_fade);
        if (t.halo && lv.R > 0.f && halo_cap && dist > lv.R * bm::body_r_sphere && halo_fade > 0.f) {
            const float Rh = bm::glow_r_x * lv.R;
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
        // Embers and fragments: closed form from the death point, the cones and the hashes; never shed.
        if (!t.embers && !t.fragments) continue;
        const unsigned count = unsigned(bm::tier_embers[tier] * (simple ? 0.5f : 1.f) + 0.5f);
        const unsigned every = unsigned(bm::fragment_every);
        const float v_e = boom_ember_speed_m(lv.R_f / view_per_m) * view_per_m; // view units per game s
        const float t_now = lv.tb * 1e-3f, t_prev = t_now > step_s ? t_now - step_s : 0.f;
        const bool first = !(t_now > t_prev);
        float p_now[3], p_prev[3];
        to_view(f.cam, s.p0, p_now);
        to_view(prev, s.p0, p_prev);
        const float rows = f.height * (1.f / 1080.f);
        const float hide_a = lv.R > 0.f ? 1.f - ((lv.x - bm::ember_hide_x0) / (bm::ember_hide_x1 - bm::ember_hide_x0) < 0.f
                                                     ? 0.f
                                                     : (lv.x - bm::ember_hide_x0) / (bm::ember_hide_x1 - bm::ember_hide_x0) > 1.f
                                                           ? 1.f
                                                           : (lv.x - bm::ember_hide_x0) / (bm::ember_hide_x1 - bm::ember_hide_x0))
                                        : 0.f; // the body's hiding share (1 young, 0 once eroded)
        for (unsigned m = 0; m < count; ++m) {
            const bool fragment = every && (m % every) == every - 1u;
            if (fragment ? !t.fragments : !t.embers) continue;
            const std::uint32_t h = element_hash(seq, bm::ember_element + m), h2 = mix32(h + 0x9e3779b9u);
            const float law = fragment ? bm::fragment_life * L : bm::ember_life[m % 3u] * L;
            const float life_law = law * (1.f + bm::ember_stagger * (byte_unit(h2, 0) - 0.5f));
            const bool fl = s.floor_ms > life_law;
            const float life = fl ? s.floor_ms : life_law;
            if (!(lv.tb < life)) continue;
            float d[3], off[3];
            boom_ember_dir(s.cone[(h >> 8) % (s.cones ? s.cones : 1u)], mix32(h ^ 0x5bd1e995u), d);
            cube_point(mix32(h2 ^ 0x27d4eb2fu), off);
            const float speed = (fragment ? bm::fragment_v : 1.f) * v_e *
                                (bm::ember_speed_lo + (bm::ember_speed_hi - bm::ember_speed_lo) * byte_unit(h2, 8));
            // the launch point inside the newborn body (within ember_hide_r R_0), the offset within the unit ball
            const float o_max = bm::ember_hide_r * lv.R_0, o = bm::ember_p0 * lv.R_f < o_max ? bm::ember_p0 * lv.R_f : o_max;
            {
                const float r2 = dot3(off, off);
                if (r2 > 1.f) {
                    const float s = rsqrt_nr(r2);
                    for (unsigned q = 0; q < 3; ++q) off[q] *= s;
                }
            }
            // hidden while inside the body: fully within ember_hide_r R(t), visible past R(t), the share fading with the
            // body's erosion
            float disp[3];
            for (unsigned q = 0; q < 3; ++q) disp[q] = off[q] * o + d[q] * speed * t_now;
            const float r_e = scalar::sqrt(dot3(disp, disp));
            float inside = lv.R > 0.f ? (lv.R - r_e) / ((1.f - bm::ember_hide_r) * lv.R) : 0.f;
            inside = inside < 0.f ? 0.f : inside > 1.f ? 1.f : inside;
            const float vis = 1.f - hide_a * inside;
            if (!(vis > 0.01f)) {
                ++n.embers_hidden;
                continue;
            }
            float dn[3], dp[3], on[3], op[3];
            rotate_world_to_view(f.cam, d, dn);
            rotate_world_to_view(prev, d, dp);
            rotate_world_to_view(f.cam, off, on);
            rotate_world_to_view(prev, off, op);
            float head[3], tail[3];
            for (unsigned q = 0; q < 3; ++q) {
                head[q] = p_now[q] + on[q] * o + dn[q] * speed * t_now;
                tail[q] = p_prev[q] + op[q] * o + dp[q] * speed * t_prev;
            }
            if (!(head[2] > f.near_z) || !(tail[2] > f.near_z)) continue;
            if (first)
                for (unsigned q = 0; q < 3; ++q) tail[q] = head[q] - dn[q] * speed * 1e-3f;
            const float fade = 1.f - lv.tb / life;
            if (nq >= sprite_cap) {
                ++n.capped;
                continue;
            }
            // the pixel mode: a hashed width in pixels (scaled with the rows), a hashed length of the cap 0.35 R_f on
            // screen (half for a fragment), never under two widths; the motion's direction only
            const float w_px = (fragment ? bm::fragment_w_px
                                         : bm::ember_w_px_lo + (bm::ember_w_px_hi - bm::ember_w_px_lo) * byte_unit(h2, 16)) *
                               rows;
            const float cap_px = bm::ember_len_max * lv.R_f * px_x / head[2];
            float len_px = (bm::ember_len_lo + (1.f - bm::ember_len_lo) * byte_unit(h2, 24)) * cap_px * (fragment ? 0.5f : 1.f);
            len_px = len_px > 2.f * w_px ? len_px : 2.f * w_px;
            if (!fragment) {
                const float e_raw = bm::ember_e * core::exp_neg(-lv.tb / (bm::ember_tau * L)) * fade * vis; // E x T
                streak_vertices(head, tail, e_raw, e_raw, -w_px, heat_packed(fade), out.sprites + 4 * nq, len_px);
                ++nq;
                ++n.embers;
            } else {
                // the hot tip and the dark box under it (surface-bound)
                const float e_raw = 0.5f * bm::ember_e * fade * vis;
                streak_vertices(head, tail, e_raw, e_raw, -w_px, heat_packed(bm::fragment_tip_t * fade), out.sprites + 4 * nq,
                                len_px);
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
    }
    // (D) The budget (section 7): past f.budget_left, (1) the third octave (hysteresis), (2) smoke largest first,
    // (3) shocks largest first, (4) other owners' glows largest first; the bodies, stars, embers and fragments stay.
    float area = star_area, body_area = 0.f;
    for (unsigned g = 0; g < nb; ++g) body_area += body_meta[g].area;
    for (unsigned g = 0; g < nm; ++g) area += smoke_meta[g].area;
    for (unsigned g = 0; g < ns; ++g) area += shock_meta[g].area;
    for (unsigned g = 0; g < ng; ++g) area += glow_meta[g].area;
    for (unsigned g = 0; g < nh; ++g) area += halo_meta[g].area;
    area += body_area;
    if (state.octaves_coarse) {
        if (state.coarse_frames >= core::look::fireball_octave_hold && area < core::look::fireball_octave_return * f.budget_left)
            state.octaves_coarse = false;
        else if (state.coarse_frames < 255)
            ++state.coarse_frames;
    } else if (area > f.budget_left && nb) {
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
        n.octaves = 2;
    }
    n.smokes_shed = shed_largest(out.smokes, &nm, smoke_meta, &area, f.budget_left);
    n.shocks_shed = shed_largest(out.shocks, &ns, shock_meta, &area, f.budget_left);
    n.glows_shed = shed_largest(out.glows, &ng, glow_meta, &area, f.budget_left);
    n.glows_shed += shed_largest(out.halos, &nh, halo_meta, &area, f.budget_left);
    n.shed = n.smokes_shed + n.shocks_shed + n.glows_shed;
    n.smokes = nm;
    n.shocks = ns;
    n.glows = ng;
    n.screens = area;
    n.body_screens = body_area;
    out.sprite_quads = nq;
    out.glow_boxes = ng;
    out.fireball_boxes = nb;
    out.smoke_boxes = nm;
    out.scorch_boxes = nd;
    out.shock_boxes = ns;
    out.halo_boxes = nh;
    n.halos = nh;
    return n;
}
} // namespace x3m::weapon_effects::stage
