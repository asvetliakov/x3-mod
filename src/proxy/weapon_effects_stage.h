#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <algorithm>

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3 and 5.1.1): the ring's
// stage-owned shield hits (flag_stage) become hex patches. Per owner its owner_hits newest hits; each hit belongs to
// the anchor region of the nearest of the owner's six axis directions (owner frame), whose tangent frame carries the
// lattice: the grid is fixed on the owner (every hit in the region lands on the same cells, whatever the owner's
// motion; no seam crosses one hit's footprint) and the stereographic plane keeps the hexagons regular within the region
// (the scale grows at most 2 / (1 + cos 54.7 deg) = 1.27 at a cube corner). The cell map of each region is rebuilt from the
// live hits every frame (fill_region: a cell's clock is the newest hit that lit it), so it needs no state of its own
// and a retired owner or a Reset leaves nothing behind. One draw per owner and region, more when its lit cells exceed
// one map (spills, from the frame's budget after every region's first draw; past min(region_draws) maps the oldest
// clocks are dropped and counted), each rasterizing only the spherical cap that bounds its cells (the CPU-side cull:
// the cost follows the patch, not the owner); the draws of two regions whose caps overlap share the light across the
// seam (link_seams, weapon_shell_ps.hlsl). Hits past an owner's owner_hits newest are counted. An owner without a usable
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
// dropped: a region or decal the frame's budget (patch_cap, decal_cap, max_owners) or a degenerate ellipsoid refused (a
// region whose cells have all faded draws nothing and is not a drop); cells_dropped: lit cells no draw carries (a
// region's draws full: the oldest clocks, the dimmest cells, go first); hits_dropped: live hits past an owner's
// owner_hits newest (their cells feed no map); spills: a region's draws past its first (lit cells beyond one map).
struct Counts {
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, hits = 0, owners = 0;
    unsigned cells_dropped = 0, hits_dropped = 0, spills = 0;
};
// The draws one region may take (its lit cells in maps of weapon_patch_cells; past them the oldest clocks are dropped).
constexpr unsigned region_draws = 8;
struct Tuning {
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    unsigned rings[core::patch_tier_count] = {core::rings[0], core::rings[1], core::rings[2]};
    // The cell clocks' law (the pass's WeaponLook carries the same defaults): ring speed in footprint radii per game
    // second, the mean fade and its per-cell spread.
    float ripple_speed = core::ripple_speed, fade_s = float(core::fade_ms) * 1e-3f, stagger = core::stagger;
    core::ShieldBands bands{};
    float weapon_tint = core::weapon_tint, tint_mix = core::tint_mix, soft = core::soft;
    unsigned region_draws = stage::region_draws; // >= 1 (the fixture's eviction witness sets 1)
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
// A region's lit-cell candidates (fill_region's scratch, on the caller's stack: about 26 KB): every footprint cell of
// the region's hits once, with its newest clock and the hit that set it, found through an open-addressed table.
constexpr unsigned footprint_cells(unsigned r) noexcept {
    return 1 + 3 * r * (r + 1);
}
constexpr unsigned header_rings() noexcept {
    unsigned m = 0;
    for (unsigned r : core::rings) m = r > m ? r : m;
    return m < max_rings ? m : max_rings;
}
constexpr unsigned region_candidates = core::owner_hits * footprint_cells(header_rings()); // 1216 at rings 2
constexpr unsigned candidate_slots = 4096; // a power of two, at least twice the candidates
static_assert(candidate_slots >= 2 * region_candidates && (candidate_slots & (candidate_slots - 1)) == 0,
              "the candidate table's load stays at or below one half");
static_assert(region_candidates < 65535 && core::owner_hits <= 255, "candidate indices are 16-bit, hit indices 8-bit");
static_assert(renderer::weapon_patch_cells == core::cell_map_capacity, "one cell-map capacity");
struct RegionScratch {
    std::int32_t key[region_candidates]; // packed (kx + bias) * 4096 + (ky + bias)
    float since[region_candidates];
    std::uint8_t hit[region_candidates]; // the hit whose clock the cell carries (its footprint contains the cell)
    std::uint8_t centre[region_candidates]; // 1: that hit's own cell (the marker level)
    std::uint16_t order[region_candidates];
    union {
        float select[region_candidates];         // the clocks, for the eviction threshold
        std::uint32_t morton[region_candidates]; // the cells' Morton codes, for the spill draws' spatial chunks
    };
    std::uint16_t table[candidate_slots]; // 0 empty, else the candidate's index + 1
};
inline std::uint32_t spread_bits(std::uint32_t v) noexcept { // 12 bits to the even bits of 24
    v &= 0xfffu;
    v = (v | (v << 8)) & 0x00ff00ffu;
    v = (v | (v << 4)) & 0x0f0f0f0fu;
    v = (v | (v << 2)) & 0x33333333u;
    v = (v | (v << 1)) & 0x55555555u;
    return v;
}
// The unit-sphere direction of the lattice plane point q (view units) of a region (the inverse stereographic map).
inline void plane_to_sphere(const renderer::WeaponPatch& p, const float q[2], float out[3]) noexcept {
    const float X = q[0] / (2.f * p.surface), Y = q[1] / (2.f * p.surface), r2 = X * X + Y * Y, k = 1.f / (1.f + r2);
    for (unsigned c = 0; c < 3; ++c)
        out[c] = (2.f * X * p.anchor_u[c] + 2.f * Y * p.anchor_v[c] + (1.f - r2) * p.anchor[c]) * k;
}
inline void cap_frame(renderer::WeaponPatch& out, const float mean[3], float angle) noexcept {
    angle = angle * 1.0049f; // 1 / cos(pi / 32): the polygonal rim
    out.cap_angle = angle < 3.14159265f ? angle : 3.14159265f;
    std::memcpy(out.cap_dir, mean, 12);
    const float ref[3] = {(mean[2] > 0.9f || mean[2] < -0.9f) ? 1.f : 0.f, 0.f,
                          (mean[2] > 0.9f || mean[2] < -0.9f) ? 0.f : 1.f};
    cross3(ref, mean, out.cap_u);
    normalize3(out.cap_u);
    cross3(mean, out.cap_u, out.cap_v); // (u, v, dir) right-handed
}
struct RegionStats {
    unsigned cells = 0;   // lit cells drawn
    unsigned dropped = 0; // lit cells left out (the region's draws full, or the scratch full)
    bool refused = false; // a degenerate ellipsoid, lattice or law (no draw; nothing lit is not refused)
};
// The draws of the owner's view-space ellipsoid and the hits of one anchor region (unit-sphere directions in the owner's
// frame, ages in game seconds, newest first, at most core::owner_hits): the lattice frame, the cell maps, the ring/core
// hits and the cap bounding every lit cell of each draw. Each hit lights its footprint, the cells whose centre lies
// within rings cells of the hit cell's centre, each once the ring front (v = ripple_speed x (rings + 0.5) cells) has
// reached it; a cell covered by several hits keeps the newest clock (the smallest non-negative time since lit), so a
// later hit resets a cell to full and never adds; a cell past the slowest fade is dropped. The region's lit cells go
// into maps of weapon_patch_cells, one draw each, at most min(out_cap, t.region_draws) draws (every cell in exactly one
// draw of the same lattice, so the draws add without doubling a cell); past them the cells with the oldest clocks (the
// dimmest) are left out and counted. Ring slots (first draw only): the region's newest hits younger than the
// ring's travel (1 / ripple_speed). One draw: its cap is the contributing hits' spread about their mean direction plus
// a footprint's extent (a lit pixel lies within reach + a cell's circumradius (planar) of its hit cell's centre and the
// plane never shrinks distances, so its angle from that hit is below that, plus the hit's offset in its cell, over S).
// Several draws: the cells are chunked in Morton order of their keys (compact chunks) and each cap bounds its cells'
// centres plus a circumradius over S (the first adds its ring hits' footprints); margins for acos_approx and the
// polygonal rim (cos(pi / 32)). Returns the draws written (0 for a degenerate ellipsoid, stats->refused, or for nothing
// lit). O(n x footprint) through the table; a spill adds a sort of the cells.
inline unsigned fill_region(renderer::WeaponPatch* outs, unsigned out_cap, const float centre[3], const float axes[9],
                            float soft, unsigned tier, unsigned anchor, const float (*dirs)[3], const float* ages,
                            unsigned n, const float colour[3], const Tuning& t, RegionStats* stats = nullptr) noexcept {
    RegionStats local{};
    RegionStats& st = stats ? *stats : local;
    st = RegionStats{};
    st.refused = true;
    if (!outs || !out_cap || n < 1 || n > core::owner_hits || anchor >= anchor_count) return 0;
    tier = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float surface = mean_axis(axes);
    if (!(surface > 0.f)) return 0;
    const float cell = metres_to_view(t.cell_m[tier]);
    const int rings = int(tier_rings(t, tier));
    if (!(cell > 0.f) || !(t.ripple_speed > 0.f) || !(t.fade_s > 0.f)) return 0;
    st.refused = false;
    renderer::WeaponPatch& head = outs[0];
    std::memcpy(head.centre, centre, 12);
    std::memcpy(head.axes, axes, 36);
    head.soft = soft;
    std::memcpy(head.colour, colour, 12);
    anchor_frame(anchor, head.anchor, head.anchor_u, head.anchor_v);
    head.surface = surface;
    head.seed = float(anchor);
    head.cell = cell;
    head.radius = (float(rings) + 0.5f) * cell;
    head.reach = float(rings) * cell;
    head.hits = 0;
    head.cells = 0;
    head.seams = 0;
    const float v = t.ripple_speed * head.radius, ring_life = 1.f / t.ripple_speed;
    const float max_since = t.fade_s * (1.f + 0.5f * t.stagger);
    RegionScratch sc;
    std::memset(sc.table, 0, sizeof sc.table);
    unsigned count = 0;
    bool ring_hit[core::owner_hits] = {};
    for (unsigned i = 0; i < n; ++i) {
        float q[2], g[2], c[2];
        int key[2];
        stereo(head.anchor, head.anchor_u, head.anchor_v, surface, dirs[i], q);
        g[0] = q[0] / cell;
        g[1] = q[1] / cell;
        hex_key(g, key, c);
        if (head.hits < renderer::weapon_patch_hits && ages[i] < ring_life) {
            const unsigned h = head.hits++;
            head.hit[h][0] = q[0];
            head.hit[h][1] = q[1];
            head.hit_cell[h][0] = c[0] * cell;
            head.hit_cell[h][1] = c[1] * cell;
            head.age_s[h] = ages[i];
            ring_hit[i] = true;
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
                const std::int32_t packed =
                    (kx + renderer::weapon_cell_key_bias) * 4096 + (ky + renderer::weapon_cell_key_bias);
                unsigned slot = (std::uint32_t(packed) * 2654435761u) >> 20; // the top 12 bits: candidate_slots
                while (sc.table[slot] && sc.key[sc.table[slot] - 1u] != packed) slot = (slot + 1u) & (candidate_slots - 1u);
                if (sc.table[slot]) {
                    const unsigned at = sc.table[slot] - 1u;
                    if (since < sc.since[at]) {
                        sc.since[at] = since;
                        sc.hit[at] = std::uint8_t(i);
                        sc.centre[at] = std::uint8_t(!dx && !dy);
                    }
                } else if (count < region_candidates) {
                    sc.key[count] = packed;
                    sc.since[count] = since;
                    sc.hit[count] = std::uint8_t(i);
                    sc.centre[count] = std::uint8_t(!dx && !dy);
                    sc.table[slot] = std::uint16_t(++count);
                } else {
                    ++st.dropped; // a footprint beyond the header's rings: the scratch is full
                }
            }
    }
    if (!count && !head.hits) return 0; // every cell faded (or not reached yet) and no ring: nothing lit, not a drop
    // The kept cells: all of them when they fit the draws, else the newest clocks (the oldest, the dimmest, go first;
    // ties keep the earlier candidate, a newer hit's).
    const unsigned per_region = t.region_draws ? t.region_draws : 1u;
    const unsigned draws_max = out_cap < per_region ? out_cap : per_region;
    const unsigned room = draws_max * renderer::weapon_patch_cells;
    unsigned kept = 0;
    if (count <= room) {
        for (unsigned k = 0; k < count; ++k) sc.order[kept++] = std::uint16_t(k);
    } else {
        std::memcpy(sc.select, sc.since, count * sizeof(float));
        std::nth_element(sc.select, sc.select + (room - 1), sc.select + count);
        const float limit = sc.select[room - 1];
        unsigned below = 0;
        for (unsigned k = 0; k < count; ++k) below += sc.since[k] < limit;
        unsigned ties = room - below;
        for (unsigned k = 0; k < count; ++k) {
            if (sc.since[k] < limit) {
                sc.order[kept++] = std::uint16_t(k);
            } else if (sc.since[k] == limit && ties) {
                --ties;
                sc.order[kept++] = std::uint16_t(k);
            }
        }
        st.dropped += count - kept;
    }
    st.cells = kept;
    const unsigned draws = kept ? (kept + renderer::weapon_patch_cells - 1) / renderer::weapon_patch_cells : 1u;
    if (draws > 1) { // compact chunks: the Morton order of the keys
        for (unsigned k = 0; k < count; ++k) {
            const std::uint32_t kx = std::uint32_t(sc.key[k]) >> 12, ky = std::uint32_t(sc.key[k]) & 0xfffu;
            sc.morton[k] = spread_bits(kx) | (spread_bits(ky) << 1);
        }
        const std::uint32_t* code = sc.morton;
        std::sort(sc.order, sc.order + kept, [code](std::uint16_t a, std::uint16_t b) { return code[a] < code[b]; });
    }
    const float extent = (head.reach + 2.f * 0.5773503f * cell) / surface; // a footprint's angle about its hit
    const float cell_extent = 0.5773503f * cell / surface;                  // a cell's about its centre
    for (unsigned d = 0; d < draws; ++d) {
        renderer::WeaponPatch& out = outs[d];
        if (d) {
            out = head; // the lattice and the look; the ring hits stay with the first draw
            out.hits = 0;
        }
        const unsigned first = d * renderer::weapon_patch_cells;
        const unsigned last = first + renderer::weapon_patch_cells < kept ? first + renderer::weapon_patch_cells : kept;
        out.cells = last - first;
        for (unsigned k = first; k < last; ++k) {
            const unsigned c = sc.order[k];
            out.cell_key[k - first] = float(sc.key[c]); // exact: below 2^24
            out.cell_since[k - first] = sc.since[c];
            out.cell_centre[k - first] = sc.centre[c];
        }
        float mean[3] = {0.f, 0.f, 0.f};
        float angle = 0.f;
        if (draws == 1) {
            bool used[core::owner_hits] = {};
            for (unsigned i = 0; i < n; ++i) used[i] = ring_hit[i];
            for (unsigned k = first; k < last; ++k) used[sc.hit[sc.order[k]]] = true;
            for (unsigned i = 0; i < n; ++i)
                if (used[i])
                    for (unsigned c = 0; c < 3; ++c) mean[c] += dirs[i][c];
            if (!normalize3(mean)) std::memcpy(mean, dirs[0], 12);
            float spread = 0.f;
            for (unsigned i = 0; i < n; ++i) {
                if (!used[i]) continue;
                const float a = acos_approx(dot3(mean, dirs[i]));
                spread = a > spread ? a : spread;
            }
            angle = spread + extent + 1e-3f;
        } else {
            float cdir[renderer::weapon_patch_cells][3];
            for (unsigned k = first; k < last; ++k) {
                const std::int32_t packed = sc.key[sc.order[k]];
                const int kx = int(std::uint32_t(packed) >> 12) - renderer::weapon_cell_key_bias;
                const int ky = int(std::uint32_t(packed) & 0xfffu) - renderer::weapon_cell_key_bias;
                const float q[2] = {0.5f * float(kx) * cell, 0.8660254f * float(ky) * cell}; // the key's centre
                plane_to_sphere(out, q, cdir[k - first]);
                for (unsigned c = 0; c < 3; ++c) mean[c] += cdir[k - first][c];
            }
            if (!d)
                for (unsigned i = 0; i < n; ++i)
                    if (ring_hit[i])
                        for (unsigned c = 0; c < 3; ++c) mean[c] += dirs[i][c];
            if (!normalize3(mean)) std::memcpy(mean, cdir[0], 12);
            for (unsigned k = 0; k < out.cells; ++k) {
                const float a = acos_approx(dot3(mean, cdir[k])) + cell_extent;
                angle = a > angle ? a : angle;
            }
            if (!d)
                for (unsigned i = 0; i < n; ++i)
                    if (ring_hit[i]) {
                        const float a = acos_approx(dot3(mean, dirs[i])) + extent;
                        angle = a > angle ? a : angle;
                    }
            angle += 1e-3f;
        }
        cap_frame(out, mean, angle);
    }
    return draws;
}
inline bool fill_patch(renderer::WeaponPatch& out, const float centre[3], const float axes[9], float soft, unsigned tier,
                       unsigned anchor, const float (*dirs)[3], const float* ages, unsigned n, const float colour[3],
                       const Tuning& t, RegionStats* stats = nullptr) noexcept {
    return fill_region(&out, 1, centre, axes, soft, tier, anchor, dirs, ages, n, colour, t, stats) == 1;
}
// The seams between an owner's anchor regions (weapon_shell_ps.hlsl): each draw's seams name the other regions with a
// draw whose cap overlaps its own, so the pixel program shares the light across the seam instead of adding the two
// misaligned lattices (spill draws of one region carry disjoint cells of one lattice and are never seams).
inline void link_seams(renderer::WeaponPatch* p, unsigned n) noexcept {
    for (unsigned i = 0; i < n; ++i) {
        p[i].seams = 0;
        for (unsigned j = 0; j < n; ++j) {
            if (p[j].seed == p[i].seed) continue;
            const float gap = acos_approx(dot3(p[i].cap_dir, p[j].cap_dir));
            if (gap < p[i].cap_angle + p[j].cap_angle) p[i].seams |= 1u << unsigned(p[j].seed);
        }
    }
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
        unsigned count, live;
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
            s->count = s->live = 0;
            s->status = read_owner(rd, e, &s->data);
        }
        if (s->status != OwnerStatus::ok) {
            n.retired += core::ring_retire_owner(ring, e.owner);
            continue;
        }
        ++n.hits;
        ++s->live;
        // The newest owner_hits by age (insertion; an older hit past them feeds no cell: hits_dropped).
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
    // The owners' regions first (every region gets its first draw before any region spills into a second).
    struct Plan {
        float dirs[core::owner_hits][3], ages[core::owner_hits];
        std::uint8_t anchors[core::owner_hits];
    };
    unsigned regions_left = 0;
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        n.hits_dropped += s.live - s.count;
        if (!(s.data.radius > 0)) continue;
        bool seen_anchor[anchor_count] = {};
        for (unsigned h = 0; h < s.count; ++h) {
            float d[3];
            hit_direction(ring.slots[s.index[h]].pos, d);
            const unsigned a = nearest_anchor(d);
            regions_left += !seen_anchor[a];
            seen_anchor[a] = true;
        }
    }
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        ++n.owners;
        const core::Event& newest = ring.slots[s.index[0]];
        float colour[3];
        patch_colour(newest, t, colour);
        const unsigned tier = newest.patch_tier < core::patch_tier_count ? unsigned(newest.patch_tier) : unsigned(core::patch_capital);
        Plan plan;
        for (unsigned h = 0; h < s.count; ++h) {
            hit_direction(ring.slots[s.index[h]].pos, plan.dirs[h]);
            plan.ages[h] = float(s.age[h]) * 1e-3f;
            plan.anchors[h] = std::uint8_t(nearest_anchor(plan.dirs[h]));
        }
        float centre[3], axes[9];
        const bool shell = s.data.radius > 0 && shell_geometry(s.data, cam, centre, axes) && mean_axis(axes) > 0.f;
        if (shell) {
            const float soft = t.soft * mean_axis(axes);
            const unsigned owner_first = n.patches;
            bool done[core::owner_hits] = {};
            for (unsigned h = 0; h < s.count; ++h) { // one region at a time, newest region first
                if (done[h]) continue;
                float gd[core::owner_hits][3], ga[core::owner_hits];
                unsigned g = 0;
                for (unsigned j = h; j < s.count; ++j)
                    if (!done[j] && plan.anchors[j] == plan.anchors[h]) {
                        done[j] = true;
                        std::memcpy(gd[g], plan.dirs[j], 12);
                        ga[g++] = plan.ages[j];
                    }
                if (regions_left) --regions_left;
                if (n.patches >= patch_cap) {
                    ++n.dropped;
                    continue;
                }
                // This region's draws: its first, and spills only from the budget the regions still to come leave.
                const unsigned free = patch_cap - n.patches;
                const unsigned cap = free > regions_left ? free - regions_left : 1u;
                RegionStats rs{};
                const unsigned drawn = fill_region(patches + n.patches, cap, centre, axes, soft, tier, plan.anchors[h],
                                                   gd, ga, g, colour, t, &rs);
                n.patches += drawn;
                n.spills += drawn > 1 ? drawn - 1 : 0;
                n.cells_dropped += rs.dropped;
                if (rs.refused) ++n.dropped; // a degenerate region; one whose cells have all faded is no drop
            }
            link_seams(patches + owner_first, n.patches - owner_first);
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
            d.age_s = plan.ages[h];
            d.soft = t.soft * d.radius;
            std::memcpy(d.colour, colour, 12);
        }
    }
    return n;
}
} // namespace x3m::weapon_effects::stage
