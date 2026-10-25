#pragma once
#include "seta_restore_core.h"
#include <cstdint>

// The SETA key stop latch (part of X3M_SETA_MANUAL_FLIGHT >= 1; docs/architecture/seta-manual-flight.md "Key stop latch";
// engine facts docs/reverse-engineering/seta-manual-flight.md section 7). One held press of the SETA key reaches the
// script's 150::ToggleFastForward an even number of times when another ring entry is read in between (a steering key
// held with it, an empty or slot-3 read), so a press made to stop SETA can start it again; vanilla hid the restart because
// steering stopped SETA, which part A removes. The latch arms in the record-only TI_SetTimeWarpFactor hook 0x004b148c
// (owned by seta_restore) on the toggle's stop and, at the shared injection site 0x00410100, calls the story's own
// `0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0)` once per main-loop iteration while the SETA action's held counter
// shows the key and for tail_entries iterations after it reads 0. StopFastForward does nothing while SETA is off.
// Pure code the host test compiles (verification/probe/seta_key_latch_host.cpp): no Windows header, no allocation, no
// floating point.
namespace x3m::seta_key_latch::core {
namespace sr = x3m::seta_restore::core;

// ---- engine addresses (seta_key_toggle_study.py pins each against X3AP.exe) ----
// The input routine's action counters: memset to 0 at 0x0041013c (after the injection stub's entry), counter[action] += 1
// at 0x00410a6a for every held key bound to the action. At the 0x00410100 entry the bytes still hold the previous
// iteration's counts; the main thread is their only writer, the handler reads them on the same thread.
constexpr std::uint32_t action_counters_va = 0x0057fc68;
constexpr unsigned seta_action = 675;
constexpr std::uint32_t seta_counter_va = action_counters_va + seta_action; // u8
static_assert(seta_counter_va == 0x0057ff0b, "the SETA action's counter byte");
// Diagnostic only (seta_key rows): the iteration's ring read (u16 [0x0060903c], stored by 0x004042a2 before Input), the
// reader and writer indices (u16), the four 12-byte ring slots' DIK bytes, the steering counters (actions 524..529),
// the input context's joystick and mouse button words and the warp factor.
constexpr std::uint32_t ring_read_va = 0x0060903c, ring_ri_va = 0x00578488, ring_wi_va = 0x0057847c,
                        ring_slots_va = 0x00595ee8;
constexpr unsigned ring_slot_stride = 12, ring_slot_count = 4;
constexpr unsigned ring_span = ring_slot_stride * (ring_slot_count - 1) + 1; // 37 bytes: slot 0 .. slot 3's DIK byte
constexpr std::uint32_t steer_counters_va = action_counters_va + 524;      // 0x0057fe74
constexpr unsigned steer_count = 6;
static_assert(steer_counters_va == 0x0057fe74, "the yaw/pitch/roll counters 524..529");
constexpr std::uint32_t input_ctx_ptr_va = 0x00606f3c;
constexpr unsigned ctx_joystick_buttons = 0x450, ctx_mouse_buttons = 0x454;

// ---- the call (the StartFastForward call's wrapper and shape; the EXE's own StopFastForward call 0x0041211f is the
// stop_call window seta_restore compares before it installs) ----
inline constexpr char call_method[] = "StopFastForward";
constexpr unsigned toggle_named = 1; // story::seta_named[1]: 150::ToggleFastForward's range
static_assert(sizeof(story::seta_named) / sizeof(story::seta_named[0]) > toggle_named, "toggle range present");

// ---- bounds, in main-loop iterations (one 0x00410100 entry, one keyboard poll and one ring read each) ----
// The key path runs in real time: SETA scales the game time a frame advances (cfg+0xcc), not the loop's iteration
// count, and each iteration polls, reads one ring entry and runs Input once whatever the factor. The hazard the tail
// covers is counted in the same unit: after the release the counter lags one iteration and the ring drains in at most
// four reads, so 8 iterations cover it at any frame rate and any warp factor. A re-delivered toggle inside the window is
// stopped in the iteration it happened (the entry follows the iteration's read and Input), before any ramp step (> 250 ms).
constexpr unsigned tail_entries = 8;
// A counter that never reads 0 (a stuck binding): released after this many iterations (about 9 s at the measured median
// 222 fps; the press itself is user time, the bound only ends a latch nobody is holding).
constexpr unsigned cap_entries = 2048;

// ---- debug-tier row budgets per session ----
constexpr unsigned factor_row_cap = 2000, key_row_cap = 4000, key_held_rows_per_press = 96, latch_row_cap = 1000;

// A start press near the ramp: a press that starts SETA is re-delivered within at most four ring reads (section 7.2), and
// the run's first ramp step comes at least one read after the start, so a stop that follows a run's first step (old 1.0,
// new > 1.0) by at most this many frames is that press's own re-delivery, not a press made to stop SETA: not armed.
// Reachable only below about 16 fps (four reads > 250 ms); counted in presented frames, one per loop iteration.
constexpr unsigned ramp_guard_frames = 4;

// The toggle's stop: StopFastForward's write (IP = the detected stop IP) returning into 150::ToggleFastForward, with SETA
// on (old > 1.0) and new 1.0. The write a press made to stop SETA produces first. A start writes no factor, and a stop
// inside one start press is at old 1.0 or (below 16 fps) inside ramp_guard_frames of the first step, so a start press
// from idle never arms the latch; one that begins inside an armed latch's tail is the accepted edge (on_entry).
inline bool stop_shaped(const sr::Write& w) noexcept {
    return w.readable && w.tag == sr::int_tag && w.old_factor > sr::factor_one && w.new_factor == sr::factor_one;
}
inline bool key_stop(const sr::Write& w) noexcept {
    const story::SetaSites* s = w.sites;
    return stop_shaped(w) && s && s->outcome.ok() && w.ip == s->stop_ip && w.ret.scan == sr::Scan::found &&
           s->named[toggle_named].contains(w.ret.offset);
}

enum class Event : unsigned { none = 0, armed = 1, fire = 2, released = 3, refused = 4 };
enum Reason : unsigned {
    reason_none = 0,
    released_tail = 1,     // the counter read 0 on tail_entries consecutive iterations
    released_new_press = 2, // the counter rose after a 0 that was not the arm's own lag entry: a new press, released
    released_cap = 3,       // cap_entries iterations
    released_vm = 4,        // VM clear or load
    refused_story_sites = 5, // a SETA stop while the story sites are not detected: not armed for it (row once per session)
    refused_no_toggle = 6,   // StopFastForward's write with the sites detected but no ToggleFastForward range: not armed
    refused_unreadable = 7,  // the counter byte unreadable: latch off for the session
    refused_call_unresolved = 8, // the wrapper returned AL 0 (StopFastForward did not resolve): off for the session
    refused_call_unwound = 9,    // the call never reported back (a script error unwound it): off for the session
    reason_count = 10
};
inline const char* reason_name(unsigned r) noexcept {
    static const char* const names[reason_count] = {"none",        "released",   "new_press", "cap",
                                                   "vm",          "story_sites", "no_toggle", "unreadable",
                                                   "call_unresolved", "call_unwound"};
    return r < reason_count ? names[r] : "?";
}
// A transition's row; formatted by the caller outside every lock.
struct Report {
    Event event = Event::none;
    unsigned reason = reason_none;
    bool rearm = false; // armed while already armed (a second key stop inside the window)
    std::uint32_t old_factor = 0, new_factor = 0, ip = 0, ret = 0, counter = 0;
    unsigned entries = 0, held = 0, zeros = 0, calls = 0, fires = 0;
    story::Fail fail = story::Fail::none;
    const char* where = "";
};

// Under the caller's lock. on_write runs at the factor hook (inside Input or inside the latch's own call), on_entry at
// the injection site's entry, on_result after the latch's call, on_vm at the VM clear/load sites.
struct Latch {
    bool enabled = false; // seta_manual_flight installed (mode >= 1) and seta_restore armed
    bool off = false;     // a runtime contract mismatch: off for the session (vanilla behaviour)
    bool armed = false, seen_held = false, calling = false, last = false;
    unsigned entries = 0, held = 0, zeros = 0, calls = 0, fires = 0;
    unsigned refused_logged = 0; // one refusal row per reason per session (bit per reason)
    std::uint64_t arms = 0, total_calls = 0, total_fires = 0, ramp_skips = 0, ends[reason_count]{};
    std::uint64_t first_step_frame = ~std::uint64_t(0); // the frame of the current SETA run's first ramp step
    bool ramp_guard = true;                              // the host model switches it off to show why it exists

    bool active() const noexcept { return armed; }

    // frame: the presented-frame count; inside_call: the write happens inside the latch's own call (the caller's routing
    // and the EH epoch both say so).
    Report on_write(const sr::Write& w, std::uint64_t frame, bool inside_call) noexcept {
        Report r;
        r.old_factor = w.old_factor;
        r.new_factor = w.new_factor;
        r.ip = w.ip;
        r.ret = w.ret.offset;
        if (!enabled || off) return r;
        if (w.readable && w.tag == sr::int_tag && w.old_factor == sr::factor_one && w.new_factor > sr::factor_one)
            first_step_frame = frame;
        if (calling && !inside_call) { // the latch's call unwound and this write is someone else's
            refuse_off(r, refused_call_unwound);
            return r;
        }
        if (calling) { // a write inside the latch's own StopFastForward: a re-delivered toggle had restarted SETA
            ++fires;
            ++total_fires;
            r.event = Event::fire;
            fill(r);
            return r;
        }
        if (!stop_shaped(w)) return r;
        const story::SetaSites* s = w.sites;
        if (!s || !s->outcome.ok()) {
            if (s) {
                r.fail = s->outcome.fail;
                r.where = s->outcome.where;
            }
            return refuse_once(r, refused_story_sites);
        }
        if (w.ip != s->stop_ip || w.ret.scan != sr::Scan::found) return r; // not StopFastForward from a script frame
        if (!s->named[toggle_named].entry) return refuse_once(r, refused_no_toggle);
        if (!key_stop(w)) return r; // a gate, sector warp, quick warp or jump stop
        if (ramp_guard && first_step_frame != ~std::uint64_t(0) && frame - first_step_frame <= ramp_guard_frames) {
            ++ramp_skips; // a start press's own re-delivery right after the first ramp step
            return r;
        }
        r.rearm = armed;
        armed = true;
        seen_held = calling = last = false;
        entries = held = zeros = calls = fires = 0;
        ++arms;
        r.event = Event::armed;
        return r;
    }
    // Returns true when StopFastForward is to be called now; *out carries a row when one is due.
    bool on_entry(bool readable, unsigned counter, Report& out) noexcept {
        out = Report{};
        out.counter = counter;
        if (!armed) return false;
        if (calling) { // the previous call never came back
            refuse_off(out, refused_call_unwound);
            return false;
        }
        if (!readable) {
            refuse_off(out, refused_unreadable);
            return false;
        }
        ++entries;
        if (counter) {
            // A rise after a 0 is a new press, except after a 0 on the arm's own entry only: a press that went down at
            // this iteration's poll shows in the counter one entry late. An arm whose key never shows in the counter (a
            // display click, a binding outside action 675) therefore treats the next press as new.
            if (zeros && (seen_held || entries > 2)) {
                end(out, released_new_press);
                return false;
            }
            seen_held = true;
            zeros = 0;
            ++held;
        } else {
            ++zeros;
        }
        if (entries > cap_entries) {
            end(out, released_cap);
            return false;
        }
        last = zeros >= tail_entries;
        calling = true;
        ++calls;
        ++total_calls;
        return true;
    }
    // The wrapper's AL after the latch's StopFastForward.
    Report on_result(unsigned al) noexcept {
        Report r;
        if (!calling) return r;
        calling = false;
        if (!(al & 0xff)) {
            refuse_off(r, refused_call_unresolved);
            return r;
        }
        if (last) end(r, released_tail);
        return r;
    }
    // The latch's call never reported back (the EH epoch moved, or the next entry found it outstanding).
    Report on_unwound() noexcept {
        Report r;
        if (calling) refuse_off(r, refused_call_unwound);
        return r;
    }
    Report on_vm() noexcept {
        Report r;
        if (armed) end(r, released_vm);
        return r;
    }

private:
    void fill(Report& r) const noexcept {
        r.entries = entries;
        r.held = held;
        r.zeros = zeros;
        r.calls = calls;
        r.fires = fires;
    }
    void end(Report& r, unsigned reason) noexcept {
        fill(r);
        r.event = Event::released;
        r.reason = reason;
        ++ends[reason < reason_count ? reason : 0];
        armed = seen_held = calling = last = false;
    }
    Report& refuse_once(Report& r, unsigned reason) noexcept {
        ++ends[reason];
        if (refused_logged & (1u << reason)) return r;
        refused_logged |= 1u << reason;
        r.event = Event::refused;
        r.reason = reason;
        return r;
    }
    void refuse_off(Report& r, unsigned reason) noexcept {
        fill(r);
        ++ends[reason];
        off = true;
        armed = seen_held = calling = last = false;
        refused_logged |= 1u << reason;
        r.event = Event::refused;
        r.reason = reason;
    }
};

// Debug tier: which injection-site entries get a seta_key row. Every entry whose counter shows the SETA key, the
// tail_entries entries after it reads 0, and at a press's rise the previous entry as well (the counter lags the press
// by one iteration, so the press's first ring read is in the entry before the rise). Per press at most
// key_held_rows_per_press rows while held (the tail is always written); key_row_cap rows per session, then one capped row.
struct KeyRows {
    enum class Emit : unsigned { none = 0, row = 1, pre_and_row = 2, capped = 3 };
    bool held_prev = false, last_logged = false, capped = false;
    unsigned tail = 0, press_rows = 0, rows = 0;
    std::uint32_t press = 0;

    Emit on_entry(unsigned counter) noexcept {
        Emit e = Emit::none;
        if (counter) {
            const bool rise = !held_prev;
            held_prev = true;
            tail = tail_entries;
            if (rise) {
                ++press;
                press_rows = 0;
            }
            if (press_rows < key_held_rows_per_press) {
                ++press_rows;
                e = rise && !last_logged ? Emit::pre_and_row : Emit::row;
            }
        } else {
            held_prev = false;
            if (tail) {
                --tail;
                e = Emit::row;
            }
        }
        if (e != Emit::none) {
            const unsigned need = e == Emit::pre_and_row ? 2u : 1u;
            if (rows + need > key_row_cap) {
                e = capped ? Emit::none : Emit::capped;
                capped = true;
            } else {
                rows += need;
            }
        }
        last_logged = e == Emit::row || e == Emit::pre_and_row;
        return e;
    }
};
}
