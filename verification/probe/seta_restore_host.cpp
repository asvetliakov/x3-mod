// SETA restore core (src/proxy/seta_restore_core.h; docs/architecture/seta-restore.md) on controlled observations:
// the arm conditions at the TI_SetTimeWarpFactor case entry, every cancel, the one-arm-per-transit rule, the injection
// sequence (Ready walk once per presented frame, parent check, two frames, controlled object, the call and its result)
// and the task-stack return scan over a fake task. No Windows, no Wine; prints "checks PASS".
#include "../../src/proxy/seta_restore_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
using namespace x3m::seta_restore::core;
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
struct Probe {
    std::uint32_t player_ship = ship_a, parent_sector = sector_a, ready_node = 0;
    std::map<std::uint32_t, std::uint32_t> ids{{sector_a, id_a}, {sector_b, id_b}};
    bool player_ok = true, is_paused = false;
    unsigned walks = 0;
    bool player(std::uint32_t& s) {
        s = player_ship;
        return player_ok;
    }
    bool parent(std::uint32_t, std::uint32_t& s) {
        s = parent_sector;
        return parent_sector != 0;
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
    check(std::strcmp(return_name(0x155ad, false), "toggle") == 0 && std::strcmp(return_name(0, true), "engine_input") == 0 &&
              std::strcmp(return_name(0x12345, false), "other") == 0,
          "return names");
    check(expiry_ms == 60000 && due_frames == 2, "timing");
}
static void arm_conditions() {
    Probe p;
    {
        State s;
        const Report r = s.on_write(gate_stop(), at(10, 1000), p);
        check(r.row == Row::armed && s.phase == Phase::armed && r.ret == gate_return && r.old_factor == 0xa0000, "arm");
        check(s.ship == ship_a && s.sector == sector_a && s.sector_id == id_a && s.active(), "arm records ship and sector");
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
    { // another story build: one refusal row, then off for good (no row, no arm)
        State s;
        Write w = gate_stop();
        w.build = 1510658513u;
        Report r = s.on_write(w, at(1, 1), p);
        check(r.row == Row::refused && r.reason == refuse_story_build && s.disabled && !s.active(), "foreign build refused");
        r = s.on_write(gate_stop(), at(2, 2), p);
        check(r.row == Row::none && !s.active() && s.refusals[refuse_story_build] == 1, "refused once, stays off");
        Write user = gate_stop();
        user.build = 1510658513u;
        user.ret.offset = 0x155ad; // any SETA stop shape is enough to bind the build
        State t;
        check(t.on_write(user, at(1, 1), p).reason == refuse_story_build, "the build is checked on any SETA stop");
    }
    { // an unreadable build id is not this write: one row, never latched, the next readable stop arms
        State s;
        Write w = gate_stop();
        w.build_readable = false;
        w.build = 0;
        Report r = s.on_write(w, at(1, 1), p);
        check(r.row == Row::refused && r.reason == refuse_build_unreadable && !s.disabled && !s.active(),
              "build unreadable: refused, not latched");
        r = s.on_write(w, at(2, 2), p);
        check(r.row == Row::none && !s.disabled && !s.active(), "build unreadable: the row once");
        check(s.on_write(gate_stop(), at(3, 3), p).row == Row::armed, "a later readable stop arms");
        Write foreign = gate_stop();
        foreign.build_readable = false;
        foreign.build = 1510658513u; // a value read without the flag is not trusted either
        State t;
        check(t.on_write(foreign, at(1, 1), p).reason == refuse_build_unreadable && !t.disabled, "flag decides");
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
        p.player_ship = ship_b;
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change && r.ship == ship_b, "ship change");
        s.on_vm(1, at(1, 1));
        p.player_ship = ship_a;
        s.on_write(gate_stop(), at(1, 1), p);
        p.player_ok = false;
        check(!s.on_inject(at(2, 2), ship_a, p, r) && r.reason == cancel_ship_change, "player unreadable");
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
int main() {
    constants();
    arm_conditions();
    cancels();
    inject_sequence();
    return_scans();
    std::printf("seta_restore_host checks=%u\nchecks PASS\n", checks);
    return 0;
}
