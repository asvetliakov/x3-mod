#pragma once
#include "weapon_effects_core.h"

// Step 4, the bolts and the muzzle: the game-thread half (docs/architecture/bolt-muzzle-look.md, weapon-look.md 3.3; the
// sites in weapon_effects_bolt_sites.h). Integer only: compiled into weapon_effects_hook.cpp with -mno-sse -mfpmath=387
// and holding no floating point; no call, no allocation, no Win32 or CRT. Portable (the host tests and both fixtures
// compile it).
//
// - The per-bullet tap (0x0045ac7d -> 0x00414590(obj), every live object of the sector once per update pass): a class-0
//   object (word obj+0x48 == 0) with a live node is recorded into the frame's bolt list (BoltList: 512 records of 64 B,
//   double-buffered by BoltHookState::generation, which Present advances once per frame): the object, its node, row
//   obj+0x4a, node +0x30 (position), +0x100 (velocity), the rotation's z row (node +0x60), the extension's expiry ext+0,
//   owner ext+4, creation ext+8 and length ext+0x78. An object seen again in the same frame (a second update pass) keeps
//   its record, refreshed (a 1024-slot generation-stamped table keyed by the object, a replaced record's key left as a
//   tombstone, at most bolt::key_probes probes). Past 512 a far record goes for a nearer newcomer in O(1): each
//   overflowing call examines the next bolt::far_window records from the list's ring cursor and keeps the farthest
//   non-own one (from the published eye, BoltHookState::eye, engine units) as the candidate; a newcomer nearer than it
//   (or the own ship's, own_owner) replaces it, else the newcomer is dropped (counted). Always forwards.
// - The muzzle (0x00446268 -> 0x00413c00(ship, slot, &pos), EAX the bullet index, EDX &orientation): one record per
//   (ship, gun slot), re-lit by every shot (t0 and the seed reset, the shot count advanced, the previous t0 kept for the
//   breathing law; a changed ship handle starts the record over); 128 records, the oldest replaced. Returns 1 (skip the
//   native muzzle effect) while muzzle_owned (Present's muzzle latch: the stage drew every muzzle it owned in the
//   previous frame with room to spare, the bolts armed) and suppress_muzzle are set, and marks the shot ours (the stage
//   builds only those), else 0 (forward, the shot native: the stage does not build it). Never two flashes, never none.
// - The flight effect (0x004146c0 -> 0x004148a0, a bullet's col 19 sprite, which would double the capsule): skipped
//   (1) while the in-place draw owned the previous frame, else forwarded.
namespace x3m::weapon_effects::core {
struct BoltRecord {
    std::uint32_t obj, node;
    std::int32_t pos[3];     // node +0x30 (engine units, world)
    std::int32_t vel[3];     // node +0x100 (the velocity vector; its direction is the travel, its unit unknown 1)
    std::int32_t zrow[3];    // node +0x40 row 2 (16.16): the travel's fallback (unknown 2)
    std::uint32_t created;   // ext+8 (game ms)
    std::uint32_t expiry;    // ext+0 (game ms)
    std::uint32_t owner;     // ext+4 (the shooter's object; compared, never dereferenced here)
    std::int32_t length;     // ext+0x78 (a beam's or fixed-length bullet's length, engine units [i])
    std::uint16_t row;       // obj+0x4a (the TBullets index)
    std::uint8_t own, pass;  // the own ship's (owner == own_owner); the update pass that wrote it last
};
static_assert(sizeof(BoltRecord) == 64, "a bolt record is 64 bytes (32 KB a list)");
struct BoltList {
    std::uint32_t generation = 0; // the BoltHookState::generation it was written in (a stale list reads as empty)
    std::uint32_t count = 0;
    std::uint32_t far_index = 0;  // the replacement candidate while full (0xffffffff: none)
    std::uint32_t cursor = 0;     // the ring cursor of the candidate's search
    std::uint64_t far_d2 = 0;
    BoltRecord records[bolt::list_capacity]{};
    std::uint16_t key_slot[bolt::list_capacity]{}; // each record's slot in the object table (its tombstone on replace)
};
struct MuzzleRecord {
    std::uint32_t ship, node, handle; // the shooter, its root node and node+0x28 (the stage validates them)
    std::uint32_t t0, prev_t0;        // game ms of this shot and of the slot's previous one (0: none)
    std::uint32_t shots, seed;        // shots on this record; the per-shot seed (hash of ship, slot and shots)
    std::int32_t pos[4];              // the fire routine's &pos (16 bytes, ship-relative [i])
    std::int32_t orient[12];          // its &orientation (3 rows of 4 dwords, 16.16 [i], unknown 6)
    std::uint16_t row, slot;          // the TBullets index (EAX), the gun slot k
    std::uint32_t sequence;           // monotonic, 0 = free
    std::uint8_t ours, pad[3];        // this shot skipped the native muzzle (the stage draws it), else native (not built)
};
struct BoltCounters {
    std::uint32_t tap_calls = 0, tap_bullets = 0, tap_no_node = 0, tap_overflow = 0, tap_replaced = 0, tap_refreshed = 0;
    std::uint32_t flight_calls = 0, flight_skipped = 0;
    std::uint32_t muzzle_calls = 0, muzzle_skipped = 0, muzzle_relit = 0, muzzle_evicted = 0, muzzle_restarted = 0;
    // the shooters' object class (weapon-look unknown 13: do stations and platforms fire through the site?)
    std::uint32_t shooter_ship = 0, shooter_dock = 0, shooter_factory = 0, shooter_other = 0;
    std::uint32_t tap_tombstoned = 0, tap_unkeyed = 0;
};
struct BoltHookState {
    std::uint8_t armed = 0;            // the bolts family installed (shields,hull,bolts)
    volatile std::uint8_t owned = 0;   // Present's latch: the in-place draw drew the previous frame (the flight skip)
    volatile std::uint8_t muzzle_owned = 0; // Present's muzzle latch (the muzzle skip; independent of owned)
    std::uint8_t suppress_muzzle = 1, suppress_flight = 1; // the native muzzle and flight effects skip while latched
    std::uint32_t generation = 1;      // Present advances it once per frame: lists[generation & 1] is this frame's
    std::uint32_t pass = 0;            // update passes seen this generation (diagnostics)
    std::uint32_t own_owner = 0;       // the own ship's object (the stage publishes it; 0 unknown)
    std::int32_t eye[3]{};             // the eye in engine units (the stage publishes it; the far-drop order)
    std::uint32_t muzzle_sequence = 0;
    std::uint16_t shooter_last_other = 0; // the last shooter class outside 5 / 6 / 7
    Globals globals{};
    BoltCounters counters{};
    // The object -> record index table of the generation (1024 slots, at most 512 used: half full).
    std::uint32_t key_obj[2 * bolt::list_capacity]{};
    std::uint32_t key_gen[2 * bolt::list_capacity]{};
    std::uint16_t key_index[2 * bolt::list_capacity]{};
    BoltList lists[2]{};
    MuzzleRecord muzzles[muzzle::record_capacity]{};
};
namespace bolt_layout {
constexpr unsigned node_velocity = 0x100, node_zrow = 0x60; // node +0x40 + row 2 (16 bytes a row)
constexpr unsigned ext_expiry = 0, ext_owner = 4, ext_created = 8, ext_length = 0x78;
}
// The list of the current generation (written by the tap this frame), and the previous frame's.
inline const BoltList& bolt_list_current(const BoltHookState& b) noexcept {
    return b.lists[b.generation & 1u];
}
inline const BoltList& bolt_list_previous(const BoltHookState& b) noexcept {
    return b.lists[(b.generation - 1u) & 1u];
}
inline std::uint32_t bolt_list_count(const BoltHookState& b, const BoltList& l) noexcept {
    return (l.generation == b.generation || l.generation + 1u == b.generation) ? l.count : 0u;
}
// The squared distance from the eye, both engine units, each difference shifted down by 8 so the product stays in 32 x
// 32 -> 64 bit multiplies (no __muldi3 call on i686).
static inline __attribute__((always_inline)) std::uint64_t eye_d2(const std::int32_t* p, const std::int32_t* eye) noexcept {
    std::uint64_t s = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int64_t d = (std::int64_t(p[i]) - std::int64_t(eye[i])) >> 8; // |d| < 2^24
        const std::int32_t d32 = std::int32_t(d);
        s += std::uint64_t(std::int64_t(d32) * std::int64_t(d32));
    }
    return s;
}
// The replacement candidate's O(1) step: the next far_window records from the ring cursor, the farthest non-own of them
// and the standing candidate kept.
static inline __attribute__((always_inline)) void bolt_far_step(BoltList& l, const std::int32_t* eye) noexcept {
    for (unsigned k = 0; k < bolt::far_window; ++k) {
        const std::uint32_t i = l.cursor;
        l.cursor = (l.cursor + 1u) & (bolt::list_capacity - 1u);
        if (i >= l.count || l.records[i].own || i == l.far_index) continue;
        const std::uint64_t d = eye_d2(l.records[i].pos, eye);
        if (l.far_index == 0xffffffffu || d > l.far_d2) {
            l.far_d2 = d;
            l.far_index = i;
        }
    }
}
static_assert((bolt::list_capacity & (bolt::list_capacity - 1u)) == 0, "the ring cursor wraps by mask");
static inline __attribute__((always_inline)) void bolt_fill(BoltRecord& r, std::uintptr_t obj, std::uintptr_t node,
                                                            std::uint32_t owner, std::uint32_t own_owner,
                                                            std::uint32_t pass) noexcept {
    const std::uintptr_t ext = rd32(obj + layout::obj_ext);
    r.obj = std::uint32_t(obj);
    r.node = std::uint32_t(node);
    read3(node + layout::node_pos, r.pos);
    read3(node + bolt_layout::node_velocity, r.vel);
    read3(node + bolt_layout::node_zrow, r.zrow);
    r.expiry = ext ? rd32(ext + bolt_layout::ext_expiry) : 0;
    r.owner = owner;
    r.created = ext ? rd32(ext + bolt_layout::ext_created) : 0;
    r.length = ext ? std::int32_t(rd32(ext + bolt_layout::ext_length)) : 0;
    r.row = rd16(obj + layout::obj_subtype);
    r.own = std::uint8_t(owner && owner == own_owner);
    r.pass = std::uint8_t(pass);
}
// The tap's body: [esp+4] = the object. Integer only, plain loads, no call.
static inline __attribute__((always_inline)) void bolt_tap(const std::uint32_t* esp, BoltHookState& b) noexcept {
    ++b.counters.tap_calls;
    if (!b.armed) return;
    const std::uintptr_t obj = esp[1];
    if (!obj || rd16(obj + layout::obj_class) != layout::class_bullet) return;
    ++b.counters.tap_bullets;
    const std::uintptr_t node = live_node(obj);
    if (!node) {
        ++b.counters.tap_no_node;
        return;
    }
    BoltList& l = b.lists[b.generation & 1u];
    if (l.generation != b.generation) { // the first record of this frame: the list starts over
        l.generation = b.generation;
        l.count = 0;
        l.far_index = 0xffffffffu;
        l.cursor = 0;
        b.pass = 0;
    }
    // The object's record of this generation (a second update pass refreshes it in place). A replaced record's key is
    // left as a tombstone (key_obj 0, still stamped: the probe chains stay whole) that the next insertion on its chain
    // reuses; at most bolt::key_probes probes (a saturated chain records the bullet without a key).
    constexpr unsigned size = 2 * bolt::list_capacity;
    unsigned slot = size, tomb = size;
    for (unsigned probe = 0, h = unsigned(std::uint32_t(obj) * 0x9e3779b1u >> 22) & (size - 1u); probe < bolt::key_probes;
         ++probe, h = (h + 1u) & (size - 1u)) {
        if (b.key_gen[h] != b.generation || b.key_obj[h] == std::uint32_t(obj)) {
            slot = h;
            break;
        }
        if (!b.key_obj[h] && tomb == size) tomb = h;
    }
    const std::uint32_t ext = rd32(obj + layout::obj_ext);
    const std::uint32_t owner = ext ? rd32(ext + bolt_layout::ext_owner) : 0u;
    if (slot < size && b.key_gen[slot] == b.generation && b.key_obj[slot] == std::uint32_t(obj)) {
        const unsigned i = b.key_index[slot];
        if (i < l.count && l.records[i].obj == std::uint32_t(obj)) {
            if (l.records[i].pass == std::uint8_t(b.pass)) ++b.pass; // the same object twice: a new update pass began
            bolt_fill(l.records[i], obj, node, owner, b.own_owner, b.pass);
            ++b.counters.tap_refreshed;
            return;
        }
    }
    if (tomb < size) slot = tomb;
    std::uint32_t at = l.count;
    if (at >= bolt::list_capacity) {
        ++b.counters.tap_overflow;
        // Full: the candidate (the farthest non-own of the cursor's windows) goes for a nearer newcomer; an own newcomer
        // always enters while there is a candidate.
        bolt_far_step(l, b.eye);
        std::int32_t pos[3];
        read3(node + layout::node_pos, pos);
        const bool own = owner && owner == b.own_owner;
        if (l.far_index == 0xffffffffu || (!own && eye_d2(pos, b.eye) >= l.far_d2)) return; // the newcomer is farther
        at = l.far_index;
        l.far_index = 0xffffffffu; // consumed: the next overflowing call searches again
        // the replaced record's key becomes a tombstone (its object is no longer listed)
        const unsigned old = l.key_slot[at];
        if (old < size && b.key_gen[old] == b.generation && b.key_obj[old] == l.records[at].obj &&
            b.key_index[old] == at) {
            b.key_obj[old] = 0;
            ++b.counters.tap_tombstoned;
        }
        ++b.counters.tap_replaced;
    } else
        ++l.count;
    bolt_fill(l.records[at], obj, node, owner, b.own_owner, b.pass);
    if (slot < size) {
        b.key_gen[slot] = b.generation;
        b.key_obj[slot] = std::uint32_t(obj);
        b.key_index[slot] = std::uint16_t(at);
        l.key_slot[at] = std::uint16_t(slot);
    } else {
        l.key_slot[at] = std::uint16_t(size);
        ++b.counters.tap_unkeyed;
    }
}
// The muzzle's body: [esp+4] ship, [esp+8] slot, [esp+12] &pos; eax the TBullets index, edx &orientation. Returns 1 to
// skip the native muzzle (0x00413c00 not called), 0 to forward.
static inline __attribute__((always_inline)) int bolt_muzzle(const std::uint32_t* esp, std::uint32_t eax, std::uint32_t edx,
                                                             BoltHookState& b) noexcept {
    ++b.counters.muzzle_calls;
    if (!b.armed) return 0;
    const std::uintptr_t ship = esp[1], pos = esp[3];
    const std::uint32_t slot = esp[2];
    const std::uintptr_t node = live_node(ship);
    if (!node || !pos || !edx) return 0;
    const std::uint16_t cls = rd16(ship + layout::obj_class);
    if (cls == layout::class_ship)
        ++b.counters.shooter_ship;
    else if (cls == layout::class_dock)
        ++b.counters.shooter_dock;
    else if (cls == layout::class_factory)
        ++b.counters.shooter_factory;
    else {
        ++b.counters.shooter_other;
        b.shooter_last_other = cls;
    }
    const std::uint32_t handle = rd32(node + layout::node_handle);
    const std::uintptr_t cfg = rd32(b.globals.cfg_slot);
    const std::uint32_t now = cfg ? rd32(cfg + layout::cfg_game_ms) : 0;
    MuzzleRecord* r = nullptr;
    MuzzleRecord* oldest = nullptr; // the first free record, else the one shot longest ago
    for (unsigned i = 0; i < muzzle::record_capacity; ++i) {
        MuzzleRecord& m = b.muzzles[i];
        if (!m.sequence) {
            if (!oldest || oldest->sequence) oldest = &m;
            continue;
        }
        if (m.ship == std::uint32_t(ship) && m.slot == std::uint16_t(slot)) {
            r = &m;
            break;
        }
        if (!oldest || (oldest->sequence && now - m.t0 > now - oldest->t0)) oldest = &m;
    }
    if (r && (r->handle != handle || r->node != std::uint32_t(node))) {
        // another ship at the same object (its handle changed): the record starts over (no breathing from its shots)
        ++b.counters.muzzle_restarted;
        r->prev_t0 = 0;
        r->shots = 1;
    } else if (r) {
        ++b.counters.muzzle_relit;
        r->prev_t0 = r->t0;
        ++r->shots;
    } else {
        r = oldest;
        if (r->sequence) ++b.counters.muzzle_evicted;
        r->ship = std::uint32_t(ship);
        r->slot = std::uint16_t(slot);
        r->prev_t0 = 0;
        r->shots = 1;
    }
    r->node = std::uint32_t(node);
    r->handle = handle;
    r->t0 = now;
    r->row = std::uint16_t(eax);
    r->seed = (std::uint32_t(ship) * 0x9e3779b1u) ^ (slot * 0x85ebca6bu) ^ (r->shots * 0xc2b2ae35u);
    for (unsigned i = 0; i < 4; ++i) r->pos[i] = std::int32_t(rd32(pos + 4 * i));
    for (unsigned i = 0; i < 12; ++i) r->orient[i] = std::int32_t(rd32(edx + 4 * i));
    r->sequence = ++b.muzzle_sequence ? b.muzzle_sequence : ++b.muzzle_sequence;
    const int skip = b.muzzle_owned && b.suppress_muzzle ? 1 : 0;
    r->ours = std::uint8_t(skip);
    if (skip) ++b.counters.muzzle_skipped;
    return skip;
}
// The flight effect's body: [esp+0x10] = the bullet. Returns 1 to skip 0x004148a0 (the col 19 sprite), 0 to forward.
static inline __attribute__((always_inline)) int bolt_flight(const std::uint32_t* esp, BoltHookState& b) noexcept {
    ++b.counters.flight_calls;
    if (!b.armed || !b.owned || !b.suppress_flight) return 0;
    const std::uintptr_t obj = esp[4];
    if (!obj || rd16(obj + layout::obj_class) != layout::class_bullet) return 0;
    ++b.counters.flight_skipped;
    return 1;
}
} // namespace x3m::weapon_effects::core
