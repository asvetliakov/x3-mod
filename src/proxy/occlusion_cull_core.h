#pragma once
// Occlusion cull of ship sub-parts (X3M_OCCLUSION_CULL=on|off, default on; docs/architecture/occlusion-cull.md).
// The pure parts: the body-name classification (hull / part / effect / other, the estimate's rules over the
// engine's body table, cull_census_core.h), the per-frame owner table (a part's hull drew this frame, and the ship root
// it belongs to), the test rectangle (the draw's vertex-extent box through its own clip rows: the screen rectangle of
// its eight corners at their nearest depth, inflated by one pixel), the stability guard, the query ring's bookkeeping
// (two frame slots of pool_per_frame records, the previous slot's results in a stamped hash) and the per-ship batching
// (Batcher: last frame's candidates of a ship, reprojected through the ship's hull rows, tested in one block at the
// ship's first part draw; a part last read visible is re-tested once every `retest` frames, on its own phase). The
// D3D side is
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
// Batching: the per-draw table (persistent, a draw unseen for part_expiry frames frees its slot), this frame's ships
// and hull rows (stamped per frame), the re-test cadence of a part last read visible (X3M_OCCLUSION_CULL_RETEST).
constexpr unsigned part_slots = 2048, part_probe = 16, part_expiry = 120, ship_slots = 256, ship_probe = 32,
                   hull_slots = 512, hull_probe = 16;
constexpr unsigned retest_default = 8, retest_max = 64;
constexpr std::uint16_t no_entry = 0xffffu;
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
// X3M_OCCLUSION_CULL_RETEST: unset/empty = retest_default; a decimal 1..retest_max; anything else is refused.
inline bool parse_retest(const char* text, unsigned* frames) {
    if (!text || !*text) {
        *frames = retest_default;
        return true;
    }
    unsigned value = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < '0' || *c > '9' || c - text >= 3) return false;
        value = value * 10 + unsigned(*c - '0');
    }
    if (value < 1 || value > retest_max) return false;
    *frames = value;
    return true;
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
        Result result;       // as last read (none before the first read)
        std::uint16_t entry; // the Batcher's part slot (no_entry: none)
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
    Record* reserve(std::uint64_t key, const Rect& rect, std::uint16_t entry = no_entry) {
        const unsigned s = frame & 1;
        if (used[s] >= pool_per_frame) return nullptr;
        Record* r = &records[s][used[s]++];
        *r = Record{key, rect, false, false, Result::none, entry};
        return r;
    }
    unsigned free_records() const { return pool_per_frame - used[frame & 1]; }
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
        std::uint32_t node, stamp, ship; // ship: the root the owner belongs to (the batch's key)
    };
    struct MemoSlot {
        std::uint32_t node, stamp, model, ship;
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
    void add_owner(std::uint32_t node, std::uint32_t ship) {
        if (!node || (node & 3)) return;
        OwnerSlot* s = &owners[slot_of(node, owner_slots)];
        for (unsigned probe = 0; probe < 8; ++probe) {
            if (s->stamp != frame || s->node == node) {
                *s = OwnerSlot{node, frame, ship};
                return;
            }
            s = &owners[(unsigned(s - owners) + 1) & (owner_slots - 1)];
        }
        // eight collisions: dropped (a part of it then reads hull_drawn false and is drawn)
    }
    bool is_owner(std::uint32_t node, std::uint32_t* ship = nullptr) const {
        const OwnerSlot* s = &owners[slot_of(node, owner_slots)];
        for (unsigned probe = 0; probe < 8; ++probe) {
            if (s->stamp != frame) return false;
            if (s->node == node) {
                if (ship) *ship = s->ship;
                return true;
            }
            s = &owners[(unsigned(s - owners) + 1) & (owner_slots - 1)];
        }
        return false;
    }
    // A hull draw this frame: the hull node owns it, and so does its parent when that parent is a ship root. A ship root
    // is a top-level node (its parent is null): every hull draw of the run14/run15 captures has ancestry 2, hull -> root
    // (verification/results/occlusion-cull/ancestry.txt; ship-scene-parts.md: part -> dummy -> root, the hull another
    // child of the root). A parent that is not top-level is never registered, so no node above a ship root (and no root
    // of another object) can own a hull. Returns the hull's ship: its top-level parent, else the hull node itself.
    template <class Read> std::uint32_t note_hull(Read& read, std::uint32_t node) {
        std::uint32_t parent = 0, grandparent = 1;
        const bool root = read(std::uintptr_t(node) + parent_offset, &parent, 4) && parent && !(parent & 3) &&
                          read(std::uintptr_t(parent) + parent_offset, &grandparent, 4) && !grandparent;
        const std::uint32_t ship = root ? parent : node;
        add_owner(node, ship);
        if (root) add_owner(parent, parent);
        return ship;
    }
    // Whether an ancestor of the part (its parent, grandparent, ... at most walk_depth links, ending at the ship root,
    // the top-level node) owns a hull drawn this frame; *ship is that owner's ship.
    template <class Read> bool hull_drawn(Read& read, std::uint32_t node, std::uint32_t* ship) {
        ++walks;
        for (unsigned depth = 0; depth < walk_depth && node && !(node & 3); ++depth) {
            std::uint32_t parent = 0;
            if (!read(std::uintptr_t(node) + parent_offset, &parent, 4)) return false;
            if (parent && is_owner(parent, ship)) return true;
            node = parent; // a null parent: the node was the ship root, the walk ends
        }
        return false;
    }
    // One scene draw's node: its class and, for a part, whether its hull drew this frame (memoised per node per frame,
    // so every draw of a node shares the first one's answer). model is set to the node's model id; ship (optional) to
    // the hull's ship (a hull) or the owner's ship (a part whose hull drew), else 0; fresh (optional) says whether this
    // was the node's first evaluation this frame (a hull's rows and signature are taken once per node per frame).
    template <class Read>
    Class evaluate(Read& read, std::uintptr_t body_global, std::uint32_t node, std::uint32_t* model, bool* hull,
                   std::uint32_t* ship = nullptr, bool* fresh = nullptr) {
        *hull = false;
        *model = 0;
        if (ship) *ship = 0;
        if (fresh) *fresh = false;
        if (!node || (node & 3)) return Class::other;
        MemoSlot& m = memo[slot_of(node, memo_slots)];
        if (m.node == node && m.stamp == frame && frame) {
            *model = m.model;
            *hull = m.hull_drawn;
            if (ship) *ship = m.ship;
            return m.cls;
        }
        if (!read(std::uintptr_t(node) + model_offset, model, 4)) return Class::other;
        const Class c = class_of(read, body_global, *model);
        bool h = false;
        std::uint32_t owner_ship = 0;
        if (c == Class::hull)
            owner_ship = note_hull(read, node);
        else if (c == Class::part && !(h = hull_drawn(read, node, &owner_ship)))
            owner_ship = 0;
        m = MemoSlot{node, frame, *model, owner_ship, c, h};
        *hull = h;
        if (ship) *ship = owner_ship;
        if (fresh) *fresh = true;
        return c;
    }
};

// ---- per-ship batching (docs/architecture/occlusion-cull.md, "Batching") ----
//
// Reprojection: the block runs at the ship's first part draw, before the other parts' rows of this frame exist, so a
// part's rectangle is carried from its last draw through its ship's reference hull (the ship's first hull node drawn
// with known rows; the hull pieces and the parts hang off the same ship root): rel = inverse(hull rows then) x part rows
// then, and this frame's rows = hull rows now x rel. Exact for a part rigid with its hull under any camera, ship or
// projection change; a turning turret keeps last frame's angle (within the stability guard's tolerance). The inverse in
// double (the rows include the projection); rel is accepted only when hull rows then x rel gives the part's rows back.
// |v| for a double without std::fabs (which the i686 build lowers to x87 fabs; check_no_x87.py).
inline double abs_d(double v) {
    return v < 0.0 ? -v : v;
}
inline bool invert(const float m[16], double out[16]) {
    double a[4][8];
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            a[r][c] = double(m[r * 4 + c]);
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r)
            if (abs_d(a[r][c]) > abs_d(a[pivot][c])) pivot = r;
        const double p = a[pivot][c];
        if (!std::isfinite(p) || abs_d(p) < 1e-30) return false;
        if (pivot != c)
            for (unsigned k = 0; k < 8; ++k) {
                const double t = a[c][k];
                a[c][k] = a[pivot][k];
                a[pivot][k] = t;
            }
        const double inv = 1.0 / p;
        for (unsigned k = 0; k < 8; ++k) a[c][k] *= inv;
        for (unsigned r = 0; r < 4; ++r) {
            const double f = a[r][c];
            if (r == c || f == 0.0) continue;
            for (unsigned k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            out[r * 4 + c] = a[r][c + 4];
            if (!std::isfinite(out[r * 4 + c])) return false;
        }
    return true;
}
// out = a x b (row-major 4x4), accumulated in double.
inline void multiply(const double a[16], const float b[16], double out[16]) {
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (unsigned k = 0; k < 4; ++k) sum += a[r * 4 + k] * double(b[k * 4 + c]);
            out[r * 4 + c] = sum;
        }
}
inline void multiply(const float a[16], const float b[16], float out[16]) {
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (unsigned k = 0; k < 4; ++k) sum += double(a[r * 4 + k]) * double(b[k * 4 + c]);
            out[r * 4 + c] = float(sum);
        }
}
// rel = inverse(ref then) x rows then, accepted when ref then x rel reproduces rows within 1e-4 of their largest entry.
inline bool relative(const float ref[16], const double ref_inverse[16], const float rows[16], float rel[16]) {
    double d[16];
    multiply(ref_inverse, rows, d);
    float scale = 0.f;
    for (unsigned i = 0; i < 16; ++i) {
        if (!std::isfinite(d[i]) || abs_d(d[i]) > 3.0e38) return false;
        rel[i] = float(d[i]);
        const float m = std::fabs(rows[i]);
        scale = m > scale ? m : scale;
    }
    if (!(scale > 0.f)) return false;
    float back[16];
    multiply(ref, rel, back);
    for (unsigned i = 0; i < 16; ++i)
        if (!(std::fabs(back[i] - rows[i]) <= 1e-4f * scale)) return false;
    return true;
}

// Plan output: one test of the block (the draw key, the rectangle tested, the Batcher's part slot).
struct BlockItem {
    std::uint64_t key;
    Rect rect;
    std::uint16_t entry;
};
struct PlanStats {
    // cadence: tests of parts last read visible taken on their phase frame (the staggered re-test);
    // forced: tests of parts last read visible taken off-phase because they moved or their hull changed.
    unsigned retest_skipped = 0, stale = 0, refused = 0, truncated = 0, cadence = 0, forced = 0;
};
// The re-test phase seed of a draw: a fixed hash of its scene node and model id. The phase itself is chosen when the
// part first reads visible: the least-loaded phase among the visible parts of the previous frame's list, searched
// from the seed, the current frame's only when it is strictly the least loaded (so a part that just turned visible
// is rarely re-tested at once), and then kept for the part's life whatever its
// results. A plain hash mod retest spreads ~40 visible parts 0..10 per phase against a mean of 5 (host simulation),
// so the balance keeps each frame near 1/retest of them; no frame carries them all.
inline unsigned retest_phase(std::uint32_t node, std::uint32_t model, unsigned retest) {
    std::uint32_t h = (node ^ 0x9e3779b9u) * 0x85ebca6bu;
    h ^= h >> 13;
    h = (h ^ model) * 0xc2b2ae35u;
    h ^= h >> 16;
    return retest > 1 ? h % retest : 0u;
}
// Spread of the cadence tests over the last `retest` frames (min and max per frame): the flight's evidence that the
// staggering keeps the per-frame re-test count flat.
struct CadenceSpread {
    unsigned counts[retest_max]{};
    unsigned frames = 0;
    void push(unsigned count, unsigned retest) {
        counts[frames % (retest ? retest : 1u)] = count;
        ++frames;
    }
    void range(unsigned retest, unsigned* lo, unsigned* hi) const {
        const unsigned n = frames < retest ? frames : retest;
        *lo = n ? ~0u : 0u;
        *hi = 0;
        for (unsigned i = 0; i < n; ++i) {
            *lo = counts[i] < *lo ? counts[i] : *lo;
            *hi = counts[i] > *hi ? counts[i] : *hi;
        }
    }
};
// The batching bookkeeping. At each part draw (hull drawn) note_part() records the draw in a persistent per-draw table
// and in this frame's list; at the ship's first part draw of the next frame plan() takes that ship's entries of the
// previous frame's list (grouped by ship once per frame) and returns the tests of its block: every part whose last
// ready result was hidden or is unknown, and a part last read visible only on its phase frame (frame mod retest ==
// its phase: once every `retest` frames, staggered across the parts), or at once when it
// moved (its rectangle unstable against its last test's), its ship's hull signature changed (the set of hull draws
// before the block) or results were reset (Reset). A part first seen this frame is drawn untested and joins next
// frame's block. No allocation: every table is a fixed array.
struct Batcher {
    struct Part {
        std::uint64_t key;
        std::uint32_t ship, seen, tested, sig, ref, record_frame;
        std::uint16_t record; // this frame's ring record index (record_frame == frame)
        Result last;          // the most recent ready result (none: unknown, an error, or reset)
        bool less, rel_ok;
        std::uint8_t phase; // its re-test phase (unphased until its first visible result), then fixed
        std::uint8_t seed;  // retest_phase(node, model, retest)
        Box box;
        float rel[16];
        Rect rect;        // at its last draw
        Rect tested_rect; // of its last issued test
    };
    struct Ship {
        std::uint32_t root, stamp, group_stamp, sig, ref;
        std::uint16_t first, count; // its entries of the previous frame's list in grouped[] (group_stamp == frame)
        bool ref_ok, inv_done, inv_ok, block_done;
        float ref_rows[16];
        double inverse[16];
    };
    struct Hull {
        std::uint32_t node, stamp;
        bool rows_ok;
        float rows[16];
    };
    Part parts[part_slots]{};
    Ship ships[ship_slots]{};
    Hull hulls[hull_slots]{};
    std::uint16_t list[2][pool_per_frame]{};
    unsigned list_n[2]{};
    std::uint32_t list_frame[2]{};
    std::uint16_t grouped[pool_per_frame]{}, group_ship[pool_per_frame]{}, touched[ship_slots]{};
    std::uint32_t frame = 0, grouped_frame = 0;
    unsigned retest = retest_default;
    unsigned load[retest_max]{}; // visible parts per phase (the previous frame's list, plus this frame's new phases)
    static constexpr std::uint8_t unphased = 0xffu;

    void begin(std::uint32_t f) { frame = f; }
    // Reset: every part is tested at its next block (the ring's results went with the queries).
    void reset_results() {
        for (Part& p : parts) {
            p.last = Result::none;
            p.tested = 0;
            p.record_frame = 0;
        }
    }
    // This frame's ship (insert: created when absent; null when the table's probe window is full).
    Ship* ship_slot(std::uint32_t root, bool insert) {
        if (!root) return nullptr;
        Ship* s = &ships[slot_of(root, ship_slots)];
        for (unsigned probe = 0; probe < ship_probe; ++probe) {
            const bool live = s->stamp == frame || s->group_stamp == frame;
            if (!live) {
                if (!insert) return nullptr;
                *s = Ship{};
                s->root = root;
                return s;
            }
            if (s->root == root) return s;
            s = &ships[(unsigned(s - ships) + 1) & (ship_slots - 1)];
        }
        return nullptr;
    }
    void touch(Ship* s) {
        if (s->stamp == frame) return;
        s->stamp = frame;
        s->sig = 0;
        s->ref = 0;
        s->ref_ok = s->inv_done = s->inv_ok = s->block_done = false;
    }
    Hull* hull_slot(std::uint32_t node, bool insert, bool* inserted = nullptr) {
        if (inserted) *inserted = false;
        if (!node) return nullptr;
        Hull* h = &hulls[slot_of(node, hull_slots)];
        for (unsigned probe = 0; probe < hull_probe; ++probe) {
            if (h->stamp != frame) {
                if (!insert) return nullptr;
                *h = Hull{node, frame, false, {}};
                if (inserted) *inserted = true;
                return h;
            }
            if (h->node == node) return h;
            h = &hulls[(unsigned(h - hulls) + 1) & (hull_slots - 1)];
        }
        return nullptr;
    }
    // A hull node's first draw this frame: its ship's signature (a commutative sum over the hull draws, so the order
    // does not matter), its rows (the reprojection reference; the ship's first hull with rows is the ship's reference).
    void note_hull(std::uint32_t ship, std::uint32_t node, std::uint64_t key, const float* rows) {
        bool inserted = false;
        Hull* h = hull_slot(node, true, &inserted);
        if (!h || !inserted) return; // a second draw of the node, or the table full (no reference: stale rectangles)
        Ship* s = ship_slot(ship, true);
        if (!s) return;
        touch(s);
        const std::uint64_t mix = key * 0x9e3779b97f4a7c15ull;
        s->sig += std::uint32_t(mix >> 32) ^ std::uint32_t(mix);
        if (!rows) return;
        std::memcpy(h->rows, rows, sizeof h->rows);
        h->rows_ok = true;
        if (!s->ref_ok) {
            s->ref = node;
            std::memcpy(s->ref_rows, rows, sizeof s->ref_rows);
            s->ref_ok = true;
        }
    }
    // A part draw whose hull drew this frame: the per-draw entry (created, or taken over from another ship), its box,
    // rectangle and reprojection, and this frame's list. Returns its slot, or no_entry when the table or the list is
    // full (drawn untested).
    std::uint16_t note_part(std::uint64_t key, std::uint32_t ship, const Box& box, const Rect& rect, const float* rows,
                            bool less, std::uint32_t model = 0) {
        // The whole probe chain is searched for the key's live entry before an expired slot is reused (an expired slot
        // ahead of the live entry would otherwise take a second copy of the draw: lost phase and result, a duplicated
        // test). A never-used slot ends the chain (slots are never emptied, so nothing was inserted past it).
        Part* p = &parts[slot_of(std::uint32_t(key) ^ std::uint32_t(key >> 32), part_slots)];
        Part *found = nullptr, *reusable = nullptr;
        for (unsigned probe = 0; probe < part_probe; ++probe) {
            if (!p->seen) {
                if (!reusable) reusable = p;
                break;
            }
            const bool expired = frame - p->seen > part_expiry;
            if (!expired && p->key == key) {
                found = p;
                break;
            }
            if (expired && !reusable) reusable = p;
            p = &parts[(unsigned(p - parts) + 1) & (part_slots - 1)];
        }
        const bool fresh = !found;
        if (!found) found = reusable;
        if (!found) return no_entry;
        if (fresh || found->ship != ship) {
            *found = Part{};
            found->key = key;
            found->ship = ship;
            found->last = Result::none;
            found->phase = unphased;
            found->seed = std::uint8_t(retest_phase(std::uint32_t(key), model, retest));
        }
        found->box = box;
        found->rect = rect;
        found->less = less;
        found->rel_ok = false;
        found->ref = 0;
        Ship* s = ship_slot(ship, false);
        if (s && s->stamp == frame && s->ref_ok && rows) {
            if (!s->inv_done) {
                s->inv_done = true;
                s->inv_ok = invert(s->ref_rows, s->inverse);
            }
            if (s->inv_ok && relative(s->ref_rows, s->inverse, rows, found->rel)) {
                found->rel_ok = true;
                found->ref = s->ref;
            }
        }
        const std::uint16_t index = std::uint16_t(found - parts);
        if (found->seen == frame) return index; // a second draw of the same key this frame: listed once
        found->seen = frame;
        const unsigned lf = frame & 1;
        if (list_frame[lf] != frame) {
            list_frame[lf] = frame;
            list_n[lf] = 0;
        }
        if (list_n[lf] >= pool_per_frame) return no_entry;
        list[lf][list_n[lf]++] = index;
        return index;
    }
    // Whether the ship's block of this frame is still to be issued (its hull drew this frame).
    bool block_pending(std::uint32_t ship) {
        Ship* s = ship_slot(ship, false);
        return s && s->stamp == frame && !s->block_done;
    }
    // Groups the previous frame's list by ship (a counting sort into grouped[]), once per frame at the first block.
    void group() {
        grouped_frame = frame;
        const unsigned lf = (frame - 1) & 1;
        if (frame <= 1 || list_frame[lf] != frame - 1) return;
        const unsigned n = list_n[lf];
        unsigned ships_touched = 0;
        for (unsigned i = 0; i < n; ++i) {
            Ship* s = ship_slot(parts[list[lf][i]].ship, true);
            if (!s) {
                group_ship[i] = 0xffffu;
                continue;
            }
            if (s->group_stamp != frame) {
                s->group_stamp = frame;
                s->first = s->count = 0;
                touched[ships_touched++] = std::uint16_t(s - ships);
            }
            ++s->count;
            group_ship[i] = std::uint16_t(s - ships);
        }
        // The phase load: the listed parts last read visible, per phase (recounted every frame: no drift from expiry).
        for (unsigned& l : load) l = 0;
        for (unsigned i = 0; i < n; ++i) {
            const Part& p = parts[list[lf][i]];
            if (p.last == Result::visible && p.phase < retest) ++load[p.phase];
        }
        unsigned at = 0;
        for (unsigned t = 0; t < ships_touched; ++t) {
            Ship& s = ships[touched[t]];
            s.first = std::uint16_t(at);
            at += s.count;
            s.count = 0;
        }
        for (unsigned i = 0; i < n; ++i)
            if (group_ship[i] != 0xffffu) {
                Ship& s = ships[group_ship[i]];
                grouped[s.first + s.count++] = list[lf][i];
            }
    }
    // The re-test rule of a part whose rectangle this frame is `now`: hidden every frame; unknown, an error or a Reset
    // at once (required); last read visible on its phase frame (cadence), or off-phase when its hull changed or it moved
    // (forced); else not this frame. A part that just turned visible keeps its phase (no immediate re-test).
    enum class Due : std::uint8_t { no, required, cadence, forced };
    Due due(const Part& p, const Rect& now, std::uint32_t sig) const {
        if (p.last != Result::visible || !p.tested) return Due::required;
        if (retest <= 1 || p.phase >= retest || frame % retest == p.phase) return Due::cadence;
        if (p.sig != sig || !stable(p.tested_rect, now)) return Due::forced;
        return Due::no;
    }
    // The ship's block: its tests (at most cap) in items. Marks the block issued for this frame whatever the outcome
    // (one attempt per ship per frame). *sig is the ship's hull signature the tests are taken under.
    unsigned plan(std::uint32_t ship, float vp_width, float vp_height, unsigned zfunc, BlockItem* items, unsigned cap,
                  PlanStats* stats, std::uint32_t* sig) {
        if (grouped_frame != frame) group();
        Ship* s = ship_slot(ship, false);
        if (!s || s->stamp != frame || s->block_done) return 0;
        s->block_done = true;
        *sig = s->sig;
        if (s->group_stamp != frame) return 0;
        const bool less = zfunc == cmp_less || zfunc == cmp_lessequal;
        unsigned n = 0;
        for (unsigned k = s->first; k < unsigned(s->first) + s->count; ++k) {
            const std::uint16_t index = grouped[k];
            Part& p = parts[index];
            if (p.ship != ship) continue;
            if (p.less != less) {
                ++stats->refused;
                continue;
            }
            Rect now{};
            bool stale = false;
            if (p.seen == frame) {
                now = p.rect; // drawn already this frame (the block's own part): its exact rectangle
            } else {
                const Hull* h = p.rel_ok ? hull_slot(p.ref, false) : nullptr;
                if (h && h->rows_ok) {
                    float rows[16];
                    multiply(h->rows, p.rel, rows);
                    if (test_rect(rows, p.box, vp_width, vp_height, zfunc, &now) != RectStatus::ok) {
                        ++stats->refused;
                        continue;
                    }
                } else {
                    now = p.rect; // no reference this frame: last frame's rectangle
                    stale = true;
                }
            }
            const Due why = due(p, now, s->sig);
            if (why == Due::no) {
                ++stats->retest_skipped;
                continue;
            }
            if (n >= cap) {
                ++stats->truncated;
                continue;
            }
            if (stale) ++stats->stale;
            if (why == Due::cadence) ++stats->cadence;
            if (why == Due::forced) ++stats->forced;
            items[n++] = BlockItem{p.key, now, index};
        }
        return n;
    }
    // The block issued the item's test (ring record `record` of this frame).
    void tested(const BlockItem& item, std::uint32_t sig, std::uint16_t record) {
        Part& p = parts[item.entry];
        if (p.key != item.key) return;
        p.tested = frame;
        p.tested_rect = item.rect;
        p.sig = sig;
        p.record = record;
        p.record_frame = frame;
    }
    // A read of an older test: a ready result becomes the part's last; an error forgets it (tested at the next block).
    // The first visible result of a part gives it its phase (see retest_phase).
    void on_result(std::uint16_t entry, std::uint64_t key, Result r) {
        if (entry >= part_slots || parts[entry].key != key) return;
        Part& p = parts[entry];
        if (r == Result::visible && p.phase == unphased && retest > 1) {
            const unsigned now = frame % retest;
            unsigned best = retest, best_load = ~0u;
            for (unsigned i = 0; i < retest; ++i) { // the current phase only when strictly less loaded than the rest
                const unsigned phase = (p.seed + i) % retest, cost = 2 * load[phase] + (phase == now ? 1u : 0u);
                if (cost < best_load) best = phase, best_load = cost;
            }
            p.phase = std::uint8_t(best);
            ++load[best];
        }
        if (ready(r))
            p.last = r;
        else if (r == Result::error)
            p.last = Result::none;
    }
    // This frame's ring record of the part's test, or -1.
    int record_of(std::uint16_t entry, std::uint64_t key) const {
        if (entry >= part_slots || parts[entry].key != key || parts[entry].record_frame != frame) return -1;
        return parts[entry].record;
    }
};
}
