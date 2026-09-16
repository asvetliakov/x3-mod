#include "frame_timing.h"
#include "linear_cutout.h" // the two qualified cutout pairs, reported even at zero
#include <cstdio>
#include <cstdint>
#include <windows.h>

// The session log (src/proxy/capture.h), declared here rather than included:
// this translation unit uses nothing else from the proxy, so the host probe
// compiles it as it is against a Win32 stand-in and its own log sink
// (verification/probe/frame_timing_host.cpp).
namespace x3m { void log(const char* format, ...); }

namespace x3m::frame_timing {

bool active = false;

namespace {
constexpr unsigned draw_bucket = static_cast<unsigned>(Bucket::Draw);
constexpr unsigned scene_bucket = static_cast<unsigned>(Bucket::Scene);
constexpr unsigned state_bucket = static_cast<unsigned>(Bucket::State);
std::uint64_t frequency = 1; // QueryPerformanceFrequency, never zero
Window window;
std::uint64_t previous_qpc = 0;  // previous frame boundary
std::uint64_t present_stamp = 0; // stamp taken ahead of the forwarded Present
std::uint64_t present_us = 0;    // native Present time of the frame in progress
std::uint64_t prims = 0;         // primitives submitted in the frame in progress
// Per-frame hooked-call accounting, in ticks; converted once at the boundary.
std::uint64_t bucket_ticks[bucket_count]{};
std::uint64_t bucket_calls[bucket_count]{};
std::uint64_t draw_native_ticks = 0;
std::uint64_t draw_native_stamp = 0;
std::uint64_t slow_call_ticks = 0;
const char* slow_call_entry = "";
// Native Present ticks since attach. A Scope subtracts what accumulated inside
// it, so the buckets stay proxy-side; it is never reset at a frame boundary
// because the Present hook's own Scope spans that boundary.
std::uint64_t native_excluded = 0;
unsigned depth = 0; // hooked-call nesting: only the outermost entry is timed
// Every Nth hooked state call is stamped; zero (the default) stamps none and
// only counts them, because two QueryPerformanceCounter reads cost more than
// the hooked state call itself under FEX.
unsigned state_stamps = 0;
// One stride counter per entry-name slot (plus one for the overflow bucket),
// so the sampling period of each hooked entry runs independently and a fixed
// period cannot align with the game's repeating per-draw setter sequence.
unsigned state_stamp_counter[state_entry_slots + 1]{};
// Running total of measured hooked ticks since attach (every bucket plus the
// forwarded native Present), snapshotted at the frame boundary and at the
// frame's first and last draw so the unhooked remainder can be split by
// position without taking another stamp. Counted-only state calls contribute
// no ticks, so their time stays inside the gaps.
std::uint64_t hooked_ticks = 0;
std::uint64_t hooked_at_frame_start = 0;
std::uint64_t hooked_at_first_draw = 0;
std::uint64_t hooked_at_last_draw = 0;
std::uint64_t first_draw_stamp = 0; // entry of the frame's first draw hook
std::uint64_t last_draw_stamp = 0;  // return of the frame's last draw hook
// State-bucket calls by entry name. The name table is keyed by the static
// `__builtin_FUNCTION()` pointer the guard passes and is never cleared, so a
// slot means the same entry in every window; the counts are per frame.
const char* state_entry_name[state_entry_slots]{};
std::uint64_t state_entry_calls[state_entry_slots]{};
std::uint64_t state_other_calls = 0;
// The three count-only window diagnostics. Unlike the per-frame sample these
// accumulate over the whole window and are reset with it; only the batch
// classifier's "previous draw" is cleared at every frame boundary.
DrawPairs draw_pairs;
RedundantStates redundant_states;
DrawBatch draw_batch;

std::uint64_t stamp() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}
std::uint64_t microseconds(std::uint64_t ticks) noexcept { return ticks * 1000000ull / frequency; }
std::uint64_t difference(std::uint64_t a, std::uint64_t b) noexcept { return a > b ? a - b : 0; }

// One hash, one pointer compare and one increment for a repeated entry; at
// most eight probes before the call is counted as "other". No allocation and
// no clock read: this is the whole cost of an unstamped hooked state call.
unsigned count_state_entry(const char* entry) noexcept {
    if (!entry) { ++state_other_calls; return state_entry_slots; }
    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(entry);
    const unsigned hash = static_cast<unsigned>((key >> 4) ^ (key >> 9));
    for (unsigned probe = 0; probe < 8; ++probe) {
        const unsigned slot = (hash + probe) & (state_entry_slots - 1);
        if (state_entry_name[slot] == entry) { ++state_entry_calls[slot]; return slot; }
        if (!state_entry_name[slot]) {
            state_entry_name[slot] = entry;
            ++state_entry_calls[slot];
            return slot;
        }
    }
    ++state_other_calls;
    return state_entry_slots; // the overflow bucket keeps its own stride
}

// Decimal parse of X3M_FRAME_TIMING_STATE_STAMPS; anything else reads as zero.
unsigned stamp_interval(const wchar_t* text, unsigned length) noexcept {
    unsigned value = 0;
    if (!length || length > 6) return 0;
    for (unsigned i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 0;
        value = value * 10 + static_cast<unsigned>(text[i] - L'0');
    }
    return value;
}
}

void initialize() noexcept {
    const DWORD saved = GetLastError();
    wchar_t setting[8]{};
    active = GetEnvironmentVariableW(L"X3M_FRAME_TIMING", setting, 8) == 1 && setting[0] == L'1';
    if (active) {
        wchar_t stamps[8]{};
        state_stamps = stamp_interval(stamps, GetEnvironmentVariableW(L"X3M_FRAME_TIMING_STATE_STAMPS", stamps, 8));
        for (unsigned e = 0; e <= state_entry_slots; ++e) state_stamp_counter[e] = 0;
        hooked_ticks = hooked_at_frame_start = hooked_at_first_draw = hooked_at_last_draw = 0;
        first_draw_stamp = last_draw_stamp = state_other_calls = 0;
        for (unsigned e = 0; e < state_entry_slots; ++e) {
            state_entry_name[e] = nullptr;
            state_entry_calls[e] = 0;
        }
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        frequency = f.QuadPart > 0 ? static_cast<std::uint64_t>(f.QuadPart) : 1;
        window.reset();
        draw_pairs.reset(); redundant_states.reset(); draw_batch.reset();
        previous_qpc = present_stamp = present_us = prims = 0;
        draw_native_ticks = draw_native_stamp = slow_call_ticks = native_excluded = 0;
        slow_call_entry = "";
        depth = 0;
        for (unsigned b = 0; b < bucket_count; ++b) bucket_ticks[b] = bucket_calls[b] = 0;
    }
    SetLastError(saved);
}

namespace detail {

void present_begin_impl() noexcept {
    const DWORD saved = GetLastError();
    present_stamp = stamp();
    SetLastError(saved);
}

void present_end_impl() noexcept {
    const DWORD saved = GetLastError();
    if (present_stamp) {
        const std::uint64_t now = stamp();
        if (now > present_stamp) {
            present_us += microseconds(now - present_stamp);
            native_excluded += now - present_stamp;
            hooked_ticks += now - present_stamp;
        }
        present_stamp = 0;
    }
    SetLastError(saved);
}

void draw_native_begin_impl() noexcept {
    const DWORD saved = GetLastError();
    draw_native_stamp = stamp();
    SetLastError(saved);
}

void draw_native_end_impl() noexcept {
    const DWORD saved = GetLastError();
    if (draw_native_stamp) {
        const std::uint64_t now = stamp();
        if (now > draw_native_stamp) draw_native_ticks += now - draw_native_stamp;
        draw_native_stamp = 0;
    }
    SetLastError(saved);
}

// One QueryPerformanceCounter pair per outermost hooked call: no division, no
// allocation and no logging on this path. A reentrant hooked call (the proxy's
// own device calls inside a pass or inside Present) only moves the nesting
// depth, so its time stays attributed to the entry the game made.
void scope_begin_impl(ScopeState& state, unsigned bucket, const char* entry) noexcept {
    state.entered = true;
    state.timed = false;
    state.outermost = depth == 0;
    ++depth;
    if (!state.outermost) return;
    state.bucket = bucket;
    // A state call is counted here rather than at the end, because by default
    // it is not stamped at all: one increment and no QueryPerformanceCounter.
    if (bucket == state_bucket) {
        ++bucket_calls[state_bucket];
        const unsigned slot = count_state_entry(entry);
        if (!state_stamps || ++state_stamp_counter[slot] < state_stamps) return;
        state_stamp_counter[slot] = 0;
    }
    const DWORD saved = GetLastError();
    state.timed = true;
    state.entry = entry ? entry : "";
    state.exclude = native_excluded;
    state.start = stamp();
    SetLastError(saved);
}

void scope_end_impl(ScopeState& state) noexcept {
    state.entered = false;
    if (depth) --depth;
    if (!state.timed) return; // reentrant, or a counted-only state call
    state.timed = false;
    const DWORD saved = GetLastError();
    const std::uint64_t now = stamp();
    std::uint64_t ticks = now > state.start ? now - state.start : 0;
    const std::uint64_t excluded = native_excluded - state.exclude;
    ticks = ticks > excluded ? ticks - excluded : 0;
    if (state.bucket < bucket_count) {
        bucket_ticks[state.bucket] += ticks;
        if (state.bucket != state_bucket) ++bucket_calls[state.bucket];
    }
    // Sampled state ticks enter the hooked total scaled by the stride, the
    // same estimate the state bucket reports, so the gaps and the buckets sum
    // to dt at every sampling interval rather than only at N=1.
    hooked_ticks += state.bucket == state_bucket ? ticks * state_stamps : ticks;
    // Frame position of the draws, for the gap split: the first draw's entry
    // and the last draw's return, with the hooked total at each.
    if (state.bucket == draw_bucket) {
        if (!first_draw_stamp) {
            first_draw_stamp = state.start;
            hooked_at_first_draw = hooked_ticks - ticks;
        }
        last_draw_stamp = now;
        hooked_at_last_draw = hooked_ticks;
    }
    if (ticks > slow_call_ticks) { slow_call_ticks = ticks; slow_call_entry = state.entry; }
    SetLastError(saved);
}

void draw_impl(unsigned primitives) noexcept { prims += primitives; }

// Per-draw counters: one table mix with at most eight probes for the program
// pair, and one comparison of the key against the previous draw of the frame.
// No clock read, no allocation, no logging and no lock of its own.
void draw_state_impl(const DrawKey& key) noexcept {
    draw_pairs.record(key.vs, key.ps);
    draw_batch.record(key);
}
// Per-state-write counter: one increment, plus at most eight probes for the
// render-state attribution. The write itself is untouched.
void state_write_impl(unsigned set, unsigned entry, bool equal) noexcept {
    redundant_states.record(set, entry, equal);
}

void frame_impl(std::uint64_t frame, std::uint64_t draws) noexcept {
    const DWORD saved = GetLastError();
    const std::uint64_t now = stamp();
    // The first observed frame only starts the interval: its dt is unknown and
    // would otherwise carry the whole load time into the first window.
    if (previous_qpc && now > previous_qpc) {
        Frame sample{};
        sample.frame = frame;
        sample.dt_us = microseconds(now - previous_qpc);
        sample.present_us = present_us;
        sample.draws = draws;
        sample.prims = prims;
        for (unsigned b = 0; b < bucket_count; ++b) {
            sample.bucket_us[b] = microseconds(bucket_ticks[b]);
            sample.bucket_calls[b] = bucket_calls[b];
        }
        // The state bucket holds time only when calls are stamped: the sampled
        // sum scaled by the interval, or a calibrated estimate from the call
        // count, or the sentinel the log prints as -1.
        if (state_stamps)
            sample.bucket_us[state_bucket] = microseconds(bucket_ticks[state_bucket] * state_stamps);
        else if (state_calibration_ns)
            sample.bucket_us[state_bucket] = bucket_calls[state_bucket] * state_calibration_ns / 1000ull;
        else
            sample.bucket_us[state_bucket] = unknown_us;
        for (unsigned e = 0; e < state_entry_slots; ++e) sample.state_entry_calls[e] = state_entry_calls[e];
        sample.state_other_calls = state_other_calls;
        sample.draw_native_us = microseconds(draw_native_ticks);
        sample.slow_call = slow_call_entry;
        sample.slow_call_us = microseconds(slow_call_ticks);
        // The unhooked remainder, split by position from the stamps already
        // taken. Each part is the elapsed time of its region minus the hooked
        // ticks measured inside it; a hooked call that straddles the frame
        // boundary (the Present hook) is charged to the following frame in
        // both its bucket and gap_pre, so a gap can clamp at zero.
        const unsigned pre = static_cast<unsigned>(Gap::Pre);
        const unsigned mid = static_cast<unsigned>(Gap::Draw);
        const unsigned post = static_cast<unsigned>(Gap::Post);
        if (first_draw_stamp >= previous_qpc && last_draw_stamp >= first_draw_stamp && first_draw_stamp) {
            sample.gap_us[pre] = microseconds(difference(first_draw_stamp - previous_qpc,
                                                        difference(hooked_at_first_draw, hooked_at_frame_start)));
            sample.gap_us[mid] = microseconds(difference(last_draw_stamp - first_draw_stamp,
                                                        difference(hooked_at_last_draw, hooked_at_first_draw)));
            sample.gap_us[post] = microseconds(difference(difference(now, last_draw_stamp),
                                                         difference(hooked_ticks, hooked_at_last_draw)));
        } else { // no draw in the frame: the whole remainder is post-draw time
            sample.gap_us[post] = microseconds(difference(now - previous_qpc,
                                                         difference(hooked_ticks, hooked_at_frame_start)));
        }
        window.add(sample);
    }
    previous_qpc = now;
    hooked_at_frame_start = hooked_at_first_draw = hooked_at_last_draw = hooked_ticks;
    first_draw_stamp = last_draw_stamp = 0;
    present_us = 0;
    prims = 0;
    draw_native_ticks = 0;
    slow_call_ticks = 0;
    slow_call_entry = "";
    for (unsigned b = 0; b < bucket_count; ++b) bucket_ticks[b] = bucket_calls[b] = 0;
    for (unsigned e = 0; e < state_entry_slots; ++e) state_entry_calls[e] = 0;
    state_other_calls = 0;
    draw_batch.end_frame(); // draws are only compared inside one frame
    if (window.full()) {
        Summary s;
        if (window.close(s)) {
            // The per-entry state mix of the window, most-called first. Built
            // once per window, off the per-frame path.
            char top[256];
            int length = 0;
            for (unsigned i = 0; i < s.state_top_used && length >= 0 && length < int(sizeof top) - 1; ++i) {
                const char* name = state_entry_name[s.state_top[i].slot];
                const int written = std::snprintf(top + length, sizeof top - std::size_t(length), "%s%s:%llu",
                                                  i ? "," : "", name ? name : "unnamed",
                                                  static_cast<unsigned long long>(s.state_top[i].calls));
                if (written <= 0) break;
                length += written;
            }
            if (length <= 0 || length >= int(sizeof top)) std::snprintf(top, sizeof top, "none");
            // The redundant render-state entries of the window, most redundant
            // first, by D3DRS index. Built once per window like `top` above.
            RedundantStates::Entry redundant[redundant_top_count]{};
            const unsigned redundant_used = redundant_states.top(redundant, redundant_top_count);
            char redundant_top[96];
            int redundant_length = 0;
            for (unsigned i = 0; i < redundant_used && redundant_length >= 0
                                 && redundant_length < int(sizeof redundant_top) - 1; ++i) {
                const int written = std::snprintf(redundant_top + redundant_length,
                                                  sizeof redundant_top - std::size_t(redundant_length), "%s%u:%llu",
                                                  i ? "," : "", redundant[i].entry,
                                                  static_cast<unsigned long long>(redundant[i].count));
                if (written <= 0) break;
                redundant_length += written;
            }
            if (redundant_length <= 0 || redundant_length >= int(sizeof redundant_top))
                std::snprintf(redundant_top, sizeof redundant_top, "none");
            constexpr unsigned rs = static_cast<unsigned>(StateSet::RenderState);
            constexpr unsigned ss = static_cast<unsigned>(StateSet::SamplerState);
            constexpr unsigned tex = static_cast<unsigned>(StateSet::Texture);
            log("frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu present_max_us=%llu"
                " draw_p50_us=%llu draw_p95_us=%llu draw_max_us=%llu draw_native_p50_us=%llu draw_native_max_us=%llu"
                " scene_p50_us=%llu scene_p95_us=%llu scene_max_us=%llu state_p50_us=%lld state_p95_us=%lld state_max_us=%lld"
                " draw_calls_p50=%llu scene_calls_p50=%llu state_calls_p50=%llu state_sampled=%u slow=%u"
                " gap_pre_p50_us=%llu gap_pre_p95_us=%llu gap_pre_max_us=%llu"
                " gap_draw_p50_us=%llu gap_draw_p95_us=%llu gap_draw_max_us=%llu"
                " gap_post_p50_us=%llu gap_post_p95_us=%llu gap_post_max_us=%llu gap_draw_per_draw_us=%llu.%03llu"
                " state_top=%s state_other_p50=%llu"
                " state_redundant=%llu,%llu,%llu state_shadowed=%llu,%llu,%llu redundant_top=%s",
                s.frame, s.frames, s.dt_p50, s.dt_p95, s.dt_max, s.draws_p50, s.draws_max,
                s.present_p50, s.present_p95, s.present_max,
                s.bucket_p50[draw_bucket], s.bucket_p95[draw_bucket], s.bucket_max[draw_bucket],
                s.draw_native_p50, s.draw_native_max,
                s.bucket_p50[scene_bucket], s.bucket_p95[scene_bucket], s.bucket_max[scene_bucket],
                static_cast<long long>(s.bucket_p50[state_bucket]), static_cast<long long>(s.bucket_p95[state_bucket]),
                static_cast<long long>(s.bucket_max[state_bucket]),
                s.bucket_calls_p50[draw_bucket], s.bucket_calls_p50[scene_bucket], s.bucket_calls_p50[state_bucket],
                state_stamps, s.slow,
                s.gap_p50[0], s.gap_p95[0], s.gap_max[0],
                s.gap_p50[1], s.gap_p95[1], s.gap_max[1],
                s.gap_p50[2], s.gap_p95[2], s.gap_max[2],
                s.gap_draw_per_draw_ns / 1000ull, s.gap_draw_per_draw_ns % 1000ull,
                top, s.state_other_p50,
                redundant_states.redundant(rs), redundant_states.redundant(ss), redundant_states.redundant(tex),
                redundant_states.shadowed(rs), redundant_states.shadowed(ss), redundant_states.shadowed(tex),
                redundant_top);
            // The window's program-pair mix, most-drawn first, and the two
            // qualified cutout pairs explicitly (zero is the finding).
            DrawPairs::Pair pairs[draw_pair_top_count]{};
            const unsigned pairs_used = draw_pairs.top(pairs, draw_pair_top_count);
            char pair_top[512];
            int pair_length = 0;
            for (unsigned i = 0; i < pairs_used && pair_length >= 0 && pair_length < int(sizeof pair_top) - 1; ++i) {
                char vs[20], ps[20];
                if (pairs[i].vs) std::snprintf(vs, sizeof vs, "%016llx", static_cast<unsigned long long>(pairs[i].vs));
                else std::snprintf(vs, sizeof vs, "none");
                if (pairs[i].ps) std::snprintf(ps, sizeof ps, "%016llx", static_cast<unsigned long long>(pairs[i].ps));
                else std::snprintf(ps, sizeof ps, "none");
                const int written = std::snprintf(pair_top + pair_length, sizeof pair_top - std::size_t(pair_length),
                                                  "%s%s/%s:%llu", i ? "," : "", vs, ps,
                                                  static_cast<unsigned long long>(pairs[i].draws));
                if (written <= 0) break;
                pair_length += written;
            }
            if (pair_length <= 0 || pair_length >= int(sizeof pair_top)) std::snprintf(pair_top, sizeof pair_top, "none");
            log("draw_pairs frame=%llu draws=%llu draw_pairs_overflow=%llu top=%s cutout_pairs=%llu,%llu",
                s.frame, draw_pairs.draws(), draw_pairs.overflow(), pair_top,
                draw_pairs.draws_of(cutout::pair_hashes[2], cutout::pair_hashes[3]),
                draw_pairs.draws_of(cutout::pair_hashes[0], cutout::pair_hashes[1]));
            log("draw_batch frame=%llu same_mesh=%llu same_mesh_any_range=%llu same_material=%llu draws=%llu",
                s.frame, draw_batch.same_mesh(), draw_batch.same_mesh_any_range(),
                draw_batch.same_material(), draw_batch.draws());
            draw_pairs.reset(); redundant_states.reset(); draw_batch.reset();
            for (unsigned i = 0; i < s.slow_frames_count; ++i) {
                const Frame& f = s.slow_frames[i];
                log("frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu"
                    " draw_us=%llu draw_native_us=%llu scene_us=%llu state_us=%lld"
                    " draw_calls=%llu scene_calls=%llu state_calls=%llu slow_call=%s slow_call_us=%llu"
                    " gap_pre_us=%llu gap_draw_us=%llu gap_post_us=%llu",
                    f.frame, f.dt_us, f.draws, f.present_us, f.prims,
                    f.bucket_us[draw_bucket], f.draw_native_us, f.bucket_us[scene_bucket],
                    static_cast<long long>(f.bucket_us[state_bucket]),
                    f.bucket_calls[draw_bucket], f.bucket_calls[scene_bucket], f.bucket_calls[state_bucket],
                    f.slow_call && f.slow_call[0] ? f.slow_call : "none", f.slow_call_us,
                    f.gap_us[0], f.gap_us[1], f.gap_us[2]);
            }
        }
    }
    SetLastError(saved);
}

}
}
