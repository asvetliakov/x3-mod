#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "sse_scalar.h"
#include "weapon_effects_bubble.h"
#include "weapon_effects_projectile.h"
#include "weapon_projectile_family.h"

// The projectile stage's CPU half (docs/architecture/projectile-look.md section 2 and "Implementation (2026-11-08)"):
// every live pool entry (weapon_effects_projectile.h) validated through the bounded reader, its node read once (live,
// handle, position +0x30, velocity +0x100, body +0x144; one read, no object read: a recycled node changes handle) and turned into one screen-space quad of four vertices: a line of
// light from P - s/2 v^ to P + s/2 v^ (P the body origin, the mesh is centred there), s = stretch_k |v| dt_game (the
// engine's game-time step of the frame, so SETA lengthens it) capped by length_cap_m and by the distance flown since
// birth, none past lod_dot_m (a dot). The core's full width is core_px at the 500 m reference through the live
// projection: its world width is core_px x 500 m / f (f = m11 H / 2, the frame's projection, never a fixed focal
// length), drawn at w = world width x f / z px; under 1 px it is drawn 1 px wide at radiance x w (a far bolt dims, it
// never fattens). A Gaussian glow of sigma glow_ratio x w / 2 (floor 1.5 px, its radiance scaled by the floor's lost
// area) in the row's colour at glow_gain; the core colour lerp(tint, white, hot_share (1 + calibre)) at
// 2^(stops + calibre). The quad spans the segment plus, on every side, the larger of the two Gaussians' reach to
// radiance_cut (1/256 of scene white) and half a pixel; the pixel program
// (weapon_projectile_ps.hlsl) evaluates the two Gaussians of the distance to the segment. Depth per end
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
constexpr unsigned vertices_per_bolt = 4, indices_per_bolt = 6;
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
// One bolt in view units: its origin (view space), its velocity (view units per second, view space), the game-time
// step and its age (seconds).
struct BoltIn {
    float p[3]{}, v[3]{};
    float step_s = 0.f, age_s = 1e6f;
};
struct BoltShape { // diagnostics and the fixture's rule checks
    float depth_m = 0, stretch_m = 0, length_px = 0, core_px = 0, dim = 1, glow_sigma_px = 0, ext_px = 0;
    float raster[4][2]{};
    bool dot = false;
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
// The four vertices of one bolt (false: culled, behind the near plane, off the target or degenerate).
inline bool bolt_vertices(const View& c, const ViewFrame& vf, const pf::Tune& tune, const BodyFrame& body,
                          const BoltIn& in, renderer::WeaponProjectileVertex out[4], BoltShape* shape = nullptr) noexcept {
    if (!vf.ok || !(in.p[2] > vf.zn)) return false;
    const float zn = vf.zn;
    const float inv_z = 1.f / in.p[2];
    const float depth_m = in.p[2] * (1.f / view_units_per_metre);
    const float speed2 = in.v[0] * in.v[0] + in.v[1] * in.v[1] + in.v[2] * in.v[2];
    const float speed = scalar::sqrt(speed2);
    float dir[3] = {1.f, 0.f, 0.f};
    if (speed > 1e-3f) {
        const float inv = 1.f / speed;
        for (unsigned k = 0; k < 3; ++k) dir[k] = in.v[k] * inv;
    }
    float s = tune.stretch_k * speed * (in.step_s > 0.f ? in.step_s : 0.f);
    const float cap = tune.length_cap_m * view_units_per_metre, flown = speed * (in.age_s > 0.f ? in.age_s : 0.f);
    s = s < cap ? s : cap;
    s = s < flown ? s : flown;
    const bool dot = depth_m > tune.lod_dot_m;
    if (dot || !(s > 0.f)) s = 0.f;
    float a[3], b[3];
    for (unsigned k = 0; k < 3; ++k) {
        a[k] = in.p[k] - 0.5f * s * dir[k];
        b[k] = in.p[k] + 0.5f * s * dir[k];
    }
    // the segment clipped to the near plane (the centre is in front)
    if (a[2] < zn) {
        const float u = (zn - b[2]) / (a[2] - b[2]);
        for (unsigned k = 0; k < 3; ++k) a[k] = b[k] + u * (a[k] - b[k]);
    }
    if (b[2] < zn) {
        const float u = (zn - a[2]) / (b[2] - a[2]);
        for (unsigned k = 0; k < 3; ++k) b[k] = a[k] + u * (b[k] - a[k]);
    }
    // raster positions (D3D9: pixel centres at integer coordinates, ndc = 2 r / size - 1)
    const float ia = 1.f / a[2], ib = s > 0.f ? 1.f / b[2] : ia;
    const float ra[2] = {(c.m00 * a[0] * ia + c.m20 + 1.f) * vf.half_w, (1.f - c.m11 * a[1] * ia - c.m21) * vf.half_h};
    const float rb[2] = {(c.m00 * b[0] * ib + c.m20 + 1.f) * vf.half_w, (1.f - c.m11 * b[1] * ib - c.m21) * vf.half_h};
    // the width through the live projection (view_frame); under 1 px drawn at 1 px with the radiance scaled
    const float w_true = vf.world_w_per_px * inv_z;
    const float w = w_true > 1.f ? w_true : 1.f, dim = w_true > 1.f ? 1.f : w_true;
    const float sigma_c = w * (1.f / 2.35482f);
    const float sigma_raw = tune.glow_ratio * w_true * 0.5f;
    const float sigma_g = sigma_raw > glow_floor_px ? sigma_raw : glow_floor_px;
    const float glow_area = sigma_raw > glow_floor_px ? 1.f : sigma_raw * (1.f / glow_floor_px);
    const float glow_scale = glow_area * glow_area;
    const float kc = 0.5f / (sigma_c * sigma_c), kg = 0.5f / (sigma_g * sigma_g);
    const float lc = body.log2_core + (dim < 1.f ? log2f_poly(dim) : 0.f);
    const float lg = body.log2_glow + (glow_scale < 1.f ? log2f_poly(glow_scale) : 0.f);
    const float rc = reach(sigma_c, lc), rg = reach(sigma_g, lg);
    const float ext = (rc > rg ? rc : rg) + quad_margin_px;
    float ux = rb[0] - ra[0], uy = rb[1] - ra[1];
    const float len = scalar::sqrt(ux * ux + uy * uy);
    if (len > 1e-4f) {
        const float inv = 1.f / len;
        ux *= inv;
        uy *= inv;
    } else {
        ux = 1.f;
        uy = 0.f;
    }
    const float nx = -uy, ny = ux, half = len > 1e-4f ? 0.5f * len : 0.f;
    const float shape4[4] = {body.core_scale * dim, body.glow_scale * glow_scale, kc, kg};
    const float za = c.m22 + c.m32 * ia, zb = c.m22 + c.m32 * ib;
    const float zs[2] = {za < 0.f ? 0.f : za > 1.f ? 1.f : za, zb < 0.f ? 0.f : zb > 1.f ? 1.f : zb};
    // corners: A - u ext -+ n ext, B + u ext -+ n ext; local (along, across) about the segment's centre
    static constexpr float corner[4][3] = {{-1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, -1.f, 1.f}, {1.f, 1.f, 1.f}};
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    for (unsigned i = 0; i < 4; ++i) {
        const float* e = corner[i];
        const bool end_b = e[2] > 0.5f;
        const float* base = end_b ? rb : ra;
        const float px = base[0] + ux * ext * e[0] + nx * ext * e[1];
        const float py = base[1] + uy * ext * e[0] + ny * ext * e[1];
        renderer::WeaponProjectileVertex& v = out[i];
        v.pos[0] = px * vf.ndc_x - 1.f;
        v.pos[1] = 1.f - py * vf.ndc_y;
        v.pos[2] = zs[end_b ? 1 : 0];
        v.local[0] = e[0] * (half + ext);
        v.local[1] = e[1] * ext;
        v.local[2] = half;
        for (unsigned k = 0; k < 4; ++k) v.shape[k] = shape4[k];
        v.core_tint = body.core_tint;
        v.glow_tint = body.glow_tint;
        minx = px < minx ? px : minx;
        maxx = px > maxx ? px : maxx;
        miny = py < miny ? py : miny;
        maxy = py > maxy ? py : maxy;
        if (shape) {
            shape->raster[i][0] = px;
            shape->raster[i][1] = py;
        }
    }
    if (shape) {
        shape->depth_m = depth_m;
        shape->stretch_m = s / view_units_per_metre;
        shape->length_px = len;
        shape->core_px = w;
        shape->dim = dim;
        shape->glow_sigma_px = sigma_g;
        shape->ext_px = ext;
        shape->dot = dot;
    }
    // off the target (a pixel centre lies in [-0.5, size - 0.5]); a non-finite corner (never from finite engine
    // integers in front of the near plane) is culled, never drawn
    return minx == minx && maxx == maxx && miny == miny && maxy == maxy && maxx < 1e30f && minx > -1e30f &&
           maxy < 1e30f && miny > -1e30f &&
           !(maxx < -0.5f || maxy < -0.5f || minx > c.width - 0.5f || miny > c.height - 0.5f);
}
// The single-bolt form (the fixture's rule checks): the view and body constants of this call alone.
inline bool bolt_vertices(const View& c, const pf::Tune& tune, const BodyLook& look, const BoltIn& in,
                          renderer::WeaponProjectileVertex out[4], BoltShape* shape = nullptr) noexcept {
    return bolt_vertices(c, view_frame(c, tune.core_px), tune, body_frame(tune, look), in, out, shape);
}
struct Counts {
    unsigned tracked = 0, drawn = 0, culled = 0, expired = 0, invalid = 0, dots = 0;
};
// The per-body looks, resolved once per body (the pool's body list only grows until the next install).
struct Looks {
    unsigned resolved = 0;
    BodyLook look[core::projectile::max_bodies];
};
// The frame's bolts: walks the pool from the last entry (a retired entry is swap-removed), reads each entry's node
// (live, handle, position, velocity, body) in one bounded read, retires an expired entry, one the tap has not seen for
// max(stale_ms, three game steps), a recycled node (another handle or body) or an unreadable one, and writes up to max_bolts quads into out. tunes[family] are the families'
// numbers (the baked constants or the dev tuning file's). now_ms / step_ms: cfg +0x708 / +0x704 (FL), game ms.
inline Counts build(core::ProjectilePool& pool, Reader rd, const core::Table* table, Looks& looks, std::uint32_t now_ms,
                    std::uint32_t step_ms, const View& view, const pf::Tune* tunes,
                    renderer::WeaponProjectileVertex* out, unsigned max_bolts) noexcept {
    Counts n;
    for (; looks.resolved < pool.body_count; ++looks.resolved)
        looks.look[looks.resolved] = body_look(table, pool.bodies[looks.resolved], rd, pool.globals);
    if (!pool.count) return n;
    const float step_s = float(step_ms < 1000u ? step_ms : 1000u) * 1e-3f;
    // not seen by the object tap for stale_ms game ms, at least three game steps (SETA-aware): retired
    const std::uint32_t unseen_ms = 3u * step_ms > core::projectile::stale_ms ? 3u * step_ms : core::projectile::stale_ms;
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
    // per family the view constants, per body the frame's radiance
    ViewFrame vfs[pf::family_count];
    for (unsigned f = 0; f < pf::family_count; ++f) vfs[f] = view_frame(view, tunes[f].core_px);
    BodyFrame bodies[core::projectile::max_bodies];
    for (unsigned b = 0; b < pool.body_count && b < core::projectile::max_bodies; ++b)
        bodies[b] = body_frame(tunes[looks.look[b].family], looks.look[b]);
    for (unsigned i = pool.count; i-- > 0;) {
        const core::ProjectileEntry e = pool.entries[i];
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
        if (n.drawn >= max_bolts) continue;
        BoltIn in;
        float rel[3], vel[3];
        for (unsigned k = 0; k < 3; ++k) {
            const std::int32_t pos = std::int32_t(u32(nb, core::layout::node_pos - node_at + 4 * k));
            rel[k] = (float(std::int32_t(std::uint32_t(pos) - std::uint32_t(eye_i[k]))) - eye_f[k]) * core::world_scale;
            vel[k] = float(std::int32_t(u32(nb, core::projectile::node_velocity - node_at + 4 * k))) * core::world_scale;
        }
        rotate_view(view, rel, in.p);
        rotate_view(view, vel, in.v);
        in.step_s = step_s;
        in.age_s = e.created ? float(std::int32_t(now_ms - e.created)) * 1e-3f : 1e6f;
        const unsigned family = looks.look[e.body].family;
        BoltShape shape;
        if (!bolt_vertices(view, vfs[family], tunes[family], bodies[e.body], in, out + 4 * n.drawn, &shape)) {
            ++n.culled;
            continue;
        }
        n.dots += shape.dot;
        ++n.drawn;
    }
    return n;
}
} // namespace x3m::weapon_effects::projectile_stage
