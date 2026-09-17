#pragma once
// Lane-independent caster-candidate counter of the motion route
// (docs/architecture/shadow-replay-gates.md, section 3; X3M_SHADOW_REPLAY_CANDIDATES=1).
// CPU bookkeeping only: fixed storage, no allocation, no D3D or COM access.
// The route feeds one draw() per routed draw and compares the recorded
// buffer-lock bookends (src/ownership/buffer_lock_observation.h) at scene end.
#include <cstdint>
#include <cstring>
#include "../ownership/buffer_lock_observation.h"

namespace x3m::shadow_replay {
constexpr unsigned record_capacity = 512;  // storage; the per-frame cap (X3M_SHADOW_REPLAY_CAP, default 512) is at most this
constexpr unsigned witness_capacity = 16;  // per device, section 3
constexpr float slice0_near = 6.f, slice0_far = 250.f; // own-ship slice of cascade 0 (origin distance, view units)
constexpr unsigned extent_reads_per_frame = 32;               // vertex-extent reads queued per frame (one per unseen buffer range)
constexpr std::uint64_t extent_read_bytes_per_frame = 1u << 20; // soft byte budget of those reads: the read that crosses it is the frame's last
constexpr unsigned extent_read_attempts = 8;                  // scene ends a range may be found locked or fail to Lock before it is given up

// Pool/usage class of a bound buffer, from the documented GetDesc of the
// application's own SetStreamSource/SetIndices argument (cached per allocation).
enum class PoolClass : std::uint8_t { Unknown = 0, Managed = 1, Dynamic = 2, Other = 3 };
inline PoolClass classify_pool(std::uint32_t pool, std::uint32_t usage) noexcept {
    if (usage & 0x200u) return PoolClass::Dynamic;        // D3DUSAGE_DYNAMIC
    return pool == 1u ? PoolClass::Managed : PoolClass::Other; // D3DPOOL_MANAGED
}
// Direct-mapped cache of allocation id -> class; a miss returns Unknown and
// the caller classifies from GetDesc while the buffer pointer is live.
struct PoolCache {
    static constexpr unsigned size = 128;
    std::uint64_t ids[size]{};
    PoolClass classes[size]{};
    PoolClass find(std::uint64_t id) const noexcept {
        const unsigned slot = unsigned(id % size);
        return id && ids[slot] == id ? classes[slot] : PoolClass::Unknown;
    }
    void store(std::uint64_t id, PoolClass value) noexcept {
        if (!id) return;
        const unsigned slot = unsigned(id % size);
        ids[slot] = id; classes[slot] = value;
    }
};

struct FrameCounts {
    std::uint32_t routed = 0, zwrite = 0, slice0 = 0, managed = 0;
    std::uint32_t bounds = 0, origin = 0, fallback = 0; // slice0 = bounds (extent meets the map box) + fallback (no extent yet: origin rule); origin: the origin rule alone
    std::uint32_t capped = 0, reads = 0;                // managed candidates beyond the per-frame cap; extent reads performed at the scene end
    std::uint32_t dynamic = 0, default_pool = 0, excluded = 0, unknown = 0;
    std::uint32_t shadow_mismatch = 0; // slice-0 draw whose shadowed binding ids differ from the route key
    std::uint32_t leased = 0, serial_changed = 0, readonly_after = 0, writable_after = 0;
    std::uint32_t pending = 0, in_flight = 0, quiet = 0, cold_thread = 0;
    std::uint32_t stale = 0;           // record whose scene-end view is another allocation or generation
    std::uint32_t nested = 0, overflow = 0;
    std::uint64_t roots = 0, waiting = 0;
};
struct Record {
    std::uint64_t vb = 0, ib = 0;                 // route allocation ids (RigidDrawKey)
    std::uintptr_t vb_identity = 0, ib_identity = 0; // wrapper identities: registry keys only, never dereferenced
    std::uint64_t vb_generation = 0, ib_generation = 0; // BufferLockView::generation at the draw (changes before every Reset)
    ownership::BufferLockObservation vb_view{}, ib_view{}; // bookend views at the draw
};
struct Witness {
    std::uint64_t allocation = 0, serial_delta = 0, revision_delta = 0;
    std::uint32_t flags = 0, offset = 0, size = 0, thread = 0;
};
// Verdict of one buffer's bookend pair (draw view versus scene-end view).
struct BufferVerdict {
    bool serial_changed = false, readonly_after = false, writable_after = false;
    bool pending = false, in_flight = false, quiet = false, cold_thread = false, changed = false;
    Witness witness{};
};
inline BufferVerdict compare(const ownership::BufferLockObservation& at_draw,
                             const ownership::BufferLockObservation& at_end, std::uint32_t presenting_thread) noexcept {
    BufferVerdict v{};
    v.serial_changed = at_end.attempt_serial != at_draw.attempt_serial;
    v.writable_after = at_end.writable_attempts != at_draw.writable_attempts;
    v.readonly_after = v.serial_changed && !v.writable_after && at_end.readonly_attempts != at_draw.readonly_attempts;
    v.pending = at_end.pending_locks != 0;
    v.in_flight = at_end.in_flight_locks != 0 || at_end.in_flight_unlocks != 0;
    const bool revision_changed = at_end.revision != at_draw.revision;
    v.quiet = at_end.quiet() && !v.serial_changed && !revision_changed && at_end.unlock_serial == at_draw.unlock_serial;
    v.cold_thread = at_end.attempt_serial != 0 && at_end.last_thread != presenting_thread;
    v.changed = v.serial_changed || revision_changed;
    v.witness.allocation = at_end.allocation_id;
    v.witness.flags = at_end.last_flags; v.witness.offset = at_end.last_offset; v.witness.size = at_end.last_size;
    v.witness.thread = at_end.last_thread;
    v.witness.serial_delta = at_end.attempt_serial - at_draw.attempt_serial;
    v.witness.revision_delta = at_end.revision - at_draw.revision;
    return v;
}

// One frame's population. Reset at the frame begin and after publication.
struct Frame {
    FrameCounts counts{};
    Record records[record_capacity]{};
    unsigned record_count = 0;
    void reset() noexcept { counts = {}; record_count = 0; }
    // Per routed draw (successful native draw only). admitted: the draw is a
    // cascade-0 caster (by_bounds: its vertex extent meets the map box; else
    // the origin rule stood in because no extent is known yet); origin_rule:
    // what the origin rule alone says (statistic). shadow_ok: the shadowed
    // binding ids equal the route key's (else the pool class and bookends
    // would describe another buffer). Returns true when the draw is a managed
    // candidate the caller should record (below the per-frame cap).
    bool draw(bool zwrite, bool admitted, bool by_bounds, bool origin_rule, bool excluded, bool shadow_ok,
              PoolClass vb, PoolClass ib, bool indexed, unsigned cap) noexcept {
        ++counts.routed;
        if (!zwrite) return false;
        ++counts.zwrite;
        if (origin_rule) ++counts.origin;
        if (!admitted) return false;
        ++counts.slice0;
        if (by_bounds) ++counts.bounds; else ++counts.fallback;
        if (excluded) { ++counts.excluded; return false; }
        if (!shadow_ok) { ++counts.shadow_mismatch; return false; }
        const PoolClass second = indexed ? ib : PoolClass::Managed;
        if (vb == PoolClass::Dynamic || second == PoolClass::Dynamic) { ++counts.dynamic; return false; }
        if (vb == PoolClass::Unknown || second == PoolClass::Unknown) { ++counts.unknown; return false; }
        if (vb != PoolClass::Managed || second != PoolClass::Managed) { ++counts.default_pool; return false; }
        ++counts.managed;
        if (record_count >= record_capacity) { ++counts.overflow; return false; }
        if (record_count >= cap) { ++counts.capped; return false; }
        return true;
    }
    Record& record() noexcept { return records[record_count++]; }
};

// ---- vertex extents (docs/architecture/shadow-replay-gates.md, "Casters by bounds") ----
// The object-space AABB of one draw's vertex range, read once per (buffer,
// revision, range, position layout) at a scene end and cached; the draw-time
// box test transforms its eight corners. Fixed storage, no allocation.
struct ExtentKey {
    std::uint64_t vb = 0, revision = 0;
    std::uint32_t stream_offset = 0, stride = 0, first = 0, count = 0, position_offset = 0, position_type = 0;
    bool operator==(const ExtentKey& o) const noexcept {
        return vb == o.vb && revision == o.revision && stream_offset == o.stream_offset && stride == o.stride && first == o.first
            && count == o.count && position_offset == o.position_offset && position_type == o.position_type;
    }
    std::uint64_t hash() const noexcept {
        std::uint64_t h = vb * 0x9E3779B97F4A7C15ull ^ revision;
        h ^= (std::uint64_t(stream_offset) << 32 | stride) * 0xC2B2AE3D27D4EB4Full;
        h ^= (std::uint64_t(first) << 32 | count) * 0x165667B19E3779F9ull;
        h ^= (std::uint64_t(position_offset) << 32 | position_type);
        h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
        return h;
    }
};
// Retry: the range was found with a Lock pending or in flight, or its own
// Lock failed, `attempts` times; its next draw re-queues the read. Unreadable:
// a nonfinite position, or extent_read_attempts retries; never re-read.
enum class ExtentState : std::uint8_t { Empty = 0, Known = 1, Unreadable = 2, Retry = 3 };
struct ExtentEntry { ExtentKey key{}; float lo[3]{}, hi[3]{}; ExtentState state = ExtentState::Empty; std::uint8_t attempts = 0; };
struct ExtentCache {
    static constexpr unsigned size = 1024; // direct-mapped; a collision evicts (the evicted range is re-read on its next draw)
    ExtentEntry entries[size]{};
    // The decided entry (Known or Unreadable); a Retry entry is a miss.
    const ExtentEntry* find(const ExtentKey& key) const noexcept {
        const ExtentEntry& e = entries[unsigned(key.hash() % size)];
        return (e.state == ExtentState::Known || e.state == ExtentState::Unreadable) && e.key == key ? &e : nullptr;
    }
    void store(const ExtentKey& key, const float lo[3], const float hi[3], ExtentState state) noexcept {
        ExtentEntry& e = entries[unsigned(key.hash() % size)];
        e.key = key; e.state = state; e.attempts = 0;
        for (unsigned i = 0; i < 3; ++i) { e.lo[i] = lo ? lo[i] : 0.f; e.hi[i] = hi ? hi[i] : 0.f; }
    }
    // One failed attempt: Retry until extent_read_attempts, then Unreadable.
    void retry(const ExtentKey& key) noexcept {
        ExtentEntry& e = entries[unsigned(key.hash() % size)];
        const unsigned attempts = e.state == ExtentState::Retry && e.key == key ? e.attempts + 1u : 1u;
        e.key = key; e.attempts = std::uint8_t(attempts);
        e.state = attempts >= extent_read_attempts ? ExtentState::Unreadable : ExtentState::Retry;
        for (unsigned i = 0; i < 3; ++i) e.lo[i] = e.hi[i] = 0.f;
    }
    void clear() noexcept { for (auto& e : entries) e.state = ExtentState::Empty; }
};
// Vertex count of a primitive range (D3D9 topology values); 0 for an unknown topology.
inline std::uint32_t vertices_of(std::uint32_t topology, std::uint32_t primitives) noexcept {
    switch (topology) {
    case 1: return primitives;          // POINTLIST
    case 2: return primitives * 2;      // LINELIST
    case 3: return primitives ? primitives + 1 : 0; // LINESTRIP
    case 4: return primitives * 3;      // TRIANGLELIST
    case 5: case 6: return primitives ? primitives + 2 : 0; // TRIANGLESTRIP, TRIANGLEFAN
    default: return 0;
    }
}
// Position element decoders (documented D3DDECLTYPE values): FLOAT3 2, FLOAT4 3, FLOAT16_4 16.
inline bool extent_type_supported(std::uint32_t type) noexcept { return type == 2 || type == 3 || type == 16; }
inline std::uint32_t extent_type_bytes(std::uint32_t type) noexcept { return type == 2 ? 12u : type == 3 ? 16u : type == 16 ? 8u : 0u; }
inline float half_to_float(std::uint16_t h) noexcept {
    const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) bits = sign;
        else { // subnormal: normalize
            std::uint32_t m = mantissa, e = 127 - 15 + 1;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
        }
    } else if (exponent == 31) bits = sign | 0x7F800000u | (mantissa << 13); // inf / nan
    else bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    float out; std::memcpy(&out, &bits, 4); return out;
}
// The AABB of `count` positions at `bytes` (the mapped range's first vertex
// at offset 0, `stride` apart, the element at position_offset). False on a
// nonfinite component or an unsupported type.
inline bool extent_of(const unsigned char* bytes, std::uint32_t stride, std::uint32_t position_offset, std::uint32_t type,
                      std::uint32_t count, float lo[3], float hi[3]) noexcept {
    if (!bytes || !count || !extent_type_supported(type)) return false;
    lo[0] = lo[1] = lo[2] = 3.4028235e38f; hi[0] = hi[1] = hi[2] = -3.4028235e38f;
    for (std::uint32_t v = 0; v < count; ++v) {
        const unsigned char* p = bytes + std::size_t(v) * stride + position_offset;
        float c[3];
        if (type == 16) { std::uint16_t h[3]; std::memcpy(h, p, 6); for (unsigned i = 0; i < 3; ++i) c[i] = half_to_float(h[i]); }
        else std::memcpy(c, p, 12);
        for (unsigned i = 0; i < 3; ++i) {
            if (!(c[i] == c[i]) || c[i] > 3.4028235e38f || c[i] < -3.4028235e38f) return false; // nan or inf
            if (c[i] < lo[i]) lo[i] = c[i];
            if (c[i] > hi[i]) hi[i] = c[i];
        }
    }
    return true;
}
// One queued read: the wrapper is retained by the caller's AddRef until the read.
struct PendingExtent { ExtentKey key{}; std::uintptr_t identity = 0; };
} // namespace x3m::shadow_replay
