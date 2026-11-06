#pragma once
#include "../renderer/weapon_effects_pass.h"
#include "weapon_effects_core.h"
#include "sse_scalar.h"
#include <cstdint>
#include <cstring>

// The ship bubble of the shield hit (docs/architecture/weapon-effects.md 5.1.1 "Placement", user decision after Run238
// Flight E): per owner an ellipsoid in its frame from the hull bounds, the hit mapped onto it, and the draw's box
// along a face's axis over the bubble's sheet on that face's side. Windows-free; reads go through the caller's reader
// (engine_memory::read in production); no allocation, no libm, SSE2 scalars only.
//
// Hull bounds (render-node-bounds.md 6.1; the same reads and guards as ship_avoidance_walk_core.h hull_children, with a
// signed box instead of the lateral width): the root node [obj+0x70]'s descendants (c = [n+0x0c]; while c && [c] != 0;
// c = [c]; at most hull_depth levels, hull_budget nodes, hull_list_limit per list), each read as one 0x144-byte block.
// A part: body id [c+0x140] > 0 and not [c+0x12c] & 0x8000000; the model through the body cache hash read-only (H =
// [[0x00608518]+0x14], buckets [H], size [H+4] a power of two, key id + 1, entries {next, key, model}, at most
// hull_chain_steps); the per-axis maximum of the extents [L+0x20..0x28] over every LOD record (n = (int16)[model+0x10]
// in 1..16, L_i = [[model+0x0c] + 4 i], each in [0, 0x20000]); half-extent ((ext x [c+0x70] + 0x8000) >> 16) x
// [c+0x80 + 4 i] / 65536 engine units. Placement: the offset [c+0x30..0x38] in the parent frame x parent [+0x80..] /
// 65536 unless parent [+0x12c] & 0x10000, x parent [+0x70] / 65536 when [c+0x12c] & 0x40; below a node whose basis is
// not the identity the offset direction is unknown (its length widens the box on every axis) and a part's extents are
// bounded by their length on every axis. Any failed guard fails the walk (the fallback sphere).
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
using Reader = bool (*)(std::uintptr_t address, void* out, std::size_t size);
constexpr unsigned hull_depth = 3, hull_budget = 128, hull_list_limit = 256, hull_chain_steps = 64, hull_lod_max = 16;
constexpr std::int32_t hull_extent_max = 0x20000, hull_basis_tol = 0x40, hull_scale_max = 16 * 0x10000;
struct HullBox {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}; // root frame, engine units
    unsigned parts = 0, nodes = 0;
};
struct HullWalk {
    Reader rd = nullptr;
    std::uint32_t buckets = 0, size = 0;
    unsigned budget = hull_budget;
    HullBox box;
};
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
    const std::uint32_t key = std::uint32_t(id) + 1u;
    std::uint32_t e = 0;
    if (!w.rd(w.buckets + 4u * (key & (w.size - 1u)), &e, 4)) return false;
    for (unsigned steps = 0; e && steps < hull_chain_steps; ++steps) {
        std::uint32_t entry[3];
        if (!w.rd(e, entry, sizeof entry)) return false;
        if (entry[1] == key) {
            *model = entry[2];
            return *model != 0;
        }
        e = entry[0];
    }
    return false;
}
// The part's half-extents (engine units, its own axes) from its model's LOD records; false on a failed guard.
inline bool hull_part_extent(HullWalk& w, const unsigned char* b, float e[3]) noexcept {
    std::uint32_t model = 0;
    if (!hull_model(w, block_s32(b, core::layout::node_body), &model)) return false;
    unsigned char head[6]; // the LOD array's pointer, then its int16 count ([model+0x0c], [model+0x10])
    if (!w.rd(model + core::layout::model_lods, head, sizeof head)) return false;
    std::uint32_t arr;
    std::int16_t n;
    std::memcpy(&arr, head, 4);
    std::memcpy(&n, head + 4, 2);
    if (n < 1 || n > std::int16_t(hull_lod_max) || !arr) return false;
    std::uint32_t lods[hull_lod_max];
    if (!w.rd(arr, lods, 4u * unsigned(n))) return false;
    std::int32_t ext[3] = {0, 0, 0};
    for (int k = 0; k < n; ++k) {
        std::int32_t v[3];
        if (!lods[k] || !w.rd(lods[k] + core::layout::lod_extent, v, sizeof v)) return false;
        for (unsigned i = 0; i < 3; ++i) {
            if (v[i] < 0 || v[i] > hull_extent_max) return false;
            if (v[i] > ext[i]) ext[i] = v[i];
        }
    }
    const std::int32_t s = block_s32(b, core::layout::node_scale);
    if (s <= 0 || s > hull_scale_max) return false;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int32_t si = block_s32(b, core::layout::node_axis_scale + 4 * i);
        if (si <= 0 || si > hull_scale_max) return false;
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
// One node's children (parent: its block; pp, pu: its position bound in the root frame, centre and slack, engine units;
// aligned: no ancestor rotated).
inline bool hull_walk_children(HullWalk& w, const unsigned char* parent, unsigned depth, const float pp[3], float pu,
                               bool aligned) noexcept {
    namespace L = core::layout;
    const std::uint32_t pflags = block_u32(parent, L::node_flags);
    const std::int32_t pscale = block_s32(parent, L::node_scale);
    unsigned visited = 0;
    for (std::uint32_t c = block_u32(parent, L::node_children); c;) {
        unsigned char b[L::node_walk_bytes];
        if (!w.rd(c, b, sizeof b)) return false;
        const std::uint32_t next = block_u32(b, 0);
        if (!next) break; // the list's sentinel (the parent's +0x10, first dword 0)
        if (++visited > hull_list_limit || !w.budget) return false;
        --w.budget;
        ++w.box.nodes;
        const std::uint32_t flags = block_u32(b, L::node_flags);
        float o[3];
        for (unsigned i = 0; i < 3; ++i) {
            std::int32_t v = block_s32(b, L::node_offset + 4 * i);
            if (!(pflags & L::node_unscaled_offset)) v = hull_scale16(v, block_s32(parent, L::node_axis_scale + 4 * i));
            if (flags & L::node_scaled) v = hull_scale16(v, pscale);
            o[i] = float(v);
        }
        float p[3] = {pp[0], pp[1], pp[2]}, u = pu;
        if (aligned) {
            for (unsigned i = 0; i < 3; ++i) p[i] += o[i];
        } else { // an ancestor is rotated: the offset may point anywhere
            u += scalar::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        }
        const bool here_aligned = aligned && hull_basis_identity(b);
        if (block_s32(b, L::node_body) > 0 && !(flags & L::node_no_collision)) {
            float e[3];
            if (!hull_part_extent(w, b, e)) return false;
            if (!here_aligned) e[0] = e[1] = e[2] = scalar::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            for (unsigned i = 0; i < 3; ++i) {
                const float lo = p[i] - u - e[i], hi = p[i] + u + e[i];
                if (!(lo > -1e30f) || !(hi < 1e30f)) return false;
                w.box.lo[i] = lo < w.box.lo[i] ? lo : w.box.lo[i];
                w.box.hi[i] = hi > w.box.hi[i] ? hi : w.box.hi[i];
            }
            ++w.box.parts;
        }
        if (depth < hull_depth && !hull_walk_children(w, b, depth + 1, p, u, here_aligned)) return false;
        c = next;
    }
    return true;
}
// The hull bounds of the root node `node` (the box of every part, root frame, engine units); false on a failed guard or
// no part. body_cache_root: the body cache's global slot (core::layout::body_cache_root in production).
inline bool hull_bounds(Reader rd, std::uintptr_t body_cache_root, std::uint32_t node, HullBox* out) noexcept {
    HullWalk w;
    w.rd = rd;
    std::uint32_t root = 0, hash = 0, head[2] = {0, 0};
    if (!rd || !node || !rd(body_cache_root, &root, 4) || !root || !rd(root + core::layout::body_cache_hash, &hash, 4) ||
        !hash || !rd(hash, head, sizeof head))
        return false;
    w.buckets = head[0];
    w.size = head[1];
    if (!w.buckets || !w.size || w.size > 0x100000u || (w.size & (w.size - 1u))) return false;
    unsigned char b[core::layout::node_walk_bytes];
    if (!rd(node, b, sizeof b)) return false;
    const float origin[3] = {0.f, 0.f, 0.f};
    if (!hull_walk_children(w, b, 1, origin, 0.f, true) || !w.box.parts) return false;
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
// the radius keeps its cap past the nose and tail; *clamped when an axis was cut); false for a degenerate box.
inline bool bubble_from_box(const HullBox& h, std::int32_t radius, Bubble* out, bool* clamped) noexcept {
    const float s = core::world_scale, r = float(radius) * s * core::bubble_scale;
    float e[3], m = 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        e[i] = 0.5f * (h.hi[i] - h.lo[i]) * s * core::bubble_scale;
        if (!(e[i] >= 0.f) || !(e[i] < 1e20f)) return false;
        m = e[i] > m ? e[i] : m;
    }
    if (!(m > 0.f) || !(r > 0.f)) return false;
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
// One frame's bubble answers for the frame row (weapon_effects_frame): walks run, of them failed, retries held back
// (a failed walk within bubble_retry_ms), walks deferred past the frame's budget (core::bubble_walks_per_frame: the
// owner takes the fallback sphere until its turn), owners whose bubble was cut to the object radius.
struct BubbleStats {
    unsigned walks = 0, failures = 0, held = 0, deferred = 0, clamped = 0;
};
// The per-owner cache of the walk: bubble_cache_entries entries keyed by (owner, root node, handle, the root's first
// child and its body id) with the object radius they were walked at; a radius or child change walks again, a failed
// walk is retried after core::bubble_retry_ms of game time (the fallback sphere meanwhile), the whole cache clears
// when the ring's scope changes (a load, a sector change, a load epoch: rescope with weapon_effects::scope_changes()).
// A miss takes an empty entry, else the least recently used. Lookup O(entries) per owner (at most core::max_owners
// owners a frame) plus two reads for the child key; a walk only while the frame's budget lasts.
constexpr unsigned bubble_cache_entries = 64;
struct BubbleCache {
    enum State : std::uint8_t { empty = 0, walked = 1, failed = 2 };
    struct Entry {
        std::uint32_t owner = 0, node = 0, handle = 0, child = 0, failed_at = 0, used = 0;
        std::int32_t radius = 0, body = 0;
        std::uint8_t state = empty;
        bool clamped = false;
        Bubble bubble{};
    };
    Entry entries[bubble_cache_entries]{};
    std::uint32_t scope = 0, clock = 0;
    bool scoped = false;
    // Running counters (the tests, diagnostics): lookups, cached answers, walks, failed walks, retries held back, walks
    // deferred past a frame's budget.
    std::uint32_t lookups = 0, hits = 0, walks = 0, failures = 0, held = 0, deferred = 0, clears = 0;
    void clear() noexcept {
        for (Entry& e : entries) e = Entry{};
        ++clears;
    }
    void rescope(std::uint32_t changes) noexcept {
        if (scoped && changes == scope) return;
        clear();
        scope = changes;
        scoped = true;
    }
};
// The owner's bubble: through the cache (nullptr: no cache, every call walks), else the fallback sphere (*fallback
// true). *budget: the walks the frame may still run (each walk takes one; at 0 an owner needing a walk is deferred:
// the fallback sphere, its entry untouched, so it walks on a later frame). st counts the answer.
inline Bubble owner_bubble(BubbleCache* cache, Reader rd, std::uintptr_t body_cache_root, std::uint32_t owner,
                           std::uint32_t node, std::uint32_t handle, std::int32_t radius, std::uint32_t now,
                           unsigned* budget, BubbleStats* st, bool* fallback) noexcept {
    bool clamped = false;
    auto walk = [&](Bubble* b) {
        HullBox h;
        ++st->walks;
        --*budget;
        if (hull_bounds(rd, body_cache_root, node, &h) && bubble_from_box(h, radius, b, &clamped)) return true;
        ++st->failures;
        return false;
    };
    Bubble b;
    *fallback = true;
    if (!cache) {
        if (!*budget) {
            ++st->deferred;
            return fallback_bubble(radius);
        }
        *fallback = !walk(&b);
        st->clamped += !*fallback && clamped;
        return *fallback ? fallback_bubble(radius) : b;
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
    if (e && e->radius == radius && e->child == child && e->body == body) {
        e->used = ++cache->clock;
        if (e->state == BubbleCache::walked) {
            ++cache->hits;
            *fallback = false;
            st->clamped += e->clamped;
            return e->bubble;
        }
        if (core::age_ms(now, e->failed_at) < core::bubble_retry_ms) {
            ++cache->held;
            ++st->held;
            return fallback_bubble(radius);
        }
    }
    if (!*budget) { // past the frame's walks: the fallback until this owner's turn
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
    e->owner = owner;
    e->node = node;
    e->handle = handle;
    e->child = child;
    e->body = body;
    e->radius = radius;
    e->used = ++cache->clock;
    ++cache->walks;
    if (walk(&b)) {
        e->state = BubbleCache::walked;
        e->bubble = b;
        e->clamped = clamped;
        st->clamped += clamped;
        *fallback = false;
        return b;
    }
    ++cache->failures;
    e->state = BubbleCache::failed;
    e->failed_at = now;
    return fallback_bubble(radius);
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
