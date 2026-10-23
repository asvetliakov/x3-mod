// SETA restore core (src/proxy/seta_restore_core.h; docs/architecture/seta-restore.md) on controlled observations:
// the arm conditions at the TI_SetTimeWarpFactor case entry, every cancel, the one-arm-per-transit rule, the injection
// sequence (Ready walk once per presented frame, parent check, two frames, controlled object, the call and its result)
// the autopilot gate at the arm, the player-ship re-bind of a gate transit (ship-script identity, the deferred sector
// check), the record-only speed fields, and the task-stack return scan over a fake task. No Windows, no Wine; prints
// "checks PASS".
#include "../../src/proxy/seta_restore_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <vector>
using namespace x3m::seta_restore::core;
// The detections of the two objects on disk as the run-time detector reports them (story_detect_host over
// verification/probe/story_vm_image.py's images reproduces exactly these): Mayhem is the default observation,
// story_builds() covers the selection.
constexpr std::uint32_t story_build = x3m::story::mayhem_build, stop_ip = x3m::story::mayhem_seta_stop_ip,
                        gate_return = x3m::story::mayhem_seta_gate_return;
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
static const x3m::story::SetaSites mayhem_sites =
    detected(x3m::story::mayhem_build, 0x1558d, 0x15ef8, {{0x15ed2, 0x15f7b}, {0x15599, 0x1562a}, {0x15f7b, 0x16027}, {0x169fb, 0x16ad4}, {0xd5f6c, 0xd6770}});
static const x3m::story::SetaSites stock_sites =
    detected(x3m::story::stock_build, 0x157a6, 0x160d1, {{0x160ab, 0x16154}, {0x157b2, 0x15843}, {0x16154, 0x16200}, {0x16bd4, 0x16cad}, {0xbd7a3, 0xbdf50}});
static const x3m::story::SetaSites* const mayhem = &mayhem_sites;
static x3m::story::SetaSites failed(std::uint32_t build, x3m::story::Fail f, const char* where) {
    x3m::story::SetaSites s = detected(build, 0, 0, {});
    s.outcome = x3m::story::Outcome{f, where};
    return s;
}
static unsigned checks = 0;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", why);
        std::exit(1);
    }
}
constexpr std::uint32_t ship_a = 0x47e69ad0u, ship_b = 0x47e70000u, sector_a = 0x265e8e08u, sector_b = 0x80e89c18u,
                        id_a = 1201, id_b = 1305;
// Run 190's transit re-pointing: new objects, one ship-script id +0x94 (the chase rows' ship_id_from/to); the native
// id +8 (record only) is a placeholder value
constexpr std::uint32_t ship_c = 0x4628c8e8u, ship_d = 0x8785a078u, ship_e = 0x4628a590u, ship_id = 0x00000c41u,
                        script_id = 0xfffebb1eu, other_script = 0xfffeb000u;
constexpr std::uint32_t autopilot_flags = 0x80000u | 0x40000u | 0x2u, manual_flags = 0x40000u | 0x2u;
struct Kind {
    std::uint16_t cls, marker;
};
struct Probe {
    std::uint32_t player_ship = ship_a, parent_sector = sector_a, ready_node = 0;
    std::map<std::uint32_t, std::uint32_t> ids{{sector_a, id_a}, {sector_b, id_b}, {ship_a, ship_id}, {ship_c, ship_id},
                                               {ship_d, ship_id}, {ship_e, ship_id}};
    std::map<std::uint32_t, std::uint32_t> parents; // per object; otherwise parent_sector
    std::map<std::uint32_t, Kind> kinds{{ship_a, {7, 0xcafe}}, {ship_b, {7, 0xcafe}}, {ship_c, {7, 0xcafe}},
                                        {ship_d, {7, 0xcafe}}, {ship_e, {7, 0xcafe}}, {sector_a, {1, 0xcafe}},
                                        {sector_b, {1, 0xcafe}}};
    std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> speeds;
    std::map<std::uint32_t, std::uint32_t> ship_flags_of{{ship_a, autopilot_flags}, {ship_b, autopilot_flags},
                                                         {ship_c, autopilot_flags}, {ship_d, autopilot_flags},
                                                         {ship_e, autopilot_flags}};
    std::map<std::uint32_t, std::uint32_t> scripts{{ship_a, script_id}, {ship_b, script_id}, {ship_c, script_id},
                                                   {ship_d, script_id}, {ship_e, script_id}};
    bool player_ok = true, is_paused = false;
    unsigned walks = 0;
    bool player(std::uint32_t& s) {
        s = player_ship;
        return player_ok;
    }
    bool parent(std::uint32_t ship, std::uint32_t& s) {
        const auto it = parents.find(ship);
        s = it != parents.end() ? it->second : parent_sector;
        return s != 0;
    }
    bool object_id(std::uint32_t obj, std::uint32_t& id) {
        const auto it = ids.find(obj);
        if (it == ids.end()) return false;
        id = it->second;
        return true;
    }
    bool kind(std::uint32_t obj, std::uint16_t& cls, std::uint16_t& marker) {
        const auto it = kinds.find(obj);
        if (it == kinds.end()) return false;
        cls = it->second.cls;
        marker = it->second.marker;
        return true;
    }
    bool speed(std::uint32_t ship, std::uint32_t& cur, std::uint32_t& desired) {
        const auto it = speeds.find(ship);
        if (it == speeds.end()) return false;
        cur = it->second.first;
        desired = it->second.second;
        return true;
    }
    bool flags(std::uint32_t ship, std::uint32_t& f) {
        const auto it = ship_flags_of.find(ship);
        if (it == ship_flags_of.end()) return false;
        f = it->second;
        return true;
    }
    bool script(std::uint32_t ship, std::uint32_t& id) {
        const auto it = scripts.find(ship);
        if (it == scripts.end()) return false;
        id = it->second;
        return true;
    }
    bool sector_id(std::uint32_t sector, std::uint32_t& id) {
        const auto it = ids.find(sector);
        if (it == ids.end()) return false;
        id = it->second;
        return true;
    }
    bool paused() { return is_paused; }
    bool ready(std::uint32_t departure, std::uint32_t& node) {
        ++walks;
        if (!ready_node || ids[ready_node] == departure) return false;
        node = ready_node;
        return true;
    }
};
static Write gate_stop() {
    Write w;
    w.readable = true;
    w.old_factor = 0xa0000;
    w.new_factor = factor_one;
    w.tag = int_tag;
    w.ip = stop_ip;
    w.build = story_build;
    w.build_readable = true;
    w.sites = &mayhem_sites;
    w.ret = Return{Scan::found, gate_return};
    return w;
}
static Clock at(std::uint64_t frame, std::uint64_t ms, std::uint32_t epoch = 1) {
    Clock c;
    c.frame = frame;
    c.ms = ms;
    c.epoch = epoch;
    return c;
}
static void constants() {
    std::uint32_t entry = 0;
    std::memcpy(&entry, table_entry, 4);
    check(entry == hook_va && ti_table_entry_va == 0x004b161c && ti_case_index < ti_case_count, "jump-table entry");
    check(hook_next_va == hook_va + hook_length && case_rest_va == hook_next_va && hook_length >= 5, "hook span");
    check(hook_bytes[0] == 0xa1 && std::memcmp(hook_bytes + 1, "\x34\x6f\x60\x00", 4) == 0, "mov eax,[0x00606f34]");
    check(case_rest[case_rest_length - 3] == 0xc2 && case_rest[case_rest_length - 2] == 0x10, "case ends in ret 0x10");
    check(dispatch_head[dispatch_length - 7] == 0xff && dispatch_head[dispatch_length - 6] == 0x24, "jmp [eax*4+table]");
    check(wrapper_body[wrapper_length - 1] == 0xc3 && stop_call[stop_call_length - 1] == 0x18, "cdecl wrapper, add esp,0x18");
    check(script_call_va == 0x0049f4c0 && call_key == 0x96 && std::strcmp(call_method, "StartFastForward") == 0 &&
              call_argc == 0 && call_return_kind == 1,
          "the call");
    check(story_build == 1462141839u && stop_ip == 0x1558d && gate_return == 0x15ef8, "story constants");
    check(std::strcmp(return_name(mayhem, 0x155ad, false), "toggle") == 0 && std::strcmp(return_name(mayhem, 0, true), "engine_input") == 0 &&
              std::strcmp(return_name(mayhem, 0x12345, false), "other") == 0 && std::strcmp(return_name(nullptr, 0x15ef8, false), "other") == 0 &&
              std::strcmp(return_name(&stock_sites, 0x160d1, false), "gate") == 0 &&
              std::strcmp(return_name(&stock_sites, 0x15ef8, false), "other") == 0 &&
              std::strcmp(return_name(mayhem, 0xd6402, false), "jumpdrive") == 0,
          "return names");
    check(expiry_ms == 60000 && due_frames == 2, "timing");
    check(ship_flags == 0x40 && autopilot_bit == 0x80000 && ship_class == 7 && sector_class == 1 &&
              alive_marker == 0xcafe && object_class == 0x48 && object_marker == 0x9c && ship_speed == 0x10 &&
              ship_desired_speed == 0x14,
          "object fields");
    check(std::strcmp(ship_check_name(ShipCheck::second), "second") == 0 && std::strcmp(skip_name(skip_manual), "manual") == 0 &&
              std::strcmp(ship_check_name(ShipCheck::identity), "identity") == 0 && ship_script == 0x94,
          "row names");
}
static void arm_conditions() {
    Probe p;
    {
        State s;
        const Report r = s.on_write(gate_stop(), at(10, 1000), p);
        check(r.row == Row::armed && s.phase == Phase::armed && r.ret == gate_return && r.old_factor == 0xa0000, "arm");
        check(s.ship == ship_a && s.sector == sector_a && s.sector_id == id_a && s.active(), "arm records ship and sector");
        check(r.autopilot && r.flags == autopilot_flags && r.ship_id == ship_id, "arm records the autopilot flags");
        check(r.script_id == script_id && s.script_id == script_id, "arm records the ship-script id");
    }
    { // the ship-script id: required to arm (unreadable or 0 refuses), record only on a manual skip
        for (int zero = 0; zero < 2; ++zero) {
            State s;
            Probe q;
            if (zero)
                q.scripts[ship_a] = 0;
            else
                q.scripts.erase(ship_a);
            Report r = s.on_write(gate_stop(), at(1, 1), q);
            check(r.row == Row::refused && r.reason == refuse_unreadable && r.script_id == 0 && !s.active() &&
                      s.transit_id == 0,
                  "script id unreadable or 0: refused");
            q.ship_flags_of[ship_a] = manual_flags;
            r = s.on_write(gate_stop(), at(2, 2), q);
            check(r.row == Row::skipped && r.script_id == 0, "manual skip without a script id");
        }
    }
    { // a manual gate pass (CanWarp: SA_GetFlags & 0x80000 clear): one skipped row, nothing armed, the transit free
        State s;
        Probe q;
        q.ship_flags_of[ship_a] = manual_flags;
        Report r = s.on_write(gate_stop(), at(1, 1), q);
        check(r.row == Row::skipped && r.reason == skip_manual && !r.autopilot && r.flags == manual_flags &&
                  r.ship == ship_a && r.ship_id == ship_id && r.sector_id == id_a && !s.active() && s.arms == 0 &&
                  s.skips[skip_manual] == 1 && s.transit_id == 0,
              "manual: skipped");
        Report none;
        check(!s.on_inject(at(2, 2), ship_a, q, none) && none.row == Row::none, "manual: no ticket at the site");
        q.ship_flags_of[ship_a] = autopilot_flags; // the next stop from the same sector under autopilot arms
        check(s.on_write(gate_stop(), at(3, 3), q).row == Row::armed, "manual skip leaves the transit unrecorded");
        q.ship_flags_of.erase(ship_a);
        State t;
        r = t.on_write(gate_stop(), at(1, 1), q);
        check(r.row == Row::refused && r.reason == refuse_unreadable && !t.active(), "flags unreadable: refused");
        q.ship_flags_of[ship_a] = 0x80000u; // the bit alone decides
        check(t.on_write(gate_stop(), at(2, 2), q).row == Row::armed, "autopilot bit alone arms");
    }
    struct Case {
        const char* why;
        void (*edit)(Write&);
    };
    const Case none[] = {
        {"factor already 1.0", [](Write& w) { w.old_factor = factor_one; }},
        {"new factor not 1.0", [](Write& w) { w.new_factor = 0x20000; }},
        {"ramp step up", [](Write& w) { w.old_factor = factor_one, w.new_factor = 0x1e666; }},
        {"not an integer cell", [](Write& w) { w.tag = 2; }},
        {"unreadable", [](Write& w) { w.readable = false; }},
        {"other native site", [](Write& w) { w.ip = 0x14341; }},
        {"user toggle", [](Write& w) { w.ret.offset = 0x155ad; }},
        {"sector warp", [](Write& w) { w.ret.offset = 0x15fa1; }},
        {"jumpdrive", [](Write& w) { w.ret.offset = 0xd62e2; }},
        {"engine input stop", [](Write& w) { w.ret = Return{Scan::bottom, 0}; }},
        {"scan truncated", [](Write& w) { w.ret.scan = Scan::truncated; }},
        {"scan unreadable", [](Write& w) { w.ret.scan = Scan::unreadable; }},
    };
    for (const auto& c : none) {
        State s;
        Write w = gate_stop();
        c.edit(w);
        const Report r = s.on_write(w, at(1, 1), p);
        check(r.row == Row::none && s.phase == Phase::idle, c.why);
    }
    { // a failed detection: one refusal row per build and reason (naming the method), never arms; a detected object
      // still arms afterwards
        State s;
        const x3m::story::SetaSites intro = failed(1462141842u, x3m::story::Fail::no_method, "150::StopFastForward");
        Write w = gate_stop();
        w.build = intro.key.build;
        w.sites = &intro;
        Report r = s.on_write(w, at(1, 1), p);
        check(r.row == Row::refused && r.reason == refuse_story_sites && r.build == 1462141842u && !s.active() &&
                  r.detect_fail == x3m::story::Fail::no_method && std::strcmp(r.detect_where, "150::StopFastForward") == 0,
              "x3intro (no StopFastForward): refused, the row names the method and the reason");
        r = s.on_write(w, at(2, 2), p);
        check(r.row == Row::none && !s.active() && s.refusals[refuse_story_sites] == 1, "the same build and reason: no second row");
        const x3m::story::SetaSites ambiguous = failed(1234567891u, x3m::story::Fail::ambiguous_site, "150::StartGateWarp");
        Write other = w;
        other.build = ambiguous.key.build;
        other.sites = &ambiguous;
        r = s.on_write(other, at(3, 3), p);
        check(r.row == Row::refused && r.build == 1234567891u && s.refusals[refuse_story_sites] == 2 &&
                  r.detect_fail == x3m::story::Fail::ambiguous_site,
              "an unknown build whose signature matches twice: its own row");
        const x3m::story::SetaSites mismatch = failed(x3m::story::mayhem_build, x3m::story::Fail::table_mismatch, "mayhem");
        Write known = gate_stop();
        known.sites = &mismatch;
        check(s.on_write(known, at(4, 4), p).reason == refuse_story_sites && !s.active(),
              "a known build whose detection differs from story_builds.h: refused (the last-resort check)");
        Write user = w;
        user.ret.offset = 0x155ad; // any SETA stop shape is enough to bind the detection
        State t;
        check(t.on_write(user, at(1, 1), p).reason == refuse_story_sites, "the detection is checked on any SETA stop");
        check(s.on_write(gate_stop(), at(5, 5), p).row == Row::armed, "a detected object arms after a failed one");
    }
    { // an unreadable build id is not this write: one row, never latched, the next readable stop arms
        State s;
        Write w = gate_stop();
        w.build_readable = false;
        w.build = 0;
        Report r = s.on_write(w, at(1, 1), p);
        check(r.row == Row::refused && r.reason == refuse_build_unreadable && !s.refused_logged && !s.active(),
              "build unreadable: refused, not latched");
        r = s.on_write(w, at(2, 2), p);
        check(r.row == Row::none && !s.refused_logged && !s.active(), "build unreadable: the row once");
        check(s.on_write(gate_stop(), at(3, 3), p).row == Row::armed, "a later readable stop arms");
        Write foreign = gate_stop();
        foreign.build_readable = false;
        foreign.build = 1510658513u; // a value read without the flag is not trusted either
        State t;
        check(t.on_write(foreign, at(1, 1), p).reason == refuse_build_unreadable && !t.refused_logged, "flag decides");
        Write none = gate_stop();
        none.sites = nullptr;
        State u;
        check(u.on_write(none, at(1, 1), p).reason == refuse_build_unreadable, "no detection: unreadable, not armed");
    }
    { // the player ship or its sector unreadable at the arm
        State s;
        Probe q;
        q.player_ok = false;
        check(s.on_write(gate_stop(), at(1, 1), q).reason == refuse_unreadable && !s.active(), "no player: refused");
        q.player_ok = true;
        q.parent_sector = 0;
        check(s.on_write(gate_stop(), at(1, 1), q).reason == refuse_unreadable, "no sector: refused");
        q.parent_sector = 0x1234;
        check(s.on_write(gate_stop(), at(1, 1), q).reason == refuse_unreadable, "no sector id: refused");
    }
}
// The per-build selection: every entry arms on its own stop IP and gate return, the other entry's offsets are another stop
// under it, the armed row carries the entry (its return names), and the table is complete.
// Selection by the detection of the loaded object: each object's own detected stop arms with its sites (the ticket keeps
// them for the rows), another object's offsets are another stop.
static void story_builds() {
    for (const x3m::story::SetaSites* d : {&mayhem_sites, &stock_sites}) {
        Probe q;
        Write w = gate_stop();
        w.build = d->key.build;
        w.sites = d;
        w.ip = d->stop_ip;
        w.ret.offset = d->gate_return;
        State s;
        Report r = s.on_write(w, at(1, 1), q);
        check(r.row == Row::armed && s.sites.stop_ip == d->stop_ip && std::strcmp(r.ret_name, "gate") == 0,
              "the object's own detected stop arms; the row names the gate");
        const x3m::story::SetaSites* o = d == &mayhem_sites ? &stock_sites : &mayhem_sites;
        Write x = w;
        x.ip = o->stop_ip;
        x.ret.offset = o->gate_return;
        State t;
        check(t.on_write(x, at(1, 1), q).row == Row::none && !t.active(), "another object's offsets: another stop, no arm");
        Write cancel = w;
        cancel.sites = nullptr; // a write while armed reads no detection: named from the ticket's copy
        cancel.ret.offset = d == &mayhem_sites ? 0x155ad : 0x157c6;
        r = s.on_write(cancel, at(2, 2), q);
        check(r.row == Row::cancelled && std::strcmp(r.ret_name, "toggle") == 0, "a cancel names its return from the ticket's sites");
    }
    check(x3m::story::build_count == 2 && x3m::story::builds[0].seta_stop_ip == mayhem_sites.stop_ip &&
              x3m::story::builds[1].seta_gate_return == stock_sites.gate_return,
          "the known builds' expectations equal the detections");
}
static void cancels() {
    { // any later factor write cancels, in armed and in due; the user's own ramp write too
        for (int due = 0; due < 2; ++due) {
            State s;
            Probe p;
            s.on_write(gate_stop(), at(10, 1000), p);
            if (due) {
                p.ready_node = sector_b;
                p.parent_sector = sector_b;
                Report r;
                s.on_inject(at(11, 1010), ship_a, p, r);
                check(s.phase == Phase::due, "due before the write");
            }
            Write ramp;
            ramp.readable = true;
            ramp.old_factor = factor_one;
            ramp.new_factor = 0x1e666;
            ramp.tag = int_tag;
            ramp.ip = 0x14341;
            ramp.build = story_build;
            ramp.ret = Return{Scan::found, 0x14898};
            const Report r = s.on_write(ramp, at(12, 1020), p);
            check(r.row == Row::cancelled && r.reason == cancel_factor_write && r.new_factor == 0x1e666 &&
                      r.frames_since_arm == 2 && r.ms_since_arm == 20 && !s.active(),
                  "factor write cancels");
        }
    }
    { // one arm per transit: the same departure sector cannot arm again until a VM clear/load
        State s;
        Probe p;
        s.on_write(gate_stop(), at(1, 1), p);
        Write other = gate_stop();
        other.ret.offset = 0x155ad;
        s.on_write(other, at(2, 2), p); // cancels
        Report r = s.on_write(gate_stop(), at(3, 3), p);
        check(r.row == Row::refused && r.reason == refuse_same_transit && !s.active(), "same transit refused");
        r = s.on_vm(1, at(4, 4));
        check(r.row == Row::none, "VM clear while idle: no row");
        check(s.on_write(gate_stop(), at(5, 5), p).row == Row::armed, "after the VM clear the sector arms again");
        p.player_ship = ship_a;
        p.ids[sector_b] = id_b;
        p.parent_sector = sector_b; // a later transit from another sector arms
        s.on_write(other, at(6, 6), p);
        check(s.on_write(gate_stop(), at(7, 7), p).row == Row::armed && s.sector_id == id_b, "the next transit arms");
    }
    { // VM clear / load, epoch, expiry (wall and frames), ship change, unreadable player, sector left
        State s;
        Probe p;
        s.on_write(gate_stop(), at(1, 1), p);
        Report r = s.on_vm(2, at(2, 2));
        check(r.row == Row::cancelled && r.reason == cancel_vm_load && !s.active(), "VM load cancels");
        s.on_vm(1, at(2, 2));
        s.on_write(gate_stop(), at(3, 3), p);
        r = s.on_vm(1, at(4, 4));
        check(r.reason == cancel_vm_clear, "VM clear cancels");
        s.on_write(gate_stop(), at(5, 5, 1), p);
        check(!s.on_inject(at(6, 6, 2), ship_a, p, r) && r.reason == cancel_epoch && !s.active(), "EH epoch cancels");
        s.on_vm(1, at(7, 7));
        s.on_write(gate_stop(), at(10, 100), p);
        check(!s.on_inject(at(11, 100 + expiry_ms), ship_a, p, r) && r.row == Row::none, "inside the wall bound");
        check(!s.on_inject(at(12, 101 + expiry_ms), ship_a, p, r) && r.reason == cancel_expiry, "wall expiry");
        s.on_vm(1, at(13, 13));
        s.on_write(gate_stop(), at(20, 200), p);
        // wall time only: any number of presented frames inside the 60 s (e.g. 7200 at 240 fps in 30 s) never expires
        check(!s.on_inject(at(20 + 7200, 200 + 30000), ship_a, p, r) && r.row == Row::none && s.active(),
              "frames alone never expire");
        check(!s.on_inject(at(20 + 7300, 201 + expiry_ms), ship_a, p, r) && r.reason == cancel_expiry, "wall expiry only");
        s.on_vm(1, at(1, 1));
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_b; // a change inside the departure sector is no transit: cancelled as before
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change && r.ship == ship_a &&
                  r.new_ship == ship_b && r.ship_check == ShipCheck::same_sector,
              "ship change: ship= the ticket's, new_ship= the new one");
        s.on_vm(1, at(1, 1));
        p.player_ship = ship_a;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ok = false;
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change &&
                  r.ship_check == ShipCheck::unreadable,
              "player unreadable");
        p.player_ok = true;
        s.on_vm(1, at(1, 1));
        s.on_write(gate_stop(), at(1, 1), p);
        p.ready_node = p.parent_sector = sector_b;
        s.on_inject(at(2, 2), ship_a, p, r);
        p.parent_sector = sector_a; // left again before the call
        check(!s.on_inject(at(4, 4), ship_a, p, r) && r.reason == cancel_sector_left && !s.active(), "sector left");
    }
}
static void inject_sequence() {
    State s;
    Probe p;
    Report r;
    check(!s.on_inject(at(1, 1), ship_a, p, r) && r.row == Row::none && p.walks == 0, "idle: no work");
    s.on_write(gate_stop(), at(100, 5000), p);
    // warp-in scene: the departure sector is the newest; one walk per presented frame
    check(!s.on_inject(at(100, 5001), ship_a, p, r) && p.walks == 1, "first walk");
    check(!s.on_inject(at(100, 5002), ship_a, p, r) && p.walks == 1, "no second walk in the same frame");
    p.is_paused = true;
    check(!s.on_inject(at(101, 5010), ship_a, p, r) && p.walks == 1 && s.phase == Phase::armed, "paused: deferred");
    p.is_paused = false;
    // the destination exists, the ship not yet in it
    p.ready_node = sector_b;
    check(!s.on_inject(at(102, 9000), ship_a, p, r) && p.walks == 2 && s.phase == Phase::armed, "ship not yet parented");
    p.parent_sector = sector_b;
    check(!s.on_inject(at(103, 9010), ship_a, p, r) && s.phase == Phase::due && s.ready_frame == 103, "Ready");
    check(!s.on_inject(at(104, 9020), ship_a, p, r) && s.phase == Phase::due, "one frame after Ready: wait");
    check(!s.on_inject(at(105, 9030), ship_b, p, r) && s.phase == Phase::due && s.skipped == 1, "another controlled object");
    p.is_paused = true;
    check(!s.on_inject(at(105, 9031), ship_a, p, r) && s.phase == Phase::due, "paused while due: deferred");
    p.is_paused = false;
    check(s.on_inject(at(105, 9032), ship_a, p, r) && s.phase == Phase::calling && r.row == Row::none, "the call");
    // a factor write during the call (none expected from StartFastForward) neither cancels nor arms
    check(s.on_write(gate_stop(), at(105, 9033), p).row == Row::none && s.phase == Phase::calling, "write during the call");
    r = s.on_result(1, at(105, 9034));
    check(r.row == Row::restored && r.result == 1 && r.frames_after_transit == 2 && r.frames_since_arm == 5 &&
              r.ms_since_arm == 4034 && !s.active() && s.restores == 1,
          "restored");
    check(s.on_result(1, at(106, 9035)).row == Row::none, "a second result is ignored");
    // AL 0 (the method did not resolve) still ends the ticket with its result byte
    State t;
    Probe q;
    q.ready_node = q.parent_sector = sector_b;
    q.parent_sector = sector_a;
    t.on_write(gate_stop(), at(1, 1), q);
    q.parent_sector = sector_b;
    t.on_inject(at(2, 2), ship_a, q, r);
    check(t.on_inject(at(4, 4), ship_a, q, r), "second ticket called");
    r = t.on_result(0x100, at(4, 5));
    check(r.row == Row::restore_failed && r.result == 0 && t.restores == 0 && t.restore_failures == 1 && !t.active(),
          "AL 0: restore_failed, ticket ended");
    // a call that never reports back is cancelled at the next entry; a VM clear during the call too
    t.on_vm(1, at(5, 5));
    q.parent_sector = sector_a;
    t.on_write(gate_stop(), at(6, 6), q);
    q.parent_sector = sector_b;
    t.on_inject(at(7, 7), ship_a, q, r);
    check(t.on_inject(at(9, 9), ship_a, q, r), "third ticket called");
    check(!t.on_inject(at(10, 10), ship_a, q, r) && r.reason == cancel_call_unwound && !t.active(), "unwound call");
    t.on_vm(1, at(11, 11));
    q.parent_sector = sector_a;
    t.on_write(gate_stop(), at(12, 12), q);
    q.parent_sector = sector_b;
    t.on_inject(at(13, 13), ship_a, q, r);
    check(t.on_inject(at(15, 15), ship_a, q, r), "fourth ticket called");
    r = t.on_vm(2, at(15, 16));
    check(r.reason == cancel_vm_load && t.on_result(1, at(15, 17)).row == Row::none, "load during the call");
}
// The gate transit re-points *(M+0x38) once (Run 190): one change to a live ship in a sector other than the departure
// sector re-binds the ticket, which then restores on the new ship; every other change cancels ship_change.
static void rebind() {
    { // Run 190's shape: arm on ship_a in sector_a, the new object ship_c in sector_b at the first entry after the stall
        State s;
        Probe p;
        Report r;
        check(s.on_write(gate_stop(), at(19177, 1000), p).row == Row::armed && s.ship_id == ship_id, "arm records id");
        check(!s.on_inject(at(19178, 1010), ship_a, p, r) && r.row == Row::none, "before the stall");
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        check(!s.on_inject(at(19362, 9000), ship_a, p, r) && r.row == Row::rebound && s.active() &&
                  s.phase == Phase::armed && s.ship == ship_c && s.rebound && s.rebinds == 1,
              "one change re-binds");
        check(r.ship == ship_a && r.new_ship == ship_c && r.ship_id == ship_id && r.new_ship_id == ship_id &&
                  r.sector_id == id_a && r.new_sector == sector_b && r.new_sector_id == id_b &&
                  r.frames_since_arm == 185 && r.ms_since_arm == 8000 && r.ship_check == ShipCheck::rebound,
              "rebound row: old/new ship and ids, sector ids, frames since arm");
        check(r.script_id == script_id && r.new_script_id == script_id && r.new_flags_readable &&
                  r.new_flags == autopilot_flags && s.pendings == 0,
              "rebound row: both script ids, the new ship's flags");
        p.ready_node = sector_b;
        check(!s.on_inject(at(19363, 9010), ship_c, p, r) && s.phase == Phase::due && s.ready_sector == sector_b,
              "Ready on the re-bound ship");
        check(!s.on_inject(at(19365, 9030), ship_a, p, r) && s.skipped == 1, "the old object is no longer the controlled one");
        check(s.on_inject(at(19365, 9031), ship_c, p, r), "the call on the re-bound ship");
        r = s.on_result(1, at(19365, 9032));
        check(r.row == Row::restored && r.ship == ship_c && s.restores == 1 && !s.active(), "restored after a re-bind");
    }
    { // a second change cancels, in armed and in due
        for (int due = 0; due < 2; ++due) {
            State s;
            Probe p;
            Report r;
            s.on_write(gate_stop(), at(1, 1), p);
            p.player_ship = ship_c;
            p.parents[ship_c] = sector_b;
            s.on_inject(at(2, 2), ship_a, p, r);
            check(r.row == Row::rebound, "first change");
            if (due) {
                p.ready_node = sector_b;
                s.on_inject(at(3, 3), ship_c, p, r);
                check(s.phase == Phase::due, "due before the second change");
            }
            p.player_ship = ship_d;
            p.parents[ship_d] = sector_b;
            check(!s.on_inject(at(4, 4), ship_c, p, r) && r.row == Row::cancelled && r.reason == cancel_ship_change &&
                      r.ship_check == ShipCheck::second && r.ship == ship_c && r.new_ship == ship_d && !s.active() &&
                      s.rebinds == 1,
                  "second change cancels");
        }
    }
    { // a change whose sector is still the departure sector cancels (no transit)
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_a;
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change &&
                  r.ship_check == ShipCheck::same_sector && r.new_sector_id == id_a && !s.active(),
              "same-sector change cancels");
    }
    struct Case {
        const char* why;
        ShipCheck expect;
        void (*edit)(Probe&);
    };
    const Case unreadable[] = {
        {"null player", ShipCheck::unreadable, [](Probe& p) { p.player_ship = 0; }},
        {"misaligned player", ShipCheck::unreadable, [](Probe& p) { p.player_ship = ship_c + 2; }},
        {"player unreadable", ShipCheck::unreadable, [](Probe& p) { p.player_ok = false; }},
        {"object unreadable", ShipCheck::unreadable, [](Probe& p) { p.kinds.erase(ship_c); }},
        {"not a ship", ShipCheck::not_ship, [](Probe& p) { p.kinds[ship_c] = Kind{10, 0xcafe}; }},
        {"dead ship", ShipCheck::not_ship, [](Probe& p) { p.kinds[ship_c] = Kind{7, 0xefac}; }},
        {"script id unreadable", ShipCheck::unreadable, [](Probe& p) { p.scripts.erase(ship_c); }},
        {"script id differs", ShipCheck::identity, [](Probe& p) { p.scripts[ship_c] = other_script; }},
        {"script id 0", ShipCheck::identity, [](Probe& p) { p.scripts[ship_c] = 0; }},
        {"other ship, no sector yet", ShipCheck::identity, [](Probe& p) {
             p.scripts[ship_c] = other_script; // identity is checked before the sector: no deferral for it
             p.parents[ship_c] = 0;
         }},
    };
    for (const auto& c : unreadable) {
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        c.edit(p);
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.row == Row::cancelled && r.reason == cancel_ship_change &&
                  r.ship_check == c.expect && !s.active() && s.rebinds == 0 && r.ship == ship_a && r.new_ship == p.player_ship,
              c.why);
    }
    { // identity mismatch: the cancel row names both script ids, the ticket's ship and the new one
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        p.scripts[ship_c] = other_script;
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change &&
                  r.ship_check == ShipCheck::identity && r.ship == ship_a && r.new_ship == ship_c &&
                  r.script_id == script_id && r.new_script_id == other_script && r.new_sector == 0 &&
                  s.cancels[cancel_ship_change] == 1 && !s.active(),
              "identity mismatch cancels before any sector read");
    }
    // The new ship's sector not live yet: no cancel, the re-bind not consumed, one rebind_pending row, polled again.
    const Case deferred[] = {
        {"no parent", ShipCheck::no_sector, [](Probe& p) { p.parents[ship_c] = 0; }},
        {"parent not a sector", ShipCheck::no_sector, [](Probe& p) { p.kinds[sector_b] = Kind{7, 0xcafe}; }},
        {"parent dead", ShipCheck::no_sector, [](Probe& p) { p.kinds[sector_b] = Kind{1, 0xefac}; }},
        {"parent unreadable", ShipCheck::no_sector, [](Probe& p) { p.kinds.erase(sector_b); }},
        {"sector id unreadable", ShipCheck::no_sector, [](Probe& p) { p.ids.erase(sector_b); }},
        {"sector id 0", ShipCheck::no_sector, [](Probe& p) { p.ids[sector_b] = 0; }},
        {"parent misaligned", ShipCheck::no_sector, [](Probe& p) { p.parents[ship_c] = sector_b + 2; }},
    };
    for (const auto& c : deferred) { // pending, then success once the parent reads as a live sector
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1000), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        Probe live = p;
        c.edit(p);
        check(!s.on_inject(at(2, 1010), ship_a, p, r) && r.row == Row::rebind_pending && r.ship_check == c.expect &&
                  r.ship == ship_a && r.new_ship == ship_c && r.new_script_id == script_id && s.active() &&
                  s.phase == Phase::armed && s.ship == ship_a && !s.rebound && s.rebinds == 0 && s.pendings == 1,
              c.why);
        check(!s.on_inject(at(3, 1020), ship_a, p, r) && r.row == Row::none && s.active() && s.pendings == 1 &&
                  s.cancels[cancel_ship_change] == 0,
              "pending: one row, polled again without a cancel");
        p = live;
        check(!s.on_inject(at(4, 1030), ship_a, p, r) && r.row == Row::rebound && s.ship == ship_c && s.rebound &&
                  s.rebinds == 1 && r.new_sector == sector_b && r.new_sector_id == id_b && r.frames_since_arm == 3,
              "pending, then the sector is live: re-bound");
        p.ready_node = sector_b;
        check(!s.on_inject(at(5, 1040), ship_c, p, r) && s.phase == Phase::due, "Ready after a pending re-bind");
        check(s.on_inject(at(7, 1060), ship_c, p, r) && s.on_result(1, at(7, 1061)).row == Row::restored,
              "restored after a pending re-bind");
    }
    { // pending in due, then success: the same deferral while the ticket is due
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.ready_node = p.parent_sector = sector_b;
        s.on_inject(at(2, 2), ship_a, p, r);
        check(s.phase == Phase::due, "due before the change");
        p.player_ship = ship_c;
        p.parents[ship_c] = 0;
        check(!s.on_inject(at(3, 3), ship_a, p, r) && r.row == Row::rebind_pending && s.phase == Phase::due,
              "pending while due");
        p.parents[ship_c] = sector_b;
        check(!s.on_inject(at(4, 4), ship_a, p, r) && r.row == Row::rebound && s.phase == Phase::due, "re-bound while due");
    }
    { // pending, then the ticket expires (60 s wall time from the arm)
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1000), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = 0;
        check(!s.on_inject(at(2, 1010), ship_a, p, r) && r.row == Row::rebind_pending, "pending");
        check(!s.on_inject(at(500, 1000 + expiry_ms), ship_a, p, r) && r.row == Row::none && s.active() &&
                  s.pendings == 1,
              "still pending at the bound");
        check(!s.on_inject(at(501, 1001 + expiry_ms), ship_a, p, r) && r.row == Row::cancelled &&
                  r.reason == cancel_expiry && !s.active() && s.rebinds == 0 && s.cancels[cancel_ship_change] == 0,
              "pending, then expiry");
    }
    { // pending, then an identity mismatch: the same pointer's script id changes, or another object replaces it
        for (int replaced = 0; replaced < 2; ++replaced) {
            State s;
            Probe p;
            Report r;
            s.on_write(gate_stop(), at(1, 1), p);
            p.player_ship = ship_c;
            p.parents[ship_c] = 0;
            check(!s.on_inject(at(2, 2), ship_a, p, r) && r.row == Row::rebind_pending, "pending");
            if (replaced) {
                p.player_ship = ship_e;
                p.scripts[ship_e] = other_script;
                p.parents[ship_e] = sector_b;
            } else {
                p.scripts[ship_c] = other_script;
                p.parents[ship_c] = sector_b;
            }
            check(!s.on_inject(at(3, 3), ship_a, p, r) && r.row == Row::cancelled && r.reason == cancel_ship_change &&
                      r.ship_check == ShipCheck::identity && r.ship == ship_a && r.new_ship == p.player_ship &&
                      r.new_script_id == other_script && !s.active() && s.rebinds == 0,
                  "pending, then identity mismatch");
        }
    }
    { // the pending row once per ticket; the next arm resets it
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = 0;
        s.on_inject(at(2, 2), ship_a, p, r);
        s.on_vm(1, at(3, 3));
        p.player_ship = ship_a;
        s.on_write(gate_stop(), at(4, 4), p);
        p.player_ship = ship_c;
        check(!s.on_inject(at(5, 5), ship_a, p, r) && r.row == Row::rebind_pending && s.pendings == 2,
              "the next ticket logs its own pending row");
    }
    { // the new ship's flags unreadable: still re-bound, new_flags_read=0
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        p.ship_flags_of.erase(ship_c);
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.row == Row::rebound && !r.new_flags_readable && r.new_flags == 0,
              "new flags record only");
    }
    { // the next arm clears the re-bind allowance: one re-bind per ticket, not per session
        State s;
        Probe p;
        Report r;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ship = ship_c;
        p.parents[ship_c] = sector_b;
        s.on_inject(at(2, 2), ship_a, p, r);
        Write ramp;
        s.on_write(ramp, at(3, 3), p); // any write while armed cancels (factor_write)
        check(!s.active() && s.rebound, "cancelled; the flag stays until the next arm");
        p.parent_sector = sector_b; // the next transit departs from sector_b on ship_c
        p.parents.clear();
        check(s.on_write(gate_stop(), at(4, 4), p).row == Row::armed && !s.rebound && s.ship == ship_c, "re-armed");
        p.player_ship = ship_d;
        p.parents[ship_d] = sector_a;
        check(!s.on_inject(at(5, 5), ship_c, p, r) && r.row == Row::rebound && s.ship == ship_d && s.rebinds == 2,
              "re-bound again on the next ticket");
    }
}
// Record-only speed fields: the current player ship's [+0x10]/[+0x14] in every transition row, none in refusals.
static void speeds() {
    Probe p;
    p.speeds[ship_a] = {120, 150};
    p.speeds[ship_c] = {0, 0};
    State s;
    Report r = s.on_write(gate_stop(), at(1, 1), p);
    record_speed(r, p);
    check(r.row == Row::armed && r.speed_readable && r.speed_ship == ship_a && r.speed == 120 && r.desired_speed == 150,
          "armed speeds");
    p.player_ship = ship_c;
    p.parents[ship_c] = sector_b;
    s.on_inject(at(2, 2), ship_a, p, r);
    record_speed(r, p);
    check(r.row == Row::rebound && r.speed_readable && r.speed_ship == ship_c && r.speed == 0, "rebound speeds");
    p.speeds.erase(ship_c);
    r = s.on_vm(2, at(3, 3));
    record_speed(r, p);
    check(r.row == Row::cancelled && !r.speed_readable && r.speed_ship == ship_c && r.speed == 0, "unreadable speeds");
    p.speeds[ship_c] = {0xffffff9cu, 0}; // int32 -100: carried raw
    p.ready_node = sector_b;
    p.parents[ship_c] = sector_a;
    s.on_write(gate_stop(), at(5, 5), p);
    p.parents[ship_c] = sector_b;
    s.on_inject(at(6, 6), ship_c, p, r);
    check(s.on_inject(at(8, 8), ship_c, p, r), "call");
    r = s.on_result(1, at(8, 9));
    record_speed(r, p);
    check(r.row == Row::restored && r.speed_readable && std::int32_t(r.speed) == -100, "restored speeds");
    p.ship_flags_of[ship_c] = manual_flags;
    p.parents[ship_c] = sector_b;
    State m;
    r = m.on_write(gate_stop(), at(9, 9), p);
    record_speed(r, p);
    check(r.row == Row::skipped && r.speed_readable && r.speed_ship == ship_c, "skipped speeds");
    Report none;
    record_speed(none, p);
    check(!none.speed_readable && !none.speed_ship, "no row: no read");
    const x3m::story::SetaSites none_found = failed(1, x3m::story::Fail::no_class, "150::StopFastForward");
    Write foreign = gate_stop();
    foreign.build = 1;
    foreign.sites = &none_found;
    State t;
    r = t.on_write(foreign, at(1, 1), p);
    record_speed(r, p);
    check(r.row == Row::refused && !r.speed_readable && !r.speed_ship, "refusals carry no speed");
}
// Fake task memory for the return scan.
struct Memory {
    std::map<std::uint32_t, unsigned char> bytes;
    unsigned reads = 0;
    void put32(std::uint32_t at, std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(v >> (8 * i));
    }
    void cell(std::uint32_t at, unsigned char tag, std::uint32_t v) {
        bytes[at] = tag;
        put32(at + 1, v);
    }
    bool read(std::uintptr_t at, void* out, unsigned n) {
        ++reads;
        auto* o = static_cast<unsigned char*>(out);
        for (unsigned i = 0; i < n; ++i) {
            const auto it = bytes.find(std::uint32_t(at + i));
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
};
constexpr std::uint32_t task = 0x10000, base = 0x20000, ctx_live = 0x30000, ctx_dead = 0x30100, desc = 0x40000;
// Cells listed from the top (index -n) towards the base.
static Memory task_with(std::initializer_list<std::pair<unsigned char, std::uint32_t>> cells, std::int32_t capacity = 64) {
    Memory m;
    const auto n = std::int32_t(cells.size());
    m.put32(task + task_stack_base, base);
    m.put32(task + task_capacity, std::uint32_t(capacity));
    m.put32(task + task_stack_index, std::uint32_t(-n));
    std::uint32_t at = base - 5 * std::uint32_t(n);
    for (const auto& c : cells) {
        m.cell(at, c.first, c.second);
        at += 5;
    }
    m.put32(ctx_live + 8, desc);
    m.put32(ctx_dead + 8, 0);
    return m;
}
static void return_scans() {
    auto scan = [](Memory& m, std::uint32_t t = task) {
        return first_return(t, [&](std::uintptr_t at, void* out, unsigned n) { return m.read(at, out, n); });
    };
    { // StopFastForward's frame: the native argument, a local, then the record of StartGateWarp's call
        Memory m = task_with({{1, 0x10000}, {1, 0}, {10, ctx_live}, {3, gate_return}, {10, 0}, {3, 0}});
        const Return r = scan(m);
        check(r.scan == Scan::found && r.offset == gate_return, "first record");
    }
    { // a null context and a dead context are skipped as the engine skips them
        Memory m = task_with({{1, 1}, {10, 0}, {3, 0x155ad}, {10, ctx_dead}, {3, 0x16a07}, {10, ctx_live}, {3, gate_return}});
        const Return r = scan(m);
        check(r.scan == Scan::found && r.offset == gate_return, "skips null and dead contexts");
    }
    { // the task bottom (return offset 0) first: the engine-originated task of the input routine
        Memory m = task_with({{1, 1}, {10, 0}, {3, 0}});
        check(scan(m).scan == Scan::bottom, "bottom record");
    }
    { // a tag-3 cell above a tag-10 cell is not a record
        Memory m = task_with({{3, gate_return}, {10, ctx_live}});
        check(scan(m).scan == Scan::bottom && scan(m).offset == 0, "order matters");
    }
    { // more used cells than the bounded scan reads, none of them a record
        Memory m;
        const std::int32_t n = std::int32_t(scan_cells) + 5;
        m.put32(task + task_stack_base, base);
        m.put32(task + task_capacity, 1024);
        m.put32(task + task_stack_index, std::uint32_t(-n));
        for (std::int32_t i = 0; i < n; ++i) m.cell(base - 5 * std::uint32_t(n - i), 1, std::uint32_t(i));
        check(scan(m).scan == Scan::truncated, "truncated");
    }
    { // implausible task fields and unreadable memory
        Memory m = task_with({{10, ctx_live}, {3, gate_return}});
        check(scan(m, 0).scan == Scan::unreadable && scan(m, task + 2).scan == Scan::unreadable, "bad task pointer");
        m.put32(task + task_stack_index, 1);
        check(scan(m).scan == Scan::unreadable, "positive index");
        m.put32(task + task_stack_index, std::uint32_t(-1000));
        check(scan(m).scan == Scan::unreadable, "index beyond the capacity");
        Memory gone = task_with({{10, 0x30200}, {3, gate_return}});
        check(scan(gone).scan == Scan::unreadable, "unreadable context");
        Memory empty;
        check(scan(empty).scan == Scan::unreadable, "unreadable task");
    }
}
// The installer's comparison over a flat image (VA image_base + i at byte i): reader, bounds, first mismatch.
constexpr std::uint32_t image_base = 0x00400000u;
struct Image {
    std::vector<unsigned char> bytes;
    bool read(std::uint32_t at, void* out, unsigned n) const {
        if (at < image_base || at - image_base > bytes.size() || n > bytes.size() - (at - image_base)) return false;
        std::memcpy(out, bytes.data() + (at - image_base), n);
        return true;
    }
};
static bool compare(const Image& image, Mismatch& m) {
    return compare_windows([&](std::uint32_t at, void* out, unsigned n) { return image.read(at, out, n); }, m);
}
static Image synthetic() {
    Image image;
    image.bytes.assign(0x00200000, 0xcc);
    for (const Window& w : windows) std::memcpy(image.bytes.data() + (w.va - image_base), w.bytes, w.length);
    return image;
}
static void window_compare() {
    check(window_count == 10 && windows[0].va == hook_va && windows[window_count - 1].va == stop_call_va, "window table");
    unsigned longest = 0, beyond_prologue = 0;
    for (const Window& w : windows) {
        longest = w.length > longest ? w.length : longest;
        beyond_prologue += w.length < 5 || w.length > 16; // engine_patch::verify_bytes' bound, which refused Run 189
    }
    check(longest == wrapper_length && longest == window_max && beyond_prologue == 8, "lengths 4..173, 8 beyond 5..16");
    // every length in use, pinned against the compare routine's buffer (a new or longer window must update both)
    constexpr unsigned lengths[window_count] = {8, 34, 33, 36, 4, 27, 15, 43, 173, 23};
    for (unsigned i = 0; i < window_count; ++i)
        check(windows[i].length == lengths[i] && windows[i].length <= window_max, windows[i].name);
    Mismatch m;
    for (unsigned i = 0; i < window_count; ++i) { // the last byte of each window counts, whatever its length
        Image flipped = synthetic();
        flipped.bytes[windows[i].va + windows[i].length - 1 - image_base] ^= 0xff;
        check(!compare(flipped, m) && m.window == &windows[i] && m.readable, windows[i].name);
    }
    Image image = synthetic();
    check(compare(image, m) && !m.window, "every window matches the unpatched image");
    image.bytes[case_rest_va + 20 - image_base] ^= 0xff; // a byte past the first 16 still counts
    check(!compare(image, m) && m.window == &windows[1] && m.readable && m.shown == 8 &&
              !std::memcmp(m.got, case_rest, 8),
          "case_rest byte 20 differs: named, got = the window's first 8 bytes");
    image = synthetic();
    image.bytes[ti_table_entry_va - image_base] = 0x90; // the 4-byte window compares too
    check(!compare(image, m) && m.window == &windows[4] && m.shown == 4 && m.got[0] == 0x90, "table entry differs");
    image = synthetic();
    image.bytes[script_call_va + wrapper_length - 1 - image_base] ^= 1; // the wrapper's last byte
    check(!compare(image, m) && m.window == &windows[8], "wrapper tail differs");
    image = synthetic();
    image.bytes[hook_va - image_base] = 0xe9; // a foreign jmp over the hook and the stop call: the hook named first
    image.bytes[stop_call_va - image_base] = 0xe9;
    check(!compare(image, m) && m.window == &windows[0] && m.got[0] == 0xe9, "first mismatch in table order");
    image = synthetic();
    image.bytes.resize(hook_va + 4 - image_base); // the hook window runs past the readable image
    check(!compare(image, m) && m.window == &windows[0] && !m.readable && m.shown == 8, "unreadable window reported");
}
// --image <flat file>: the comparison over a live-image model (test_seta_restore builds it from X3AP.exe with the earlier
// features' claim bytes applied). One line: windows=N mismatch=none | mismatch=<name> va=0x... readable=0|1 got=hex.
static int run_image(const char* path) {
    Image image;
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    unsigned char buffer[65536];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
        image.bytes.insert(image.bytes.end(), buffer, buffer + n);
    std::fclose(f);
    Mismatch m;
    if (compare(image, m)) {
        std::printf("image windows=%u mismatch=none\n", window_count);
        return 0;
    }
    std::printf("image windows=%u mismatch=%s va=0x%08x readable=%u got=", window_count, m.window->name,
                unsigned(m.window->va), unsigned(m.readable));
    for (unsigned i = 0; i < m.shown; ++i) std::printf("%02x", m.got[i]);
    std::printf("\n");
    return 1;
}
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--image")) return run_image(argv[2]);
    if (argc == 2 && !std::strcmp(argv[1], "--windows")) { // the table, one "name va length" line each
        for (const Window& w : windows) std::printf("%s 0x%08x %u\n", w.name, unsigned(w.va), w.length);
        return 0;
    }
    constants();
    arm_conditions();
    story_builds();
    cancels();
    inject_sequence();
    rebind();
    speeds();
    return_scans();
    window_compare();
    std::printf("seta_restore_host checks=%u\nchecks PASS\n", checks);
    return 0;
}
