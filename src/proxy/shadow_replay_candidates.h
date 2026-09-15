#pragma once
// Lane-independent caster-candidate counter of the motion route
// (docs/architecture/shadow-replay-gates.md, section 3; X3M_SHADOW_REPLAY_CANDIDATES=1).
// CPU bookkeeping only: fixed storage, no allocation, no D3D or COM access.
// The route feeds one draw() per routed draw and compares the recorded
// buffer-lock bookends (src/ownership/buffer_lock_observation.h) at scene end.
#include <cstdint>
#include "../ownership/buffer_lock_observation.h"

namespace x3m::shadow_replay {
constexpr unsigned record_capacity = 64;   // section 2: cap the record array at 64, report overflow
constexpr unsigned witness_capacity = 16;  // per device, section 3
constexpr float slice0_near = 6.f, slice0_far = 250.f; // own-ship slice of cascade 0 (origin distance, view units)

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
    // Per routed draw (successful native draw only). shadow_ok: the shadowed
    // binding ids equal the route key's (else the pool class and bookends
    // would describe another buffer). Returns true when the draw is a managed
    // candidate the caller should record (below capacity).
    bool draw(bool zwrite, bool in_slice, bool excluded, bool shadow_ok, PoolClass vb, PoolClass ib, bool indexed) noexcept {
        ++counts.routed;
        if (!zwrite) return false;
        ++counts.zwrite;
        if (!in_slice) return false;
        ++counts.slice0;
        if (excluded) { ++counts.excluded; return false; }
        if (!shadow_ok) { ++counts.shadow_mismatch; return false; }
        const PoolClass second = indexed ? ib : PoolClass::Managed;
        if (vb == PoolClass::Dynamic || second == PoolClass::Dynamic) { ++counts.dynamic; return false; }
        if (vb == PoolClass::Unknown || second == PoolClass::Unknown) { ++counts.unknown; return false; }
        if (vb != PoolClass::Managed || second != PoolClass::Managed) { ++counts.default_pool; return false; }
        ++counts.managed;
        if (record_count >= record_capacity) { ++counts.overflow; return false; }
        return true;
    }
    Record& record() noexcept { return records[record_count++]; }
};
} // namespace x3m::shadow_replay
