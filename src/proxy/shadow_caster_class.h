#pragma once
// Static/moving classification of a live caster draw for the static-only far
// cascades (docs/architecture/shadow-cascade-extents.md, "Caster pool control";
// X3M_SHADOW_CASCADE_STATIC_FROM). The retention store's verdict is used for
// the nodes it knows (shadow_retention_core.h: eight verified sightings within
// eps); a node the store does not know, or with the store off, is classified
// here against its own anchor sighting: the draw's object -> world rows
// (world_rows, from the clip rows and the frame's camera latch) must be within
// eps of the rows of the same node and draw range at its anchor (drift2 on the
// draw's own extent, the store's law). The anchor is the first sighting and
// moves only on a beyond-eps move (as the store's `d.world`), so a slow
// drifter accumulates against it and is reclassified when its drift reaches
// eps, never re-anchored under it. A sighting without an anchor is a miss and
// counts as moving for that frame. CPU bookkeeping only: storage allocated
// once at attach while the option is on (sized to the record capacity), no
// allocation, locking or device call on the draw path.
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "shadow_replay_candidates.h"
#include "shadow_retention_core.h"

namespace x3m::shadow_caster_class {
enum class Verdict : std::uint8_t { Miss = 0, Moving = 1, Static = 2 };
// Two-way set-associative ring keyed by the node serial and the draw's range
// (a node's parts carry their own rows; shadow_replay::caster_key). Sized by
// the owner to the record list (`sets` = the record capacity: two entries per
// record, so a full list of distinct casters fits with room); a collision
// evicts the older way and the evicted draw misses once when next seen.
struct Ring {
    static constexpr unsigned ways = 2;
    struct Entry { std::uint64_t key = 0; std::uint32_t stamp = 0; double world[12]{}; };
    std::unique_ptr<Entry[]> entries;
    unsigned sets = 0;
    explicit Ring(unsigned set_count) noexcept : entries(new (std::nothrow) Entry[std::size_t(set_count ? set_count : 1u) * ways]), sets(set_count ? set_count : 1u) {}
    bool valid() const noexcept { return entries != nullptr; }
    unsigned size() const noexcept { return sets * ways; }
    static std::uint64_t key_of(std::uint64_t serial, std::uint64_t vb, std::uint32_t first, std::int32_t base_vertex, std::uint32_t count) noexcept {
        return shadow_replay::caster_key(serial, vb, first, base_vertex, count);
    }
    void clear() noexcept { if (entries) for (unsigned i = 0; i < size(); ++i) { entries[i].key = 0; entries[i].stamp = 0; } }
    // `world`: this sighting's rows; `lo`/`hi`: the draw's own extent (zero
    // boxes compare the origin alone); `frame`: the current frame. Static:
    // within eps of the anchor (which stays). Moving: beyond eps; the sighting
    // becomes the new anchor. Miss: no anchor; the sighting becomes one.
    Verdict test(std::uint64_t key, const double world[12], const float lo[3], const float hi[3], double eps, std::uint32_t frame) noexcept {
        if (!entries) return Verdict::Miss;
        Entry* set = entries.get() + std::size_t(key % sets) * ways;
        Entry* found = nullptr; Entry* victim = set;
        for (unsigned w = 0; w < ways; ++w) {
            if (set[w].key == key) { found = &set[w]; break; }
            if (set[w].key == 0) { victim = &set[w]; break; }
            if (set[w].stamp < victim->stamp) victim = &set[w];
        }
        if (!found) {
            victim->key = key; victim->stamp = frame; std::memcpy(victim->world, world, sizeof victim->world);
            return Verdict::Miss;
        }
        found->stamp = frame;
        const double d2 = shadow_retention::drift2(world, found->world, lo, hi);
        if (d2 <= eps * eps) return Verdict::Static;
        std::memcpy(found->world, world, sizeof found->world); // the new anchor
        return Verdict::Moving;
    }
};
} // namespace x3m::shadow_caster_class
