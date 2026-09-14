#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "fade_region_math.h"

// Bound source for admitted distance-fade draws (docs/architecture/
// linear-distance-fade-region.md, section 1), free of Windows and D3D so the
// host test can drive it against fake memory and a fake buffer registry
// (verification/probe/fade_region_host.cpp --table). Production binds the
// Environment to engine_memory::read, object_trace::scope_descriptor and
// ownership::get_buffer_content_view in fade_region.cpp.
//
// The owning mesh part's object-space AABB is reached from the 0x004c5228
// seam's descriptor argument (part = *(descriptor + 0), AABB at
// part+0x40..0x5b as int32 in 4 x int16 units, back-link part+0x64 ==
// descriptor) and proven to own the drawn buffers: some subset record of the
// descriptor (+0x08 count, +0x0c array, stride 0x1a8, +0x0c VB, +0x10 IB) must
// hold exactly the application buffer pointers the device was given at
// SetStreamSource/SetIndices (the capture hooks observe the public ownership
// wrappers, the same COM identities D3DX handed the engine). Entries are keyed
// by the VB allocation id in a fixed-capacity open-addressed table; a probe
// window that is full evicts its oldest unpoisoned entry (poisoned entries are
// kept as long as possible). Any observed write revision advance, lock,
// ambiguity or identity mismatch poisons the entry. Everything fails closed:
// any status but Bound means the caller composes the full viewport.
namespace x3m::fade_region {

enum class Status : unsigned {
    Bound = 0,          // box valid for this draw
    NoTable = 1,        // table not reserved (attach failed or never requested)
    NoScope = 2,        // object trace off, no seam scope on this thread, or no buffers bound
    ContentUnknown = 3, // buffer write tracking unavailable, locked or ambiguous
    Poisoned = 4,       // entry poisoned now or earlier
    ReadFailed = 5,     // an engine read was refused
    BackLink = 6,       // part+0x64 != descriptor or null part
    NoRecord = 7,       // no subset record holds the bound VB/IB
    Invalid = 8,        // negative half-extent or box outside the |p| <= 2 domain
    Count = 9
};
inline const char* status_name(Status status) noexcept {
    static const char* const names[] = {"bound", "no_table", "no_scope", "content_unknown", "poisoned", "read_failed", "back_link", "no_record", "invalid"};
    const unsigned i = unsigned(status);
    return i < unsigned(Status::Count) ? names[i] : "invalid";
}

struct Environment {
    // Validated read of the game's own memory; false refuses the span.
    bool (*read)(std::uintptr_t address, void* out, std::size_t size) noexcept;
    // Innermost seam scope: descriptor (args[0]) and depth; false without one.
    bool (*scope)(std::uintptr_t* descriptor, std::uint32_t* depth) noexcept;
    // Write revision of a recognised, tracked, unlocked, unambiguous
    // application buffer wrapper; false otherwise. Never dereferences.
    bool (*content)(std::uintptr_t wrapper, std::uint64_t* revision) noexcept;
};

struct Query {
    std::uint64_t vb_id = 0, ib_id = 0; // allocation ids of the bound buffers (shadow)
    std::uintptr_t vb = 0, ib = 0;      // the application buffer identities the device was given
};
struct Result {
    Status status = Status::NoTable;
    bool hit = false;            // served from an existing entry
    bool poisoned_now = false;   // this call poisoned the entry
    bool evicted = false;        // this call reused an entry's slot
    std::uint32_t depth = 0;     // seam scope depth (0: none)
    std::uintptr_t descriptor = 0, part = 0;
    std::int32_t aabb[6]{};      // centre x4, half-extent x4 (raw int32 fields)
    std::uint64_t vb_revision = 0, ib_revision = 0;
    Box box{};                   // POSITION0 units when status == Bound
};

namespace layout {
constexpr std::uintptr_t part_aabb = 0x40;          // int32 centre x4 at +0x40/44/48, half-extent x4 at +0x50/54/58
constexpr std::uintptr_t part_descriptor = 0x64;    // back-link to the descriptor
constexpr std::uintptr_t descriptor_records = 0x0c; // subset record array; count (short) at +0x08
constexpr std::uintptr_t record_stride = 0x1a8;
constexpr std::uintptr_t record_buffers = 0x0c;     // VB at +0x0c, IB at +0x10
constexpr unsigned record_cap = 16;
constexpr double units = 1.0 / 65536.0;             // 4 x int16 / 16384: POSITION0 units
constexpr std::int64_t domain = 2 * 65536;          // |centre| + half must stay within |p| <= 2
}

class BoundTable {
public:
    static constexpr unsigned capacity = 1024, probe_window = 32;
    BoundTable() noexcept = default;
    BoundTable(const BoundTable&) = delete;
    BoundTable& operator=(const BoundTable&) = delete;
    // Allocates the table once (attach). false leaves every resolve at NoTable.
    bool reserve() noexcept {
        if (entries_) return true;
        entries_.reset(new (std::nothrow) Entry[capacity]);
        used_ = poisoned_ = evictions_ = 0; clock_ = 0;
        return entries_ != nullptr;
    }
    // Drops every entry and the storage (Reset, teardown).
    void clear() noexcept { entries_.reset(); used_ = poisoned_ = evictions_ = 0; clock_ = 0; }
    bool reserved() const noexcept { return entries_ != nullptr; }
    unsigned used() const noexcept { return used_; }
    unsigned poisoned() const noexcept { return poisoned_; }
    unsigned evictions() const noexcept { return evictions_; }
    // Per draw: the scope (no read), one probe; on a hit two content lookups;
    // on a miss the slot is chosen (evicting if needed) before any game read,
    // then the validated reads described above and two content lookups. No
    // allocation. The caller preserves LastError around this call.
    Result resolve(const Query& query, const Environment& env) noexcept {
        Result out{};
        if (!entries_) { out.status = Status::NoTable; return out; }
        std::uintptr_t descriptor = 0; std::uint32_t depth = 0;
        const bool scoped = env.scope(&descriptor, &depth) && descriptor && depth;
        out.depth = depth; out.descriptor = descriptor;
        if (!scoped || !query.vb_id || !query.ib_id || !query.vb || !query.ib) { out.status = Status::NoScope; return out; }
        ++clock_;
        Entry* entry = find(query.vb_id);
        if (entry) {
            entry->stamp = clock_;
            out.hit = true; out.part = entry->part;
            std::memcpy(out.aabb, entry->aabb, sizeof out.aabb);
            if (entry->poisoned) { out.status = Status::Poisoned; return out; }
            std::uint64_t vb_revision = 0, ib_revision = 0;
            const bool valid = entry->ib == query.ib_id && entry->descriptor == descriptor
                && entry->vb_wrapper == query.vb && entry->ib_wrapper == query.ib
                && env.content(query.vb, &vb_revision) && env.content(query.ib, &ib_revision)
                && vb_revision == entry->vb_revision && ib_revision == entry->ib_revision;
            out.vb_revision = vb_revision; out.ib_revision = ib_revision;
            if (!valid) { entry->poisoned = true; ++poisoned_; out.poisoned_now = true; out.status = Status::Poisoned; return out; }
        } else if (!learn(query, descriptor, env, out)) return out;
        finish(out);
        return out;
    }
    // Read-only twin of resolve for diagnostics (the capture-only refused-draw
    // rectangle): the same scope, probe and validation, but no clock, stamp,
    // poison, insert or eviction. A hit is served from the entry (an invalid
    // one reports Poisoned without poisoning it); a miss performs the same
    // validated reads into `out` only. Table counters never change.
    Result peek(const Query& query, const Environment& env) const noexcept {
        Result out{};
        if (!entries_) { out.status = Status::NoTable; return out; }
        std::uintptr_t descriptor = 0; std::uint32_t depth = 0;
        const bool scoped = env.scope(&descriptor, &depth) && descriptor && depth;
        out.depth = depth; out.descriptor = descriptor;
        if (!scoped || !query.vb_id || !query.ib_id || !query.vb || !query.ib) { out.status = Status::NoScope; return out; }
        if (const Entry* entry = find(query.vb_id)) {
            out.hit = true; out.part = entry->part;
            std::memcpy(out.aabb, entry->aabb, sizeof out.aabb);
            if (entry->poisoned) { out.status = Status::Poisoned; return out; }
            std::uint64_t vb_revision = 0, ib_revision = 0;
            const bool valid = entry->ib == query.ib_id && entry->descriptor == descriptor
                && entry->vb_wrapper == query.vb && entry->ib_wrapper == query.ib
                && env.content(query.vb, &vb_revision) && env.content(query.ib, &ib_revision)
                && vb_revision == entry->vb_revision && ib_revision == entry->ib_revision;
            out.vb_revision = vb_revision; out.ib_revision = ib_revision;
            if (!valid) { out.status = Status::Poisoned; return out; }
        } else {
            std::uintptr_t part = 0; std::uint64_t vb_revision = 0, ib_revision = 0;
            if (!read_box(query, descriptor, env, out, part, vb_revision, ib_revision)) return out;
            out.vb_revision = vb_revision; out.ib_revision = ib_revision;
        }
        finish(out);
        return out;
    }
private:
    struct Entry {
        std::uint64_t vb = 0, ib = 0;
        std::uintptr_t descriptor = 0, part = 0;
        std::uintptr_t vb_wrapper = 0, ib_wrapper = 0;
        std::uint64_t vb_revision = 0, ib_revision = 0;
        std::uint64_t stamp = 0;
        std::int32_t aabb[6]{};
        bool used = false, poisoned = false;
    };
    static std::uint32_t slot_of(std::uint64_t vb) noexcept {
        return std::uint32_t((vb * 0x9e3779b97f4a7c15ull) >> 32) & (capacity - 1);
    }
    Entry* find(std::uint64_t vb) noexcept {
        const std::uint32_t start = slot_of(vb);
        for (unsigned i = 0; i < probe_window; ++i) {
            Entry& e = entries_[(start + i) & (capacity - 1)];
            if (!e.used) return nullptr;
            if (e.vb == vb) return &e;
        }
        return nullptr;
    }
    const Entry* find(std::uint64_t vb) const noexcept { return const_cast<BoundTable*>(this)->find(vb); }
    static void finish(Result& out) noexcept {
        for (unsigned a = 0; a < 3; ++a) {
            out.box.centre[a] = double(out.aabb[a]) * layout::units;
            out.box.half[a] = double(out.aabb[3 + a]) * layout::units;
        }
        out.status = Status::Bound;
    }
    // The validated game reads and content lookups of a miss, into `out`
    // only: no table state is touched. Shared by learn (which then commits
    // the entry) and peek.
    static bool read_box(const Query& query, std::uintptr_t descriptor, const Environment& env, Result& out,
                         std::uintptr_t& part, std::uint64_t& vb_revision, std::uint64_t& ib_revision) noexcept {
        std::uint32_t head[4]{}; // +0 part, +4 (+6 short 1), +8 count (short), +c records
        if (!env.read(descriptor, head, sizeof head)) { out.status = Status::ReadFailed; return false; }
        part = head[0];
        out.part = part;
        if (!part) { out.status = Status::BackLink; return false; }
        std::int32_t fields[7]{}; std::uint32_t back = 0;
        if (!env.read(part + layout::part_aabb, fields, sizeof fields) || !env.read(part + layout::part_descriptor, &back, sizeof back)) {
            out.status = Status::ReadFailed; return false;
        }
        if (back != descriptor) { out.status = Status::BackLink; return false; }
        out.aabb[0] = fields[0]; out.aabb[1] = fields[1]; out.aabb[2] = fields[2];
        out.aabb[3] = fields[4]; out.aabb[4] = fields[5]; out.aabb[5] = fields[6];
        // Negative extents, or a box outside the |p| <= 2 POSITION0 domain
        // (int16/16384 encoding limit, 2 x 65536 in these units) that the
        // 2^-10 half-float expansion is justified for, are not a bound.
        for (unsigned a = 0; a < 3; ++a) {
            const std::int64_t centre = fields[a], half = fields[4 + a];
            if (half < 0 || (centre < 0 ? -centre : centre) + half > layout::domain) { out.status = Status::Invalid; return false; }
        }
        const unsigned count = head[2] & 0xffffu;
        const std::uintptr_t records = head[3];
        if (!count || !records) { out.status = Status::NoRecord; return false; }
        const unsigned walk = count < layout::record_cap ? count : layout::record_cap;
        bool matched = false;
        for (unsigned i = 0; i < walk && !matched; ++i) {
            std::uint32_t buffers[2]{};
            if (!env.read(records + i * layout::record_stride + layout::record_buffers, buffers, sizeof buffers)) { out.status = Status::ReadFailed; return false; }
            matched = std::uintptr_t(buffers[0]) == query.vb && std::uintptr_t(buffers[1]) == query.ib;
        }
        if (!matched) { out.status = Status::NoRecord; return false; }
        if (!env.content(query.vb, &vb_revision) || !env.content(query.ib, &ib_revision)) { out.status = Status::ContentUnknown; return false; }
        return true;
    }
    // A free slot in the window, else the oldest unpoisoned entry, else the
    // oldest poisoned one. Never null; chosen before any game read, committed
    // by learn only after every read and lookup succeeded.
    Entry* slot_for(std::uint64_t vb) noexcept {
        const std::uint32_t start = slot_of(vb);
        Entry* oldest = nullptr; Entry* oldest_poisoned = nullptr;
        for (unsigned i = 0; i < probe_window; ++i) {
            Entry& e = entries_[(start + i) & (capacity - 1)];
            if (!e.used) return &e;
            if (e.poisoned) { if (!oldest_poisoned || e.stamp < oldest_poisoned->stamp) oldest_poisoned = &e; }
            else if (!oldest || e.stamp < oldest->stamp) oldest = &e;
        }
        return oldest ? oldest : oldest_poisoned;
    }
    bool learn(const Query& query, std::uintptr_t descriptor, const Environment& env, Result& out) noexcept {
        Entry* entry = slot_for(query.vb_id); // table checked before any game read
        std::uintptr_t part = 0; std::uint64_t vb_revision = 0, ib_revision = 0;
        if (!read_box(query, descriptor, env, out, part, vb_revision, ib_revision)) return false;
        if (entry->used) { out.evicted = true; ++evictions_; --used_; if (entry->poisoned) --poisoned_; }
        *entry = Entry{};
        entry->used = true; entry->stamp = clock_;
        entry->vb = query.vb_id; entry->ib = query.ib_id;
        entry->descriptor = descriptor; entry->part = part;
        entry->vb_wrapper = query.vb; entry->ib_wrapper = query.ib;
        entry->vb_revision = vb_revision; entry->ib_revision = ib_revision;
        std::memcpy(entry->aabb, out.aabb, sizeof entry->aabb);
        ++used_;
        out.vb_revision = vb_revision; out.ib_revision = ib_revision;
        return true;
    }
    std::unique_ptr<Entry[]> entries_;
    unsigned used_ = 0, poisoned_ = 0, evictions_ = 0;
    std::uint64_t clock_ = 0;
};

} // namespace x3m::fade_region
