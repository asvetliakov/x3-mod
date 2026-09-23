#pragma once
// Fog hand-over R3 (docs/architecture/fog-handover.md, "R3 implementation"; layout proof:
// docs/reverse-engineering/sector-transit-order.md §1, §5): during a transit stall the destination
// sector object is already in the engine's global object list with its background index, before any
// destination body loads. This header is the read side and the two small policies around it, value
// only: no Windows/D3D dependency, no engine calls, no allocation, no pointer kept across polls.
#include "sector_background.h"
#include <cstdint>

namespace x3m::fog_prefill {
constexpr std::uint32_t kManager = 0x0060850c;   // M = *kManager; list header at M+8 (head, tail 0, tailpred)
constexpr unsigned kMaxSteps = 8;                // nodes examined backward from the tail
constexpr unsigned kReadBudget = 25;             // reads per poll, recipe included
// Worst case of walk(): manager, tailpred, 8 type words and 7 links, marker, block, id, count, table, row, name.
static_assert(2 + 2 * kMaxSteps - 1 + 3 + 4 <= kReadBudget, "the walk must stay within its read budget");
constexpr std::uint64_t kStallMs = 250;          // no Present for longer than this: the frame is stalled
constexpr std::uint64_t kPollMs = 250;           // at most one poll per this interval

enum class Walk : unsigned { Found, ReadFailure, NoManager, Malformed, Head, Bound, Dead, NoScene, SameId, Loading, BadCount, BadIndex, BadRecord };
inline const char* name(Walk w) {
    switch (w) {
    case Walk::Found: return "found"; case Walk::ReadFailure: return "read_failure"; case Walk::NoManager: return "no_manager";
    case Walk::Malformed: return "malformed"; case Walk::Head: return "head"; case Walk::Bound: return "bound";
    case Walk::Dead: return "dead"; case Walk::NoScene: return "no_scene"; case Walk::SameId: return "same_id";
    case Walk::Loading: return "loading"; case Walk::BadCount: return "bad_count"; case Walk::BadIndex: return "bad_index";
    case Walk::BadRecord: return "bad_record";
    }
    return "invalid";
}
struct Result {
    Walk status = Walk::ReadFailure;
    unsigned steps = 0, reads = 0;               // nodes whose type word was read; reads issued
    std::uint32_t node = 0, id = 0;              // the candidate (value only)
    sector_background::Sample sample{};          // Ready with sector/index/record/family when Found
};
// Backward walk from the list tail (sector-transit-order.md §5): the first node whose type word is class 1
// subtype 0, then alive marker 0xcafe, scene +0x130 set, id +8 != last_ready_id (0: none known), then the
// record recipe. Every read is aligned and goes through `read` (engine_memory::read in production).
template<class Read> Result walk(Read& read, std::uint32_t last_ready_id) {
    namespace sb = sector_background;
    Result out;
    auto counted = [&](std::uintptr_t at, void* to, std::size_t n) { ++out.reads; return read(at, to, n); };
    std::uint32_t manager = 0, node = 0;
    if (!sb::field(counted, kManager, 0, manager)) return out;
    if (!manager) { out.status = Walk::NoManager; return out; }
    if (manager & 3) { out.status = Walk::Malformed; return out; }
    if (!sb::field(counted, manager, 0x10, node)) return out;
    const std::uint32_t head = manager + 8;
    for (;;) {
        if (!node || node == head) { out.status = Walk::Head; return out; }
        if (node & 3) { out.status = Walk::Malformed; return out; }
        std::uint32_t type = 0;
        if (!sb::field(counted, node, 0x48, type)) return out;
        ++out.steps;
        if ((type & 0xffff) == 1 && (type >> 16) == 0) break;
        if (out.steps == kMaxSteps) { out.status = Walk::Bound; return out; }
        if (!sb::field(counted, node, 4, node)) return out;
    }
    out.node = node;
    std::uint16_t marker = 0; std::uint32_t block[8]{};
    if (!sb::field(counted, node, 0x9c, marker)) return out;
    if (marker != 0xcafe) { out.status = Walk::Dead; return out; }
    if (!sb::field(counted, node, 0x130, block)) return out;   // scene, galaxy, dust, index, stars, neb, flags, size
    if (!block[0]) { out.status = Walk::NoScene; return out; }  // caught between the index write and the scene
    if (!sb::field(counted, node, 8, out.id)) return out;
    if (last_ready_id && out.id == last_ready_id) { out.status = Walk::SameId; return out; }
    sb::Sample& s = out.sample;
    s.sector = node; s.class48 = 1; s.sector_id = out.id; s.index = std::int32_t(block[3]);
    if (!sb::field(counted, 0x607040, 0, s.count) || !sb::field(counted, 0x606fc0, 0, s.table)) return out;
    if (!s.table || !s.count) { out.status = Walk::Loading; return out; }
    if (s.count < 0 || s.count > 4096) { out.status = Walk::BadCount; return out; }
    if (s.table & 3) { out.status = Walk::Malformed; return out; }
    if (s.index < 0 || s.index >= s.count) { out.status = Walk::BadIndex; return out; }
    if (!sb::read_record(counted, s)) { out.status = s.status == sb::Status::ReadFailure ? Walk::ReadFailure : Walk::BadRecord; return out; }
    s.status = sb::Status::Ready;
    out.status = Walk::Found;
    return out;
}

// The stall gate, called from the resource-creation hooks: the first compare is the whole cost outside a stall.
struct Gate {
    std::uint64_t present_ms = 0, poll_ms = 0;
    bool presented = false, polled = false;
    void present(std::uint64_t now) { present_ms = now; presented = true; polled = false; }
    bool stalled(std::uint64_t now) const { return presented && now - present_ms > kStallMs; }
    // Consumes the 250 ms slot; the caller checks the thread between stalled() and take().
    bool take(std::uint64_t now) {
        if (polled && now - poll_ms < kPollMs) return false;
        poll_ms = now; polled = true; return true;
    }
    bool due(std::uint64_t now) { return stalled(now) && take(now); }
};

// A started prefill, value only; decided at the detector's first Ready sample.
struct Record {
    bool pending = false;
    std::uint32_t sector = 0, id = 0;
    std::int32_t index = -1;
    unsigned profile = 0, recipe = 0;
    std::uint64_t key = 0;
    long long qpc = 0;                           // when the fill was started
};
// The poll's plan for a Found sector: a pending prefill of the same key and recipe is left alone; the resident
// field's own sector (the flown one, seen by its token: an in-flight hitch over 250 ms with its id unread, run273
// review F4) is kept; the resident key in another sector is re-centred as a cold start; anything else starts.
enum class Plan { AlreadyStarted, CurrentSector, Recentre, Start };
inline const char* name(Plan p) { return p == Plan::AlreadyStarted ? "already_started" : p == Plan::CurrentSector ? "current_sector" : p == Plan::Recentre ? "recentred" : "started"; }
inline Plan plan(const Record& r, std::uint64_t key, unsigned recipe, bool resident, bool same_token) {
    if (r.pending && r.key == key && r.recipe == recipe) return Plan::AlreadyStarted;
    if (resident && same_token) return Plan::CurrentSector;
    return resident ? Plan::Recentre : Plan::Start;
}
enum class Decision { None, Confirmed, Discarded };
inline const char* name(Decision d) { return d == Decision::Confirmed ? "confirmed" : d == Decision::Discarded ? "discarded" : "none"; }
// The key owns the field (fog_sector_policy.h): the same placement key keeps the fill whatever the heap token;
// `usable` is a Ready sample with a fog profile (a clear or refused sector discards).
inline Decision decide(const Record& r, bool usable, std::uint64_t key, unsigned recipe) {
    if (!r.pending) return Decision::None;
    return usable && key == r.key && recipe == r.recipe ? Decision::Confirmed : Decision::Discarded;
}
}  // namespace x3m::fog_prefill
