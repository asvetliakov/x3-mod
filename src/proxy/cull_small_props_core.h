#pragma once
// Render-only cull of small prop nodes (X3M_CULL_SMALL_PROPS=on|off, default
// off; docs/verification/cull-small-parts.md, "Small props"). The decision of
// one main-scene draw: its scope node's body path (the engine's body table,
// cull_census_core.h) starts with `ships\props\` (turret bases and sockets,
// weapon dummies), the draw's own vertex extent (the object-space AABB of the
// drawn POSITION0 range, the shadow-replay extent cache the object_bounds rows
// print) projected through the draw's own clip rows spans less than
// X3M_CULL_SMALL_PARTS_PX pixels of radius, and the node is not the player's
// ship or its current target or a descendant of either (object_capture.h:
// own_ship and target through the cockpit registry 0x608504). Such a draw is
// not forwarded (the hook returns D3D_OK, as for a drawn call); nothing of the
// engine's state is read back or written, so the cull/LOD pass, the renderable
// bit and every simulation consumer see the frame as vanilla.
//
// Pure: no D3D, no Windows; the engine reads go through the caller's `read`
// (engine_memory::read in production, a synthetic image in the fixture and on
// the host). Integer and float scalar arithmetic only, no allocation.
#include "cull_census_core.h"
#include "object_capture.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace x3m::cull_small_props::core {
constexpr std::uintptr_t cockpit_registry_root = 0x608504; // object_capture::own_ship / target
constexpr unsigned model_offset = 0x140, parent_offset = 0x18, handle_offset = 0x28;
// Mesh part: centre +0x40/+0x44/+0x48, (+0x4c unused), half-extent +0x50/+0x54/+0x58, both x4 int16 units; / 65536
// gives POSITION0 units (render-node-bounds.md 2, "Units line up exactly with the submitted world matrix"). Diagnostics
// only since run376: projected, this box measured every prop at or above 4 px where the drawn vertex range spans
// 0.85-2.1 px (docs/verification/cull-small-parts.md, "Run 105 A"), so the decision takes the vertex extent instead.
constexpr unsigned part_bounds_offset = 0x40, part_bounds_words = 7;
constexpr float part_bounds_scale = 1.f / 65536.f;
constexpr char prop_prefix[] = "ships\\props\\";
constexpr unsigned prop_prefix_length = sizeof prop_prefix - 1; // 12
constexpr unsigned window_frames = 300;
constexpr unsigned class_slots = 1024, memo_slots = 1024, walk_slots = 256, walk_depth = 16;
constexpr unsigned walks_per_frame = 64, walk_frames = 256;
constexpr float w_epsilon = 1e-6f;
static_assert((memo_slots & (memo_slots - 1)) == 0 && (walk_slots & (walk_slots - 1)) == 0, "power-of-two tables");
// Multiplicative (Fibonacci) hash of a node address into a power-of-two table: engine nodes are allocated at a fixed
// stride, which a plain (node >> 4) % N folds onto a few slots.
inline std::uint32_t slot_of(std::uint32_t node, std::uint32_t slots) {
    return (node * 2654435761u) >> 16 & (slots - 1);
}

// `on` enables, `off` or unset/empty leaves it off, anything else is refused.
inline bool parse_mode(const char* text, bool* on) {
    if (!text || !*text || !std::strcmp(text, "off")) {
        *on = false;
        return true;
    }
    if (!std::strcmp(text, "on")) {
        *on = true;
        return true;
    }
    return false;
}
// Case-insensitive `ships\props\` prefix; '/' matches '\'.
inline bool is_prop_name(const char* name) {
    if (!name) return false;
    for (unsigned i = 0; i < prop_prefix_length; ++i) {
        char c = name[i];
        if (!c) return false;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c == '/') c = '\\';
        if (c != prop_prefix[i]) return false;
    }
    return true;
}

struct Box {
    float lo[3], hi[3];
};
// raw = the seven words at part+0x40: centre [0..2], unused [3], half-extent [4..6].
inline bool part_box(const std::int32_t raw[part_bounds_words], Box& out) {
    for (unsigned i = 0; i < 3; ++i) {
        const std::int32_t h = raw[4 + i];
        if (h < 0) return false;
        out.lo[i] = float(raw[i] - h) * part_bounds_scale;
        out.hi[i] = float(raw[i] + h) * part_bounds_scale;
    }
    return true;
}
// Half the larger side of the unclipped screen box of the eight corners in pixels (the
// census's projected-radius unit). false when a corner is at or behind the eye plane or
// not finite: no bounded size, the draw is kept.
inline bool screen_radius(const float rows[16], const Box& b, unsigned width, unsigned height, float* radius) {
    if (!rows || !width || !height) return false;
    float x_lo = 3.4e38f, x_hi = -3.4e38f, y_lo = 3.4e38f, y_hi = -3.4e38f;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? b.hi[0] : b.lo[0], y = (corner & 2) ? b.hi[1] : b.lo[1],
                    z = (corner & 4) ? b.hi[2] : b.lo[2];
        const float cx = rows[0] * x + rows[1] * y + rows[2] * z + rows[3];
        const float cy = rows[4] * x + rows[5] * y + rows[6] * z + rows[7];
        const float cw = rows[12] * x + rows[13] * y + rows[14] * z + rows[15];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) || cw <= w_epsilon) return false;
        const float sx = cx / cw * .5f * float(width), sy = cy / cw * .5f * float(height);
        if (!std::isfinite(sx) || !std::isfinite(sy)) return false;
        if (sx < x_lo) x_lo = sx;
        if (sx > x_hi) x_hi = sx;
        if (sy < y_lo) y_lo = sy;
        if (sy > y_hi) y_hi = sy;
    }
    const float span = x_hi - x_lo > y_hi - y_lo ? x_hi - x_lo : y_hi - y_lo;
    *radius = .5f * span;
    return true;
}

enum class Verdict : std::uint8_t {
    no_scope,      // no scope node or descriptor, or the node unreadable: drawn, not a prop draw
    not_prop,      // body path outside ships\props\ (or unknown): drawn
    no_bounds,     // prop, the draw's vertex extent not known (yet): drawn
    unbounded,     // prop, no clip rows or a corner at/behind the eye plane: drawn
    kept_size,     // prop at or above the threshold: drawn
    exempt_own,    // prop below it on the player's ship: drawn
    exempt_target, // prop below it on the current target: drawn
    unresolved,    // prop below it, the frame's own ship / target unknown: drawn (fail closed)
    deferred,      // prop below it, over this frame's walk budget: drawn
    culled,        // skipped
    count
};
inline const char* verdict_name(Verdict v) {
    static const char* const names[unsigned(Verdict::count)] = {"no_scope",   "not_prop",   "no_bounds",
                                                                "unbounded",  "kept_size",  "exempt_own",
                                                                "exempt_target", "unresolved", "deferred", "culled"};
    return unsigned(v) < unsigned(Verdict::count) ? names[unsigned(v)] : "?";
}
inline bool prop_verdict(Verdict v) {
    return v != Verdict::no_scope && v != Verdict::not_prop && v < Verdict::count;
}

// Production and fixture engine addresses.
struct Addresses {
    std::uintptr_t body_global = x3m::cull_census::core::body_global_va;
    std::uintptr_t cockpit_slot = cockpit_registry_root;
};

// Per-device state: the model-id class cache (flushed every window), the per-frame node
// memo (every draw of a node in a frame shares the first draw's verdict: a node is
// skipped whole or not at all), the frame's own-ship/target roots (resolved once, at the
// first prop below the threshold) and the (node, handle) -> ancestry cache (flushed when
// either root changes; at most walks_per_frame walks per frame, the rest drawn).
struct Culler {
    enum Class : std::uint8_t { unknown = 0, prop = 1, other = 2 };
    struct ClassSlot {
        std::uint32_t model;
        std::uint8_t cls;
    };
    struct MemoSlot {
        std::uint32_t node, frame;
        Verdict verdict;
    };
    struct WalkSlot {
        std::uint32_t node, handle, stamp;
        std::uint8_t verdict; // 0 unused, 1 neither, 2 own, 3 target
    };
    struct Window {
        unsigned frames, draws, culled, kept, kept_size, exempt_own, exempt_target, unresolved, deferred, no_bounds,
            unbounded, no_scope, nodes_culled, resolves, walks;
    };
    float px = 0.f;
    std::uint32_t frame = 0;
    bool frame_started = false;
    ClassSlot classes[class_slots]{};
    MemoSlot memo[memo_slots]{};
    WalkSlot walks[walk_slots]{};
    bool roots_known = false, roots_ok = false;
    std::uint32_t own = 0, own_handle = 0, target = 0, target_handle = 0;
    std::uint32_t walk_own = 0, walk_own_handle = 0, walk_target = 0, walk_target_handle = 0; // the walk cache's roots
    unsigned walks_this_frame = 0;
    Window window{};

    // A new frame: the memo is stamped (no clear), the roots re-resolve on demand, the walk budget resets.
    void begin_frame(std::uint32_t f) {
        frame = f;
        frame_started = true;
        roots_known = roots_ok = false;
        walks_this_frame = 0;
        ++window.frames;
    }
    void flush_classes() { std::memset(classes, 0, sizeof classes); }
    void reset_window() { window = Window{}; }

    template <class Read> Class resolve_class(Read& read, const Addresses& a, std::uint32_t model) {
        using namespace x3m::cull_census::core;
        ++window.resolves;
        std::uint32_t g = 0;
        std::int32_t head[3]{};
        if (!read(a.body_global, &g, 4) || !g || !read(std::uintptr_t(g) + body_fixed_count_offset, head, sizeof head))
            return other;
        if (head[0] != body_fixed_count || head[1] < 0 || head[1] >= body_dynamic_limit || !head[2]) return other;
        std::uint32_t index = 0, p = 0;
        if (!body_slot(std::int32_t(model), head[0], head[1], &index)) return other;
        const std::uint64_t entry =
            std::uint64_t(std::uint32_t(head[2])) + std::uint64_t(index) * body_slot_stride + body_slot_name_offset;
        if (entry > 0xfffffffcu || !read(std::uintptr_t(entry), &p, 4) || !p) return other; // null: "v\%05d"
        char name[prop_prefix_length + 1]{};
        unsigned have = 0;
        while (have < prop_prefix_length) { // page-bounded: a short name before an unreadable page still reads
            const std::uint64_t at = std::uint64_t(p) + have;
            if (at > 0xffffffffu) return other;
            unsigned chunk = unsigned(0x1000 - (at & 0xfff));
            if (chunk > prop_prefix_length - have) chunk = prop_prefix_length - have;
            if (!read(std::uintptr_t(at), name + have, chunk)) return other;
            for (unsigned i = have; i < have + chunk; ++i)
                if (!name[i]) return other; // shorter than the prefix
            have += chunk;
        }
        return is_prop_name(name) ? prop : other;
    }
    template <class Read> bool is_prop(Read& read, const Addresses& a, std::uint32_t model) {
        ClassSlot& s = classes[model % class_slots];
        if (s.cls == unknown || s.model != model) {
            s.model = model;
            s.cls = std::uint8_t(resolve_class(read, a, model)); // unreadable/invalid is cached as other until the flush
        }
        return s.cls == prop;
    }
    template <class Read> void resolve_roots(Read& read, const Addresses& a) {
        roots_known = true;
        using object_capture::Status;
        const auto o = object_capture::own_ship(read, std::uint32_t(a.cockpit_slot));
        const auto t = object_capture::target(read, std::uint32_t(a.cockpit_slot));
        const bool own_ok = o.status == Status::Ready || o.status == Status::NoTarget;
        const bool target_ok = t.status == Status::Ready || t.status == Status::NoTarget ||
                               (o.status == Status::NoTarget && t.status == Status::Missing);
        roots_ok = own_ok && target_ok;
        own = o.status == Status::Ready ? o.node : 0;
        own_handle = o.status == Status::Ready ? o.node_handle : 0;
        target = t.status == Status::Ready ? t.root : 0;
        target_handle = t.status == Status::Ready ? t.root_handle : 0;
        if (own != walk_own || own_handle != walk_own_handle || target != walk_target ||
            target_handle != walk_target_handle) {
            std::memset(walks, 0, sizeof walks);
            walk_own = own;
            walk_own_handle = own_handle;
            walk_target = target;
            walk_target_handle = target_handle;
        }
    }
    // 1 neither, 2 own, 3 target, 0 unreadable (kept as deferred); the root is confirmed by its handle.
    template <class Read> std::uint8_t walk(Read& read, std::uint32_t node) {
        ++window.walks;
        for (unsigned depth = 0; depth < walk_depth && node && !(node & 3); ++depth) {
            std::uint32_t words[5]{}; // +0x18 parent .. +0x28 handle
            if (!read(std::uintptr_t(node) + parent_offset, words, sizeof words)) return 0;
            if (own && node == own && words[4] == own_handle) return 2;
            if (target && node == target && words[4] == target_handle) return 3;
            node = words[0];
        }
        return 1;
    }
    template <class Read> Verdict ancestry(Read& read, std::uint32_t node) {
        if (!own && !target) return Verdict::culled;
        std::uint32_t handle = 0;
        if (!read(std::uintptr_t(node) + handle_offset, &handle, 4)) return Verdict::deferred;
        WalkSlot& s = walks[slot_of(node, walk_slots)];
        std::uint8_t v = 0;
        if (s.verdict && s.node == node && s.handle == handle && frame - s.stamp < walk_frames)
            v = s.verdict;
        else {
            if (walks_this_frame >= walks_per_frame) return Verdict::deferred;
            ++walks_this_frame;
            v = walk(read, node);
            if (!v) return Verdict::deferred;
            s = WalkSlot{node, handle, frame, v};
        }
        return v == 2 ? Verdict::exempt_own : v == 3 ? Verdict::exempt_target : Verdict::culled;
    }
    // The engine's mesh-part box behind a scope descriptor (diagnostic rows only).
    template <class Read> static bool engine_part_box(Read& read, std::uint32_t descriptor, Box& box) {
        std::uint32_t part = 0;
        std::int32_t raw[part_bounds_words]{};
        return descriptor && !(descriptor & 3) && read(std::uintptr_t(descriptor), &part, 4) && part && !(part & 3) &&
               read(std::uintptr_t(part) + part_bounds_offset, raw, sizeof raw) && part_box(raw, box);
    }
    // The node-level decision (no memo, no counters): model class, the draw's vertex extent (`extent_of()`: the
    // object-space box of the drawn range, null when not known; asked for prop draws only), size, ancestry.
    template <class Read, class Extent>
    Verdict decide(Read& read, const Addresses& a, std::uint32_t node, Extent&& extent_of, const float* rows,
                   unsigned width, unsigned height) {
        if (!node || (node & 3)) return Verdict::no_scope;
        std::uint32_t model = 0;
        if (!read(std::uintptr_t(node) + model_offset, &model, 4)) return Verdict::no_scope;
        if (!is_prop(read, a, model)) return Verdict::not_prop;
        const Box* extent = extent_of();
        if (!extent) return Verdict::no_bounds;
        float radius = 0.f;
        if (!rows || !screen_radius(rows, *extent, width, height, &radius)) return Verdict::unbounded;
        if (!(radius < px)) return Verdict::kept_size;
        if (!roots_known) resolve_roots(read, a);
        if (!roots_ok) return Verdict::unresolved;
        return ancestry(read, node);
    }
    // One scene draw: the node's verdict of this frame (the first draw decides) and the window counts. `first` is
    // set when this draw decided (the caller then reports a culled node to the census once).
    template <class Read, class Extent>
    Verdict evaluate(Read& read, const Addresses& a, std::uint32_t node, Extent&& extent_of, const float* rows,
                     unsigned width, unsigned height, bool* first = nullptr) {
        if (first) *first = false;
        Verdict v;
        MemoSlot* m = node ? &memo[slot_of(node, memo_slots)] : nullptr;
        if (m && m->node == node && m->frame == frame && frame_started)
            v = m->verdict;
        else {
            v = decide(read, a, node, extent_of, rows, width, height);
            if (m) *m = MemoSlot{node, frame, v};
            if (first) *first = true;
            if (v == Verdict::culled) ++window.nodes_culled;
        }
        count(v);
        return v;
    }
    void count(Verdict v) {
        switch (v) {
        case Verdict::no_scope: ++window.no_scope; return;
        case Verdict::not_prop: return;
        case Verdict::culled: ++window.culled; break;
        case Verdict::kept_size: ++window.kept_size; break;
        case Verdict::exempt_own: ++window.exempt_own; break;
        case Verdict::exempt_target: ++window.exempt_target; break;
        case Verdict::unresolved: ++window.unresolved; break;
        case Verdict::deferred: ++window.deferred; break;
        case Verdict::no_bounds: ++window.no_bounds; break;
        case Verdict::unbounded: ++window.unbounded; break;
        default: return;
        }
        ++window.draws;
        if (v != Verdict::culled) ++window.kept;
    }
};
}
