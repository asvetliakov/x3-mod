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
// and the detached fixture drive it directly. Storage: max_vertices*24 bytes per
// slot (12 of positions, 12 of UV/colour words for the bolt footprint),
// allocated when a slot is first marked and pooled for the table's
// lifetime, so erase/clear never free memory a draw may still be reading and
// no draw or lock allocates.
//
// Staging (2026-10-23, bolt-footprint.md "Staged Lock"): on native D3D9 the
// DISCARD mapping is write-combined, so reading it back at Unlock (the scan)
// cost 1.6-5.2 ms per bullet batch. Production therefore never hands the
// driver mapping to the table: begin_lock with a Stage returns a pooled,
// cacheable, page-aligned staging block of the slot covering the whole lock
// range (grow-only, at least max_bytes; all slots together at most
// staging_pool_limit) that the caller gives the application instead of the
// mapping. The sentinel goes into the staging block's scan window, the
// application writes it, and at Unlock finish_lock scans the staging block,
// then copy_out copies the written extent staging -> mapping (one sequential
// write into write-combined memory): the prefix up to the first vertex whose
// six words are all sentinel, or the whole lock range when the scan met no
// such vertex or did not publish (Run 194 A, 2026-10-23: the game's bullet
// buffer is 2211840 bytes, 92160 vertices, locked whole per batch). An open
// stage pins its slot: eviction, erase and clear never reuse or free it until
// close(). Without a Stage (host fixtures only) the old direct mode scans the
// given window itself.
namespace x3m::fade_region::prefix {

constexpr unsigned stride = 24;
constexpr unsigned max_vertices = 6144; // 1024 bullets x 6: the captured 147456-byte buffer
constexpr std::size_t max_bytes = std::size_t(max_vertices) * stride;
constexpr std::size_t storage_floats = std::size_t(max_vertices) * 3;
// The other 12 bytes of each vertex (TEXCOORD FLOAT2 at 12, D3DCOLOR at 20),
// copied beside the positions for the bolt footprint (bolt_footprint_core.h:
// the UV period rule and the verbatim rewrite); 3 words per vertex.
constexpr std::size_t storage_words = std::size_t(max_vertices) * 3;
// World-size sanity limit: |component| <= 2^24 (integer-exact float range);
// X3 sector coordinates are metres within a few hundred kilometres of the
// origin, so anything beyond is stale memory, never geometry.
constexpr float world_limit = 16777216.f;
// The sentinel DWORD; a position whose three words are all sentinel ends the scan.
constexpr std::uint32_t sentinel_word = 0xffffffffu;
// Total staging held across all slots (capacities, alignment slack
// excluded), the peak included (a regrow frees the old block first): a lock
// whose slot would have to grow past it is passed through unscanned
// (Invalid) and counted (size_refusals, largest_refused). The engine's bullet
// buffers are max instances x vertices per instance x 24
// (effects-engine-remaining-emission.md: 1024 x 6 x 24 = 147456, 1024 x 90 x
// 24 = 2211840); 32 MiB holds 14 of the 2.2 MB blocks, against a handful of
// bullet buffers in flight, in a 32-bit process.
constexpr std::size_t staging_pool_limit = std::size_t(32) << 20;
// Staging blocks are page aligned, like a driver mapping at offset 0.
constexpr std::size_t staging_alignment = 4096;

// One open staged lock, held by the caller from Lock to Unlock. bytes is what
// the application received; mapping is the driver's pointer. Copy
// bytes -> mapping for length at the outermost Unlock (or once, before a nested
// Lock that cannot alias the staging block), then
// finish_lock, then close.
struct Stage {
    unsigned char* bytes = nullptr;
    void* mapping = nullptr;
    std::size_t length = 0;
    unsigned slot = ~0u;
    explicit operator bool() const noexcept { return bytes != nullptr; }
};

struct Scan {
    std::uint32_t vertices = 0;   // written vertices: up to the first sentinel, the window end or max_vertices
    std::uint32_t bad_from = ~0u; // first vertex holding NaN, +-inf or a component beyond world_limit
    bool window_end = false;      // no sentinel met: the scan ran to the window end (stale tail not sentinel)
    bool sentinel = false;        // the scan stopped at a vertex whose six words are all sentinel: nothing written
                                  // from there on (contiguous-prefix writer), the written extent is vertices * stride
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
// storage_floats) and, when given, extras the vertex's remaining 3 words
// (capacity storage_words). Integer and SSE scalar float only (compiled with
// -mfpmath=sse): no x87 on the Unlock path. Cost proportional to the vertices
// written: ~12 (24 with extras) bytes read and as many written per vertex.
inline std::uint32_t scan(const void* bytes, std::size_t length, float* positions, Scan* out,
                          std::uint32_t* extras = nullptr) noexcept {
    *out = Scan{};
    if (!bytes || !positions) return 0;
    const std::size_t count_size = length / stride;
    const std::uint32_t window = count_size > max_vertices ? max_vertices : std::uint32_t(count_size);
    const unsigned char* p = static_cast<const unsigned char*>(bytes);
    std::uint32_t i = 0;
    for (; i < window; ++i) {
        std::uint32_t words[6];
        std::memcpy(words, p + std::size_t(i) * stride, sizeof words);
        if (words[0] == sentinel_word && words[1] == sentinel_word && words[2] == sentinel_word) {
            out->sentinel = words[3] == sentinel_word && words[4] == sentinel_word && words[5] == sentinel_word;
            break;
        }
        if (extras) std::memcpy(extras + std::size_t(i) * 3, words + 3, 12);
        float v[3];
        std::memcpy(v, words, 3 * sizeof(float));
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
    Unknown = 1, // no scan for this buffer (unmarked, marked but not locked since, erased, evicted, cleared or storage
                 // unavailable)
    Pending = 2, // locked now, or locked again since the last publication (also: the record changed under the draw)
    Invalid = 3, // the last lock was not scanned (non-DISCARD, size unknown, nested, thread mismatch, failed Unlock,
                 // ProcessVertices, staging unavailable)
    Empty = 4,   // vertex_count 0
    Beyond = 5,  // vertex_count past the scanned vertices (or max_vertices)
    NonFinite = 6, // the drawn prefix holds NaN, inf or a component beyond world_limit
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
        if (index >= capacity) return false; // every slot pins an open stage
        Entry& e = entries_[index];
        if (!storage_[index]) storage_[index].reset(new (std::nothrow) float[storage_floats]);
        if (!extras_[index]) extras_[index].reset(new (std::nothrow) std::uint32_t[storage_words]);
        if (!storage_[index] || !extras_[index]) {
            e = Entry{};
            return false;
        }
        e.key = key;
        e.stamp = ++clock_;
        e.state = State::Marked;
        e.positions = storage_[index].get();
        e.extras = extras_[index].get();
        e.sentinel_vertices = max_vertices;
        return true;
    }
    // A successful Lock of a marked buffer (unmarked buffers are ignored:
    // returns 0). scannable: DISCARD with a known window (mapping, length) on
    // this thread; a nested lock or an unscannable one turns the record
    // Invalid until the next fresh lock. A fresh scannable lock writes the
    // sentinel over the previous prefix's slots (the whole window the first
    // time). Returns the new revision.
    //
    // stage given (production): a pending lock is served from the slot's
    // staging block (*stage filled, sentinel written there, the mapping never
    // touched); a slot already open, a growth past staging_pool_limit or a
    // failed allocation makes the lock Invalid with *stage empty (the caller
    // passes the mapping through). stage null: direct mode (host fixtures).
    std::uint64_t begin_lock(std::uintptr_t key, bool scannable, void* mapping, std::size_t length,
                             std::uint32_t thread, Stage* stage = nullptr) noexcept {
        if (stage) *stage = Stage{};
        Entry* e = find(key);
        if (!e) return 0;
        const bool nested = e->state == State::Pending;
        e->revision += 1;
        e->stamp = ++clock_;
        bool pending = !nested && scannable && mapping && length;
        void* window = mapping;
        if (pending && stage) {
            const unsigned slot = unsigned(e - entries_);
            unsigned char* bytes = open_[slot] ? nullptr : staging_for(slot, length);
            pending = bytes != nullptr;
            if (pending) {
                open_[slot] = true;
                ++staged_locks_;
                *stage = Stage{bytes, mapping, length, slot};
                window = bytes;
            } else
                ++unstaged_locks_;
        }
        e->mapping = pending ? window : nullptr;
        e->length = pending ? length : 0;
        e->thread = thread;
        e->state = pending ? State::Pending : State::Invalid;
        if (pending) sentinel_bytes_ += write_sentinel(window, length, e->sentinel_vertices);
        return e->revision;
    }
    // The whole lock range, staging -> mapping: once before a nested Lock
    // that detaches the stage (and in host fixtures). Returns the bytes copied.
    static std::size_t flush(const Stage& stage) noexcept {
        if (!stage.bytes || !stage.mapping || !stage.length) return 0;
        std::memcpy(stage.mapping, stage.bytes, stage.length);
        return stage.length;
    }
    // The bytes of a staged lock the outermost Unlock copies, given the scan
    // finish_lock published from it (Scan{} when it published nothing):
    // vertices * stride when the scan stopped at an all-sentinel vertex, else
    // the whole lock range. Relies on the engine's contiguous-prefix write (one
    // memcpy of count*24 at offset 0) and on no written vertex being six
    // sentinel words; a write past such a vertex would be lost for that lock.
    static std::size_t written_extent(const Stage& stage, const Scan& published) noexcept {
        if (!published.sentinel) return stage.length;
        const std::size_t prefix = std::size_t(published.vertices) * stride;
        return prefix < stage.length ? prefix : stage.length;
    }
    // The outermost Unlock's copy: written_extent bytes staging -> mapping,
    // counted as a prefix or a full-range copy. Returns the bytes copied.
    std::size_t copy_out(const Stage& stage, const Scan& published) noexcept {
        if (!stage.bytes || !stage.mapping || !stage.length) return 0;
        const std::size_t bytes = written_extent(stage, published);
        if (bytes) std::memcpy(stage.mapping, stage.bytes, bytes);
        if (bytes == stage.length)
            ++full_copies_;
        else
            ++prefix_copies_;
        return bytes;
    }
    // After finish_lock (or instead, when the buffer is gone): unpins the slot.
    void close(const Stage& stage) noexcept {
        if (stage.slot < capacity) open_[stage.slot] = false;
    }
    // Before the backend Unlock while the mapping is valid: scans a pending
    // record on its own thread and publishes it. Returns the vertices scanned
    // (0: nothing published, the record is now Invalid or absent); published,
    // when given, receives the scan (Scan{} when nothing was published).
    std::uint32_t finish_lock(std::uintptr_t key, std::uint32_t thread, Scan* published = nullptr) noexcept {
        if (published) *published = Scan{};
        Entry* e = find(key);
        if (!e) return 0;
        if (e->state != State::Pending || e->thread != thread) {
            e->state = State::Invalid;
            return 0;
        }
        const std::uint32_t vertices = scan(e->mapping, e->length, e->positions, &e->scan, e->extras);
        if (published) *published = e->scan;
        e->mapping = nullptr;
        e->length = 0;
        // Next lock: sentinel exactly the slots this prefix used (the scan
        // overshoot included, so a recycled non-sentinel tail is covered once).
        e->sentinel_vertices = vertices;
        e->state = State::Published;
        ++publications_;
        scanned_vertices_ += vertices;
        if (e->scan.window_end) ++window_end_scans_;
        return vertices;
    }
    // A failed Unlock, ProcessVertices into the buffer, or any doubt.
    void invalidate(std::uintptr_t key) noexcept {
        if (Entry* e = find(key)) e->state = State::Invalid;
    }
    bool marked(std::uintptr_t key) const noexcept { return find(key) != nullptr; }
    // The buffer is gone; its identity may recur for a new allocation. The
    // slot's storage stays pooled.
    void erase(std::uintptr_t key) noexcept {
        if (Entry* e = find(key)) *e = Entry{};
    }
    // Reset, device release: every record goes, and the staging blocks of
    // slots without an open stage are freed (only a Lock/Unlock pair reads
    // staging, so no draw can hold them); the position/extras storage stays
    // pooled. A pinned slot keeps its block until its Unlock.
    void clear() noexcept {
        for (auto& e : entries_) e = Entry{};
        for (unsigned i = 0; i < capacity; ++i)
            if (!open_[i]) release_staging(i);
    }
    // Per draw: one linear probe of the fixed table; no allocation, no scan.
    // Bound hands out the record's positions (3 floats per vertex, at least
    // vertex_count of them), on request the vertices' other 3 words (extras),
    // and its revision; the caller projects them and rechecks the revision
    // afterwards (a Lock on another thread in between advances it). revision
    // reports the record's revision when present; scanned the published count.
    Lookup lookup(std::uintptr_t key, std::uint32_t vertex_count, const float** positions, std::uint64_t* revision,
                  std::uint32_t* scanned, const std::uint32_t** extras = nullptr) const noexcept {
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
        if (extras) *extras = e->extras;
        return Lookup::Bound;
    }
    unsigned used() const noexcept {
        unsigned n = 0;
        for (const auto& e : entries_) n += e.state != State::Free;
        return n;
    }
    std::uint64_t evictions() const noexcept { return evictions_; }
    std::uint64_t publications() const noexcept { return publications_; }
    std::uint64_t scanned_vertices() const noexcept { return scanned_vertices_; }
    std::uint64_t sentinel_bytes() const noexcept { return sentinel_bytes_; }
    std::uint64_t window_end_scans() const noexcept { return window_end_scans_; }
    unsigned allocated() const noexcept {
        unsigned n = 0;
        for (const auto& s : storage_) n += s != nullptr;
        return n;
    }
    std::uint64_t staged_locks() const noexcept { return staged_locks_; }
    std::uint64_t unstaged_locks() const noexcept { return unstaged_locks_; }
    // Bytes of staging held (capacities, alignment slack excluded) and open stages.
    std::size_t staging_bytes() const noexcept {
        std::size_t n = 0;
        for (std::size_t c : staging_capacity_) n += c;
        return n;
    }
    unsigned open_stages() const noexcept {
        unsigned n = 0;
        for (bool o : open_) n += o;
        return n;
    }
    std::uint64_t full_copies() const noexcept { return full_copies_; }
    std::uint64_t prefix_copies() const noexcept { return prefix_copies_; }
    // Scannable locks refused staging because the pool would pass staging_pool_limit, and the largest such range;
    // staging allocations that failed (the lock is passed through, like a refusal).
    std::uint64_t size_refusals() const noexcept { return size_refusals_; }
    std::uint64_t staging_failures() const noexcept { return staging_failures_; }
    std::size_t largest_refused() const noexcept { return largest_refused_; }

private:
    struct Entry {
        std::uintptr_t key = 0;
        std::uint64_t revision = 0, stamp = 0;
        void* mapping = nullptr;
        std::size_t length = 0;
        std::uint32_t thread = 0;
        std::uint32_t sentinel_vertices = max_vertices; // slots to sentinel at the next fresh lock
        State state = State::Free;
        float* positions = nullptr;      // the slot's pooled storage
        std::uint32_t* extras = nullptr; // the slot's pooled UV/colour words
        Scan scan{};
    };
    Entry* find(std::uintptr_t key) noexcept {
        for (auto& e : entries_)
            if (e.state != State::Free && e.key == key) return &e;
        return nullptr;
    }
    const Entry* find(std::uintptr_t key) const noexcept { return const_cast<Table*>(this)->find(key); }
    // A slot pinned by an open stage is never handed out (its staging block
    // is the application's current lock pointer); capacity when all are.
    unsigned slot_for(std::uintptr_t key) noexcept {
        (void)key;
        unsigned oldest = capacity;
        for (unsigned i = 0; i < capacity; ++i) {
            const Entry& e = entries_[i];
            if (open_[i]) continue;
            if (e.state == State::Free) return i;
            if (oldest == capacity || e.stamp < entries_[oldest].stamp) oldest = i;
        }
        if (oldest == capacity) return capacity;
        ++evictions_;
        entries_[oldest] = Entry{};
        return oldest;
    }
    // The slot's staging block for a lock of length bytes: the whole range,
    // grow-only (at least max_bytes), allocated only on the slot's first
    // staged lock or a longer range; its scan window (the leading max_bytes)
    // is filled once with the sentinel pattern (so a range that grew past the
    // previous block still ends the scan at the first unwritten vertex), the
    // rest is never read by the scan and reaches the mapping only through a
    // full-range copy (DISCARD contents are undefined). nullptr when the pool
    // would pass staging_pool_limit or the allocation fails (each counted).
    // Called only for a slot without an open stage, so a regrow frees the old
    // block before allocating the new one (the peak stays within the pool; a
    // failed allocation leaves the slot without a block until the next lock).
    unsigned char* staging_for(unsigned slot, std::size_t length) noexcept {
        if (staging_capacity_[slot] >= length) return staging_aligned_[slot];
        const std::size_t want = length > max_bytes ? length : max_bytes;
        if (want > staging_pool_limit || staging_bytes() - staging_capacity_[slot] > staging_pool_limit - want) {
            ++size_refusals_;
            if (length > largest_refused_) largest_refused_ = length;
            return nullptr;
        }
        release_staging(slot);
        std::unique_ptr<unsigned char[]> block(new (std::nothrow) unsigned char[want + staging_alignment]);
        if (!block) {
            ++staging_failures_;
            return nullptr;
        }
        const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(block.get());
        const std::uintptr_t aligned = (raw + staging_alignment - 1) & ~std::uintptr_t(staging_alignment - 1);
        std::memset(reinterpret_cast<unsigned char*>(aligned), 0xff, max_bytes);
        staging_[slot] = std::move(block);
        staging_aligned_[slot] = reinterpret_cast<unsigned char*>(aligned);
        staging_capacity_[slot] = want;
        return staging_aligned_[slot];
    }
    void release_staging(unsigned slot) noexcept {
        staging_[slot].reset();
        staging_aligned_[slot] = nullptr;
        staging_capacity_[slot] = 0;
    }
    Entry entries_[capacity]{};
    std::unique_ptr<float[]> storage_[capacity];
    std::unique_ptr<std::uint32_t[]> extras_[capacity];
    std::unique_ptr<unsigned char[]> staging_[capacity]; // pooled until clear() (Reset, device release)
    unsigned char* staging_aligned_[capacity]{};
    std::size_t staging_capacity_[capacity]{};
    bool open_[capacity]{}; // slot pinned by an open stage (kept, with its block, across erase/clear/eviction)
    std::uint64_t clock_ = 0, evictions_ = 0, publications_ = 0, scanned_vertices_ = 0, sentinel_bytes_ = 0,
                  window_end_scans_ = 0, staged_locks_ = 0, unstaged_locks_ = 0, full_copies_ = 0, prefix_copies_ = 0,
                  size_refusals_ = 0, staging_failures_ = 0;
    std::size_t largest_refused_ = 0;
};

} // namespace x3m::fade_region::prefix
