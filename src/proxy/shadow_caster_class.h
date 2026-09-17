#pragma once
// Static/moving classification of a live caster draw for the static-only far
// cascades (docs/architecture/shadow-cascade-extents.md, "Caster pool control";
// X3M_SHADOW_CASCADE_STATIC_FROM). The retention store's verdict is used for
// the nodes it knows (shadow_retention_core.h: eight verified sightings within
// eps); a node the store does not know, or with the store off, is classified
// here against its own previous sighting: the draw's object -> world rows
// (world_rows, from the clip rows and the frame's camera latch) must be within
// eps of the rows of the same node and draw range last time (drift2 on the
// draw's own extent, the store's law). A sighting without a previous one is a
// miss and counts as moving for that frame. CPU bookkeeping only: fixed
// storage allocated once at attach while the option is on, no allocation,
// locking or device call on the draw path.
#include <cstdint>
#include <cstring>
#include "shadow_retention_core.h"

namespace x3m::shadow_caster_class {
enum class Verdict : std::uint8_t { Miss = 0, Moving = 1, Static = 2 };
// Two-way set-associative ring keyed by the node serial and the draw's range
// (a node's parts carry their own rows). A collision evicts the older way; the
// evicted draw misses once when it is next seen.
struct Ring {
    static constexpr unsigned sets = 1024, ways = 2, size = sets * ways;
    struct Entry { std::uint64_t key = 0; std::uint32_t stamp = 0; double world[12]{}; };
    Entry entries[size];
    static std::uint64_t key_of(std::uint64_t serial, std::uint64_t vb, std::uint32_t first, std::int32_t base_vertex, std::uint32_t count) noexcept {
        std::uint64_t h = serial * 0x9E3779B97F4A7C15ull;
        h ^= vb * 0xC2B2AE3D27D4EB4Full;
        h ^= (std::uint64_t(first) << 32 | std::uint32_t(base_vertex)) * 0x165667B19E3779F9ull;
        h ^= std::uint64_t(count) * 0x27D4EB2F165667C5ull;
        h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
        return h ? h : 1u; // 0 marks an empty entry
    }
    void clear() noexcept { for (auto& e : entries) { e.key = 0; e.stamp = 0; } }
    // `world`: this sighting's rows; `lo`/`hi`: the draw's own extent (zero
    // boxes compare the origin alone); `frame`: the current frame (a second
    // draw of the same key in one frame compares without replacing the
    // sighting). The entry is updated to this sighting either way.
    Verdict test(std::uint64_t key, const double world[12], const float lo[3], const float hi[3], double eps, std::uint32_t frame) noexcept {
        Entry* set = entries + std::size_t(key % sets) * ways;
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
        const double d2 = shadow_retention::drift2(world, found->world, lo, hi);
        const Verdict v = d2 <= eps * eps ? Verdict::Static : Verdict::Moving;
        if (found->stamp != frame) { found->stamp = frame; std::memcpy(found->world, world, sizeof found->world); }
        return v;
    }
};
} // namespace x3m::shadow_caster_class
