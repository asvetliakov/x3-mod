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
// the child's cached subtree radius is `+0xa4` (-1 = dirty).
namespace x3m::ship_avoidance::walk {
namespace av = x3m::ship_avoidance::core;
namespace sc = x3m::ship_steering::core;
constexpr unsigned kBuckets[6] = {5, 6, 7, 17, 18, 28};
constexpr unsigned kWalkLimit = 8192; // objects visited per snapshot at most: beyond it the snapshot is overflow (pass-through)
constexpr unsigned kChildLimit = 256; // nodes visited per station at most: beyond it the station uses the fallback body
constexpr std::uint16_t kClassShip = 7, kClassStationA = 5, kClassStationB = 6, kClassAsteroid = 17, kClassGate = 18, kClassDebris = 28;
constexpr std::uint32_t kSectorBuckets = 0x50, kBucketStride = 0xc;
constexpr std::uint32_t kObjFlags = 0x40, kObjFlags2 = 0x44, kObjClass = 0x48, kObjFlight = 0x50, kObjPhys = 0x70, kObjRadius = 0xa4,
                        kObjDocked = 0xf8;
constexpr std::uint32_t kFlagDeleted = 0x8000000, kFlagAttached = 0x200, kFlags2NoPair = 0x20000;
constexpr std::uint32_t kFlightTarget = 0x224, kFlightOrder = 0x258;
constexpr std::uint32_t kPhysPosition = 0x30, kPhysBasis = 0x40, kPhysVelocity = 0x100;
constexpr std::uint32_t kNodeChildren = 0xc, kNodeOffset = 0x30, kNodeScale = 0x70, kNodeOwnRadius = 0xa0, kNodeRadius = 0xa4,
                        kNodeFlags = 0x12c, kNodeFlagScaled = 0x40;
constexpr float kUnits = sc::kUnitsPerMetre;

// The --debug census of one sector's first snapshot: the class histogram, the per-bucket counts and one station's walk.
struct Census {
    std::uint32_t sector, objects, overflow;
    std::uint16_t classes[32];
    std::uint16_t buckets[32];
    std::uint32_t station, station_root_r, station_scale, station_children, station_valid;
    std::int32_t part_offset[4][3];
    std::int32_t part_r[4];
    std::uint32_t part_flags[4];
};
struct Stats {
    unsigned visited = 0, stations = 0, part_built = 0, part_fallback = 0, part_invalid = 0, part_cached = 0;
    bool overflow = false;
};
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
    unsigned find(std::uint32_t station, std::uint32_t generation, std::uint32_t stamp) const noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station && sets[i].generation == generation && sets[i].stamp == stamp) return i;
        return av::kStationCapacity;
    }
    unsigned slot_for(std::uint32_t station) noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station) return i;
        if (count < av::kStationCapacity) return count++;
        const unsigned s = next;
        next = (next + 1) % av::kStationCapacity;
        return s;
    }
    void invalidate(std::uint32_t station) noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (sets[i].station == station) sets[i].station = 0;
    }
};
inline float fixed16(std::int32_t v) noexcept {
    return float(v) * (1.0f / 65536.0f);
}
// The station part walk into `out` (false = fallback body: dirty radii only, an empty list, more than kMaxParts
// children, the node limit, or a part outside the root sphere).
template <class Mem>
inline bool part_walk(Mem& mem, std::uint32_t node, const float pos[3], float root_r, av::PartSet& out, Census* census,
                      unsigned* children_out) noexcept {
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
    for (std::uint32_t c = mem.u32(node + kNodeChildren); c && mem.u32(c) != 0; c = mem.u32(c)) {
        if (++visited > kChildLimit) {
            ok = false;
            break;
        }
        std::int32_t off[3] = {mem.s32(c + kNodeOffset), mem.s32(c + kNodeOffset + 4), mem.s32(c + kNodeOffset + 8)};
        const std::int32_t r = mem.s32(c + kNodeRadius);
        const std::uint32_t flags = mem.u32(c + kNodeFlags);
        if (flags & kNodeFlagScaled)
            for (unsigned i = 0; i < 3; ++i) off[i] = std::int32_t((std::int64_t(off[i]) * std::int64_t(scale) + 0x8000) >> 16);
        if (census && children < 4) {
            for (unsigned i = 0; i < 3; ++i) census->part_offset[children][i] = off[i];
            census->part_r[children] = r;
            census->part_flags[children] = flags;
        }
        ++children;
        if (r <= 0) continue; // dirty or empty subtree: not a body
        if (n >= av::kMaxParts) { // more parts than the model keeps: the fallback body (section 3.5 item 3)
            ok = false;
            break;
        }
        for (unsigned k = 0; k < 3; ++k)
            centres[n][k] = pos[k] + (float(off[0]) * basis[0][k] + float(off[1]) * basis[1][k] + float(off[2]) * basis[2][k]) / kUnits;
        radii[n++] = float(r) / kUnits;
    }
    if (census) {
        census->station_scale = std::uint32_t(scale);
        census->station_children = children;
    }
    if (children_out) *children_out = children;
    if (!ok) return false;
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
        return hit;
    }
    ++stats.stations;
    unsigned children = 0;
    if (census) census->station = obj;
    const bool built = part_walk(mem, node, pos, root_r, cache.scratch, census, &children);
    if (census) {
        std::memcpy(&census->station_root_r, &root_r, 4);
        census->station_valid = built ? 1 : 0;
    }
    if (!built) {
        cache.invalidate(obj); // a stale model of a moved station must not survive its failed rebuild
        ++stats.part_invalid;
        return av::kStationCapacity;
    }
    const unsigned slot = cache.slot_for(obj);
    cache.sets[slot] = cache.scratch;
    cache.sets[slot].station = obj;
    cache.sets[slot].generation = generation;
    cache.sets[slot].stamp = stamp;
    ++stats.part_built;
    return slot;
}
// The sector snapshot: the obstacle classes' buckets only; the walk limit marks the snapshot overflow (the
// layer passes the point through rather than acting on a partial picture).
template <class Mem>
inline void fill_snapshot(Mem& mem, std::uint32_t sector, std::uint32_t now, av::Snapshot& s, PartCache& cache, Stats& stats,
                          Census* census) noexcept {
    s.sector = sector;
    s.epoch = now;
    s.count = 0;
    s.overflow = 0;
    s.max_r = 0;
    stats = Stats{};
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
            if (cls == kClassShip) {
                it.kind = std::uint8_t(av::Kind::ship);
                for (unsigned c = 0; c < 3; ++c) it.v[c] = float(mem.s32(ph + kPhysVelocity + 4 * c)) / kUnits;
                const std::uint32_t fl = mem.u32(o + kObjFlight);
                if (fl) {
                    it.leader = mem.u32(fl + kFlightTarget);
                    it.order = mem.u16(fl + kFlightOrder);
                }
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
}
}
