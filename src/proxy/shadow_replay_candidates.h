#pragma once
// Lane-independent caster-candidate counter of the motion route
// (docs/architecture/shadow-replay-gates.md, section 3; X3M_SHADOW_REPLAY_CANDIDATES=1).
// CPU bookkeeping only: fixed storage, no allocation, no D3D or COM access.
// The route feeds one draw() per routed draw and compares the recorded
// buffer-lock bookends (src/ownership/buffer_lock_observation.h) at scene end.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "../ownership/buffer_lock_observation.h"

namespace x3m::shadow_replay {
constexpr unsigned record_capacity = 1024; // inline storage (fixed arrays in MotionOutput, no allocation); the per-frame cap is at most this unless a cascade set asks for more records
constexpr unsigned record_capacity_max = 4096; // X3M_SHADOW_CASCADE_RECORDS: the largest per-cascade record capacity; storage beyond record_capacity is allocated once at attach
constexpr unsigned default_cap = 512;       // X3M_SHADOW_REPLAY_CAP default (1..record_capacity): managed candidates recorded and replayed per frame
constexpr unsigned cascade_capacity = 5;    // = renderer::shadow_cascade_max (docs/architecture/shadow-cascades.md); this header stays D3D- and renderer-free
constexpr unsigned witness_capacity = 16;  // per device, section 3
constexpr float slice0_near = 6.f, slice0_far = 250.f; // own-ship slice of cascade 0 (origin distance, view units)
constexpr unsigned extent_reads_per_frame = 32;               // vertex-extent reads queued per frame (one per unseen buffer range)
constexpr std::uint64_t extent_read_bytes_per_frame = 1u << 20; // soft byte budget of those reads: the read that crosses it is the frame's last
constexpr unsigned extent_read_attempts = 8;                  // scene ends a range may be found locked or fail to Lock before it is given up
// A range whose buffer revision moved answers with its previous extent only
// this long. Its re-read goes to the front of the frame's read queue, so an
// unread revision after eight scene ends means the buffer is rewritten every
// frame or cannot be read (the same eight ends extent_read_attempts allows);
// from then on the previous extent is not trusted as it is: it is doubled about
// its centre (a re-uploaded or animated mesh of the same vertex range stays
// inside that) and the verdict is reported as `inflated`.
constexpr unsigned extent_stale_frames = 8;
// Diagnostic lines the minimum-footprint gate may write per device: one per
// distinct resolved law (the set's ladder commits, an FOV change, a Reset).
constexpr unsigned footprint_line_max = 16;

// The identity of one caster draw across frames: the node's lifetime serial
// and the draw's vertex range (a node's parts carry their own rows). Never 0.
inline std::uint64_t caster_key(std::uint64_t serial, std::uint64_t vb, std::uint32_t first, std::int32_t base_vertex, std::uint32_t count) noexcept {
    std::uint64_t h = serial * 0x9E3779B97F4A7C15ull;
    h ^= vb * 0xC2B2AE3D27D4EB4Full;
    h ^= (std::uint64_t(first) << 32 | std::uint32_t(base_vertex)) * 0x165667B19E3779F9ull;
    h ^= std::uint64_t(count) * 0x27D4EB2F165667C5ull;
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return h ? h : 1u;
}
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
    // Cascades on: records carrying cascade i (c<i>=) and admitted draws whose
    // cascade i was dropped by that cascade's cap (capped<i>=); `capped` then
    // counts the draws every one of whose cascades was dropped.
    std::uint32_t cascade[cascade_capacity]{}, cascade_capped[cascade_capacity]{};
    // Static-only cascades (X3M_SHADOW_CASCADE_STATIC_FROM): draws whose cascade i
    // was refused because the caster is not classified static (static_only_refused<i>=),
    // and how the frame's classifications were decided (shadow_caster_class.h).
    std::uint32_t static_only_refused[cascade_capacity]{};
    std::uint32_t large_admitted[cascade_capacity]{}; // ... and moving draws admitted to cascade i by their extent (X3M_SHADOW_CASCADE_LARGE_MIN)
    // Minimum light-space footprint (X3M_SHADOW_CASCADE_MIN_FOOTPRINT;
    // renderer/shadow_cascade_footprint_core.h): live draws whose cascade i was
    // dropped because their lateral sun-space footprint is below that cascade's
    // threshold (footprint_refused<i>=), and the same for the retention store's
    // re-issued records (footprint_aged<i>=, copied from the store's frame stats).
    std::uint32_t footprint_refused[cascade_capacity]{};
    std::uint32_t class_store = 0, class_ring = 0; // draws classified by the retention store's verdict; by the ring's anchor
    std::uint32_t class_miss[cascade_capacity]{}; // draws refused from cascade i with no anchor at all (first sighting, evicted, no serial or rows): the ring's limit, per cascade
    // Importance drop order (X3M_SHADOW_CASCADE_DROP_ORDER=importance): per cascade the
    // largest projected size among the casters its cap dropped this frame (dropped_min_size<i>=;
    // 0 while nothing was dropped) and the selection's cost.
    float dropped_size[cascade_capacity]{};
    double select_us = 0;
};
struct Record {
    std::uint64_t vb = 0, ib = 0;                 // route allocation ids (RigidDrawKey)
    std::uintptr_t vb_identity = 0, ib_identity = 0; // wrapper identities: registry keys only, never dereferenced
    std::uint64_t vb_generation = 0, ib_generation = 0; // BufferLockView::generation at the draw (changes before every Reset)
    ownership::BufferLockObservation vb_view{}, ib_view{}; // bookend views at the draw
    std::uint64_t serial = 0; // the node's lifetime serial (0 unknown): the importance order's tie-break
    std::uint64_t key = 0;    // caster_key of the draw: the importance order's kept-last-frame identity
    float size = 0.f;         // projected size at the camera (renderer::shadow_cascade_projected_size; 0 without an extent)
    std::uint8_t cascades = 1; // bit i: the draw is replayed into cascade i (the single map is cascade 0)
    std::uint8_t verdict = 0;  // VerdictSource: what admitted the draw (capture-frame diagnostics)
};
// What decided a record's admission: the draw-time box test on the range's own
// extent, on the extent of an earlier buffer revision (the new one is queued
// with priority), on that extent doubled once it is older than
// extent_stale_frames, or the origin rule while no extent is known.
enum class VerdictSource : std::uint8_t { Origin = 0, Bounds = 1, Retained = 2, Inflated = 3 };
constexpr const char* verdict_source_name(std::uint8_t v) noexcept { return v == 1 ? "bounds" : v == 2 ? "retained" : v == 3 ? "inflated" : "origin"; }
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

// The importance order's memory of the previous frame's kept casters: an
// open-addressed table of caster keys with the cascade mask each was kept
// in, refilled at every scene end (owner storage, 2 slots per record).
struct KeptEntry { std::uint64_t key = 0; std::uint8_t mask = 0; };
constexpr float importance_hysteresis = .8f; // a caster kept last frame stays kept while its size >= this x the cascade's cutoff
// Cascade-membership flip counter (docs/architecture/shadow-caster-retention.md,
// "Membership flips"): the previous frame's cascade mask of every caster key,
// so the frame line can report, per cascade, how many casters entered or left
// it since the previous frame (flip_c<k>=) and how many flipped on each of the
// last two frames (period2_c<k>=: the period-2 blink signature). Fixed owner
// storage, refreshed in place once per frame; no allocation and no per-draw
// work beyond the key the record already carries.
//
// `stamp` is the frame the entry was last written on. An entry not written on
// the previous frame contributes mask 0 and no flip history (a caster that was
// away for two frames and returns counts one flip, not a period-2 blink). A
// slot older than `flip_stale_frames` is dead and is reused by the next key
// that probes over it; slots are never emptied, so probe chains stay intact.
//
// Two frames are not comparable and are seeded instead of counted (`reset`,
// reported as flip_reset=1 with every count zero): the first frame of a table,
// a frame that does not directly follow the previously counted one (the scene
// ends stopped: a menu, a load, the A/B off), and a frame whose cascade shape
// changed (count, or the adaptive ladder's active mask: a dropped cascade
// takes its bit from every caster, which is a configuration change, not a
// blink). A key that does not fit its probe chain is counted in `untracked`
// (reported as flip_untracked=): an undercount is never silent.
struct FlipEntry {
    std::uint64_t key = 0;
    std::uint32_t stamp = 0;         // frame `mask`/`flipped` describe
    std::uint32_t pending_stamp = 0; // frame `pending` was accumulated on
    std::uint8_t mask = 0, flipped = 0, pending = 0;
};
constexpr unsigned flip_probe_limit = 16;      // bounded probe: a key that does not fit its chain is not tracked this frame
constexpr std::uint32_t flip_stale_frames = 2; // an entry unseen this long is reusable (its bits have already been reported leaving)
struct FlipTable {
    FlipEntry* entries = nullptr; unsigned slots = 0;
    std::uint32_t untracked = 0;   // casters the probe limit could not place this frame
    bool reset = false;            // this frame was seeded, not counted
    void attach(FlipEntry* storage, unsigned count) noexcept {
        entries = storage; slots = storage ? count : 0;
        untracked = 0; reset = false; primed_ = false; last_frame_ = 0; last_cascades_ = 0; last_active_ = 0;
        for (unsigned i = 0; i < slots; ++i) entries[i] = FlipEntry{};
    }
    // The frame's flips from the records' final cascade masks: two passes over
    // fixed storage (the records' keys, then the slots), one mask compare and a
    // counter increment per caster. `flips` and `period2` take `cascades`
    // entries; `active` is the set's active-cascade mask. `frame_number` must
    // increase. A seeded frame answers all zeros with `reset` set.
    void update(const Record* records, unsigned count, unsigned cascades, unsigned active, std::uint64_t frame_number,
                std::uint32_t* flips, std::uint32_t* period2) noexcept {
        for (unsigned k = 0; k < cascades; ++k) flips[k] = period2[k] = 0;
        untracked = 0; reset = false;
        if (!entries || !slots || !cascades) return;
        const std::uint32_t frame = std::uint32_t(frame_number);
        const std::uint8_t bits = std::uint8_t(cascades >= 8 ? 0xFFu : (1u << cascades) - 1u);
        // Comparable with the previous counted frame?
        reset = !primed_ || last_frame_ + 1 != frame || last_cascades_ != cascades || last_active_ != active;
        // This frame's mask per key (two records of one key contribute their union).
        for (unsigned r = 0; r < count; ++r) {
            const std::uint64_t key = records[r].key;
            if (!key) continue;
            const std::uint8_t mask = std::uint8_t(records[r].cascades & bits);
            FlipEntry* found = nullptr; FlipEntry* reusable = nullptr;
            for (unsigned probe = 0, i = unsigned(key % slots); probe < flip_probe_limit; ++probe, i = i + 1 == slots ? 0 : i + 1) {
                FlipEntry& e = entries[i];
                if (e.key == key) { found = &e; break; }
                if (!e.key) { if (!reusable) reusable = &e; break; }
                if (!reusable && frame - e.stamp > flip_stale_frames && frame - e.pending_stamp > flip_stale_frames) reusable = &e;
            }
            if (!found && !reusable) { ++untracked; continue; } // chain full: this caster is not tracked this frame
            if (!found) { *reusable = FlipEntry{}; reusable->key = key; found = reusable; }
            found->pending = found->pending_stamp == frame ? std::uint8_t(found->pending | mask) : mask;
            found->pending_stamp = frame;
        }
        // One pass over the slots: the casters seen this frame against their
        // previous mask, and the ones that left (mask 0 now) beside them.
        for (unsigned i = 0; i < slots; ++i) {
            FlipEntry& e = entries[i];
            if (!e.key) continue;
            const std::uint8_t now = e.pending_stamp == frame ? std::uint8_t(e.pending & bits) : 0;
            if (reset) { // seeded: this frame becomes the baseline, nothing is counted
                if (!now && e.stamp + 1 != frame && e.stamp != frame) continue; // stale: left to age out of its slot
                e.mask = now; e.flipped = 0; e.stamp = frame;
                continue;
            }
            const bool fresh = e.stamp + 1 == frame; // the entry describes the previous frame: its mask and flips carry over
            const std::uint8_t previous = fresh ? std::uint8_t(e.mask & bits) : 0;
            const std::uint8_t before = fresh ? e.flipped : 0; // the bits that flipped on the previous frame
            if (!now && !previous) continue;                   // absent both frames: left to age out of its slot
            const std::uint8_t changed = std::uint8_t(previous ^ now);
            for (unsigned k = 0; k < cascades; ++k) if (changed >> k & 1u) { ++flips[k]; if (before >> k & 1u) ++period2[k]; }
            e.mask = now; e.flipped = changed; e.stamp = frame;
        }
        primed_ = true; last_frame_ = frame; last_cascades_ = cascades; last_active_ = active;
    }

private:
    std::uint32_t last_frame_ = 0;
    unsigned last_cascades_ = 0, last_active_ = 0;
    bool primed_ = false;
};

// One frame's population. Reset at the frame begin and after publication.
// `records` is the inline array unless the owner attached larger storage
// (attach-time allocation for a cascade set with more than record_capacity
// records); never copied (the pointer would dangle).
struct Frame {
    FrameCounts counts{};
    Record inline_records[record_capacity]{};
    Record* records = inline_records;
    unsigned capacity = record_capacity;
    unsigned record_count = 0;
    KeptEntry* kept_last = nullptr; unsigned kept_slots = 0; // the owner's table (importance order on); persists across frames
    void attach_kept(KeptEntry* table, unsigned slots) noexcept { kept_last = table; kept_slots = slots; if (table) for (unsigned i = 0; i < slots; ++i) table[i] = KeptEntry{}; }
    std::uint8_t kept_mask(std::uint64_t key) const noexcept {
        if (!kept_last || !kept_slots) return 0;
        for (unsigned probe = 0, i = unsigned(key % kept_slots); probe < kept_slots; ++probe, i = i + 1 == kept_slots ? 0 : i + 1) {
            if (kept_last[i].key == key) return kept_last[i].mask;
            if (!kept_last[i].key) return 0;
        }
        return 0;
    }
    void remember_kept() noexcept {
        if (!kept_last || !kept_slots) return;
        for (unsigned i = 0; i < kept_slots; ++i) kept_last[i] = KeptEntry{};
        for (unsigned r = 0; r < record_count; ++r) {
            const Record& rec = records[r];
            if (!rec.cascades || !rec.key) continue;
            for (unsigned probe = 0, i = unsigned(rec.key % kept_slots); probe < kept_slots; ++probe, i = i + 1 == kept_slots ? 0 : i + 1) {
                if (!kept_last[i].key || kept_last[i].key == rec.key) { kept_last[i].key = rec.key; kept_last[i].mask |= rec.cascades; break; }
            }
        }
    }
    Frame() noexcept = default;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    void attach_storage(Record* storage, unsigned storage_capacity) noexcept {
        records = storage && storage_capacity > record_capacity ? storage : inline_records;
        capacity = records == inline_records ? record_capacity : storage_capacity;
    }
    void reset() noexcept { counts = {}; record_count = 0; }
    // Per routed draw (successful native draw only). admitted: the draw is a
    // cascade-0 caster (by_bounds: its vertex extent meets the map box; else
    // the origin rule stood in because no extent is known yet); origin_rule:
    // what the origin rule alone says (statistic). shadow_ok: the shadowed
    // binding ids equal the route key's (else the pool class and bookends
    // would describe another buffer). Returns true when the draw is a managed
    // candidate the caller should record (below the per-frame cap).
    // Cascades on: *cascade_mask is the draw's cascades on entry (admitted ==
    // mask != 0) and the cascades whose own cap still has room on return; the
    // counts move when the record is made (record(mask)).
    bool draw(bool zwrite, bool admitted, bool by_bounds, bool origin_rule, bool excluded, bool shadow_ok,
              PoolClass vb, PoolClass ib, bool indexed, unsigned cap, std::uint8_t* cascade_mask = nullptr, const unsigned* cascade_caps = nullptr) noexcept {
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
        if (record_count >= capacity) { ++counts.overflow; return false; }
        if (record_count >= cap) { ++counts.capped; return false; }
        if (cascade_mask && cascade_caps) {
            std::uint8_t kept = 0;
            for (unsigned i = 0; i < cascade_capacity; ++i) {
                if (!(*cascade_mask & (1u << i))) continue;
                if (counts.cascade[i] >= cascade_caps[i]) ++counts.cascade_capped[i]; else kept |= std::uint8_t(1u << i);
            }
            *cascade_mask = kept;
            if (!kept) { ++counts.capped; return false; }
        }
        return true;
    }
    Record& record(std::uint8_t cascade_mask = 1) noexcept {
        Record& r = records[record_count++];
        r.cascades = cascade_mask;
        return r;
    }
    // Cascades on: the per-cascade record counts (the caps' ledger).
    void count_cascades(std::uint8_t cascade_mask) noexcept {
        for (unsigned i = 0; i < cascade_capacity; ++i) if (cascade_mask & (1u << i)) ++counts.cascade[i];
    }
    // Importance drop order (docs/architecture/shadow-cascade-extents.md, "Caster
    // pool control"): at the scene end, a cascade whose records exceed its cap
    // keeps the `cap` largest projected casters; the order is size descending,
    // then node serial ascending, then record index ascending, so the kept set
    // is a function of the frame's casters and not of their submission order.
    // Hysteresis at the cap boundary: a caster this cascade kept last frame
    // (kept_last) ranks above the rest while its size is at least
    // importance_hysteresis x the cutoff (the smallest size the plain order
    // would keep), still bounded by the cap, so near-equal sizes do not flip
    // between frames. Two nth_elements over the cascade's records (`scratch`:
    // capacity indices, the owner's); the drop moves the bit off the record and
    // counts as the draw-time cap does (cascade_capped<i>, then `capped` for a
    // record left without a cascade: the owner compacts those out with
    // `compact`). dropped_size<i>: the largest size among the dropped (what
    // popping costs). The frame's final kept set is remembered at the end.
    void select_cascades(const unsigned* caps, unsigned cascades, std::uint16_t* scratch) noexcept {
        if (!caps || !scratch) return;
        for (unsigned k = 0; k < cascades && k < cascade_capacity; ++k) {
            counts.dropped_size[k] = 0.f;
            if (counts.cascade[k] <= caps[k]) continue;
            unsigned n = 0;
            for (unsigned i = 0; i < record_count; ++i) if (records[i].cascades & (1u << k)) scratch[n++] = std::uint16_t(i);
            const Record* r = records;
            const auto better = [r](std::uint16_t a, std::uint16_t b) noexcept {
                if (r[a].size != r[b].size) return r[a].size > r[b].size;
                if (r[a].serial != r[b].serial) return r[a].serial < r[b].serial;
                return a < b;
            };
            const unsigned keep = caps[k];
            std::nth_element(scratch, scratch + keep, scratch + n, better);
            if (kept_last && kept_slots) {
                float boundary = r[scratch[0]].size; // the plain order's smallest kept size
                for (unsigned q = 1; q < keep; ++q) if (r[scratch[q]].size < boundary) boundary = r[scratch[q]].size;
                const float cutoff = boundary * importance_hysteresis;
                const std::uint8_t bit = std::uint8_t(1u << k);
                const auto sticky = [this, r, cutoff, bit](std::uint16_t a) noexcept { return r[a].size >= cutoff && (kept_mask(r[a].key) & bit) != 0; };
                bool any = false;
                for (unsigned q = keep; q < n && !any; ++q) any = sticky(scratch[q]); // a dropped caster that was kept: re-rank with the priority
                if (any) std::nth_element(scratch, scratch + keep, scratch + n, [&](std::uint16_t a, std::uint16_t b) noexcept {
                    const bool sa = sticky(a), sb = sticky(b);
                    return sa != sb ? sa : better(a, b);
                });
            }
            float largest = 0.f;
            for (unsigned q = keep; q < n; ++q) {
                Record& d = records[scratch[q]];
                d.cascades = std::uint8_t(d.cascades & ~(1u << k));
                if (d.size > largest) largest = d.size;
                ++counts.cascade_capped[k];
            }
            counts.cascade[k] = keep;
            counts.dropped_size[k] = largest;
        }
        remember_kept();
    }
    // Removes the records left without a cascade (stable). `dropped(i)` is
    // called for each such record before it goes; `moved(from, to)` for each
    // surviving record that changes index, so the owner's parallel arrays follow.
    template <class Dropped, class Moved> unsigned compact(Dropped dropped, Moved moved) noexcept {
        unsigned w = 0;
        for (unsigned i = 0; i < record_count; ++i) {
            if (!records[i].cascades) { dropped(i); ++counts.capped; --counts.leased; continue; }
            if (w != i) { records[w] = records[i]; moved(i, w); }
            ++w;
        }
        const unsigned removed = record_count - w;
        record_count = w;
        return removed;
    }
};

// ---- alpha-tested casters (docs/architecture/shadow-replay-gates.md, "Alpha-tested casters") ----
// X3M_SHADOW_ALPHA_CASTERS: what an alpha-tested routed draw's own alpha test
// (the documented D3DRS_ALPHAFUNC / D3DRS_ALPHAREF values) makes of it as a
// caster. Opaque: every pixel passes (ALWAYS, or GREATEREQUAL / GREATER below
// the lowest alpha), cast with the depth-only program. Tested: cast with the
// alpha program, discarding where the sampled alpha is below `threshold`
// (GREATEREQUAL ref: ref / 255; GREATER ref: half a code above it, so an
// 8-bit texel equal to ref is discarded). Refused: NEVER draws nothing, and
// LESS, EQUAL, LESSEQUAL and NOTEQUAL keep low alpha, which one threshold
// cannot express: no caster. The reference is the low 8 bits (the documented
// range 0x00..0xFF).
enum class AlphaCaster : std::uint8_t { Opaque = 0, Tested = 1, Refused = 2 };
inline AlphaCaster alpha_caster(std::uint32_t func, std::uint32_t ref, float& threshold) noexcept {
    threshold = 0.f;
    const std::uint32_t code = ref & 0xFFu;
    switch (func) {
    case 8: return AlphaCaster::Opaque;                                          // D3DCMP_ALWAYS
    case 7: if (!code) return AlphaCaster::Opaque; threshold = float(code) / 255.f; return AlphaCaster::Tested;  // GREATEREQUAL
    case 5: threshold = (float(code) + .5f) / 255.f; return AlphaCaster::Tested; // GREATER
    default: return AlphaCaster::Refused;                                        // NEVER, LESS, EQUAL, LESSEQUAL, NOTEQUAL, unknown
    }
}
// Per-frame counts of the alpha-tested draws that reached the candidate
// decision with the option on (the shadow_alpha_casters line).
struct AlphaCasterCounts {
    std::uint32_t seen = 0, tested = 0, opaque = 0; // reached the decision; admitted with the alpha program; admitted as opaque
    std::uint32_t state = 0, function = 0, uv = 0, texture = 0, pool = 0; // refused: state read failed; the comparison; no stream-0 TEXCOORD0; no 2D stage-0 texture; texture not D3DPOOL_MANAGED
};

// ---- vertex extents (docs/architecture/shadow-replay-gates.md, "Casters by bounds") ----
// The object-space AABB of one draw's vertex range, read once per (buffer,
// revision, range, position layout) at a scene end and cached; the draw-time
// box test transforms its eight corners. Fixed storage, no allocation.
struct ExtentKey {
    std::uint64_t vb = 0, revision = 0;
    std::uint32_t stream_offset = 0, stride = 0, first = 0, count = 0, position_offset = 0, position_type = 0;
    // The same vertex range and position layout, whatever the buffer revision.
    bool same_range(const ExtentKey& o) const noexcept {
        return vb == o.vb && stream_offset == o.stream_offset && stride == o.stride && first == o.first
            && count == o.count && position_offset == o.position_offset && position_type == o.position_type;
    }
    bool operator==(const ExtentKey& o) const noexcept { return revision == o.revision && same_range(o); }
    // Of the range alone: every revision of a range lives in one set, so a
    // rewritten buffer finds its previous extent while the new one is read.
    std::uint64_t hash() const noexcept {
        std::uint64_t h = vb * 0x9E3779B97F4A7C15ull;
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
struct ExtentEntry {
    ExtentKey key{}; float lo[3]{}, hi[3]{}; ExtentState state = ExtentState::Empty; std::uint8_t attempts = 0;
    std::uint32_t used = 0; // frame stamp of the last lookup that returned this entry
    // While the entry answers stale: the revision being waited for (failed
    // reads of it are counted in `attempts`; a further revision restarts both)
    // and the frame it was first asked for.
    std::uint64_t pending_revision = 0;
    std::uint32_t stale_since = 0;
    // The read of the pending revision was given up after extent_read_attempts: not queued again.
    bool abandoned() const noexcept { return attempts >= extent_read_attempts; }
    // Doubled about its centre (the answer of a stale entry older than extent_stale_frames).
    void inflated(float out_lo[3], float out_hi[3]) const noexcept {
        for (unsigned i = 0; i < 3; ++i) { const float half = .5f * (hi[i] - lo[i]); out_lo[i] = lo[i] - half; out_hi[i] = hi[i] + half; }
    }
};
// Set-associative, sized for eight times the record capacity (at 1,024 live
// ranges a set of eight overflows about once in a million sets)
// (docs/verification/directional-shadows.md, "Run 38 A (run111) diagnosis",
// cause 2: the direct-mapped cache evicted colliding ranges every frame, so a
// frozen scene's admitted set cycled and READONLY locks never stopped). An
// entry returned by a lookup this frame is never evicted this frame; a store
// into a set whose every way was used this frame is refused and counted
// (the range is re-queued by its next draw). A range whose buffer revision
// moved keeps its previous extent as the `stale` answer until the new one is
// read, so its verdict does not drop to the origin rule in between; the stale
// answer is bounded: stale_age() beyond extent_stale_frames asks the caller to
// inflate it, and its re-read is a priority read.
struct ExtentCache {
    static constexpr unsigned sets = 1024, ways = 8, size = sets * ways;
    ExtentEntry entries[size]{};
    std::uint32_t stamp = 1;   // the current frame
    std::uint32_t refused = 0;       // stores refused since clear()
    std::uint32_t refused_frame = 0; // ... since begin_frame()
    void begin_frame() noexcept {
        refused_frame = 0;
        if (++stamp == 0) { stamp = 1; for (auto& e : entries) { e.used = 0; e.stale_since = 0; } }
    }
    // Frames the entry has been answering for a revision it does not hold.
    std::uint32_t stale_age(const ExtentEntry& e) const noexcept { return stamp - e.stale_since; }
    ExtentEntry* set_of(const ExtentKey& key) noexcept { return entries + std::size_t(key.hash() % sets) * ways; }
    const ExtentEntry* set_of(const ExtentKey& key) const noexcept { return entries + std::size_t(key.hash() % sets) * ways; }
    // The decided entry of this exact key (Known or Unreadable); a Retry
    // entry is a miss. `stale`: on a miss, the Known extent of the same range
    // at another revision, if any.
    const ExtentEntry* find(const ExtentKey& key, const ExtentEntry** stale = nullptr) noexcept {
        if (stale) *stale = nullptr;
        ExtentEntry* set = set_of(key);
        for (unsigned w = 0; w < ways; ++w) {
            ExtentEntry& e = set[w];
            if (e.state == ExtentState::Empty || !e.key.same_range(key)) continue;
            e.used = stamp;
            if (e.key.revision == key.revision) return e.state == ExtentState::Retry ? nullptr : &e;
            if (stale && e.state == ExtentState::Known) {
                if (e.pending_revision != key.revision) { e.pending_revision = key.revision; e.attempts = 0; e.stale_since = stamp; }
                *stale = &e;
            }
        }
        return nullptr;
    }
    // The way a key goes to: its own range's entry, an empty way, else the
    // least recently used way that no lookup returned this frame; null when
    // every way was used this frame.
    ExtentEntry* way_for(const ExtentKey& key) noexcept {
        ExtentEntry* set = set_of(key); ExtentEntry* empty = nullptr; ExtentEntry* oldest = nullptr;
        for (unsigned w = 0; w < ways; ++w) {
            ExtentEntry& e = set[w];
            if (e.state == ExtentState::Empty) { if (!empty) empty = &e; continue; }
            if (e.key.same_range(key)) return &e;
            if (e.used != stamp && (!oldest || e.used < oldest->used)) oldest = &e;
        }
        return empty ? empty : oldest;
    }
    bool store(const ExtentKey& key, const float lo[3], const float hi[3], ExtentState state) noexcept {
        ExtentEntry* e = way_for(key);
        if (!e) { ++refused; ++refused_frame; return false; }
        e->key = key; e->state = state; e->attempts = 0; e->used = stamp; e->pending_revision = 0; e->stale_since = 0;
        for (unsigned i = 0; i < 3; ++i) { e->lo[i] = lo ? lo[i] : 0.f; e->hi[i] = hi ? hi[i] : 0.f; }
        return true;
    }
    // One failed attempt: Retry until extent_read_attempts, then Unreadable. A
    // range that still has a Known extent of an earlier revision keeps it (the
    // stale, later inflated, answer) while the attempts are counted on it; at
    // extent_read_attempts the read is abandoned and the entry stays.
    void retry(const ExtentKey& key) noexcept {
        ExtentEntry* e = way_for(key);
        if (!e) { ++refused; ++refused_frame; return; }
        const bool same = e->state != ExtentState::Empty && e->key.same_range(key);
        const bool kept = same && e->state == ExtentState::Known && e->key.revision != key.revision;
        if (kept) {
            if (e->pending_revision != key.revision) { e->pending_revision = key.revision; e->attempts = 0; e->stale_since = stamp; }
            if (e->attempts < extent_read_attempts) ++e->attempts;
            e->used = stamp;
            return;
        }
        const unsigned attempts = same && e->state == ExtentState::Retry && e->key.revision == key.revision ? e->attempts + 1u : 1u;
        e->attempts = std::uint8_t(attempts); e->used = stamp; e->pending_revision = 0; e->stale_since = 0;
        e->key = key;
        e->state = attempts >= extent_read_attempts ? ExtentState::Unreadable : ExtentState::Retry;
        for (unsigned i = 0; i < 3; ++i) e->lo[i] = e->hi[i] = 0.f;
    }
    void clear() noexcept { for (auto& e : entries) { e.state = ExtentState::Empty; e.used = 0; e.stale_since = 0; e.pending_revision = 0; } stamp = 1; refused = refused_frame = 0; }
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
