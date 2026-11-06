#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <algorithm>

// The weapon effects' scene-end step (docs/architecture/weapon-effects.md 3.1, 5.1 item 3 and 5.1.1): the ring's
// stage-owned shield hits (flag_stage) become hex patches AT the recorded hit points. Per owner its owner_hits newest
// hits; each hit belongs to the axis region facing its projectile (core::travel_region: the owner axis with the largest
// component of the travel (owner frame, recorded by the hook at the hit), signed against it; the radial core::hit_region
// when the record carries no travel), whose
// plane (perpendicular to that axis, a fixed lattice in the owner's frame) carries the hexagon lattice:
// the hit's cell is the lattice cell containing its projection onto that plane, so the grid is fixed on the owner
// (every hit on a face lands on the same cells, whatever the owner's motion) and adjacent hits on one face share it.
// Each lit cell carries its own clock (the hit that most recently lit it) and its depth along the region axis: the
// depth it was first lit at, held in the DepthCache until the cell has fully faded (a re-light only resets the clock),
// so the cells sit at the hull depth where the hits happened and a re-lit cell never moves (Run235 Flight B: the
// bounding-sphere projection put a flank hit on a long hull on its tail and a station's 417 to 710 m off its hull).
// The cell set of each region is rebuilt from the live hits every frame (fill_region); the depth cache is the stage's
// only state (CPU memory keyed by owner, handle, region and cell, its entries expiring in game time), so a retired
// owner or a Reset leaves nothing that can draw. One draw per owner and region, more when its cells exceed one draw (spills, from the frame's
// budget after every region's first draw; past min(region_draws) draws the oldest clocks are dropped and counted); a
// draw rasterizes only its cells, each as its own flat hexagon (the cost follows the patch, not the owner), both faces;
// the draws of two regions whose cells come near share the light across the seam (link_seams, weapon_shell_ps.hlsl).
// Hits past an owner's owner_hits newest are counted. An owner without a usable frame (radius 0) gets a flat patch
// (decal) per hit at the hit point. Cell size and footprint rings are per PatchTier (the hook's tier of the hit
// object); the colour follows the newest hit's shield fraction (core::shield_colour), with weapon_tint mixing the
// weapon colour in. The owner's rows are read here, at scene end, through the caller's reader (engine_memory::read in
// production) and validated: obj+0x70 == node, node+0x28 == handle and the dying bit obj+0x40 & 0x8000000 clear, else
// every record of that owner leaves the ring (a dead or replaced owner draws nothing). Ages are game time (cfg+0x718):
// SETA shortens the patch exactly as the engine's own effects. World = engine integers x world_scale (0.01); world ->
// view in double (camera_scene_'s rows), so a far camera keeps the patch on its owner. No allocation, no libm; at most
// max_patches + max_decals outputs, O(ring_capacity x owners) with owners <= max_owners.
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
// The six axis regions (the owner's local axes +-z, +-x, +-y: the faces, owner frame; core::hit_region's order) and
// their lattice frames: u the next axis (z -> x, x -> y, y -> z), v = a x u, so (u, v, a) is right-handed. The index
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
// The depth memory of lit cells (coordinator decision 2026-11-06): per (owner, handle, region, cell) the depth the cell
// was first lit at, its own fade (computed once, at the insert) and the game ms its newest clock fades out at; an entry
// holds while now <= until (and until is at most a fade plus a second ahead, so a clock reset by a load never revives
// one). Open addressing over a window of `probe` slots: a lookup takes its key's slot in the window, an insert the first
// empty or expired one; a full window leaves the cell on its newest hit's depth (counted: depth_full). 8192 slots (at
// most half full under the frame's 32 draws x 128 cells), 192 KB, no allocation; the caller owns it (one per process in
// the DLL, cleared on a Reset and after a load).
struct DepthCache {
    static constexpr unsigned slots = 8192, probe = 16;
    struct Entry {
        std::uint32_t owner, handle;
        std::int32_t key; // the packed cell key | region << 24
        float depth;
        std::uint32_t until_ms; // 0: empty
        std::uint32_t fade_ms;  // the cell's own fade
    };
    Entry entry[slots]{};
};
struct DepthContext {
    DepthCache* cache;
    std::uint32_t owner, handle, now_ms;
};
// dropped: a region or decal the frame's budget (patch_cap, decal_cap, max_owners) or a degenerate frame refused (a
// region whose cells have all faded draws nothing and is not a drop); cells_dropped: lit cells no draw carries (a
// region's draws full: the oldest clocks, the dimmest cells, go first); hits_dropped: live hits past an owner's
// owner_hits newest (their cells feed no map); spills: a region's draws past its first (lit cells beyond one map).
struct Counts {
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, hits = 0, owners = 0;
    unsigned cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned depth_full = 0; // drawn lit cells the depth cache had no room for (on their newest hit's depth)
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
// region's hits once, with its newest clock, its depth and the hit that set them, found through an open-addressed table.
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
    float depth[region_candidates];      // along the region axis: the depth of the hit whose clock it carries
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
struct RegionStats {
    unsigned cells = 0;   // lit cells drawn
    unsigned dropped = 0; // lit cells left out (the region's draws full, or the scratch full)
    bool refused = false; // a degenerate frame, lattice or law (no draw; nothing lit is not refused)
    unsigned depth_full = 0; // drawn lit cells the depth cache had no room for
};
// The held depth of a lit cell (since: its newest clock, seconds; kx, ky, seed: its key and region for its own fade,
// cell_fade_s, evaluated only when the cell enters the cache): the cached depth while the cell has not faded out since,
// else `depth` (the newest lit hit's) remembered from now on; *full when the window had no room (the newest depth, not
// remembered).
inline float held_depth(const DepthContext& c, std::int32_t key, int kx, int ky, float seed, const Tuning& t, float depth,
                        float since, float fade_max, bool* full) noexcept {
    DepthCache& m = *c.cache;
    const std::uint32_t lit_ms = c.now_ms - std::uint32_t(std::int32_t(since * 1000.f)); // the newest clock's start
    const std::uint32_t horizon = std::uint32_t(std::int32_t(fade_max * 1000.f)) + 1000u;
    std::uint32_t h = (c.owner * 2654435761u) ^ (c.handle * 2246822519u) ^ (std::uint32_t(key) * 3266489917u);
    h = (h ^ (h >> 15)) & (DepthCache::slots - 1u);
    DepthCache::Entry* free = nullptr;
    for (unsigned i = 0; i < DepthCache::probe; ++i) {
        DepthCache::Entry& e = m.entry[(h + i) & (DepthCache::slots - 1u)];
        const bool live = e.until_ms && c.now_ms <= e.until_ms && e.until_ms - c.now_ms <= horizon;
        if (e.until_ms && e.owner == c.owner && e.handle == c.handle && e.key == key) {
            if (!live) e.depth = depth; // faded out since: the new lighting's depth
            e.until_ms = lit_ms + e.fade_ms + 1u;
            return e.depth;
        }
        if (!free && !live) free = &e;
    }
    if (!free) {
        *full = true;
        return depth;
    }
    const std::uint32_t fade_ms = std::uint32_t(std::int32_t(cell_fade_s(kx, ky, seed, t.fade_s, t.stagger) * 1000.f));
    *free = DepthCache::Entry{c.owner, c.handle, key, depth, lit_ms + fade_ms + 1u, fade_ms};
    return depth;
}
// The draws of one axis region of an owner (its view-space centre and frame, owner_frame) from the region's hits (their
// owner-frame points, view units, ages in game seconds, newest first, at most core::owner_hits): the lattice plane
// perpendicular to the region's axis, its cells and the ring hits. A hit's plane point is its projection (p.u, p.v),
// its depth p.a; it lights its footprint, the cells whose centre lies within rings cells of the hit cell's centre, each
// once the ring front (v = ripple_speed x (rings + 0.5) cells) has reached it; a cell covered by several hits keeps the
// newest clock (the smallest non-negative time since lit) and that hit's level, so a later hit resets a cell to full and
// never adds; its depth is that hit's, or with `depths` the depth it was first lit at while it has not faded out since
// (held_depth); a cell past the slowest fade is dropped. A ring hit (the region's newest four younger than
// the ring's travel, 1 / ripple_speed) also carries the footprint cells its front has not reached yet (not lit, at its
// depth unless a lit clock gives one), so its ring shows over them. The region's cells go into draws of
// weapon_patch_cells, at most min(out_cap, t.region_draws) (every cell in exactly one draw, so the draws add without
// doubling a cell; all of them carry the ring hits); past them the cells with the oldest clocks (the dimmest) are left
// out and counted. Several draws: the cells are chunked in Morton order of their keys (compact chunks). Each draw's
// bound is the bounding sphere of its cells (owner frame), for link_seams. Returns the draws written (0 for a
// degenerate frame or law, stats->refused, or for nothing lit). O(n x footprint) through the table; a spill adds a sort
// of the cells.
inline unsigned fill_region(renderer::WeaponPatch* outs, unsigned out_cap, const float centre[3], const float frame[9],
                            float soft, unsigned tier, unsigned anchor, const float (*points)[3], const float* ages,
                            unsigned n, const float colour[3], const Tuning& t, RegionStats* stats = nullptr,
                            const DepthContext* depths = nullptr) noexcept {
    RegionStats local{};
    RegionStats& st = stats ? *stats : local;
    st = RegionStats{};
    st.refused = true;
    if (!outs || !out_cap || n < 1 || n > core::owner_hits || anchor >= anchor_count) return 0;
    tier = tier < core::patch_tier_count ? tier : unsigned(core::patch_capital);
    const float cell = metres_to_view(t.cell_m[tier]);
    const int rings = int(tier_rings(t, tier));
    if (!(cell > 0.f) || !(t.ripple_speed > 0.f) || !(t.fade_s > 0.f) || !(soft > 0.f)) return 0;
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
    head.soft = soft;
    std::memcpy(head.colour, colour, 12);
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
    for (unsigned i = 0; i < n; ++i) {
        float q[2] = {dot3(points[i], head.anchor_u), dot3(points[i], head.anchor_v)}, g[2], c[2];
        const float depth = dot3(points[i], head.anchor);
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
                    ky <= -renderer::weapon_cell_key_bias || ky >= renderer::weapon_cell_key_bias)
                    continue;
                const float ox = 0.5f * float(dx), oy = 0.8660254f * float(dy);
                const float since = ages[i] - cell * scalar::sqrt(ox * ox + oy * oy) / v;
                if (since > max_since || (since < 0.f && !ring_hit)) continue; // faded out, or not reached and no ring
                const std::int32_t packed =
                    (kx + renderer::weapon_cell_key_bias) * 4096 + (ky + renderer::weapon_cell_key_bias);
                unsigned slot = (std::uint32_t(packed) * 2654435761u) >> 20; // the top 12 bits: candidate_slots
                while (sc.table[slot] && sc.key[sc.table[slot] - 1u] != packed) slot = (slot + 1u) & (candidate_slots - 1u);
                if (sc.table[slot]) {
                    const unsigned at = sc.table[slot] - 1u;
                    if (since >= 0.f && (sc.since[at] < 0.f || since < sc.since[at])) { // a newer lit clock
                        sc.since[at] = since;
                        sc.depth[at] = depth;
                        sc.hit[at] = std::uint8_t(i);
                        sc.centre[at] = std::uint8_t(!dx && !dy);
                    }
                } else if (count < region_candidates) {
                    sc.key[count] = packed;
                    sc.since[count] = since;
                    sc.depth[count] = depth;
                    sc.hit[count] = std::uint8_t(i);
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
    if (depths && depths->cache) // a drawn lit cell keeps the depth it was first lit at until it has faded out
        for (unsigned j = 0; j < kept; ++j) {
            const unsigned k = sc.order[j];
            if (!(sc.since[k] >= 0.f)) continue;
            const int kx = int(std::uint32_t(sc.key[k]) >> 12) - renderer::weapon_cell_key_bias;
            const int ky = int(std::uint32_t(sc.key[k]) & 0xfffu) - renderer::weapon_cell_key_bias;
            bool full = false;
            sc.depth[k] = held_depth(*depths, sc.key[k] | std::int32_t(anchor << 24), kx, ky, float(anchor), t, sc.depth[k],
                                     sc.since[k], max_since, &full);
            st.depth_full += full;
        }
    const unsigned draws = (kept + renderer::weapon_patch_cells - 1) / renderer::weapon_patch_cells;
    if (draws > 1) { // compact chunks: the Morton order of the keys
        for (unsigned k = 0; k < count; ++k) {
            const std::uint32_t kx = std::uint32_t(sc.key[k]) >> 12, ky = std::uint32_t(sc.key[k]) & 0xfffu;
            sc.morton[k] = spread_bits(kx) | (spread_bits(ky) << 1);
        }
        const std::uint32_t* code = sc.morton;
        std::sort(sc.order, sc.order + kept, [code](std::uint16_t a, std::uint16_t b) { return code[a] < code[b]; });
    }
    const float circum = 0.5773503f * cell; // a cell's circumradius
    for (unsigned d = 0; d < draws; ++d) {
        renderer::WeaponPatch& out = outs[d];
        if (d) out = head; // the lattice, the look and the ring hits
        const unsigned first = d * renderer::weapon_patch_cells;
        const unsigned last = first + renderer::weapon_patch_cells < kept ? first + renderer::weapon_patch_cells : kept;
        out.cells = last - first;
        float mean[3] = {0.f, 0.f, 0.f};
        for (unsigned k = first; k < last; ++k) {
            const unsigned c = sc.order[k];
            out.cell_key[k - first] = float(sc.key[c]); // exact: below 2^24
            out.cell_since[k - first] = sc.since[c];
            out.cell_depth[k - first] = sc.depth[c];
            out.cell_centre[k - first] = sc.centre[c];
        }
        // The bound: the cells' centres (owner frame) about their mean, plus a cell's circumradius.
        float pts[renderer::weapon_patch_cells][3];
        for (unsigned k = 0; k < out.cells; ++k) {
            const std::uint32_t packed = std::uint32_t(std::int32_t(out.cell_key[k])); // exact: below 2^24
            const float cx = 0.5f * float(int(packed >> 12) - renderer::weapon_cell_key_bias) * cell;
            const float cy = 0.8660254f * float(int(packed & 0xfffu) - renderer::weapon_cell_key_bias) * cell;
            for (unsigned c = 0; c < 3; ++c) {
                pts[k][c] = out.anchor_u[c] * cx + out.anchor_v[c] * cy + out.anchor[c] * out.cell_depth[k];
                mean[c] += pts[k][c];
            }
        }
        const float inv = out.cells ? 1.f / float(out.cells) : 0.f;
        float r2 = 0.f;
        for (unsigned c = 0; c < 3; ++c) mean[c] *= inv;
        for (unsigned k = 0; k < out.cells; ++k) {
            const float e[3] = {pts[k][0] - mean[0], pts[k][1] - mean[1], pts[k][2] - mean[2]};
            const float m = dot3(e, e);
            r2 = m > r2 ? m : r2;
        }
        std::memcpy(out.bound, mean, 12);
        out.bound[3] = scalar::sqrt(r2) + circum;
    }
    return draws;
}
inline bool fill_patch(renderer::WeaponPatch& out, const float centre[3], const float frame[9], float soft, unsigned tier,
                       unsigned anchor, const float (*points)[3], const float* ages, unsigned n, const float colour[3],
                       const Tuning& t, RegionStats* stats = nullptr) noexcept {
    return fill_region(&out, 1, centre, frame, soft, tier, anchor, points, ages, n, colour, t, stats) == 1;
}
// The seams between an owner's axis regions (weapon_shell_ps.hlsl): each draw's seam neighbours are the draws of its
// owner's other regions whose cells' bounding sphere meets its own (up to four, the deepest overlaps first), so the
// pixel program shares the light by the power distance to the two bounds instead of adding the two lattices; spill
// draws of one region carry disjoint cells of one lattice and are never seams. The share depends only on where the
// cells are, not on which rule (travel or radial) assigned their hits.
inline void link_seams(renderer::WeaponPatch* p, unsigned n) noexcept {
    for (unsigned i = 0; i < n; ++i) {
        p[i].seams = 0;
        p[i].seam_count = 0;
        float depth[4] = {};
        for (unsigned j = 0; j < n; ++j) {
            if (p[j].seed == p[i].seed) continue;
            const float e[3] = {p[i].bound[0] - p[j].bound[0], p[i].bound[1] - p[j].bound[1], p[i].bound[2] - p[j].bound[2]};
            const float reach = p[i].bound[3] + p[j].bound[3], dist = scalar::sqrt(dot3(e, e));
            if (!(dist < reach)) continue;
            p[i].seams |= 1u << unsigned(int(p[j].seed));
            const float overlap = reach - dist;
            unsigned at = p[i].seam_count < 4 ? p[i].seam_count++ : 4u;
            if (at == 4u) { // full: replace the shallowest overlap if this one is deeper
                unsigned low = 0;
                for (unsigned k = 1; k < 4; ++k) low = depth[k] < depth[low] ? k : low;
                if (!(overlap > depth[low])) continue;
                at = low;
            }
            depth[at] = overlap;
            std::memcpy(p[i].seam_bound[at], p[j].bound, 16);
        }
    }
}
// Builds the frame's patches and decals from the ring (see the header). now = cfg+0x718.
// The region of a record: by the projectile's travel (owner frame, recorded at the hit), else radially.
inline unsigned event_region(const core::Event& e) noexcept {
    const int r = core::travel_region(e.dir);
    return r >= 0 ? unsigned(r) : core::hit_region(e.pos);
}
inline Counts build(core::Ring& ring, std::uint32_t now, Reader rd, const Camera& cam, const Tuning& t,
                    renderer::WeaponPatch* patches, unsigned patch_cap, renderer::WeaponDecal* decals,
                    unsigned decal_cap, DepthCache* depth_cache = nullptr) noexcept {
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
        float points[core::owner_hits][3], ages[core::owner_hits];
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
            const unsigned a = event_region(ring.slots[s.index[h]]);
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
            hit_local(ring.slots[s.index[h]].pos, plan.points[h]);
            plan.ages[h] = float(s.age[h]) * 1e-3f;
            plan.anchors[h] = std::uint8_t(event_region(ring.slots[s.index[h]]));
        }
        float centre[3], frame[9];
        if (owner_frame(s.data, cam, centre, frame)) {
            const float soft = t.soft * metres_to_view(t.cell_m[tier]);
            const float owner_radius = float(double(s.data.radius) * double(core::world_scale));
            const DepthContext held{depth_cache, s.owner, newest.handle, now};
            const unsigned owner_first = n.patches;
            bool done[core::owner_hits] = {};
            for (unsigned h = 0; h < s.count; ++h) { // one region at a time, newest region first
                if (done[h]) continue;
                float gd[core::owner_hits][3], ga[core::owner_hits];
                unsigned g = 0;
                for (unsigned j = h; j < s.count; ++j)
                    if (!done[j] && plan.anchors[j] == plan.anchors[h]) {
                        done[j] = true;
                        std::memcpy(gd[g], plan.points[j], 12);
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
                const unsigned drawn = fill_region(patches + n.patches, cap, centre, frame, soft, tier, plan.anchors[h],
                                                   gd, ga, g, colour, t, &rs, depth_cache ? &held : nullptr);
                for (unsigned k = 0; k < drawn; ++k) {
                    patches[n.patches + k].owner = s.owner;
                    patches[n.patches + k].owner_node = ring.slots[s.index[0]].node;
                    patches[n.patches + k].owner_radius = owner_radius;
                }
                n.patches += drawn;
                n.spills += drawn > 1 ? drawn - 1 : 0;
                n.cells_dropped += rs.dropped;
                n.depth_full += rs.depth_full;
                if (rs.refused) ++n.dropped; // a degenerate law; a region whose cells have all faded is no drop
            }
            link_seams(patches + owner_first, n.patches - owner_first);
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
    return n;
}
} // namespace x3m::weapon_effects::stage
