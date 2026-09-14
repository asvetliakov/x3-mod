#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "fade_region_math.h"

// Locked-prefix bound for the bullet screen-emission draws (docs/architecture/
// screen-emission-region.md step B; docs/architecture/
// screen-emission-bullet-bound.md step D). The game fills a dynamic write-only
// vertex buffer under one whole-buffer D3DLOCK_DISCARD lock per part batch
// (memcpy of count*24 bytes from its system copy), unlocks, and draws the
// leading primCount*3 vertices non-indexed from StartVertex 0
// (effects-engine-remaining-emission.md, "Bullet vertex buffer writer"). The
// tail past count*24 is undefined DISCARD memory.
//
// Step D replaces the 96-vertex extrema checkpoints with the vertices
// themselves. At Lock, before the mapping is returned, a sentinel (all-ones
// DWORDs: a NaN no writer produces) is written over the slots the previous
// prefix used (the whole window the first time); DISCARD contents are
// undefined to the application and the writer only copies its prefix, so the
// sentinel survives past it. At Unlock the window is scanned from vertex 0 to
// the first sentinel vertex: exact count, finiteness and world_limit over the
// written vertices, and their positions (12 bytes each, POSITION FLOAT3 at
// offset 0, stride 24) copied into the record's storage. At the draw the
// leading primCount*3 positions are projected per triangle (fade_region_math.h,
// project_prefix); a draw past the scanned count is Beyond (refused), a
// nonfinite or absurd vertex inside the drawn prefix is NonFinite (refused). A
// stale tail that is not sentinel (memory recycled by the driver) only lengthens
// the scan; the draw count is exact regardless. Free of Windows and D3D: the
// ownership layer binds the table to its Lock/Unlock observation, the host test
// and the detached fixture drive it directly. Storage: max_vertices*12 bytes per
// slot, allocated when a slot is first marked and pooled for the table's
// lifetime, so erase/clear never free memory a draw may still be reading and
// no draw or lock allocates.
namespace x3m::fade_region::prefix {

constexpr unsigned stride = 24;
constexpr unsigned max_vertices = 6144; // 1024 bullets x 6: the captured 147456-byte buffer
constexpr std::size_t max_bytes = std::size_t(max_vertices) * stride;
constexpr std::size_t storage_floats = std::size_t(max_vertices) * 3;
// World-size sanity limit: |component| <= 2^24 (integer-exact float range);
// X3 sector coordinates are metres within a few hundred kilometres of the
// origin, so anything beyond is stale memory, never geometry.
constexpr float world_limit = 16777216.f;
// The sentinel DWORD; a position whose three words are all sentinel ends the scan.
constexpr std::uint32_t sentinel_word = 0xffffffffu;

struct Scan {
    std::uint32_t vertices = 0;      // written vertices: up to the first sentinel, the window end or max_vertices
    std::uint32_t bad_from = ~0u;    // first vertex holding NaN, +-inf or a component beyond world_limit
    bool window_end = false;         // no sentinel met: the scan ran to the window end (stale tail not sentinel)
};

// Writes the sentinel over the leading min(length, vertices*stride) bytes of
// the mapping; returns the bytes written. Integer stores only.
inline std::size_t write_sentinel(void* bytes, std::size_t length, std::uint32_t vertices) noexcept {
    if (!bytes || !length) return 0;
    std::size_t span = std::size_t(vertices) * stride;
    if (span > length) span = length;
    if (span > max_bytes) span = max_bytes;
    if (span) std::memset(bytes, 0xff, span);
    return span;
}

// One pass over min(length / 24, max_vertices) vertices, stopping at the
// first sentinel; positions receives 3 floats per scanned vertex (capacity
// storage_floats). Integer and SSE scalar float only (compiled with
// -mfpmath=sse): no x87 on the Unlock path. Cost proportional to the vertices
// written: ~12 bytes read and 12 written per vertex.
inline std::uint32_t scan(const void* bytes, std::size_t length, float* positions, Scan* out) noexcept {
    *out = Scan{};
    if (!bytes || !positions) return 0;
    const std::size_t count_size = length / stride;
    const std::uint32_t window = count_size > max_vertices ? max_vertices : std::uint32_t(count_size);
    const unsigned char* p = static_cast<const unsigned char*>(bytes);
    std::uint32_t i = 0;
    for (; i < window; ++i) {
        std::uint32_t words[3];
        std::memcpy(words, p + std::size_t(i) * stride, sizeof words);
        if (words[0] == sentinel_word && words[1] == sentinel_word && words[2] == sentinel_word) break;
        float v[3];
        std::memcpy(v, words, sizeof v);
        for (unsigned a = 0; a < 3; ++a) {
            const float x = v[a];
            const float m = x < 0 ? -x : x;
            if (!(m <= world_limit) && out->bad_from == ~0u) out->bad_from = i; // NaN, +-inf or beyond the limit
            positions[std::size_t(i) * 3 + a] = x;
        }
    }
    out->vertices = i;
    out->window_end = i == window;
    return i;
}

// Why a lookup gave no bound; Bound == 0 as everywhere in the region code.
enum class Lookup : unsigned {
    Bound = 0,
    Unknown = 1,    // no scan for this buffer (unmarked, marked but not locked since, erased, evicted, cleared or storage unavailable)
    Pending = 2,    // locked now, or locked again since the last publication (also: the record changed under the draw)
    Invalid = 3,    // the last lock was not scanned (non-DISCARD, size unknown, thread mismatch, failed Unlock, ProcessVertices)
    Empty = 4,      // vertex_count 0
    Beyond = 5,     // vertex_count past the scanned vertices (or max_vertices)
    NonFinite = 6,  // the drawn prefix holds NaN, inf or a component beyond world_limit
    Count = 7
};
inline const char* lookup_name(Lookup l) noexcept {
    static const char* const names[] = {"bound", "unknown", "pending", "invalid", "empty", "beyond", "nonfinite"};
    const unsigned i = unsigned(l);
    return i < unsigned(Lookup::Count) ? names[i] : "invalid";
}

// Fixed table of per-buffer records keyed by an opaque identity (the
// ownership node). Only a marked buffer (one an admitted screen-emission draw
// has been seen from, marked at that draw) is ever sentinelled or scanned, so
// the first draw after creation is refused by design and unrelated DISCARD
// locks cost nothing. State machine per record: Marked (no scan yet) ->
// Pending (locked) -> Published (scanned at Unlock) | Invalid (non-DISCARD or
// nested lock, unknown window, thread mismatch, failed Unlock,
// ProcessVertices); every Lock advances the revision. A full table evicts the
// oldest record by stamp (bullet batches are a handful of buffers per frame;
// the capacity is a bound, not a budget).
class Table {
public:
    static constexpr unsigned capacity = 16;
    enum class State : unsigned char { Free = 0, Marked, Pending, Published, Invalid };
    // Learned at the draw: the next DISCARD lock of this buffer is sentinelled
    // and its Unlock scanned. Idempotent; an existing record keeps its state.
    // false: the slot's storage could not be allocated (no record).
    bool mark(std::uintptr_t key) noexcept {
        if (find(key)) return true;
        const unsigned index = slot_for(key);
        Entry& e = entries_[index];
        if (!storage_[index]) storage_[index].reset(new (std::nothrow) float[storage_floats]);
        if (!storage_[index]) { e = Entry{}; return false; }
        e.key = key; e.stamp = ++clock_; e.state = State::Marked;
        e.positions = storage_[index].get(); e.sentinel_vertices = max_vertices;
        return true;
    }
    // A successful Lock of a marked buffer (unmarked buffers are ignored:
    // returns 0). scannable: DISCARD with a known window (mapping, length) on
    // this thread; a nested lock or an unscannable one turns the record
    // Invalid until the next fresh lock. A fresh scannable lock writes the
    // sentinel over the previous prefix's slots (the whole window the first
    // time). Returns the new revision.
    std::uint64_t begin_lock(std::uintptr_t key, bool scannable, void* mapping, std::size_t length, std::uint32_t thread) noexcept {
        Entry* e = find(key);
        if (!e) return 0;
        const bool nested = e->state == State::Pending;
        e->revision += 1; e->stamp = ++clock_;
        const bool pending = !nested && scannable && mapping && length;
        e->mapping = pending ? mapping : nullptr; e->length = pending ? length : 0; e->thread = thread;
        e->state = pending ? State::Pending : State::Invalid;
        if (pending) sentinel_bytes_ += write_sentinel(mapping, length, e->sentinel_vertices);
        return e->revision;
    }
    // Before the backend Unlock while the mapping is valid: scans a pending
    // record on its own thread and publishes it. Returns the vertices scanned
    // (0: nothing published, the record is now Invalid or absent).
    std::uint32_t finish_lock(std::uintptr_t key, std::uint32_t thread) noexcept {
        Entry* e = find(key);
        if (!e) return 0;
        if (e->state != State::Pending || e->thread != thread) { e->state = State::Invalid; return 0; }
        const std::uint32_t vertices = scan(e->mapping, e->length, e->positions, &e->scan);
        e->mapping = nullptr; e->length = 0;
        // Next lock: sentinel exactly the slots this prefix used (the scan
        // overshoot included, so a recycled non-sentinel tail is covered once).
        e->sentinel_vertices = vertices;
        e->state = State::Published; ++publications_; scanned_vertices_ += vertices;
        if (e->scan.window_end) ++window_end_scans_;
        return vertices;
    }
    // A failed Unlock, ProcessVertices into the buffer, or any doubt.
    void invalidate(std::uintptr_t key) noexcept { if (Entry* e = find(key)) e->state = State::Invalid; }
    bool marked(std::uintptr_t key) const noexcept { return find(key) != nullptr; }
    // The buffer is gone; its identity may recur for a new allocation. The
    // slot's storage stays pooled.
    void erase(std::uintptr_t key) noexcept { if (Entry* e = find(key)) *e = Entry{}; }
    void clear() noexcept { for (auto& e : entries_) e = Entry{}; }
    // Per draw: one linear probe of the fixed table; no allocation, no scan.
    // Bound hands out the record's positions (3 floats per vertex, at least
    // vertex_count of them) and its revision; the caller projects them and
    // rechecks the revision afterwards (a Lock on another thread in between
    // advances it). revision reports the record's revision when present;
    // scanned the published count.
    Lookup lookup(std::uintptr_t key, std::uint32_t vertex_count, const float** positions, std::uint64_t* revision, std::uint32_t* scanned) const noexcept {
        const Entry* e = find(key);
        if (!e || e->state == State::Marked) return Lookup::Unknown;
        if (revision) *revision = e->revision;
        if (e->state == State::Pending) return Lookup::Pending;
        if (e->state != State::Published) return Lookup::Invalid;
        if (scanned) *scanned = e->scan.vertices;
        if (!vertex_count) return Lookup::Empty;
        if (vertex_count > e->scan.vertices) return Lookup::Beyond;
        if (vertex_count > e->scan.bad_from) return Lookup::NonFinite;
        if (positions) *positions = e->positions;
        return Lookup::Bound;
    }
    unsigned used() const noexcept { unsigned n = 0; for (const auto& e : entries_) n += e.state != State::Free; return n; }
    std::uint64_t evictions() const noexcept { return evictions_; }
    std::uint64_t publications() const noexcept { return publications_; }
    std::uint64_t scanned_vertices() const noexcept { return scanned_vertices_; }
    std::uint64_t sentinel_bytes() const noexcept { return sentinel_bytes_; }
    std::uint64_t window_end_scans() const noexcept { return window_end_scans_; }
    unsigned allocated() const noexcept { unsigned n = 0; for (const auto& s : storage_) n += s != nullptr; return n; }
private:
    struct Entry {
        std::uintptr_t key = 0;
        std::uint64_t revision = 0, stamp = 0;
        void* mapping = nullptr;
        std::size_t length = 0;
        std::uint32_t thread = 0;
        std::uint32_t sentinel_vertices = max_vertices; // slots to sentinel at the next fresh lock
        State state = State::Free;
        float* positions = nullptr; // the slot's pooled storage
        Scan scan{};
    };
    Entry* find(std::uintptr_t key) noexcept {
        for (auto& e : entries_) if (e.state != State::Free && e.key == key) return &e;
        return nullptr;
    }
    const Entry* find(std::uintptr_t key) const noexcept { return const_cast<Table*>(this)->find(key); }
    unsigned slot_for(std::uintptr_t key) noexcept {
        (void)key;
        unsigned oldest = 0; bool found = false;
        for (unsigned i = 0; i < capacity; ++i) {
            const Entry& e = entries_[i];
            if (e.state == State::Free) return i;
            if (!found || e.stamp < entries_[oldest].stamp) { oldest = i; found = true; }
        }
        ++evictions_;
        entries_[oldest] = Entry{};
        return oldest;
    }
    Entry entries_[capacity]{};
    std::unique_ptr<float[]> storage_[capacity];
    std::uint64_t clock_ = 0, evictions_ = 0, publications_ = 0, scanned_vertices_ = 0, sentinel_bytes_ = 0, window_end_scans_ = 0;
};

} // namespace x3m::fade_region::prefix
