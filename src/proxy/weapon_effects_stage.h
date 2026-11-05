#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3 and 5.1.1): the ring's
// stage-owned shield hits (flag_stage) become hex patches. Per owner its hit_slots newest hits; each hit belongs to the
// anchor region of the nearest of the twelve icosahedron vertices (owner frame), whose tangent frame carries the
// lattice: cells are fixed on the owner (a repeated hit in the region re-lights the same cells; no seam crosses one
// hit's patch) and the stereographic plane keeps the hexagons regular within the region (the scale grows at most
// 2 / (1 + cos 37.4 deg) = 1.11 at its edge). One draw per owner and region, rasterizing only the spherical cap that
// bounds its hits' patches (the CPU-side cull: the cost follows the patch, not the owner). An owner without a usable
// ellipsoid (radius 0) gets a flat patch (decal) per hit at the hit point. Cell size and radius are world units per
// PatchTier (the hook's tier of the hit object), the radius capped at max_radius_share of the owner's mean axis; the
// colour follows the newest hit's shield fraction (core::shield_colour), with weapon_tint mixing the weapon colour in.
// The owner's rows are read here, at scene end, through the caller's reader (engine_memory::read in production) and
// validated: obj+0x70 == node, node+0x28 == handle and the dying bit obj+0x40 & 0x8000000 clear, else every record of
// that owner leaves the ring (a dead or replaced owner draws nothing). Ages are game time (cfg+0x718): SETA shortens the
// patch exactly as the engine's own effects. World = engine integers x world_scale (0.01); world -> view in double
// (camera_scene_'s rows), so a far camera keeps the patch on its owner. No allocation, no libm; at most max_patches +
// max_decals outputs, O(ring_capacity x owners) with owners <= max_owners.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
struct Camera {
    float r[9]{}, t[3]{}; // view_j = sum_i world_i r[i*3+j] + t_j (renderer::CameraState)
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
};
struct Owner {
    std::int32_t pos[3]{}, rows[12]{}, radius = 0;
};
enum class OwnerStatus : std::uint8_t { ok, unreadable, mismatch, dying };
inline OwnerStatus read_owner(Reader rd, const core::Event& e, Owner* out) noexcept {
    std::uint32_t node = 0, handle = 0, flags = 0;
    if (!rd(e.owner + core::layout::obj_node, &node, 4) || !rd(e.owner + core::layout::obj_flags, &flags, 4))
        return OwnerStatus::unreadable;
    if (node != e.node) return OwnerStatus::mismatch;
    if (!rd(node + core::layout::node_handle, &handle, 4)) return OwnerStatus::unreadable;
    if (handle != e.handle) return OwnerStatus::mismatch;
    if (flags & core::layout::obj_dying) return OwnerStatus::dying;
    if (!rd(node + core::layout::node_pos, out->pos, 12) || !rd(node + core::layout::node_rot, out->rows, 48) ||
        !rd(e.owner + core::layout::obj_radius, &out->radius, 4))
        return OwnerStatus::unreadable;
    return OwnerStatus::ok;
}
inline void to_view(const Camera& c, const double w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = float(w[0] * double(c.r[j]) + w[1] * double(c.r[3 + j]) + w[2] * double(c.r[6 + j]) + double(c.t[j]));
}
inline void rotate_view(const Camera& c, const double w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = float(w[0] * double(c.r[j]) + w[1] * double(c.r[3 + j]) + w[2] * double(c.r[6 + j]));
}
// The owner's ellipsoid in view space: centre = its node position, axes = its local axes (16.16 rows) x radius.
inline bool shell_geometry(const Owner& o, const Camera& c, float centre[3], float axes[9]) noexcept {
    const double s = double(core::world_scale);
    const double w[3] = {double(o.pos[0]) * s, double(o.pos[1]) * s, double(o.pos[2]) * s};
    to_view(c, w, centre);
    const double R = double(o.radius > 0 ? o.radius : 0) * s / 65536.0;
    if (!(R > 0.0)) return false;
    for (unsigned k = 0; k < 3; ++k) {
        const double a[3] = {double(o.rows[4 * k]) * R, double(o.rows[4 * k + 1]) * R, double(o.rows[4 * k + 2]) * R};
        rotate_view(c, a, axes + 3 * k);
    }
    return true;
}
// The hit point in view space: node position + sum_j pos_j row_j (the engine's local -> world, 16.16 rows).
inline void hit_point(const Owner& o, const std::int32_t pos[3], const Camera& c, float out[3]) noexcept {
    const double s = double(core::world_scale);
    double w[3];
    for (unsigned i = 0; i < 3; ++i) {
        double v = double(o.pos[i]);
        for (unsigned j = 0; j < 3; ++j) v += double(o.rows[4 * j + i]) * double(pos[j]) / 65536.0;
        w[i] = v * s;
    }
    to_view(c, w, out);
}
inline void hit_direction(const std::int32_t pos[3], float out[3]) noexcept {
    const float v[3] = {float(pos[0]), float(pos[1]), float(pos[2])};
    const float n = scalar::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(n > 0.f)) {
        out[0] = out[1] = 0.f;
        out[2] = 1.f;
        return;
    }
    for (unsigned k = 0; k < 3; ++k) out[k] = v[k] / n;
}
inline float mean_axis(const float axes[9]) noexcept {
    float m = 0.f;
    for (unsigned k = 0; k < 3; ++k)
        m += scalar::sqrt(axes[3 * k] * axes[3 * k] + axes[3 * k + 1] * axes[3 * k + 1] + axes[3 * k + 2] * axes[3 * k + 2]);
    return m * (1.f / 3.f);
}
inline float dot3(const float* a, const float* b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline bool normalize3(float* v) noexcept {
    const float n = scalar::sqrt(dot3(v, v));
    if (!(n > 1e-12f)) return false;
    for (unsigned k = 0; k < 3; ++k) v[k] /= n;
    return true;
}
inline void cross3(const float* a, const float* b, float* out) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
// acos without libm (Abramowitz and Stegun 4.4.45, |error| <= 6.7e-5 rad); the caps add a margin above it.
inline float acos_approx(float x) noexcept {
    x = x < -1.f ? -1.f : x > 1.f ? 1.f : x;
    const float a = x < 0.f ? -x : x;
    const float r = scalar::sqrt(1.f - a) * (1.5707288f + a * (-0.2121144f + a * (0.0742610f - 0.0187293f * a)));
    return x < 0.f ? 3.14159265f - r : r;
}
// The twelve anchor directions (the icosahedron's vertices, owner frame) and their tangent frames: u = z x a
// normalized (no vertex lies on z), v = a x u, so (u, v, a) is right-handed. The index is the cell hash's seed.
constexpr unsigned anchor_count = 12;
inline void anchor_frame(unsigned k, float a[3], float u[3], float v[3]) noexcept {
    const float t = 1.6180339887f, n = 1.f / 1.9021130326f; // |(0, 1, t)|
    const float s1 = (k & 1) ? -1.f : 1.f, s2 = (k & 2) ? -1.f : 1.f;
    switch ((k >> 2) % 3) {
    case 0: a[0] = 0.f; a[1] = s1 * n; a[2] = s2 * t * n; break;
    case 1: a[0] = s1 * n; a[1] = s2 * t * n; a[2] = 0.f; break;
    default: a[0] = s2 * t * n; a[1] = 0.f; a[2] = s1 * n; break;
    }
    const float z[3] = {0.f, 0.f, 1.f};
    cross3(z, a, u);
    normalize3(u);
    cross3(a, u, v);
}
inline unsigned nearest_anchor(const float d[3]) noexcept {
    unsigned best = 0;
    float best_dot = -2.f;
    for (unsigned k = 0; k < anchor_count; ++k) {
        float a[3], u[3], v[3];
        anchor_frame(k, a, u, v);
        const float c = dot3(a, d);
        if (c > best_dot) {
            best_dot = c;
            best = k;
        }
    }
    return best;
}
// The lattice plane: q = 2 S (p.u, p.v) / (1 + p.a) (the stereographic projection from the anchor's antipode).
inline void stereo(const float a[3], const float u[3], const float v[3], float surface, const float p[3], float q[2]) noexcept {
    float den = 1.f + dot3(p, a);
    den = den > 1e-3f ? den : 1e-3f;
    q[0] = 2.f * surface * dot3(p, u) / den;
    q[1] = 2.f * surface * dot3(p, v) / den;
}
struct Counts {
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, hits = 0, owners = 0;
};
struct Tuning {
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    float radius_m[core::patch_tier_count] = {core::radius_m[0], core::radius_m[1], core::radius_m[2]};
    float max_radius_share = 0.9f; // the patch radius at most this share of the owner's mean axis (cap < 90 deg)
    core::ShieldBands bands{};
    float weapon_tint = core::weapon_tint, tint_mix = core::tint_mix, soft = core::soft;
};
inline float metres_to_view(float m) noexcept {
    return m * core::engine_units_per_metre * core::world_scale;
}
// The patch colour of a record: its shield fraction's colour, weapon_tint toward the weapon's (classifier) colour.
inline void patch_colour(const core::Event& e, const Tuning& t, float out[3]) noexcept {
    core::shield_colour(core::shield_fraction(e), t.bands, out);
    if (t.weapon_tint > 0.f) {
        float w[3];
        core::event_tint(e, t.tint_mix, w);
        const float k = t.weapon_tint > 1.f ? 1.f : t.weapon_tint;
        for (unsigned i = 0; i < 3; ++i) out[i] += (w[i] - out[i]) * k;
    }
}
// One patch draw from the owner's view-space ellipsoid and the hits of one anchor region (unit-sphere directions in
// the owner's frame, ages in game seconds, newest first): the lattice frame, the planar hits and the cap bounding every
// lit pixel. Lit pixels lie within the planar radius R of a hit and the plane never shrinks distances, so their angle
// from that hit is below R / S; the cap is the hits' spread about their mean direction plus R / S, with a margin for
// acos_approx and the polygonal rim (cos(pi / 32)). False for a degenerate ellipsoid (the caller makes decals).
inline bool fill_patch(renderer::WeaponPatch& out, const float centre[3], const float axes[9], float soft, unsigned tier,
                       unsigned anchor, const float (*dirs)[3], const float* ages, unsigned n, const float colour[3],
                       const Tuning& t) noexcept {
    if (n < 1 || n > core::hit_slots || anchor >= anchor_count) return false;
    tier = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float surface = mean_axis(axes);
    if (!(surface > 0.f)) return false;
    float radius = metres_to_view(t.radius_m[tier]);
    const float cap_r = t.max_radius_share * surface;
    radius = radius < cap_r ? radius : cap_r;
    const float cell = metres_to_view(t.cell_m[tier]);
    if (!(radius > 0.f) || !(cell > 0.f)) return false;
    std::memcpy(out.centre, centre, 12);
    std::memcpy(out.axes, axes, 36);
    out.soft = soft;
    std::memcpy(out.colour, colour, 12);
    anchor_frame(anchor, out.anchor, out.anchor_u, out.anchor_v);
    out.surface = surface;
    out.seed = float(anchor);
    out.cell = cell;
    out.radius = radius;
    out.hits = n;
    float mean[3] = {0.f, 0.f, 0.f};
    for (unsigned i = 0; i < n; ++i) {
        stereo(out.anchor, out.anchor_u, out.anchor_v, surface, dirs[i], out.hit[i]);
        out.age_s[i] = ages[i];
        for (unsigned k = 0; k < 3; ++k) mean[k] += dirs[i][k];
    }
    if (!normalize3(mean)) std::memcpy(mean, dirs[0], 12);
    float spread = 0.f;
    for (unsigned i = 0; i < n; ++i) {
        const float a = acos_approx(dot3(mean, dirs[i]));
        spread = a > spread ? a : spread;
    }
    const float angle = (spread + radius / surface + 1e-3f) * 1.0049f; // 1 / cos(pi / 32)
    out.cap_angle = angle < 3.14159265f ? angle : 3.14159265f;
    std::memcpy(out.cap_dir, mean, 12);
    const float ref[3] = {(mean[2] > 0.9f || mean[2] < -0.9f) ? 1.f : 0.f, 0.f,
                          (mean[2] > 0.9f || mean[2] < -0.9f) ? 0.f : 1.f};
    cross3(ref, mean, out.cap_u);
    normalize3(out.cap_u);
    cross3(mean, out.cap_u, out.cap_v); // (u, v, dir) right-handed
    return true;
}
// Builds the frame's patches and decals from the ring (see the header). now = cfg+0x718.
inline Counts build(core::Ring& ring, std::uint32_t now, Reader rd, const Camera& cam, const Tuning& t,
                    renderer::WeaponPatch* patches, unsigned patch_cap, renderer::WeaponDecal* decals,
                    unsigned decal_cap) noexcept {
    Counts n{};
    struct Seen {
        std::uint32_t owner;
        OwnerStatus status;
        Owner data;
        unsigned count;
        std::uint32_t age[core::hit_slots];
        unsigned index[core::hit_slots];
    };
    Seen seen[core::max_owners];
    unsigned seen_count = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_shield_hit || !(e.flags & core::flag_stage)) continue;
        const std::uint32_t age = core::age_ms(now, e.t0_game_ms);
        if (age > e.lifetime_ms) continue; // Present releases it
        Seen* s = nullptr;
        for (unsigned k = 0; k < seen_count && !s; ++k)
            if (seen[k].owner == e.owner) s = &seen[k];
        if (!s) {
            if (seen_count >= core::max_owners) {
                ++n.dropped;
                continue;
            }
            s = &seen[seen_count++];
            s->owner = e.owner;
            s->count = 0;
            s->status = read_owner(rd, e, &s->data);
        }
        if (s->status != OwnerStatus::ok) {
            n.retired += core::ring_retire_owner(ring, e.owner);
            continue;
        }
        ++n.hits;
        // The newest hit_slots by age (insertion; an older hit past the fourth is not drawn).
        unsigned at = s->count;
        while (at > 0 && s->age[at - 1] > age) --at;
        if (at >= core::hit_slots) continue;
        const unsigned last = s->count < core::hit_slots ? s->count : core::hit_slots - 1;
        for (unsigned k = last; k > at; --k) {
            s->age[k] = s->age[k - 1];
            s->index[k] = s->index[k - 1];
        }
        s->age[at] = age;
        s->index[at] = i;
        if (s->count < core::hit_slots) ++s->count;
    }
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        ++n.owners;
        const core::Event& newest = ring.slots[s.index[0]];
        float colour[3];
        patch_colour(newest, t, colour);
        const unsigned tier = newest.patch_tier < core::patch_tier_count ? unsigned(newest.patch_tier) : unsigned(core::patch_capital);
        float dirs[core::hit_slots][3], ages[core::hit_slots];
        unsigned anchors[core::hit_slots];
        for (unsigned h = 0; h < s.count; ++h) {
            hit_direction(ring.slots[s.index[h]].pos, dirs[h]);
            ages[h] = float(s.age[h]) * 1e-3f;
            anchors[h] = nearest_anchor(dirs[h]);
        }
        float centre[3], axes[9];
        const bool shell = shell_geometry(s.data, cam, centre, axes) && mean_axis(axes) > 0.f;
        if (shell) {
            const float soft = t.soft * mean_axis(axes);
            bool done[core::hit_slots] = {};
            for (unsigned h = 0; h < s.count; ++h) { // one draw per anchor region, newest region first
                if (done[h]) continue;
                float gd[core::hit_slots][3], ga[core::hit_slots];
                unsigned g = 0;
                for (unsigned j = h; j < s.count; ++j)
                    if (!done[j] && anchors[j] == anchors[h]) {
                        done[j] = true;
                        std::memcpy(gd[g], dirs[j], 12);
                        ga[g++] = ages[j];
                    }
                if (n.patches >= patch_cap) {
                    ++n.dropped;
                    continue;
                }
                if (fill_patch(patches[n.patches], centre, axes, soft, tier, anchors[h], gd, ga, g, colour, t))
                    ++n.patches;
                else
                    ++n.dropped;
            }
            continue;
        }
        // No usable ellipsoid: a flat patch per hit at the hit point.
        for (unsigned h = 0; h < s.count; ++h) {
            if (n.decals >= decal_cap) {
                ++n.dropped;
                continue;
            }
            renderer::WeaponDecal& d = decals[n.decals++];
            hit_point(s.data, ring.slots[s.index[h]].pos, cam, d.centre);
            d.radius = metres_to_view(t.radius_m[tier]);
            d.cell = metres_to_view(t.cell_m[tier]);
            d.age_s = ages[h];
            d.soft = t.soft * d.radius;
            std::memcpy(d.colour, colour, 12);
        }
    }
    return n;
}
} // namespace x3m::weapon_effects::stage
