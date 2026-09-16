#pragma once
#include <cstdint>
#include "stamp_core.h"

// Value-only state of the media-cue gate (X3M_MEDIA_CUE_TRACE=1 /
// X3M_MEDIA_CUE_CACHE=1) on the media-record allocator 0x00498140: caller
// classification from the return addresses on the game's stack, the negative
// cache of media ids whose last selector-path build returned 0, the pending
// stack that pairs each proceeded entry with its captured return, the trace
// ring the frame boundary drains, the per-second line limit and the 300-frame
// window reduction. No Win32, no allocation, no locking, no floating point.
// Host-tested by verification/probe/media_cue_host.cpp.
namespace x3m::media_cue::detail {
using Gate = x3m::stamp::Gate;
enum Caller : unsigned char { selector = 0, speech = 1, script = 2, savegame = 3, query = 4, other = 5, caller_count = 6 };
inline constexpr const char* const caller_names[caller_count] = {"selector", "speech", "script", "savegame", "query", "other"};

// The discriminators (media_cue_sites.h holds the EXE's values; the CPU fixture
// supplies its own trampolines).
struct Addresses {
    std::uint32_t query_return = 0, savegame_return = 0, script_return = 0, speech_return = 0;
    std::uint32_t helper_return = 0;    // [esp]: the play helper 0x004f65f0
    std::uint32_t selector_return = 0;  // [esp+0xc] behind the helper: the sector selector
    std::uint32_t selector_kind = 0;    // [esp+0x10] behind the helper: the cue kind
};
// `ret` is [esp] at entry, `slot_c` is [esp+0xc]. The selector is the play
// helper called from the selector; every other helper caller is `other`.
inline Caller classify(std::uint32_t ret, std::uint32_t slot_c, const Addresses& a) noexcept {
    if (ret == a.helper_return) return slot_c == a.selector_return ? selector : other;
    if (ret == a.speech_return) return speech;
    if (ret == a.script_return) return script;
    if (ret == a.savegame_return) return savegame;
    if (ret == a.query_return) return query;
    return other;
}
// The negative cache applies only here: the per-sector cue restart.
inline bool selector_scope(Caller caller, std::uint32_t kind, const Addresses& a) noexcept {
    return caller == selector && kind == a.selector_kind;
}

enum Outcome : unsigned char { unobserved = 0, observed = 1, refused = 2 };
struct Entry {
    std::uint64_t qpc = 0, frame = 0, duration_ticks = 0;
    std::uint32_t id = 0, kind = 0, flags = 0, result = 0, attempt = 0;
    Caller caller = other;
    Outcome outcome = unobserved;
    bool scoped = false;  // selector path with the selector's kind
};

// Fixed table of ids whose last scoped build returned 0, with the failure's
// clock. A full table evicts the oldest failure. A success for an id from any
// caller clears it. Refusals inside the retry interval are counted per slot.
inline constexpr unsigned cache_entries = 32;
struct NegativeCache {
    struct Slot { std::uint32_t id = 0; std::uint64_t failed_qpc = 0; std::uint32_t failures = 0, refusals = 0; bool used = false; };
    Slot slots[cache_entries]{};
    unsigned used = 0;
    std::uint64_t evictions = 0, clock_errors = 0;
    Slot* find(std::uint32_t id) noexcept {
        for (auto& s : slots) if (s.used && s.id == id) return &s;
        return nullptr;
    }
    bool refuses(std::uint32_t id, std::uint64_t now, std::uint64_t retry_ticks) noexcept {
        Slot* s = find(id);
        if (!s) return false;
        if (now < s->failed_qpc) { ++clock_errors; return false; }  // a backward clock never refuses
        if (now - s->failed_qpc >= retry_ticks) return false;       // the retry is due
        ++s->refusals;
        return true;
    }
    void fail(std::uint32_t id, std::uint64_t now) noexcept {
        Slot* s = find(id);
        if (!s) {
            for (auto& candidate : slots) if (!candidate.used) { s = &candidate; break; }
            if (!s) {
                s = &slots[0];
                for (auto& candidate : slots) if (candidate.failed_qpc < s->failed_qpc) s = &candidate;
                ++evictions;
            } else ++used;
            *s = Slot{}; s->used = true; s->id = id;
        }
        s->failed_qpc = now; ++s->failures;
    }
    void success(std::uint32_t id) noexcept {
        if (Slot* s = find(id)) { *s = Slot{}; --used; }
    }
    void clear() noexcept { for (auto& s : slots) s = Slot{}; used = 0; evictions = clock_errors = 0; }
};

// Proceeded entries whose return the gate captured, innermost last. A nested
// call (COM apartment dispatch inside the graph constructor) pushes a second
// entry; its return pops first. `esp` is the game's stack slot that held the
// return address, so a return matches its entry exactly. An entry whose frame
// was unwound past the trampoline (exception, longjmp) never returns through
// it: it is reclaimed as `stale` either when a later return matches an entry
// below it, or when a new call arrives at the same or a higher stack address
// (the stack grows down, so a live outer entry always has a higher `esp` than
// the call nested inside it). `last_return` is the most recent substituted
// real return address, the fail-safe for a return that matches no entry.
inline constexpr unsigned pending_depth = 4;
struct Pending {
    std::uint32_t ret = 0, esp = 0, id = 0, kind = 0, flags = 0, attempt = 0;
    std::uint64_t qpc = 0, frame = 0;
    Caller caller = other;
    bool scoped = false;
};
struct PendingStack {
    Pending items[pending_depth]{};
    unsigned depth = 0, max_depth = 0;
    std::uint64_t overflow = 0, stale = 0, lost = 0, mismatched = 0;
    std::uint32_t last_return = 0;
    bool push(const Pending& p) noexcept {
        while (depth && items[depth - 1].esp <= p.esp) { --depth; ++stale; }  // unwound frames at or below the new call
        if (depth >= pending_depth) { ++overflow; return false; }
        items[depth++] = p; last_return = p.ret;
        if (depth > max_depth) max_depth = depth;
        return true;
    }
    bool pop(std::uint32_t esp, Pending& out) noexcept {
        for (unsigned i = depth; i-- > 0;) {
            if (items[i].esp != esp) continue;
            out = items[i];
            stale += depth - i - 1;
            depth = i;
            return true;
        }
        ++lost;
        return false;
    }
};

// Entries the handlers record, drained by the frame boundary on the same
// (owner) thread; a full ring drops the oldest and counts it.
inline constexpr unsigned ring_entries = 64;
struct TraceRing {
    Entry items[ring_entries]{};
    unsigned head = 0, count = 0;
    std::uint64_t dropped = 0;
    void push(const Entry& e) noexcept {
        items[(head + count) % ring_entries] = e;
        if (count < ring_entries) ++count;
        else { head = (head + 1) % ring_entries; ++dropped; }
    }
    bool pop(Entry& out) noexcept {
        if (!count) return false;
        out = items[head]; head = (head + 1) % ring_entries; --count;
        return true;
    }
};

// The first `lines_per_second` trace lines of each clock second are emitted;
// the rest are counted as suppressed and stay in the window summary.
inline constexpr unsigned lines_per_second = 32;
struct RateLimit {
    std::uint64_t second_start = 0, suppressed = 0;
    unsigned emitted = 0;
    bool admit(std::uint64_t now, std::uint64_t frequency) noexcept {
        if (!frequency) frequency = 1;
        if (!second_start || now < second_start || now - second_start >= frequency) { second_start = now; emitted = 0; }
        if (emitted < lines_per_second) { ++emitted; return true; }
        ++suppressed;
        return false;
    }
};

inline constexpr unsigned window_frames = 300, id_slots = 8;
struct IdCount { std::uint32_t id = 0, count = 0; };
struct Summary {
    std::uint64_t frame = 0;
    unsigned frames = 0;
    std::uint64_t attempts = 0, failures = 0, successes = 0, refused = 0, unobserved = 0;
    std::uint64_t attempts_frame_p50 = 0;
    std::uint32_t attempts_frame_max = 0;
    IdCount ids[id_slots]{};
    unsigned id_count = 0;
    std::uint64_t id_overflow = 0;  // attempts for ids beyond the eight slots
};
// Fixed 300-frame window: `frame` stores the frame's attempt count, `entry`
// adds a drained entry; `close` runs one std::nth_element, once per window.
class Window {
public:
    bool full() const noexcept { return frames_ >= window_frames; }
    unsigned frames() const noexcept { return frames_; }
    void frame(std::uint64_t frame_index, std::uint32_t attempts) noexcept {
        if (frames_ < window_frames) attempts_[frames_++] = attempts;
        if (attempts > max_) max_ = attempts;
        last_frame_ = frame_index;
    }
    void entry(const Entry& e) noexcept {
        ++attempts_total_;
        if (e.outcome == refused) ++refused_;
        else if (e.outcome == unobserved) ++unobserved_;
        else if (e.result) ++successes_;
        else ++failures_;
        for (unsigned i = 0; i < id_count_; ++i) if (ids_[i].id == e.id) { ++ids_[i].count; return; }
        if (id_count_ < id_slots) { ids_[id_count_].id = e.id; ids_[id_count_++].count = 1; }
        else ++id_overflow_;
    }
    bool close(Summary& out) noexcept {
        if (!frames_) { reset(); return false; }
        out = Summary{};
        out.frame = last_frame_; out.frames = frames_;
        out.attempts = attempts_total_; out.failures = failures_; out.successes = successes_;
        out.refused = refused_; out.unobserved = unobserved_;
        out.attempts_frame_p50 = x3m::stamp::percentile(attempts_, frames_, 50, scratch_);
        out.attempts_frame_max = max_;
        out.id_count = id_count_; out.id_overflow = id_overflow_;
        for (unsigned i = 0; i < id_count_; ++i) out.ids[i] = ids_[i];
        reset();
        return true;
    }
    void reset() noexcept {
        frames_ = 0; last_frame_ = 0; max_ = 0; attempts_total_ = failures_ = successes_ = refused_ = unobserved_ = 0;
        id_count_ = 0; id_overflow_ = 0;
        for (auto& c : ids_) c = IdCount{};
    }

private:
    std::uint64_t attempts_[window_frames]{}, scratch_[window_frames]{};
    unsigned frames_ = 0;
    std::uint64_t last_frame_ = 0;
    std::uint32_t max_ = 0;
    std::uint64_t attempts_total_ = 0, failures_ = 0, successes_ = 0, refused_ = 0, unobserved_ = 0, id_overflow_ = 0;
    IdCount ids_[id_slots]{};
    unsigned id_count_ = 0;
};
}
