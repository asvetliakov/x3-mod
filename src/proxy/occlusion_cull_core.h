#pragma once
// Occlusion cull of ship sub-parts (X3M_OCCLUSION_CULL=on|off, default on; docs/architecture/occlusion-cull.md).
// The pure parts: the body-name classification (hull / part / effect / other, the estimate's rules over the
// engine's body table, cull_census_core.h), the per-frame owner table (a part's hull drew this frame), the test
// rectangle (the draw's vertex-extent box through its own clip rows: the screen rectangle of its eight corners at
// their nearest depth, inflated by one pixel), the stability guard, and the query ring's bookkeeping (two frame slots
// of pool_per_frame records, the previous slot's results in a stamped hash). The D3D side is
// renderer::OcclusionCullPass; the draw-site integration is motion_output_occlusion_cull_inc.h.
//
// Pure: no D3D, no Windows; the engine reads go through the caller's `read` (engine_memory::read in production, a
// synthetic image on the host and in the fixtures). Scalar float and integer arithmetic only, no allocation.
#include "cull_census_core.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace x3m::occlusion_cull::core {
// The query pool: sized once per device, two frame slots (the ring), no per-frame allocation (user 2026-10-08:
// every candidate is tested every frame, limited only by the pool).
constexpr unsigned pool_size = 1024, pool_per_frame = pool_size / 2;
constexpr unsigned model_offset = 0x140, parent_offset = 0x18;
constexpr unsigned name_read = 96; // body-path bytes classified (the rules look at the leading directories and words)
constexpr unsigned class_slots = 1024, memo_slots = 1024, owner_slots = 1024, result_slots = 2048;
constexpr unsigned walk_depth = 6; // part -> dummy -> ship root is two links (ship-scene-parts.md); turrets one more
constexpr unsigned window_frames = 300;
constexpr float w_epsilon = 1e-6f;
static_assert((memo_slots & (memo_slots - 1)) == 0 && (owner_slots & (owner_slots - 1)) == 0 &&
                  (result_slots & (result_slots - 1)) == 0 && result_slots >= 2 * pool_per_frame,
              "power-of-two tables, the result table at most half full");

inline std::uint32_t slot_of(std::uint32_t value, std::uint32_t slots) {
    return (value * 2654435761u) >> 16 & (slots - 1);
}
// `on` or unset/empty enables (the default), `off` disables, anything else is refused.
inline bool parse_mode(const char* text, bool* on) {
    if (text && !std::strcmp(text, "off")) {
        *on = false;
        return true;
    }
    if (!text || !*text || !std::strcmp(text, "on")) {
        *on = true;
        return true;
    }
    return false;
}

enum class Class : std::uint8_t { unknown = 0, hull, part, effect, other };
inline const char* class_name(Class c) {
    switch (c) {
    case Class::hull: return "hull";
    case Class::part: return "part";
    case Class::effect: return "effect";
    case Class::other: return "other";
    default: return "unknown";
    }
}
inline bool contains(const char* s, const char* word) {
    return std::strstr(s, word) != nullptr;
}
// The estimate's rules (verification/results/occlusion-cull-estimate/analyze.py, classify) on the lower-cased path
// with '/' read as '\': a dock cut scene's inline body id is a part whatever its name; an `effects\` path is an effect
// (engine jets, glows); turret, weapon, gun-barrel, dock and antenna bodies and `ships\props\` are parts; any other
// `ships\` body is a hull piece; everything else (stations, environments, menu graphics, unnamed) is other.
inline Class classify_name(const char* name, std::uint32_t model) {
    if (x3m::cull_census::core::dock_model(model)) return Class::part;
    if (!name || !*name) return Class::other;
    char s[name_read + 1]{};
    for (unsigned i = 0; i < name_read && name[i]; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        s[i] = c == '/' ? '\\' : c;
    }
    if (contains(s, "effects\\")) return Class::effect;
    const bool ship = !std::strncmp(s, "ships\\", 6);
    if (!std::strncmp(s, "ships\\props\\", 12) || contains(s, "turret") || contains(s, "weapon") ||
        contains(s, "gbarrel") || contains(s, "dock") || contains(s, "antenna"))
        return Class::part;
    return ship ? Class::hull : Class::other;
}

struct Box {
    float lo[3], hi[3];
};
// D3DCMPFUNC values (d3d9types.h) without the header.
constexpr unsigned cmp_less = 2, cmp_lessequal = 4, cmp_greater = 5, cmp_greaterequal = 7;
enum class RectStatus : std::uint8_t { ok, near_plane, zfunc, degenerate };
// The test shape: c252 = (x0, y0, z, 1) and c253 = (x1 - x0, y1 - y0, 0, 0) in clip space with w = 1 (the strip's
// corners (0|1, 0|1) map through `mad o0, v0, c253, c252`), the box's eight corners' screen rectangle inflated by one
// pixel on every side at their nearest depth (the smallest z/w under LESS/LESSEQUAL, the largest under
// GREATER/GREATEREQUAL). Every pixel the box's projection covers is inside the rectangle and no point of the box is
// nearer than its depth, so a rectangle whose every pixel fails the depth test proves the drawn range hidden at this
// depth buffer (conservative: it can only report visible where the box is hidden). The pixel inflation covers the
// rasterisation rules and the frame's sub-pixel TAA jitter (the test is issued before the draw's jitter). cx, cy, w, h
// are the rectangle in viewport pixels (the stability guard's input).
struct Rect {
    float c252[4], c253[4];
    float cx, cy, w, h;
};
inline RectStatus test_rect(const float rows[16], const Box& b, float vp_width, float vp_height, unsigned zfunc,
                            Rect* out) {
    if (!rows || !(vp_width >= 1.f) || !(vp_height >= 1.f)) return RectStatus::degenerate;
    const bool less = zfunc == cmp_less || zfunc == cmp_lessequal;
    if (!less && zfunc != cmp_greater && zfunc != cmp_greaterequal) return RectStatus::zfunc;
    float x_lo = 3.4e38f, x_hi = -3.4e38f, y_lo = 3.4e38f, y_hi = -3.4e38f, z_near = less ? 3.4e38f : -3.4e38f;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? b.hi[0] : b.lo[0], y = (corner & 2) ? b.hi[1] : b.lo[1],
                    z = (corner & 4) ? b.hi[2] : b.lo[2];
        const float cx = rows[0] * x + rows[1] * y + rows[2] * z + rows[3];
        const float cy = rows[4] * x + rows[5] * y + rows[6] * z + rows[7];
        const float cz = rows[8] * x + rows[9] * y + rows[10] * z + rows[11];
        const float cw = rows[12] * x + rows[13] * y + rows[14] * z + rows[15];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || !std::isfinite(cw) || cw <= w_epsilon)
            return RectStatus::near_plane;
        const float nx = cx / cw, ny = cy / cw, nz = cz / cw;
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return RectStatus::near_plane;
        x_lo = nx < x_lo ? nx : x_lo;
        x_hi = nx > x_hi ? nx : x_hi;
        y_lo = ny < y_lo ? ny : y_lo;
        y_hi = ny > y_hi ? ny : y_hi;
        z_near = less ? (nz < z_near ? nz : z_near) : (nz > z_near ? nz : z_near);
    }
    // A corner in front of the near plane (or beyond the far plane under a reversed depth) has no depth the test can
    // use: the box is drawn.
    if (!(z_near >= 0.f) || !(z_near <= 1.f)) return RectStatus::near_plane;
    const float px = 2.f / vp_width, py = 2.f / vp_height;
    x_lo -= px;
    x_hi += px;
    y_lo -= py;
    y_hi += py;
    out->c252[0] = x_lo;
    out->c252[1] = y_lo;
    out->c252[2] = z_near;
    out->c252[3] = 1.f;
    out->c253[0] = x_hi - x_lo;
    out->c253[1] = y_hi - y_lo;
    out->c253[2] = 0.f;
    out->c253[3] = 0.f;
    out->w = (x_hi - x_lo) * .5f * vp_width;
    out->h = (y_hi - y_lo) * .5f * vp_height;
    out->cx = (x_hi + x_lo) * .25f * vp_width;
    out->cy = (y_hi + y_lo) * .25f * vp_height;
    if (!std::isfinite(out->w) || !std::isfinite(out->h) || !std::isfinite(out->cx) || !std::isfinite(out->cy))
        return RectStatus::degenerate;
    return RectStatus::ok;
}
// The transform guard: last frame's hidden verdict carries over only while the part's test rectangle has not jumped.
// A turret turning in place keeps its rectangle (centre within a quarter of its size or 16 px, size within 0.75..1.33);
// a node teleported, a camera cut or a fast pan does not, and the part is drawn that frame.
constexpr float stable_min_px = 16.f, stable_shift = .25f, stable_ratio = 4.f / 3.f;
inline bool stable(const Rect& was, const Rect& now) {
    const float size = was.w > was.h ? was.w : was.h;
    const float shift = size * stable_shift > stable_min_px ? size * stable_shift : stable_min_px;
    if (!(std::fabs(now.cx - was.cx) <= shift) || !(std::fabs(now.cy - was.cy) <= shift)) return false;
    const float a = was.w * was.h, b = now.w * now.h;
    if (!(a > 0.f) || !(b > 0.f)) return false;
    return b <= a * stable_ratio * stable_ratio && a <= b * stable_ratio * stable_ratio;
}

// The query ring's bookkeeping (no D3D): frame f issues into slot f & 1. At the frame's first candidate the caller
// reads the two older frames' queries before the slot is reused: lag(2) = frame f - 2 (slot f & 1, only the records
// whose result was not ready when read at f - 1) through ingest(2, ...), then lag(1) = frame f - 1 through
// ingest(1, ...), then start() takes the slot for this frame. lookup() answers with the most recent READY result of a
// draw up to two frames old (age 1 or 2), else not ready / error / none; the rectangle it carries is that result's.
enum class Result : std::uint8_t { none = 0, visible, hidden, not_ready, error };
inline bool ready(Result r) {
    return r == Result::visible || r == Result::hidden;
}
struct Ring {
    struct Record {
        std::uint64_t key;
        Rect rect;
        bool issued, skipped;
        Result result; // as last read (none before the first read)
    };
    struct ResultSlot {
        std::uint64_t key;
        std::uint32_t stamp;
        Result result;
        std::uint8_t age; // 1 or 2 frames old
        Rect rect;
    };
    Record records[2][pool_per_frame]{};
    unsigned used[2]{};
    std::uint32_t slot_frame[2]{}; // the frame whose records a slot holds (0: none)
    ResultSlot results[result_slots]{};
    std::uint32_t frame = 0, results_frame = 0;
    bool previous_read = false;
    // Empties every slot: the first frame after a Reset (or an attach) has no previous result and draws everything.
    void clear() {
        used[0] = used[1] = 0;
        slot_frame[0] = slot_frame[1] = 0;
        std::memset(results, 0, sizeof results);
        results_frame = 0;
        previous_read = false;
    }
    void begin(std::uint32_t f) {
        frame = f;
        previous_read = false;
    }
    // The records of frame f - lag still held (0 when the slot holds another frame or nothing).
    unsigned lag_count(unsigned lag) const {
        const unsigned s = (frame - lag) & 1;
        return frame > lag && slot_frame[s] == frame - lag ? used[s] : 0;
    }
    unsigned lag_slot(unsigned lag) const { return (frame - lag) & 1; }
    Record& lag_record(unsigned lag, unsigned index) { return records[lag_slot(lag)][index]; }
    // A read of record `index` of frame f - lag. Lag 2 (read first) enters only a ready result; lag 1 enters a ready
    // result over it, and a not-ready or error one only where lag 2 left nothing ready.
    void ingest(unsigned lag, unsigned index, Result r) {
        Record& rec = lag_record(lag, index);
        rec.result = r;
        if (lag == 2 && !ready(r)) return;
        ResultSlot* slot = &results[slot_of(std::uint32_t(rec.key) ^ std::uint32_t(rec.key >> 32), result_slots)];
        for (unsigned probe = 0; probe < result_slots; ++probe) {
            if (slot->stamp != frame || slot->key == rec.key) break;
            slot = &results[(unsigned(slot - results) + 1) & (result_slots - 1)];
        }
        if (slot->stamp == frame && slot->key == rec.key && ready(slot->result) && !ready(r)) return; // keep age 2
        *slot = ResultSlot{rec.key, frame, r, std::uint8_t(lag), rec.rect};
        results_frame = frame;
    }
    // This frame takes its slot (after the lag reads).
    void start() {
        const unsigned s = frame & 1;
        used[s] = 0;
        slot_frame[s] = frame;
        previous_read = true;
    }
    const ResultSlot* lookup(std::uint64_t key) const {
        if (results_frame != frame) return nullptr;
        const ResultSlot* slot = &results[slot_of(std::uint32_t(key) ^ std::uint32_t(key >> 32), result_slots)];
        for (unsigned probe = 0; probe < result_slots; ++probe) {
            if (slot->stamp != frame) return nullptr;
            if (slot->key == key) return slot;
            slot = &results[(unsigned(slot - results) + 1) & (result_slots - 1)];
        }
        return nullptr;
    }
    // This frame's next record, or null when the slot is full (pool_truncated).
    Record* reserve(std::uint64_t key, const Rect& rect) {
        const unsigned s = frame & 1;
        if (used[s] >= pool_per_frame) return nullptr;
        Record* r = &records[s][used[s]++];
        *r = Record{key, rect, false, false, Result::none};
        return r;
    }
    unsigned index_of(const Record* r) const { return unsigned(r - records[frame & 1]); }
};
// The skip rule: the draw's most recent ready test (one or two frames old) read 0 samples, its hull drew this frame and
// its rectangle is stable against that test's. Anything else draws.
inline bool may_skip(const Ring::ResultSlot* previous, const Rect& now, bool hull_drawn) {
    return previous && previous->result == Result::hidden && hull_drawn && stable(previous->rect, now);
}
// The draw identity across frames: the scope node and the drawn geometry (vertex buffer identity, first vertex,
// vertex count) and the model id (a node reused for another body is a different draw).
inline std::uint64_t draw_key(std::uint32_t node, std::uint64_t buffer, std::uint32_t first, std::uint32_t count,
                              std::uint32_t model) {
    std::uint32_t h = 2166136261u;
    const std::uint32_t words[5] = {std::uint32_t(buffer), std::uint32_t(buffer >> 32), first, count, model};
    for (std::uint32_t w : words)
        for (unsigned i = 0; i < 4; ++i) h = (h ^ ((w >> (8 * i)) & 0xffu)) * 16777619u;
    return std::uint64_t(node) | std::uint64_t(h) << 32;
}

// The engine side (motion route): the model-class cache (flushed every window), this frame's hull owners and the
// per-node memo of a part's verdict (its class and whether one of its ancestors owns a hull drawn this frame).
struct Classifier {
    struct ClassSlot {
        std::uint32_t model;
        Class cls;
    };
    struct OwnerSlot {
        std::uint32_t node, stamp;
    };
    struct MemoSlot {
        std::uint32_t node, stamp, model;
        Class cls;
        bool hull_drawn;
    };
    ClassSlot classes[class_slots]{};
    OwnerSlot owners[owner_slots]{};
    MemoSlot memo[memo_slots]{};
    std::uint32_t frame = 0;
    unsigned resolves = 0, walks = 0;
    void begin(std::uint32_t f) { frame = f; }
    void flush_classes() { std::memset(classes, 0, sizeof classes); }
    void clear() {
        std::memset(owners, 0, sizeof owners);
        std::memset(memo, 0, sizeof memo);
    }
    template <class Read> Class resolve(Read& read, std::uintptr_t body_global, std::uint32_t model) {
        using namespace x3m::cull_census::core;
        ++resolves;
        if (dock_model(model)) return Class::part;
        std::uint32_t g = 0;
        std::int32_t head[3]{};
        if (!read(body_global, &g, 4) || !g || !read(std::uintptr_t(g) + body_fixed_count_offset, head, sizeof head))
            return Class::other;
        if (head[0] != body_fixed_count || head[1] < 0 || head[1] >= body_dynamic_limit || !head[2]) return Class::other;
        std::uint32_t index = 0, p = 0;
        if (!body_slot(std::int32_t(model), head[0], head[1], &index)) return Class::other;
        const std::uint64_t entry =
            std::uint64_t(std::uint32_t(head[2])) + std::uint64_t(index) * body_slot_stride + body_slot_name_offset;
        if (entry > 0xfffffffcu || !read(std::uintptr_t(entry), &p, 4) || !p) return Class::other;
        char name[name_read + 1]{};
        unsigned have = 0;
        while (have < name_read) { // page-bounded: a short name before an unreadable page still reads
            const std::uint64_t at = std::uint64_t(p) + have;
            if (at > 0xffffffffu) return Class::other;
            unsigned chunk = unsigned(0x1000 - (at & 0xfff));
            if (chunk > name_read - have) chunk = name_read - have;
            if (!read(std::uintptr_t(at), name + have, chunk)) {
                if (!have) return Class::other;
                break;
            }
            bool end = false;
            for (unsigned i = have; i < have + chunk; ++i)
                if (!name[i]) end = true;
            have += chunk;
            if (end) break;
        }
        name[name_read] = 0;
        return classify_name(name, model);
    }
    template <class Read> Class class_of(Read& read, std::uintptr_t body_global, std::uint32_t model) {
        ClassSlot& s = classes[model % class_slots];
        if (s.cls == Class::unknown || s.model != model) s = ClassSlot{model, resolve(read, body_global, model)};
        return s.cls;
    }
    void add_owner(std::uint32_t node) {
        if (!node || (node & 3)) return;
        OwnerSlot* s = &owners[slot_of(node, owner_slots)];
        for (unsigned probe = 0; probe < 8; ++probe) {
            if (s->stamp != frame || s->node == node) {
                *s = OwnerSlot{node, frame};
                return;
            }
            s = &owners[(unsigned(s - owners) + 1) & (owner_slots - 1)];
        }
        // eight collisions: dropped (a part of it then reads hull_drawn false and is drawn)
    }
    bool is_owner(std::uint32_t node) const {
        const OwnerSlot* s = &owners[slot_of(node, owner_slots)];
        for (unsigned probe = 0; probe < 8; ++probe) {
            if (s->stamp != frame) return false;
            if (s->node == node) return true;
            s = &owners[(unsigned(s - owners) + 1) & (owner_slots - 1)];
        }
        return false;
    }
    // A hull draw this frame: the hull node owns it, and so does its parent when that parent is a ship root. A ship root
    // is a top-level node (its parent is null): every hull draw of the run14/run15 captures has ancestry 2, hull -> root
    // (verification/results/occlusion-cull/ancestry.txt; ship-scene-parts.md: part -> dummy -> root, the hull another
    // child of the root). A parent that is not top-level is never registered, so no node above a ship root (and no root
    // of another object) can own a hull.
    template <class Read> void note_hull(Read& read, std::uint32_t node) {
        add_owner(node);
        std::uint32_t parent = 0, grandparent = 1;
        if (read(std::uintptr_t(node) + parent_offset, &parent, 4) && parent && !(parent & 3) &&
            read(std::uintptr_t(parent) + parent_offset, &grandparent, 4) && !grandparent)
            add_owner(parent);
    }
    // Whether an ancestor of the part (its parent, grandparent, ... at most walk_depth links, ending at the ship root,
    // the top-level node) owns a hull drawn this frame.
    template <class Read> bool hull_drawn(Read& read, std::uint32_t node) {
        ++walks;
        for (unsigned depth = 0; depth < walk_depth && node && !(node & 3); ++depth) {
            std::uint32_t parent = 0;
            if (!read(std::uintptr_t(node) + parent_offset, &parent, 4)) return false;
            if (parent && is_owner(parent)) return true;
            node = parent; // a null parent: the node was the ship root, the walk ends
        }
        return false;
    }
    // One scene draw's node: its class and, for a part, whether its hull drew this frame (memoised per node per frame,
    // so every draw of a node shares the first one's answer). model is set to the node's model id.
    template <class Read>
    Class evaluate(Read& read, std::uintptr_t body_global, std::uint32_t node, std::uint32_t* model, bool* hull) {
        *hull = false;
        *model = 0;
        if (!node || (node & 3)) return Class::other;
        MemoSlot& m = memo[slot_of(node, memo_slots)];
        if (m.node == node && m.stamp == frame && frame) {
            *model = m.model;
            *hull = m.hull_drawn;
            return m.cls;
        }
        if (!read(std::uintptr_t(node) + model_offset, model, 4)) return Class::other;
        const Class c = class_of(read, body_global, *model);
        bool h = false;
        if (c == Class::hull)
            note_hull(read, node);
        else if (c == Class::part)
            h = hull_drawn(read, node);
        m = MemoSlot{node, frame, *model, c, h};
        *hull = h;
        return c;
    }
};
}
