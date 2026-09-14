#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "fade_region_math.h"

// Locked-prefix bound for the bullet screen-emission draws (docs/architecture/
// screen-emission-region.md, step B). The game fills a dynamic write-only
// vertex buffer under one whole-buffer D3DLOCK_DISCARD lock per part batch
// (memcpy of count*24 bytes from its system copy), unlocks, and draws the
// leading primCount*3 vertices non-indexed from StartVertex 0
// (effects-engine-remaining-emission.md, "Bullet vertex buffer writer"). The
// tail past count*24 is undefined DISCARD memory. At Unlock the mapped window
// is scanned once, interpreted as POSITION FLOAT3 at offset 0 with stride 24,
// into cumulative extrema checkpoints: checkpoint k (0-based) holds the
// component-wise min/max over vertices [0, 96(k+1)), plus the first checkpoint
// whose prefix contains a NaN, an infinity or a component beyond world_limit.
// At the draw the first checkpoint covering >= vertex_count vertices is a
// conservative superset of the drawn prefix that includes at most 95 stale
// vertices; garbage in those makes the box larger (finite) or refuses it
// (nonfinite or absurd). Free of Windows and D3D: the ownership layer binds
// the table to its Lock/Unlock observation, the host test and the detached
// fixture drive it directly. No allocation: fixed storage, fixed capacity.
namespace x3m::fade_region::prefix {

constexpr unsigned stride = 24;
constexpr unsigned interval = 96;
constexpr unsigned checkpoints = 64;
constexpr unsigned max_vertices = interval * checkpoints; // 6144 = 1024 bullets x 6
constexpr std::size_t max_bytes = std::size_t(max_vertices) * stride; // 147456: the captured buffer
// World-size sanity limit: |component| <= 2^24 (integer-exact float range);
// X3 sector coordinates are metres within a few hundred kilometres of the
// origin, so anything beyond is stale memory, never geometry.
constexpr float world_limit = 16777216.f;

struct Checkpoint { float lo[3]; float hi[3]; };
struct Scan {
    std::uint32_t vertices = 0;        // vertices scanned (<= max_vertices)
    std::uint32_t blocks = 0;          // checkpoints filled: ceil(vertices / 96)
    std::uint32_t bad_block = ~0u;     // first checkpoint whose prefix holds a nonfinite/absurd component
    Checkpoint at[checkpoints]{};
};

// One pass over min(length / 24, max_vertices) vertices. Integer and SSE
// scalar float only (compiled with -mfpmath=sse): no x87 on the Unlock path.
inline std::uint32_t scan(const void* bytes, std::size_t length, Scan* out) noexcept {
    *out = Scan{};
    if (!bytes) return 0;
    const std::size_t count_size = length / stride;
    const std::uint32_t count = count_size > max_vertices ? max_vertices : std::uint32_t(count_size);
    const unsigned char* p = static_cast<const unsigned char*>(bytes);
    float lo[3] = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f};
    float hi[3] = {-3.4028235e38f, -3.4028235e38f, -3.4028235e38f};
    bool bad = false;
    std::uint32_t block = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        float v[3];
        std::memcpy(v, p + std::size_t(i) * stride, sizeof v);
        for (unsigned a = 0; a < 3; ++a) {
            const float x = v[a];
            const float m = x < 0 ? -x : x;
            if (!(m <= world_limit)) bad = true;   // NaN, +-inf or beyond the limit
            if (x < lo[a]) lo[a] = x;
            if (x > hi[a]) hi[a] = x;
        }
        if ((i + 1) % interval == 0 || i + 1 == count) {
            Checkpoint& c = out->at[block];
            std::memcpy(c.lo, lo, sizeof lo); std::memcpy(c.hi, hi, sizeof hi);
            if (bad && out->bad_block == ~0u) out->bad_block = block;
            ++block;
        }
    }
    out->vertices = count; out->blocks = block;
    return count;
}

enum class Cover : unsigned {
    Bound = 0,      // box valid: the covering checkpoint is finite
    Empty = 1,      // vertex_count 0
    Beyond = 2,     // vertex_count past the scanned vertices (or max_vertices)
    NonFinite = 3   // the covering prefix holds NaN, inf or a component beyond world_limit
};
// The first checkpoint covering >= vertex_count vertices; its box in double.
inline Cover cover(const Scan& s, std::uint32_t vertex_count, Box* box, std::uint32_t* checkpoint) noexcept {
    if (!vertex_count) return Cover::Empty;
    if (vertex_count > s.vertices) return Cover::Beyond;
    const std::uint32_t k = (vertex_count + interval - 1) / interval - 1;
    if (k >= s.blocks) return Cover::Beyond;
    if (checkpoint) *checkpoint = k;
    if (k >= s.bad_block) return Cover::NonFinite;
    const Checkpoint& c = s.at[k];
    for (unsigned a = 0; a < 3; ++a) {
        const double lo = c.lo[a], hi = c.hi[a];
        box->centre[a] = (lo + hi) * 0.5;
        box->half[a] = (hi - lo) * 0.5;
    }
    return Cover::Bound;
}

// Why a lookup gave no bound; Bound == 0 as everywhere in the region code.
enum class Lookup : unsigned {
    Bound = 0,
    Unknown = 1,    // no entry for this buffer (never DISCARD-locked, erased or evicted)
    Pending = 2,    // locked now, or locked again since the last publication
    Invalid = 3,    // the last lock was not scanned (non-DISCARD, size unknown, thread mismatch, failed Unlock, ProcessVertices)
    Empty = 4, Beyond = 5, NonFinite = 6, // cover() outcomes on the published scan
    Count = 7
};
inline const char* lookup_name(Lookup l) noexcept {
    static const char* const names[] = {"bound", "unknown", "pending", "invalid", "empty", "beyond", "nonfinite"};
    const unsigned i = unsigned(l);
    return i < unsigned(Lookup::Count) ? names[i] : "invalid";
}

// Fixed table of per-buffer lock records keyed by an opaque identity (the
// ownership node). Every Lock of a buffer starts a new revision; only a
// DISCARD lock with a known window can be published at its Unlock. A full
// table evicts the oldest record by lock stamp (bullet batches are a handful
// of buffers per frame; the capacity is a bound, not a budget).
class Table {
public:
    static constexpr unsigned capacity = 16;
    enum class State : unsigned char { Free = 0, Pending, Published, Invalid };
    // A successful Lock. scannable: DISCARD with a known window (mapping,
    // length) on this thread; otherwise the record turns Invalid until the
    // next lock. Returns the new revision.
    std::uint64_t begin_lock(std::uintptr_t key, bool scannable, const void* mapping, std::size_t length, std::uint32_t thread) noexcept {
        Entry* e = find(key);
        if (!e) e = slot_for(key);
        const std::uint64_t revision = e->key == key && e->state != State::Free ? e->revision + 1 : 1;
        e->key = key; e->revision = revision; e->stamp = ++clock_;
        e->mapping = scannable ? mapping : nullptr; e->length = scannable ? length : 0; e->thread = thread;
        e->state = scannable && mapping && length ? State::Pending : State::Invalid;
        return revision;
    }
    // Before the backend Unlock while the mapping is valid: scans a pending
    // record on its own thread and publishes it. Returns the vertices scanned
    // (0: nothing published, the record is now Invalid or absent).
    std::uint32_t finish_lock(std::uintptr_t key, std::uint32_t thread) noexcept {
        Entry* e = find(key);
        if (!e) return 0;
        if (e->state != State::Pending || e->thread != thread) { e->state = State::Invalid; return 0; }
        const std::uint32_t vertices = scan(e->mapping, e->length, &e->scan);
        e->mapping = nullptr; e->length = 0;
        e->state = State::Published; ++publications_; scanned_vertices_ += vertices;
        return vertices;
    }
    // A failed Unlock, ProcessVertices into the buffer, or any doubt.
    void invalidate(std::uintptr_t key) noexcept { if (Entry* e = find(key)) e->state = State::Invalid; }
    // The buffer is gone; its identity may recur for a new allocation.
    void erase(std::uintptr_t key) noexcept { if (Entry* e = find(key)) *e = Entry{}; }
    void clear() noexcept { for (auto& e : entries_) e = Entry{}; }
    // Per draw: one linear probe of the fixed table and one cover(); no
    // allocation, no scan. revision reports the record's revision when present.
    Lookup lookup(std::uintptr_t key, std::uint32_t vertex_count, Box* box, std::uint64_t* revision, std::uint32_t* checkpoint) const noexcept {
        const Entry* e = find(key);
        if (!e) return Lookup::Unknown;
        if (revision) *revision = e->revision;
        if (e->state == State::Pending) return Lookup::Pending;
        if (e->state != State::Published) return Lookup::Invalid;
        switch (cover(e->scan, vertex_count, box, checkpoint)) {
        case Cover::Bound: return Lookup::Bound;
        case Cover::Empty: return Lookup::Empty;
        case Cover::Beyond: return Lookup::Beyond;
        default: return Lookup::NonFinite;
        }
    }
    unsigned used() const noexcept { unsigned n = 0; for (const auto& e : entries_) n += e.state != State::Free; return n; }
    std::uint64_t evictions() const noexcept { return evictions_; }
    std::uint64_t publications() const noexcept { return publications_; }
    std::uint64_t scanned_vertices() const noexcept { return scanned_vertices_; }
private:
    struct Entry {
        std::uintptr_t key = 0;
        std::uint64_t revision = 0, stamp = 0;
        const void* mapping = nullptr;
        std::size_t length = 0;
        std::uint32_t thread = 0;
        State state = State::Free;
        Scan scan{};
    };
    Entry* find(std::uintptr_t key) noexcept {
        for (auto& e : entries_) if (e.state != State::Free && e.key == key) return &e;
        return nullptr;
    }
    const Entry* find(std::uintptr_t key) const noexcept { return const_cast<Table*>(this)->find(key); }
    Entry* slot_for(std::uintptr_t key) noexcept {
        (void)key;
        Entry* oldest = nullptr;
        for (auto& e : entries_) {
            if (e.state == State::Free) return &e;
            if (!oldest || e.stamp < oldest->stamp) oldest = &e;
        }
        ++evictions_;
        *oldest = Entry{};
        return oldest;
    }
    Entry entries_[capacity]{};
    std::uint64_t clock_ = 0, evictions_ = 0, publications_ = 0, scanned_vertices_ = 0;
};

} // namespace x3m::fade_region::prefix
