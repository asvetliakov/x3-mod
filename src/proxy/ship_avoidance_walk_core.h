#pragma once
#include "ship_avoidance_core.h"

// Portable core of the sector snapshot walk and the station part walk
// (docs/architecture/ship-steering-and-avoidance.md sections 3.2 and 3.5;
// facts in ship_steering_sites.h (f) and (g)). Templated on a memory reader
// (`u32`, `s32`, `u16` of an address) so that the DLL runs it over the engine's
// memory and the host fixture over a synthetic sector with the same bytes.
//
// Sector buckets [m]: `table = [sector+0x50]`, `head = [table + 0xc * k]`
// (0x00452b4d, 0x0045d275, 0x0045cbf4: two dereferences), each list linked at
// `[obj+0]` and ended by a node whose first dword is 0 (the loop runs while
// `[node] != 0`); the bucket index is the class word, so only the buckets of
// the obstacle classes are walked (5/6 stations, 7 ships, 17 asteroids, 18
// gates, 28 debris). Station children [m]: `c = [node+0xc]; while [c] != 0;
// c = [c]` (0x0048aa46, 0x00488248: the node whose first dword is 0 is the
// sentinel, not a part); a child's local offset `+0x30..` is scaled by the
// parent's 16.16 `+0x70` when `[child+0x12c] & 0x40` (0x004881a9..0x004881e7);
// the child's subtree radius is the engine getter 0x00488170's value: its cache
// `+0xa4` when filled (>= 0), else the getter's recursion replicated read-only
// below (fact (g), resolved 2026-10-12): the walk never calls the getter and
// never writes `+0xa4`.
namespace x3m::ship_avoidance::walk {
namespace av = x3m::ship_avoidance::core;
namespace sc = x3m::ship_steering::core;
constexpr unsigned kBuckets[6] = {5, 6, 7, 17, 18, 28};
constexpr unsigned kWalkLimit = 8192; // objects visited per snapshot at most: beyond it the snapshot is overflow (pass-through)
constexpr unsigned kChildLimit = 256; // nodes visited per station at most: beyond it the station uses the fallback body
constexpr unsigned kRadiusDepth = 3;  // the replicated getter recurses at most this deep below a part (deeper unfilled caches: fallback)
constexpr unsigned kRadiusBudget = 64; // and visits at most this many nodes per station
constexpr std::uint16_t kClassShip = 7, kClassStationA = 5, kClassStationB = 6, kClassAsteroid = 17, kClassGate = 18, kClassDebris = 28;
constexpr std::uint32_t kSectorBuckets = 0x50, kBucketStride = 0xc;
constexpr std::uint32_t kObjFlags = 0x40, kObjFlags2 = 0x44, kObjClass = 0x48, kObjFlight = 0x50, kObjPhys = 0x70, kObjRadius = 0xa4,
                        kObjDocked = 0xf8;
constexpr std::uint32_t kFlagDeleted = 0x8000000, kFlagAttached = 0x200, kFlags2NoPair = 0x20000;
constexpr std::uint32_t kFlagPlayerShip = 0x2; // [obj+0x40]: SA_SetAsPlayerObject (ship_steering_sites.h kFlagPlayerObject)
constexpr std::uint32_t kFlightTarget = 0x224, kFlightOrder = 0x258;
constexpr std::uint32_t kObjId = 0x08; // the engine's native object id (SA_GetEventObject 0x004607f4 resolves it through the registry)
constexpr std::uint32_t kObjType = 0x4a, kShipTypeTableSlot = 0x00606fd4, kRecordStride = 0xdb8, kRecordClassCode = 0x14;
constexpr std::uint32_t kPhysPosition = 0x30, kPhysBasis = 0x40, kPhysVelocity = 0x100;
constexpr std::uint32_t kObjSlip = 0x60; // the integrator's slip velocity (ship-movement.md section 13.2); a ship's actual velocity is p+0x100 + it
constexpr std::uint32_t kNodeChildren = 0xc, kNodeOffset = 0x30, kNodeScale = 0x70, kNodeOwnRadius = 0xa0, kNodeRadius = 0xa4,
                        kNodeFlags = 0x12c, kNodeFlagScaled = 0x40;
constexpr float kUnits = sc::kUnitsPerMetre;

// The --debug census of one sector's first snapshot: the class histogram, the per-bucket counts and one station's walk.
struct Census {
    std::uint32_t sector, objects, overflow;
    std::uint16_t classes[32];
    std::uint16_t buckets[32];
    std::uint32_t station, station_root_r, station_scale, station_children, station_valid;
    std::uint32_t station_own_r, station_dirty, station_scaled, station_extent; // own radius (units), dirty / flagged children, extent (units)
    std::int32_t part_offset[4][3];
    std::int32_t part_r[4];       // the subtree radius used (the cache, or the replicated getter's value)
    std::int32_t part_cache[4];   // +0xa4 as read (-1 = the engine's getter 0x00488170 has not run; replicated then)
    std::int32_t part_own[4];     // +0xa0, the node's own radius
    std::uint32_t part_scale[4];  // +0x70, the node's scale
    std::uint32_t part_flags[4];
};
struct Stats {
    unsigned part_pinned_full = 0; // amendment 18: a valid model found no slot (every slot pinned by the current snapshot)
    unsigned visited = 0, stations = 0, part_built = 0, part_fallback = 0, part_invalid = 0, part_cached = 0,
             part_dirty = 0,  // stations with at least one unfilled child cache (+0xa4 = -1): the radius was replicated
             part_scaled = 0; // stations with at least one flagged child (offset scaled by the parent's +0x70)
    bool overflow = false;
};
constexpr float kExtentFraction = 0.9f; // the parts must reach at least this fraction of the root radius (the engine computes
                                        // the root radius from the same offsets and radii: a collapsed model is a wrong one)
inline std::uint32_t fnv1a(std::uint32_t h, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) {
        h ^= (v >> (8 * i)) & 0xff;
        h *= 16777619u;
    }
    return h;
}
// The station part cache: one slot per station, keyed on (object, root radius, position-and-basis stamp); built
// into a scratch set and copied on success, so a failing rebuild never leaves a half-written model and a moved or
// re-oriented station (or a reused address) never keeps stale world-space parts.
struct PartCache {
    av::PartSet sets[av::kStationCapacity];
    unsigned count = 0, next = 0;
    av::PartSet scratch;
    // amendment 18 (review 6): a slot the current snapshot references is pinned with its epoch and never evicted in that epoch; with every
    // slot pinned a new model gets no slot (the fallback body), counted in Stats.part_pinned_full
    std::uint32_t pin[av::kStationCapacity] = {};
    std::uint32_t epoch = 0; // the pinning epoch (fill_snapshot: the snapshot's game time, never 0)
    unsigned find(std::uint32_t station, std::uint32_t generation, std::uint32_t stamp) const noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station && sets[i].generation == generation && sets[i].stamp == stamp) return i;
        return av::kStationCapacity;
    }
    unsigned slot_for(std::uint32_t station) noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station) return i;
        if (count < av::kStationCapacity) return count++;
        for (unsigned k = 0; k < av::kStationCapacity; ++k) {
            const unsigned s = (next + k) % av::kStationCapacity;
            if (epoch && pin[s] == epoch) continue; // the current snapshot references it
            next = (s + 1) % av::kStationCapacity;
            return s;
        }
        return av::kStationCapacity;
    }
    void invalidate(std::uint32_t station) noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station) sets[i].station = 0;
    }
};
inline float fixed16(std::int32_t v) noexcept {
    return float(v) * (1.0f / 65536.0f);
}
// The engine's subtree radius getter 0x00488170, replicated read-only (fact (g)): a filled cache `+0xa4` (>= 0) is
// the value; otherwise the max over the children of |offset_i x (flag 0x40 ? parent +0x70 : 1)| + radius(child) on
// each axis, seeded by the node's own radius `+0xa0`, exactly the engine's loop (0x004881a0..0x00488256: per axis
// `max(r, off + rc, rc - off)`). Depth- and budget-limited: -1 = not computable (the caller falls back and retries
// on the next snapshot). Never writes the cache.
template <class Mem>
inline std::int32_t subtree_radius(Mem& mem, std::uint32_t node, unsigned depth, unsigned& budget) noexcept {
    const std::int32_t cached = mem.s32(node + kNodeRadius);
    if (cached >= 0) return cached;
    if (depth >= kRadiusDepth) return -1;
    std::int32_t r = mem.s32(node + kNodeOwnRadius);
    if (r < 0) r = 0;
    const std::int32_t scale = mem.s32(node + kNodeScale);
    for (std::uint32_t c = mem.u32(node + kNodeChildren); c && mem.u32(c) != 0; c = mem.u32(c)) {
        if (!budget) return -1;
        --budget;
        const std::int32_t rc = subtree_radius(mem, c, depth + 1, budget);
        if (rc < 0) return -1;
        const bool scaled = (mem.u32(c + kNodeFlags) & kNodeFlagScaled) != 0;
        for (unsigned i = 0; i < 3; ++i) {
            std::int32_t off = mem.s32(c + kNodeOffset + 4 * i);
            if (scaled) off = std::int32_t((std::int64_t(off) * std::int64_t(scale) + 0x8000) >> 16);
            const std::int32_t hi = off + rc, lo = rc - off;
            if (hi > r) r = hi;
            if (lo > r) r = lo;
        }
    }
    return r;
}
// The station part walk into `out` (false = fallback body: a radius the replication cannot settle, an empty list,
// more than kMaxParts children, the node limit, or a part outside the root sphere).
struct WalkOut { // what a walk saw besides its model: the counts the DLL reports per station
    unsigned children = 0, dirty = 0, scaled = 0;
};
template <class Mem>
inline bool part_walk(Mem& mem, std::uint32_t node, const float pos[3], float root_r, av::PartSet& out, Census* census,
                      WalkOut* seen) noexcept {
    float centres[av::kMaxParts][3], radii[av::kMaxParts];
    unsigned n = 0, children = 0, visited = 0;
    float basis[3][3];
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) basis[r][c] = fixed16(mem.s32(node + kPhysBasis + 0x10 * r + 4 * c));
    const std::int32_t scale = mem.s32(node + kNodeScale);
    const std::int32_t own = mem.s32(node + kNodeOwnRadius);
    if (own > 0) {
        centres[n][0] = pos[0];
        centres[n][1] = pos[1];
        centres[n][2] = pos[2];
        radii[n++] = float(own) / kUnits;
    }
    bool ok = true;
    unsigned dirty = 0, scaled = 0, budget = kRadiusBudget;
    float extent = own > 0 ? float(own) / kUnits : 0.0f;
    for (std::uint32_t c = mem.u32(node + kNodeChildren); c && mem.u32(c) != 0; c = mem.u32(c)) {
        if (++visited > kChildLimit) {
            ok = false;
            break;
        }
        std::int32_t off[3] = {mem.s32(c + kNodeOffset), mem.s32(c + kNodeOffset + 4), mem.s32(c + kNodeOffset + 8)};
        const std::int32_t cache = mem.s32(c + kNodeRadius);
        const std::int32_t r = cache >= 0 ? cache : subtree_radius(mem, c, 1, budget); // the engine's value, or its replication
        const std::uint32_t flags = mem.u32(c + kNodeFlags);
        if (flags & kNodeFlagScaled) {
            ++scaled;
            for (unsigned i = 0; i < 3; ++i) off[i] = std::int32_t((std::int64_t(off[i]) * std::int64_t(scale) + 0x8000) >> 16);
        }
        if (census && children < 4) {
            for (unsigned i = 0; i < 3; ++i) census->part_offset[children][i] = off[i];
            census->part_r[children] = r;
            census->part_cache[children] = cache;
            census->part_own[children] = mem.s32(c + kNodeOwnRadius);
            census->part_scale[children] = mem.u32(c + kNodeScale);
            census->part_flags[children] = flags;
        }
        ++children;
        if (cache < 0) ++dirty; // unfilled cache: replicated
        if (r < 0) { // beyond the replication's depth or budget: no model (retried on the next snapshot)
            ok = false;
            break;
        }
        if (r == 0) continue; // empty subtree: not a body
        if (n >= av::kMaxParts) { // more parts than the model keeps: the fallback body (section 3.5 item 3)
            ok = false;
            break;
        }
        for (unsigned k = 0; k < 3; ++k)
            centres[n][k] = pos[k] + (float(off[0]) * basis[0][k] + float(off[1]) * basis[1][k] + float(off[2]) * basis[2][k]) / kUnits;
        radii[n] = float(r) / kUnits;
        const float dc[3] = {centres[n][0] - pos[0], centres[n][1] - pos[1], centres[n][2] - pos[2]};
        const float reach = sc::sqrt_ss(sc::dot3(dc, dc)) + radii[n];
        if (reach > extent) extent = reach;
        ++n;
    }
    if (census) {
        census->station_scale = std::uint32_t(scale);
        census->station_children = children;
        census->station_own_r = std::uint32_t(own);
        census->station_dirty = dirty;
        census->station_scaled = scaled;
        census->station_extent = std::uint32_t(std::int32_t(extent * kUnits)); // via int32: a float to uint32 cast compiles to x87 fistp on i686
    }
    if (seen) {
        seen->children = children;
        seen->dirty = dirty;
        seen->scaled = scaled;
    }
    if (!ok) return false;
    // the parts must reach the root radius the engine computed from the same offsets and radii (a model whose parts
    // collapsed to the centre passes the inside-root check and would still be wrong)
    if (extent < kExtentFraction * root_r) return false;
    return av::build_parts(out, 0, root_r, pos, centres, radii, n);
}
// The part model index for a station (cached per object, root radius and position/basis stamp), or
// av::kStationCapacity for the fallback body.
template <class Mem>
inline unsigned station_parts(Mem& mem, PartCache& cache, std::uint32_t obj, std::uint32_t node, const float pos[3], float root_r,
                              Census* census, Stats& stats) noexcept {
    std::uint32_t generation, stamp = 2166136261u;
    std::memcpy(&generation, &root_r, 4);
    for (unsigned i = 0; i < 3; ++i) stamp = fnv1a(stamp, mem.u32(node + kPhysPosition + 4 * i));
    for (unsigned i = 0; i < 12; ++i) stamp = fnv1a(stamp, mem.u32(node + kPhysBasis + 4 * i));
    const unsigned hit = cache.find(obj, generation, stamp);
    if (hit < av::kStationCapacity) {
        ++stats.part_cached;
        cache.pin[hit] = cache.epoch;
        return hit;
    }
    ++stats.stations;
    WalkOut seen;
    if (census) census->station = obj;
    const bool built = part_walk(mem, node, pos, root_r, cache.scratch, census, &seen);
    if (seen.dirty) ++stats.part_dirty;   // stations with an unfilled child cache (the radius replicated)
    if (seen.scaled) ++stats.part_scaled; // stations with a flagged child (offset scaled by the parent's +0x70)
    if (census) {
        std::memcpy(&census->station_root_r, &root_r, 4);
        census->station_valid = built ? 1 : 0;
    }
    if (!built) {
        cache.invalidate(obj); // a stale model of a moved station must not survive its failed rebuild
        ++stats.part_invalid;
        return av::kStationCapacity; // not cached: a station the replication could not settle is retried on the next snapshot
    }
    const unsigned slot = cache.slot_for(obj);
    if (slot >= av::kStationCapacity) { // every slot pinned by the current snapshot: no model this time
        cache.invalidate(obj);
        ++stats.part_pinned_full;
        return av::kStationCapacity;
    }
    cache.sets[slot] = cache.scratch;
    cache.sets[slot].station = obj;
    cache.sets[slot].generation = generation;
    cache.sets[slot].stamp = stamp;
    cache.pin[slot] = cache.epoch;
    ++stats.part_built;
    return slot;
}
// The sector snapshot: the obstacle classes' buckets only; the walk limit marks the snapshot overflow (the
// layer passes the point through rather than acting on a partial picture).
template <class Mem>
inline void fill_snapshot(Mem& mem, std::uint32_t sector, std::uint32_t now, av::Snapshot& s, PartCache& cache, Stats& stats,
                          Census* census, av::Members* members = nullptr, av::MemberStats* member_stats = nullptr) noexcept {
    s.sector = sector;
    s.epoch = now;
    s.count = 0;
    s.player = 0;
    s.overflow = 0;
    s.max_r = 0;
    stats = Stats{};
    cache.epoch = now ? now : 1; // amendment 18: this snapshot's slots are pinned for its epoch
    const std::uint32_t table = mem.u32(sector + kSectorBuckets);
    if (!table) {
        s.overflow = 1;
        stats.overflow = true;
        return;
    }
    for (unsigned bi = 0; bi < 6 && !stats.overflow; ++bi) {
        const unsigned b = kBuckets[bi];
        for (std::uint32_t o = mem.u32(table + kBucketStride * b); o && mem.u32(o) != 0; o = mem.u32(o)) {
            if (++stats.visited > kWalkLimit) {
                stats.overflow = true;
                break;
            }
            const std::uint16_t cls = mem.u16(o + kObjClass);
            if (census) {
                ++census->objects;
                if (cls < 32) ++census->classes[cls];
                if (b < 32) ++census->buckets[b];
            }
            if (cls != kClassShip && cls != kClassStationA && cls != kClassStationB && cls != kClassAsteroid && cls != kClassGate &&
                cls != kClassDebris)
                continue;
            const std::uint32_t flags = mem.u32(o + kObjFlags), flags2 = mem.u32(o + kObjFlags2);
            const std::uint32_t ph = mem.u32(o + kObjPhys);
            if (!ph || (flags & (kFlagDeleted | kFlagAttached)) || mem.u32(o + kObjDocked) || (flags2 & kFlags2NoPair)) continue;
            if (s.count >= av::kSnapshotCapacity) {
                ++s.overflow;
                continue;
            }
            av::Obstacle& it = s.items[s.count];
            it = av::Obstacle{};
            for (unsigned c = 0; c < 3; ++c) it.x[c] = float(mem.s32(ph + kPhysPosition + 4 * c)) / kUnits;
            it.r = float(mem.s32(o + kObjRadius)) / kUnits;
            it.ptr = o;
            if (cls == kClassShip && (flags & kFlagPlayerShip)) { // (the arrival round item 6: ship_player_pos)
                s.player = o;
                for (unsigned c = 0; c < 3; ++c) s.player_x[c] = it.x[c];
            }
            if (cls == kClassShip) {
                it.kind = std::uint8_t(av::Kind::ship);
                // the actual velocity p+0x100 + obj+0x60 (amendment 14: the slip term moves the ship too; three more reads per ship)
                // (summed in 64 bits and saturated to int32: two fields of any value never overflow, and the conversion stays the
                // SSE2 int32 one, cvtsi2ss: an int64 -> float conversion is x87 fild on i686)
                for (unsigned c = 0; c < 3; ++c) {
                    const std::int64_t sum = std::int64_t(mem.s32(ph + kPhysVelocity + 4 * c)) + std::int64_t(mem.s32(o + kObjSlip + 4 * c));
                    const std::int32_t sat = sum > 2147483647 ? 2147483647 : sum < -2147483647 - 1 ? -2147483647 - 1 : std::int32_t(sum);
                    it.v[c] = float(sat) / kUnits;
                }
                const std::uint32_t fl = mem.u32(o + kObjFlight);
                if (fl) {
                    it.leader = mem.u32(fl + kFlightTarget);
                    it.order = mem.u16(fl + kFlightOrder);
                }
                // amendment 15: the identity and the autopilot bit (the membership memory), the live effective leader
                it.id = mem.u32(o + kObjId);
                if (flags & av::kObjFlagAutopilot) it.flags = std::uint8_t(it.flags | av::kObstacleAutopilot);
                it.member_of = av::member_order(it.order) ? it.leader : 0;
                // the record code -> capital-coded (amendment 13: a capital keeps every capital-coded ship and ignores other
                // ships below r / 3); 2 when it cannot be read
                const std::int32_t type = std::int16_t(mem.u16(o + kObjType));
                const std::uint32_t table = mem.u32(kShipTypeTableSlot);
                if (table && type >= 0) {
                    const std::uint32_t code = mem.u32(table + std::uint32_t(type) * kRecordStride + kRecordClassCode);
                    it.size = code < sc::kClassCount ? std::uint8_t((sc::kCapitalMask >> code) & 1u) : std::uint8_t(2);
                } else
                    it.size = 2;
            } else {
                it.kind = std::uint8_t(cls == kClassAsteroid ? av::Kind::asteroid
                                       : cls == kClassGate     ? av::Kind::gate
                                       : cls == kClassDebris   ? av::Kind::debris
                                                               : av::Kind::station);
                if (it.kind == std::uint8_t(av::Kind::station) && it.r > 0.0f) {
                    const unsigned idx = station_parts(mem, cache, o, ph, it.x, it.r, census && !census->station ? census : nullptr, stats);
                    if (idx < av::kStationCapacity) {
                        it.flags = 1;
                        it.parts = std::uint16_t(idx);
                    } else
                        ++stats.part_fallback;
                }
            }
            if (it.r > s.max_r) s.max_r = it.r;
            ++s.count;
        }
    }
    if (stats.overflow) {
        s.overflow = 1; // a partial picture is never acted on
        s.count = 0;
    }
    if (census) census->overflow = s.overflow;
    if (members && member_stats && !s.overflow) av::update_members(s, *members, now, *member_stats); // amendment 15
}
// ---- The arrival round item 4: whether a gate's body carries a collision tree (convoy-gate-transit.md section 12.6) -------------------
// The gate's root scene node [gate+0x70] (the station walk's layout, ship-navigation-ai.md section 10.9 "Collision-tree read": children
// head [n+0xc], sentinel n+0x10 whose first dword is 0, sibling link [c], parent [c+0x18], scene id [c+0x258]) and the nodes below it
// (depth <= kGateTreeDepth, at most kGateTreeBudget nodes, a sibling chain at most kChildLimit): one with [c+0x12c] & 0x1000000 (the part
// builder's collision tree) and without 0x8000000 is the solid ("tree": the fail-closed side, no guard). "None" only under the positive
// guard of section 10.9 (review of the arrival round item 1): the object is a gate (class 0x12) with a root and a scene id; every visited
// child's [c+0x18] is its parent; every chain ends exactly at its parent's sentinel; a child at the depth limit has no children; the
// budget is not exhausted; at least one visited node carries the gate's scene id. Anything else is unknown (the caller fails closed to the
// ring of the gate's TGates row). Every pointer is range- and alignment-checked before its first read (the DLL's reads are plain loads:
// a wrong layout must not fault). Read-only, no engine call; once per gate (the board's Gate::ring, the glue's small cache).
constexpr std::uint32_t kNodeFlagNoTree = 0x8000000; // (the body slot out of range: no tree, 0x0047ece8)
constexpr std::uint32_t kNodeFlagTree = 0x1000000, kNodeSentinel = 0x10, kNodeParent = 0x18, kNodeSceneId = 0x258, kNodeBytes = 0x270;
constexpr std::uint32_t kObjScene = 0x98; // [gate+0x98]: the TGates +0x44 scene id (0x00441686..0x004416a1)
constexpr std::uint32_t kGateObjBytes = 0x100;
constexpr unsigned kGateTreeDepth = 3;
// Run176 diagnostics item 2: the gate read's own node budget (the station part walk keeps kRadiusBudget). Run176 measured gates whose depth-1
// children alternate 1 <-> 33 at runtime, the extra 32 identical effect leaves (scene id 0, part index 0, body 0xc8, flags 0x5000); the
// budget leaves room for that count to grow without exhausting the read into unknown (the ring). Effect nodes are counted apart.
constexpr unsigned kGateTreeBudget = 256;
// A user-mode pointer of a 32-bit process, 4-aligned, with span bytes readable in range: [0x10000, 0xFFFF0000) covers a large-address-aware
// process on 64-bit Windows (X3AP.exe has IMAGE_FILE_LARGE_ADDRESS_AWARE); the range is a plausibility test, not a mapping test
constexpr std::uint32_t kPtrLo = 0x10000u, kPtrHi = 0xffff0000u;
__attribute__((always_inline)) inline bool ptr_ok(std::uint32_t p, std::uint32_t span) noexcept {
    return p >= kPtrLo && p < kPtrHi && span <= kPtrHi - p && !(p & 3u);
}
enum class TreeWhy : std::uint8_t { ok = 0, gate_ptr, cls, root, scene, node_ptr, parent, chain, depth, budget, no_scene_node, count };
inline const char* tree_why_name(TreeWhy w) noexcept {
    static const char* const n[unsigned(TreeWhy::count)] = {"ok", "gate_ptr", "class", "root", "scene", "node_ptr", "parent", "chain", "depth",
                                                            "budget", "no_scene_node"};
    return unsigned(w) < unsigned(TreeWhy::count) ? n[unsigned(w)] : "?";
}
// Run175 round item 6 (--debug): one record per visited node when the caller hands gate_tree_read a buffer: its depth, [c+0x258] (the
// scene id), [c+0x25c], [c+0x140] and its flags [c+0x12c] (the fields ship-navigation-ai.md section 10.9 reads the tree verdict from)
struct GateTreeNode {
    std::uint32_t scene, f25c, f140, flags;
    std::uint8_t depth;
};
struct GateTreeRead {
    std::uint8_t state = 0;   // 2 a tree, 1 none (the guard held), 0 unknown
    TreeWhy why = TreeWhy::ok; // the first guard that failed (ok: none or tree)
    std::uint8_t seen = 0;    // a visited node carried the gate's scene id
    std::uint16_t nodes = 0;  // nodes visited
    std::uint16_t effects = 0; // of them effect nodes: scene id [c+0x258] 0 and part index [c+0x25c] 0 (Run176 diagnostics item 2)
    std::uint32_t scene = 0;  // [gate+0x98]
    GateTreeNode* out = nullptr; // (item 6: the node records, at most cap; nullptr: none)
    unsigned cap = 0;
};
// 1 a tree, 0 the subtree below n consistent and without one, -1 a guard failed (rd.why)
template <class Mem>
inline int gate_tree_walk(Mem& mem, std::uint32_t n, unsigned depth, unsigned& budget, GateTreeRead& rd) noexcept {
    std::uint32_t c = mem.u32(n + kNodeChildren);
    for (unsigned k = 0;; ++k) {
        if (c == n + kNodeSentinel) { // the chain's end is its parent's own sentinel, whose first dword is 0
            if (mem.u32(c) != 0) break;
            return 0;
        }
        if (!ptr_ok(c, kNodeBytes)) {
            rd.why = TreeWhy::node_ptr;
            return -1;
        }
        const std::uint32_t next = mem.u32(c);
        if (!next) break; // (a chain ending anywhere but n + 0x10)
        if (k >= kChildLimit || !budget) {
            rd.why = TreeWhy::budget;
            return -1;
        }
        --budget;
        ++rd.nodes;
        if (mem.u32(c + kNodeParent) != n) {
            rd.why = TreeWhy::parent;
            return -1;
        }
        const std::uint32_t sid = mem.u32(c + kNodeSceneId), part = mem.u32(c + kNodeSceneId + 4);
        if (sid == rd.scene) rd.seen = 1;
        if (!sid && !part) ++rd.effects;
        const std::uint32_t fl = mem.u32(c + kNodeFlags);
        if (rd.out && rd.nodes <= rd.cap) // (item 6: the record; the reads are plain loads inside the node's checked span)
            rd.out[rd.nodes - 1] = GateTreeNode{sid, part, mem.u32(c + 0x140), fl, std::uint8_t(depth + 1)};
        if ((fl & kNodeFlagTree) && !(fl & kNodeFlagNoTree)) return 1;
        if (depth + 1 < kGateTreeDepth) {
            const int r = gate_tree_walk(mem, c, depth + 1, budget, rd);
            if (r) return r;
        } else if (mem.u32(c + kNodeChildren) != c + kNodeSentinel || mem.u32(c + kNodeSentinel) != 0) { // (the depth limit: no children)
            rd.why = TreeWhy::depth;
            return -1;
        }
        c = next;
    }
    rd.why = TreeWhy::chain;
    return -1;
}
template <class Mem>
inline GateTreeRead gate_tree_read(Mem& mem, std::uint32_t gate, GateTreeNode* out = nullptr, unsigned cap = 0) noexcept {
    GateTreeRead rd;
    rd.out = out;
    rd.cap = out ? cap : 0;
    if (!ptr_ok(gate, kGateObjBytes)) {
        rd.why = TreeWhy::gate_ptr;
        return rd;
    }
    if (mem.u16(gate + kObjClass) != kClassGate) {
        rd.why = TreeWhy::cls;
        return rd;
    }
    const std::uint32_t root = mem.u32(gate + kObjPhys);
    rd.scene = mem.u32(gate + kObjScene);
    if (!ptr_ok(root, kNodeBytes)) {
        rd.why = TreeWhy::root;
        return rd;
    }
    if (!rd.scene) {
        rd.why = TreeWhy::scene;
        return rd;
    }
    unsigned budget = kGateTreeBudget;
    const int r = gate_tree_walk(mem, root, 0, budget, rd);
    if (r > 0) rd.state = 2;
    else if (r == 0) {
        if (rd.seen) rd.state = 1;
        else rd.why = TreeWhy::no_scene_node;
    }
    return rd;
}
template <class Mem>
inline unsigned gate_tree_state(Mem& mem, std::uint32_t gate) noexcept {
    return gate_tree_read(mem, gate).state;
}
// ---- Amendment 16 (capital-gate-transit.md section 16.1, ratification addendum 2026-10-13 (5)): the hull's lateral half-width ---
// render-node-bounds.md section 6.1. The ship's root node [obj+0x70] carries `testbody`; the hull and every other part are its
// descendants (`c = [n+0xc]; while c && [c] != 0; c = [c]`, at most kHullDepth deep and kHullBudget nodes). Per node: body id
// [c+0x140] > 0 and not [c+0x12c] & 0x8000000 (no collision tree); the model through the body cache hash, read-only (H =
// [[0x00608518]+0x14], buckets [H], size [H+4], key = id + 1, bucket [buckets + 4 (key & (size - 1))], entries [e+4] == key ->
// model [e+8], next [e]); the engine's lookup 0x004863c0 is never called (it writes and may load from disk). LOD records:
// n = (int16)[model+0x10] in 1..16, L_i = [[model+0xc] + 4 i]; the per-axis max over ALL records of [L+0x20/0x24/0x28] (each
// in [0, 0x20000], normalised 65536 = 1.0; the collision LOD can be wider than LOD 0), world half-extent
// ((ext x [c+0x70] + 0x8000) >> 16) x [c+0x80 + 4 i] / 65536 units. Placement: the offset [c+0x30..0x38] in the parent frame
// x parent [+0x80..] / 65536 unless parent [+0x12c] & 0x10000, x parent [+0x70] / 65536 when [c+0x12c] & 0x40; a node whose
// basis [c+0x40..] (3 rows of 4 dwords, 16.16) or an ancestor's is not the identity is bounded by |e| (and |o|) on every axis.
// w = max over the parts of sqrt((|o_x| + e_x)^2 + (|o_y| + e_y)^2), in m, at most r. Any failed guard (a null pointer, a
// missing model, a count or extent out of range, a non-positive scale, no part at all) gives w = r with `failed`.
constexpr std::uint32_t kBodyCacheRoot = 0x00608518, kBodyCacheHash = 0x14, kNodeBodyId = 0x140, kNodeAxisScale = 0x80, kNodeBasis = 0x40;
constexpr std::uint32_t kNodeFlagNoCollision = 0x8000000, kNodeFlagUnscaledOffset = 0x10000;
constexpr std::uint32_t kModelLods = 0x0c, kModelLodCount = 0x10, kLodExtent = 0x20;
// kHullBudget: the installed capitals have at most 108 parts (render-node-bounds.md section 6.2; review F3), the walk stays bounded.
constexpr unsigned kHullDepth = 3, kHullBudget = 128, kHullChainSteps = 64, kHullLodMax = 16;
constexpr std::int32_t kLodExtentMax = 0x20000, kBasisTol = 0x40;
constexpr std::int32_t kHullScaleMax = 16 * 0x10000; // review F7: a node or axis scale outside (0, 16.0] is a guard failure (the
                                                      // int64 products below then stay far from overflow)
struct HullWidth {
    float w_m = 0, half_len_m = 0;
    unsigned parts = 0, nodes = 0;
    x3m::ship_avoidance::lane::WidthSrc src = x3m::ship_avoidance::lane::WidthSrc::failed;
    // amendment 17 (section 17 item 5, the census row): the part that sets w: its body id, its lateral offset bound (|o_x|, |o_y|
    // accumulated in the root frame, m), its lateral half-extents (e_x, e_y, m) and the LOD record with the widest lateral extent
    std::int32_t widest_id = 0;
    float widest_o[2] = {0, 0}, widest_e[2] = {0, 0};
    std::int32_t widest_lod = -1;
};
template <class Mem>
inline bool hull_model(Mem& mem, std::int32_t id, std::uint32_t* model) noexcept {
    const std::uint32_t root = mem.u32(kBodyCacheRoot);
    const std::uint32_t H = root ? mem.u32(root + kBodyCacheHash) : 0;
    if (!H) return false;
    const std::uint32_t buckets = mem.u32(H), size = mem.u32(H + 4);
    if (!buckets || !size || size > 0x100000u || (size & (size - 1))) return false;
    const std::uint32_t key = std::uint32_t(id) + 1;
    std::uint32_t e = mem.u32(buckets + 4 * (key & (size - 1)));
    for (unsigned steps = 0; e && steps < kHullChainSteps; ++steps, e = mem.u32(e))
        if (mem.u32(e + 4) == key) {
            *model = mem.u32(e + 8);
            return *model != 0;
        }
    return false;
}
template <class Mem>
inline bool basis_identity(Mem& mem, std::uint32_t node) noexcept {
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) {
            const std::int32_t v = mem.s32(node + kNodeBasis + 0x10 * r + 4 * c);
            const std::int32_t want = r == c ? 0x10000 : 0;
            if (v - want > kBasisTol || want - v > kBasisTol) return false;
        }
    return true;
}
__attribute__((always_inline)) inline std::int32_t scale16(std::int32_t v, std::int32_t s) noexcept {
    std::int64_t p = std::int64_t(v) * std::int64_t(s) / 65536;
    if (p > 0x7fffffffLL) p = 0x7fffffffLL;
    if (p < -0x7fffffffLL) p = -0x7fffffffLL;
    return std::int32_t(p);
}
// One node's children (depth = their depth below the root; px/py the parent's lateral offset bound in the root frame, units).
template <class Mem>
inline bool hull_children(Mem& mem, std::uint32_t parent, unsigned depth, float px, float py, float pz, bool aligned, unsigned& budget,
                          HullWidth& out, float& w_units, float& len_units) noexcept {
    const std::uint32_t pflags = mem.u32(parent + kNodeFlags);
    const std::int32_t pscale = mem.s32(parent + kNodeScale);
    unsigned visited = 0;
    for (std::uint32_t c = mem.u32(parent + kNodeChildren); c && mem.u32(c) != 0; c = mem.u32(c)) {
        if (++visited > kChildLimit || !budget) return false;
        --budget;
        ++out.nodes;
        const std::uint32_t flags = mem.u32(c + kNodeFlags);
        std::int32_t o[3];
        for (unsigned i = 0; i < 3; ++i) {
            o[i] = mem.s32(c + kNodeOffset + 4 * i);
            if (!(pflags & kNodeFlagUnscaledOffset)) o[i] = scale16(o[i], mem.s32(parent + kNodeAxisScale + 4 * i));
            if (flags & kNodeFlagScaled) o[i] = scale16(o[i], pscale);
        }
        const float of[3] = {float(o[0]), float(o[1]), float(o[2])};
        float cx, cy, cz;
        if (aligned) {
            cx = px + sc::fabs_f(of[0]);
            cy = py + sc::fabs_f(of[1]);
            cz = pz + sc::fabs_f(of[2]);
        } else { // an ancestor is rotated: the offset may point anywhere
            const float on = sc::sqrt_ss(sc::dot3(of, of));
            cx = px + on;
            cy = py + on;
            cz = pz + on;
        }
        const bool here_aligned = aligned && basis_identity(mem, c);
        const std::int32_t id = mem.s32(c + kNodeBodyId);
        if (id > 0 && !(flags & kNodeFlagNoCollision)) {
            std::uint32_t model = 0;
            if (!hull_model(mem, id, &model)) return false;
            const std::int32_t n = std::int16_t(mem.u16(model + kModelLodCount));
            const std::uint32_t arr = mem.u32(model + kModelLods);
            if (n < 1 || n > std::int32_t(kHullLodMax) || !arr) return false;
            std::int32_t ext[3] = {0, 0, 0};
            std::int32_t lod = -1, lod_lat = -1; // the record with the widest lateral extent max(x, y) (the census row)
            for (std::int32_t k = 0; k < n; ++k) {
                const std::uint32_t L = mem.u32(arr + 4 * std::uint32_t(k));
                if (!L) return false;
                std::int32_t lat = 0;
                for (unsigned i = 0; i < 3; ++i) {
                    const std::int32_t v = mem.s32(L + kLodExtent + 4 * i);
                    if (v < 0 || v > kLodExtentMax) return false;
                    if (v > ext[i]) ext[i] = v;
                    if (i < 2 && v > lat) lat = v;
                }
                if (lat > lod_lat) {
                    lod_lat = lat;
                    lod = k;
                }
            }
            const std::int32_t s = mem.s32(c + kNodeScale);
            if (s <= 0 || s > kHullScaleMax) return false;
            float e[3];
            for (unsigned i = 0; i < 3; ++i) {
                const std::int32_t si = mem.s32(c + kNodeAxisScale + 4 * i);
                if (si <= 0 || si > kHullScaleMax) return false;
                const std::int64_t base = (std::int64_t(ext[i]) * std::int64_t(s) + 0x8000) >> 16;
                const std::int64_t v = base * std::int64_t(si) / 65536;
                e[i] = float(std::int32_t(v > 0x7fffffffLL ? 0x7fffffffLL : v));
            }
            if (!here_aligned) {
                const float en = sc::sqrt_ss(sc::dot3(e, e));
                e[0] = e[1] = e[2] = en;
            }
            const float lx = cx + e[0], ly = cy + e[1];
            const float lat = sc::sqrt_ss(lx * lx + ly * ly);
            if (!sc::finite_f(lat)) return false;
            if (lat > w_units) {
                w_units = lat;
                out.widest_id = id;
                out.widest_o[0] = cx / kUnits;
                out.widest_o[1] = cy / kUnits;
                out.widest_e[0] = e[0] / kUnits;
                out.widest_e[1] = e[1] / kUnits;
                out.widest_lod = lod;
            }
            if (cz + e[2] > len_units) len_units = cz + e[2];
            ++out.parts;
        }
        if (depth < kHullDepth && !hull_children(mem, c, depth + 1, cx, cy, cz, here_aligned, budget, out, w_units, len_units)) return false;
    }
    return true;
}
template <class Mem>
inline HullWidth hull_width(Mem& mem, std::uint32_t obj, float r_m) noexcept {
    HullWidth out;
    out.w_m = r_m;
    const std::uint32_t root = obj ? mem.u32(obj + kObjPhys) : 0;
    if (!root || !(r_m > 0.0f) || !sc::finite_f(r_m)) return out;
    unsigned budget = kHullBudget;
    float w_units = 0.0f, len_units = 0.0f;
    if (!hull_children(mem, root, 1, 0.0f, 0.0f, 0.0f, true, budget, out, w_units, len_units) || !out.parts) {
        out.w_m = r_m;
        return out;
    }
    const float w = w_units / kUnits;
    if (!sc::finite_f(w) || !(w > 0.0f)) return out;
    out.w_m = w < r_m ? w : r_m;
    out.half_len_m = len_units / kUnits;
    out.src = x3m::ship_avoidance::lane::WidthSrc::lod;
    return out;
}
}
