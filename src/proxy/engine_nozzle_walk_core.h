#pragma once
#include <cstddef>
#include <cstdint>
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
    std::uint16_t order[capacity]; // the slots in insertion order
    unsigned count = 0, overflow = 0;
    std::uint32_t generation = 1;
    RootSet() noexcept : root{}, camera_handle{}, camera{}, registry{}, stamp{}, order{} {}
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
    bool insert(std::uint32_t r, std::uint32_t handle, std::uint32_t cam, std::uint32_t reg) noexcept {
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
            order[count++] = std::uint16_t(i);
            return true;
        }
        ++overflow; // unreachable: the set is never more than half full
        return false;
    }
};

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
};
// Walks `root`'s child list through `read`, filling a Raw per main jet that passes the guards (node, parent = root,
// handle, model, scales, position, basis rows x and z; view_handle = `view_handle`, context 0) and calling
// emit(const Raw&). Returns true when the list's end was reached (the sentinel) within the bound and every element
// read: the walk is then authoritative for the root (a ship with no emitted jet has no live nozzle). Bounded reads
// per element: its next pointer (the sentinel is told by it alone, so no other read crosses its end), its flag pair
// at +0x12c (a part without the JET pair ends there: two cache lines, the common case on a ship of tens of parts) and,
// for a jet, its first node_bytes (the host harness: the cost is cache misses, about 6 lines per block).
template <class Emit>
inline bool walk(std::uint32_t root, std::uint32_t view_handle, Reader read, void* context, WalkStats* st,
                 Emit&& emit) noexcept {
    *st = WalkStats{};
    std::uint32_t child = 0;
    if (!root || !read(context, root + child_offset, &child, 4)) {
        st->unreadable = 1;
        return false;
    }
    std::uint32_t block[node_bytes / 4];
    for (unsigned n = 0;; ++n) {
        if (!child) return true; // an empty list: the root's +0xc is the sentinel's address, never 0 in the game
        std::uint32_t next = 0;
        if (!read(context, child + next_offset, &next, 4)) {
            st->unreadable = 1;
            return false;
        }
        if (!next) return true; // the sentinel
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
            continue;
        }
        if (block[scale80_offset / 4] != scale_one || block[scale84_offset / 4] != scale_one) {
            ++st->guard_scale;
            continue;
        }
        const std::uint32_t z = block[scale88_offset / 4];
        if (z < scale88_min || z > scale88_max) {
            ++st->guard_throttle;
            continue;
        }
        fj::Raw raw{};
        raw.node = here;
        raw.view_handle = view_handle;
        raw.context = 0;
        raw.parent = root;
        raw.handle = block[handle_offset / 4];
        raw.model = block[model_offset / 4];
        raw.scale70 = block[scale70_offset / 4];
        raw.scale80 = block[scale80_offset / 4];
        raw.scale88 = z;
        for (unsigned i = 0; i < 3; ++i) {
            raw.position[i] = std::int32_t(block[position_offset / 4 + i]);
            raw.basis_x[i] = std::int32_t(block[basis_x_offset / 4 + i]);
            raw.basis_z[i] = std::int32_t(block[basis_z_offset / 4 + i]);
        }
        ++st->emitted;
        emit(raw);
    }
}
} // namespace x3m::engine_nozzle::core
