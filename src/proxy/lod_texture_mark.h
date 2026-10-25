#pragma once
// Merged-LOD overlay atlas marks (docs/architecture/overlay-material-strength.md 4; the baker's lod_atlas.dds_mark):
// the DDS header the engine hands D3DXCreateTextureFromFileInMemoryEx carries 'X3ML', a version word and a
// descriptor in dwReserved1[0..2] (file bytes 32-43) when the texture is one of the baker's atlases; the specular
// atlas's descriptor says "alpha = diffuse-strength ratio / K, K = 2". Header-only, portable (no Windows types): the
// parser and the fixed-capacity pointer table are host-tested (verification/analysis/test_material_strength.py through
// verification/probe/lod_texture_mark_check.cpp). No allocation, no floating point.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace x3m::lod_texture_mark {
// Exact count of marked entries stored in every device's table (incremented by a new insert, decremented by every
// erase and by a table's destruction): read lock-free by the final-release observer before it takes any lock, so a
// session whose marked atlases have all unloaded is back to one relaxed load per final texture release.
inline std::atomic<unsigned> live_marks{0};
constexpr std::uint32_t magic = 0x4c4d3358u; // 'X3ML' little-endian bytes
constexpr std::uint32_t version = 1;
constexpr unsigned header_bytes = 128, mark_offset = 32;
constexpr unsigned payload_none = 0, payload_strength = 1; // descriptor bits 24-31
constexpr unsigned lane_w = 3, strength_k = 2;
constexpr unsigned slot_count = 4; // diffuse, light, bump, specular (descriptor bits 0-7)
// What the sampler shadow carries per stage: 0 = unmarked (or a marked atlas without a payload), 1 = the specular
// atlas whose alpha is the ratio / 2 (the twin reads it).
constexpr std::uint8_t lane_none = 0, lane_strength_alpha = 1;
struct Mark {
    std::uint32_t version = 0;
    unsigned slot = 0, lane = 0, k = 0, payload = 0;
};
inline std::uint32_t word(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
// True when `data` (the bytes the engine passed, `size` of them) is a DDS file whose reserved words carry the mark.
inline bool parse(const void* data, std::size_t size, Mark& out) noexcept {
    out = {};
    if (!data || size < header_bytes) return false;
    const auto* p = static_cast<const unsigned char*>(data);
    if (word(p) != 0x20534444u /* 'DDS ' */ || word(p + 4) != 124u || word(p + mark_offset) != magic) return false;
    const std::uint32_t v = word(p + mark_offset + 4), d = word(p + mark_offset + 8);
    out.version = v;
    out.slot = d & 255u;
    out.lane = (d >> 8) & 255u;
    out.k = (d >> 16) & 255u;
    out.payload = d >> 24;
    return true;
}
// The sampler-shadow byte of a parsed mark: the strength lane only for the exact descriptor the baker writes for the
// specular atlas (version 1, slot 3, lane w, K 2, payload strength); any other marked atlas is just counted.
inline std::uint8_t lane_of(const Mark& m) noexcept {
    return m.version == version && m.slot == 3 && m.lane == lane_w && m.k == strength_k && m.payload == payload_strength
               ? lane_strength_alpha
               : lane_none;
}
// Marked textures by the application-visible pointer, with the proxy's resource identity (capture_state resource_id)
// so a pointer reused by a later allocation is caught: open addressing, fixed capacity, no allocation. `insert` with
// a pointer already present overwrites; a full table refuses (fail closed: the texture draws at the merged
// strength) and counts. `erase` frees the slot (tombstone-free: entries are re-inserted after a removal in the probe
// run, so lookups stay exact).
template <unsigned Capacity> struct Table {
    static_assert((Capacity & (Capacity - 1)) == 0, "power of two");
    struct Entry {
        std::uintptr_t key = 0;
        std::uint64_t identity = 0;
        std::uint8_t lane = 0, slot = 0;
    };
    Entry entries[Capacity]{};
    unsigned count = 0, refused_full = 0;
    static unsigned hash(std::uintptr_t key) noexcept {
        std::uint64_t h = std::uint64_t(key) * 0x9e3779b97f4a7c15ull;
        return unsigned(h >> 40) & (Capacity - 1);
    }
    Entry* find(const void* texture) noexcept {
        const auto key = reinterpret_cast<std::uintptr_t>(texture);
        if (!key) return nullptr;
        for (unsigned i = hash(key), n = 0; n < Capacity; i = (i + 1) & (Capacity - 1), ++n) {
            if (!entries[i].key) return nullptr;
            if (entries[i].key == key) return &entries[i];
        }
        return nullptr;
    }
    // true when stored; `added` (optional) tells a new entry from an overwrite.
    bool insert(const void* texture, std::uint64_t identity, std::uint8_t lane, std::uint8_t slot,
                bool* added = nullptr) noexcept {
        if (added) *added = false;
        const auto key = reinterpret_cast<std::uintptr_t>(texture);
        if (!key) return false;
        if (Entry* e = find(texture)) {
            e->identity = identity;
            e->lane = lane;
            e->slot = slot;
            return true;
        }
        if (count >= Capacity / 2) { // half full: the probe runs stay short
            ++refused_full;
            return false;
        }
        for (unsigned i = hash(key);; i = (i + 1) & (Capacity - 1))
            if (!entries[i].key) {
                entries[i] = {key, identity, lane, slot};
                ++count;
                live_marks.fetch_add(1, std::memory_order_relaxed);
                if (added) *added = true;
                return true;
            }
    }
    ~Table() {
        if (count) live_marks.fetch_sub(count, std::memory_order_relaxed);
    }
    void erase(const void* texture) noexcept {
        Entry* e = find(texture);
        if (!e) return;
        *e = {};
        --count;
        live_marks.fetch_sub(1, std::memory_order_relaxed);
        // Re-insert the rest of the probe run so no lookup stops at the hole.
        unsigned i = unsigned(e - entries);
        for (unsigned j = (i + 1) & (Capacity - 1); entries[j].key; j = (j + 1) & (Capacity - 1)) {
            const Entry moved = entries[j];
            entries[j] = {};
            --count;
            live_marks.fetch_sub(1, std::memory_order_relaxed); // the re-insert below adds it back
            insert(reinterpret_cast<const void*>(moved.key), moved.identity, moved.lane, moved.slot);
        }
    }
};
} // namespace x3m::lod_texture_mark
