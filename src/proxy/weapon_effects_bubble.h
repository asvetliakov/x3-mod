#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

// The ship bubble of the shield hit (docs/architecture/weapon-effects.md 5.1.1 "Placement", user decision after Run238
// Flight E): per owner an ellipsoid in its frame from the hull bounds, the hit mapped onto it, and the draw's box
// along a face's axis over the bubble's sheet on that face's side. Windows-free; reads go through the caller's reader
// (engine_memory::read in production); no allocation, no libm, SSE2 scalars only.
//
// Hull bounds (render-node-bounds.md 6.1; the same reads and guards as ship_avoidance_walk_core.h hull_children, with a
// signed box instead of the lateral width): the root node [obj+0x70]'s descendants (c = [n+0x0c]; while c && [c] != 0;
// c = [c]; at most hull_depth levels, hull_budget nodes, hull_list_limit per list), each read as one node_walk_bytes
// block. A part: body id [c+node_body] > 0 and not [c+0x12c] & 0x8000000; the model through the body cache hash
// read-only (H = [[body cache root]+0x14], buckets [H], size [H+4] a power of two, key id + 1, entries {next, key,
// model}, at most hull_chain_steps); the per-axis maximum of the extents [L+0x20..0x28] over every LOD record (n =
// (int16)[model+0x10] in 1..16, L_i = [[model+0x0c] + 4 i], each in [0, 0x20000]); half-extent ((ext x [c+0x70] +
// 0x8000) >> 16) x [c+0x80 + 4 i] / 65536 engine units. Placement: the offset [c+0x30..0x38] in the parent frame x
// parent [+0x80..] / 65536 unless parent [+0x12c] & 0x10000, x parent [+0x70] / 65536 when [c+0x12c] & 0x40; below a
// node whose basis is not the identity the offset direction is unknown (its length widens the box on every axis) and a
// part's extents are bounded by their length on every axis. Any failed guard fails the walk (the fallback sphere) and
// names itself (HullGuard: the weapon_bubble_walk row).
//
// The budgets (FL run 65, 2026-11-10): 128 nodes and 256 per list were sized against fighters; the Argon Titan's walk
// failed on every attempt (123 of 150 walks in the run, all on it) and drew the radius sphere. Its stock scene is 98
// collision bodies in up to 152 nodes (fl/ship-enclosure.md section 3), past 128. hull_budget and hull_list_limit are
// now 1024 (6.7x the Titan; the per-list limit no tighter than the budget, so a capital's flat turret list cannot fail
// before the budget). One 1024-node walk through engine_memory::read costs about 110 us (measured: best 110.0 / mean
// 114.6, the native fixture's HULL_WALK case=capital_cost row; 8,325 reads), so a walk runs in slices of at most
// hull_slice_nodes nodes and hull_slice_reads engine reads (a node with 16 LOD records and a 64-step hash chain takes 84
// reads: the read bound keeps such a slice near the 256 ordinary nodes' cost), one slice per walk budget unit of a
// frame (core::bubble_walks_per_frame), the fallback sphere held until it finishes: a fighter or the Titan finishes in
// its first slice, a 1024-node hull in four frames. A walk parked longer than hull_park_ms or two game steps
// (whichever is larger: SETA scales it) restarts from the root; a walk normally finishes on consecutive frames.
//
// Resuming (HullWalk keeps an explicit stack of hull_depth levels): each level holds its next child c and the address
// whose dword held it (the parent's +0x0c, or the previous sibling's +0); a slice after the first re-reads every
// level's link and fails the walk (HullGuard::changed) when it no longer names c, so a list relinked between frames
// (a part attached or destroyed) is never followed from a stale position. The owner's cache key (root node, handle,
// first child and its body, radius) is checked every frame before a slice (owner_bubble). A changed walk restarts from
// the root on the owner's next stage call (no retry hold); its row says restarted=1.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
constexpr unsigned hull_depth = 3, hull_budget = 1024, hull_list_limit = 1024, hull_chain_steps = 64, hull_lod_max = 16;
constexpr unsigned hull_slice_nodes = 256, hull_slice_reads = 2400, hull_park_ms = 500;
constexpr std::int32_t hull_extent_max = 0x20000, hull_basis_tol = 0x40, hull_scale_max = 256 * 0x10000;
// hull_scale_max: 16.16 fixed point, 256x. A fighter's nodes stay below 16x; a capital's section nodes do not (FL 66 A:
// the Titan's first section node at 0x1b6beb, 27.4x). The int64 products cannot overflow before their clamps:
// hull_part_extent: ext <= hull_extent_max (2^17) x s <= 2^24 is <= 2^41, >> 16 gives base <= 2^25, x si <= 2^24 is
// <= 2^49, / 2^16 <= 2^33 (clamped to INT32_MAX); hull_scale16: |v| < 2^31 x s <= 2^24 is < 2^55.
static_assert(std::int64_t(hull_extent_max) * hull_scale_max <= (std::int64_t(1) << 41) &&
                  ((std::int64_t(hull_extent_max) * hull_scale_max + 0x8000) >> 16) * hull_scale_max <
                      (std::int64_t(1) << 62),
              "hull_part_extent's int64 products stay in range");
static_assert(hull_list_limit >= hull_budget, "a list cannot fail before the walk's budget");
static_assert(hull_slice_nodes > 0 && hull_slice_nodes <= hull_budget, "a slice is part of one walk");
// A node's reads at most: its block, the bucket, hull_chain_steps entries, the model head, the LOD array, its records.
constexpr unsigned hull_node_reads_max = 4 + hull_chain_steps + hull_lod_max;
static_assert(hull_slice_reads >= hull_node_reads_max, "a slice can always take one node");
// The guard a walk failed on (the weapon_bubble_walk row's guard=): cache (the body cache's root, hash or bucket size),
// read (an engine read refused; value: its address), list (a sibling list past hull_list_limit), budget (past
// hull_budget nodes), chain (a hash chain past hull_chain_steps), model (a body id with no model), lod (a LOD count
// outside 1..16, a null array or record), extent (a LOD extent outside [0, hull_extent_max]), scale (a node or axis scale
// outside (0, hull_scale_max], 256x), bounds (a non-finite box), empty (no part), changed (a level's link moved between
// slices), box (bubble_from_box: a degenerate box or radius).
enum class HullGuard : std::uint8_t { none, cache, read, list, budget, chain, model, lod, extent, scale, bounds, empty,
                                      changed, box };
inline const char* hull_guard_name(HullGuard g) noexcept {
    switch (g) {
    case HullGuard::none: return "none";
    case HullGuard::cache: return "cache";
    case HullGuard::read: return "read";
    case HullGuard::list: return "list";
    case HullGuard::budget: return "budget";
    case HullGuard::chain: return "chain";
    case HullGuard::model: return "model";
    case HullGuard::lod: return "lod";
    case HullGuard::extent: return "extent";
    case HullGuard::scale: return "scale";
    case HullGuard::bounds: return "bounds";
    case HullGuard::empty: return "empty";
    case HullGuard::changed: return "changed";
    case HullGuard::box: return "box";
    }
    return "unknown";
}
struct HullBox {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}; // root frame, engine units
    unsigned parts = 0, nodes = 0;
};
// One level of the walk: the parent's fields its children's placement reads (flags, scale, axis scales), the parent's
// position bound in the root frame (centre p, slack u; aligned: no ancestor rotated), the next child c, the address
// whose dword held c, and the list's nodes visited so far.
struct HullFrame {
    std::uint32_t c = 0, link = 0, flags = 0;
    std::int32_t scale = 0, axis[3] = {0, 0, 0};
    float p[3] = {0.f, 0.f, 0.f}, u = 0.f;
    unsigned visited = 0;
    bool aligned = true;
};
enum class HullStep : std::uint8_t { more, done, failed };
struct HullWalk {
    Reader rd = nullptr;
    std::uint32_t buckets = 0, size = 0;
    unsigned budget = hull_budget;
    HullBox box;
    HullFrame stack[hull_depth];
    unsigned top = 0;        // levels in use (the level of the node being visited, 1..hull_depth)
    unsigned deepest = 0;    // the deepest level a node was visited at
    unsigned slices = 0;     // hull_walk_step calls
    unsigned reads = 0;      // engine reads so far
    unsigned fail_depth = 0; // the level the guard failed at (0: before the root's children)
    HullGuard guard = HullGuard::none;
    std::uint32_t value = 0;     // the guard's offending value
    bool extent_refused = false; // the walk failed on the LOD extent bound (FL: the [i] bound's guard)
};
inline bool hull_read(HullWalk& w, std::uint32_t address, void* out, std::size_t size) noexcept {
    ++w.reads;
    return w.rd(address, out, size);
}
inline bool hull_fail(HullWalk& w, HullGuard g, std::uint32_t value, unsigned depth) noexcept {
    w.guard = g;
    w.value = value;
    w.fail_depth = depth;
    w.extent_refused = g == HullGuard::extent;
    return false;
}
inline bool hull_fail(HullWalk& w, HullGuard g, std::uint32_t value) noexcept {
    return hull_fail(w, g, value, w.top);
}
inline HullStep hull_stop(HullWalk& w, HullGuard g, std::uint32_t value, unsigned depth) noexcept {
    hull_fail(w, g, value, depth);
    return HullStep::failed;
}
inline std::uint32_t block_u32(const unsigned char* b, unsigned off) noexcept {
    std::uint32_t v;
    std::memcpy(&v, b + off, 4);
    return v;
}
inline std::int32_t block_s32(const unsigned char* b, unsigned off) noexcept {
    std::int32_t v;
    std::memcpy(&v, b + off, 4);
    return v;
}
inline std::int32_t hull_scale16(std::int32_t v, std::int32_t s) noexcept {
    std::int64_t p = std::int64_t(v) * std::int64_t(s) / 65536;
    if (p > 0x7fffffffLL) p = 0x7fffffffLL;
    if (p < -0x7fffffffLL) p = -0x7fffffffLL;
    return std::int32_t(p);
}
inline bool hull_model(HullWalk& w, std::int32_t id, std::uint32_t* model) noexcept {
    const std::uint32_t key = std::uint32_t(id) + 1u, bucket = w.buckets + 4u * (key & (w.size - 1u));
    std::uint32_t e = 0;
    if (!hull_read(w, bucket, &e, 4)) return hull_fail(w, HullGuard::read, bucket);
    for (unsigned steps = 0; e; ++steps) {
        if (steps == hull_chain_steps) return hull_fail(w, HullGuard::chain, steps);
        std::uint32_t entry[3];
        if (!hull_read(w, e, entry, sizeof entry)) return hull_fail(w, HullGuard::read, e);
        if (entry[1] == key) {
            *model = entry[2];
            return *model != 0 || hull_fail(w, HullGuard::model, std::uint32_t(id));
        }
        e = entry[0];
    }
    return hull_fail(w, HullGuard::model, std::uint32_t(id));
}
// The part's half-extents (engine units, its own axes) from its model's LOD records; false on a failed guard.
inline bool hull_part_extent(HullWalk& w, const unsigned char* b, float e[3]) noexcept {
    std::uint32_t model = 0;
    if (!hull_model(w, block_s32(b, core::layout::node_body), &model)) return false;
    unsigned char head[6]; // the LOD array's pointer, then its int16 count ([model+0x0c], [model+0x10])
    if (!hull_read(w, model + core::layout::model_lods, head, sizeof head))
        return hull_fail(w, HullGuard::read, model + core::layout::model_lods);
    std::uint32_t arr;
    std::int16_t n;
    std::memcpy(&arr, head, 4);
    std::memcpy(&n, head + 4, 2);
    if (n < 1 || n > std::int16_t(hull_lod_max) || !arr) return hull_fail(w, HullGuard::lod, std::uint32_t(std::int32_t(n)));
    std::uint32_t lods[hull_lod_max];
    if (!hull_read(w, arr, lods, 4u * unsigned(n))) return hull_fail(w, HullGuard::read, arr);
    std::int32_t ext[3] = {0, 0, 0};
    for (int k = 0; k < n; ++k) {
        std::int32_t v[3];
        if (!lods[k]) return hull_fail(w, HullGuard::lod, std::uint32_t(k));
        if (!hull_read(w, lods[k] + core::layout::lod_extent, v, sizeof v))
            return hull_fail(w, HullGuard::read, lods[k] + core::layout::lod_extent);
        for (unsigned i = 0; i < 3; ++i) {
            if (v[i] < 0 || v[i] > hull_extent_max) return hull_fail(w, HullGuard::extent, std::uint32_t(v[i]));
            if (v[i] > ext[i]) ext[i] = v[i];
        }
    }
    const std::int32_t s = block_s32(b, core::layout::node_scale);
    if (s <= 0 || s > hull_scale_max) return hull_fail(w, HullGuard::scale, std::uint32_t(s));
    for (unsigned i = 0; i < 3; ++i) {
        const std::int32_t si = block_s32(b, core::layout::node_axis_scale + 4 * i);
        if (si <= 0 || si > hull_scale_max) return hull_fail(w, HullGuard::scale, std::uint32_t(si));
        const std::int64_t base = (std::int64_t(ext[i]) * std::int64_t(s) + 0x8000) >> 16;
        const std::int64_t v = base * std::int64_t(si) / 65536;
        e[i] = float(std::int32_t(v > 0x7fffffffLL ? 0x7fffffffLL : v)); // int32 -> float: cvtsi2ss
    }
    return true;
}
inline bool hull_basis_identity(const unsigned char* b) noexcept {
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) {
            const std::int32_t v = block_s32(b, core::layout::node_basis + 0x10 * r + 4 * c);
            const std::int32_t want = r == c ? 0x10000 : 0;
            if (v - want > hull_basis_tol || want - v > hull_basis_tol) return false;
        }
    return true;
}
// A level for the children of the node at `self` (its block b; p, u: its position bound; aligned: it and no ancestor
// rotated).
inline void hull_push(HullWalk& w, std::uint32_t self, const unsigned char* b, const float p[3], float u,
                      bool aligned) noexcept {
    namespace L = core::layout;
    HullFrame& f = w.stack[w.top++];
    f.c = block_u32(b, L::node_children);
    f.link = self + L::node_children;
    f.flags = block_u32(b, L::node_flags);
    f.scale = block_s32(b, L::node_scale);
    for (unsigned i = 0; i < 3; ++i) {
        f.axis[i] = block_s32(b, L::node_axis_scale + 4 * i);
        f.p[i] = p[i];
    }
    f.u = u;
    f.visited = 0;
    f.aligned = aligned;
}
// A walk of the root node `node`'s hull (body_cache_root: the body cache's global slot, core::layout::body_cache_root
// on AP, FL: the bound Globals::body_cache_slot): the body cache's hash and the root's block; more (then
// hull_walk_step) or failed.
inline HullStep hull_walk_start(HullWalk& w, Reader rd, std::uintptr_t body_cache_root, std::uint32_t node) noexcept {
    w = HullWalk{};
    w.rd = rd;
    if (!rd || !node) {
        hull_fail(w, HullGuard::read, node);
        return HullStep::failed;
    }
    std::uint32_t root = 0, hash = 0, head[2] = {0, 0};
    const std::uint32_t slot = std::uint32_t(body_cache_root);
    if (!rd(body_cache_root, &root, 4)) hull_fail(w, HullGuard::read, slot);
    else if (!root) hull_fail(w, HullGuard::cache, 0);
    else if (!rd(root + core::layout::body_cache_hash, &hash, 4)) hull_fail(w, HullGuard::read, root + core::layout::body_cache_hash);
    else if (!hash) hull_fail(w, HullGuard::cache, 0);
    else if (!rd(hash, head, sizeof head)) hull_fail(w, HullGuard::read, hash);
    else if (!head[0] || !head[1] || head[1] > 0x100000u || (head[1] & (head[1] - 1u))) hull_fail(w, HullGuard::cache, head[1]);
    if (w.guard != HullGuard::none) return HullStep::failed;
    w.buckets = head[0];
    w.size = head[1];
    unsigned char b[core::layout::node_walk_bytes];
    if (!rd(node, b, sizeof b)) {
        hull_fail(w, HullGuard::read, node);
        return HullStep::failed;
    }
    const float origin[3] = {0.f, 0.f, 0.f};
    hull_push(w, node, b, origin, 0.f, true);
    return HullStep::more;
}
// At most `quota` nodes and about `read_quota` engine reads more of the walk (pre-order, every level's list in order;
// no node is begun past read_quota, so a slice reads at most read_quota + hull_node_reads_max): more (the walk
// continues on a later call), done (the box holds every part), failed (w.guard names the guard).
inline HullStep hull_walk_step(HullWalk& w, unsigned quota, unsigned read_quota = ~0u) noexcept {
    namespace L = core::layout;
    if (w.guard != HullGuard::none) return HullStep::failed;
    if (w.slices++) // a resumed walk: every level's pending child is still linked where it was found
        for (unsigned k = 0; k < w.top; ++k) {
            const HullFrame& f = w.stack[k];
            std::uint32_t v = 0;
            if (!f.c) continue;
            if (!hull_read(w, f.link, &v, 4)) return hull_stop(w, HullGuard::read, f.link, k + 1);
            if (v != f.c) return hull_stop(w, HullGuard::changed, f.link, k + 1);
        }
    unsigned visited = 0;
    const unsigned reads0 = w.reads;
    while (w.top) {
        HullFrame& f = w.stack[w.top - 1];
        if (!f.c) {
            --w.top;
            continue;
        }
        unsigned char b[L::node_walk_bytes];
        if (!hull_read(w, f.c, b, sizeof b)) return hull_stop(w, HullGuard::read, f.c, w.top);
        const std::uint32_t next = block_u32(b, 0);
        if (!next) { // the list's sentinel (the parent's +0x10, first dword 0)
            --w.top;
            continue;
        }
        // past the slice's nodes: the next slice reads this node again (a walk ending on the quota finishes here)
        if (visited == quota || w.reads - reads0 >= read_quota) return HullStep::more;
        if (++f.visited > hull_list_limit) return hull_stop(w, HullGuard::list, f.visited, w.top);
        if (!w.budget) return hull_stop(w, HullGuard::budget, w.box.nodes, w.top);
        --w.budget;
        ++w.box.nodes;
        ++visited;
        w.deepest = w.top > w.deepest ? w.top : w.deepest;
        const std::uint32_t flags = block_u32(b, L::node_flags);
        float o[3];
        for (unsigned i = 0; i < 3; ++i) {
            std::int32_t v = block_s32(b, L::node_offset + 4 * i);
            if (!(f.flags & L::node_unscaled_offset)) v = hull_scale16(v, f.axis[i]);
            if (flags & L::node_scaled) v = hull_scale16(v, f.scale);
            o[i] = float(v);
        }
        float p[3] = {f.p[0], f.p[1], f.p[2]}, u = f.u;
        if (f.aligned) {
            for (unsigned i = 0; i < 3; ++i) p[i] += o[i];
        } else { // an ancestor is rotated: the offset may point anywhere
            u += scalar::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        }
        const bool here_aligned = f.aligned && hull_basis_identity(b);
        if (block_s32(b, L::node_body) > 0 && !(flags & L::node_no_collision)) {
            float e[3];
            if (!hull_part_extent(w, b, e)) return HullStep::failed;
            if (!here_aligned) e[0] = e[1] = e[2] = scalar::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            for (unsigned i = 0; i < 3; ++i) {
                const float lo = p[i] - u - e[i], hi = p[i] + u + e[i];
                if (!(lo > -1e30f) || !(hi < 1e30f)) return hull_stop(w, HullGuard::bounds, i, w.top);
                w.box.lo[i] = lo < w.box.lo[i] ? lo : w.box.lo[i];
                w.box.hi[i] = hi > w.box.hi[i] ? hi : w.box.hi[i];
            }
            ++w.box.parts;
        }
        const std::uint32_t self = f.c;
        f.link = self; // the next sibling is the dword at this node's +0
        f.c = next;
        if (w.top < hull_depth) hull_push(w, self, b, p, u, here_aligned);
    }
    if (!w.box.parts) return hull_stop(w, HullGuard::empty, 0, w.top);
    return HullStep::done;
}
// The hull bounds of the root node `node` in one call (the box of every part, root frame, engine units); false on a
// failed guard or no part. *extent_refused (when given): the walk failed on the LOD extent bound; *state (when given):
// the walk, its guard and counts.
inline bool hull_bounds(Reader rd, std::uintptr_t body_cache_root, std::uint32_t node, HullBox* out,
                        bool* extent_refused = nullptr, HullWalk* state = nullptr) noexcept {
    HullWalk local;
    HullWalk& w = state ? *state : local;
    HullStep s = hull_walk_start(w, rd, body_cache_root, node);
    if (s == HullStep::more) s = hull_walk_step(w, ~0u); // the budget bounds it
    if (extent_refused) *extent_refused = w.extent_refused;
    if (s != HullStep::done) return false;
    *out = w.box;
    return true;
}

// The bubble: centre c and half-extents e in the owner frame (view units).
struct Bubble {
    float c[3]{}, e[3]{1.f, 1.f, 1.f};
};
// From the hull bounds: the box's centre, its half-extents x bubble_scale, each at least bubble_floor of the largest
// and at most bubble_scale x the object radius (radius, engine units: obj+0xa4, the engine's own bound of the object;
// a guard against a bogus part, as ship_avoidance's width takes w <= r, at the bubble's own scale so a hull reaching
// the radius keeps its cap past the nose and tail; *clamped when an axis was cut); false for a degenerate box or a
// centre farther than the object radius from the root node (a misfit: the hull lies within the radius of its origin).
inline bool bubble_from_box(const HullBox& h, std::int32_t radius, Bubble* out, bool* clamped) noexcept {
    const float s = core::world_scale, r = float(radius) * s * core::bubble_scale;
    float e[3], m = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        e[i] = 0.5f * (h.hi[i] - h.lo[i]) * s * core::bubble_scale;
        if (!(e[i] >= 0.f) || !(e[i] < 1e20f)) return false;
        m = e[i] > m ? e[i] : m;
    }
    if (!(m > 0.f) || !(r > 0.f)) return false;
    const float R = float(radius) * s;
    float c2 = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        const float c = 0.5f * (h.hi[i] + h.lo[i]) * s;
        c2 += c * c;
    }
    if (!(c2 <= R * R)) return false;
    *clamped = false;
    for (unsigned i = 0; i < 3; ++i) {
        out->c[i] = 0.5f * (h.hi[i] + h.lo[i]) * s;
        float v = e[i] > m * core::bubble_floor ? e[i] : m * core::bubble_floor;
        if (v > r) {
            v = r;
            *clamped = true;
        }
        out->e[i] = v;
    }
    return true;
}
// The fallback: a sphere of the object's radius (engine units) about the root node.
inline Bubble fallback_bubble(std::int32_t radius) noexcept {
    Bubble b;
    const float r = float(radius) * core::world_scale;
    for (unsigned i = 0; i < 3; ++i) {
        b.c[i] = 0.f;
        b.e[i] = r;
    }
    return b;
}
// The root's first child and its body id (render-node-bounds.md 6.1: the derived bounds are keyed on the object, the
// root node and the child ids; a rebuilt scene changes them). 0, 0 for a childless or unreadable root.
inline void root_first_child(Reader rd, std::uint32_t node, std::uint32_t* child, std::int32_t* body) noexcept {
    *child = 0;
    *body = 0;
    std::uint32_t c = 0;
    if (!rd(node + core::layout::node_children, &c, 4) || !c || c == node + core::layout::node_sentinel) return;
    std::int32_t id = 0;
    if (!rd(c + core::layout::node_body, &id, 4)) return;
    *child = c;
    *body = id;
}
// A failed walk for the weapon_bubble_walk row: the owner, its root node and radius, the game ms, the guard and its
// offending value, the nodes and parts counted so far, the level it failed at, the deepest level reached, the walk's
// slices, and whether the walk restarts on the owner's next stage call (guard changed) instead of holding the retry.
struct HullFailure {
    std::uint32_t owner = 0, node = 0, now = 0, value = 0;
    std::int32_t radius = 0;
    unsigned nodes = 0, parts = 0, depth = 0, deepest = 0, slices = 0;
    HullGuard guard = HullGuard::none;
    bool restarted = false;
};
inline HullFailure hull_failure(const HullWalk& w, std::uint32_t owner, std::uint32_t node, std::int32_t radius,
                                std::uint32_t now) noexcept {
    HullFailure f;
    f.owner = owner;
    f.node = node;
    f.now = now;
    f.value = w.value;
    f.radius = radius;
    f.nodes = w.box.nodes;
    f.parts = w.box.parts;
    f.depth = w.fail_depth;
    f.deepest = w.deepest;
    f.slices = w.slices;
    f.guard = w.guard;
    f.restarted = w.guard == HullGuard::changed;
    return f;
}
// The row (one per failed walk the limit admits; edition "fl" or "ap"; now: game ms; suppressed: rows the limit dropped
// since the last row).
inline int format_walk_row(char* out, std::size_t size, const HullFailure& f, const char* edition,
                           unsigned suppressed) noexcept {
    return std::snprintf(out, size,
                         "weapon_bubble_walk edition=%s state=failed guard=%s owner=%08lx node=%08lx radius=%ld nodes=%u "
                         "parts=%u depth=%u deepest=%u slices=%u value=0x%08lx restarted=%u now=%lu budget=%u "
                         "list_limit=%u suppressed=%u",
                         edition, hull_guard_name(f.guard), static_cast<unsigned long>(f.owner),
                         static_cast<unsigned long>(f.node), static_cast<long>(f.radius), f.nodes, f.parts, f.depth,
                         f.deepest, f.slices, static_cast<unsigned long>(f.value), unsigned(f.restarted),
                         static_cast<unsigned long>(f.now), hull_budget, hull_list_limit, suppressed);
}
// The rows' limit on wall-clock ms (the caller's QPC clock, so SETA does not multiply rows): an owner at most once per
// bubble_walk_row_ms (the last bubble_walk_row_owners owners remembered; a changed walk restarts at once, so the cache's
// retry hold does not bound it), and at most bubble_walk_rows rows per bubble_walk_row_ms window in all; the rest
// counted into the next row's suppressed=.
constexpr unsigned bubble_walk_rows = 8, bubble_walk_row_ms = 2000, bubble_walk_row_owners = 16;
struct WalkRowLimit {
    std::uint32_t window_at = 0;
    unsigned rows = 0, suppressed = 0, next = 0;
    bool open = false;
    struct Seen {
        std::uint32_t owner = 0, at = 0;
        bool used = false;
    } seen[bubble_walk_row_owners];
    bool admit(std::uint32_t owner, std::uint32_t now_ms) noexcept {
        for (const Seen& x : seen)
            if (x.used && x.owner == owner && now_ms - x.at < bubble_walk_row_ms) {
                ++suppressed;
                return false;
            }
        if (!open || now_ms - window_at >= bubble_walk_row_ms) {
            open = true;
            window_at = now_ms;
            rows = 0;
        }
        if (rows >= bubble_walk_rows) {
            ++suppressed;
            return false;
        }
        ++rows;
        Seen* slot = nullptr;
        for (Seen& x : seen)
            if (x.used && x.owner == owner) slot = &x;
        if (!slot) {
            slot = &seen[next];
            next = (next + 1) % bubble_walk_row_owners;
        }
        *slot = Seen{owner, now_ms, true};
        return true;
    }
    unsigned take_suppressed() noexcept {
        const unsigned n = suppressed;
        suppressed = 0;
        return n;
    }
};
// One frame's bubble answers for the frame row (weapon_effects_frame): walk slices run (a walk takes one slice per
// frame, at most hull_slice_nodes nodes), of them failed walks, retries held back (a failed walk within
// bubble_retry_ms), walks deferred past the frame's budget (core::bubble_walks_per_frame: the owner takes the fallback
// sphere until its turn), owners whose bubble was cut to the object radius; the slices that left their walk unfinished
// (the fallback sphere meanwhile) and the failures' records (the first bubble_failure_rows of the frame).
constexpr unsigned bubble_failure_rows = core::bubble_walks_per_frame;
struct BubbleStats {
    unsigned walks = 0, failures = 0, held = 0, deferred = 0, clamped = 0;
    unsigned extent_refused = 0; // of the failures, those on the LOD extent bound (one FL row: weapon_bubble_extent)
    unsigned partial = 0;
    unsigned failed_rows = 0;
    HullFailure failed[bubble_failure_rows];
};
// The per-owner cache of the walk: bubble_cache_entries entries keyed by (owner, root node, handle, the root's first
// child and its body id) with the object radius they were walked at; a radius or child change walks again, a walk
// unfinished after its slice continues on the owner's next frame (state walking; the fallback sphere meanwhile), a
// failed walk is retried after core::bubble_retry_ms of game time (the fallback sphere meanwhile), the whole cache clears
// when the ring's scope changes (a load, a sector change, a load epoch: rescope with weapon_effects::scope_changes()).
// A miss takes an empty entry, else the least recently used. Lookup O(entries) per owner (at most core::max_owners
// owners a frame) plus two reads for the child key; a slice only while the frame's budget lasts. About 20 KB (each
// entry holds its walk's stack).
constexpr unsigned bubble_cache_entries = 64;
struct BubbleCache {
    // restart: a walk that failed on `changed`, walked again from the root on the owner's next call (no retry hold)
    enum State : std::uint8_t { empty = 0, walked = 1, failed = 2, walking = 3, restart = 4 };
    struct Entry {
        std::uint32_t owner = 0, node = 0, handle = 0, child = 0, failed_at = 0, used = 0;
        std::uint32_t sliced_at = 0; // game ms of the walk's last slice (walking: its park age)
        std::int32_t radius = 0, body = 0;
        std::uint8_t state = empty;
        bool clamped = false;
        Bubble bubble{};
        HullWalk walk{};
    };
    Entry entries[bubble_cache_entries]{};
    std::uint32_t scope = 0, clock = 0;
    bool scoped = false;
    // The game step (ms) between the two latest distinct stage frames (frame(), once per stage::build: the parked walks'
    // age bound; a paused clock keeps the last step).
    std::uint32_t frame_now = 0, step_ms = 0;
    bool framed = false;
    // Running counters (the tests, diagnostics): lookups, cached answers, walks started, failed walks, retries held back,
    // walks deferred past a frame's budget, clears, slices run, parked walks restarted past their age.
    std::uint32_t lookups = 0, hits = 0, walks = 0, failures = 0, held = 0, deferred = 0, clears = 0, slices = 0,
                  parked = 0;
    void clear() noexcept {
        for (Entry& e : entries) e = Entry{};
        framed = false;
        frame_now = step_ms = 0;
        ++clears;
    }
    void frame(std::uint32_t now) noexcept {
        if (framed && now == frame_now) return;
        if (framed) step_ms = core::age_ms(now, frame_now);
        frame_now = now;
        framed = true;
    }
    // A parked walk's age bound: two game steps or hull_park_ms, whichever is larger.
    std::uint32_t park_limit_ms() const noexcept {
        return 2u * step_ms > hull_park_ms ? 2u * step_ms : hull_park_ms;
    }
    void rescope(std::uint32_t changes) noexcept {
        if (scoped && changes == scope) return;
        clear();
        scope = changes;
        scoped = true;
    }
};
// The owner's bubble: through the cache (nullptr: no cache, every call walks the whole hull), else the fallback sphere
// (*fallback true). *budget: the walk slices the frame may still run (each slice takes one; at 0 an owner needing a
// walk is deferred: the fallback sphere, its entry untouched, so it walks on a later frame). st counts the answer.
inline Bubble owner_bubble(BubbleCache* cache, Reader rd, std::uintptr_t body_cache_root, std::uint32_t owner,
                           std::uint32_t node, std::uint32_t handle, std::int32_t radius, std::uint32_t now,
                           unsigned* budget, BubbleStats* st, bool* fallback) noexcept {
    // A slice's answer: a finished walk's bubble (a degenerate box fails it on HullGuard::box); a failure counted and
    // recorded for the weapon_bubble_walk row.
    auto finish = [&](HullWalk& w, HullStep s, Bubble* b, bool* clamped) {
        if (s == HullStep::done && !bubble_from_box(w.box, radius, b, clamped)) s = hull_stop(w, HullGuard::box, 0, 0);
        st->partial += s == HullStep::more;
        if (s == HullStep::failed) {
            ++st->failures;
            st->extent_refused += w.extent_refused;
            if (st->failed_rows < bubble_failure_rows) st->failed[st->failed_rows++] = hull_failure(w, owner, node, radius, now);
        }
        return s;
    };
    Bubble b;
    bool clamped = false;
    *fallback = true;
    if (!cache) {
        if (!*budget) {
            ++st->deferred;
            return fallback_bubble(radius);
        }
        ++st->walks;
        --*budget;
        HullWalk w;
        HullStep s = hull_walk_start(w, rd, body_cache_root, node);
        if (s == HullStep::more) s = hull_walk_step(w, ~0u); // the budget bounds it
        if (finish(w, s, &b, &clamped) != HullStep::done) return fallback_bubble(radius);
        st->clamped += clamped;
        *fallback = false;
        return b;
    }
    ++cache->lookups;
    std::uint32_t child = 0;
    std::int32_t body = 0;
    root_first_child(rd, node, &child, &body);
    BubbleCache::Entry* e = nullptr;
    for (BubbleCache::Entry& x : cache->entries)
        if (x.state != BubbleCache::empty && x.owner == owner && x.node == node && x.handle == handle) {
            e = &x;
            break;
        }
    const bool same = e && e->radius == radius && e->child == child && e->body == body;
    if (same) {
        e->used = ++cache->clock;
        if (e->state == BubbleCache::walked) {
            ++cache->hits;
            *fallback = false;
            st->clamped += e->clamped;
            return e->bubble;
        }
        if (e->state == BubbleCache::failed && core::age_ms(now, e->failed_at) < core::bubble_retry_ms) {
            ++cache->held;
            ++st->held;
            return fallback_bubble(radius);
        }
    }
    if (!*budget) { // past the frame's slices: the fallback until this owner's turn
        ++cache->deferred;
        ++st->deferred;
        return fallback_bubble(radius);
    }
    if (!e) { // an empty entry, else the least recently used
        e = &cache->entries[0];
        for (BubbleCache::Entry& x : cache->entries) {
            if (x.state == BubbleCache::empty) {
                e = &x;
                break;
            }
            if (x.used < e->used) e = &x;
        }
    }
    HullStep s = HullStep::more;
    if (same && e->state == BubbleCache::walking && core::age_ms(now, e->sliced_at) > cache->park_limit_ms()) {
        ++cache->parked; // parked past its age: from the root again
        e->state = BubbleCache::restart;
    }
    if (!same || e->state != BubbleCache::walking) { // a new walk (a miss, a changed key, a retry, a restart)
        e->owner = owner;
        e->node = node;
        e->handle = handle;
        e->child = child;
        e->body = body;
        e->radius = radius;
        e->used = ++cache->clock;
        e->state = BubbleCache::walking;
        ++cache->walks;
        s = hull_walk_start(e->walk, rd, body_cache_root, node);
    }
    e->walk.rd = rd;
    ++cache->slices;
    ++st->walks;
    --*budget;
    e->sliced_at = now;
    if (s == HullStep::more) s = hull_walk_step(e->walk, hull_slice_nodes, hull_slice_reads);
    s = finish(e->walk, s, &b, &clamped);
    if (s == HullStep::done) {
        e->state = BubbleCache::walked;
        e->bubble = b;
        e->clamped = clamped;
        st->clamped += clamped;
        *fallback = false;
        return b;
    }
    if (s == HullStep::failed) {
        ++cache->failures;
        e->state = e->walk.guard == HullGuard::changed ? BubbleCache::restart : BubbleCache::failed;
        e->failed_at = now;
    }
    return fallback_bubble(radius);
}

// The owner's walked bubble if the cache holds one (no read, no walk, no counter: the death stage asks for a dying owner,
// whose node may already be gone); false otherwise (nullptr cache too).
inline bool bubble_peek(const BubbleCache* cache, std::uint32_t owner, std::uint32_t node, std::uint32_t handle,
                        Bubble* out) noexcept {
    if (!cache) return false;
    for (const BubbleCache::Entry& x : cache->entries)
        if (x.state == BubbleCache::walked && x.owner == owner && x.node == node && x.handle == handle) {
            *out = x.bubble;
            return true;
        }
    return false;
}
// The hit mapping: the recorded hit point p (owner frame, view units) in the bubble's normalised space q = (p - c) / e,
// Q = q / |q|, the bubble point B = c + e Q (a hit at the centre takes Q = +z). The hit lights both faces of every axis
// (the hull decal's rule, review F1); the opposite sheet's cells lie outside their anchor's depth window and
// bubble_box drops them, so a hit near an axis plane lights the same cells whichever side of it it lies.
inline void bubble_point(const Bubble& b, const float p[3], float out_b[3], float out_q[3]) noexcept {
    float q[3], n2 = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        q[i] = (p[i] - b.c[i]) / b.e[i];
        n2 += q[i] * q[i];
    }
    if (!(n2 > 1e-24f) || !(n2 < 1e30f)) {
        q[0] = q[1] = 0.f;
        q[2] = n2 = 1.f;
    }
    const float inv = 1.f / scalar::sqrt(n2);
    for (unsigned i = 0; i < 3; ++i) {
        out_q[i] = q[i] * inv;
        out_b[i] = b.c[i] + b.e[i] * out_q[i];
    }
}
// The cosine of the triplanar cut (core::face_cull_sin): a face takes no share where the bubble's normal is tilted
// further from its axis.
inline float bubble_cut_cos() noexcept {
    return scalar::sqrt(1.f - core::face_cull_sin * core::face_cull_sin);
}
// A face draw's box over the bubble (the draw's cells from fill_region; p.anchor, anchor_u, anchor_v its face frame):
// the plane bounds of its cells clipped to the bubble's projection where the face can take a share, and along the axis
// the depths of the bubble's sheet on the face's side over each cell's rectangle (centre +- a circumradius) within that
// share region (the normal within the cut of the axis implies rho^2 <= rho2_cap in the plane's normalised coordinates),
// each cell's within its anchor's depth -+ the extent (the hull decal's footprint rule: a lit point lies within the
// extent of its anchor hit in the plane and along the axis, so a face nearly edge-on to a hit never carries its cells
// far round the bubble). The pixel program tests its points against this box. False when no cell can show (the draw is
// not needed).
// The bubble in a face's frame (a, u, v: signed owner axes): its centre and half-extents along u, v, a, the share
// region's bound rho2_cap and the depth range of the face's sheet over it ([sheet_lo, ca + ea]); false when degenerate.
struct FaceSheet {
    float cu = 0.f, cv = 0.f, ca = 0.f, eu = 0.f, ev = 0.f, ea = 0.f, rho2_cap = 0.f, sheet_lo = 0.f;
};
inline bool face_sheet(const Bubble& b, const float a[3], const float u[3], const float v[3], FaceSheet* f) noexcept {
    *f = FaceSheet{};
    for (unsigned i = 0; i < 3; ++i) {
        f->cu += b.c[i] * u[i];
        f->cv += b.c[i] * v[i];
        f->ca += b.c[i] * a[i];
        f->eu += b.e[i] * (u[i] < 0.f ? -u[i] : u[i]);
        f->ev += b.e[i] * (v[i] < 0.f ? -v[i] : v[i]);
        f->ea += b.e[i] * (a[i] < 0.f ? -a[i] : a[i]);
    }
    if (!(f->eu > 0.f) || !(f->ev > 0.f) || !(f->ea > 0.f)) return false;
    const float c = bubble_cut_cos(), E = f->eu > f->ev ? f->eu : f->ev;
    f->rho2_cap = (1.f - c * c) * E * E / (c * c * f->ea * f->ea + (1.f - c * c) * E * E);
    f->sheet_lo = f->ca + f->ea * scalar::sqrt(1.f - f->rho2_cap);
    return true;
}
inline bool bubble_box(renderer::WeaponPatch& p, const Bubble& b) noexcept {
    FaceSheet fs;
    if (!face_sheet(b, p.anchor, p.anchor_u, p.anchor_v, &fs)) return false;
    const float cu = fs.cu, cv = fs.cv, ca = fs.ca, eu = fs.eu, ev = fs.ev, ea = fs.ea, rho2_cap = fs.rho2_cap;
    const float circum = 0.5773503f * p.cell;
    float lo = 1e30f, hi = -1e30f;
    auto range = [](float x0, float x1, float* mn, float* mx) {
        const float a = x0 * x0, d = x1 * x1;
        *mn = x0 <= 0.f && x1 >= 0.f ? 0.f : (a < d ? a : d);
        *mx = a > d ? a : d;
    };
    for (unsigned k = 0; k < p.cells; ++k) {
        const std::uint32_t packed = std::uint32_t(std::int32_t(p.cell_key[k])); // exact: below 2^24
        const float qx = 0.5f * float(int(packed >> 12) - renderer::weapon_cell_key_bias) * p.cell;
        const float qy = 0.8660254f * float(int(packed & 0xfffu) - renderer::weapon_cell_key_bias) * p.cell;
        float ux0, ux1, vy0, vy1;
        range((qx - circum - cu) / eu, (qx + circum - cu) / eu, &ux0, &ux1);
        range((qy - circum - cv) / ev, (qy + circum - cv) / ev, &vy0, &vy1);
        const float r_lo = ux0 + vy0;
        if (!(r_lo <= rho2_cap)) continue; // the sheet over this cell is steeper than the cut: nothing shows
        const float r_hi = ux1 + vy1 < rho2_cap ? ux1 + vy1 : rho2_cap;
        float d_hi = ca + ea * scalar::sqrt(1.f - r_lo), d_lo = ca + ea * scalar::sqrt(1.f - r_hi);
        const float w_lo = p.cell_anchor[k] - p.extent, w_hi = p.cell_anchor[k] + p.extent;
        d_lo = d_lo > w_lo ? d_lo : w_lo;
        d_hi = d_hi < w_hi ? d_hi : w_hi;
        if (!(d_hi >= d_lo)) continue; // the sheet over this cell lies outside its anchor's window
        lo = d_lo < lo ? d_lo : lo;
        hi = d_hi > hi ? d_hi : hi;
    }
    if (!(hi >= lo)) return false;
    const float pad = 1e-3f * p.cell + 1e-5f * (ea + (ca < 0.f ? -ca : ca));
    const float ru = eu * scalar::sqrt(rho2_cap), rv = ev * scalar::sqrt(rho2_cap);
    p.box[0] = p.box[0] > cu - ru ? p.box[0] : cu - ru;
    p.box[3] = p.box[3] < cu + ru ? p.box[3] : cu + ru;
    p.box[1] = p.box[1] > cv - rv ? p.box[1] : cv - rv;
    p.box[4] = p.box[4] < cv + rv ? p.box[4] : cv + rv;
    p.box[2] = lo - pad;
    p.box[5] = hi + pad;
    if (!(p.box[3] > p.box[0]) || !(p.box[4] > p.box[1])) return false;
    p.bubble = 1;
    for (unsigned i = 0; i < 3; ++i) {
        p.bubble_c[i] = b.c[i];
        p.bubble_e[i] = b.e[i];
    }
    return true;
}
} // namespace x3m::weapon_effects::stage
