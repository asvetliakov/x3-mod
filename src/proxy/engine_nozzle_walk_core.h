#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "engine_effects_core.h"
#include "engine_far_jets_core.h"

// Portable core of the node-sourced engine nozzles (docs/architecture/engine-nozzle-source.md, ini
// engine_nozzle_source = node): for every ship whose hull the route drew this frame, the ship root's child list
// (root+0xc, [child+0] = next, the list ends at an element whose +0 is 0: the shape the jet drive 0x004596e0, the
// emitter site 0x00414590 and the pass 0x0047cfe0 walk) is read from the proxy at the plume stage's append point, and
// every main jet found becomes a far record (engine_effects_core.h far_record from a engine_far_jets_core.h Raw the
// walk fills), deduplicated by (node handle, view handle) against the frame's draw and far records (draw wins, the far
// copy next, the node record last) and against the far handler's engine-culled pairs. A nozzle the game's frustum test
// culled while its hull stayed on screen keeps its plume, plate and light this way. Reads go through a caller-supplied
// bounded reader (engine_memory::read in the proxy; a synthetic image in the host test); no allocation; integer and
// float-free except the throttle range compare. No Windows dependency: the host test compiles it.
namespace x3m::engine_nozzle::core {
namespace fj = x3m::engine_far_jets::core;

// X3M_ENGINE_NOZZLE_SOURCE=node|draw (ini engine_nozzle_source), read once at load: node walks the lists (default),
// draw keeps the record path as before the walk (draw and far records only, the Run139 hold for every ship).
enum class Source : std::uint8_t { node = 0, draw = 1 };
constexpr Source default_source = Source::node;
inline const char* source_name(Source s) noexcept {
    return s == Source::draw ? "draw" : "node";
}
template <class Char> inline bool parse_source(const Char* text, std::size_t n, Source* out) noexcept {
    const auto is = [&](const char* word) {
        std::size_t i = 0;
        for (; word[i]; ++i)
            if (i >= n || text[i] != Char(word[i])) return false;
        return i == n;
    };
    if (is("node")) {
        *out = Source::node;
        return true;
    }
    if (is("draw")) {
        *out = Source::draw;
        return true;
    }
    return false;
}

// The node block as the walk reads it (the first 0x150 bytes, the seam fixture's node size; every offset below is
// inside it and is one the far handler or the draw path's object scope already reads).
constexpr unsigned next_offset = 0, child_offset = 0xc, node_bytes = 0x150;
constexpr unsigned parent_offset = fj::parent_offset, handle_offset = fj::handle_offset,
                   scale70_offset = fj::scale70_offset, scale80_offset = fj::scale80_offset, scale84_offset = 0x84,
                   scale88_offset = fj::scale88_offset, position_offset = fj::position_offset,
                   basis_x_offset = fj::basis_x_offset, basis_z_offset = fj::basis_z_offset,
                   flags12c_offset = fj::flags12c_offset, flags130_offset = 0x130, model_offset = fj::model_offset;
// The guards (engine-nozzle-source.md section 1): the JET pair on +0x130 (SBTYPE_JET bodies, 0x00434708), the drive's
// hidden flag on +0x12c (the alternate nozzle set the drive is not showing), the back-pointer +0x18 = the root (a
// reused or foreign block), the unit x and y scales and a throttle between the drive's start value 0x147 (0.005) and
// the brake ceiling 9.0 (engine-effects.md sections 2 and 4).
constexpr std::uint32_t jet_pair = 0x4000001u, hidden_flag = 0x100000u, scale_one = 0x10000u;
constexpr std::uint32_t scale88_min = 0x147u, scale88_max = 0x90000u;
// A root's list is read for at most walk_bound elements (installed maximum 108 direct parts, stock 87); a longer list
// is cut there (overflow counted, the part read used).
constexpr unsigned walk_bound = 256;

// A bounded read of the process's memory: false when the span is not readable (engine_memory::read).
using Reader = bool (*)(void* context, std::uint32_t address, void* out, unsigned size);

// The ships whose hull the route drew this frame: their roots (the scope node's parent +0x18, or the node itself when
// parentless), each with the camera the hull draw was tagged with (handle and node pointer: the view handle of the
// records and the context scale's source) and the draw's registry (the lifetime serial's lookup). A generation-stamped
// open-addressing set of 512 slots (never cleared per frame) holding at most `capacity` roots; a root beyond that
// counts overflow. insert() is one multiplicative hash and a short probe per routed scene draw.
struct RootSet {
    static constexpr unsigned slots = 512, capacity = 256;
    static_assert((slots & (slots - 1)) == 0 && capacity * 2 <= slots, "a power of two, at most half full");
    std::uint32_t root[slots];
    std::uint32_t camera_handle[slots];
    std::uint32_t camera[slots];
    std::uint32_t registry[slots];
    std::uint32_t stamp[slots];
    std::uint8_t probe[slots];     // 1: admitted by the anchor probe (AnchorProbes), walked for anchors only
    std::uint16_t order[capacity]; // the slots in insertion order
    unsigned count = 0, overflow = 0;
    std::uint32_t generation = 1;
    RootSet() noexcept : root{}, camera_handle{}, camera{}, registry{}, stamp{}, probe{}, order{} {}
    void begin() noexcept {
        count = overflow = 0;
        if (++generation) return;
        for (unsigned i = 0; i < slots; ++i) stamp[i] = 0;
        generation = 1;
    }
    static unsigned hash(std::uint32_t r) noexcept { return unsigned((r * 0x9e3779b1u) >> 23); } // 9 bits: 0 .. 511
    // The slot of `r` this generation, or -1.
    int find(std::uint32_t r) const noexcept {
        if (!r) return -1;
        unsigned i = hash(r);
        for (unsigned n = 0; n < slots; ++n, i = (i + 1) & (slots - 1)) {
            if (stamp[i] != generation) return -1;
            if (root[i] == r) return int(i);
        }
        return -1;
    }
    // true when `r` is new this generation (inserted), false when present already or when the set is full (overflow).
    // `probed`: admitted by the anchor probe (the walk emits anchors only, plumes-no-glow-hulls.md step 1).
    bool insert(std::uint32_t r, std::uint32_t handle, std::uint32_t cam, std::uint32_t reg, bool probed = false) noexcept {
        if (!r) return false;
        unsigned i = hash(r);
        for (unsigned n = 0; n < slots; ++n, i = (i + 1) & (slots - 1)) {
            if (stamp[i] == generation) {
                if (root[i] == r) return false;
                continue;
            }
            if (count >= capacity) {
                ++overflow;
                return false;
            }
            stamp[i] = generation;
            root[i] = r;
            camera_handle[i] = handle;
            camera[i] = cam;
            registry[i] = reg;
            probe[i] = probed ? 1u : 0u;
            order[count++] = std::uint16_t(i);
            return true;
        }
        ++overflow; // unreachable: the set is never more than half full
        return false;
    }
};

// The walk on Farnham's Legacy (docs/architecture/fl-nozzle-walk-activation.md, option (b) ratified 2026-11-03). Two
// rules the AP route does not apply, both used only by the FL build:
//
// The owned context scale. The gather owns each newly admitted root's scale while the engine is still submitting the
// hull draw that named it (sample_scope inside the FL material scope): the draw's camera +0x1c is the view's scene
// context (X3FL.exe 0x48ba48 `mov ecx,[esi+0x1c]`, the sector scene's camera ESI), whose +0x2c is the float the world
// matrix scales positions by (0x4def18 `mulss xmm1,[esi+0x2c]`). Two bounded reads per camera, memoised over the
// frame's four most recent cameras; the float bits go into the slot's entry (indexed like RootSet's slots) and the
// append bit-casts them, so it never dereferences a camera or a context later. 0 (either read failed, a null context)
// makes the append count no_scale for that root, as before.
constexpr unsigned camera_context_offset = fj::view_context_offset, context_scale_offset = fj::context_scale_offset;
struct OwnedScales {
    static constexpr unsigned cameras = 4;
    std::uint32_t bits[RootSet::slots];
    std::uint32_t camera[cameras];
    std::uint32_t camera_bits[cameras];
    std::uint64_t frame = ~std::uint64_t(0);
    unsigned next = 0;
    unsigned chains = 0; // camera -> context -> scale chains read (memo misses), never cleared here
    OwnedScales() noexcept : bits{}, camera{}, camera_bits{} {}
    // The scale bits of `cam`'s context this frame: the memo, else camera +0x1c then context +0x2c through `read`.
    std::uint32_t of(std::uint32_t cam, std::uint64_t now, Reader read, void* context) noexcept {
        if (!cam) return 0;
        if (now != frame) {
            for (unsigned i = 0; i < cameras; ++i) camera[i] = 0;
            frame = now;
            next = 0;
        } else
            for (unsigned i = 0; i < cameras; ++i)
                if (camera[i] == cam) return camera_bits[i];
        ++chains;
        std::uint32_t scene = 0, value = 0;
        if (!read(context, cam + camera_context_offset, &scene, 4) || !scene ||
            !read(context, scene + context_scale_offset, &value, 4))
            value = 0;
        camera[next] = cam;
        camera_bits[next] = value;
        next = (next + 1) % cameras;
        return value;
    }
    // At the gather, for the RootSet slot just admitted.
    void own(unsigned slot, std::uint32_t cam, std::uint64_t now, Reader read, void* context) noexcept {
        if (slot < RootSet::slots) bits[slot] = of(cam, now, read, context);
    }
    // At the append: the owned value (0.f when none; the append counts no_scale, far_record refuses it).
    float scale(unsigned slot) const noexcept {
        float f = 0.f;
        if (slot < RootSet::slots) std::memcpy(&f, &bits[slot], sizeof f);
        return f;
    }
};
// The boundary. On FL the append walks only inside the frame's pre-compositor scene end (scene_end_hook ->
// resolve_hdr(SceneEndSource::Hook) -> the temporal stage -> run_engine_plumes; FL site 0x48b929), the synchronous
// native borrow under which the accepted main scene's roots are still submitted and unmutated
// (docs/reverse-engineering/fl/nozzle-scene-window.md). The StretchRect resolve (the bloom copy, after the compositor
// ran) and the next frame's Present-time light boundary (engine_light_frame) read nothing: node_no_boundary.
enum class Boundary : std::uint8_t { hook = 0, stretch_rect = 1, present = 2 };
inline const char* boundary_name(Boundary b) noexcept {
    return b == Boundary::hook ? "hook" : b == Boundary::stretch_rect ? "stretchrect" : "present";
}
inline bool walk_admitted(bool hook_only, Boundary b) noexcept {
    return !hook_only || b == Boundary::hook;
}
// The FL walk's report window (engine_node_walk_window, one row per `frames` frames of appends, written at any log
// tier): the walked appends' times and the window's counts. At most one append per frame, so `capacity` samples hold a
// whole window; quantiles() sorts them in place (insertion sort, at most 300 values once per window) and returns the
// nearest-rank p50 and p99 and the maximum.
struct WalkWindow {
    static constexpr unsigned capacity = 300;
    static constexpr std::uint64_t frames = 300;
    float us[capacity];
    unsigned count = 0;
    std::uint64_t start = 0;
    unsigned appends = 0, walked = 0, refused = 0, roots_max = 0, unreadable = 0, no_scale = 0;
    WalkWindow() noexcept : us{} {}
    void reset(std::uint64_t now) noexcept {
        count = appends = walked = refused = roots_max = unreadable = no_scale = 0;
        start = now;
    }
    void add(float v) noexcept {
        if (count < capacity) us[count++] = v;
    }
    void quantiles(float* p50, float* p99, float* max) noexcept {
        for (unsigned i = 1; i < count; ++i) {
            const float v = us[i];
            unsigned j = i;
            for (; j > 0 && us[j - 1] > v; --j) us[j] = us[j - 1];
            us[j] = v;
        }
        const auto rank = [&](unsigned percent) { return count ? us[(count * percent + 99) / 100 - 1] : 0.f; };
        *p50 = rank(50);
        *p99 = rank(99);
        *max = count ? us[count - 1] : 0.f;
    }
};
// The anchor probe clock's step (AnchorProbes::retry_ms is game time): the plume stage's wall step in seconds x the
// SETA rate, in game ms; 0 for a non-positive or NaN step, capped at 1 s as the engine caps its own step.
inline double probe_step_ms(double step_seconds, float seta_rate) noexcept {
    double ms = step_seconds * 1000. * double(seta_rate);
    if (!(ms > 0.)) ms = 0.;
    return ms < 1000. ? ms : 1000.;
}

// The ship roots (engine-nozzle-source.md "Implementation", review S1): the gather admits a root only when it is a
// ship, i.e. a root some engine record (draw, far or node) named as its parent within the last `recent_window` frames,
// or the own ship's root. Stations, asteroids and debris never enter the RootSet, so no non-ship root can displace a
// ship there and no list of theirs is read. A ship whose first visible frame has every nozzle off screen is not walked
// until a nozzle draws once (it has no engine light then either). Open addressing with bounded probes and no empties:
// mark() updates the root's stamp or takes the stalest of the probed slots (a root unseen for the longest), so the
// table never fills with history. 512 slots, 6 KB.
struct ShipRoots {
    static constexpr unsigned slots = 512, probes = 8;
    static_assert((slots & (slots - 1)) == 0, "a power of two");
    static constexpr std::uint64_t recent_window = 2; // frames: the previous ring's marks, plus one without a stage
    std::uint32_t root[slots];
    std::uint64_t stamp[slots]; // the frame of the last record naming the root, plus one; 0 empty
    ShipRoots() noexcept : root{}, stamp{} {}
    static unsigned hash(std::uint32_t r) noexcept { return unsigned((r * 0x85ebca6bu) >> 23); }
    void mark(std::uint32_t r, std::uint64_t frame) noexcept {
        if (!r) return;
        unsigned i = hash(r), oldest = i;
        for (unsigned n = 0; n < probes; ++n, i = (i + 1) & (slots - 1)) {
            if (!stamp[i] || root[i] == r) {
                root[i] = r;
                stamp[i] = frame + 1;
                return;
            }
            if (stamp[i] < stamp[oldest]) oldest = i;
        }
        root[oldest] = r;
        stamp[oldest] = frame + 1;
    }
    bool recent(std::uint32_t r, std::uint64_t frame) const noexcept {
        if (!r) return false;
        unsigned i = hash(r);
        for (unsigned n = 0; n < probes; ++n, i = (i + 1) & (slots - 1)) {
            if (!stamp[i]) return false;
            if (root[i] == r) return stamp[i] + recent_window > frame;
        }
        return false;
    }
};

// Hidden-emitter anchors (docs/architecture/plumes-no-glow-hulls.md, step 1). A hull without a glow part has no draw,
// far or node record; the M6M family and the drones carry hidden `effects\engines\fx_engine_emitter` JET children instead
// (C = 0 or 1: main, never visible, no steering bits; the drive hides them and still drives +0x88 toward its base, AP
// 0x0045af0a..0x0045b09a, FL 0x46484b..0x4649be). A complete walk of a root that found no glow part keeps those
// children as anchors. The mode word C is node +0x260 on both editions (the drive's read, AP 0x0045ad85, FL 0x4646b6),
// outside the node_bytes block: one 4-byte read per hidden JET child until the root shows a glow part. Glow = a JET
// child the drive shows: a hidden one whose C has the visible bit (the main set while a travel-mode swap shows the
// alternate one), or a child shown now whose body is not a steering body (RCS v/00566 or a SMALLJET entry; a brake jet
// shown while braking counts as glow). An anchor also needs the emitter body identity (the caller's predicate over the
// model id), unit x/y scales and a throttle the drive has reached (the main target range 0x4000 .. 0x20000). That guard
// is the rule's ship-only gate: placement sets every object's JET children to (1, 1, 0x28f) and hidden (AP 0x451cd5,
// FL 0x45b830, no class test) and only the class-7 branch of the sector update drives them (AP jump table 0x45b634 /
// 0x45b64c, FL 0x464fa8 / 0x464fc0: class 7 alone reaches the drive), so a station's, missile's or debris' hidden
// emitter stays at 0x28f and never anchors. The walk has no object pointer, so the class word obj+0x48 is not read.
constexpr unsigned mode_offset = 0x260;
constexpr std::uint32_t mode_visible = 0x1000u, mode_not_main = 0xffeu;
constexpr std::uint32_t anchor_z_min = 0x4000u, anchor_z_max = 0x20000u;
constexpr unsigned anchor_capacity = 8; // anchors kept per root (stock maximum 4); more count anchor_overflow
static_assert(mode_offset >= node_bytes, "C lies outside the node block (its own 4-byte read)");
// The record flag of an anchor (engine_effects_core.h RecordFlag: bits 0..10 taken, 12..15 the cluster).
constexpr std::uint16_t flag_anchor = 1u << 11;
static_assert(flag_anchor > x3m::engine_effects::core::flag_node &&
                  flag_anchor < (1u << x3m::engine_effects::core::cluster_shift),
              "flag_anchor sits between the last RecordFlag and the cluster bits");

// An anchor's record from the emitter's far_record (its origin = +0xb0 x the context scale, axis = -basis z, z and s
// from its own +0x88, the drive's throttle; serial and identity as a node record): size = k x R, the plume floor's
// ratio k(R) at the ship radius R in record units (engine_plumes_core.h floor_ratio_at) without the floor's cap, since
// the emitter body's value (17) means nothing; body -1 in the cyan cluster of the stock engines; flag_far and
// flag_unknown_body cleared, flag_node and flag_anchor set. false (no record) when k x R is not a positive finite size
// (the floor off, or the radius unknown).
inline bool anchor_record(float k, float radius, x3m::engine_effects::core::Record* r) noexcept {
    namespace ee = x3m::engine_effects::core;
    const float size = k * radius;
    if (!(size > 0.f) || !ee::finite_f(size)) return false;
    constexpr std::uint16_t dropped = std::uint16_t(ee::flag_far | ee::flag_unknown_body | (15u << ee::cluster_shift));
    r->size = size;
    r->body = -1;
    r->flags = std::uint16_t((r->flags & ~dropped) | ee::flag_node | flag_anchor | (unsigned(ee::cyan) << ee::cluster_shift));
    return true;
}

// The walk's options. `anchors`: keep hidden emitters as anchors (the rule above); `jets`: emit the ordinary main jets
// (false for a root the anchor probe admitted: anchors only); `model_offset`: the node's model id (the build's
// engine_node_layout: AP +0x140, FL +0x144). The two predicates take the model id: the emitter body, a steering body.
struct Options {
    bool anchors = false;
    bool jets = true;
    unsigned model_offset = fj::model_offset;
    bool (*is_emitter)(void* context, std::uint32_t model) = nullptr;
    bool (*is_steering)(void* context, std::uint32_t model) = nullptr;
    void* context = nullptr;
};

// The anchor probe (step 1's admission): a root no record named within ShipRoots::recent_window frames is walked for
// anchors when it has no entry here, its last walk anchored, or its last walk is older than retry_ms of game time (the plume stage's
// SETA-scaled step, summed by the caller) or retry_frames frames (a clock that does not advance). An anchored root is
// kept recent by its own records' marks, so the probe matters at first sight and after a root's address is reused.
// The verdict keeps the last walk's outcome (the newly-admitted row). Open addressing, bounded probes, the stalest of a
// probe run replaced; clear() on a load. 512 slots, 12 KB.
struct AnchorProbes {
    static constexpr unsigned slots = 512, probes = 8;
    static constexpr double retry_ms = 2000.;
    static constexpr std::uint64_t retry_frames = 600;
    enum Verdict : std::uint8_t { none = 0, anchored = 1, glow = 2 };
    std::uint32_t root[slots];
    std::uint64_t frame[slots]; // the frame of the last walk, plus one; 0 empty
    double clock[slots];        // the probe clock (game ms) at the last walk
    std::uint8_t verdict[slots];
    AnchorProbes() noexcept : root{}, frame{}, clock{}, verdict{} {}
    static unsigned hash(std::uint32_t r) noexcept { return unsigned((r * 0xc2b2ae35u) >> 23); }
    void clear() noexcept {
        for (unsigned i = 0; i < slots; ++i) frame[i] = 0;
    }
    int find(std::uint32_t r) const noexcept {
        if (!r) return -1;
        unsigned i = hash(r);
        for (unsigned n = 0; n < probes; ++n, i = (i + 1) & (slots - 1)) {
            if (!frame[i]) return -1;
            if (root[i] == r) return int(i);
        }
        return -1;
    }
    // One probe per non-recent routed draw: whether `r` is due for an anchor walk. An anchored root is always due: a
    // frame without its anchor records (hull not drawn, ring full, no camera scale, a view switch) lets its recent
    // window lapse, and it is walked again at its next draw, not after retry_ms (the per-frame probe cap bounds it).
    bool due(std::uint32_t r, std::uint64_t now_frame, double now_ms) const noexcept {
        if (!r) return false;
        const int i = find(r);
        if (i < 0 || verdict[i] == anchored) return true;
        return now_ms - clock[i] >= retry_ms || now_frame + 1 >= frame[i] + retry_frames;
    }
    // After a walk of `r`: records it and returns the previous verdict (none for a new entry).
    Verdict note(std::uint32_t r, std::uint64_t now_frame, double now_ms, Verdict v) noexcept {
        if (!r) return none;
        unsigned i = hash(r), oldest = i;
        for (unsigned n = 0; n < probes; ++n, i = (i + 1) & (slots - 1)) {
            if (!frame[i] || root[i] == r) {
                const Verdict previous = frame[i] && root[i] == r ? Verdict(verdict[i]) : none;
                root[i] = r;
                frame[i] = now_frame + 1;
                clock[i] = now_ms;
                verdict[i] = v;
                return previous;
            }
            if (frame[i] < frame[oldest]) oldest = i;
        }
        root[oldest] = r;
        frame[oldest] = now_frame + 1;
        clock[oldest] = now_ms;
        verdict[oldest] = v;
        return none;
    }
};

// One root's walk: the counts.
struct WalkStats {
    unsigned children = 0;       // list elements read (the sentinel excluded)
    unsigned jets = 0;           // elements with the JET pair, before the guards
    unsigned hidden = 0;         // jets the drive hides (+0x12c & 0x100000): no record
    unsigned guard_parent = 0;   // jets whose +0x18 is not the root
    unsigned guard_scale = 0;    // jets whose +0x80 / +0x84 are not 1.0
    unsigned guard_throttle = 0; // jets whose +0x88 is outside 0x147 .. 0x90000
    unsigned unreadable = 0;     // the list was cut by an unreadable element (1 at most per root)
    unsigned overflow = 0;       // the list was cut at walk_bound (1 at most per root)
    unsigned emitted = 0;        // jets handed to the caller
    // With Options::anchors (all 0 otherwise):
    unsigned mode_reads = 0;      // C words read (hidden JET children before the first glow part)
    unsigned mode_unreadable = 0; // C words that could not be read: that child is no anchor
    unsigned candidates = 0;      // hidden children whose C has neither the visible bit nor a bit in 0xffe
    unsigned not_emitter = 0;     // candidates whose model is not the emitter body
    unsigned anchor_rejected = 0; // emitters failing the unit-scale or driven-throttle guard
    unsigned anchor_overflow = 0; // emitters beyond anchor_capacity
    unsigned glow = 0;            // 1: the root has a glow part (anchors withheld)
    unsigned anchors = 0;         // anchors handed to the caller (after the list's end)
};
namespace detail {
inline void fill_raw(const std::uint32_t* block, std::uint32_t here, std::uint32_t root, std::uint32_t view_handle,
                     unsigned model_offset, fj::Raw* raw) noexcept {
    *raw = fj::Raw{};
    raw->node = here;
    raw->view_handle = view_handle;
    raw->context = 0;
    raw->parent = root;
    raw->handle = block[handle_offset / 4];
    raw->model = block[model_offset / 4];
    raw->scale70 = block[scale70_offset / 4];
    raw->scale80 = block[scale80_offset / 4];
    raw->scale88 = block[scale88_offset / 4];
    for (unsigned i = 0; i < 3; ++i) {
        raw->position[i] = std::int32_t(block[position_offset / 4 + i]);
        raw->basis_x[i] = std::int32_t(block[basis_x_offset / 4 + i]);
        raw->basis_z[i] = std::int32_t(block[basis_z_offset / 4 + i]);
    }
}
} // namespace detail
// Walks `root`'s child list through `read`, filling a Raw per main jet that passes the guards (node, parent = root,
// handle, model, scales, position, basis rows x and z; view_handle = `view_handle`, context 0) and calling
// emit(const Raw&). Returns true when the list's end was reached (the sentinel) within the bound and every element
// read: the walk is then authoritative for the root (a ship with no emitted jet has no live nozzle). Bounded reads
// per element: its next pointer (the sentinel is told by it alone, so no other read crosses its end), its flag pair
// at +0x12c (a part without the JET pair ends there: two cache lines, the common case on a ship of tens of parts) and,
// for a jet, its first node_bytes (the host harness: the cost is cache misses, about 6 lines per block). With
// opt.anchors also C of a hidden jet (above) and, after a complete walk of a root without a glow part, anchor(const
// Raw&) per kept emitter in list order; with !opt.jets no jet is emitted. Default options: the walk before step 1,
// read for read.
template <class Emit, class Anchor>
inline bool walk(std::uint32_t root, std::uint32_t view_handle, Reader read, void* context, WalkStats* st,
                 const Options& opt, Emit&& emit, Anchor&& anchor) noexcept {
    *st = WalkStats{};
    const unsigned model_offset = opt.model_offset;
    if (model_offset + 4 > node_bytes || (model_offset & 3u)) {
        st->unreadable = 1;
        return false;
    }
    std::uint32_t child = 0;
    if (!root || !read(context, root + child_offset, &child, 4)) {
        st->unreadable = 1;
        return false;
    }
    const bool anchors = opt.anchors && opt.is_emitter && opt.is_steering;
    std::uint32_t block[node_bytes / 4];
    fj::Raw kept[anchor_capacity];
    unsigned kept_count = 0;
    bool glow = false;
    for (unsigned n = 0;; ++n) {
        if (!child) break; // an empty list: the root's +0xc is the sentinel's address, never 0 in the game
        std::uint32_t next = 0;
        if (!read(context, child + next_offset, &next, 4)) {
            st->unreadable = 1;
            return false;
        }
        if (!next) break; // the sentinel
        if (n >= walk_bound) {
            st->overflow = 1;
            return false;
        }
        ++st->children;
        const std::uint32_t here = child;
        child = next;
        std::uint32_t pair[2];
        if (!read(context, here + flags12c_offset, pair, 8)) {
            st->unreadable = 1;
            return false;
        }
        if ((pair[1] & jet_pair) != jet_pair) continue;
        if (!read(context, here, block, node_bytes)) {
            st->unreadable = 1;
            return false;
        }
        ++st->jets;
        if (block[parent_offset / 4] != root) {
            ++st->guard_parent;
            continue;
        }
        if (block[flags12c_offset / 4] & hidden_flag) {
            ++st->hidden;
            if (!anchors || glow) continue;
            std::uint32_t mode = 0;
            ++st->mode_reads;
            if (!read(context, here + mode_offset, &mode, 4)) {
                ++st->mode_unreadable;
                continue;
            }
            if (mode & mode_visible) {
                glow = true;
                continue;
            }
            if (mode & mode_not_main) continue;
            ++st->candidates;
            if (!opt.is_emitter(opt.context, block[model_offset / 4])) {
                ++st->not_emitter;
                continue;
            }
            const std::uint32_t z = block[scale88_offset / 4];
            if (block[scale80_offset / 4] != scale_one || block[scale84_offset / 4] != scale_one || z < anchor_z_min ||
                z > anchor_z_max) {
                ++st->anchor_rejected;
                continue;
            }
            if (kept_count >= anchor_capacity) {
                ++st->anchor_overflow;
                continue;
            }
            detail::fill_raw(block, here, root, view_handle, model_offset, &kept[kept_count++]);
            continue;
        }
        if (anchors && !glow && !opt.is_steering(opt.context, block[model_offset / 4])) glow = true;
        if (block[scale80_offset / 4] != scale_one || block[scale84_offset / 4] != scale_one) {
            ++st->guard_scale;
            continue;
        }
        const std::uint32_t z = block[scale88_offset / 4];
        if (z < scale88_min || z > scale88_max) {
            ++st->guard_throttle;
            continue;
        }
        if (!opt.jets) continue;
        fj::Raw raw;
        detail::fill_raw(block, here, root, view_handle, model_offset, &raw);
        ++st->emitted;
        emit(raw);
    }
    // The list's end: the walk is complete. Anchors only when no part of the root is a glow part.
    if (anchors) {
        st->glow = glow ? 1u : 0u;
        if (!glow)
            for (unsigned i = 0; i < kept_count; ++i) {
                ++st->anchors;
                anchor(static_cast<const fj::Raw&>(kept[i]));
            }
    }
    return true;
}
// The walk before step 1 (default Options: AP's model offset, every main jet emitted, no anchor).
template <class Emit>
inline bool walk(std::uint32_t root, std::uint32_t view_handle, Reader read, void* context, WalkStats* st,
                 Emit&& emit) noexcept {
    return walk(root, view_handle, read, context, st, Options{}, static_cast<Emit&&>(emit), [](const fj::Raw&) {});
}
} // namespace x3m::engine_nozzle::core
