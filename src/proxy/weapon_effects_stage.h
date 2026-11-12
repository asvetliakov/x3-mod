#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_bubble.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <algorithm>

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3 and 5.1.1): the ring's
// stage-owned shield hits (flag_stage) become hex patches. A fighter or corvette (core::shield_bubble of the newest
// record's PatchTier and TierSource) gets its bubble (user decision after Run238 Flight E; weapon_effects_bubble.h): the
// hull bounds' ellipsoid x bubble_scale (the walk through the caller's BubbleCache, else the fallback sphere of the
// object radius, Counts::bubble_fallback), each hit mapped onto it radially in its normalised space (bubble_point) and
// lighting both faces of every axis with the same cells, clocks and draws as below; a face draw's box bounds the
// bubble's sheet over its cells within their anchors' windows (bubble_box: the opposite sheet drops out) and the pixel
// program draws the bubble's near and
// far points (weapon_bubble_ps.hlsl), so the bubble draws no face cull (its far side faces away from the eye). A
// station keeps the hull decal laid on the hull through the lane depth (the screen-space patch, user decision
// 2026-11-06), and so does every capital-tier owner (FL 67 A, 2026-11-12: a box-fitted ellipsoid cannot fit a long hull
// with fins; no hull walk runs for it) except the own ship in the cockpit view, which keeps its eye sphere. Per owner its owner_hits newest hits light cells on the owner's six faces (the
// anchor_frame lattices +-z, +-x, +-y: a planar hex lattice perpendicular to the face's axis, fixed in the owner's
// frame, so every hit on a face lands on the same cells whatever the owner's motion), each hit on both faces of every
// axis (the pixel's normal and the face's box decide what shows; review F1: no side of the owner's origin decides); a
// hit's cell on a face is the cell containing its projection, its footprint the cells within rings cells of it. Each
// lit cell carries its own clock (the hit that most recently lit it) and its anchor's depth along the axis. A draw is
// one face's cells as one box (their hexagons in the plane, their anchors -+ the extent along the axis;
// weapon_shell_vs.hlsl); its pixels read the lane depth, rebuild the scene point in the owner frame and its normal,
// keep a point inside the box whose cell is in the draw's map, and weigh the face by the outward normal's component on
// its axis (triplanar, weapon_shell_ps.hlsl), so a hit shows on whatever hull lies under it and the faces share each
// point (the weights sum to one). A face whose box points away from the eye past the triplanar cut is not drawn
// (faces_culled), and past the frame's screen-area budget the oldest owners' boxes go (area_dropped). The cell sets are rebuilt from the live hits every frame
// (fill_region); the stage keeps no state, so a retired owner or a Reset leaves nothing that can draw. One draw per
// owner and lit face, more when its cells exceed one draw (spills, from the frame's budget after every face's first
// draw; past min(region_draws) draws the oldest clocks are dropped and counted). Hits past an owner's owner_hits newest are counted. An owner without a usable frame (radius 0) gets a flat
// patch (decal) per hit at the hit point. Cell size and footprint rings are per PatchTier (the hook's tier of the hit
// object); the colour follows the newest hit's shield fraction (core::shield_colour), with weapon_tint mixing the
// weapon colour in. The owner's rows are read here, at scene end, through the caller's reader (engine_memory::read in
// production) and validated: obj+0x70 == node, node+0x28 == handle and the dying bit obj+0x40 & 0x8000000 clear, else
// every record of that owner leaves the ring (a dead or replaced owner draws nothing). Ages are game time (cfg+0x718):
// SETA shortens the patch exactly as the engine's own effects. World = engine integers x world_scale (0.01); world ->
// view in double (camera_scene_'s rows), so a far camera keeps the patch on its owner. Event::dir is not used (kept for
// the diagnostics rows). No allocation, no libm; at most max_patches + max_decals outputs, O(ring_capacity x owners)
// with owners <= max_owners.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
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
// The owner's frame in view space: centre = its node position, frame = its local unit axes (the 16.16 rows) taken to
// view space (L_k, view units per owner-frame view unit); false for a radius that is not positive (the decal fallback)
// or degenerate rows.
inline bool owner_frame(const Owner& o, const Camera& c, float centre[3], float frame[9]) noexcept {
    const double s = double(core::world_scale);
    const double w[3] = {double(o.pos[0]) * s, double(o.pos[1]) * s, double(o.pos[2]) * s};
    to_view(c, w, centre);
    if (!(o.radius > 0)) return false;
    for (unsigned k = 0; k < 3; ++k) {
        const double a[3] = {double(o.rows[4 * k]) / 65536.0, double(o.rows[4 * k + 1]) / 65536.0,
                             double(o.rows[4 * k + 2]) / 65536.0};
        if (!(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] > 1e-6)) return false;
        rotate_view(c, a, frame + 3 * k);
    }
    return true;
}
// Whether the owner's sphere (its node position, core::visible_radius_scale x obj+0xa4) reaches the view volume of the
// jittered projection: in front of the near plane, inside the far plane (far_z 0: none) and inside the four side planes
// (clip x' = m00 x + m20 z against w = z, likewise y). The overflow order's "drawn" (core::VisibleOwners); a camera
// without a projection (m00 or m11 not positive) counts every owner as drawn. sphere_in_view: any view-space sphere
// (centre v, radius R) against the same planes.
inline bool sphere_in_view(const float v[3], float R, const Camera& c, float near_z, float far_z) noexcept {
    if (!(c.m00 > 0.f) || !(c.m11 > 0.f)) return true;
    if (!(v[2] + R > near_z)) return false;
    if (far_z > 0.f && !(v[2] - R < far_z)) return false;
    const float m[2] = {c.m00, c.m11}, off[2] = {c.m20, c.m21};
    for (unsigned a = 0; a < 2; ++a) {
        const float lo = m[a] * v[a] + (off[a] + 1.f) * v[2], hi = m[a] * v[a] + (off[a] - 1.f) * v[2];
        if (-lo > R * scalar::sqrt(m[a] * m[a] + (off[a] + 1.f) * (off[a] + 1.f))) return false;
        if (hi > R * scalar::sqrt(m[a] * m[a] + (off[a] - 1.f) * (off[a] - 1.f))) return false;
    }
    return true;
}
inline bool owner_in_view(const Owner& o, const Camera& c, float near_z, float far_z) noexcept {
    if (!(c.m00 > 0.f) || !(c.m11 > 0.f)) return true;
    const double s = double(core::world_scale);
    const double w[3] = {double(o.pos[0]) * s, double(o.pos[1]) * s, double(o.pos[2]) * s};
    float v[3];
    to_view(c, w, v);
    const float R = o.radius > 0 ? float(double(o.radius) * s) * core::visible_radius_scale : 0.f;
    return sphere_in_view(v, R, c, near_z, far_z);
}
// The frame's located owners (core::VisibleOwners::located): a read owner's status, position and radius under its key
// (owner, node, handle). remember_owner files a drawn path's read (read_owner); locate_owner returns the filed entry or
// reads an owner past a stage's cap for the stamp only, through the drawn path's checks (obj+0x70 == node, node+0x28 ==
// handle, not dying) and its position and radius, not its rows (five reads; out->rows stay zero). Either way an owner
// is read at most once per frame for both stages (a full table, never at 1024 records, reads without filing).
inline void remember_owner(core::VisibleOwners* v, const core::Event& e, OwnerStatus st, const Owner& o) noexcept {
    if (!v) return;
    core::VisibleOwners::Located* l = v->find(e.owner, e.node, e.handle);
    if (!l || l->generation == v->generation) return;
    *l = core::VisibleOwners::Located{e.owner, e.node, e.handle, v->generation, {o.pos[0], o.pos[1], o.pos[2]}, o.radius,
                                      std::uint8_t(st)};
    ++v->located_count;
}
inline OwnerStatus locate_owner(Reader rd, const core::Event& e, core::VisibleOwners& v, Owner* out) noexcept {
    core::VisibleOwners::Located* l = v.find(e.owner, e.node, e.handle);
    if (l && l->generation == v.generation) {
        for (unsigned q = 0; q < 3; ++q) out->pos[q] = l->pos[q];
        out->radius = l->radius;
        return OwnerStatus(l->status);
    }
    ++v.located_reads;
    OwnerStatus st = OwnerStatus::ok;
    std::uint32_t node = 0, handle = 0, flags = 0;
    if (!rd(e.owner + core::layout::obj_node, &node, 4) || !rd(e.owner + core::layout::obj_flags, &flags, 4))
        st = OwnerStatus::unreadable;
    else if (node != e.node)
        st = OwnerStatus::mismatch;
    else if (!rd(node + core::layout::node_handle, &handle, 4))
        st = OwnerStatus::unreadable;
    else if (handle != e.handle)
        st = OwnerStatus::mismatch;
    else if (flags & core::layout::obj_dying)
        st = OwnerStatus::dying;
    else if (!rd(node + core::layout::node_pos, out->pos, 12) || !rd(e.owner + core::layout::obj_radius, &out->radius, 4))
        st = OwnerStatus::unreadable;
    remember_owner(&v, e, st, *out);
    return st;
}
// A per-call map from an owner to its candidate (the stages' owner lists): open addressing, 2048 entries for at most
// ring_capacity owners (half full); value 0 is an empty entry, else the candidate's index + 1.
struct OwnerMap {
    static constexpr unsigned bits = 11, size = 1u << bits;
    std::uint32_t key[size];
    std::uint16_t value[size];
    void clear() noexcept {
        for (unsigned k = 0; k < size; ++k) value[k] = 0;
    }
    // The entry of owner o: its own, or the empty one it would take.
    unsigned find(std::uint32_t o) const noexcept {
        unsigned h = unsigned((o * 0x9e3779b1u) >> (32u - bits));
        while (value[h] && key[h] != o) h = (h + 1u) & (size - 1u);
        return h;
    }
};
static_assert(2 * core::ring_capacity <= OwnerMap::size && core::ring_capacity < 0xffffu, "the owner map's entries");
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
// The hit point in the owner's frame, view units (engine integers x world_scale).
inline void hit_local(const std::int32_t pos[3], float out[3]) noexcept {
    for (unsigned k = 0; k < 3; ++k) out[k] = float(double(pos[k]) * double(core::world_scale));
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
// The six faces' lattice frames (the owner's local axes +-z, +-x, +-y, owner frame; core::hit_region's order): u the
// next axis (z -> x, x -> y, y -> z), v = a x u, so (u, v, a) is right-handed. The index is the cell hash's seed.
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
// A cell's own fade (game seconds): weapon_hex_patch.hlsl's fade x (1 + stagger (hash.y - 1/2)), the hash of its key
// and the region seed in float (hex_hash).
inline float cell_fade_s(int kx, int ky, float seed, float fade_s, float stagger) noexcept {
    auto fr = [](float v) { return v - float(scalar::floor(double(v))); };
    float x = fr(float(kx) * 0.1031f), y = fr(float(ky) * 0.1030f), z = fr((float(kx) + float(ky) + seed * 17.f) * 0.0973f);
    const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
    x += d;
    y += d;
    z += d;
    return fade_s * (1.f + stagger * (fr((x + z) * y) - 0.5f));
}
// dropped: a region or decal the frame's budget (patch_cap, decal_cap, max_owners) or a degenerate frame refused (a
// region whose cells have all faded draws nothing and is not a drop); cells_dropped: lit cells no draw carries (a
// region's draws full: the oldest clocks, the dimmest cells, go first); hits_dropped: live hits past an owner's
// owner_hits newest (their cells feed no map); spills: a region's draws past its first (lit cells beyond one map).
struct Counts {
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, hits = 0, owners = 0;
    unsigned cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned faces_culled = 0; // face draws whose box faces away from the eye past the triplanar cut (not drawn)
    unsigned area_dropped = 0; // face draws past the frame's screen-area budget (the oldest newest hit first)
    unsigned bubbles = 0, bubble_fallback = 0; // owners drawn on their bubble; of them on the fallback sphere
    BubbleStats bubble;                        // the frame's walks, failures, held retries, deferred walks, clamps
    float screens = 0.f;                       // the drawn patches' screens
    unsigned eye_spheres = 0;                  // owners drawn on the eye sphere (the own ship in the cockpit)
    unsigned cells_key_dropped = 0; // footprint cells past the packed key's bound (RegionStats::key_dropped; review F6)
};
// The draws one region may take (its cells in draws of weapon_patch_cells; past them the oldest clocks are dropped).
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
    float screen_budget = core::screen_budget;    // screens of boxes per frame
    float near_z = 1.f;                           // the projection's near view z (the box area's clip)
    std::uintptr_t body_cache_root = core::layout::body_cache_root; // the hull walk's body cache slot (the fixture's own)
    // The own ship (its root node; 0 unknown) and the cockpit view: its bubble is the eye sphere (core::eye_sphere_min_m).
    std::uint32_t own_node = 0;
    bool cockpit = false;
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
// A region's cell candidates (fill_region's scratch, on the caller's stack: about 31 KB): every footprint cell of the
// region's hits once, with its newest clock and the hit that set it, found through an open-addressed table.
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
    float since[region_candidates];      // < 0: not lit yet (only a ring hit's front not reached covers it)
    std::uint8_t hit[region_candidates]; // the cell's anchor: of the hits whose footprint contains it, the nearest
    float anchor_d2[region_candidates];  // that hit's squared plane distance from the cell's centre
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
struct RegionStats {
    unsigned cells = 0;   // lit cells drawn
    unsigned dropped = 0; // lit cells left out (the region's draws full, or the scratch full)
    unsigned key_dropped = 0; // footprint cells past the packed key's bound (+-weapon_cell_key_bias cells), never lit
    bool refused = false; // a degenerate frame, lattice or law (no draw; nothing lit is not refused)
};
// The draws of one face of an owner (its view-space centre and frame, owner_frame) from the hits on its side (their
// owner-frame points, view units, ages in game seconds, newest first, at most core::owner_hits): the face's plane
// perpendicular to the axis, its cells and the ring hits. A hit's plane point is its projection (p.u, p.v); it lights
// its footprint, the cells whose centre lies within rings cells of the hit cell's centre, each once the ring front
// (v = ripple_speed x (rings + 0.5) cells) has reached it; a cell covered by several hits keeps the newest clock (the
// smallest non-negative time since lit) and that hit's level, so a later hit resets a cell to full and never adds; its
// anchor (cell_anchor: the depth of the hit that bounds its surface along the axis) is the covering hit
// nearest to its centre in the plane, so a newer hit elsewhere in its footprint does not move the box much; a cell
// past the slowest fade is dropped. A ring hit (the lattice's newest four younger than the ring's travel,
// 1 / ripple_speed) also carries the footprint cells its front has not reached yet (not lit), so its ring shows over
// them. Every draw carries the extent (the footprint's outermost cell centres plus two cell
// circumradii: the pixel program lights the surface only within it of the cell's hit). The cells go into draws of
// weapon_patch_cells, at most min(out_cap, t.region_draws) (every cell in exactly one draw, so the draws add without
// doubling a cell; all of them carry the ring hits); past them the cells with the oldest clocks (the dimmest) are left
// out and counted. Several draws: the cells are chunked in Morton order of their keys (compact chunks); each draw's
// box bounds its cells (the plane) and its anchors -+ the extent (the axis). Returns the
// draws written (0 for a degenerate frame or law, stats->refused, or for nothing lit). O(n x footprint) through the
// table; a spill adds a sort of the cells.
inline unsigned fill_region(renderer::WeaponPatch* outs, unsigned out_cap, const float centre[3], const float frame[9],
                            unsigned tier, unsigned anchor, const float (*points)[3], const float* ages, unsigned n,
                            const float colour[3], const Tuning& t, RegionStats* stats = nullptr) noexcept {
    RegionStats local{};
    RegionStats& st = stats ? *stats : local;
    st = RegionStats{};
    st.refused = true;
    if (!outs || !out_cap || n < 1 || n > core::owner_hits || anchor >= anchor_count) return 0;
    tier = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float cell = metres_to_view(t.cell_m[tier]);
    const int rings = int(tier_rings(t, tier));
    if (!(cell > 0.f) || !(t.ripple_speed > 0.f) || !(t.fade_s > 0.f)) return 0;
    st.refused = false;
    renderer::WeaponPatch& head = outs[0];
    std::memcpy(head.centre, centre, 12);
    anchor_frame(anchor, head.anchor, head.anchor_u, head.anchor_v);
    for (unsigned c = 0; c < 3; ++c) { // the region's axes in view space: sum_k e_k L_k
        head.view_u[c] = head.view_v[c] = head.view_a[c] = 0.f;
        for (unsigned k = 0; k < 3; ++k) {
            head.view_u[c] += head.anchor_u[k] * frame[3 * k + c];
            head.view_v[c] += head.anchor_v[k] * frame[3 * k + c];
            head.view_a[c] += head.anchor[k] * frame[3 * k + c];
        }
    }
    std::memcpy(head.axes, frame, 36);
    std::memcpy(head.colour, colour, 12);
    head.seed = float(anchor);
    head.cell = cell;
    head.radius = (float(rings) + 0.5f) * cell;
    head.reach = float(rings) * cell;
    head.extent = head.reach + 1.1547005f * cell; // the hit within its cell, the far cell's corner: two circumradii
    head.hits = 0;
    head.cells = 0;
    head.bubble = 0; // the hull decal unless bubble_box makes it the ship bubble's draw (outs are reused frame to frame)

    const float v = t.ripple_speed * head.radius, ring_life = 1.f / t.ripple_speed;
    const float max_since = t.fade_s * (1.f + 0.5f * t.stagger);
    RegionScratch sc;
    std::memset(sc.table, 0, sizeof sc.table);
    unsigned count = 0;
    for (unsigned i = 0; i < n; ++i) {
        float q[2] = {dot3(points[i], head.anchor_u), dot3(points[i], head.anchor_v)}, g[2], c[2];
        int key[2];
        g[0] = q[0] / cell;
        g[1] = q[1] / cell;
        hex_key(g, key, c);
        bool ring_hit = false;
        if (head.hits < renderer::weapon_patch_hits && ages[i] < ring_life) {
            const unsigned h = head.hits++;
            head.hit[h][0] = q[0];
            head.hit[h][1] = q[1];
            head.hit_cell[h][0] = c[0] * cell;
            head.hit_cell[h][1] = c[1] * cell;
            head.age_s[h] = ages[i];
            ring_hit = true;
        }
        for (int dy = -rings; dy <= rings; ++dy)
            for (int dx = -2 * rings; dx <= 2 * rings; ++dx) {
                const int ady = dy < 0 ? -dy : dy, adx = dx < 0 ? -dx : dx;
                if (((dx + dy) & 1) || (ady > (adx + ady) / 2 ? ady : (adx + ady) / 2) > rings) continue; // hex distance
                const int kx = key[0] + dx, ky = key[1] + dy;
                if (kx <= -renderer::weapon_cell_key_bias || kx >= renderer::weapon_cell_key_bias ||
                    ky <= -renderer::weapon_cell_key_bias || ky >= renderer::weapon_cell_key_bias) {
                    ++st.key_dropped;
                    continue;
                }
                const float ox = 0.5f * float(dx), oy = 0.8660254f * float(dy);
                const float since = ages[i] - cell * scalar::sqrt(ox * ox + oy * oy) / v;
                if (since > max_since || (since < 0.f && !ring_hit)) continue; // faded out, or not reached and no ring
                const float ex = 0.5f * float(kx) * cell - q[0], ey = 0.8660254f * float(ky) * cell - q[1];
                const float d2 = ex * ex + ey * ey;
                const std::int32_t packed =
                    (kx + renderer::weapon_cell_key_bias) * 4096 + (ky + renderer::weapon_cell_key_bias);
                unsigned slot = (std::uint32_t(packed) * 2654435761u) >> 20; // the top 12 bits: candidate_slots
                while (sc.table[slot] && sc.key[sc.table[slot] - 1u] != packed) slot = (slot + 1u) & (candidate_slots - 1u);
                if (sc.table[slot]) {
                    const unsigned at = sc.table[slot] - 1u;
                    if (since >= 0.f && (sc.since[at] < 0.f || since < sc.since[at])) { // a newer lit clock
                        sc.since[at] = since;
                        sc.centre[at] = std::uint8_t(!dx && !dy);
                    }
                    if (d2 < sc.anchor_d2[at]) { // a nearer anchor (ties keep the newer hit)
                        sc.anchor_d2[at] = d2;
                        sc.hit[at] = std::uint8_t(i);
                    }
                } else if (count < region_candidates) {
                    sc.key[count] = packed;
                    sc.since[count] = since;
                    sc.hit[count] = std::uint8_t(i);
                    sc.anchor_d2[count] = d2;
                    sc.centre[count] = std::uint8_t(!dx && !dy);
                    sc.table[slot] = std::uint16_t(++count);
                } else {
                    ++st.dropped; // a footprint beyond the header's rings: the scratch is full
                }
            }
    }
    if (!count) return 0; // every cell faded: nothing lit, not a drop
    // The kept cells: all of them when they fit the draws, else the newest clocks (the oldest, the dimmest, go first;
    // a ring's cells not reached yet come first; ties keep the earlier candidate, a newer hit's).
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
    const unsigned draws = (kept + renderer::weapon_patch_cells - 1) / renderer::weapon_patch_cells;
    if (draws > 1) { // compact chunks: the Morton order of the keys
        for (unsigned k = 0; k < count; ++k) {
            const std::uint32_t kx = std::uint32_t(sc.key[k]) >> 12, ky = std::uint32_t(sc.key[k]) & 0xfffu;
            sc.morton[k] = spread_bits(kx) | (spread_bits(ky) << 1);
        }
        const std::uint32_t* code = sc.morton;
        std::sort(sc.order, sc.order + kept, [code](std::uint16_t a, std::uint16_t b) { return code[a] < code[b]; });
    }
    for (unsigned d = 0; d < draws; ++d) {
        renderer::WeaponPatch& out = outs[d];
        if (d) out = head; // the lattice, the look and the ring hits
        const unsigned first = d * renderer::weapon_patch_cells;
        const unsigned last = first + renderer::weapon_patch_cells < kept ? first + renderer::weapon_patch_cells : kept;
        out.cells = last - first;
        for (unsigned k = first; k < last; ++k) {
            const unsigned c = sc.order[k];
            out.cell_key[k - first] = float(sc.key[c]); // exact: below 2^24
            out.cell_since[k - first] = sc.since[c];
            out.cell_anchor[k - first] = dot3(points[sc.hit[c]], head.anchor);
            out.cell_centre[k - first] = sc.centre[c];
        }
        // The draw's box in the face's frame: the cells' hexagons in the plane (centres +- a circumradius) and their
        // anchors' depths -+ the extent along the axis.
        const float circum = 0.5773503f * cell;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (unsigned k = 0; k < out.cells; ++k) {
            const std::uint32_t packed = std::uint32_t(std::int32_t(out.cell_key[k])); // exact: below 2^24
            const float q[3] = {0.5f * float(int(packed >> 12) - renderer::weapon_cell_key_bias) * cell,
                                0.8660254f * float(int(packed & 0xfffu) - renderer::weapon_cell_key_bias) * cell,
                                out.cell_anchor[k]};
            const float pad[3] = {circum, circum, out.extent};
            for (unsigned i = 0; i < 3; ++i) {
                lo[i] = q[i] - pad[i] < lo[i] ? q[i] - pad[i] : lo[i];
                hi[i] = q[i] + pad[i] > hi[i] ? q[i] + pad[i] : hi[i];
            }
        }
        for (unsigned i = 0; i < 3; ++i) {
            out.box[i] = lo[i];
            out.box[3 + i] = hi[i];
        }
    }
    return draws;
}
// The face cull (core::face_cull_sin): a draw whose face axis points away from the eye at every corner of its box by
// more than the triplanar floor's cut (a face tilted past it takes no share, and a visible surface faces the eye) can
// light nothing: dot(A, normalize(eye - corner)) < -face_cull_sin at all eight corners (the set where the dot is below
// it is a convex cone about the axis, so the corners decide for the whole box). View space, the eye at the origin.
inline bool box_culled(const float centre[3], const float vu[3], const float vv[3], const float va[3],
                       const float box[6]) noexcept {
    for (unsigned c = 0; c < 8; ++c) {
        const float u = box[c & 1u ? 3 : 0], v = box[c & 2u ? 4 : 1], a = box[c & 4u ? 5 : 2];
        float e[3];
        for (unsigned k = 0; k < 3; ++k) e[k] = -(centre[k] + vu[k] * u + vv[k] * v + va[k] * a);
        const float len = scalar::sqrt(dot3(e, e));
        if (!(len > 1e-6f) || dot3(va, e) >= -core::face_cull_sin * len) return false;
    }
    return true;
}
inline bool face_culled(const renderer::WeaponPatch& p) noexcept {
    return box_culled(p.centre, p.view_u, p.view_v, p.view_a, p.box);
}
inline bool fill_patch(renderer::WeaponPatch& out, const float centre[3], const float frame[9], unsigned tier,
                       unsigned anchor, const float (*points)[3], const float* ages, unsigned n, const float colour[3],
                       const Tuning& t, RegionStats* stats = nullptr) noexcept {
    return fill_region(&out, 1, centre, frame, tier, anchor, points, ages, n, colour, t, stats) == 1;
}
// One owner's bubble draws (5.1.1 "Placement"): its hits (owner frame, view units, newest first, ages in game seconds)
// mapped onto bubble b (bubble_point), each face's draws from every hit (both faces of every axis) through fill_region
// into patches[n->patches..] (at most cap draws in all; *regions_left reserves one draw for every face still to come,
// as the builder's), every draw's box over the bubble's sheet (bubble_box; a draw whose cells cannot show is not
// kept). Adds the draws, spills, dropped cells and refusals to *n. No face cull: the far side faces away from the eye.
inline void fill_bubble(renderer::WeaponPatch* patches, unsigned cap, unsigned* regions_left, const float centre[3],
                        const float frame[9], unsigned tier, const Bubble& b, const float (*hits)[3], const float* ages,
                        unsigned count, const float colour[3], const Tuning& t, Counts* n) noexcept {
    float points[core::owner_hits][3], sub_points[core::owner_hits][3], sub_ages[core::owner_hits];
    count = count < core::owner_hits ? count : core::owner_hits;
    if (!count) return;
    for (unsigned h = 0; h < count; ++h) { // the hit onto the bubble
        float q[3];
        bubble_point(b, hits[h], points[h], q);
    }
    const unsigned tr = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float cell = metres_to_view(t.cell_m[tr]), extent = float(tier_rings(t, tr)) * cell + 1.1547005f * cell;
    for (unsigned face = 0; face < anchor_count; ++face) {
        if (*regions_left) --*regions_left;
        // The cost filter (review F1 keeps every face): a hit whose depth window along the face's axis (B.a -+ the
        // extent, every cell it anchors lies in it) misses the face's sheet within the share region lights nothing there
        // (bubble_box would drop its cells), so it does not feed this face's cells; a face no hit reaches is skipped.
        float a[3], u[3], v[3];
        anchor_frame(face, a, u, v);
        FaceSheet fs;
        if (!face_sheet(b, a, u, v, &fs)) continue;
        unsigned m = 0;
        for (unsigned h = 0; h < count; ++h) {
            const float d = dot3(points[h], a);
            if (d + extent < fs.sheet_lo || d - extent > fs.ca + fs.ea) continue;
            std::memcpy(sub_points[m], points[h], sizeof sub_points[m]);
            sub_ages[m++] = ages[h];
        }
        if (!m) continue;
        if (n->patches >= cap) {
            ++n->dropped;
            continue;
        }
        const unsigned free = cap - n->patches;
        const unsigned room = free > *regions_left ? free - *regions_left : 1u;
        RegionStats rs{};
        unsigned drawn = fill_region(patches + n->patches, room, centre, frame, tier, face, sub_points, sub_ages, m,
                                     colour, t, &rs);
        for (unsigned d = 0; d < drawn;) // a draw whose cells' sheet is steeper than the cut shows nothing
            if (!bubble_box(patches[n->patches + d], b)) {
                patches[n->patches + d] = patches[n->patches + drawn - 1];
                --drawn;
            } else {
                ++d;
            }
        n->patches += drawn;
        n->spills += drawn > 1 ? drawn - 1 : 0;
        n->cells_dropped += rs.dropped;
        n->cells_key_dropped += rs.key_dropped;
        if (rs.refused) ++n->dropped;
    }
}
// The own ship in the cockpit view (core::own_cockpit_gain, 5.1.1): every patch draw whose owner node is own_node (the
// own-ship root node, nonzero) is scaled to `gain` of its radiance (its colour: the patch blends ONE/ONE, so the colour
// is its intensity) when cockpit is true; nothing else changes. Returns the draws dimmed. O(n), no allocation.
inline unsigned dim_own_cockpit(renderer::WeaponPatch* p, unsigned n, std::uint32_t own_node, bool cockpit,
                                float gain = core::own_cockpit_gain) noexcept {
    if (!cockpit || !own_node || !p) return 0;
    unsigned dimmed = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (p[i].owner_node != own_node) continue;
        for (unsigned c = 0; c < 3; ++c) p[i].colour[c] *= gain;
        ++dimmed;
    }
    return dimmed;
}
// The own ship's cockpit view, fresh (review F2): the chase camera's last active-control visit read view mode 1 and
// that visit is this frame's or the previous one's (its visit count moved since; the clock stamps the frame it did),
// else no dim (a menu, a load, a camera no longer visiting).
struct CockpitClock {
    std::uint64_t visits = 0, stamp = 0;
    bool seen = false;
    // visits: the chase camera's visit counter now; frame: the device frame. True when the view is the cockpit.
    bool cockpit(std::uint64_t visits_now, std::uint32_t view_mode, std::uint64_t frame) noexcept {
        if (!seen || visits_now != visits) {
            seen = true;
            visits = visits_now;
            stamp = frame;
        }
        return view_mode == 1 && frame - stamp <= 1;
    }
};
// The frame's screen-area budget (review F4): each draw's box projected (its eight corners' bounding rectangle in
// the jittered projection, clipped to the screen; a corner at or behind the near plane counts the whole screen), in
// screens.
inline float box_screens(const renderer::WeaponPatch& p, const Camera& c, float near_z) noexcept {
    float lo[2] = {1.f, 1.f}, hi[2] = {-1.f, -1.f};
    for (unsigned k = 0; k < 8; ++k) {
        const float u = p.box[k & 1u ? 3 : 0], v = p.box[k & 2u ? 4 : 1], a = p.box[k & 4u ? 5 : 2];
        float e[3];
        for (unsigned j = 0; j < 3; ++j) e[j] = p.centre[j] + p.view_u[j] * u + p.view_v[j] * v + p.view_a[j] * a;
        if (!(e[2] > near_z)) return 1.f;
        const float x = (c.m00 * e[0] + c.m20 * e[2]) / e[2], y = (c.m11 * e[1] + c.m21 * e[2]) / e[2];
        lo[0] = x < lo[0] ? x : lo[0];
        lo[1] = y < lo[1] ? y : lo[1];
        hi[0] = x > hi[0] ? x : hi[0];
        hi[1] = y > hi[1] ? y : hi[1];
    }
    const float w = (hi[0] < 1.f ? hi[0] : 1.f) - (lo[0] > -1.f ? lo[0] : -1.f);
    const float h = (hi[1] < 1.f ? hi[1] : 1.f) - (lo[1] > -1.f ? lo[1] : -1.f);
    return w > 0.f && h > 0.f ? 0.25f * w * h : 0.f;
}
// Builds the frame's patches and decals from the ring (see the header). now = cfg+0x718. bubbles: the hull walk's cache
// (production: one per process, rescoped by the caller; nullptr walks every owner every call). visible (nullable): adds
// every owner it read and kept (max_owners) whose sphere reaches the view volume (owner_in_view) as drawn, and every
// candidate owner in view (kept or not) as in view, for the stamp (core::ring_stamp_hidden); its located table holds
// the owners read. Internal linkage (static), so its ring-sized static scratch is the TU's zero-initialised .bss, not a
// comdat .data$ section of the file (the production DLL calls it from one TU).
static inline Counts build(core::Ring& ring, std::uint32_t now, Reader rd, const Camera& cam, const Tuning& t,
                    renderer::WeaponPatch* patches, unsigned patch_cap, renderer::WeaponDecal* decals,
                    unsigned decal_cap, BubbleCache* bubbles = nullptr, core::VisibleOwners* visible = nullptr) noexcept {
    Counts n{};
    if (bubbles) bubbles->frame(now); // the game step: a parked hull walk's age bound
    struct Seen {
        std::uint32_t owner;
        OwnerStatus status;
        Owner data;
        unsigned count, live;
        std::uint32_t age[core::owner_hits];
        unsigned index[core::owner_hits];
    };
    // The owners, visible first (2026-11-07): every owner of a live stage-owned shield hit, those with a record the
    // previous frame stamped off screen (core::hidden_off_screen) after the others (on screen, or new this frame), then
    // by its newest hit, then by address; the first max_owners are read and drawn, the rest only located for the stamp.
    // One pass over the ring with the owner map, a partial selection sort (keep <= 16). Static scratch, off the game
    // thread's stack (the game thread is the only caller; nothing carries over).
    struct Pick {
        std::uint32_t owner, newest, index; // index: a record of the owner (the reader's validation)
        std::uint8_t off;
    };
    static Pick pick[core::ring_capacity];
    static OwnerMap map;
    map.clear();
    unsigned picks = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        const core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_shield_hit || !(e.flags & core::flag_stage)) continue;
        const std::uint32_t age = core::age_ms(now, e.t0_game_ms);
        if (age > e.lifetime_ms) continue;
        const unsigned h = map.find(e.owner);
        Pick* p;
        if (!map.value[h]) {
            map.key[h] = e.owner;
            map.value[h] = std::uint16_t(picks + 1);
            p = &pick[picks++];
            *p = Pick{e.owner, age, i, 0};
        } else {
            p = &pick[map.value[h] - 1];
        }
        p->off = std::uint8_t(p->off | (ring.hidden[i] & core::hidden_off_screen));
        if (age < p->newest) p->newest = age;
    }
    const unsigned keep = picks < core::max_owners ? picks : core::max_owners;
    for (unsigned a = 0; a < keep; ++a) {
        unsigned best = a;
        for (unsigned b = a + 1; b < picks; ++b) {
            const Pick &x = pick[b], &y = pick[best];
            if (x.off != y.off ? x.off < y.off : x.newest != y.newest ? x.newest < y.newest : x.owner < y.owner) best = b;
        }
        if (best != a) {
            const Pick tmp = pick[a];
            pick[a] = pick[best];
            pick[best] = tmp;
        }
    }
    Seen seen[core::max_owners];
    const unsigned seen_count = keep;
    for (unsigned k = 0; k < keep; ++k) {
        seen[k].owner = pick[k].owner;
        seen[k].count = seen[k].live = 0;
        seen[k].status = read_owner(rd, ring.slots[pick[k].index], &seen[k].data);
        remember_owner(visible, ring.slots[pick[k].index], seen[k].status, seen[k].data);
    }
    if (visible) // the owners past the cap, located for the stamp only (the next frame's order); each read once a frame
        for (unsigned k = keep; k < picks; ++k) {
            Owner o;
            if (locate_owner(rd, ring.slots[pick[k].index], *visible, &o) == OwnerStatus::ok &&
                owner_in_view(o, cam, t.near_z, visible->far_z))
                visible->add_in_view(pick[k].owner);
        }
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_shield_hit || !(e.flags & core::flag_stage)) continue;
        const std::uint32_t age = core::age_ms(now, e.t0_game_ms);
        if (age > e.lifetime_ms) continue; // Present releases it
        Seen* s = nullptr;
        for (unsigned k = 0; k < seen_count && !s; ++k)
            if (seen[k].owner == e.owner) s = &seen[k];
        if (!s) { // its owner past max_owners this frame
            ++n.dropped;
            continue;
        }
        if (s->status != OwnerStatus::ok) {
            n.retired += core::ring_retire_owner(ring, e.owner, true);
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
    // The owners' faces first (every face gets its first draw before any face spills into a second).
    struct Plan {
        float points[core::owner_hits][3], ages[core::owner_hits];
    };
    auto tier_of = [&](const Seen& s) {
        const unsigned v = ring.slots[s.index[0]].patch_tier;
        return v < core::patch_tier_count ? v : unsigned(core::patch_capital);
    };
    unsigned regions_left = 0, walks_left = core::bubble_walks_per_frame;
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        n.hits_dropped += s.live - s.count;
        if (s.data.radius > 0) regions_left += anchor_count;
    }
    for (unsigned k = 0; k < seen_count; ++k) {
        Seen& s = seen[k];
        if (s.status != OwnerStatus::ok || !s.count) continue;
        ++n.owners;
        if (visible && owner_in_view(s.data, cam, t.near_z, visible->far_z)) {
            visible->add_in_view(s.owner);
            visible->add(s.owner);
        }
        const core::Event& newest = ring.slots[s.index[0]];
        float colour[3];
        patch_colour(newest, t, colour);
        const unsigned tier = tier_of(s);
        Plan plan;
        for (unsigned h = 0; h < s.count; ++h) {
            hit_local(ring.slots[s.index[h]].pos, plan.points[h]);
            plan.ages[h] = float(s.age[h]) * 1e-3f;
        }
        float centre[3], frame[9];
        if (owner_frame(s.data, cam, centre, frame)) {
            const float owner_radius = float(double(s.data.radius) * double(core::world_scale));
            // The own ship in the cockpit (Run240 Flight G): the sphere about the eye, of the bubble's smallest
            // half-extent (>= eye_sphere_min_m); the eye in the owner frame through its axes in view. It keeps that path
            // whatever its tier (FL 67 A review F2, main session decision 2026-11-12); an external capital takes the decal.
            const bool eye = t.cockpit && t.own_node && newest.node == t.own_node;
            if (eye || core::shield_bubble(std::uint8_t(tier), newest.tier_source)) { // the ship bubble (not a capital)
                bool fallback = false;
                const unsigned fits = n.bubble.fit_rows;
                Bubble b = owner_bubble(bubbles, rd, t.body_cache_root, s.owner, newest.node, newest.handle,
                                        s.data.radius, now, &walks_left, &n.bubble, &fallback);
                for (unsigned r = fits; r < n.bubble.fit_rows; ++r) n.bubble.fit[r].tier = std::uint8_t(tier);
                ++n.bubbles;
                n.bubble_fallback += fallback;
                if (eye) {
                    float r = b.e[0] < b.e[1] ? b.e[0] : b.e[1];
                    r = r < b.e[2] ? r : b.e[2];
                    const float r_min = metres_to_view(core::eye_sphere_min_m);
                    r = r > r_min ? r : r_min;
                    for (unsigned k = 0; k < 3; ++k) {
                        const float* L = frame + 3 * k;
                        const float l2 = dot3(L, L);
                        b.c[k] = l2 > 1e-12f ? -dot3(centre, L) / l2 : 0.f;
                        b.e[k] = r;
                    }
                    ++n.eye_spheres;
                }
                const unsigned first = n.patches;
                fill_bubble(patches, patch_cap, &regions_left, centre, frame, tier, b, plan.points, plan.ages, s.count,
                            colour, t, &n);
                for (unsigned d = first; d < n.patches; ++d) {
                    patches[d].eye_sphere = eye;
                    patches[d].newest_s = plan.ages[0];
                    patches[d].owner = s.owner;
                    patches[d].owner_node = newest.node;
                    patches[d].owner_radius = owner_radius;
                }
                continue;
            }
            const float cell = metres_to_view(t.cell_m[tier]), reach = float(tier_rings(t, tier)) * cell;
            const float extent = reach + 1.1547005f * cell;
            for (unsigned face = 0; face < anchor_count; ++face) { // every hit on both faces of every axis
                if (regions_left) --regions_left;
                // The cull before the cells (review F4's CPU budget): the hits' box grown by the footprint in the plane
                // and the extent along the axis holds every cell the face could get, so a face it culls is culled.
                {
                    float a[3], u[3], v[3], vu[3] = {}, vv[3] = {}, va[3] = {};
                    anchor_frame(face, a, u, v);
                    for (unsigned c = 0; c < 3; ++c)
                        for (unsigned k = 0; k < 3; ++k) {
                            vu[c] += u[k] * frame[3 * k + c];
                            vv[c] += v[k] * frame[3 * k + c];
                            va[c] += a[k] * frame[3 * k + c];
                        }
                    float box[6] = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
                    for (unsigned h = 0; h < s.count; ++h) {
                        const float q[3] = {dot3(plan.points[h], u), dot3(plan.points[h], v), dot3(plan.points[h], a)};
                        const float pad[3] = {extent, extent, extent};
                        for (unsigned i = 0; i < 3; ++i) {
                            box[i] = q[i] - pad[i] < box[i] ? q[i] - pad[i] : box[i];
                            box[3 + i] = q[i] + pad[i] > box[3 + i] ? q[i] + pad[i] : box[3 + i];
                        }
                    }
                    if (box_culled(centre, vu, vv, va, box)) {
                        ++n.faces_culled;
                        continue;
                    }
                }
                if (n.patches >= patch_cap) {
                    ++n.dropped;
                    continue;
                }
                // This face's draws: its first, and spills only from the budget the faces still to come leave.
                const unsigned free = patch_cap - n.patches;
                const unsigned cap = free > regions_left ? free - regions_left : 1u;
                RegionStats rs{};
                unsigned drawn = fill_region(patches + n.patches, cap, centre, frame, tier, face, plan.points, plan.ages,
                                             s.count, colour, t, &rs);
                for (unsigned d = 0; d < drawn;) // the face cull: a box turned away from the eye is not drawn
                    if (face_culled(patches[n.patches + d])) {
                        ++n.faces_culled;
                        patches[n.patches + d] = patches[n.patches + drawn - 1];
                        --drawn;
                    } else {
                        ++d;
                    }
                for (unsigned d = 0; d < drawn; ++d) {
                    patches[n.patches + d].newest_s = plan.ages[0];
                    patches[n.patches + d].owner = s.owner;
                    patches[n.patches + d].owner_node = ring.slots[s.index[0]].node;
                    patches[n.patches + d].owner_radius = owner_radius;
                }
                n.patches += drawn;
                n.spills += drawn > 1 ? drawn - 1 : 0;
                n.cells_dropped += rs.dropped;
                n.cells_key_dropped += rs.key_dropped;
                if (rs.refused) ++n.dropped; // a degenerate law; a face whose cells have all faded is no drop
            }
            continue;
        }
        // No usable frame: a flat patch per hit at the hit point (the newest hit_slots).
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
            d.soft = t.soft * d.cell; // the two-sided surface fade, soft cells
            std::memcpy(d.colour, colour, 12);
        }
    }
    // The screen-area budget (review F4): past t.screen_budget screens of boxes the draws whose owner's newest hit is
    // the oldest go first (area_dropped); O(patches^2) at most 64.
    float area = 0.f;
    for (unsigned i = 0; i < n.patches; ++i) area += box_screens(patches[i], cam, t.near_z);
    while (area > t.screen_budget && n.patches) {
        unsigned old = 0;
        for (unsigned i = 1; i < n.patches; ++i) old = patches[i].newest_s > patches[old].newest_s ? i : old;
        area -= box_screens(patches[old], cam, t.near_z);
        patches[old] = patches[n.patches - 1];
        --n.patches;
        ++n.area_dropped;
    }
    n.screens = area;
    return n;
}
} // namespace x3m::weapon_effects::stage
