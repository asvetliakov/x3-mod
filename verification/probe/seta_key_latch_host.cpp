// SETA key stop latch core (src/proxy/seta_key_latch_core.h; docs/architecture/seta-manual-flight.md "Key stop latch") on
// controlled observations: the key-stop classification against both detected story builds, arm / hold / tail release,
// new press (also after an arm the counter never showed), cap, VM, the fire row, the unwound call, every refusal, the
// ramp guard, the debug row selection, and the iteration model of docs/reverse-engineering/seta-manual-flight.md
// section 7 (seta_key_toggle_study.py's ring with the script's filter, start / stop / 250 ms ramp, the counter's
// one-iteration lag) at 222, 60 and 12 fps, isolated presses and a stop press followed by a start press inside and
// outside the tail, run with and without the latch. No Windows, no Wine; prints "checks PASS".
#include "../../src/proxy/seta_key_latch_core.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <vector>
using namespace x3m::seta_key_latch::core;
namespace sr = x3m::seta_restore::core;

static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", why);
        std::exit(1);
    }
}
static x3m::story::SetaSites detected(std::uint32_t build, std::uint32_t stop, std::uint32_t gate,
                                      std::initializer_list<x3m::story::Range> named) {
    x3m::story::SetaSites s;
    s.detected = true;
    s.key = x3m::story::VmKey{0x10000000u, 0x20000000u, 0x1d2000u, build};
    s.stop_ip = stop;
    s.gate_return = gate;
    unsigned i = 0;
    for (const x3m::story::Range& r : named) s.named[i++] = r;
    return s;
}
// The detections seta_restore_host.cpp uses (story_detect_host over the objects on disk reproduces them).
static const x3m::story::SetaSites mayhem =
    detected(x3m::story::mayhem_build, 0x1558d, 0x15ef8, {{0x15ed2, 0x15f7b}, {0x15599, 0x1562a}, {0x15f7b, 0x16027}, {0x169fb, 0x16ad4}, {0xd5f6c, 0xd6770}});
static const x3m::story::SetaSites stock =
    detected(x3m::story::stock_build, 0x157a6, 0x160d1, {{0x160ab, 0x16154}, {0x157b2, 0x15843}, {0x16154, 0x16200}, {0x16bd4, 0x16cad}, {0xbd7a3, 0xbdf50}});
constexpr std::uint32_t one = sr::factor_one, ramped = 0xa0000u, key_ret = 0x155ad, ramp_ip = 0x14341;
constexpr std::uint64_t far_frame = 100000; // a frame far from any first ramp step

static sr::Write write(std::uint32_t old_factor, std::uint32_t new_factor, std::uint32_t ip, std::uint32_t ret,
                       sr::Scan scan = sr::Scan::found, const x3m::story::SetaSites* sites = &mayhem) {
    sr::Write w;
    w.readable = true;
    w.tag = sr::int_tag;
    w.old_factor = old_factor;
    w.new_factor = new_factor;
    w.ip = ip;
    w.ret.scan = scan;
    w.ret.offset = ret;
    w.sites = sites;
    w.build_readable = sites != nullptr;
    return w;
}
static sr::Write key_write() {
    return write(ramped, one, mayhem.stop_ip, key_ret);
}
static Report put(Latch& l, const sr::Write& w, bool inside = false, std::uint64_t frame = far_frame) {
    return l.on_write(w, frame, inside);
}
static Latch enabled() {
    Latch l;
    l.enabled = true;
    return l;
}
// One injection entry with the call reported back as AL; returns whether the latch called.
static bool entry(Latch& l, unsigned counter, Report& row, unsigned al = 1) {
    const bool call = l.on_entry(true, counter, row);
    if (call) {
        const Report r = l.on_result(al);
        if (r.event != Event::none) row = r;
    }
    return call;
}

static void constants() {
    check(seta_counter_va == 0x0057ff0b && steer_counters_va == 0x0057fe74, "counter addresses");
    check(ring_span == 37 && ring_slot_count == 4, "ring span covers slot 3's DIK byte");
    check(std::string_view(x3m::story::seta_named[toggle_named]) == "toggle", "toggle range index");
    check(std::string_view(call_method) == "StopFastForward", "method name");
    check(tail_entries == 8 && cap_entries == 2048 && ramp_guard_frames == 4, "bounds");
}
static void classification() {
    check(key_stop(key_write()), "Mayhem toggle stop");
    check(key_stop(write(ramped, one, stock.stop_ip, 0x157c6, sr::Scan::found, &stock)), "stock toggle stop");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, mayhem.gate_return)), "gate stop is not a key stop");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, 0x15f90)), "sector warp stop");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, 0, sr::Scan::bottom)), "engine input stop (bottom record)");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, key_ret, sr::Scan::truncated)), "truncated scan");
    check(!key_stop(write(one, one, mayhem.stop_ip, key_ret)), "a stop at factor 1.0 (start then stop in one press)");
    check(!key_stop(write(ramped, 0x20000, mayhem.stop_ip, key_ret)), "new factor not 1.0");
    check(!key_stop(write(one, 0x1e666, ramp_ip, 0x14000)), "ramp step");
    check(!key_stop(write(ramped, one, 0x1558e, key_ret)), "another IP");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, 0x15599)), "range entry itself is no return offset");
    sr::Write w = key_write();
    w.tag = 3;
    check(!key_stop(w), "not an int cell");
    w = key_write();
    w.readable = false;
    check(!key_stop(w), "unreadable");
    check(!key_stop(write(ramped, one, mayhem.stop_ip, key_ret, sr::Scan::found, nullptr)), "no sites");
}
static void hold_and_release() {
    Latch l = enabled();
    Report r = put(l, key_write());
    check(r.event == Event::armed && !r.rearm && l.active(), "armed on the key stop");
    unsigned calls = 0;
    // the arm's own iteration: counter 0 (the lag); then held 3; then 8 zero entries, the 8th releases
    for (unsigned c : {0u, 1u, 1u, 1u}) {
        calls += entry(l, c, r);
        check(r.event == Event::none, "no row while held");
    }
    for (unsigned i = 1; i <= tail_entries; ++i) {
        calls += entry(l, 0, r);
        check((i < tail_entries) == (r.event == Event::none), "tail entries silent until the last");
    }
    check(r.event == Event::released && r.reason == released_tail && !l.active(), "released after the tail");
    check(calls == 12 && r.calls == 12 && r.held == 3 && r.zeros == tail_entries, "one call per entry, held and zeros");
    check(!l.on_entry(true, 1, r) && r.event == Event::none, "idle after release");
    // two keys bound to SETA: counter 2 is held as well
    put(l, key_write());
    check(entry(l, 2, r) && l.seen_held, "counter 2 held");
}
static void fire_and_rearm() {
    Latch l = enabled();
    put(l, key_write());
    Report r;
    check(l.on_entry(true, 1, r), "call handed out");
    r = put(l, write(one, one, mayhem.stop_ip, 0, sr::Scan::bottom), true); // StopFastForward inside the latch's call
    check(r.event == Event::fire && r.fires == 1 && l.total_fires == 1, "a write inside the call is a fire");
    r = l.on_result(1);
    check(r.event == Event::none && l.active(), "still armed after the fire");
    r = put(l, key_write());
    check(r.event == Event::armed && r.rearm && l.entries == 0, "a second key stop re-arms");
    // a ramp step or a gate stop while armed neither arms nor releases
    check(put(l, write(one, 0x1e666, ramp_ip, 0x14000)).event == Event::none, "ramp write ignored");
    check(put(l, write(ramped, one, mayhem.stop_ip, mayhem.gate_return)).event == Event::none && l.active(),
          "gate stop ignored");
}
static void new_press_cap_vm() {
    Latch l = enabled();
    put(l, key_write());
    Report r;
    entry(l, 0, r);
    entry(l, 1, r); // the lag: the rise after the arm's own 0 is the same press
    check(r.event == Event::none && l.seen_held, "rise after the arm's own 0 is the same press");
    entry(l, 1, r);
    entry(l, 0, r);
    entry(l, 0, r);
    check(!entry(l, 1, r) && r.event == Event::released && r.reason == released_new_press && !l.active(),
          "a rise after the fall is a new press: released without a call");
    // an arm the counter never shows (a display click): a press two or more entries later is new
    put(l, key_write());
    entry(l, 0, r);
    entry(l, 0, r);
    entry(l, 0, r);
    check(!entry(l, 1, r) && r.event == Event::released && r.reason == released_new_press, "click arm, then a press");
    put(l, key_write());
    entry(l, 0, r);
    entry(l, 0, r);
    check(!entry(l, 1, r) && r.reason == released_new_press, "click arm, press at the third entry");
    l.on_write(key_write(), far_frame, false);
    unsigned calls = 0;
    for (unsigned i = 0; i < cap_entries; ++i) calls += entry(l, 1, r);
    check(calls == cap_entries && l.active(), "held up to the cap");
    check(!entry(l, 1, r) && r.event == Event::released && r.reason == released_cap, "cap");
    put(l, key_write());
    r = l.on_vm();
    check(r.event == Event::released && r.reason == released_vm && !l.active(), "VM clear/load releases");
    check(l.on_vm().event == Event::none, "VM while idle: no row");
}
static void ramp_guard() {
    Latch l = enabled();
    put(l, write(one, 0x1e666, ramp_ip, 0x14000), false, 500); // a run's first ramp step at frame 500
    for (std::uint64_t f = 500; f <= 500 + ramp_guard_frames; ++f)
        check(put(l, write(0x1e666, one, mayhem.stop_ip, key_ret), false, f).event == Event::none && !l.active(),
              "a toggle stop within the guard after the first step: not armed");
    check(l.ramp_skips == ramp_guard_frames + 1, "ramp skips counted");
    check(put(l, write(0x1e666, one, mayhem.stop_ip, key_ret), false, 501 + ramp_guard_frames).event == Event::armed,
          "past the guard: armed");
    Latch g = enabled();
    g.ramp_guard = false;
    put(g, write(one, 0x1e666, ramp_ip, 0x14000), false, 500);
    check(put(g, write(0x1e666, one, mayhem.stop_ip, key_ret), false, 500).event == Event::armed, "guard off: armed");
    Latch s = enabled(); // later steps (old > 1.0) do not move the first-step frame
    put(s, write(one, 0x1e666, ramp_ip, 0x14000), false, 500);
    put(s, write(0x1e666, 0x2cccc, ramp_ip, 0x14000), false, 600);
    check(put(s, write(0x2cccc, one, mayhem.stop_ip, key_ret), false, 601).event == Event::armed, "steps after the first");
}
static void unwound_and_refusals() {
    Latch l;
    check(put(l, key_write()).event == Event::none && !l.active(), "disabled: nothing");
    l = enabled();
    x3m::story::SetaSites failed = mayhem;
    failed.outcome = x3m::story::Outcome{x3m::story::Fail::no_site, "150::StopFastForward"};
    Report r = put(l, write(ramped, one, mayhem.stop_ip, key_ret, sr::Scan::found, &failed));
    check(r.event == Event::refused && r.reason == refused_story_sites && r.fail == x3m::story::Fail::no_site, "story sites");
    check(put(l, write(ramped, one, mayhem.stop_ip, key_ret, sr::Scan::found, nullptr)).event == Event::none,
          "story refusal once per session");
    check(!l.off && put(l, key_write()).event == Event::armed, "a story refusal does not switch the latch off");
    l = enabled();
    x3m::story::SetaSites no_toggle = mayhem;
    no_toggle.named[toggle_named] = x3m::story::Range{};
    r = put(l, write(ramped, one, mayhem.stop_ip, key_ret, sr::Scan::found, &no_toggle));
    check(r.event == Event::refused && r.reason == refused_no_toggle && !l.active(), "no toggle range");
    check(put(l, write(ramped, one, mayhem.stop_ip, mayhem.gate_return, sr::Scan::found, &no_toggle)).event == Event::none,
          "no_toggle once");
    l = enabled();
    put(l, key_write());
    check(!l.on_entry(false, 0, r) && r.event == Event::refused && r.reason == refused_unreadable && l.off, "unreadable counter");
    check(put(l, key_write()).event == Event::none && !l.active(), "off for the session");
    l = enabled();
    put(l, key_write());
    check(entry(l, 1, r, 0) && r.event == Event::refused && r.reason == refused_call_unresolved && l.off, "AL 0: off");
    // an unwound call found at the next entry
    l = enabled();
    put(l, key_write());
    check(l.on_entry(true, 1, r), "call handed out (never reported)");
    check(!l.on_entry(true, 1, r) && r.event == Event::refused && r.reason == refused_call_unwound && l.off, "unwound: off");
    check(l.on_result(1).event == Event::none, "a late result after the refusal: nothing");
    // an unwound call seen by the next write (the EH epoch moved): no fire is counted for a stranger's write
    l = enabled();
    put(l, key_write());
    check(l.on_entry(true, 1, r), "call handed out");
    r = put(l, write(one, one, mayhem.stop_ip, 0, sr::Scan::bottom), false);
    check(r.event == Event::refused && r.reason == refused_call_unwound && l.total_fires == 0 && !l.calling && l.off,
          "a write outside the call while it is outstanding: unwound, not a fire");
    // ... or by the caller at the next poll
    l = enabled();
    put(l, key_write());
    check(l.on_entry(true, 1, r), "call handed out");
    r = l.on_unwound();
    check(r.event == Event::refused && r.reason == refused_call_unwound && !l.calling && l.off, "on_unwound");
    check(l.on_unwound().event == Event::none, "on_unwound idle");
}
static void key_rows() {
    KeyRows k;
    using E = KeyRows::Emit;
    check(k.on_entry(0) == E::none, "idle");
    check(k.on_entry(1) == E::pre_and_row && k.press == 1, "rise: the previous entry and this one");
    check(k.on_entry(1) == E::row, "held");
    unsigned tail = 0;
    for (unsigned i = 0; i < 12; ++i) tail += k.on_entry(0) == E::row;
    check(tail == tail_entries, "eight tail rows");
    check(k.on_entry(1) == E::pre_and_row && k.press == 2, "second press");
    for (unsigned i = 0; i < 3; ++i) k.on_entry(0);
    check(k.on_entry(1) == E::row && k.press == 3, "a rise inside the tail: the previous entry is already written");
    unsigned held = 1;
    for (unsigned i = 0; i < 200; ++i) held += k.on_entry(1) == E::row;
    check(held == key_held_rows_per_press, "held rows per press capped");
    tail = 0;
    for (unsigned i = 0; i < 10; ++i) tail += k.on_entry(0) == E::row;
    check(tail == tail_entries, "the tail after a capped press is written");
    KeyRows s;
    unsigned rows = 0, capped = 0;
    for (unsigned p = 0; p < 200; ++p) {
        for (unsigned i = 0; i < 30; ++i) {
            const E e = s.on_entry(1);
            rows += e == E::row ? 1 : e == E::pre_and_row ? 2 : 0;
            capped += e == E::capped;
        }
        for (unsigned i = 0; i < 20; ++i) {
            const E e = s.on_entry(0);
            rows += e == E::row;
            capped += e == E::capped;
        }
    }
    check(rows <= key_row_cap && rows + 2 > key_row_cap && capped == 1, "session cap, one capped row");
}

// ---- the iteration model (section 7.1): poll, one ring read, Input (Vbi ramp, the filter, the toggle), the 0x00410100
// entry (the latch), the counter update. One presented frame per iteration. ----
constexpr unsigned J = 0x24, L = 0xcb, U = 0xc8;
struct Press {
    unsigned start, held; // first iteration whose poll sees the key, iterations held
};
struct Trace {
    std::vector<unsigned char> call, on;   // per iteration: the latch called; m21 after the iteration
    std::vector<unsigned> ramp_steps;      // iterations with a ramp step
    unsigned first_stop = ~0u;             // the first key stop with old > 1.0
    unsigned toggles = 0, arms = 0, fires = 0;
    std::uint32_t factor = 0;
};
static Trace iterate(bool with_latch, bool guard, bool start_on, const std::vector<Press>& presses, unsigned steer,
                     unsigned ri, unsigned wi, unsigned frame_us) {
    Latch l = enabled();
    l.enabled = with_latch;
    l.ramp_guard = guard;
    unsigned char slot[4]{};
    unsigned prev = ~0u, counter = 0;
    bool on = start_on;
    std::uint32_t factor = start_on ? ramped : one;
    std::uint64_t now = 0, last_step = 0;
    const unsigned frames = presses.back().start + presses.back().held + 40;
    Trace t;
    t.call.assign(frames, 0);
    t.on.assign(frames, 0);
    for (unsigned f = 0; f < frames; ++f, now += frame_us) {
        bool j = false;
        for (const Press& p : presses) j = j || (f >= p.start && f < p.start + p.held);
        unsigned held[3], n = 0;
        if (j) held[n++] = J; // ascending DIK order: J (0x24) before the arrows (0xc8, 0xcb)
        if (steer == 2) held[n++] = U;
        if (steer >= 1) held[n++] = L;
        for (unsigned a = 0; a < n; ++a) {
            if (++wi >= 3) wi = 0;
            slot[wi] = static_cast<unsigned char>(held[a]);
        }
        unsigned key = 0;
        if (ri != wi) {
            const unsigned old = ri;
            ri = old + 1;
            if (old >= 3) ri = 0;
            key = slot[ri];
        }
        // Input: Vbi first (a step more than 250 ms after the start or the previous step), then the filter and toggle
        if (on && now - last_step > 250000) {
            put(l, write(factor, factor + 0xe666, ramp_ip, 0x14000), false, f);
            factor += 0xe666;
            last_step = now;
            t.ramp_steps.push_back(f);
        }
        if (key == J && prev != J) {
            ++t.toggles;
            if (on) {
                const Report r = put(l, write(factor, one, mayhem.stop_ip, key_ret), false, f);
                t.arms += r.event == Event::armed;
                if (factor > one && t.first_stop == ~0u) t.first_stop = f;
                on = false;
                factor = one;
            } else {
                on = true;
                last_step = now;
            }
        }
        prev = key;
        // the 0x00410100 entry: the counter still holds the previous iteration's count
        Report r;
        if (l.on_entry(true, counter, r)) {
            t.call[f] = 1;
            if (on) { // StopFastForward with SETA running: its write reaches the hook inside the call
                t.fires += put(l, write(factor, one, mayhem.stop_ip, 0, sr::Scan::bottom), true, f).event == Event::fire;
                on = false;
                factor = one;
            }
            l.on_result(1);
        }
        counter = j ? 1 : 0;
        t.on[f] = on;
    }
    t.factor = factor;
    return t;
}
static bool ramp_between(const Trace& t, unsigned from, unsigned to) {
    for (unsigned s : t.ramp_steps)
        if (s > from && s < to) return true;
    return false;
}
static bool any_call(const Trace& t, unsigned from) {
    for (unsigned f = from; f < t.call.size(); ++f)
        if (t.call[f]) return true;
    return false;
}
constexpr unsigned rates[] = {4505u, 16667u, 83333u}; // 222 fps (measured median), 60 fps, 12 fps
static void model_isolated() {
    unsigned off = 0, restarted = 0, fired = 0, on_cases = 0, on_latched = 0, lost = 0, on_latched_unguarded = 0;
    for (unsigned frame_us : rates)
        for (unsigned steer = 0; steer < 3; ++steer)
            for (unsigned held = 2; held <= 60; ++held)
                for (unsigned ri = 0; ri < 4; ++ri)
                    for (unsigned wi = 0; wi < 3; ++wi) {
                        const std::vector<Press> p{{3, held}};
                        const Trace v = iterate(false, true, true, p, steer, ri, wi, frame_us);
                        const Trace k = iterate(true, true, true, p, steer, ri, wi, frame_us);
                        if (v.toggles) {
                            // the press stopped SETA: with the latch it stays off, no ramp after the stop
                            check(k.arms >= 1 && !k.on.back() && k.factor == one && !ramp_between(k, k.first_stop, ~0u),
                                  "OFF press stays off");
                            ++off;
                            fired += k.fires != 0;
                            restarted += v.on.back() || ramp_between(v, v.first_stop, ~0u);
                        } else {
                            ++lost; // the ring overwrote or skipped every J read (two arrows; some short presses)
                            check(!k.arms && k.on.back() && v.on.back(), "a lost press is vanilla");
                        }
                        // a press made to start SETA from idle
                        const Trace von = iterate(false, true, false, p, steer, ri, wi, frame_us);
                        const Trace kon = iterate(true, true, false, p, steer, ri, wi, frame_us);
                        const Trace uon = iterate(true, false, false, p, steer, ri, wi, frame_us);
                        ++on_cases;
                        on_latched += kon.arms != 0;
                        on_latched_unguarded += uon.arms != 0;
                        check(!kon.arms && kon.on == von.on && kon.toggles == von.toggles, "ON press from idle untouched");
                    }
    check(restarted > 0 && fired > 0, "the model reproduces the restart without the latch, and the latch fires");
    check(off > 0 && on_latched == 0, "OFF presses latched, ON presses not");
    std::printf("model off_presses=%u restarted_without_latch=%u latch_fired=%u on_presses=%u on_presses_latched=%u "
                "on_presses_latched_without_ramp_guard=%u lost=%u\n",
                off, restarted, fired, on_cases, on_latched, on_latched_unguarded, lost);
}
// A stop press from steady SETA, then a start press `gap` iterations after its release: outside the tail the latch has
// released before the start press's first poll and makes no call during it; inside, it calls at most once (the start
// press's first entry, before the counter shows it), then releases on the rise.
static void model_back_to_back() {
    unsigned outside = 0, inside = 0, inside_called = 0, inside_ended_off = 0, outside_ended_on = 0;
    for (unsigned frame_us : rates)
        for (unsigned steer = 0; steer < 2; ++steer)
            for (unsigned h1 : {4u, 10u, 22u})
                for (unsigned h2 : {4u, 10u, 22u})
                    for (unsigned gap = 1; gap <= 14; ++gap)
                        for (unsigned ri = 0; ri < 4; ++ri)
                            for (unsigned wi = 0; wi < 3; ++wi) {
                                const unsigned q = 3 + h1 + gap;
                                const std::vector<Press> p{{3, h1}, {q, h2}};
                                const Trace k = iterate(true, true, true, p, steer, ri, wi, frame_us);
                                if (!k.arms || k.on[q - 1]) continue; // press 1 lost or left SETA on: not this case
                                const Trace v = iterate(false, true, true, p, steer, ri, wi, frame_us);
                                unsigned calls = 0;
                                for (unsigned f = q; f < k.call.size(); ++f) calls += k.call[f];
                                if (gap > tail_entries) {
                                    ++outside;
                                    check(!any_call(k, q), "outside the tail: no call during the start press");
                                    outside_ended_on += k.on.back();
                                    // from the same state at q (SETA off) the start press runs as without the latch
                                    if (!v.on[q - 1]) check(k.on.back() == v.on.back(), "outside the tail: as without latch");
                                } else {
                                    ++inside;
                                    check(calls <= 1, "inside the tail: at most the first entry's call");
                                    inside_called += calls != 0;
                                    inside_ended_off += !k.on.back();
                                }
                            }
    check(outside > 0 && inside > 0, "both windows covered");
    std::printf("back_to_back outside_tail=%u outside_ended_on=%u inside_tail=%u inside_called=%u inside_ended_off=%u\n",
                outside, outside_ended_on, inside, inside_called, inside_ended_off);
}
int main() {
    constants();
    classification();
    hold_and_release();
    fire_and_rearm();
    new_press_cap_vm();
    ramp_guard();
    unwound_and_refusals();
    key_rows();
    model_isolated();
    model_back_to_back();
    std::printf("seta_key_latch_host checks=%u\nchecks PASS\n", checks);
    return 0;
}
