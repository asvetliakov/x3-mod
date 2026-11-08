#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "sse_scalar.h"
#include "weapon_effects_bubble.h"
#include "weapon_effects_projectile.h"
#include "weapon_projectile_family.h"

// The projectile stage's CPU half (docs/architecture/projectile-look.md section 2 and "Implementation (2026-11-08)"):
// every live pool entry (weapon_effects_projectile.h) validated through the bounded reader, its node read once (live,
// handle, position +0x30, velocity +0x100, body +0x144; one read, no object read: a recycled node changes handle) and
// turned into one to five screen-space quads of four vertices: a streak of light on screen from the previous frame's projected
// head to this frame's projected head P (P the body origin, where the vanilla mesh is centred; both through the
// unjittered projection), so consecutive frames' streaks meet end to end on screen at any frame rate and whatever the
// camera did (the FL 59 A dashed trail was the gap between short dashes); a first frame (a jump, a cut, a frame the
// stage did not run) from the projection of P - v_rel dt_game (v_rel relative to the camera; the engine's game-time
// step, so SETA lengthens it), never behind the muzzle. A static frame (node +0x30 unchanged) extrapolates the head by
// v x render dt x SETA rate. No multiplier; the radiance factor is min(1, length_cap_m / the world travel |v| dt_game);
// a motion under the core's width (end-on) is a spot of the core's width at full core radiance; past lod_dot_m a spot. Along the
// axis each segment has logistic end ramps whose half maximum sits at each end, so two segments sharing an end add to
// one streak; with trail_ms (FL 60 A) the comet: the brightness falls off with the age of each point along the path
// (trail_falloff), the bolt's own segment carries the head (head_gain) and the entry's ring of up to trail_ring older
// heads is redrawn behind it with the continued falloff; trail_ms 0 is the flat streak of FL run 60. The core's full
// width is core_px at the 500 m reference through the live
// projection: its world width is core_px x 500 m / f (f = m11 H / 2, the frame's projection, never a fixed focal
// length), drawn at w = world width x f / z px; under 1 px it is drawn 1 px wide at radiance x w (a far bolt dims, it
// never fattens). A Gaussian glow of sigma glow_ratio x w / 2 (floor 1.5 px, its radiance scaled by the floor's lost
// area) in the row's colour at glow_gain; the core colour lerp(tint, white, hot_share (1 + calibre)) at
// 2^(stops + calibre). The quad spans the segment plus, on every side, the larger of the two Gaussians' reach to
// radiance_cut (1/256 of scene white) and half a pixel (along, the logistic ramps' reach); the pixel program
// (weapon_projectile_ps.hlsl) evaluates the two Gaussians across times their along profiles. Depth the head's
// (m22 + m32 / z). The row's look (colour from texture_rgb or the override tint, calibre from the override or the
// damage per shot) is resolved once per body. No allocation, no libm (the exp2/log2 below are SSE polynomials).
namespace x3m::weapon_effects::projectile_stage {
namespace core = x3m::weapon_effects::core;
namespace pf = x3m::weapon_effects::projectile_family;
using Reader = x3m::weapon_effects::stage::Reader;
constexpr float view_units_per_metre = 5.f; // engine units (500 per metre) x world_scale (0.01)
constexpr float glow_floor_px = 1.5f, quad_margin_px = 0.5f, near_margin = 1.001f;
// The quad ends where both Gaussians fall below this radiance (scene white 1): r = sigma sqrt(2 ln(peak / cut)).
constexpr float radiance_cut = 1.f / 256.f;
constexpr unsigned vertices_per_quad = 4, indices_per_quad = 6;
static_assert(1u + core::projectile::trail_ring == renderer::weapon_projectile_quads_per_bolt,
              "a bolt is its own segment and the comet's ring");
// 2^x and log2 x in float without libm (|error| < 2e-6 relative over the used ranges).
inline float exp2f_poly(float x) noexcept {
    if (!(x > -126.f)) return 0.f;
    if (x > 127.f) x = 127.f;
    const float n = float(scalar::floor(double(x))), f = x - n;
    const float p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const std::int32_t e = std::int32_t(n) + 127;
    std::uint32_t bits = std::uint32_t(e) << 23;
    float scale;
    std::memcpy(&scale, &bits, 4);
    return p * scale;
}
inline float log2f_poly(float x) noexcept {
    if (!(x > 0.f)) return -126.f;
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    const int e = int((bits >> 23) & 0xffu) - 127;
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy(&m, &bits, 4); // [1, 2)
    const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    const float s = t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f + t2 * (1.f / 9.f + t2 / 11.f)))));
    return float(e) + 2.8853900817779268f * s; // 2 / ln 2
}
// sqrt(2 ln(peak / cut)) for peak > cut (log2 based), else 0.
inline float cut_sigmas(float peak) noexcept {
    if (!(peak > radiance_cut)) return 0.f;
    return scalar::sqrt(2.f * 0.69314718f * log2f_poly(peak / radiance_cut));
}
inline float srgb_to_linear(float c) noexcept {
    if (!(c > 0.04045f)) return c > 0.f ? c / 12.92f : 0.f;
    return exp2f_poly(2.4f * log2f_poly((c + 0.055f) / 1.055f));
}
// The row's look, resolved once per body: the tint at full radiance (linear, its largest channel 1) and the calibre.
struct BodyLook {
    float tint[3] = {1.f, 1.f, 1.f};
    float calibre = 0.f;
    std::uint8_t family = 0, valid = 0;
};
inline void tint_of(std::uint32_t rgb, float out[3]) noexcept {
    float m = 0.f;
    for (unsigned k = 0; k < 3; ++k) {
        out[k] = srgb_to_linear(float((rgb >> (8 * k)) & 0xffu) / 255.f);
        m = out[k] > m ? out[k] : m;
    }
    for (unsigned k = 0; k < 3; ++k) out[k] = m > 0.f ? out[k] / m : 1.f;
}
// calibre k = clamp(log10(d / 300) / 2, 0, 1) over the damage per shot (core::calibre_reference / calibre_decades).
inline float calibre_of_damage(float d) noexcept {
    if (!(d > core::calibre_reference)) return 0.f;
    const float k = log2f_poly(d / core::calibre_reference) * 0.30102999566f / core::calibre_decades;
    return k < 1.f ? k : 1.f;
}
inline BodyLook body_look(const core::Table* t, const core::ProjectileBody& b, Reader rd, const core::Globals& g) noexcept {
    BodyLook l;
    l.family = b.family < pf::family_count ? b.family : 0;
    l.valid = 1;
    const unsigned r = b.row;
    if (t && r < core::table_capacity && (t->bits[r] & core::row_present)) {
        if (t->bits[r] & core::row_tint_override)
            tint_of(t->override_rgb[r], l.tint);
        else if (t->bits[r] & core::row_texture)
            tint_of(t->texture_rgb[r], l.tint);
        if (t->bits[r] & core::row_calibre_override) {
            l.calibre = float(t->calibre_q[r]) / 255.f;
            return l;
        }
    }
    std::uint32_t table = 0;
    std::int32_t damage = 0;
    if (rd && g.bullet_table_slot && rd(g.bullet_table_slot, &table, 4) && table &&
        rd(table + std::uintptr_t(r) * core::layout::type_stride + core::layout::bullet_shield_damage, &damage, 4))
        l.calibre = calibre_of_damage(float(damage));
    return l;
}
// The frame's view: the camera rows (view_j = sum_i world_i r[i*3+j] + t_j, world = engine units x 0.01), the jittered
// projection and the target.
struct View {
    float r[9]{}, t[3]{};
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    float width = 0, height = 0, near_z = 1.f;
};
// One bolt: its head (view space, view units; the body origin, or extrapolated on a static frame), its velocity (view
// units per second) and the camera's translation over the frame (view space), the previous frame's head on screen
// (unjittered raster px) when the entry was drawn in the previous frame (has_tail) and whether that head was
// extrapolated, the game-time step (0: the game is paused) and its age (seconds). With a tail, the comet's ring: the
// game time from that head to this one (tail_age_s) and the heads drawn before it (raster px, newest first), each with
// the game time from it to the newer head (ring_span_s[0]: to tail_px).
struct BoltIn {
    float p[3]{}, v[3]{}, cam[3]{};
    float tail_px[2]{};
    float step_s = 0.f, age_s = 1e6f;
    bool has_tail = false, tail_extrapolated = false;
    float tail_age_s = 0.f;
    float ring_px[core::projectile::trail_ring][2]{};
    float ring_span_s[core::projectile::trail_ring]{};
    unsigned ring_count = 0;
};
struct BoltShape { // diagnostics, the bridge's next tail and the fixture's rule checks
    float depth_m = 0;
    float streak_m = 0;  // the world travel this frame, |v| dt_game (metres: the cap's length)
    float length_px = 0; // the on-screen motion (tail to head)
    float drawn_px = 0;  // the drawn segment (at least the core's width; a segment past trail_ms clipped there)
    float core_px = 0, dim = 1, energy = 1, glow_sigma_px = 0, ext_px = 0, ext_along_px = 0;
    float raster[4][2]{}, head[2]{}, tail[2]{}; // the quad's corners; the drawn segment's ends (unjittered raster)
    float motion_tail[2]{}; // the motion's tail on screen (a first frame's: the next frame's first ring head)
    float tail_age_s = 0;   // the game time over the motion (tail to head; 0 when held)
    float trail_age_s = 0;  // the age at the drawn comet's far end (0: no trail)
    float trail_end[2]{};   // that end on screen
    float head_gain = 1;    // the head's core over the trail's start as written (1: no head)
    unsigned quads = 0, trail_quads = 0; // quads written; the ring's segments drawn (in one quad when merged)
    bool merged = false;                 // the comet drawn as one quad (its knots on one line, its ages linear)
    bool dot = false, held = false, projected = false; // projected: head[] is the bridge's next tail
};
inline void rotate_view(const View& c, const float w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j) out[j] = w[0] * c.r[j] + w[1] * c.r[3 + j] + w[2] * c.r[6 + j];
}
// The frame's per-view constants (reciprocals and the reference width), once per build.
struct ViewFrame {
    float zn = 0, half_w = 0, half_h = 0, ndc_x = 0, ndc_y = 0, world_w_per_px = 0;
    bool ok = false;
};
inline ViewFrame view_frame(const View& c, float core_px) noexcept {
    ViewFrame v;
    v.ok = c.width > 0.f && c.height > 0.f && c.m11 > 0.f && c.m00 > 0.f && c.near_z > 0.f;
    if (!v.ok) return v;
    v.zn = c.near_z * near_margin;
    v.half_w = 0.5f * c.width;
    v.half_h = 0.5f * c.height;
    v.ndc_x = 2.f / c.width;
    v.ndc_y = 2.f / c.height;
    // the live projection: f = m11 H / 2 px per view unit at depth 1; the core's world width at the reference is
    // core_px x 500 m / f, so its pixels at depth z are that x f / z = core_px x 500 m / z (any field of view)
    const float f_px = c.m11 * v.half_h;
    const float world_w = core_px * pf::reference_m * view_units_per_metre / f_px; // view units
    v.world_w_per_px = world_w * f_px; // x 1 / z: the core's pixels
    return v;
}
// A body's radiance for a frame: the core colour lerp(tint, white, hot) at 2^(stops + calibre) and the glow's tint at
// glow_gain (colours as D3DCOLOR words, the scales as floats), and log2 of each peak over the cut (the quad's reach).
struct BodyFrame {
    std::uint32_t core_tint = 0, glow_tint = 0;
    float core_scale = 0, glow_scale = 0;
    float log2_core = 0, log2_glow = 0;
};
inline std::uint32_t colour_word(const float c[3]) noexcept {
    std::uint32_t w = 0xff000000u;
    for (unsigned k = 0; k < 3; ++k) {
        const float v = c[k] < 0.f ? 0.f : c[k] > 1.f ? 1.f : c[k];
        w |= std::uint32_t(v * 255.f + 0.5f) << (16 - 8 * k); // ARGB: red in bits 16..23
    }
    return w;
}
inline BodyFrame body_frame(const pf::Tune& tune, const BodyLook& look) noexcept {
    BodyFrame b;
    const float hot = tune.hot_share * (1.f + look.calibre), hot_c = hot < 1.f ? hot : 1.f;
    float core[3], glow[3], cp = 0.f, gp = 0.f;
    for (unsigned k = 0; k < 3; ++k) {
        core[k] = look.tint[k] + (1.f - look.tint[k]) * hot_c;
        glow[k] = look.tint[k];
        cp = core[k] > cp ? core[k] : cp;
        gp = glow[k] > gp ? glow[k] : gp;
    }
    b.core_tint = colour_word(core);
    b.glow_tint = colour_word(glow);
    b.core_scale = exp2f_poly(tune.stops + look.calibre);
    b.glow_scale = tune.glow_gain;
    b.log2_core = cp * b.core_scale > 0.f ? log2f_poly(cp * b.core_scale / radiance_cut) : -126.f;
    b.log2_glow = gp * b.glow_scale > 0.f ? log2f_poly(gp * b.glow_scale / radiance_cut) : -126.f;
    return b;
}
// sigma x sqrt(2 ln(peak / cut)) from log2(peak / cut) (0 when the peak is under the cut).
inline float reach(float sigma, float log2_ratio) noexcept {
    return log2_ratio > 0.f ? sigma * scalar::sqrt(2.f * 0.69314718f * log2_ratio) : 0.f;
}
// The along-axis profile's logistic CDF slope: Phi(u) = 1 / (1 + 2^(-logistic_slope u / sigma)) (1.702 / ln 2; the
// logistic's largest gap to the normal CDF is under 0.01). The pixel program evaluates Phi(x + h) - Phi(x - h): two
// segments sharing an end sum to Phi(x + h1) - Phi(x - h2) whatever Phi is (they telescope), so consecutive frames'
// streaks add to one flat streak with no bead and no gap at the seam.
constexpr float logistic_slope = 2.45546f;
// 1 / (Phi(h) - Phi(-h)) for half length h and slope m / sigma (the centre's along profile at 1).
inline float along_norm(float h, float m_over_sigma) noexcept {
    const float e = exp2f_poly(-m_over_sigma * h);
    return (1.f + e) / (1.f - e);
}
// The comet (FL 60 A, projectile-look.md section 2): along the bolt's on-screen path the brightness is a function of the
// age a of each point (the game time since the head passed it), F(a) = (8 2^(-3 a / trail_ms) - 1) / 7: 1 at the head,
// 0 at trail_ms, monotonic. A frame draws its segment (the previous head to this head) and up to trail_ring older
// segments from the entry's ring, each with F over its own age span, so neighbouring segments meet at a shared end with
// equal values (the ramps telescope as before) and the profile is one falloff. The comet is drawn while F >=
// trail_floor, to the age trail_drawn x trail_ms, with the end ramp beyond it (the budget: the faint end of the law costs
// its area; at 5 % the 512-bolt case measured 50.0 and 50.6 us of the 50 us GPU budget, at 10 % see the ledger); a ring
// segment whose newer end is past it is not drawn, the one crossing it is clipped there. weapon_projectile_ps.hlsl
// evaluates saturate((8 2^e - 1) / 7) with e = -3 a / trail_ms linear along the segment (e = 0 without a trail: F = 1).
constexpr float trail_k = 3.f, trail_floor = 0.1f;
constexpr float trail_drawn = 0.74482f; // log2(8 / (1 + 7 trail_floor)) / trail_k
inline float trail_falloff(float age_s, float inv_trail_s) noexcept {
    const float f = (8.f * exp2f_poly(-trail_k * age_s * inv_trail_s) - 1.f) * (1.f / 7.f);
    return f < 0.f ? 0.f : f > 1.f ? 1.f : f;
}
// The head: the core of the bolt's own segment adds 4 a (Phi(x - h + L) - Phi(x - h)) over its last L = head_widths core
// widths (m L = head_widths x 2.35482 x logistic_slope = 8.67327, the program's constant), a in the core colour's alpha:
// at the window's centre (L / 2 behind the head) the segment's own profile is head_ramp (its end ramp) and the window
// head_window, so a = (head_gain - head_ramp) / head_window / 4 puts the head's core at head_gain x the trail's start.
constexpr float head_widths = 1.5f;
constexpr float head_ramp = 0.95294f;   // 1 - 1 / (1 + 2^4.33664), m L / 2 = 4.33664
constexpr float head_window = 0.90574f; // (1 - 2^-4.33664) / (1 + 2^-4.33664)
// One quad for the whole comet when its knots (the segments' ends) lie on the line from its far end to the head within
// merge_px and their ages are linear along it within merge_age x trail_ms (a straight on-screen path at a steady screen
// speed, the common case: a 3D line projects to a line); else one quad per segment.
constexpr float merge_px = 0.25f, merge_age = 0.02f;
// The program's across coordinate q: exp2(-q^2) = exp(-y^2 / (2 sigma^2)) for q = y q_per_sigma / sigma.
constexpr float q_per_sigma = 0.84932180f; // 1 / sqrt(2 ln 2)
// The quads of one bolt (0: culled, the head behind the near plane, off the target or degenerate), at most max_quads, in
// screen space through the caller's projection (the stage passes the unjittered one: the bolt never enters the TAA
// history, and jitter would only misalign the seam). The bolt's segment runs from the tail to the head on screen: the
// previous frame's projected head (in.tail_px) when the entry was drawn in the previous frame, so the streak covers the
// bolt's on-screen path between the two frames whatever the camera did (translation and rotation); else (a first frame,
// a jump, a cut) the projection of P - v_rel min(dt_game, age), v_rel the velocity relative to the camera (never behind
// the muzzle). A head extrapolated last frame past this frame's position (against the on-screen motion) holds there. No
// multiplier. Energy: the radiance factor is min(1, length_cap_m / the world travel |v| dt_game) over the whole comet (a
// fast or SETA bolt is a longer, fainter streak; a travel under the cap keeps full radiance whatever its on-screen
// length); an on-screen motion under the core's width (an end-on bolt flying along the line of sight) is a spot of the
// core's width at full core radiance at its centre, no trail and no head; past lod_dot_m the same spot on the head with
// the streak's factor. With trail_ms the comet: the segment's falloff from the head over its age span, the head on its
// core and the ring's older segments behind it (with a tail and max_quads > 1). Width, glow and depth (occlusion) are
// the head's. The bolt's own segment is out[0..3], the ring's follow.
inline unsigned bolt_quads(const View& c, const ViewFrame& vf, const pf::Tune& tune, const BodyFrame& body,
                           const BoltIn& in, renderer::WeaponProjectileVertex* out, unsigned max_quads,
                           BoltShape* shape = nullptr) noexcept {
    if (!vf.ok || !(in.p[2] > vf.zn) || !max_quads) return 0;
    const float zn = vf.zn;
    const float inv_z = 1.f / in.p[2];
    const float depth_m = in.p[2] * (1.f / view_units_per_metre);
    const auto raster = [&](const float q[3], float r[2]) {
        const float iz = 1.f / q[2];
        r[0] = (c.m00 * q[0] * iz + c.m20 + 1.f) * vf.half_w;
        r[1] = (1.f - c.m11 * q[1] * iz - c.m21) * vf.half_h;
    };
    float rb[2];
    raster(in.p, rb);
    const float step = in.step_s > 0.f ? in.step_s : 0.f;
    const float speed_m = scalar::sqrt(in.v[0] * in.v[0] + in.v[1] * in.v[1] + in.v[2] * in.v[2]) *
                          (1.f / view_units_per_metre);
    // the velocity relative to the camera (a cut's travel, longer than the bolt's, is not subtracted)
    float rel[3] = {in.v[0], in.v[1], in.v[2]};
    if (step > 0.f) {
        const float c2 = in.cam[0] * in.cam[0] + in.cam[1] * in.cam[1] + in.cam[2] * in.cam[2];
        const float v2 = (rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]) * step * step;
        if (c2 <= v2)
            for (unsigned k = 0; k < 3; ++k) rel[k] -= in.cam[k] / step;
    }
    // its on-screen velocity at the head (px per second)
    const float sx = vf.half_w * c.m00 * (rel[0] * in.p[2] - in.p[0] * rel[2]) * inv_z * inv_z;
    const float sy = -vf.half_h * c.m11 * (rel[1] * in.p[2] - in.p[1] * rel[2]) * inv_z * inv_z;
    float ra[2];
    bool held = false;
    float motion_age = 0.f; // the game time over the motion (its tail's age)
    if (in.has_tail) {
        ra[0] = in.tail_px[0];
        ra[1] = in.tail_px[1];
        motion_age = in.tail_age_s > 0.f ? in.tail_age_s : 0.f;
        if (in.tail_extrapolated && (rb[0] - ra[0]) * sx + (rb[1] - ra[1]) * sy < 0.f) { // never backwards on screen
            rb[0] = ra[0];
            rb[1] = ra[1];
            held = true;
            motion_age = 0.f; // the head is the previous one: the trail's ages start there
        }
    } else {
        const float age = in.age_s > 0.f ? in.age_s : 0.f, t = step < age ? step : age;
        float a[3];
        for (unsigned k = 0; k < 3; ++k) a[k] = in.p[k] - rel[k] * t;
        motion_age = t;
        if (a[2] < zn) { // the tail clipped to the near plane (the head is in front)
            const float u = (zn - in.p[2]) / (a[2] - in.p[2]);
            for (unsigned k = 0; k < 3; ++k) a[k] = in.p[k] + u * (a[k] - in.p[k]);
            motion_age = t * u;
        }
        raster(a, ra);
    }
    float ux = rb[0] - ra[0], uy = rb[1] - ra[1];
    const float len = scalar::sqrt(ux * ux + uy * uy);
    if (len > 1e-4f) {
        const float inv = 1.f / len;
        ux *= inv;
        uy *= inv;
    } else {
        const float sl = scalar::sqrt(sx * sx + sy * sy);
        ux = sl > 1e-6f ? sx / sl : 1.f;
        uy = sl > 1e-6f ? sy / sl : 0.f;
    }
    // the width through the live projection (view_frame); under 1 px drawn at 1 px with the radiance scaled
    const float w_true = vf.world_w_per_px * inv_z;
    const float w = w_true > 1.f ? w_true : 1.f, dim = w_true > 1.f ? 1.f : w_true;
    const float sigma_c = w * (1.f / 2.35482f), inv_sc = 2.35482f / w;
    const float sigma_raw = tune.glow_ratio * w_true * 0.5f;
    const float sigma_g = sigma_raw > glow_floor_px ? sigma_raw : glow_floor_px, inv_sg = 1.f / sigma_g;
    const float glow_area = sigma_raw > glow_floor_px ? 1.f : sigma_raw * (1.f / glow_floor_px);
    const float glow_scale = glow_area * glow_area;
    // energy: the radiance factor min(1, length_cap_m / the world travel this frame, |v| dt_game); a travel under the
    // cap (or a paused game) keeps 1, whatever its on-screen length; the dot keeps the same factor
    const float travel_m = speed_m * step;
    const float cap = tune.length_cap_m;
    const float energy = travel_m > cap ? cap / travel_m : 1.f;
    const bool dot = depth_m > tune.lod_dot_m;
    // a motion under the core's width (an end-on or a slow bolt) and the dot: a spot of the core's width centred on the
    // motion (the dot on the head), its along profile normalised to 1 at its centre (full core radiance), no trail
    const bool spot = dot || len < w;
    const bool trail = !spot && tune.trail_ms > 0.f;
    const float inv_trail = trail ? 1000.f / tune.trail_ms : 0.f;
    const float trail_s = tune.trail_ms * 1e-3f * trail_drawn; // the drawn comet's age (F = trail_floor)
    float ta[2] = {ra[0], ra[1]}, tb[2] = {rb[0], rb[1]}; // the drawn ends
    float drawn = spot ? w : len, seg_age = trail ? motion_age : 0.f;
    if (spot) {
        const float mx = dot ? rb[0] : 0.5f * (ra[0] + rb[0]), my = dot ? rb[1] : 0.5f * (ra[1] + rb[1]);
        ta[0] = mx - 0.5f * w * ux, ta[1] = my - 0.5f * w * uy;
        tb[0] = mx + 0.5f * w * ux, tb[1] = my + 0.5f * w * uy;
    } else if (trail && seg_age > trail_s) { // the comet ends inside this segment (a SETA step)
        const float u = trail_s / seg_age;
        ta[0] = rb[0] + (ra[0] - rb[0]) * u, ta[1] = rb[1] + (ra[1] - rb[1]) * u;
        drawn = len * u;
        seg_age = trail_s;
    }
    // The ring's older segments behind the motion's tail (newest first): drawn while their newer end is younger than the
    // drawn comet's age (F >= trail_floor), the one crossing it clipped there; a zero-length one is skipped (its time
    // still counts).
    constexpr unsigned ring_max = core::projectile::trail_ring;
    float seg_end[ring_max][2][2], seg_ages[ring_max][2], seg_f[ring_max];
    unsigned ring_n = 0;
    float trail_end_age = trail ? seg_age : 0.f, trail_end[2] = {ta[0], ta[1]};
    if (trail && in.has_tail && motion_age < trail_s) {
        float age = motion_age;
        const float* newer = ra;
        const unsigned count = in.ring_count < ring_max ? in.ring_count : ring_max;
        for (unsigned k = 0; k < count && ring_n + 1u < max_quads && age < trail_s; ++k) {
            const float f = trail_falloff(age, inv_trail);
            const float span = in.ring_span_s[k] > 0.f ? in.ring_span_s[k] : 0.f;
            const float* older = in.ring_px[k];
            float end[2] = {older[0], older[1]}, old_age = age + span;
            if (old_age > trail_s) {
                const float u = (trail_s - age) / span;
                end[0] = newer[0] + (older[0] - newer[0]) * u, end[1] = newer[1] + (older[1] - newer[1]) * u;
                old_age = trail_s;
            }
            const float dx = newer[0] - end[0], dy = newer[1] - end[1];
            if (dx * dx + dy * dy > 1e-8f) {
                seg_end[ring_n][0][0] = end[0], seg_end[ring_n][0][1] = end[1];
                seg_end[ring_n][1][0] = newer[0], seg_end[ring_n][1][1] = newer[1];
                seg_ages[ring_n][0] = old_age, seg_ages[ring_n][1] = age;
                seg_f[ring_n] = f;
                trail_end_age = old_age;
                trail_end[0] = end[0], trail_end[1] = end[1];
                ++ring_n;
            }
            newer = older;
            age += span;
        }
    }
    const bool lone = ring_n == 0;
    bool merged = false;
    if (ring_n) {
        const float ex = tb[0] - trail_end[0], ey = tb[1] - trail_end[1], el = scalar::sqrt(ex * ex + ey * ey);
        if (el > 1e-3f) {
            const float inv = 1.f / el, mx = ex * inv, my = ey * inv, slope = trail_end_age * inv; // age per px
            const float tol = merge_age * trail_s;
            const auto fits = [&](const float k[2], float age) {
                const float dx = tb[0] - k[0], dy = tb[1] - k[1];
                const float behind = dx * mx + dy * my, off = dx * my - dy * mx;
                return (off < 0.f ? -off : off) <= merge_px && (behind * slope - age < 0.f ? age - behind * slope
                                                                                           : behind * slope - age) <= tol;
            };
            merged = fits(ta, seg_age);
            for (unsigned k = 0; merged && k + 1u < ring_n; ++k) merged = fits(seg_end[k][0], seg_ages[k][0]);
            if (merged) {
                ta[0] = trail_end[0], ta[1] = trail_end[1];
                drawn = el;
                seg_age = trail_end_age;
                ux = mx, uy = my;
            }
        }
    }
    const float half = 0.5f * drawn;
    // 1 / (Phi(h) - Phi(-h)) per Gaussian for a lone segment (past 16 / m the ramps no longer meet: 1 within 2^-15, no
    // exp2): a streak of a few core widths and longer is unchanged, a spot reaches the full radiance at its centre; the
    // comet's segments are not normalised (they telescope with their neighbours)
    const float mc = logistic_slope * inv_sc, mg = logistic_slope * inv_sg;
    const float nc = lone && mc * half < 16.f ? along_norm(half, mc) : 1.f;
    const float ng = lone && mg * half < 16.f ? along_norm(half, mg) : 1.f;
    const float fc = dim * energy * nc, fg = glow_scale * energy * ng;
    const float core_k = body.core_scale * fc, glow_k = body.glow_scale * fg;
    const float lc0 = body.log2_core + (fc != 1.f ? log2f_poly(fc) : 0.f);
    const float lg0 = body.log2_glow + (fg != 1.f ? log2f_poly(fg) : 0.f);
    // the head (the comet only): its gain over 4 in the core colour's alpha byte
    const float gain = trail ? (tune.head_gain > 1.f ? tune.head_gain : 1.f) : 0.f;
    const float head_a = gain > head_ramp ? (gain - head_ramp) * (0.25f / head_window) : 0.f;
    const std::uint32_t head_byte = std::uint32_t((head_a < 1.f ? head_a : 1.f) * 255.f + 0.5f);
    const std::uint32_t core_trail = body.core_tint & 0x00ffffffu, core_head = core_trail | head_byte << 24;
    const float lh = gain > 1.f ? log2f_poly(gain) : 0.f; // the head's peak over the trail's start
    const float zb = c.m22 + c.m32 * inv_z, zd = zb < 0.f ? 0.f : zb > 1.f ? 1.f : zb; // the head's depth
    const float qc = q_per_sigma * inv_sc, ratio = sigma_c * inv_sg;
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    // One segment's quad from a (older) to b (newer) along the unit (dx, dy), half length h, ages age_a / age_b, peaks
    // lc / lg (log2 over the cut) at b, g_b = 2^(-trail_k age_b / trail_ms), the core colour word; its reach across (at b)
    // and along into ext / ext_a. With the trail the quad tapers to a's end: the reach r = sigma sqrt(2 ln 2 (l +
    // log2 F)) is concave in the age, so its tangent at b bounds it over the segment: r_a = r_b (1 - s), s = (-d log2 F
    // / d age at b) (age_a - age_b) / (2 l), -d log2 F / d age = trail_k 8 g / (7 F trail_ms) (every attribute is affine
    // over the trapezoid, so the interpolation stays exact).
    const auto emit = [&](renderer::WeaponProjectileVertex* v, const float a[2], const float b[2], float dx, float dy,
                          float h, float age_a, float age_b, float lc, float lg, float g_b, std::uint32_t core_word,
                          float& ext, float& ext_a, float (*corners)[2]) {
        // across, where both Gaussians fall under radiance_cut; along past the ends, the logistic's tail (peak
        // 2^(-m z), m = logistic_slope / sigma) under it
        const float rc = reach(sigma_c, lc), rg = reach(sigma_g, lg);
        ext = (rc > rg ? rc : rg) + quad_margin_px;
        float ext_old = ext;
        if (trail) {
            const float f_b = (8.f * g_b - 1.f) * (1.f / 7.f);
            const float d = f_b > 1e-3f ? trail_k * 8.f * g_b * inv_trail * (age_a - age_b) / (7.f * f_b) : 0.f;
            const float sc = lc > 0.f ? 1.f - d * 0.5f / lc : 0.f, sg = lg > 0.f ? 1.f - d * 0.5f / lg : 0.f;
            const float tc = rc * (sc > 0.f ? sc : 0.f), tg = rg * (sg > 0.f ? sg : 0.f);
            ext_old = (tc > tg ? tc : tg) + quad_margin_px;
        }
        const float ac = lc > 0.f ? lc * sigma_c * (1.f / logistic_slope) : 0.f;
        const float ag = lg > 0.f ? lg * sigma_g * (1.f / logistic_slope) : 0.f;
        ext_a = (ac > ag ? ac : ag) + quad_margin_px;
        // the falloff's exponent e = -trail_k age / trail_ms, linear along the segment: e0 at its centre, e1 per px
        const float e0 = -trail_k * 0.5f * (age_a + age_b) * inv_trail;
        const float e1 = h > 1e-6f ? trail_k * (age_a - age_b) * inv_trail * 0.5f / h : 0.f;
        const float nx = -dy, ny = dx;
        static constexpr float corner[4][3] = {{-1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, -1.f, 1.f}, {1.f, 1.f, 1.f}};
        for (unsigned i = 0; i < 4; ++i) {
            const float* e = corner[i];
            const bool newer = e[2] > 0.5f;
            const float* base = newer ? b : a;
            const float across = newer ? ext : ext_old;
            const float px = base[0] + dx * ext_a * e[0] + nx * across * e[1];
            const float py = base[1] + dy * ext_a * e[0] + ny * across * e[1];
            const float x = e[0] * (h + ext_a); // along, about the segment's centre (+ towards the head)
            renderer::WeaponProjectileVertex& o = v[i];
            o.pos[0] = px * vf.ndc_x - 1.f;
            o.pos[1] = 1.f - py * vf.ndc_y;
            o.pos[2] = zd;
            o.local[0] = e[1] * across * qc;
            o.local[1] = -mc * (x + h);
            o.local[2] = -mc * (x - h);
            o.local[3] = e0 + e1 * x;
            o.shape[0] = core_k;
            o.shape[1] = glow_k;
            o.shape[2] = ratio;
            o.core_tint = core_word;
            o.glow_tint = body.glow_tint;
            minx = px < minx ? px : minx;
            maxx = px > maxx ? px : maxx;
            miny = py < miny ? py : miny;
            maxy = py > maxy ? py : maxy;
            if (corners) {
                corners[i][0] = px;
                corners[i][1] = py;
            }
        }
    };
    float ext = 0.f, ext_a = 0.f, ring_ext = 0.f, ring_ext_a = 0.f;
    emit(out, ta, tb, ux, uy, half, seg_age, 0.f, lc0 + lh, lg0, 1.f, head_byte ? core_head : core_trail, ext, ext_a,
         shape ? shape->raster : nullptr);
    const unsigned ring_quads = merged ? 0u : ring_n;
    for (unsigned k = 0; k < ring_quads; ++k) {
        const float* a = seg_end[k][0];
        const float* b = seg_end[k][1];
        float dx = b[0] - a[0], dy = b[1] - a[1];
        const float l = scalar::sqrt(dx * dx + dy * dy), inv = 1.f / l;
        const float lf = log2f_poly(seg_f[k]);
        emit(out + 4 * (k + 1), a, b, dx * inv, dy * inv, 0.5f * l, seg_ages[k][0], seg_ages[k][1], lc0 + lf, lg0 + lf,
             (7.f * seg_f[k] + 1.f) * 0.125f, core_trail, ring_ext, ring_ext_a, nullptr);
    }
    if (shape) {
        shape->depth_m = depth_m;
        shape->streak_m = travel_m;
        shape->length_px = len;
        shape->drawn_px = drawn;
        shape->core_px = w;
        shape->dim = dim;
        shape->energy = energy;
        shape->glow_sigma_px = sigma_g;
        shape->ext_px = ext;
        shape->ext_along_px = ext_a;
        shape->head[0] = rb[0], shape->head[1] = rb[1];
        shape->tail[0] = ta[0], shape->tail[1] = ta[1];
        shape->motion_tail[0] = ra[0], shape->motion_tail[1] = ra[1];
        shape->tail_age_s = motion_age;
        shape->trail_age_s = trail_end_age;
        shape->trail_end[0] = trail_end[0], shape->trail_end[1] = trail_end[1];
        shape->head_gain = head_byte ? head_ramp + float(head_byte) * (4.f / 255.f) * head_window : 1.f;
        shape->quads = 1 + ring_quads;
        shape->trail_quads = ring_n;
        shape->merged = merged;
        shape->dot = dot;
        shape->held = held;
        shape->projected = rb[0] == rb[0] && rb[1] == rb[1] && rb[0] < 1e30f && rb[0] > -1e30f && rb[1] < 1e30f &&
                           rb[1] > -1e30f;
    }
    // off the target (a pixel centre lies in [-0.5, size - 0.5]); a non-finite corner (never from finite engine
    // integers in front of the near plane) is culled, never drawn
    const bool keep = minx == minx && maxx == maxx && miny == miny && maxy == maxy && maxx < 1e30f && minx > -1e30f &&
                      maxy < 1e30f && miny > -1e30f &&
                      !(maxx < -0.5f || maxy < -0.5f || minx > c.width - 0.5f || miny > c.height - 0.5f);
    return keep ? 1u + ring_quads : 0u;
}
// The single-segment form (the fixture's rule checks): the view and body constants of this call alone, the bolt's own
// segment only (with trail_ms its falloff and head, no ring).
inline bool bolt_vertices(const View& c, const pf::Tune& tune, const BodyLook& look, const BoltIn& in,
                          renderer::WeaponProjectileVertex out[4], BoltShape* shape = nullptr) noexcept {
    return bolt_quads(c, view_frame(c, tune.core_px), tune, body_frame(tune, look), in, out, 1, shape) != 0;
}
struct Counts {
    unsigned tracked = 0, drawn = 0, culled = 0, expired = 0, invalid = 0, dots = 0;
    // evaluated entries whose node +0x30 did not change since the previous stage frame (drawn extrapolated), and those
    // whose read position left the velocity prediction by more than the tolerance (drawn as a first frame)
    unsigned statics = 0, jumped = 0;
    // the quads written (each drawn bolt's own and its comet's ring segments), of them the ring's, and the drawn bolts
    // with a trail behind their own segment (comets; merged: drawn as one quad)
    unsigned quads = 0, trail = 0, comets = 0, merged = 0;
};
// The per-body looks, resolved once per body (the pool's body list only grows until the next install).
struct Looks {
    unsigned resolved = 0;
    BodyLook look[core::projectile::max_bodies];
};
// A jump: the read position farther from read_pos + v x (game time since its change) than this (engine units) plus a
// quarter of the frame's travel.
constexpr float jump_tolerance_units = 500.f; // 1 m
// A static entry's head is extrapolated by at most this many game steps (a paused game, a bullet the engine stopped
// moving: the head holds instead of flying on).
constexpr std::uint32_t max_static_steps = 2;
// The frame's bolts: walks the pool from the last entry (a retired entry is swap-removed), reads each entry's node
// (live, handle, position, velocity, body) in one bounded read, retires an expired entry, one the tap has not seen for
// max(stale_ms, three game steps), a recycled node (another handle or body) or an unreadable one, and writes up to
// max_bolts bolts of at most 1 + trail_ring quads each, max_quads quads in all, into out (4 vertices a quad; a bolt
// whose own segment no longer fits is not drawn, one whose ring does not fit draws fewer ring segments). tunes[family] are the families'
// numbers (the baked constants or the dev tuning file's). now_ms / step_ms: cfg +0x708 / +0x704 (FL), game ms.
// frame: the device's frame number (an entry bridges only from the frame just before: a frame the stage did not run
// makes the next one a first frame); cut: the resolve's camera cut (every entry a first frame, no camera travel).
// render_dt_s: the proxy's frame timer since the previous stage frame (seconds); seta_rate: the engine's effective SETA
// rate (1 off): a static entry's head moves on by v x render dt x seta_rate per frame (game time, as the stage's step).
// view: the unjittered projection. Each evaluated entry keeps its read position and its head on screen
// (ProjectileEntry): the next frame's streak starts there; and the comet's ring: the head's game time (the last
// change's ms x 1000 + the extrapolated us) and the heads before it with theirs (a held head keeps the ring, a first
// frame restarts it from its motion's tail). shapes (the fixture's, else null): each drawn bolt's BoltShape, max_bolts
// of them.
inline Counts build(core::ProjectilePool& pool, Reader rd, const core::Table* table, Looks& looks, std::uint32_t frame,
                    bool cut, std::uint32_t now_ms, std::uint32_t step_ms, float render_dt_s, float seta_rate,
                    const View& view, const pf::Tune* tunes, renderer::WeaponProjectileVertex* out, unsigned max_bolts,
                    unsigned max_quads, BoltShape* shapes = nullptr) noexcept {
    Counts n;
    for (; looks.resolved < pool.body_count; ++looks.resolved)
        looks.look[looks.resolved] = body_look(table, pool.bodies[looks.resolved], rd, pool.globals);
    if (!pool.count) return n;
    const float step_s = float(step_ms < 1000u ? step_ms : 1000u) * 1e-3f;
    // not seen by the object tap for stale_ms game ms, at least three game steps (SETA-aware): retired
    const std::uint32_t unseen_ms = 3u * step_ms > core::projectile::stale_ms ? 3u * step_ms : core::projectile::stale_ms;
    // a static frame's extrapolation: render dt x the SETA rate (game us), bounded per entry by max_static_steps steps
    const float dt_game = (render_dt_s > 0.f && render_dt_s < 1.f ? render_dt_s : 0.f) *
                          (seta_rate > 0.f && seta_rate < 64.f ? seta_rate : 1.f);
    const std::uint32_t static_add_us = std::uint32_t(dt_game * 1e6f);
    const std::uint32_t static_max_us = max_static_steps * (step_ms < 1000u ? step_ms : 1000u) * 1000u;
    constexpr unsigned node_at = core::layout::node_live, node_span = core::layout::node_body + 4 - node_at;
    unsigned char nb[node_span];
    const auto u32 = [](const unsigned char* b, unsigned off) {
        std::uint32_t v;
        std::memcpy(&v, b + off, 4);
        return v;
    };
    // The eye in engine units (world = -t R^-1 / world_scale, the adjugate inverse: the engine's 16.16 basis is
    // orthonormal only to ~1e-5, which the transpose would turn into a false eye of up to a unit far from the origin;
    // camera_reprojection.h), once: each bolt's offset from it is a small float, so the per-bolt view transform runs in
    // float without losing the far-from-origin precision. A singular basis draws nothing this frame.
    double a[9], cof[9], eye[3];
    for (unsigned i = 0; i < 9; ++i) a[i] = view.r[i];
    cof[0] = a[4] * a[8] - a[5] * a[7], cof[1] = a[2] * a[7] - a[1] * a[8], cof[2] = a[1] * a[5] - a[2] * a[4];
    cof[3] = a[5] * a[6] - a[3] * a[8], cof[4] = a[0] * a[8] - a[2] * a[6], cof[5] = a[2] * a[3] - a[0] * a[5];
    cof[6] = a[3] * a[7] - a[4] * a[6], cof[7] = a[1] * a[6] - a[0] * a[7], cof[8] = a[0] * a[4] - a[1] * a[3];
    const double det = a[0] * cof[0] + a[1] * cof[3] + a[2] * cof[6];
    if (!(det > .5 || det < -.5)) return n;
    for (unsigned k = 0; k < 3; ++k) // world_k = -sum_j t_j inv[j][k], inv[j][k] = cof[j*3+k] / det (the adjugate)
        eye[k] = -(cof[k] * view.t[0] + cof[3 + k] * view.t[1] + cof[6 + k] * view.t[2]) / det / double(core::world_scale);
    const std::int32_t eye_i[3] = {std::int32_t(eye[0]), std::int32_t(eye[1]), std::int32_t(eye[2])};
    const float eye_f[3] = {float(eye[0] - double(eye_i[0])), float(eye[1] - double(eye_i[1])),
                            float(eye[2] - double(eye_i[2]))};
    // The camera's translation since the previous stage frame (0 without one or across a cut): a first frame's streak
    // is the bolt's travel relative to the camera (its velocity includes the shooter's).
    float cam_world[3] = {0.f, 0.f, 0.f}, cam[3];
    if (!cut && pool.stage_eye_frame && pool.stage_eye_frame + 1u == frame)
        for (unsigned k = 0; k < 3; ++k)
            cam_world[k] = float(std::int32_t(std::uint32_t(eye_i[k]) - std::uint32_t(pool.stage_eye[k]))) * core::world_scale;
    for (unsigned k = 0; k < 3; ++k) pool.stage_eye[k] = eye_i[k];
    pool.stage_eye_frame = frame;
    rotate_view(view, cam_world, cam);
    // per family the view constants, per body the frame's radiance
    ViewFrame vfs[pf::family_count];
    for (unsigned f = 0; f < pf::family_count; ++f) vfs[f] = view_frame(view, tunes[f].core_px);
    BodyFrame bodies[core::projectile::max_bodies];
    for (unsigned b = 0; b < pool.body_count && b < core::projectile::max_bodies; ++b)
        bodies[b] = body_frame(tunes[looks.look[b].family], looks.look[b]);
    for (unsigned i = pool.count; i-- > 0;) {
        core::ProjectileEntry& e = pool.entries[i];
        ++n.tracked;
        if ((e.expiry && std::int32_t(e.expiry - now_ms) <= 0) || std::int32_t(now_ms - e.seen_ms) > std::int32_t(unseen_ms)) {
            ++n.expired;
            core::projectile_remove(pool, i);
            continue;
        }
        if (!rd(e.node + node_at, nb, node_span) || !u32(nb, 0) ||
            u32(nb, core::layout::node_handle - node_at) != e.handle || e.body >= pool.body_count ||
            u32(nb, core::layout::node_body - node_at) != pool.bodies[e.body].id) {
            ++n.invalid;
            core::projectile_remove(pool, i);
            continue;
        }
        if (n.drawn >= max_bolts || n.quads >= max_quads) continue;
        std::int32_t pos[3];
        float vel[3], speed2 = 0.f; // engine units per second
        for (unsigned k = 0; k < 3; ++k) {
            pos[k] = std::int32_t(u32(nb, core::layout::node_pos - node_at + 4 * k));
            vel[k] = float(std::int32_t(u32(nb, core::projectile::node_velocity - node_at + 4 * k)));
            speed2 += vel[k] * vel[k];
        }
        // The bridge: an entry evaluated in the frame just before starts its streak at that frame's head on screen
        // (none across a cut or when that head was not projected). Its position unchanged since then (a static frame):
        // the head moves on by v x the extrapolated game time. Changed but far from the velocity's prediction (a jump):
        // a first frame.
        const bool consecutive = e.stage_frame && e.stage_frame + 1u == frame;
        bool bridged = consecutive;
        const bool was_extrapolated = e.static_us != 0;
        float ext_s = 0.f;
        if (consecutive && pos[0] == e.read_pos[0] && pos[1] == e.read_pos[1] && pos[2] == e.read_pos[2]) {
            ++n.statics;
            const std::uint32_t us = e.static_us + static_add_us;
            e.static_us = us < static_max_us ? us : static_max_us;
            ext_s = float(e.static_us) * 1e-6f;
        } else {
            if (consecutive) {
                const std::int32_t since = std::int32_t(now_ms - e.changed_ms);
                const float dt = since > 0 ? float(since) * 1e-3f : 0.f;
                float err2 = 0.f;
                for (unsigned k = 0; k < 3; ++k) {
                    const float d = float(std::int32_t(std::uint32_t(pos[k]) - std::uint32_t(e.read_pos[k]))) - vel[k] * dt;
                    err2 += d * d;
                }
                const float tol = jump_tolerance_units + 0.25f * scalar::sqrt(speed2) * step_s;
                if (err2 > tol * tol) {
                    ++n.jumped;
                    bridged = false;
                }
            }
            e.changed_ms = now_ms;
            e.static_us = 0;
        }
        BoltIn in;
        float head[3], v[3];
        for (unsigned k = 0; k < 3; ++k) {
            head[k] = (float(std::int32_t(std::uint32_t(pos[k]) - std::uint32_t(eye_i[k]))) - eye_f[k] + vel[k] * ext_s) *
                      core::world_scale;
            v[k] = vel[k] * core::world_scale;
        }
        rotate_view(view, head, in.p);
        rotate_view(view, v, in.v);
        for (unsigned k = 0; k < 3; ++k) in.cam[k] = cam[k];
        float tail_z = 0.f;
        std::memcpy(&in.tail_px[0], &e.head_px[0], 4);
        std::memcpy(&in.tail_px[1], &e.head_px[1], 4);
        std::memcpy(&tail_z, &e.head_z, 4);
        in.has_tail = bridged && !cut && tail_z > 0.f;
        in.tail_extrapolated = was_extrapolated;
        in.step_s = step_s;
        in.age_s = e.created ? float(std::int32_t(now_ms - e.created)) * 1e-3f : 1e6f;
        // the head's game time (us, wrapping) and, with the bridge, the comet's ring from the entry
        const std::uint32_t head_us = e.changed_ms * 1000u + e.static_us;
        const auto span_s = [](std::uint32_t newer, std::uint32_t older) {
            const std::int32_t d = std::int32_t(newer - older);
            return d > 0 ? float(d) * 1e-6f : 0.f;
        };
        if (in.has_tail) {
            in.tail_age_s = span_s(head_us, e.head_us);
            in.ring_count = e.ring_count < core::projectile::trail_ring ? e.ring_count : core::projectile::trail_ring;
            for (unsigned k = 0; k < in.ring_count; ++k) {
                std::memcpy(&in.ring_px[k][0], &e.ring_px[k][0], 4);
                std::memcpy(&in.ring_px[k][1], &e.ring_px[k][1], 4);
                in.ring_span_s[k] = span_s(k ? e.ring_us[k - 1] : e.head_us, e.ring_us[k]);
            }
        }
        for (unsigned k = 0; k < 3; ++k) e.read_pos[k] = pos[k];
        e.stage_frame = frame;
        const unsigned family = looks.look[e.body].family;
        BoltShape shape;
        unsigned room = max_quads - n.quads;
        room = room < 1u + core::projectile::trail_ring ? room : 1u + core::projectile::trail_ring;
        const unsigned quads = bolt_quads(view, vfs[family], tunes[family], bodies[e.body], in, out + 4 * n.quads, room,
                                          &shape);
        // the comet's ring: the previous head joins it when the game time advanced (a held head, a paused game keep it
        // and its time); a first frame restarts it from its motion's tail
        if (in.has_tail) {
            if (!shape.held && std::int32_t(head_us - e.head_us) > 0) {
                for (unsigned k = core::projectile::trail_ring; k-- > 1;) {
                    e.ring_px[k][0] = e.ring_px[k - 1][0], e.ring_px[k][1] = e.ring_px[k - 1][1];
                    e.ring_us[k] = e.ring_us[k - 1];
                }
                e.ring_px[0][0] = e.head_px[0], e.ring_px[0][1] = e.head_px[1];
                e.ring_us[0] = e.head_us;
                e.ring_count = e.ring_count < core::projectile::trail_ring ? e.ring_count + 1u : core::projectile::trail_ring;
                e.head_us = head_us;
            }
        } else {
            const bool finite = shape.motion_tail[0] == shape.motion_tail[0] && shape.motion_tail[1] == shape.motion_tail[1] &&
                                shape.motion_tail[0] < 1e30f && shape.motion_tail[0] > -1e30f &&
                                shape.motion_tail[1] < 1e30f && shape.motion_tail[1] > -1e30f;
            const bool ring = finite && shape.tail_age_s > 0.f;
            if (ring) {
                std::memcpy(&e.ring_px[0][0], &shape.motion_tail[0], 4);
                std::memcpy(&e.ring_px[0][1], &shape.motion_tail[1], 4);
                e.ring_us[0] = head_us - std::uint32_t(shape.tail_age_s * 1e6f + 0.5f);
            }
            e.ring_count = ring ? 1u : 0u;
            e.head_us = head_us;
        }
        // the next frame's tail: this frame's head on screen and its depth (0: none, the next frame is a first frame)
        const float head_z = shape.projected ? in.p[2] : 0.f;
        std::memcpy(&e.head_px[0], &shape.head[0], 4);
        std::memcpy(&e.head_px[1], &shape.head[1], 4);
        std::memcpy(&e.head_z, &head_z, 4);
        if (!quads) {
            ++n.culled;
            continue;
        }
        n.dots += shape.dot;
        n.quads += quads;
        n.trail += quads - 1u;
        n.comets += shape.trail_quads != 0;
        n.merged += shape.merged;
        if (shapes) shapes[n.drawn] = shape;
        ++n.drawn;
    }
    return n;
}
} // namespace x3m::weapon_effects::projectile_stage
