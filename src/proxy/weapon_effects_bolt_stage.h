#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_bolt.h"
#include "weapon_effects_hull.h"
#include "sse_scalar.h"

// Step 4, the stage half of the bolts and the muzzle (docs/architecture/bolt-muzzle-look.md, weapon-look.md 3.3; the
// hooks' lists in weapon_effects_bolt.h). Portable: no Windows, no D3D call, no allocation, no libm (SSE scalar only);
// the production glue (motion_output_weapon_effects_inc.h) and the D3D fixture compile it.
//
// - bolt_look_of: a TBullets row's look from the live record and the generated table (the classifier's tint rule, the
//   style, the calibre k, the mount tier and range, the muzzle class), cached per frame by BoltRowCache.
// - bolt_width_m: section 1's law, w = (w_px_target d_eng / f_ref) (1 + w_k k), d_eng = min(d_eng_m[tier], 0.75 range).
// - build_bolts: the frame's list (and the previous frame's, for each bolt's previous head) into one capsule quad per
//   visible bolt (renderer::WeaponBoltQuad), every quantity of the vertex law (5.2) laid on the CPU: the world segment
//   from the head back along the travel for min(L + T, v age) (birth: never longer than flown), the eye-plane clip of the
//   back end (degenerate case A; a head behind the plane is culled), both ends projected through the frame's jittered
//   projection, the per-end body widths and halo sigmas with the floors, the own ship's ceiling and the halo cap, the
//   length floor x the foreshortening c (x the flown share), the streak to the previous head capped at (1 + c_s) core
//   lengths with the energy factor g_s, the end-on disc (degenerate case B), the quad around both segments, the frustum
//   cull. Quads leave in kind order (line kinds, orbs, waves), rim-eligible first within the line and orb ranges.
// - bolt_parity: the parity rule (weapon-look 3.3, unknown 14): when the tap list and the late draw's instances disagree
//   in count, only records whose nearest instance lies within the tolerance (scaled by the instance's on-screen size) and
//   whose body (the instance period the record's row learned) agrees are kept; the learning runs every frame.
// - bolt_take: the frame's ownership, decided at its first bullet-producer draw, and each bullet draw's action.
// - build_muzzles: the muzzle records the hook marked ours into the stage's sprite, cone (end-on: the star), glow,
//   fireball and scorch elements (section 3); muzzle_latch: Present's muzzle ownership for the next frame's shots.
namespace x3m::weapon_effects::stage {
namespace bolt = core::bolt;
namespace mz = core::muzzle;

// ---------------------------------------------------------------------------------------------- the row's look
struct BoltLook {
    std::uint8_t valid = 0;
    std::uint8_t style = bolt::slug; // 0 tracer, 1 slug, 2 orb, 3 wave (the classifier's shape)
    std::uint8_t heavy = 0;          // k >= heavy_k on a tracer or slug
    std::uint8_t beam = 0;           // a beam row (flags 0x2 / 0x4): step 5's; drawn as a plain line meanwhile
    std::uint8_t tier = 0;           // the mount tier (0 fighter, 1 corvette, 2 capital)
    std::uint8_t mclass = mz::energy;
    std::uint32_t flags = 0;
    float k = 0.f, w_m = 1.f, range_m = 0.f;
    float tint[3] = {1.f, 1.f, 1.f};
    // the hand pins (bolt-muzzle-look.md 11.5, the table's bolt_* keys): the body radiance, the orb diameter (w), no halo
    float e_body = bolt::e_body, w_orb = bolt::rest_w[2];
    std::uint8_t halo_off = 0;
};
inline float bolt_d_eng_m(unsigned tier, float range_m) noexcept {
    const float d = bolt::d_eng_m[tier < 3 ? tier : 0];
    const float r = range_m > 0.f ? bolt::d_eng_range * range_m : d;
    return r < d ? r : d;
}
inline float bolt_width_m(unsigned tier, float range_m, float k) noexcept {
    const float w = bolt::w_px_target * bolt_d_eng_m(tier, range_m) / bolt::f_ref * (1.f + bolt::w_k * k);
    return w < bolt::w_cap_m ? w : bolt::w_cap_m;
}
inline std::uint8_t muzzle_class_of(std::uint32_t flags, std::uint8_t style, float k) noexcept {
    if (flags & core::bullet_burst_bits) return mz::flak;
    if (k >= bolt::heavy_k) return mz::heavy;
    if (flags & core::bullet_ammunition_bit) return mz::ballistic;
    if (style == bolt::orb || style == bolt::wave) return mz::plasma;
    return mz::energy;
}
// The look from the parts (the fixture's and the lab's twin; bolt_look_of reads them from the game).
inline BoltLook bolt_look_from(std::uint32_t flags, std::uint8_t shape, float k, std::uint8_t tier, float range_m,
                               const float tint[3]) noexcept {
    BoltLook l;
    l.valid = 1;
    l.flags = flags;
    l.beam = (flags & core::bullet_beam_bits) ? 1 : 0;
    const std::uint8_t s = std::uint8_t(shape & core::style_shape_mask);
    l.style = s == core::shape_orb ? bolt::orb : s == core::shape_wave ? bolt::wave : s == core::shape_tracer ? bolt::tracer
                                                                                                             : bolt::slug;
    l.k = k < 0.f ? 0.f : k > 1.f ? 1.f : k;
    l.heavy = (l.k >= bolt::heavy_k && (l.style == bolt::tracer || l.style == bolt::slug)) ? 1 : 0;
    l.tier = tier < 3 ? tier : 0;
    l.range_m = range_m > 0.f ? range_m : 0.f;
    l.w_m = bolt_width_m(l.tier, l.range_m, l.k);
    for (unsigned i = 0; i < 3; ++i) l.tint[i] = tint[i];
    l.mclass = muzzle_class_of(flags, l.style, l.k);
    return l;
}
// The live row (the impact handler's classifier rule, weapon_effects_core.h): the flag word, the light colour, the
// per-shot damage, the table's shape, overrides, mount tier and range. False (valid 0) when the row cannot be read.
inline BoltLook bolt_look_of(Reader rd, const core::Globals& g, const core::Table* t, unsigned row) noexcept {
    BoltLook out;
    std::uint32_t base = 0, count = 0;
    if (!rd(g.bullet_table_slot, &base, 4) || !rd(g.bullet_count_slot, &count, 4) || !base || row >= count) return out;
    const std::uintptr_t rec = std::uintptr_t(base) + std::uintptr_t(row) * core::layout::type_stride;
    std::uint32_t flags = 0;
    std::int32_t light[3]{}, shield = 0, hull = 0;
    if (!rd(rec + core::layout::bullet_flags, &flags, 4) || !rd(rec + core::layout::bullet_light, light, 12) ||
        !rd(rec + core::layout::bullet_shield_damage, &shield, 4) || !rd(rec + core::layout::bullet_hull_damage, &hull, 4))
        return out;
    const bool table_ok = t && t->rows == count && row < core::table_capacity;
    const std::uint8_t bits = table_ok ? t->bits[row] : 0;
    std::uint8_t shape = (bits & core::row_present) ? t->shape[row] : std::uint8_t(core::shape_slug);
    shape = core::style_from(flags, shape);
    if (bits & core::row_style_override) shape = t->shape[row];
    const std::uint32_t lw = core::pack_rgb(core::clamp_channel(light[0]), core::clamp_channel(light[1]),
                                            core::clamp_channel(light[2]));
    std::uint32_t word = 0xffffffu | core::rgb_present;
    const std::uint8_t pin = table_ok ? t->bolt_pin[row] : 0;
    if (bits & core::row_tint_override)
        word = t->override_rgb[row];
    else if ((pin & core::pin_tint_texture) && (bits & core::row_texture))
        word = t->texture_rgb[row];
    else if (core::light_saturated(light[0], light[1], light[2]))
        word = lw;
    else if (bits & core::row_texture)
        word = t->texture_rgb[row];
    float tint[3];
    core::tint_linear(word, tint);
    const float k = (bits & core::row_calibre_override) ? float(t->calibre_q[row]) * (1.f / 255.f)
                                                        : core::calibre_of(shield > hull ? shield : hull);
    BoltLook l = bolt_look_from(flags, shape, k, table_ok ? t->mount_tier[row] : 0, table_ok ? float(t->range_m[row]) : 0.f,
                                tint);
    if (pin & core::pin_e_body) l.e_body = float(t->bolt_e_body_q[row]) * 0.01f;
    if (pin & core::pin_w_orb) l.w_orb = float(t->bolt_w_orb_q[row]) * 0.01f;
    l.halo_off = (pin & core::pin_halo_off) ? 1 : 0;
    // The table's class pin (hull-impact-classes.md section 2, weapon_overrides.json `hull`) sets the muzzle class too (a
    // Mayhem mass driver carries no ammunition flag; its pin is ballistic); a flak row keeps the flak muzzle.
    if ((bits & core::row_hull_class) && !(flags & core::bullet_burst_bits)) {
        const std::uint8_t pin = t->hull_class[row];
        l.mclass = pin == core::class_ballistic ? std::uint8_t(mz::ballistic)
                   : pin == core::class_plasma  ? std::uint8_t(mz::plasma)
                   : pin == core::class_heavy   ? std::uint8_t(mz::heavy)
                                                : std::uint8_t(mz::energy);
    }
    return l;
}
// One look per row per frame (512 rows; the stamp is the caller's frame counter, never 0).
struct BoltRowCache {
    std::uint32_t stamp[core::table_capacity]{};
    BoltLook look[core::table_capacity]{};
};
inline const BoltLook& bolt_look_cached(BoltRowCache& c, std::uint32_t frame, Reader rd, const core::Globals& g,
                                        const core::Table* t, unsigned row) noexcept {
    static const BoltLook none{};
    if (row >= core::table_capacity) return none;
    if (c.stamp[row] != frame) {
        c.look[row] = bolt_look_of(rd, g, t, row);
        c.stamp[row] = frame;
    }
    return c.look[row];
}

// ---------------------------------------------------------------------------------------------- the law (section 2)
struct BoltLaw {
    float w_m = 1.f, L_m = 0.f, T_m = 0.f; // body width (orb: the orb diameter; wave: the thickness), core and tail length
    float sigma_w = 0.8f, h = 0.75f, tail_root = 0.6f, tail_gain = 0.5f, e_halo = bolt::e_halo, streak_c = 1.f;
    float chord_m = 0.f;                    // the wave's chord S
    float e_body = bolt::e_body;            // the body radiance E_b (a hand pin may lower it)
    std::uint8_t kind = 0;                  // 0 line, 1 orb, 2 wave
    float code = 0.f;                       // the pixel program's style code (0..3, + 4 heavy)
};
inline BoltLaw bolt_law(const BoltLook& l, float v_mps, bool simple) noexcept {
    BoltLaw o;
    const float w = l.w_m;
    const float sv = 0.75f + 0.5f * (v_mps < bolt::sv_ref_mps ? (v_mps > 0.f ? v_mps : 0.f) / bolt::sv_ref_mps : 1.f);
    const unsigned s = l.style < 4 ? l.style : 1u;
    const unsigned ts = l.heavy ? unsigned(bolt::heavy) : s; // the tail, halo and h index (heavy modifies)
    o.sigma_w = bolt::halo_sigma_w[ts];
    o.h = l.heavy ? 1.f : bolt::h[s];
    o.e_halo = l.halo_off ? 0.f : l.heavy ? bolt::e_halo_heavy : bolt::e_halo;
    o.e_body = l.e_body;
    o.tail_root = bolt::tail_root_w[ts];
    o.tail_gain = bolt::tail_gain[ts];
    o.streak_c = simple ? (s == bolt::wave ? 0.f : bolt::streak_c_simple) : bolt::streak_c[s];
    const float tau_s = bolt::tail_tau_ms[ts] * 0.001f, tmax = bolt::tail_max_w[ts] * w;
    o.code = float(s) + (l.heavy ? 4.f : 0.f);
    if (l.beam) { // step 5's until then: a plain line, no tail
        o.kind = 0;
        o.w_m = bolt::beam_w_m;
        o.code = 0.f;
        o.tail_root = 0.6f;
        return o;
    }
    if (s == bolt::orb) {
        o.kind = 1;
        o.w_m = l.w_orb * w; // the orb's diameter w_orb (rest_w[2], or the hand pin's)
        o.sigma_w = bolt::halo_sigma_w[2] * l.w_orb; // 0.9 w_orb in units of w
        o.T_m = simple ? 0.f : (v_mps * tau_s < bolt::tail_max_w[2] * w ? v_mps * tau_s : bolt::tail_max_w[2] * w);
        o.tail_root = bolt::tail_root_w[2];
        o.h = bolt::orb_h_hi;
        return o;
    }
    if (s == bolt::wave) {
        o.kind = 2;
        o.w_m = bolt::rest_w[3] * w;          // the crescent's thickness
        o.chord_m = bolt::wave_span_w * w;    // col 18 once its unit is pinned (unknown 3)
        o.streak_c = 0.f;
        return o;
    }
    o.kind = 0;
    o.w_m = w;
    o.L_m = bolt::rest_w[s] * w * sv;
    o.T_m = simple ? 0.f : (v_mps * tau_s < tmax ? v_mps * tau_s : tmax);
    return o;
}
// The trim rule (2.7): a stream ratio (L + T / 2) f / v above trim_rs cuts the core to trim_rs v / f - T / 2 (shot
// interval in ms; 0 unknown: untrimmed).
inline float bolt_trimmed_core(float L_m, float T_m, float v_mps, float interval_ms) noexcept {
    if (!(interval_ms > 0.f) || !(v_mps > 0.f)) return L_m;
    const float spacing = v_mps * interval_ms * 0.001f;
    const float rs = (L_m + 0.5f * T_m) / spacing;
    if (!(rs > bolt::trim_rs)) return L_m;
    const float cut = bolt::trim_rs * spacing - 0.5f * T_m;
    return cut > 0.f ? cut : 0.f;
}

// The rim band in rho (body half widths) for a body w_px wide (2.1, 2026-11-08 #5): under rim_px_w px the band lies
// rim_px_band px past the body's edge, from rim_px_blend_w the w-relative 2 rim_band_w, linear in w_px between (the
// pixel program's twin, weapon_bolt_ps.hlsl rim_rho).
inline void bolt_rim_rho(float wpx, float* lo, float* hi) noexcept {
    const float half = 0.5f * (wpx > 1e-3f ? wpx : 1e-3f);
    const float plo = 1.f + bolt::rim_px_band[0] / half, phi = 1.f + bolt::rim_px_band[1] / half;
    float t = (wpx - bolt::rim_px_w) / (bolt::rim_px_blend_w - bolt::rim_px_w);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    *lo = plo + (2.f * bolt::rim_band_w[0] - plo) * t;
    *hi = phi + (2.f * bolt::rim_band_w[1] - phi) * t;
}

// ---------------------------------------------------------------------------------------------- the frame
struct BoltView {
    Camera cam{}, prev{};    // this frame's view (jittered projection) and the previous frame's rows
    bool prev_valid = false;
    float width = 0.f, height = 0.f, near_z = 1.f;
    float m22 = 1.f, m32 = 0.f; // the scene's depth law: clip z = m22 view z + m32, w = view z
    std::uint32_t now = 0, step_ms = 0;
    bool cockpit = false;
    bool simple = bolt::mode == bolt::simple;
    bool rim = bolt::rim;
};
struct BoltCounts {
    unsigned listed = 0, drawn = 0, behind = 0, offscreen = 0, expired = 0, invalid = 0, beams = 0;
    unsigned lines = 0, orbs = 0, waves = 0, rim_lines = 0, rim_orbs = 0, streaks = 0, no_prev = 0, discs = 0;
    unsigned floored_len = 0, floored_body = 0, capped_own = 0, capped_halo = 0, parity_dropped = 0, capped_back = 0;
    unsigned capped_hostile = 0; // ends (and wave chords) held at hostile_body_max_px of the view height
};
// The previous frame's records by (object, creation): 1024 open-addressed slots stamped per build. The stamp must be
// unique per previous list (the caller passes the hooks' Present generation, monotonic): a stamp repeated on another list
// would leave the older list's slots valid and hand a bolt another bolt's previous record (review 2026-11-07, finding 1:
// frame | 1 repeated on frames 2k and 2k + 1). The same stamp again is the same list: the index is kept.
struct BoltPrevIndex {
    std::uint32_t generation = 0;
    std::uint32_t obj[2 * bolt::list_capacity]{}, created[2 * bolt::list_capacity]{}, stamp[2 * bolt::list_capacity]{};
    std::uint16_t index[2 * bolt::list_capacity]{};
};
inline unsigned bolt_key_slot(std::uint32_t obj, std::uint32_t created) noexcept {
    return unsigned((obj ^ (created * 0x85ebca6bu)) * 0x9e3779b1u >> 22) & (2 * bolt::list_capacity - 1u);
}
inline void bolt_prev_index(BoltPrevIndex& x, const core::BoltRecord* r, unsigned n, std::uint32_t stamp) noexcept {
    if (stamp == x.generation && stamp) return; // this list is indexed already (a second build in the frame)
    x.generation = stamp;
    constexpr unsigned size = 2 * bolt::list_capacity;
    for (unsigned i = 0; i < n; ++i) {
        unsigned h = bolt_key_slot(r[i].obj, r[i].created);
        for (unsigned probe = 0; probe < size; ++probe, h = (h + 1u) & (size - 1u))
            if (x.stamp[h] != stamp) {
                x.stamp[h] = stamp;
                x.obj[h] = r[i].obj;
                x.created[h] = r[i].created;
                x.index[h] = std::uint16_t(i);
                break;
            }
    }
}
inline int bolt_prev_find(const BoltPrevIndex& x, std::uint32_t obj, std::uint32_t created) noexcept {
    constexpr unsigned size = 2 * bolt::list_capacity;
    unsigned h = bolt_key_slot(obj, created);
    for (unsigned probe = 0; probe < size; ++probe, h = (h + 1u) & (size - 1u)) {
        if (x.stamp[h] != x.generation) return -1;
        if (x.obj[h] == obj && x.created[h] == created) return int(x.index[h]);
    }
    return -1;
}
inline float bolt_rows_scale(const BoltView& v) noexcept {
    return v.height / bolt::ref_rows;
}
inline void bolt_px(const Camera& c, float hw, float hh, const float view[3], float* x, float* y) noexcept {
    const float iz = 1.f / view[2];
    *x = (c.m00 * view[0] * iz + c.m20 + 1.f) * hw;
    *y = (1.f - c.m11 * view[1] * iz - c.m21) * hh;
}
// One bolt's quad. False when it draws nothing (counted). `prev` the previous frame's record of this bolt (or null),
// `v_mps` its speed, `trim_core_m` the trimmed core (or < 0: the law's).
struct BoltInput {
    double head[3]{};      // world (view units)
    float dir[3]{0.f, 0.f, 1.f}; // the unit travel (world)
    bool prev = false;
    double prev_head[3]{};
    float v_mps = 1000.f;
    float age_ms = 1e6f;   // since creation (game ms)
    float remain_ms = 1e6f; // to expiry (game ms)
    bool own = false;
    float seed = 0.f;
    float length_m = -1.f; // a beam's fixed length (ext+0x78 / 500), else < 0
};
inline bool bolt_quad(const BoltView& v, const BoltLook& look, const BoltLaw& law, const BoltInput& in,
                      renderer::WeaponBoltQuad* q, bool* rim_ok, BoltCounts* n) noexcept {
    const float hw = 0.5f * v.width, hh = 0.5f * v.height, fy = v.cam.m11 * hh, rows = bolt_rows_scale(v);
    const float upm = core::engine_units_per_metre * core::world_scale; // view units per metre (5)
    // Expiry: x saturate(remain / max(end_ms, 3 steps)).
    const float end = float(bolt::end_ms) > 3.f * float(v.step_ms) ? float(bolt::end_ms) : 3.f * float(v.step_ms);
    if (!(in.remain_ms > 0.f)) {
        ++n->expired;
        return false;
    }
    const float fade = in.remain_ms < end ? in.remain_ms / end : 1.f;
    // The world segment (birth: never longer than flown).
    float L = law.L_m, T = law.T_m;
    if (in.length_m >= 0.f) {
        L = in.length_m;
        T = 0.f;
    }
    const float flown = in.v_mps * (in.age_ms > 0.f ? in.age_ms : 0.f) * 0.001f;
    const float full = L + T;
    const float ell = law.kind == 0 ? (flown < full ? flown : full) : (law.kind == 1 ? (flown < T ? flown : T) : 0.f);
    const float Lv = law.kind == 0 ? (ell < L ? ell : L) : 0.f;
    float hv[3], bv[3];
    to_view(v.cam, in.head, hv);
    const double back[3] = {in.head[0] - double(in.dir[0]) * double(ell * upm),
                            in.head[1] - double(in.dir[1]) * double(ell * upm),
                            in.head[2] - double(in.dir[2]) * double(ell * upm)};
    to_view(v.cam, back, bv);
    const float znear = v.near_z + 0.5f * upm; // the eye plane + 0.5 m (degenerate case A)
    if (!(hv[2] > znear)) {
        ++n->behind;
        return false;
    }
    float ell_vis = ell;
    if (bv[2] < znear) { // clip the back end towards the head
        const float f = (hv[2] - znear) / (hv[2] - bv[2]);
        for (unsigned i = 0; i < 3; ++i) bv[i] = hv[i] + (bv[i] - hv[i]) * f;
        ell_vis = ell * f;
    }
    float hx, hy, bx, by;
    bolt_px(v.cam, hw, hh, hv, &hx, &hy);
    bolt_px(v.cam, hw, hh, bv, &bx, &by);
    const float ppm_h = upm * fy / hv[2], ppm_b = upm * fy / bv[2];
    // Per-end widths, floors, ceilings.
    const float own_max = bolt::own_body_max_px * rows, halo_max = bolt::halo_px_max * rows;
    // another ship's bolt near the eye (review 2026-11-08): body (and the wave's chord) never above hostile_body_max_px of
    // the view height at either end, radiance unchanged (the own ceiling's rule), so no hostile bolt fills the view
    const float hostile_max = bolt::hostile_body_max_px * v.height;
    // another ship's back end near the eye (note 15): never wider than the head or back_w_own_x x the own ceiling,
    // whichever is wider (a bolt the head of which is that wide, or one side-on, is untouched)
    float back_max = bolt::back_w_own_x * own_max;
    auto width_at = [&](float ppm, bool back) {
        float w = law.w_m * ppm;
        if (w < bolt::floor_body_px) {
            w = bolt::floor_body_px;
            ++n->floored_body;
        }
        if (in.own && w > own_max) {
            w = own_max;
            ++n->capped_own;
        } else if (back && !in.own && w > back_max) {
            w = back_max;
            ++n->capped_back;
        }
        if (!in.own && w > hostile_max) {
            w = hostile_max;
            ++n->capped_hostile;
        }
        return w;
    };
    // The bounded halo cap (2026-11-08, #3): sigma at most the cap (an own bolt's at its body ceiling) and, at an end the
    // cap binds, E_h x cap / sigma_world there (per end: a far head keeps its halo when its near back end is capped).
    const float halo_cap = in.own && own_max < halo_max ? own_max : halo_max;
    auto sigma_at = [&](float ppm, float* k) {
        float s = law.sigma_w * look.w_m * ppm;
        if (s < bolt::floor_halo_px) s = bolt::floor_halo_px;
        *k = 1.f;
        if (s > halo_cap) {
            *k = halo_cap / s;
            s = halo_cap;
            ++n->capped_halo;
        }
        return s;
    };
    const float wh = width_at(ppm_h, false);
    back_max = back_max > wh ? back_max : wh;
    float kh = 1.f, kb = 1.f;
    const float wb = width_at(ppm_b, true), sh = sigma_at(ppm_h, &kh), sb = sigma_at(ppm_b, &kb);
    // The projected axis, its foreshortening c and the length floor (x c x the flown share).
    float ax = bx - hx, ay = by - hy;
    float A = scalar::sqrt(ax * ax + ay * ay);
    const float side_on = ell_vis * ppm_h;
    float c = side_on > 1e-3f ? A / side_on : 0.f;
    c = c > 1.f ? 1.f : c;
    if (A > 1e-3f) {
        ax /= A;
        ay /= A;
    } else {
        // end-on or unborn: the screen direction of the travel for the orb's halo and the wave's arc, else (1, 0)
        const double ahead[3] = {in.head[0] + double(in.dir[0]) * double(upm), in.head[1] + double(in.dir[1]) * double(upm),
                                 in.head[2] + double(in.dir[2]) * double(upm)};
        float av[3];
        to_view(v.cam, ahead, av);
        float tx = 0.f, ty = 0.f;
        if (av[2] > znear) {
            float px, py;
            bolt_px(v.cam, hw, hh, av, &px, &py);
            tx = hx - px;
            ty = hy - py;
        }
        const float tl = scalar::sqrt(tx * tx + ty * ty);
        ax = tl > 1e-3f ? tx / tl : 1.f;
        ay = tl > 1e-3f ? ty / tl : 0.f;
        // the side-on screen direction's foreshortening for the wave (c from the travel's projection over a metre)
        if (law.kind == 2) c = tl / (upm * fy / hv[2]) > 1.f ? 1.f : tl / (upm * fy / hv[2]);
        A = 0.f;
    }
    if (law.kind == 2 && A > 1e-3f) c = 1.f;
    float Lpx = 0.f, Tpx = 0.f;
    if (law.kind == 0 && ell_vis > 0.f) {
        const float share = full > 0.f ? ell / full : 0.f;
        const float floor = bolt::floor_len_px * rows * c * share;
        float Aa = A;
        if (Aa < floor) {
            Aa = floor;
            ++n->floored_len;
        }
        Lpx = ell > 0.f ? Aa * (Lv / ell) : 0.f;
        Tpx = Aa - Lpx;
        A = Aa;
    } else if (law.kind == 1) {
        Tpx = A; // the wisp
        Lpx = 0.f;
    } else
        A = 0.f;
    if (law.kind == 0 && A < 0.5f) ++n->discs;
    // The end-on footprint (section 2.4, core::bolt::floor_endon_px): a line whose projected axis is shorter than the
    // end-on floor keeps a disc of at least that body width, falling linearly to the side-on floor as the axis grows.
    float wh_e = wh, wb_e = wb;
    if (law.kind == 0) {
        // keyed on the travel's own foreshortening (a newborn side-on bolt, its axis not yet flown, keeps the side-on
        // floor): c_dir = the screen length of a metre of travel at the head over a side-on metre
        float c_dir = 1.f;
        {
            const double ahead[3] = {in.head[0] + double(in.dir[0]) * double(upm), in.head[1] + double(in.dir[1]) * double(upm),
                                     in.head[2] + double(in.dir[2]) * double(upm)};
            float av[3];
            to_view(v.cam, ahead, av);
            if (av[2] > znear) {
                float px, py;
                bolt_px(v.cam, hw, hh, av, &px, &py);
                const float tl = scalar::sqrt((px - hx) * (px - hx) + (py - hy) * (py - hy)), side = upm * fy / hv[2];
                c_dir = side > 1e-6f ? (tl / side < 1.f ? tl / side : 1.f) : 1.f;
            } else
                c_dir = 0.f;
        }
        const float fe = bolt::floor_endon_px * rows, k = A < fe ? 1.f - A / fe : 0.f, floor_e = fe * k * (1.f - c_dir);
        if (wh_e < floor_e) wh_e = floor_e;
        if (wb_e < floor_e) wb_e = floor_e;
    }
    // The streak towards the previous head (capped at (1 + c_s) rest lengths, energy kept by g_s).
    float sdx = 0.f, sdy = 0.f, D = 0.f, g_s = 1.f;
    // The rest image along the streak: a line's core length; an orb's diameter, its stretch beginning at its edge (the
    // streak from its centre: lit length at most 0.5 W + c_s W behind the centre, 1.5 W in all with the disc).
    const float rest = law.kind == 1 ? wh : Lpx, stretch_at = law.kind == 1 ? 0.5f * wh : Lpx;
    if (in.prev && v.prev_valid && law.streak_c > 0.f && rest > 0.f) {
        float pv[3];
        to_view(v.prev, in.prev_head, pv);
        if (pv[2] > znear) {
            float px, py;
            bolt_px(v.cam, hw, hh, pv, &px, &py);
            const float dx = px - hx, dy = py - hy, dl = scalar::sqrt(dx * dx + dy * dy);
            const float cap = stretch_at + law.streak_c * rest;
            D = dl < cap ? dl : cap;
            if (dl > 1e-3f) {
                sdx = dx / dl;
                sdy = dy / dl;
            }
            const float e = D > stretch_at ? D - stretch_at : 0.f;
            g_s = rest / (rest + e * (1.f / 3.f));
            if (D > 0.5f) ++n->streaks;
        }
    } else if (!in.prev)
        ++n->no_prev;
    if (D < 0.5f) D = 0.f;
    // The quad: around the head, the back end and the streak's end, R past them: the halo's 3 sigma, and where the rim
    // draws, its band's outer edge (bolt_rim_rho's; an orb's of its lobed radius) plus a pixel, so a bolt
    // wider than about 90 px keeps its whole rim (review 2026-11-07, finding 6).
    const float big_w = wh_e > wb_e ? wh_e : wb_e, big_s = sh > sb ? sh : sb;
    // the rim band in rho of the body (an orb's of its diameter), its outer edge on the lobed radius
    const float rim_w = law.kind == 1 ? wh : big_w;
    float rim_lo = 0.f, rim_hi = 0.f;
    bolt_rim_rho(rim_w, &rim_lo, &rim_hi);
    const bool rim = v.rim && !v.simple && law.kind < 2 && 0.5f * (rim_hi - rim_lo) * rim_w >= bolt::rim_min_px - 1e-3f;
    float R = 0.5f * big_w + 3.f * big_s;
    float S_px = 0.f;
    if (law.kind == 1) R = 0.5f * wh * (1.f + bolt::orb_lobe_amp) + 3.f * big_s * bolt::orb_stretch;
    if (rim) {
        const float edge = 0.5f * rim_hi * rim_w * (law.kind == 1 ? 1.f + bolt::orb_lobe_amp : 1.f) + 1.f;
        R = R > edge ? R : edge;
    }
    if (law.kind == 2) {
        S_px = law.chord_m * ppm_h;
        // an own wave's lens (and crescent) never wider than wave_lens_own_x body ceilings (2026-11-08, #2)
        const float s_own = bolt::wave_lens_own_x * own_max;
        if (in.own && S_px > s_own) S_px = s_own;
        if (!in.own && S_px > hostile_max) {
            S_px = hostile_max;
            ++n->capped_hostile;
        }
        R = 0.5f * S_px + 0.5f * big_w + 3.f * big_s;
    }
    const float px[3] = {hx, hx + ax * A, hx + sdx * D}, py[3] = {hy, hy + ay * A, hy + sdy * D};
    float ux, uy;
    {
        const float a2 = A, d2 = D;
        if (a2 >= d2 && a2 > 0.5f) {
            ux = -ax;
            uy = -ay;
        } else if (d2 > 0.5f) {
            ux = -sdx;
            uy = -sdy;
        } else {
            ux = 1.f;
            uy = 0.f;
        }
    }
    const float nx = -uy, ny = ux;
    float lo = 0.f, hi = 0.f, clo = 0.f, chi = 0.f;
    for (unsigned i = 1; i < 3; ++i) {
        const float a = (px[i] - hx) * ux + (py[i] - hy) * uy, b = (px[i] - hx) * nx + (py[i] - hy) * ny;
        lo = a < lo ? a : lo;
        hi = a > hi ? a : hi;
        clo = b < clo ? b : clo;
        chi = b > chi ? b : chi;
    }
    lo -= R;
    hi += R;
    clo -= R;
    chi += R;
    // The frustum cull on the quad's bounds.
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    const float al[4] = {hi, hi, lo, lo}, cr[4] = {clo, chi, clo, chi};
    float cx[4], cy[4];
    for (unsigned k = 0; k < 4; ++k) {
        cx[k] = hx + ux * al[k] + nx * cr[k];
        cy[k] = hy + uy * al[k] + ny * cr[k];
        x0 = cx[k] < x0 ? cx[k] : x0;
        x1 = cx[k] > x1 ? cx[k] : x1;
        y0 = cy[k] < y0 ? cy[k] : y0;
        y1 = cy[k] > y1 ? cy[k] : y1;
    }
    if (x1 < 0.f || y1 < 0.f || x0 > v.width || y0 > v.height) {
        ++n->offscreen;
        return false;
    }
    // Corner depths along the axis: 1 / z is linear in screen space along a world segment, so each corner takes the
    // segment's 1 / z extrapolated to its position along the axis (the corners R past the head lie beyond it), kept
    // positive; the streak lies at the head's depth. The depth test then cuts a bolt where its own axis passes a hull.
    // (never nearer than the eye plane: an incoming bolt's corners ahead of its head would cross the near plane and be
    // clipped through the disc)
    const float izh = 1.f / hv[2], izb = 1.f / bv[2], iz_min = 0.5f * (izh < izb ? izh : izb), iz_max = 1.f / znear;
    for (unsigned k = 0; k < 4; ++k) {
        const float f = A > 0.5f ? ((cx[k] - hx) * ax + (cy[k] - hy) * ay) / A : 0.f;
        float iz = izh + (izb - izh) * f;
        iz = iz > iz_min ? iz : iz_min;
        iz = iz < iz_max ? iz : iz_max;
        const float z = 1.f / iz;
        q->clip[k][0] = (cx[k] * (2.f / v.width) - 1.f) * z;
        q->clip[k][1] = (1.f - cy[k] * (2.f / v.height)) * z;
        q->clip[k][2] = v.m22 * z + v.m32;
        q->clip[k][3] = z;
    }
    const float tint_c[3] = {look.tint[0], look.tint[1], look.tint[2]};
    float (*p)[4] = q->p;
    p[0][0] = hx;
    p[0][1] = hy;
    p[0][2] = ax;
    p[0][3] = ay;
    p[1][0] = A;
    p[1][1] = Lpx;
    p[1][2] = Tpx;
    p[1][3] = D;
    p[2][0] = sdx;
    p[2][1] = sdy;
    // lines: the back end's E_h (their streak's rest length is the core's, p1.y); orbs: where the stretch begins
    p[2][2] = law.kind == 0 ? law.e_halo * kb : stretch_at;
    p[2][3] = g_s;
    p[3][0] = wh_e;
    p[3][1] = wb_e;
    p[3][2] = sh;
    p[3][3] = sb;
    p[4][0] = tint_c[0];
    p[4][1] = tint_c[1];
    p[4][2] = tint_c[2];
    p[4][3] = law.h;
    p[5][0] = law.code;
    p[5][1] = fade;
    p[5][2] = law.tail_root;
    p[5][3] = law.tail_gain;
    p[6][0] = law.kind == 0 ? law.e_body : in.seed; // line kinds: the body radiance E_b; orbs and waves: the seed
    p[6][1] = c;
    p[6][2] = S_px;
    p[6][3] = law.e_halo * kh; // the head's E_h (an orb's or wave's only)
    // The rim: drawn where its band is at least rim_min_px wide (line and orb kinds, full mode; the pixel band of a
    // narrow bolt is 3 px, so every line and orb carries one).
    *rim_ok = rim;
    return true;
}
// The pixel constants (weapon_bolt_ps.hlsl c0..c3; c1.z and c3.x the pass sets per draw). c3.y the orbs' fewest lobes.
inline void bolt_pixel_constants(std::uint32_t now_ms, bool simple, float out[4][4]) noexcept {
    out[0][0] = core::look::core_e;
    out[0][1] = bolt::e_body;
    out[0][2] = 0.f;
    out[0][3] = bolt::far_flag_k;
    out[1][0] = bolt::head_gain;
    out[1][1] = float(now_ms % 1000000u) * 0.001f;
    out[1][2] = 0.f;
    out[1][3] = simple ? 1.f : 0.f;
    out[2][0] = bolt::rim_dark;
    out[2][1] = 2.f * bolt::rim_band_w[0];
    out[2][2] = 2.f * bolt::rim_band_w[1];
    out[2][3] = 0.4f;
    out[3][0] = 0.f;
    out[3][1] = bolt::orb_lobes;
    out[3][2] = simple ? 0.f : bolt::orb_lobe_amp;
    out[3][3] = bolt::orb_lobe_turns;
}
// The frame's records (the current list, the previous one for the streak) into quads. Records are read only from the
// lists the hooks wrote (never from game memory): the row's look is the caller's (looks[i], valid 0 skips).
struct BoltFrameOut {
    renderer::WeaponBoltQuad* quads = nullptr; // capacity renderer::weapon_bolt_quads, in kind order on return
    unsigned lines = 0, orbs = 0, waves = 0, rim_lines = 0, rim_orbs = 0;
};
struct BoltScratch {
    renderer::WeaponBoltQuad quad[renderer::weapon_bolt_quads];
    std::uint8_t kind[renderer::weapon_bolt_quads];
    std::uint8_t rim[renderer::weapon_bolt_quads];
};
inline BoltInput bolt_input(const core::BoltRecord& r, const core::BoltRecord* prev, const BoltLook& look,
                            const BoltView& v) noexcept {
    BoltInput in;
    const double s = double(core::world_scale);
    for (unsigned i = 0; i < 3; ++i) in.head[i] = double(r.pos[i]) * s;
    // The travel: the velocity's direction, else the rotation's z row, else the displacement since the last frame.
    float d[3] = {float(r.vel[0]), float(r.vel[1]), float(r.vel[2])};
    float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (!(l2 > 0.f)) {
        for (unsigned i = 0; i < 3; ++i) d[i] = float(r.zrow[i]);
        l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    }
    if (!(l2 > 0.f) && prev) {
        for (unsigned i = 0; i < 3; ++i) d[i] = float(core::wrap_sub(r.pos[i], prev->pos[i]));
        l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    }
    if (l2 > 0.f) {
        const float il = 1.f / scalar::sqrt(l2);
        for (unsigned i = 0; i < 3; ++i) in.dir[i] = d[i] * il;
    }
    if (prev) {
        in.prev = true;
        for (unsigned i = 0; i < 3; ++i) in.prev_head[i] = double(prev->pos[i]) * s;
    }
    // The speed: the table's range over the record's lifetime, else the displacement over the frame's game step.
    const std::uint32_t life = r.expiry - r.created;
    if (look.range_m > 0.f && r.expiry > r.created && life < 600000u)
        in.v_mps = look.range_m * 1000.f / float(std::int32_t(life));
    else if (prev && v.step_ms) {
        float dd = 0.f;
        for (unsigned i = 0; i < 3; ++i) {
            const float x = float(core::wrap_sub(r.pos[i], prev->pos[i]));
            dd += x * x;
        }
        in.v_mps = scalar::sqrt(dd) / core::engine_units_per_metre * 1000.f / float(v.step_ms);
    }
    const std::int32_t age = std::int32_t(v.now - r.created), remain = std::int32_t(r.expiry - v.now);
    in.age_ms = r.created ? float(age > 0 ? age : 0) : 1e6f;
    in.remain_ms = r.expiry ? float(remain) : 1e6f;
    in.own = r.own != 0;
    in.seed = float(mix32(r.obj ^ r.created) >> 8) * (1.f / 16777216.f);
    if (look.beam) in.length_m = r.length > 0 ? float(r.length) / core::engine_units_per_metre : 0.f;
    return in;
}
inline void bolt_order(BoltScratch& s, unsigned count, BoltFrameOut& out) noexcept {
    // Kind order with the rim-eligible first in the line and orb ranges: a counting pass, then the copies.
    unsigned n[5] = {0, 0, 0, 0, 0}; // line rim, line, orb rim, orb, wave
    for (unsigned i = 0; i < count; ++i) ++n[s.kind[i] == 2 ? 4 : s.kind[i] * 2 + (s.rim[i] ? 0 : 1)];
    unsigned at[5];
    at[0] = 0;
    for (unsigned k = 1; k < 5; ++k) at[k] = at[k - 1] + n[k - 1];
    for (unsigned i = 0; i < count; ++i) {
        const unsigned k = s.kind[i] == 2 ? 4 : s.kind[i] * 2 + (s.rim[i] ? 0 : 1);
        out.quads[at[k]++] = s.quad[i];
    }
    out.rim_lines = n[0];
    out.lines = n[0] + n[1];
    out.rim_orbs = n[2];
    out.orbs = n[2] + n[3];
    out.waves = n[4];
}
// The whole frame: `records` this frame's list, `prev` the previous one (its index built here), `keep` the parity
// rule's verdict per record (null: all). Looks by row from the caller's cache function.
template <class LookFn>
inline BoltCounts build_bolts(const BoltView& v, const core::BoltRecord* records, unsigned count,
                              const core::BoltRecord* prev, unsigned prev_count, BoltPrevIndex& index,
                              std::uint32_t stamp, const std::uint8_t* keep, LookFn&& look_of, BoltScratch& s,
                              BoltFrameOut& out) noexcept {
    BoltCounts n;
    n.listed = count;
    bolt_prev_index(index, prev, prev_count, stamp);
    unsigned m = 0;
    for (unsigned i = 0; i < count && m < renderer::weapon_bolt_quads; ++i) {
        if (keep && !keep[i]) {
            ++n.parity_dropped;
            continue;
        }
        const core::BoltRecord& r = records[i];
        const BoltLook& look = look_of(r.row);
        if (!look.valid) {
            ++n.invalid;
            continue;
        }
        const int pi = bolt_prev_find(index, r.obj, r.created);
        const core::BoltRecord* pr = pi >= 0 ? &prev[pi] : nullptr;
        const BoltInput in = bolt_input(r, pr, look, v);
        const BoltLaw law = bolt_law(look, in.v_mps, v.simple);
        bool rim = false;
        if (!bolt_quad(v, look, law, in, &s.quad[m], &rim, &n)) continue;
        s.kind[m] = law.kind;
        s.rim[m] = rim ? 1 : 0;
        n.beams += look.beam;
        ++m;
    }
    bolt_order(s, m, out);
    n.drawn = m;
    n.lines = out.lines;
    n.orbs = out.orbs;
    n.waves = out.waves;
    n.rim_lines = out.rim_lines;
    n.rim_orbs = out.rim_orbs;
    return n;
}

// ---------------------------------------------------------------------------------------------- parity (unknown 14)
// The late draw's instances (the locked-prefix scan's bodies: centroid in pixels, its on-screen radius r, the body's
// vertex period) against the tap list's records (head in pixels, row). A record matches the nearest instance within
// parity_tol_px + that instance's r (the head lies up to a body's half length from the body's centroid, so the tolerance
// scales with the bolt's on-screen length), searched among at most parity_window instances of the x-sorted list from the
// record's x - (tol + the largest r): O(m log m + n window). Every frame the first parity_learn records with an
// unambiguous match (the next instance at least twice as far) learn their row's body period. Equal counts keep every
// record; otherwise a record is kept when it matches, and once its row's period is known, only an instance of that
// period. Returns the records dropped.
struct BoltInstance {
    float x = 0.f, y = 0.f;
    float r = 0.f; // on-screen radius (px) of the body
    std::uint32_t period = 0;
};
struct BoltRowPeriods {
    std::uint32_t period[core::table_capacity]{};
};
struct BoltParityScratch {
    std::uint8_t order[bolt::parity_instances];
    float key[bolt::parity_instances];
};
struct BoltParityStats {
    unsigned matched = 0, learned = 0, compared = 0;
};
inline unsigned bolt_parity(const float* rec_px, const std::uint16_t* rec_row, unsigned n, const BoltInstance* inst,
                            unsigned m, float tol_px, BoltRowPeriods* learned, std::uint8_t* keep, BoltParityScratch& sc,
                            BoltParityStats* stats = nullptr) noexcept {
    if (m > bolt::parity_instances) m = bolt::parity_instances;
    float r_max = 0.f;
    for (unsigned j = 0; j < m; ++j) {
        sc.order[j] = std::uint8_t(j);
        sc.key[j] = inst[j].x;
        r_max = inst[j].r > r_max ? inst[j].r : r_max;
    }
    renderer::sort_by_key(sc.order, m, sc.key);
    const float reach = tol_px + r_max;
    unsigned dropped = 0, compared = 0, matched = 0, learnt = 0;
    for (unsigned i = 0; i < n; ++i) {
        const float x = rec_px[2 * i], y = rec_px[2 * i + 1];
        const std::uint32_t want = rec_row[i] < core::table_capacity && learned ? learned->period[rec_row[i]] : 0u;
        // the first sorted instance with x >= x - reach (binary search)
        unsigned lo = 0, hi = m;
        while (lo < hi) {
            const unsigned mid = (lo + hi) >> 1;
            if (sc.key[sc.order[mid]] < x - reach)
                lo = mid + 1;
            else
                hi = mid;
        }
        float best = 3.4e38f, second = 3.4e38f;
        unsigned at = m;
        for (unsigned k = lo, seen = 0; k < m && seen < bolt::parity_window; ++k, ++seen) {
            const BoltInstance& c = inst[sc.order[k]];
            if (c.x > x + reach) break;
            ++compared;
            if (n != m && want && c.period != want) continue;
            const float t = tol_px + c.r, dx = c.x - x, dy = c.y - y, d2 = dx * dx + dy * dy;
            if (d2 > t * t) continue;
            // nearest by distance over the instance's own tolerance (a long body's centroid lies farther from the head)
            const float q = d2 / (t * t);
            if (q < best) {
                second = best;
                best = q;
                at = sc.order[k];
            } else if (q < second)
                second = q;
        }
        const bool hit = at < m;
        matched += hit;
        if (hit && i < bolt::parity_learn && learned && rec_row[i] < core::table_capacity && second >= 4.f * best &&
            (n == m || !want)) {
            learned->period[rec_row[i]] = inst[at].period;
            ++learnt;
        }
        keep[i] = n == m || hit ? 1 : 0;
        dropped += keep[i] ? 0u : 1u;
    }
    if (stats) {
        stats->matched = matched;
        stats->learned = learnt;
        stats->compared = compared;
    }
    return dropped;
}

// ---------------------------------------------------------------------------------------------- the frame's ownership (5.1)
// Decided once per frame, at its first bullet-producer draw and before any is suppressed (review 2026-11-07, findings 3
// and 13): ours takes the frame when the in-place draw is armed (bolts_mode, the screen_emission_additive route, the
// HDR / TAA path, the camera, the pass, the hooks, no failure latch), the tap listed bullets, and a late copy is expected:
// this draw is late (the scene wrote depth), or the previous frame had a late bullet draw (an early copy waits for it).
// In a frame ours took, the first late copy draws ours (when its draw is possible here) and every other bullet draw is
// suppressed, whatever the additive route would have decided for it (a refused sRGB-sampler draw included). A frame
// ours did not take draws every copy natively (nothing suppressed). If the late copy cannot draw ours (its draw state or
// a failed draw), it and the rest of the frame draw natively; early copies already suppressed are the single-copy rule's
// drop (the native late copy carries the bullets). A frame whose expected late copy never came lost its early copies for
// that frame; the next frame then waits for a late copy before taking any.
enum class BoltAction : std::uint8_t { native, draw, suppress };
struct BoltOwnership {
    std::uint8_t decision = 0; // this frame: 0 undecided, 1 ours, 2 native
    bool drew = false;         // ours drew this frame
    bool late_seen = false;    // a late bullet-producer draw this frame
    bool late_prev = false;    // the previous frame had one
    bool suppressed = false;   // a draw was suppressed this frame
};
inline BoltAction bolt_take(BoltOwnership& o, bool armed, bool listed, bool late, bool drawable) noexcept {
    if (late) o.late_seen = true;
    if (!o.decision) o.decision = armed && listed && (late || o.late_prev) ? 1 : 2;
    if (o.decision != 1) return BoltAction::native;
    if (o.drew || !late) {
        o.suppressed = true;
        return BoltAction::suppress;
    }
    if (!drawable) {
        o.decision = 2;
        return BoltAction::native;
    }
    return BoltAction::draw;
}
// The draw's outcome: drew (every later bullet draw suppressed) or failed (the rest of the frame native).
inline void bolt_take_result(BoltOwnership& o, bool drew) noexcept {
    if (drew)
        o.drew = true;
    else
        o.decision = 2;
}
// Present: the next frame starts undecided with this frame's late copy as its expectation.
inline void bolt_take_frame_end(BoltOwnership& o) noexcept {
    o.late_prev = o.late_seen;
    o.decision = 0;
    o.drew = o.late_seen = o.suppressed = false;
}

// ---------------------------------------------------------------------------------------------- the muzzle (section 3)
struct MuzzleCounts {
    unsigned live = 0, drawn = 0, discs = 0, cones = 0, stars = 0, vents = 0, blooms = 0, glows = 0, puffs = 0;
    unsigned orphans = 0, offscreen = 0, capped = 0, clustered = 0, floored = 0, cockpit = 0;
    unsigned native = 0;  // live shots the native muzzle drew (the hook forwarded): not built
    unsigned starved = 0; // elements a full batch refused (Present's muzzle latch drops: the next shots forward)
    unsigned endon = 0;   // muzzles drawn under the view rule (the star for the cone)
    float max_life_ms = 0.f;
};
struct MuzzleTuning {
    bool glow = true, smoke = true, star = true, cone = true, bloom = true, vents = true;
    bool endon_star = true; // the view rule (core::muzzle::endon_cos); off: the cone at every angle (the cone gates)
    float own_gain = core::own_cockpit_gain;
    unsigned min_frames = core::look::min_frames;
};
// The muzzle record's pose this frame: the gun in view space, its axis and side (view, unit), the shooter's radius (m).
struct MuzzlePose {
    float pos[3]{}, axis[3]{0.f, 0.f, 1.f}, side[3]{1.f, 0.f, 0.f};
    float radius_m = 10.f;
    bool own = false;
};
inline bool muzzle_pose(Reader rd, const core::MuzzleRecord& m, const Camera& cam, std::uint32_t own_node,
                        MuzzlePose* out) noexcept {
    std::uint32_t node = 0, handle = 0, flags = 0;
    std::int32_t pos[3]{}, rows[12]{}, radius = 0;
    if (!rd(m.ship + core::layout::obj_node, &node, 4) || node != m.node ||
        !rd(node + core::layout::node_handle, &handle, 4) || handle != m.handle ||
        !rd(m.ship + core::layout::obj_flags, &flags, 4) || (flags & core::layout::obj_dying) ||
        !rd(node + core::layout::node_pos, pos, 12) || !rd(node + core::layout::node_rot, rows, 48) ||
        !rd(m.ship + core::layout::obj_radius, &radius, 4))
        return false;
    std::int32_t off[3] = {m.pos[0], m.pos[1], m.pos[2]}, p[3];
    core::rotate_out(rows, off, p);
    const double s = double(core::world_scale);
    const double w[3] = {double(core::wrap_add(pos[0], p[0])) * s, double(core::wrap_add(pos[1], p[1])) * s,
                         double(core::wrap_add(pos[2], p[2])) * s};
    to_view(cam, w, out->pos);
    // The gun's z row (the axis) and x row (the side) in the ship frame (16.16, unknown 6), into the world, then view.
    auto row_world = [&](unsigned r, float o[3]) {
        std::int32_t a[3] = {m.orient[4 * r], m.orient[4 * r + 1], m.orient[4 * r + 2]}, b[3];
        core::rotate_out(rows, a, b);
        float f[3] = {float(b[0]), float(b[1]), float(b[2])};
        const float l2 = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
        if (!(l2 > 0.f)) return false;
        const float il = 1.f / scalar::sqrt(l2);
        for (unsigned i = 0; i < 3; ++i) f[i] *= il;
        rotate_world_to_view(cam, f, o);
        return true;
    };
    if (!row_world(2, out->axis)) return false;
    if (!row_world(0, out->side)) {
        out->side[0] = 1.f;
        out->side[1] = out->side[2] = 0.f;
    }
    out->radius_m = float(radius) / core::engine_units_per_metre;
    out->own = own_node && node == own_node;
    return true;
}
// One cone sprite (the sprite program's kind 4): base c (view), along a unit axis for `len` view units, base radius r
// (view units), radiance e, colour (linear, packed with alpha 1/255).
inline bool cone_quad(const HullView& f, const float c[3], const float a[3], float len, float r, float e,
                      const float colour[3], renderer::WeaponSpriteVertex* out) noexcept {
    const float tip[3] = {c[0] + a[0] * len, c[1] + a[1] * len, c[2] + a[2] * len};
    if (!(c[2] > f.near_z) || !(tip[2] > f.near_z)) return false;
    const Screen sc = screen_of(f);
    const Px pb = to_px(f, sc, c), pt = to_px(f, sc, tip);
    float dx = pt.x - pb.x, dy = pt.y - pb.y;
    const float l = scalar::sqrt(dx * dx + dy * dy);
    const float px_x = 0.5f * f.width * f.cam.m00;
    float rb = r * px_x * pb.iz;
    if (rb < 0.5f * core::look::floor_lit_px) rb = 0.5f * core::look::floor_lit_px;
    if (l > 1e-3f) {
        dx /= l;
        dy /= l;
    } else {
        dx = 1.f;
        dy = 0.f;
    }
    const float nx = -dy, ny = dx;
    const float L = l > rb ? l : rb; // an end-on cone keeps a disc of its base
    const float u[4] = {0.f, 0.f, 1.f, 1.f}, v[4] = {-1.f, 1.f, -1.f, 1.f};
    const std::uint32_t col = pack_colour(colour, 1.f / 255.f);
    for (unsigned k = 0; k < 4; ++k) {
        const float along = u[k] * L - (u[k] < 0.5f ? 0.25f * rb : 0.f);
        const float x = pb.x + dx * along + nx * v[k] * rb, y = pb.y + dy * along + ny * v[k] * rb;
        const float z = u[k] < 0.5f ? c[2] : tip[2];
        px_clip(f, sc, x, y, z, out[k].a);
        out[k].b[0] = u[k] < 0.5f ? -0.25f * rb / L : 1.f;
        out[k].b[1] = v[k];
        out[k].b[2] = e;
        out[k].b[3] = r;
        out[k].c[0] = 4.f;
        out[k].c[1] = r;
        out[k].c[2] = 0.f;
        out[k].colour = col;
    }
    return true;
}
// The muzzle disc: the sprite batch's flash (core radius r, the halo to R, view units) at the gun, no eye bias (the hull
// flash's bias keeps a contact flash out of its hull; the gun's disc sits on it). The core never under half the flash
// floor in pixels. False off the near plane.
inline bool disc_quad(const HullView& f, const float c[3], float r, float R, const float colour[3], float radiance,
                      float core_share, renderer::WeaponSpriteVertex* v) noexcept {
    if (!(c[2] > f.near_z)) return false;
    const Screen sc = screen_of(f);
    const Px p = to_px(f, sc, c);
    const float px_x = 0.5f * f.width * f.cam.m00;
    float core_px = r * px_x * p.iz, halo_px = R * px_x * p.iz;
    if (core_px < 0.5f * core::look::floor_flash_px) core_px = 0.5f * core::look::floor_flash_px;
    if (halo_px < core_px) halo_px = core_px;
    const float r_view = halo_px * c[2] / px_x;
    const std::uint32_t col = pack_colour(colour, core_px / halo_px);
    const float sx[4] = {-1.f, 1.f, -1.f, 1.f}, sy[4] = {1.f, 1.f, -1.f, -1.f};
    for (unsigned q = 0; q < 4; ++q) {
        px_clip(f, sc, p.x + sx[q] * halo_px, p.y - sy[q] * halo_px, c[2], v[q].a);
        v[q].b[0] = sx[q];
        v[q].b[1] = sy[q];
        v[q].b[2] = radiance;
        v[q].b[3] = r_view;
        v[q].c[0] = v[q].c[1] = 0.f;
        v[q].c[2] = core_share;
        v[q].colour = col;
    }
    return true;
}
struct MuzzleBuffers {
    renderer::WeaponSpriteVertex* sprites = nullptr; // discs and cones (the hull stage's batch, appended)
    unsigned sprite_cap = 0, sprite_quads = 0;
    renderer::WeaponGlowVertex* glows = nullptr;     // the hull glows
    unsigned glow_cap = 0, glow_boxes = 0;
    renderer::WeaponFireballVertex* fireballs = nullptr; // the plasma blooms
    unsigned fireball_cap = 0, fireball_boxes = 0;
    renderer::WeaponGlowVertex* scorches = nullptr;  // the smoke puffs (the multiply decals, surface only)
    unsigned scorch_cap = 0, scorch_boxes = 0;
};
// The live elements' life (game ms) of a class: its flash, the heavy afterglow, the smoke.
inline float muzzle_life_ms(unsigned cls) noexcept {
    float l = mz::class_life[cls < 5 ? cls : 0];
    if (cls == mz::heavy) l = mz::afterglow_ms > l ? mz::afterglow_ms : l;
    const float smoke = mz::class_smoke[cls < 5 ? cls : 0][3];
    return smoke > l ? smoke : l;
}
// One muzzle's elements at age `age_ms` (game). Sizes in the weapon's w (look.w_m), capped by the shooter's radius.
// `cluster` the broadside factor s(N) on its glow and halo; `gain` the cockpit gain. Appends to b; counts in n.
inline void muzzle_elements(const HullView& f, const MuzzlePose& pose, const BoltLook& look, const core::MuzzleRecord& m,
                            float age_ms, float cluster, float gain, const MuzzleTuning& t, MuzzleBuffers& b,
                            MuzzleCounts* n) noexcept {
    const unsigned cls = look.mclass < 5 ? look.mclass : 0u;
    const float upm = core::engine_units_per_metre * core::world_scale;
    const float w = look.w_m * upm; // view units
    const float Rs = pose.radius_m * upm;
    const float cap_core = mz::cap_core_rs * Rs, cap_flame = mz::cap_flame_rs * Rs;
    const float floor_ms = float(t.min_frames) * float(f.step_ms);
    const std::uint32_t h = mix32(m.seed);
    const float stagger = 1.f + core::stagger * (unit_of(h) - 0.5f);
    auto life_of = [&](float law) {
        const float l = law * stagger;
        return l > floor_ms ? l : floor_ms;
    };
    const float life = life_of(mz::class_life[cls]);
    // Breathing (3.2): x (1 + a (2 h - 1)), a = breath x saturate(dt_shot / (3 steps) - 1): full when a shot's three frames
    // are its own, none when shots arrive within three frames (the gate's steady flame under SETA; the note's literal
    // saturate(dt / 3 steps) leaves 9 % frame to frame at SETA 6, 10 Hz: fixture muzzle_breathing_suppressed).
    const float dt = m.prev_t0 ? float(std::int32_t(m.t0 - m.prev_t0)) : 1e6f;
    const float steps3 = 3.f * float(f.step_ms > 0 ? f.step_ms : 16u);
    const float ratio = dt / steps3 - 1.f;
    const float a = mz::breath * (ratio > 1.f ? 1.f : ratio > 0.f ? ratio : 0.f);
    const float breathe = 1.f + a * (2.f * unit_of(mix32(h + m.shots)) - 1.f);
    const float* tint = look.tint;
    const bool flash_live = age_ms < life;
    const float fl = flash_live ? 1.f - age_ms / life : 0.f;
    // Room in the batches: an element a full batch refuses counts as starved (the muzzle latch).
    auto sprite_room = [&] {
        if (b.sprite_quads < b.sprite_cap) return true;
        ++n->starved;
        return false;
    };
    // The view rule: the gun axis within 30 degrees of the eye ray (either way).
    const float pd = scalar::sqrt(pose.pos[0] * pose.pos[0] + pose.pos[1] * pose.pos[1] + pose.pos[2] * pose.pos[2]);
    const float ac = pd > 1e-6f ? (pose.axis[0] * pose.pos[0] + pose.axis[1] * pose.pos[1] + pose.axis[2] * pose.pos[2]) / pd
                                : 0.f;
    const bool endon = t.endon_star && (ac >= mz::endon_cos || -ac >= mz::endon_cos) && mz::class_star[cls] <= 0.f;
    // The core disc (and the heavy's afterglow).
    {
        const float dw = mz::class_disc[cls][0] * w;
        float r = 0.5f * dw;
        if (r > cap_core) {
            r = cap_core;
            ++n->capped;
        }
        float e = mz::class_disc[cls][2] * fl;
        const float wh = mz::class_disc[cls][1];
        float core_share = 1.f - wh; // the core's tint share (0 white)
        if (cls == mz::heavy && !flash_live) {
            const float ag = life_of(mz::afterglow_ms);
            if (age_ms < ag) {
                const float x = (age_ms - life) / (ag - life > 1.f ? ag - life : 1.f);
                e = mz::afterglow_e * (1.f - x);
                core_share = x;
            }
        }
        if (e > 0.f && sprite_room()) {
            const float halo_r = r + 2.f * mz::disc_halo_w * w * cluster;
            const float colour[3] = {tint[0], tint[1], tint[2]};
            if (disc_quad(f, pose.pos, r, halo_r, colour, e * gain, core_share, b.sprites + 4 * b.sprite_quads)) {
                ++b.sprite_quads;
                ++n->discs;
            }
        }
    }
    // The flame: a forward cone (energy, ballistic, heavy with its 40 ms rise); end-on (the view rule) the hull burst's
    // four-arm star of the cone's length about the disc instead (energy, heavy: the classes without a star of their own).
    if (t.cone && flash_live && mz::class_flame[cls][0] > 0.f && sprite_room()) {
        float len = mz::class_flame[cls][0] * w * breathe;
        if (cls == mz::heavy) {
            const float rise = mz::rise_ms > floor_ms ? mz::rise_ms : floor_ms;
            len *= age_ms < rise ? age_ms / rise : 1.f;
        }
        if (len > cap_flame) {
            len = cap_flame;
            ++n->capped;
        }
        const float base = 0.5f * mz::class_flame[cls][1] * w;
        if (endon) {
            // the star's unit is the disc's core radius, its arms (hull::star_arm_len x A) A = the cone's length; its
            // angle hashed per shot in [0, pi / 2) (the unequal arms take the rest of the turn)
            const float r = 0.5f * mz::class_disc[cls][0] * w < cap_core ? 0.5f * mz::class_disc[cls][0] * w : cap_core;
            const float angle = 1.5707963f * unit_of(mix32(h ^ 0x3c6ef372u));
            float screens = 0.f;
            if (len > 0.f && r > 0.f &&
                star_quad(f, screen_of(f), pose.pos, r, 0.f, len / r, angle, tint, mz::star_e * fl * gain,
                          b.sprites + 4 * b.sprite_quads, &screens)) {
                ++b.sprite_quads;
                ++n->stars;
                ++n->endon;
            }
        } else if (len > 0.f && cone_quad(f, pose.pos, pose.axis, len, base < cap_core ? base : cap_core,
                                          mz::class_flame[cls][2] * fl * gain, tint, b.sprites + 4 * b.sprite_quads)) {
            ++b.sprite_quads;
            ++n->cones;
        }
    }
    // The star (ballistic, flak): four unequal arms rotated per shot, as cones from the core.
    if (t.star && flash_live && mz::class_star[cls] > 0.f) {
        const float rot = 6.2831853f * unit_of(mix32(h ^ 0x51ed270bu));
        // the arms in the plane across the gun axis: side and up = axis x side
        const float* s = pose.side;
        const float* ax = pose.axis;
        const float up[3] = {ax[1] * s[2] - ax[2] * s[1], ax[2] * s[0] - ax[0] * s[2], ax[0] * s[1] - ax[1] * s[0]};
        // cos / sin of rot (its quarter turn reduced into [0, pi / 2): sincos_quarter's 1e-8), then each arm's exact
        // quarter turn (cs, sn) -> (-sn, cs); the old truncated series on [-pi, pi] was off by 4.4 degrees / 2.2 %
        // at the interval's ends (review note 11)
        float cs, sn;
        {
            const unsigned quarter = unsigned(rot * (1.f / 1.5707963f)) & 3u;
            float a = rot - 1.5707963f * float(quarter);
            a = a < 0.f ? 0.f : a > 1.5707963f ? 1.5707963f : a;
            float qs, qc;
            sincos_quarter(a, &qs, &qc);
            cs = qc;
            sn = qs;
            for (unsigned k = 0; k < quarter; ++k) {
                const float t0 = cs;
                cs = -sn;
                sn = t0;
            }
        }
        for (unsigned arm = 0; arm < 4; ++arm) {
            if (!sprite_room()) break;
            if (arm) {
                const float t0 = cs;
                cs = -sn;
                sn = t0;
            }
            const float dir[3] = {s[0] * cs + up[0] * sn, s[1] * cs + up[1] * sn, s[2] * cs + up[2] * sn};
            float len = mz::star_arms[arm] * mz::class_star[cls] * w * breathe;
            if (len > cap_core) {
                len = cap_core;
                ++n->capped;
            }
            if (cone_quad(f, pose.pos, dir, len, 0.5f * mz::star_width_w * w, mz::star_e * fl * gain, tint,
                          b.sprites + 4 * b.sprite_quads)) {
                ++b.sprite_quads;
                ++n->stars;
            }
        }
    }
    // The heavy's vents: two jets across the gun axis (the side row, +-), 150 ms.
    if (t.vents && cls == mz::heavy) {
        const float vl = life_of(mz::vent_ms);
        if (age_ms < vl)
            for (int sgn = -1; sgn <= 1 && sprite_room(); sgn += 2) {
                const float dir[3] = {pose.side[0] * float(sgn), pose.side[1] * float(sgn), pose.side[2] * float(sgn)};
                float len = mz::vent_w * w;
                if (len > cap_flame) len = cap_flame;
                if (cone_quad(f, pose.pos, dir, len, 0.25f * mz::vent_w * w, mz::vent_e * (1.f - age_ms / vl) * gain,
                              tint, b.sprites + 4 * b.sprite_quads)) {
                    ++b.sprite_quads;
                    ++n->vents;
                }
            }
    }
    // The plasma bloom: the burst's noise body in the tint (a fireball impostor of radius 0.5 -> 1.0 w).
    if (t.bloom && cls == mz::plasma && flash_live && b.fireball_boxes >= b.fireball_cap) ++n->starved;
    if (t.bloom && cls == mz::plasma && flash_live && b.fireball_boxes < b.fireball_cap) {
        const float x = age_ms / life;
        float R = (mz::bloom_w[0] + (mz::bloom_w[1] - mz::bloom_w[0]) * x) * w;
        if (R > cap_flame) R = cap_flame;
        float rect[4];
        bool full = false;
        if (sphere_rect(f, pose.pos, R, rect, &full)) {
            // the burst body's law (weapon_fireball_ps.hlsl since the burst rebuild): look = (T, E, age fraction, seed),
            // disc = (rotation, lobe seed, advection in noise cells, octaves), the tint's full share
            const float lk[4] = {0.3f, mz::bloom_e * fl * gain, x, float(h & 0xffu)};
            const float tn[4] = {tint[0], tint[1], tint[2], 1.f};
            const float dk[4] = {6.2831853f * unit_of(mix32(h ^ 0x2545f491u)), float((h >> 8) & 0xffu), 2.f * x, 2.f};
            fireball_quad(f, pose.pos, R, rect, full, lk, tn, dk, b.fireballs + 4 * b.fireball_boxes);
            ++b.fireball_boxes;
            ++n->blooms;
        }
    }
    // The hull glow (the light on the shooter's hull, 2.8): R, E in the tint, the cluster factor.
    if (t.glow && flash_live && b.glow_boxes >= b.glow_cap) ++n->starved;
    if (t.glow && flash_live && b.glow_boxes < b.glow_cap) {
        const float R = mz::class_glow[cls][0] * w;
        float rect[4];
        bool full = false;
        if (sphere_rect(f, pose.pos, R, rect, &full)) {
            const float e = mz::class_glow[cls][1] * fl * cluster * gain;
            const float col[3] = {tint[0] * e, tint[1] * e, tint[2] * e};
            glow_quad(f, pose.pos, R, rect, full, col, b.glows + 4 * b.glow_boxes);
            ++b.glow_boxes;
            ++n->glows;
        }
    }
    // The smoke puff (ballistic, heavy, flak): the scorch-state multiply on the surface, drifting along the gun axis; not
    // in the own cockpit.
    if (t.smoke && b.scorches && mz::class_smoke[cls][3] > 0.f && !(pose.own && gain < 1.f)) {
        const float sl = life_of(mz::class_smoke[cls][3]);
        if (age_ms < sl && b.scorch_boxes >= b.scorch_cap) ++n->starved;
        if (age_ms < sl && b.scorch_boxes < b.scorch_cap) {
            const float x = age_ms / sl;
            const float R = (mz::class_smoke[cls][0] + (mz::class_smoke[cls][1] - mz::class_smoke[cls][0]) * x) * w;
            const float drift = mz::class_smoke[cls][4] * w * x;
            const float c[3] = {pose.pos[0] + pose.axis[0] * drift, pose.pos[1] + pose.axis[1] * drift,
                                pose.pos[2] + pose.axis[2] * drift};
            float rect[4];
            bool full = false;
            if (sphere_rect(f, c, R, rect, &full)) {
                const float s0 = mz::class_smoke[cls][2] * (1.f - x);
                const float col[3] = {s0, s0, s0};
                glow_quad(f, c, R, rect, full, col, b.scorches + 4 * b.scorch_boxes);
                ++b.scorch_boxes;
                ++n->puffs;
            }
        }
    }
    ++n->drawn;
}
// Every live muzzle record the hook marked ours (the native muzzle skipped for it): its pose (validated), its age, the
// broadside cluster s(N) (N = the shooter's live muzzles within 2 R_glow), the cockpit gain on the own ship.
// Present's muzzle latch for the next frame's shots (weapon_effects_bolt.h, BoltHookState::muzzle_owned): the bolts armed,
// this frame's stage built and drew its muzzles, and no element went short of room (so a skipped native muzzle is
// always drawn by ours: never two flashes, never none).
inline bool muzzle_latch(bool armed, bool stage_drew, unsigned starved) noexcept {
    return armed && stage_drew && !starved;
}
template <class LookFn>
inline MuzzleCounts build_muzzles(Reader rd, const core::MuzzleRecord* records, unsigned count, const HullView& f,
                                  const MuzzleTuning& t, LookFn&& look_of, MuzzleBuffers& b) noexcept {
    MuzzleCounts n;
    MuzzlePose poses[mz::record_capacity];
    std::uint8_t live[mz::record_capacity]{};
    float ages[mz::record_capacity]{};
    const unsigned cap = count < mz::record_capacity ? count : mz::record_capacity;
    for (unsigned i = 0; i < cap; ++i) {
        const core::MuzzleRecord& m = records[i];
        if (!m.sequence) continue;
        const BoltLook& look = look_of(m.row);
        if (!look.valid) continue;
        const float age = float(std::int32_t(f.now - m.t0));
        const float floor_ms = float(t.min_frames) * float(f.step_ms);
        float life = muzzle_life_ms(look.mclass) * (1.f + 0.5f * core::stagger);
        life = life > floor_ms ? life : floor_ms;
        if (age < 0.f || age >= life) continue;
        if (!m.ours) { // the hook forwarded this shot: the native muzzle draws it (never two flashes)
            ++n.native;
            continue;
        }
        if (!muzzle_pose(rd, m, f.cam, f.own_node, &poses[i])) {
            ++n.orphans;
            continue;
        }
        live[i] = 1;
        ages[i] = age;
        ++n.live;
    }
    for (unsigned i = 0; i < cap; ++i) {
        if (!live[i]) continue;
        const core::MuzzleRecord& m = records[i];
        const BoltLook& look = look_of(m.row);
        const unsigned cls = look.mclass < 5 ? look.mclass : 0u;
        const float Rg = mz::class_glow[cls][0] * look.w_m * core::engine_units_per_metre * core::world_scale;
        unsigned N = 0;
        for (unsigned j = 0; j < cap; ++j) {
            if (!live[j] || records[j].ship != m.ship) continue;
            float d2 = 0.f;
            for (unsigned k = 0; k < 3; ++k) {
                const float d = poses[j].pos[k] - poses[i].pos[k];
                d2 += d * d;
            }
            N += d2 <= 4.f * Rg * Rg;
        }
        const float cluster = N > 1 ? 1.f / (1.f + float(N - 1) / mz::cluster_n0) : 1.f;
        n.clustered += N > 1;
        const float gain = poses[i].own && f.cockpit ? t.own_gain : 1.f;
        n.cockpit += gain < 1.f;
        muzzle_elements(f, poses[i], look, m, ages[i], cluster, gain, t, b, &n);
    }
    return n;
}
} // namespace x3m::weapon_effects::stage
