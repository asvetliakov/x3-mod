#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3 and 5.1.1): the ring's
// stage-owned shield hits (flag_stage) become hex patches. Per owner its owner_hits newest hits; each hit belongs to
// the anchor region of the nearest of the owner's six axis directions (owner frame), whose tangent frame carries the
// lattice: the grid is fixed on the owner (every hit in the region lands on the same cells, whatever the owner's
// motion; no seam crosses one hit's footprint) and the stereographic plane keeps the hexagons regular within the region
// (the scale grows at most 2 / (1 + cos 54.7 deg) = 1.27 at a cube corner). The cell map of each region is rebuilt from the
// live hits every frame (fill_patch: a cell's clock is the newest hit that lit it), so it needs no state of its own and
// a retired owner or a Reset leaves nothing behind. One draw per owner and region, rasterizing only the spherical cap
// that bounds its lit cells (the CPU-side cull: the cost follows the patch, not the owner). An owner without a usable
// ellipsoid (radius 0) gets a flat patch (decal) per hit at the hit point. Cell size and footprint rings are per
// PatchTier (the hook's tier of the hit object); the colour follows the newest hit's shield fraction (core::shield_colour), with weapon_tint mixing the weapon colour in.
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
// The six anchor directions (the owner's local axes +-z, +-x, +-y: the cube's faces, owner frame) and their tangent
// frames: u the next axis (z -> x, x -> y, y -> z), v = a x u, so (u, v, a) is right-handed. Centring the regions on
// the axes puts the nose, tail, flanks, top and belly (where sustained fire lands) mid-region, the seams on the cube's
// edges 45 deg away; the stereographic scale grows at most 2 / (1 + cos 54.7 deg) = 1.27 at a cube corner. The index
// is the cell hash's seed.
constexpr unsigned anchor_count = 6;
inline void anchor_frame(unsigned k, float a[3], float u[3], float v[3]) noexcept {
    const unsigned i = (2u + (k >> 1)) % 3u, j = (i + 1u) % 3u; // k 0, 1: z; 2, 3: x; 4, 5: y
    for (unsigned c = 0; c < 3; ++c) {
        a[c] = 0.f;
        u[c] = 0.f;
    }
    a[i] = (k & 1u) ? -1.f : 1.f;
    u[j] = 1.f;
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
    unsigned rings[core::patch_tier_count] = {core::rings[0], core::rings[1], core::rings[2]};
    // The cell clocks' law (the pass's WeaponLook carries the same defaults): ring speed in footprint radii per game
    // second, the mean fade and its per-cell spread.
    float ripple_speed = core::ripple_speed, fade_s = float(core::fade_ms) * 1e-3f, stagger = core::stagger;
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
constexpr unsigned max_rings = 6; // a footprint's cell centres within rings cells of the hit cell's centre (exact to 6)
inline unsigned tier_rings(const Tuning& t, unsigned tier) noexcept {
    const unsigned r = t.rings[tier < core::patch_tier_count ? tier : unsigned(core::patch_capital)];
    return r < max_rings ? r : max_rings;
}
// The cell of plane point g (cells), weapon_hex_patch.hlsl hex_cell in float: the integer key (2 cx, 2 cy / sqrt 3)
// and the centre (cells).
inline void hex_key(const float g[2], int key[2], float centre[2]) noexcept {
    const float sx = 1.f, sy = 1.7320508f;
    auto fl = [](float x) { return float(scalar::floor(double(x))); };
    const float ax = g[0] - sx * fl(g[0] / sx) - 0.5f * sx, ay = g[1] - sy * fl(g[1] / sy) - 0.5f * sy;
    const float hx = g[0] - 0.5f * sx, hy = g[1] - 0.5f * sy;
    const float bx = hx - sx * fl(hx / sx) - 0.5f * sx, by = hy - sy * fl(hy / sy) - 0.5f * sy;
    const bool use_a = ax * ax + ay * ay < bx * bx + by * by;
    centre[0] = g[0] - (use_a ? ax : bx);
    centre[1] = g[1] - (use_a ? ay : by);
    key[0] = int(fl(centre[0] * 2.f + 0.5f));
    key[1] = int(fl(centre[1] * 1.1547005f + 0.5f));
}
inline float pack_key(int kx, int ky) noexcept {
    return float((kx + renderer::weapon_cell_key_bias) * 4096 + (ky + renderer::weapon_cell_key_bias));
}
// One patch draw from the owner's view-space ellipsoid and the hits of one anchor region (unit-sphere directions in
// the owner's frame, ages in game seconds, newest first, at most core::owner_hits): the lattice frame, the cell map,
// the ring/core hits and the cap bounding every lit cell. Each hit lights its footprint, the cells whose centre lies
// within rings cells of the hit cell's centre, each once the ring front (v = ripple_speed x (rings + 0.5) cells) has
// reached it; a cell covered by several hits keeps the newest clock (the smallest non-negative time since lit), so a
// later hit resets a cell to full and never adds; a cell past the slowest fade is dropped; the map holds the first
// weapon_patch_cells cells, newest hit first (an older hit's cells are dropped when it is full). Ring/core slots: the
// region's newest hits younger than the ring's travel (1 / ripple_speed). A lit pixel lies within reach + a cell's
// circumradius (planar) of a contributing hit's cell centre, and the plane never shrinks distances, so its angle from
// that hit is below that (plus the hit's offset in its cell, under the circumradius) over S: the cap is the
// contributing hits' spread about their mean direction plus that, with a margin for acos_approx and the polygonal rim
// (cos(pi / 32)). False for a degenerate ellipsoid or nothing to draw. O(n x footprint x cells), n <= owner_hits.
inline bool fill_patch(renderer::WeaponPatch& out, const float centre[3], const float axes[9], float soft, unsigned tier,
                       unsigned anchor, const float (*dirs)[3], const float* ages, unsigned n, const float colour[3],
                       const Tuning& t) noexcept {
    if (n < 1 || n > core::owner_hits || anchor >= anchor_count) return false;
    tier = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float surface = mean_axis(axes);
    if (!(surface > 0.f)) return false;
    const float cell = metres_to_view(t.cell_m[tier]);
    const int rings = int(tier_rings(t, tier));
    if (!(cell > 0.f) || !(t.ripple_speed > 0.f) || !(t.fade_s > 0.f)) return false;
    std::memcpy(out.centre, centre, 12);
    std::memcpy(out.axes, axes, 36);
    out.soft = soft;
    std::memcpy(out.colour, colour, 12);
    anchor_frame(anchor, out.anchor, out.anchor_u, out.anchor_v);
    out.surface = surface;
    out.seed = float(anchor);
    out.cell = cell;
    out.radius = (float(rings) + 0.5f) * cell;
    out.reach = float(rings) * cell;
    out.hits = 0;
    out.cells = 0;
    const float v = t.ripple_speed * out.radius, ring_life = 1.f / t.ripple_speed;
    const float max_since = t.fade_s * (1.f + 0.5f * t.stagger);
    float mean[3] = {0.f, 0.f, 0.f};
    bool used[core::owner_hits] = {};
    for (unsigned i = 0; i < n; ++i) {
        float q[2], g[2], c[2];
        int key[2];
        stereo(out.anchor, out.anchor_u, out.anchor_v, surface, dirs[i], q);
        g[0] = q[0] / cell;
        g[1] = q[1] / cell;
        hex_key(g, key, c);
        if (out.hits < renderer::weapon_patch_hits && ages[i] < ring_life) {
            const unsigned h = out.hits++;
            out.hit[h][0] = q[0];
            out.hit[h][1] = q[1];
            out.hit_cell[h][0] = c[0] * cell;
            out.hit_cell[h][1] = c[1] * cell;
            out.age_s[h] = ages[i];
            used[i] = true;
        }
        for (int dy = -rings; dy <= rings; ++dy)
            for (int dx = -2 * rings; dx <= 2 * rings; ++dx) {
                const int ady = dy < 0 ? -dy : dy, adx = dx < 0 ? -dx : dx;
                if (((dx + dy) & 1) || (ady > (adx + ady) / 2 ? ady : (adx + ady) / 2) > rings) continue; // hex distance
                const int kx = key[0] + dx, ky = key[1] + dy;
                if (kx <= -renderer::weapon_cell_key_bias || kx >= renderer::weapon_cell_key_bias ||
                    ky <= -renderer::weapon_cell_key_bias || ky >= renderer::weapon_cell_key_bias)
                    continue;
                const float ox = 0.5f * float(dx), oy = 0.8660254f * float(dy);
                const float since = ages[i] - cell * scalar::sqrt(ox * ox + oy * oy) / v;
                if (since < 0.f || since > max_since) continue; // not reached yet, or faded out
                const float packed = pack_key(kx, ky);
                unsigned at = 0;
                while (at < out.cells && out.cell_key[at] != packed) ++at;
                if (at < out.cells) {
                    if (since < out.cell_since[at]) out.cell_since[at] = since;
                    used[i] = true;
                } else if (out.cells < renderer::weapon_patch_cells) {
                    out.cell_key[out.cells] = packed;
                    out.cell_since[out.cells++] = since;
                    used[i] = true;
                }
            }
        if (used[i])
            for (unsigned k = 0; k < 3; ++k) mean[k] += dirs[i][k];
    }
    if (!out.cells && !out.hits) return false;
    if (!normalize3(mean)) std::memcpy(mean, dirs[0], 12);
    float spread = 0.f;
    for (unsigned i = 0; i < n; ++i) {
        if (!used[i]) continue;
        const float a = acos_approx(dot3(mean, dirs[i]));
        spread = a > spread ? a : spread;
    }
    // The outermost centres plus a cell's circumradius, plus the hit's own offset from its cell's centre (under the
    // circumradius).
    const float extent = out.reach + 2.f * 0.5773503f * cell;
    const float angle = (spread + extent / surface + 1e-3f) * 1.0049f; // 1 / cos(pi / 32)
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
        std::uint32_t age[core::owner_hits];
        unsigned index[core::owner_hits];
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
        // The newest owner_hits by age (insertion; an older hit past them feeds no cell).
        unsigned at = s->count;
        while (at > 0 && s->age[at - 1] > age) --at;
        if (at >= core::owner_hits) continue;
        const unsigned last = s->count < core::owner_hits ? s->count : core::owner_hits - 1;
        for (unsigned k = last; k > at; --k) {
            s->age[k] = s->age[k - 1];
            s->index[k] = s->index[k - 1];
        }
        s->age[at] = age;
        s->index[at] = i;
        if (s->count < core::owner_hits) ++s->count;
    }
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        ++n.owners;
        const core::Event& newest = ring.slots[s.index[0]];
        float colour[3];
        patch_colour(newest, t, colour);
        const unsigned tier = newest.patch_tier < core::patch_tier_count ? unsigned(newest.patch_tier) : unsigned(core::patch_capital);
        float dirs[core::owner_hits][3], ages[core::owner_hits];
        unsigned anchors[core::owner_hits];
        for (unsigned h = 0; h < s.count; ++h) {
            hit_direction(ring.slots[s.index[h]].pos, dirs[h]);
            ages[h] = float(s.age[h]) * 1e-3f;
            anchors[h] = nearest_anchor(dirs[h]);
        }
        float centre[3], axes[9];
        const bool shell = shell_geometry(s.data, cam, centre, axes) && mean_axis(axes) > 0.f;
        if (shell) {
            const float soft = t.soft * mean_axis(axes);
            bool done[core::owner_hits] = {};
            for (unsigned h = 0; h < s.count; ++h) { // one draw per anchor region, newest region first
                if (done[h]) continue;
                float gd[core::owner_hits][3], ga[core::owner_hits];
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
        // No usable ellipsoid: a flat patch per hit at the hit point (the newest hit_slots).
        for (unsigned h = 0; h < s.count && h < core::hit_slots; ++h) {
            if (n.decals >= decal_cap) {
                ++n.dropped;
                continue;
            }
            renderer::WeaponDecal& d = decals[n.decals++];
            hit_point(s.data, ring.slots[s.index[h]].pos, cam, d.centre);
            d.cell = metres_to_view(t.cell_m[tier]);
            d.radius = (float(tier_rings(t, tier)) + 0.5f) * d.cell;
            d.reach = float(tier_rings(t, tier)) * d.cell;
            d.age_s = ages[h];
            d.soft = t.soft * d.radius;
            std::memcpy(d.colour, colour, 12);
        }
    }
    return n;
}
} // namespace x3m::weapon_effects::stage
