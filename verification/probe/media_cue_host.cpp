// Host checks of the media-cue gate policy in src/proxy/media_cue_core.h:
// caller classification from the stacked return addresses, the negative cache
// (fail -> refuse inside the interval -> retry after it -> cleared by a
// success; a backward clock never refuses; a full table evicts the oldest
// failure; non-selector callers are never scoped), the pending stack (nested
// returns pop innermost first, an unwound entry is stale, an unmatched return
// is lost, depth exhaustion is overflow), the trace ring (oldest dropped), the
// 32-lines-per-second limit and the 300-frame window reduction (attempt p50/
// max, outcome counts, the eight id slots) and no allocation. No Win32, no game.
#include "../../src/proxy/media_cue_core.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) { std::fprintf(stderr, "allocated\n"); std::abort(); } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace x3m::media_cue::detail;
static unsigned checks = 0;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "check %u failed\n", checks); std::exit(1); } }

static const Addresses game{0x0049873f, 0x00498bb3, 0x00498cdd, 0x00498efd, 0x004f6615, 0x0045c60c, 0x5a};
static NegativeCache cache;   // static storage like the production objects
static PendingStack pending;
static TraceRing ring;
static RateLimit limit;
static Window window;
static Gate gate;
static constexpr std::uint64_t frequency = 1000000, retry = 30 * frequency;  // one tick per microsecond, 30 s

static Entry entry(std::uint32_t id, Caller caller, Outcome outcome, std::uint32_t result, std::uint64_t qpc) {
    Entry e; e.id = id; e.caller = caller; e.outcome = outcome; e.result = result; e.qpc = qpc; return e;
}

int main() {
    refuse_allocation = true;
    // Classification: the play helper is the selector only behind the selector's return address.
    check(classify(0x004f6615, 0x0045c60c, game) == selector);
    check(classify(0x004f6615, 0x00460429, game) == other);
    check(classify(0x004f6615, 0x004f683b, game) == other);
    check(classify(0x00498efd, 0x0045c60c, game) == speech);
    check(classify(0x00498cdd, 0, game) == script);
    check(classify(0x00498bb3, 0, game) == savegame);
    check(classify(0x0049873f, 0, game) == query);
    check(classify(0x00401000, 0x0045c60c, game) == other);
    check(selector_scope(selector, 0x5a, game) && !selector_scope(selector, 0x11, game) && !selector_scope(speech, 0x5a, game));
    // Cache: fail -> refuse within the interval -> retry after it -> success clears.
    std::uint64_t now = 1000;
    check(!cache.refuses(812, now, retry) && cache.used == 0);
    cache.fail(812, now);
    check(cache.used == 1 && cache.find(812)->failures == 1);
    check(cache.refuses(812, now + 1, retry) && cache.refuses(812, now + retry - 1, retry) && cache.find(812)->refusals == 2);
    check(!cache.refuses(812, now + retry, retry));           // the retry is due exactly at the interval
    check(!cache.refuses(812, now - 1, retry) && cache.clock_errors == 1);  // a backward clock never refuses
    cache.fail(812, now + retry);                             // the retry failed again: the interval restarts
    check(cache.find(812)->failures == 2 && cache.refuses(812, now + retry + 1, retry));
    check(!cache.refuses(813, now, retry));                   // another id is not affected
    cache.success(812);
    check(cache.used == 0 && !cache.refuses(812, now + retry + 2, retry));
    cache.success(999);                                       // a success for an unknown id is a no-op
    check(cache.used == 0);
    // Table full: the oldest failure is evicted.
    for (std::uint32_t id = 0; id < cache_entries; ++id) cache.fail(1000 + id, now + id);
    check(cache.used == cache_entries && cache.evictions == 0);
    cache.fail(2000, now + 100);
    check(cache.used == cache_entries && cache.evictions == 1 && !cache.find(1000) && cache.find(1001) && cache.find(2000));
    check(!cache.refuses(1000, now + 101, retry) && cache.refuses(2000, now + 101, retry));
    cache.clear();
    check(cache.used == 0 && cache.evictions == 0);
    // Pending stack: nested returns pop innermost first; an unwound entry is stale.
    Pending p; p.esp = 0x1000; p.ret = 0x004f6615; p.id = 1;
    check(pending.push(p) && pending.depth == 1);
    p.esp = 0x0f00; p.ret = 0x00498efd; p.id = 2;
    check(pending.push(p) && pending.depth == 2 && pending.max_depth == 2);
    Pending out;
    check(pending.pop(0x0f00, out) && out.id == 2 && pending.depth == 1 && pending.stale == 0);
    check(pending.pop(0x1000, out) && out.id == 1 && pending.depth == 0);
    p.esp = 0x1000; p.id = 3; pending.push(p); p.esp = 0x0f00; p.id = 4; pending.push(p);
    check(pending.pop(0x1000, out) && out.id == 3 && pending.depth == 0 && pending.stale == 1);  // the inner entry was unwound past its return
    check(!pending.pop(0x2000, out) && pending.lost == 1);
    pending = {};
    // An entry unwound past the trampoline is reclaimed when a later call arrives at the same or a higher ESP.
    p.esp = 0x1000; p.id = 5; p.ret = 0xa1; check(pending.push(p) && pending.last_return == 0xa1);
    p.esp = 0x1000; p.id = 6; p.ret = 0xa2; check(pending.push(p) && pending.depth == 1 && pending.stale == 1 && pending.items[0].id == 6 && pending.last_return == 0xa2);
    p.esp = 0x0ff0; p.id = 7; check(pending.push(p) && pending.depth == 2);        // a nested call keeps the outer entry
    p.esp = 0x1010; p.id = 8; check(pending.push(p) && pending.depth == 1 && pending.stale == 3 && pending.items[0].id == 8);
    for (unsigned i = 0; i < 100; ++i) { p.esp = 0x1010; check(pending.push(p) && pending.depth == 1); }  // repeated unwinds at one frame never exhaust the slots
    check(pending.stale == 103);
    pending = {};
    for (unsigned i = 0; i < pending_depth; ++i) { p.esp = 0x3000 - 0x10 * i; check(pending.push(p)); }
    p.esp = 0x2000; check(!pending.push(p) && pending.overflow == 1 && pending.depth == pending_depth);
    pending = {};
    // Ring: FIFO, the oldest dropped when full.
    for (std::uint32_t i = 0; i < ring_entries + 3; ++i) ring.push(entry(i, selector, observed, 0, i));
    check(ring.count == ring_entries && ring.dropped == 3);
    Entry e;
    check(ring.pop(e) && e.id == 3);
    unsigned drained = 1; while (ring.pop(e)) ++drained;
    check(drained == ring_entries && e.id == ring_entries + 2 && ring.count == 0);
    // Rate limit: 32 lines per clock second, then suppressed; a new second resets.
    unsigned admitted = 0;
    for (unsigned i = 0; i < 100; ++i) admitted += limit.admit(5 * frequency + i, frequency);
    check(admitted == lines_per_second && limit.suppressed == 100 - lines_per_second);
    check(limit.admit(6 * frequency, frequency) && limit.emitted == 1);
    check(limit.admit(4 * frequency, frequency));  // a backward clock starts a new second rather than blocking
    // Gate: early before admission, foreign from another thread.
    check(!gate.owned(7) && gate.early.load() == 1);
    check(gate.admit(7) && gate.owned(7) && !gate.owned(8) && gate.foreign.load() == 1 && !gate.admit(8));
    // Window: attempts per frame, outcome counts, the eight id slots, p50 and max.
    Summary s;
    check(!window.close(s));
    for (unsigned f = 1; f <= 10; ++f) {
        window.frame(f, f <= 5 ? 1 : 3);
        for (unsigned a = 0; a < (f <= 5 ? 1u : 3u); ++a) window.entry(entry(812, selector, f == 1 ? observed : refused, 0, f));
    }
    window.entry(entry(245, speech, observed, 0x40001000, 11));
    window.entry(entry(300, script, unobserved, 0, 12));
    for (std::uint32_t id = 0; id < 9; ++id) window.entry(entry(5000 + id, other, observed, 1, 13));
    check(!window.full() && window.frames() == 10);
    check(window.close(s));
    check(s.frame == 10 && s.frames == 10 && s.attempts == 20 + 2 + 9 && s.failures == 1 && s.refused == 19 && s.successes == 10 && s.unobserved == 1);
    check(s.attempts_frame_p50 == 3 && s.attempts_frame_max == 3);
    check(s.id_count == id_slots && s.ids[0].id == 812 && s.ids[0].count == 20 && s.ids[1].id == 245 && s.ids[2].id == 300 && s.id_overflow == 4);
    check(!window.close(s));
    for (unsigned f = 0; f < window_frames; ++f) window.frame(f, 0);
    check(window.full() && window.close(s) && s.frames == window_frames && s.attempts == 0 && s.attempts_frame_p50 == 0 && s.id_count == 0);
    // Video blit witness: the consumer's return range and the line cadence.
    Addresses video_game = game; video_game.blit_begin = 0x004d0c40; video_game.blit_end = 0x004d14e0;
    check(video_blit_caller(0x004d0d27, video_game) && video_blit_caller(0x004d14ba, video_game) && video_blit_caller(0x004d0c40, video_game) && video_blit_caller(0x004d14df, video_game));
    check(!video_blit_caller(0x004d14e0, video_game) && !video_blit_caller(0x004d0c3f, video_game) && !video_blit_caller(0x00401000, video_game));
    check(!video_blit_caller(0x004d0d27, game) && !video_blit_caller(0, game));  // an empty range admits nothing
    VideoBlit video;
    check(video.lock_enter() && video.lock_result(false) && !video.lock_result(false) && video.locks == 1 && video.locks_total == 1 && video.depth == 0);
    unsigned lines = 0;
    for (unsigned i = 1; i < 3 * video_blit_line_interval; ++i) { const bool line = video.lock_enter(); lines += line; check(video.lock_result(i == 7) == line); }
    check(lines == 2 && video.locks == 3 * video_blit_line_interval && video.failures == 1);
    check(video.unlock_enter() && video.unlock_result(true) && !video.unlock_enter() && !video.unlock_result(false) && video.unlocks == 2 && video.failures == 2);
    // A re-entrant in-range call between an enter and its result: counted, no line, the outer pair still matched.
    check(video.lock_enter() && video.depth == 1);                       // blit 181 is on the interval
    check(!video.lock_enter() && video.reentries == 1 && video.depth == 2 && video.locks == 3 * video_blit_line_interval + 1);
    check(!video.lock_result(true) && video.depth == 1 && video.failures == 3);
    check(video.lock_result(false) && video.depth == 0);                 // the outer result keeps its line
    for (unsigned i = 0; i < video_blit_line_interval - 1; ++i) { check(!video.lock_enter()); check(!video.lock_result(false)); }  // blits 182..240
    check(video.lock_enter() && video.depth == 1 && video.locks_total == 4 * video_blit_line_interval + 1);  // blit 241 is on the interval
    check(!video.unlock_enter() && video.reentries == 2 && !video.unlock_result(false) && video.depth == 1);  // a nested unlock does not steal the outer line
    check(video.lock_result(false) && video.depth == 0);
    video.close();
    check(video.locks == 0 && video.unlocks == 0 && video.failures == 0 && video.reentries == 0 && video.locks_total == 4 * video_blit_line_interval + 1 && video.unlock_written);
    refuse_allocation = false;
    std::printf("media_cue_host checks=%u failures=0 cache_bytes=%zu pending_bytes=%zu ring_bytes=%zu window_bytes=%zu cache_entries=%u pending_depth=%u lines_per_second=%u\n",
                checks, sizeof cache, sizeof pending, sizeof ring, sizeof window, cache_entries, pending_depth, lines_per_second);
    return 0;
}
