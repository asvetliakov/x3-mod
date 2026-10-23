"""Host checks of the combat-music trigger (src/proxy/music_alert*.{h,cpp}; docs/architecture/music-combat-trigger.md).

The site verifier on the installed executable and its refusals on patched copies (every window, the site, an interior
branch, an extra caller, the identity); the core header's windows against the verifier's table; the rule compiled from
the core header on the host against a fake memory (each reason a/b/c, the hold expiry in game ms, the newer-than-now
rule, the vanilla precondition, the baseline rule of the counters, the bounded walk, the relation test, the option
parsers); the stub's shape; the production wiring; the schema entries and the launcher options (--dry-run only,
never a launch). No Wine.
"""
import contextlib
import importlib.util
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('verify_music_alert_sites', ROOT / 'verification/results/music-script-driver/verify_music_alert_sites.py')
CORE = ROOT / 'src/proxy/music_alert_core.h'
MODULE = ROOT / 'src/proxy/music_alert.cpp'
TEXT_VA, TEXT_RAW = 0x401000, 0x400

HARNESS = r'''
#include "music_alert_core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace x3m::music_alert::core;
struct Span { std::uint32_t at; unsigned n; };
struct FakeMem {
    std::map<std::uint32_t, unsigned char> bytes;
    unsigned writes = 0, image_loads = 0;
    bool logging = false;
    std::vector<Span> log; // every validated read while logging
    bool read(std::uint32_t at, void* out, unsigned n) {
        if (logging) log.push_back(Span{at, n});
        unsigned char* o = static_cast<unsigned char*>(out);
        for (unsigned i = 0; i < n; ++i) {
            auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    std::uint32_t image32(std::uint32_t at) { // the plain load of an image global (0 where the fake maps nothing)
        ++image_loads;
        unsigned char b[4];
        for (unsigned i = 0; i < 4; ++i) { auto it = bytes.find(at + i); if (it == bytes.end()) return 0; b[i] = it->second; }
        return le32(b);
    }
    void write32(std::uint32_t at, std::uint32_t v) { ++writes; put32(at, v); }
    void fill(std::uint32_t at, unsigned n) { for (unsigned i = 0; i < n; ++i) bytes[at + i] = 0; }
    void start() { logging = true; log.clear(); }
    unsigned count(std::uint32_t at, unsigned n) const { unsigned k = 0; for (const Span& s : log) k += s.at == at && s.n == n; return k; }
    void put32(std::uint32_t at, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) bytes[at + i] = (v >> (8 * i)) & 0xff; }
    void put16(std::uint32_t at, std::uint16_t v) { for (unsigned i = 0; i < 2; ++i) bytes[at + i] = (v >> (8 * i)) & 0xff; }
    std::uint32_t get32(std::uint32_t at) { std::uint32_t v = 0; read(at, &v, 4); return v; }
};
// World: R 0x10000 (table 0x10100, buckets 0x10200, link 0x10300, cockpit 0x10400); cfg 0x20000; M 0x30000; P 0x40000
// (flight 0x40400, render 0x40800); sector 0x50000 whose list array [sector+0x50] is 0x51000 (the 32 12-byte headers;
// of the sector object only +0x48..+0x54 is mapped, so its own bytes at +0x5c.. can never be read as lists),
// terminators 0x5f000+c*0x100 (only the link mapped); every object maps exactly its header span, flight-block span and
// position span; H 0x60000 (hostile, group 3),
// O 0x61000 (owned, group 10), F 0x62000 (friend, group 4), missile X 0x63000 in list 10.
constexpr std::uint32_t R = 0x10000, CFG = 0x20000, M = 0x30000, P = 0x40000, PF = 0x40400, PR = 0x40800, SECTOR = 0x50000, LISTS = 0x51000,
                        H = 0x60000, HF = 0x60400, HR = 0x60800, O = 0x61000, OF = 0x61400, OR_ = 0x61800, F = 0x62000, FF = 0x62400,
                        FR = 0x62800, X = 0x63000, XD = 0x63400, XR = 0x63800, COCKPIT = 0x10400;
std::uint32_t term(unsigned c) { return 0x5f000 + c * 0x100; }
void object(FakeMem& m, std::uint32_t o, std::uint32_t next, std::uint16_t cls, std::uint16_t group, std::uint32_t data, std::uint32_t render,
            std::int32_t x, std::int32_t y, std::int32_t z) {
    m.fill(o, header_span); m.fill(data + flight_begin, flight_span); m.fill(render + render_position, 12);
    m.put32(o + obj_next, next); m.put16(o + obj_group, group); m.put32(o + obj_flags1, 0); m.put32(o + obj_flags2, 0);
    m.put16(o + obj_class, cls); m.put32(o + obj_data, data); m.put32(o + obj_parent, SECTOR); m.put32(o + obj_render, render);
    m.put32(render + render_position, std::uint32_t(x)); m.put32(render + render_position + 4, std::uint32_t(y));
    m.put32(render + render_position + 8, std::uint32_t(z));
    m.put32(data + flight_friend_mask, 0); m.put32(data + flight_foe_mask, 0); m.put32(data + flight_enemy_of_enabled, 0);
    m.put16(data + flight_order, 0); m.put32(data + flight_target, 0);
}
FakeMem world() {
    FakeMem m;
    m.put32(registry_va, R); m.put32(R, 0x10100); m.put32(R + registry_committed, 0); m.put32(R + registry_level, 0); m.put32(R + registry_cockpit_handle, 5);
    m.put32(0x10100, 0x10200); m.put32(0x10104, 4);
    for (unsigned i = 0; i < 4; ++i) m.put32(0x10200 + 4 * i, 0);
    m.put32(0x10200 + 4 * 1, 0x10300); m.put32(0x10300, 0); m.put32(0x10304, 5); m.put32(0x10308, COCKPIT);
    m.put32(COCKPIT + cockpit_target, 0);
    m.put32(cfg_va, CFG); m.put32(CFG + cfg_clock, 1000); m.put32(CFG + cfg_step, 16); m.put32(CFG + cfg_seta_a, 0x10000); m.put32(CFG + cfg_seta_b, 10);
    m.put32(player_root_va, M); m.put32(M + root_player_ship, P); m.put32(M + root_laser_fired, 100); m.put32(M + root_missile_fired, 7);
    m.put32(M + root_laser_hits, 0); m.put32(M + root_missile_hits, 0);
    m.fill(SECTOR + obj_class, sector_lists + 4 - obj_class); m.put16(SECTOR + obj_class, class_sector); m.put32(SECTOR + sector_lists, LISTS);
    m.fill(LISTS, 32 * list_stride);
    for (unsigned c = 0; c < 32; ++c) { m.put32(LISTS + c * list_stride, term(c)); m.put32(term(c), 0); }
    object(m, P, H, class_ship, player_group, PF, PR, 0, 0, 0);
    m.put32(P + obj_flags1, 2);
    m.put32(PF + flight_friend_mask, 1u << 4); m.put32(PF + flight_foe_mask, 1u << 3);
    object(m, H, O, class_ship, 3, HF, HR, 500000, 0, 0);
    object(m, O, F, class_ship, player_group, OF, OR_, 1000000, 0, 0);
    object(m, F, term(7), class_ship, 4, FF, FR, 0, 2000000, 0);
    m.put32(LISTS + class_ship * list_stride, P);
    object(m, X, term(10), class_missile, 3, XD, XR, 0, 0, 100);
    m.put32(XD + missile_launcher, H);
    m.put32(LISTS + class_missile * list_stride, X);
    return m;
}
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    const std::uint32_t hold = 20000;
    {   // Relation test and distance.
        FakeMem m = world();
        check(is_enemy(m, H, P) && !is_enemy(m, F, P) && !is_enemy(m, O, P) && !is_enemy(m, 0, P) && !is_enemy(m, H, 0), "relation: foe mask bit, friend, own group, null");
        check(is_enemy(m, X, P), "missile of a foe group: hostile through the masks");
        m.put16(X + obj_group, 5);
        check(!is_enemy(m, X, P), "unarmed missile of a neutral group: not hostile");
        m.put32(X + obj_flags1, flags1_missile_hostile);
        check(is_enemy(m, X, P), "armed missile of another group launched by another: hostile");
        m.put32(XD + missile_launcher, P);
        check(!is_enemy(m, X, P), "armed missile launched by P: not hostile");
        m.put32(XD + missile_launcher, H); m.put16(X + obj_group, player_group);
        check(!is_enemy(m, X, P), "armed missile of P's own group: not hostile");
        m.put16(X + obj_group, 3);
        m.put32(P + obj_flags2, flags2_neutral);
        check(!is_enemy(m, H, P), "neutral flag on P: nothing hostile");
        m.put32(P + obj_flags2, 0);
        m.put32(PF + flight_foe_mask, 0);
        check(!is_enemy(m, H, P), "no foe bit: not hostile");
        m.put32(PF + flight_enemy_of_enabled, 1); m.put32(HF + flight_foe_mask, 1u << player_group);
        check(is_enemy(m, H, P), "enemy-of enabled: H's stance towards P counts");
        m.put16(H + obj_group, 0x8000);
        m.put32(PF + flight_foe_mask, 0xffffffff); m.put32(PF + flight_enemy_of_enabled, 0);
        check(!is_enemy(m, H, P), "negative group: neutral");
        std::int32_t d = 0;
        check(chebyshev(m, H, P, d) && d == 500000 && chebyshev(m, F, P, d) && d == 2000000, "chebyshev: largest axis, absolute");
        m.put32(HR + render_position, std::uint32_t(-3000000));
        check(chebyshev(m, H, P, d) && d == 3000000, "chebyshev: negative difference");
        check(!chebyshev(m, 0x77777, P, d), "chebyshev: unreadable object fails");
        std::uint32_t cockpit = 0;
        check(active_cockpit(m, R, cockpit) && cockpit == COCKPIT, "cockpit lookup through the registry hash");
        m.put32(R + registry_cockpit_handle, 9);
        check(!active_cockpit(m, R, cockpit), "cockpit lookup: unknown handle");
    }
    {   // (b), the hold, newer-than-now, the vanilla precondition, the edges and the suppressed episode.
        FakeMem m = world();
        State s;
        m.put32(R + registry_level, 1);
        Frame f = frame(m, s, R, P, hold);
        check(f.ok && !f.engaged && f.wrote_zero && f.filtered == 0 && m.get32(R + registry_level) == 0 && m.writes == 1 && f.suppressed_start && !f.edge && f.cause == Cause::none,
              "no stamp: vanilla 1 held at 0, suppressed episode starts, no edge");
        check(f.seta == 10 && f.step == 16 && f.now == 1000, "clock, step and SETA factor read");
        m.put32(R + registry_level, 1); m.put32(P + obj_flags2, flags2_hit); m.put32(CFG + cfg_clock, 2000);
        f = frame(m, s, R, P, hold);
        check(f.ok && (f.reasons & reason_player_hit) && f.engaged && !f.wrote_zero && f.filtered == 1 && m.get32(R + registry_level) == 1 && f.edge && f.from == 0
              && f.suppressed_end && f.episode_frames == 1 && f.cause == Cause::stamp && f.age == 0, "(b) player hit: stamp, level kept, edge 0->1, episode ends after 1 frame");
        m.put32(P + obj_flags2, 0); m.put32(R + registry_level, 1); m.put32(R + registry_committed, 1); m.put32(CFG + cfg_clock, 2000 + hold - 1);
        f = frame(m, s, R, P, hold);
        check(f.engaged && !f.edge && !f.wrote_zero && f.cause == Cause::held && f.age == hold - 1 && !f.suppressed_start, "held for hold-1 ms");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 2000 + hold);
        f = frame(m, s, R, P, hold);
        check(!f.engaged && f.wrote_zero && f.edge && f.from == 1 && f.filtered == 0 && f.vanilla == 1 && f.cause == Cause::expired && f.suppressed_start && m.get32(R + registry_level) == 0,
              "expired at hold ms: zeroed, edge 1->0, new episode");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1500);
        f = frame(m, s, R, P, hold);
        check(!f.engaged && f.wrote_zero && f.cause == Cause::newer && !f.suppressed_start, "stamp newer than now: expired, same episode");
        m.put32(R + registry_level, 1); m.put32(P + obj_flags2, flags2_hit); m.put32(CFG + cfg_clock, 50000);
        f = frame(m, s, R, P, hold);
        check(f.engaged && f.filtered == 1 && f.suppressed_end && f.episode_frames == 2, "episode of 2 frames ends on the next stamp");
        m.put32(P + obj_flags2, 0); m.put32(R + registry_level, 0); m.put32(CFG + cfg_clock, 50001);
        const unsigned writes = m.writes;
        f = frame(m, s, R, P, hold);
        check(f.ok && f.engaged && f.filtered == 0 && !f.wrote_zero && m.writes == writes && f.edge && f.from == 1 && f.cause == Cause::range && !f.suppressed_start && f.walk.visited == 0,
              "vanilla 0: precondition (still within the hold), nothing written, no walk, edge 1->0 out_of_range");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 50002);
        f = frame(m, s, R, P, hold);
        check(f.engaged && f.filtered == 1 && f.cause == Cause::held, "vanilla back within the hold: engaged again");
    }
    {   // (a): the counters and the selected target.
        FakeMem m = world();
        State s;
        m.put32(M + root_laser_fired, 100); m.put32(COCKPIT + cockpit_target, H); m.put32(R + registry_level, 0);
        Frame f = frame(m, s, R, P, hold);
        check(f.ok && !f.fired && f.reasons == 0, "first sample is the baseline, not a shot");
        m.put32(M + root_laser_fired, 101); m.put32(CFG + cfg_clock, 60000);
        f = frame(m, s, R, P, hold);
        check(f.fired && (f.reasons & reason_fire) && f.walk.target_found && f.walk.target_hostile && f.walk.visited >= 2 && f.filtered == 0 && !f.wrote_zero,
              "(a) laser counter advanced with a hostile target: stamp even with vanilla 0, nothing written");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 60000 + 1000);
        f = frame(m, s, R, P, hold);
        check(f.engaged && f.filtered == 1 && f.cause == Cause::held, "a fight that started before the hostile came into range: combat music on the first in-range frame");
        m.put32(M + root_laser_fired, 102); m.put32(COCKPIT + cockpit_target, F); m.put32(CFG + cfg_clock, 60000 + hold + 5000); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.fired && !(f.reasons & reason_fire) && f.walk.target_found && !f.walk.target_hostile && !f.engaged && f.wrote_zero, "firing at a friend: no stamp, expired, zeroed");
        m.put32(M + root_missile_fired, 8); m.put32(COCKPIT + cockpit_target, 0x77770); m.put32(R + registry_level, 0);
        f = frame(m, s, R, P, hold);
        check(f.fired && !f.walk.target_found && f.reasons == 0, "missile counter advanced with a target not in the lists: no stamp (pointer never dereferenced)");
        m.put32(M + root_missile_fired, 2); m.put32(COCKPIT + cockpit_target, H);
        f = frame(m, s, R, P, hold);
        check(!f.fired && f.reasons == 0, "a decreased counter is a new baseline");
        m.put32(M + root_laser_fired, 103); m.put32(COCKPIT + cockpit_target, P);
        f = frame(m, s, R, P, hold);
        check(f.fired && f.walk.target == 0 && f.walk.visited == 0 && f.reasons == 0, "the player ship as target: ignored");
        m.put32(M + root_laser_fired, 104); m.put32(COCKPIT + cockpit_target, H); m.put16(SECTOR + obj_class, 6);
        f = frame(m, s, R, P, hold);
        check(f.fired && !f.in_sector && f.walk.visited == 0 && f.reasons == 0, "docked (parent not a sector): no walk, no (a)");
    }
    {   // (c): player-owned ships near P, only while the vanilla level is nonzero.
        FakeMem m = world();
        State s;
        m.put32(O + obj_flags2, flags2_hit); m.put32(R + registry_level, 0);
        Frame f = frame(m, s, R, P, hold);
        check(f.ok && f.walk.visited == 0 && f.reasons == 0, "(c) not evaluated while vanilla is 0");
        m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_owned) && f.walk.owned_hit && f.walk.owned_near == 1 && f.engaged && f.filtered == 1, "(c) owned ship hit within range");
        m.put32(O + obj_flags2, 0); m.put32(OR_ + render_position, 2500000); m.put32(O + obj_flags2, flags2_hit); m.put32(CFG + cfg_clock, 1000 + hold); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_owned) && f.walk.owned_near == 0 && !f.engaged && f.wrote_zero, "owned ship at exactly 2,500,000 units: out of range");
        m.put32(O + obj_flags2, 0); m.put32(OR_ + render_position, 2499999); m.put16(OF + flight_order, order_attack); m.put32(OF + flight_target, H); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_owned) && f.walk.owned_attack && f.engaged, "(c) owned ship with an attack order on a hostile");
        m.put16(OF + flight_order, order_attack_big); m.put32(OF + flight_target, F); m.put32(CFG + cfg_clock, 1000 + 2 * hold); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_owned) && !f.engaged && f.wrote_zero, "attack order on a friend: no stamp");
        m.put16(OF + flight_order, 0x15); m.put32(OF + flight_target, H); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_owned), "order 0x15 on a hostile: no stamp");
        m.put16(OF + flight_order, order_attack); m.put32(OF + flight_target, 0); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_owned), "attack order with a null target (death fix-up): no stamp");
        m.put16(PF + flight_order, order_attack); m.put32(PF + flight_target, H); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.reasons == 0, "the player ship's own order is not an owned-ship event");
        m.put16(H + obj_group, player_group); m.put32(H + obj_flags2, flags2_hit); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_owned) && f.walk.owned_near == 1, "a second owned ship (group 10) hit in range counts; the walk stops at the first event");
        m.put32(R + registry_level, 1); m.put32(H + obj_flags2, 0); m.put16(H + obj_group, 3);
        m.put32(LISTS + class_5 * list_stride, 0x70000); m.fill(0x70000, header_span); m.put32(0x70000, 0x70000); m.put16(0x70000 + obj_group, 3);
        m.put32(CFG + cfg_clock, 1000 + 3 * hold);
        f = frame(m, s, R, P, hold);
        check(f.ok && f.walk.cuts == 1 && f.walk.visited == walk_node_budget && walk_node_budget < list_walk_limit && f.walk.status == WalkStatus::ok && !f.engaged,
              "a cyclic list is cut at the node budget (below the per-list limit) and counted");
    }
    {   // No player ship, unreadable inputs.
        FakeMem m = world();
        State s;
        m.put32(R + registry_level, 1);
        Frame f = frame(m, s, R, 0, hold);
        check(f.ok && !f.in_sector && f.reasons == 0 && f.wrote_zero, "P = 0: vanilla level held at 0");
        f = frame(m, s, 0x99990, P, hold);
        check(!f.ok && !f.wrote_zero, "unreadable registry: nothing decided, nothing written");
        m.bytes.erase(cfg_va); m.bytes.erase(cfg_va + 1); m.bytes.erase(cfg_va + 2); m.bytes.erase(cfg_va + 3);
        f = frame(m, s, R, P, hold);
        check(!f.ok && !f.wrote_zero, "unreadable clock: nothing decided");
    }
    {   // Parsers and names.
        std::uint32_t ms = 0;
        check(parse_hold_s(L"20", 2, ms) && ms == 20000 && parse_hold_s(L"1", 1, ms) && ms == 1000 && parse_hold_s(L"600", 3, ms) && ms == 600000, "hold_s accepted");
        check(!parse_hold_s(L"0", 1, ms) && ms == default_hold_ms && !parse_hold_s(L"601", 3, ms) && !parse_hold_s(L"2a", 2, ms) && !parse_hold_s(L"", 0, ms) && !parse_hold_s(L"1000", 4, ms) && !parse_hold_s(L"-5", 2, ms),
              "hold_s refused: 0, 601, letters, empty, four digits, sign (default stands)");
        check(parse_trigger(L"", 0) == Trigger::engaged && parse_trigger(L"engaged", 7) == Trigger::engaged && parse_trigger(L"vanilla", 7) == Trigger::vanilla, "trigger words");
        check(parse_trigger(L"Engaged", 7) == Trigger::invalid && parse_trigger(L"engagedx", 8) == Trigger::invalid && parse_trigger(L"on", 2) == Trigger::invalid && parse_trigger(L"vanill", 6) == Trigger::invalid,
              "trigger: case, length, other words invalid");
        char r[4];
        check(!std::strcmp(reason_text(reason_fire | reason_player_hit | reason_owned, r), "abc") && !std::strcmp(reason_text(0, r), "-") && !std::strcmp(reason_text(reason_owned, r), "c"), "reason text");
        check(!std::strcmp(cause_name(Cause::newer), "newer_than_now") && !std::strcmp(cause_name(Cause::range), "out_of_range") && !std::strcmp(cause_name(Cause::none), "none"), "cause names");
        check(window_count == 19 && site_length == 6 && site_va + site_length == continue_va && site_window_offset == 15 && site_window[site_window_offset] == site_bytes[0], "site constants");
        check(!std::strcmp(reason_text(reason_missile | reason_hit | reason_hostile_order, r), "def"), "reason text d/e/f");
    }
    {   // (d) level 2, (e) the hit counters, (f) a hostile's attack order on P or an owned ship, the class-7 rule for orders.
        FakeMem m = world();
        State s;
        m.put32(R + registry_level, 2);
        Frame f = frame(m, s, R, P, hold);
        check(f.ok && (f.reasons & reason_missile) && f.engaged && f.filtered == 2 && !f.wrote_zero, "(d) vanilla level 2: stamp, level kept");
        m.put32(R + registry_level, 1); m.put32(M + root_laser_hits, 1); m.put32(CFG + cfg_clock, 1000 + hold);
        f = frame(m, s, R, P, hold);
        check(f.hit && !f.walk.hit_victim && f.reasons == 0 && !f.engaged && f.wrote_zero, "(e) hit counter rose but no hostile carries the player-hit bit (asteroid): no stamp");
        m.put32(R + registry_level, 0); m.put32(M + root_laser_hits, 2); m.put32(F + obj_flags2, flags2_player_hit);
        f = frame(m, s, R, P, hold);
        check(f.hit && !f.walk.hit_victim && f.reasons == 0, "(e) the victim is a friend: no stamp");
        m.put32(F + obj_flags2, 0); m.put32(M + root_missile_hits, 1); m.put32(H + obj_flags2, flags2_player_hit);
        f = frame(m, s, R, P, hold);
        check(f.hit && f.walk.hit_victim && (f.reasons & reason_hit) && f.filtered == 0 && !f.wrote_zero, "(e) missile hit counter rose and a hostile carries the player-hit bit: stamp even with vanilla 0");
        m.put32(H + obj_flags2, 0); m.put32(M + root_missile_hits, 1); m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1000 + 3 * hold);
        f = frame(m, s, R, P, hold);
        check(!f.hit && f.walk.visited > 0 && f.reasons == 0 && f.wrote_zero, "unchanged hit counters: no (e) walk term; the events walk alone finds nothing");
        m.put16(HF + flight_order, order_attack); m.put32(HF + flight_target, P); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_hostile_order) && f.walk.hostile_orders == 1 && f.engaged, "(f) hostile attack order on the player ship");
        m.put32(HF + flight_target, O); m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1000 + 5 * hold);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_hostile_order) && f.walk.hostile_orders == 1, "(f) hostile attack order on an owned ship within range");
        m.put32(OR_ + render_position, 2500000); m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1000 + 7 * hold);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_hostile_order) && f.walk.hostile_orders == 0 && f.wrote_zero, "(f) the owned target out of range: no stamp");
        m.put32(OR_ + render_position, 0); m.put32(HF + flight_target, F); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_hostile_order) && f.walk.hostile_orders == 0, "(f) attack order on a third party: no stamp");
        m.put32(HF + flight_target, P); m.put16(HF + flight_order, order_attack_big); m.put16(H + obj_group, 4); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_hostile_order) && f.walk.hostile_orders == 1, "(f) a friend's attack order on P: one relation test, no stamp");
        m.put16(H + obj_group, 3); m.put16(H + obj_class, class_6); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_hostile_order) && f.walk.hostile_orders == 0, "(f) orders are read on class-7 ships only");
        m.put16(H + obj_class, class_ship); m.put16(HF + flight_order, 0);
        m.put16(O + obj_class, class_6); m.put16(OF + flight_order, order_attack); m.put32(OF + flight_target, H); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!(f.reasons & reason_owned) && f.walk.owned_near == 1, "(c) an owned class-6 object's order word is not read (stations and missiles carry group 10)");
        m.put32(O + obj_flags2, flags2_hit); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check((f.reasons & reason_owned) && f.walk.owned_hit, "(c) the hit bit counts for any owned node");
    }
    {   // Re-baseline: a save-load event, a changed player ship, a clock that went backwards.
        FakeMem m = world();
        State s;
        m.put32(COCKPIT + cockpit_target, H); m.put32(M + root_laser_fired, 100);
        Frame f = frame(m, s, R, P, hold);
        check(f.rebaselined && !f.fired, "first frame");
        m.put32(M + root_laser_fired, 150); m.put32(M + root_laser_hits, 9); s.rebaseline = true; m.put32(CFG + cfg_clock, 2000);
        f = frame(m, s, R, P, hold);
        check(f.rebaselined && !f.fired && !f.hit && f.reasons == 0 && !s.rebaseline, "after the save-load marker the restored counters are a baseline");
        m.put32(M + root_laser_fired, 151); m.put32(CFG + cfg_clock, 2100);
        f = frame(m, s, R, P, hold);
        check(!f.rebaselined && f.fired && (f.reasons & reason_fire), "the next shot counts");
        m.put32(M + root_laser_fired, 152); m.put32(CFG + cfg_clock, 1900);
        f = frame(m, s, R, P, hold);
        check(f.rebaselined && !f.fired && f.reasons == 0, "a clock that went backwards: baseline");
        m.put32(M + root_laser_fired, 153); m.put32(CFG + cfg_clock, 2200);
        object(m, 0x64000, term(7), class_ship, player_group, 0x64400, 0x64800, 0, 0, 0); m.put32(F + obj_next, 0x64000);
        m.put32(M + root_player_ship, 0x64000);
        f = frame(m, s, R, 0x64000, hold);
        check(f.rebaselined && !f.fired && f.reasons == 0 && f.in_sector, "a changed player ship pointer (load, ship change): baseline");
        m.put32(M + root_laser_fired, 154); m.put32(CFG + cfg_clock, 2300);
        f = frame(m, s, R, 0x64000, hold);
        check(!f.rebaselined && f.fired, "then shots count again");
    }
    {   // Run 190 (x3m-flight2.log): the list headers are in the array [sector+0x50]; walk= and fire= name the outcome.
        FakeMem m = world();
        State s;
        m.put32(R + registry_level, 1); m.put16(HF + flight_order, order_attack); m.put32(HF + flight_target, P);
        Frame f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::ok && f.walk.visited >= 1 && (f.reasons & reason_hostile_order) && f.engaged && !f.wrote_zero,
              "Run 190: a hostile in range with an attack order on P is found through the list array (visited > 0, f)");
        check(!std::strcmp(walk_status_name(f.walk.status), "ok") && s.last_fire == FireStatus::none && !std::strcmp(fire_status_name(s.last_fire), "none"), "walk=ok fire=none");
        m.put32(SECTOR + sector_lists + class_ship * list_stride, H); // the old, inline reading would find H here
        m.put32(SECTOR + sector_lists, 0); m.put32(CFG + cfg_clock, 1000 + hold); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::lists_base && f.walk.visited == 0 && f.reasons == 0 && f.wrote_zero && !std::strcmp(walk_status_name(f.walk.status), "lists_base"),
              "a null list array: walk=lists_base, the sector's own bytes are never read as list headers");
        m.put32(SECTOR + sector_lists, 0x58000); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::list_head && f.walk.visited == 0 && f.reasons == 0, "an unreadable list header: walk=list_head");
        m.put32(SECTOR + sector_lists, LISTS); m.put32(LISTS + class_5 * list_stride, 0x77770); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::node_link && (f.reasons & reason_hostile_order) && f.walk.visited >= 1, "an unreadable node in list 5: walk=node_link, list 7 still walked");
        m.put32(LISTS + class_5 * list_stride, term(5)); m.put32(R + registry_level, 0); m.put32(CFG + cfg_clock, 1000 + 2 * hold + 10);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::idle && f.walk.visited == 0, "vanilla 0 and no counter rose: walk=idle");
        m.put32(R + registry_level, 1);
        f = frame(m, s, R, 0, hold);
        check(f.walk.status == WalkStatus::no_player && !std::strcmp(walk_status_name(f.walk.status), "no_player"), "P = 0: walk=no_player");
        m.put32(P + obj_parent, 0); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::player_parent && !f.in_sector, "P without a parent: walk=player_parent");
        m.put32(P + obj_parent, SECTOR); m.put16(SECTOR + obj_class, 6); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.walk.status == WalkStatus::parent_class && !f.in_sector, "docked: walk=parent_class");
        m.put16(SECTOR + obj_class, class_sector);
    }
    {   // Run 190: the player's shots at a hostile, and the stamp before the hold test in the same frame.
        FakeMem m = world();
        State s;
        m.put32(R + registry_level, 1); m.put32(P + obj_flags2, flags2_hit); m.put32(CFG + cfg_clock, 27726147);
        Frame f = frame(m, s, R, P, hold); // baseline frame; (b) stamps
        check(f.engaged && f.reasons == reason_player_hit && f.edge, "Run 190: hit by a hostile, edge 0->1 reason=b");
        m.put32(P + obj_flags2, 0); m.put32(COCKPIT + cockpit_target, H);
        std::uint32_t laser = 100, now = 27726147;
        bool dropped = false;
        for (unsigned i = 0; i < 2500; ++i) { // 40 s of 16 ms frames, a shot every 8th frame, no further hit on P
            now += 16;
            if (i % 8 == 0) m.put32(M + root_laser_fired, ++laser);
            m.put32(CFG + cfg_clock, now); m.put32(R + registry_level, 1); m.put32(R + registry_committed, 1);
            f = frame(m, s, R, P, hold);
            dropped = dropped || !f.engaged || f.wrote_zero;
        }
        check(!dropped && s.last_fire == FireStatus::ok && (s.last_reason & reason_fire), "Run 190: 40 s of player fire at a hostile target keeps the alert past age 20,002 ms");
        check(!std::strcmp(fire_status_name(s.last_fire), "ok"), "fire=ok");
        // The frame whose hold would expire takes its own stamp first.
        m.put32(CFG + cfg_clock, now + hold + 5000); m.put32(M + root_laser_fired, ++laser); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.engaged && f.cause == Cause::stamp && f.age == 0 && !f.wrote_zero && f.filtered == 1 && !f.edge, "a shot on the frame the hold would run out: stamp first, no drop");
        // (e) through the list array: the player's hit counter rose and a hostile carries the player-hit bit.
        m.put32(CFG + cfg_clock, now + 3 * hold); m.put32(M + root_laser_hits, 1); m.put32(H + obj_flags2, flags2_player_hit); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.hit && (f.reasons & reason_hit) && f.walk.hit_victim && f.walk.status == WalkStatus::ok && f.engaged, "Run 190: (e) a hostile hit by the player is found through the list array");
        m.put32(H + obj_flags2, 0);
        const struct { std::uint32_t target; FireStatus want; const char* name; } cases[] = {
            {F, FireStatus::not_hostile, "not_hostile"}, {0x77770, FireStatus::not_found, "not_found"}, {P, FireStatus::self_target, "self_target"},
            {0, FireStatus::no_target, "no_target"}};
        for (const auto& c : cases) {
            m.put32(COCKPIT + cockpit_target, c.target); m.put32(M + root_laser_fired, ++laser); m.put32(R + registry_level, 0);
            f = frame(m, s, R, P, hold);
            check(f.fired && !(f.reasons & reason_fire) && s.last_fire == c.want && !std::strcmp(fire_status_name(s.last_fire), c.name), c.name);
        }
        m.put32(R + registry_cockpit_handle, 9); m.put32(COCKPIT + cockpit_target, H); m.put32(M + root_laser_fired, ++laser);
        f = frame(m, s, R, P, hold);
        check(f.fired && f.reasons == 0 && s.last_fire == FireStatus::cockpit, "unknown cockpit handle: fire=cockpit");
        m.put32(R + registry_cockpit_handle, 5); m.put16(SECTOR + obj_class, 6); m.put32(M + root_laser_fired, ++laser);
        f = frame(m, s, R, P, hold);
        check(f.fired && f.reasons == 0 && s.last_fire == FireStatus::not_in_sector, "docked: fire=not_in_sector");
        m.put16(SECTOR + obj_class, class_sector); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(!f.fired && s.last_fire == FireStatus::not_in_sector, "fire= keeps the last fired frame's outcome");
    }
    {   // The span-read layout (performance pass): a quiet frame makes six validated span reads and two image loads; the
        // walk reads the four list headers in one span, one header span per node, an object's flight block and position
        // once each, P's flight block and position once per frame; no object is read field by field.
        FakeMem m = world();
        State s;
        frame(m, s, R, P, hold); // baseline
        m.start(); m.image_loads = 0; m.put32(CFG + cfg_clock, 1016);
        Frame f = frame(m, s, R, P, hold);
        const Span quiet[] = {{R + registry_committed, 8}, {CFG + cfg_step, 8}, {M + root_laser_fired, 16}, {CFG + cfg_seta_a, 8}, {P, header_span},
                              {SECTOR + obj_class, sector_lists + 4 - obj_class}};
        const unsigned nq = sizeof quiet / sizeof quiet[0];
        bool same = m.log.size() == nq && f.reads == nq && nq == 6;
        for (unsigned i = 0; same && i < nq; ++i) same = m.log[i].at == quiet[i].at && m.log[i].n == quiet[i].n;
        check(f.ok && same && m.image_loads == 2 && f.walk.status == WalkStatus::idle && f.now == 1016 && f.seta == 10,
              "quiet frame: six validated span reads in order, cfg and M through two image loads");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1032); m.start();
        f = frame(m, s, R, P, hold);
        const unsigned heads = class_missile * list_stride + 4 - class_5 * list_stride;
        const Span walk[] = {{LISTS + class_5 * list_stride, heads}, {term(5), header_span}, {term(5), 4}, {term(6), header_span}, {term(6), 4},
                             {P, header_span}, {H, header_span}, {HF + flight_begin, flight_span}, {O, header_span}, {OR_ + render_position, 12},
                             {PR + render_position, 12}, {OF + flight_begin, flight_span}, {F, header_span}, {FF + flight_begin, flight_span},
                             {term(7), header_span}, {term(7), 4}, {X, header_span}, {term(10), header_span}, {term(10), 4}};
        const unsigned nw = sizeof walk / sizeof walk[0];
        same = m.log.size() == nq + nw && f.reads == nq + nw;
        for (unsigned i = 0; same && i < nw; ++i) same = m.log[nq + i].at == walk[i].at && m.log[nq + i].n == walk[i].n;
        check(f.ok && same && f.walk.status == WalkStatus::ok && f.walk.visited == 5 && f.walk.owned_near == 1 && f.reasons == 0,
              "walking frame: list headers in one span, one header span per node, flight blocks and positions once, terminators by their link");
        m.put32(R + registry_level, 0); m.put32(CFG + cfg_clock, 1048);
        f = frame(m, s, R, P, hold);
        check(f.alert_end && f.alert.frames == 1 && f.alert.reads_max == nq + nw && f.alert.reads == nq + nw && f.alert.visited_max == 5 && f.alert.cuts == 0 && s.alert_episode.frames == 0,
              "episode summary: frames, reads and visited of the vanilla-nonzero run, reported when vanilla returns to 0");
        m.put32(R + registry_level, 1); m.put32(CFG + cfg_clock, 1064);
        f = frame(m, s, R, P, hold);
        check(!f.alert_end && s.alert_episode.frames == 1, "a new episode starts on the next nonzero frame");
        // (e) with two non-hostile victims carrying the player-hit bit: P's flight block is read once for both tests.
        m.put32(R + registry_level, 0); m.put32(M + root_laser_hits, 1); m.put32(O + obj_flags2, flags2_player_hit); m.put32(F + obj_flags2, flags2_player_hit);
        m.put32(CFG + cfg_clock, 1080); m.start();
        f = frame(m, s, R, P, hold);
        check(f.hit && !f.walk.hit_victim && m.count(PF + flight_begin, flight_span) == 1 && m.count(OF + flight_begin, flight_span) == 0 && m.count(P, 4) == 0 && m.count(P + obj_flags2, 4) == 0,
              "relation tests share P's flight block (one read); no field-by-field read of P");
    }
    {   // The node budget over the four lists: 5,000 passive hostiles in lists 5 and 6 (each under the per-list limit).
        FakeMem m = world();
        State s;
        auto chain = [&](unsigned c, std::uint32_t base, unsigned n) {
            m.put32(LISTS + c * list_stride, base);
            for (unsigned i = 0; i < n; ++i) {
                const std::uint32_t o = base + i * 0x80;
                m.fill(o, header_span); m.put32(o + obj_next, i + 1 < n ? o + 0x80 : term(c)); m.put16(o + obj_group, 3); m.put16(o + obj_class, std::uint16_t(c));
            }
        };
        chain(class_5, 0x1000000, 2500); chain(class_6, 0x2000000, 2500);
        m.put32(R + registry_level, 1);
        Frame f = frame(m, s, R, P, hold);
        check(f.ok && f.walk.visited == walk_node_budget && f.walk.cuts == 1 && f.walk.status == WalkStatus::ok && f.reads <= walk_node_budget + 16 && f.wrote_zero,
              "the node budget ends the walk over several lists and is counted in cuts");
        chain(class_6, 0x2000000, 1000); m.put32(CFG + cfg_clock, 1016); m.put32(R + registry_level, 1);
        f = frame(m, s, R, P, hold);
        check(f.ok && 2500 + 1000 + 5 <= walk_node_budget && f.walk.visited == 2500 + 1000 + 5 && f.walk.cuts == 0, "under the budget: every node visited, no cut");
    }
    std::printf("checks done, failures=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def header_windows():
    """(name, va, bytes) of core::windows parsed from the header, in table order."""
    text = CORE.read_text()
    arrays = {}
    for name, n, body in re.findall(r'constexpr unsigned char (\w+)\[(\w+)\] = \{([^}]*)\};', text):
        values = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', body))
        assert len(values) == (int(n) if n.isdigit() else 6), name  # site_bytes[site_length]
        arrays[name] = values
    table = re.search(r'constexpr Window windows\[\] = \{(.*?)\n\};', text, re.S).group(1)
    rows = re.findall(r'\{"(\w+)", (0x[0-9a-f]+), (\w+), sizeof \w+\}', table)
    return [(name, int(va, 16), arrays[array]) for name, va, array in rows], arrays


class MusicAlertSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.EXE.read_bytes() if probe.EXE.is_file() else None

    def need_exe(self):
        if self.data is None:
            self.skipTest(f'{probe.EXE} absent')

    def test_installed_executable_passes(self):
        self.need_exe()
        failures, checks, detail = probe.verify(self.data)
        self.assertEqual(failures, [])
        self.assertEqual(checks, 1 + len(probe.WINDOWS) + 2 + 1 + len(probe.CALLS) + len(probe.CALLERS) + 1)
        self.assertEqual(detail['callers'], {'0042c890': ['0041cdea'], '0041cde0': ['00403f2f']})
        self.assertEqual(detail['interior_branches'], [])

    def mutated(self, va, new):
        data = bytearray(self.data)
        off = TEXT_RAW + va - TEXT_VA
        data[off:off + len(new)] = new
        return bytes(data)

    def test_patched_copies_are_refused(self):
        self.need_exe()
        for name, va, hx in probe.WINDOWS:
            with self.subTest(window=name):
                raw = probe.window_bytes(hx)
                for position in (0, len(raw) - 1):
                    failures, _, _ = probe.verify(self.mutated(va + position, bytes([raw[position] ^ 0x01])))
                    self.assertIn('window_' + name, failures)
        failures, _, _ = probe.verify(self.mutated(probe.SITE_VA, b'\x90' * 6))
        self.assertIn('site_bytes', failures)
        self.assertIn('window_site_window', failures)
        # A rel32 jump from the int3 padding after 0x004508f0 into the site's interior, and an extra caller.
        pad = 0x004508f1
        jump = b'\xe9' + struct.pack('<i', probe.SITE_VA + 3 - (pad + 5))
        failures, _, detail = probe.verify(self.mutated(pad, jump))
        self.assertIn('interior_branch', failures)
        self.assertEqual(detail['interior_branches'], ['%08x->%08x' % (pad, probe.SITE_VA + 3)])
        call = b'\xe8' + struct.pack('<i', probe.ALERT_VA - (pad + 5))
        failures, _, _ = probe.verify(self.mutated(pad, call))
        self.assertIn('callers_%08x' % probe.ALERT_VA, failures)
        onto_site = b'\xe9' + struct.pack('<i', probe.SITE_VA - (pad + 5))
        failures, _, _ = probe.verify(self.mutated(pad, onto_site))
        self.assertNotIn('interior_branch', failures)  # a branch onto the first byte is allowed
        data = bytearray(self.data)
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        struct.pack_into('<I', data, pe + 8, 0x12345678)  # TimeDateStamp
        failures, _, _ = probe.verify(bytes(data))
        self.assertIn('exe_identity', failures)

    def test_core_header_matches_the_verifier(self):
        rows, arrays = header_windows()
        self.assertEqual([(n, va, b) for n, va, b in rows], [(n, va, probe.window_bytes(hx)) for n, va, hx in probe.WINDOWS])
        self.assertEqual(arrays['site_bytes'], probe.SITE_BYTES)
        text = CORE.read_text()
        for name, value in (('site_va', probe.SITE_VA), ('continue_va', probe.CONTINUE_VA), ('alert_va', probe.ALERT_VA),
                            ('site_branch_va', probe.SITE_BRANCH_VA), ('walk_va', probe.WALK_VA), ('walk_caller_va', probe.WALK_CALLER_VA),
                            ('main_loop_alert_va', probe.MAIN_LOOP_ALERT_VA), ('relation_va', probe.RELATION_VA),
                            ('stance_va', probe.STANCE_VA), ('distance_va', probe.DISTANCE_VA)):
            self.assertRegex(text, r'\b%s = 0x%08x\b' % (name, value), name)
        self.assertIn('alert_range = 2500000', text)
        self.assertIn('bytes.fromhex(\'8b4e08 8b4604\'', (ROOT / 'verification/results/music-script-driver/verify_music_alert_sites.py').read_text())


class MusicAlertRule(unittest.TestCase):
    def test_rule_compiled_from_the_core_header(self):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'harness.cpp'
            source.write_text(HARNESS)
            executable = Path(directory) / 'harness'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True, timeout=300)
            self.assertEqual(build.returncode, 0, build.stderr[-3000:])
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertNotIn('FAIL', run.stdout)
            self.assertIn('failures=0', run.stdout)
            self.assertGreaterEqual(HARNESS.count('check('), 50)


class MusicAlertSource(unittest.TestCase):
    def test_stub_shape(self):
        text = MODULE.read_text()
        asm = re.search(r'asm\(R"\((.*?)\)"\);', text, re.S).group(1)
        lines = [l.strip() for l in asm.split('\n') if l.strip() and not l.strip().startswith('.')]
        self.assertEqual(lines, ['_x3m_music_alert_stub:', 'pushfd', 'cld', 'push eax', 'push ecx', 'push edx', 'push edi', 'push esi',
                                 'call _x3m_music_alert_frame', 'add esp, 8', 'pop edx', 'pop ecx', 'pop eax', 'popfd',
                                 'jmp dword ptr [_x3m_music_alert_continue]'])
        for word in ('fld', 'fst', 'fild', 'xmm', 'mov esi', 'mov edi', 'ret'):
            self.assertNotIn(word, asm, word)
        self.assertIn('LightCallBoundary cpu', text)
        self.assertIn('if (!armed_) return;', text)
        self.assertNotIn('malloc', text)
        self.assertNotIn('new ', text)

    def test_production_wiring(self):
        self.assertIn('src/proxy/music_alert.cpp', (ROOT / 'CMakeLists.txt').read_text())
        self.assertIn('music_alert::initialize();', (ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertIn('x3m::music_alert::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        roots = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_music_alert_stub'", roots)
        self.assertIn("'_x3m_music_alert_frame'", roots)
        text = MODULE.read_text()
        self.assertIn('x3m::config::get(L"X3M_MUSIC_COMBAT_TRIGGER"', text)
        self.assertIn('x3m::config::get(L"X3M_MUSIC_COMBAT_HOLD_S"', text)
        self.assertIn('engine_memory::read', text)
        self.assertIn('install_window_open()', text)
        self.assertIn('executable_verified()', text)
        self.assertIn('"late_claim"', text)
        self.assertIn('"bytes_mismatch"', text)
        self.assertIn('"rollback_failed"', text)

    def test_schema_entries(self):
        trigger = schema.BY_KEY['music_combat_trigger']
        self.assertEqual((trigger['type'], trigger['section'], trigger['default'], trigger['choices'], trigger['launcher']),
                         ('enum', 'audio', 'engaged', ('engaged', 'vanilla'), '--music-combat-trigger'))
        hold = schema.BY_KEY['music_combat_hold_s']
        self.assertEqual((hold['type'], hold['section'], hold['default'], hold['launcher'], hold['requires']),
                         ('int', 'audio', '20', '--music-combat-hold-s', ('music_combat_trigger',)))
        self.assertEqual(hold['range'], ((1.0, 600.0, False),))
        self.assertFalse(trigger['developer'] or hold['developer'])
        keys = [e['key'] for e in schema.SETTINGS if e['section'] == 'audio' and not e['developer']]
        self.assertEqual(keys[:3], ['music_keep', 'music_queue', 'voice_dmo_fallback'])
        self.assertIn('music_combat_trigger', keys)
        self.assertIn('music_combat_hold_s', keys)
        template = (ROOT / 'assets/x3m.ini').read_text()
        self.assertIn(';music_combat_trigger = engaged', template)
        self.assertIn(';music_combat_hold_s = 20', template)


INSTALL_HARNESS = r'''
#include "music_alert_core.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace x3m::music_alert::core;
// argv: the .text image file, its VA, and optionally a VA from which every read fails.
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<unsigned char> text;
    for (int c; (c = std::fgetc(f)) != EOF;) text.push_back(static_cast<unsigned char>(c));
    std::fclose(f);
    const std::uint32_t base = std::uint32_t(std::strtoul(argv[2], nullptr, 0));
    const std::uint32_t fail = argc > 3 ? std::uint32_t(std::strtoul(argv[3], nullptr, 0)) : 0xffffffffu;
    auto read = [&](std::uint32_t at, unsigned char* out, unsigned n) {
        if (at < base || at - base + n > text.size() || at + n > fail) return false;
        for (unsigned i = 0; i < n; ++i) out[i] = text[at - base + i];
        return true;
    };
    Mismatch m;
    if (install_windows_match(read, m)) {
        std::printf("match\n");
        return 0;
    }
    std::printf("mismatch name=%s va=0x%08x readable=%u expected=", m.name, unsigned(m.va), m.readable ? 1u : 0u);
    for (unsigned i = 0; i < m.shown; ++i) std::printf("%02x", m.expected[i]);
    std::printf(" got=");
    for (unsigned i = 0; i < m.shown; ++i) std::printf("%02x", m.got[i]);
    std::printf("\n");
    return 0;
}
'''


def collected_claims():
    """(owner base, name, va, length) of every engine site src/proxy claims outside music_alert, through the
    claim collector's own patterns (verify_collide_sites: SiteSpec initialisers and *site_va constants)."""
    sys.path.insert(0, str(ROOT / 'verification/probe'))
    import verify_collide_sites as collector
    claims = []
    for path in sorted((ROOT / 'src/proxy').glob('*')):
        if path.suffix not in ('.h', '.cpp') or path.name.startswith('music_alert'):
            continue
        text = path.read_text(errors='replace')
        base = re.sub(r'_(core|sites)$', '', path.stem)
        claims += [(base, name, int(va, 16), int(length)) for name, va, _, length in collector._SPEC_RE.findall(text)]
        claims += [(base, name, int(va, 16), 8) for name, va in collector._CORE_SITE_RE.findall(text)]
    return claims


# Sites the Run 189 install rows report before the music_alert row that the collector's patterns do not reach
# (music_keep site_e / site_entry, music_trace play / stop-all, seta_restore's hook): applied with an 8-byte cover.
UNCOLLECTED_EARLIER = (('music_keep', 'site_e', 0x00498d71, 8), ('music_keep', 'site_entry', 0x004982b0, 8),
                       ('music_keep', 'trace_play', 0x00498c90, 8), ('seta_restore', 'hook', 0x004b148c, 8))


def earlier_features():
    """The engine-patch namespaces capture.cpp initialises before music_alert::initialize(), in that order."""
    text = (ROOT / 'src/proxy/capture.cpp').read_text()
    return re.findall(r'\b(\w+)::initialize\(', text[:text.index('music_alert::initialize();')])


class MusicAlertInstallCheck(unittest.TestCase):
    """core::install_windows_match, the installer's compare, compiled on the host against a copy of the EXE's .text
    with the claims of the patches installed before it applied (the live image at music_alert::initialize)."""

    @classmethod
    def setUpClass(cls):
        cls.data = probe.EXE.read_bytes() if probe.EXE.is_file() else None
        cls.compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        cls.directory = tempfile.TemporaryDirectory()
        cls.harness = None
        if cls.compiler:
            source = Path(cls.directory.name) / 'install.cpp'
            source.write_text(INSTALL_HARNESS)
            cls.harness = Path(cls.directory.name) / 'install'
            cls.build = subprocess.run([cls.compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I',
                                        str(ROOT / 'src/proxy'), str(source), '-o', str(cls.harness)],
                                       capture_output=True, text=True, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def check(self, text, fail_va=None):
        if self.data is None:
            self.skipTest(f'{probe.EXE} absent')
        if not self.compiler:
            self.skipTest('no host C++ compiler')
        self.assertEqual(self.build.returncode, 0, self.build.stderr[-3000:])
        image = Path(self.directory.name) / 'text.bin'
        image.write_bytes(bytes(text))
        args = [str(self.harness), str(image), hex(TEXT_VA)] + ([hex(fail_va)] if fail_va else [])
        run = subprocess.run(args, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return run.stdout.strip()

    def text(self):
        return bytearray(self.data[TEXT_RAW:TEXT_RAW + probe.TEXT_SIZE])

    @staticmethod
    def claim(text, va, length):
        """The bytes a claim leaves: jmp rel32 to an arena address, the rest of the claimed span as 0xcc (the
        five-byte write plus a conservative cover of the whole spec length)."""
        if not TEXT_VA <= va < TEXT_VA + len(text):
            return False
        patch = b'\xe9' + struct.pack('<i', 0x041a0000 - (va + 5)) + b'\xcc' * max(0, length - 5)
        off = va - TEXT_VA
        text[off:off + length] = patch[:len(text) - off]
        return True

    def test_live_image_after_the_earlier_claims_matches(self):
        order = earlier_features()
        for name in ('media_cue', 'fov', 'ship_steering', 'collide_box_cull', 'collide_memo', 'music_keep', 'seta_restore'):
            self.assertIn(name, order)
        rank = {name: i for i, name in enumerate(order)}

        def owner(base):
            return rank.get(base, rank.get(base + 's', rank.get({'terran_lod': 'terran_station_lod'}.get(base, ''))))

        claims = collected_claims() + list(UNCOLLECTED_EARLIER)
        earlier = sorted((c for c in claims if owner(c[0]) is not None), key=lambda c: owner(c[0]))
        text = self.text()
        applied = {va for _, _, va, length in earlier if self.claim(text, va, length)}
        for va in (0x00498140, 0x004cfd46, 0x0041c9d9, 0x0044ccc0, 0x0044dd90, 0x00454b91, 0x0045d58e, 0x004e25a3,
                   0x004043a5, 0x0047f329, 0x004982db, 0x00498d71, 0x004b148c, 0x00403f2f):
            self.assertIn(va, applied, hex(va))
        self.assertGreaterEqual(len(applied), 100)
        self.assertEqual(self.check(text), 'match')
        # Every other collected claim too (the chase, sun-occlusion, cull and loading sites installed later).
        later = [c for c in claims if owner(c[0]) is None]
        self.assertGreaterEqual(len(later), 20)
        for _, _, va, length in later:
            self.claim(text, va, length)
        self.assertEqual(self.check(text), 'match')

    def test_unmodified_image_matches_and_long_windows_are_compared_whole(self):
        text = self.text()
        self.assertEqual(self.check(text), 'match')
        rows, _ = header_windows()
        long_rows = [(n, va, raw) for n, va, raw in rows if len(raw) > 16]
        self.assertEqual(len(long_rows), 14)  # engine_patch::verify_bytes refuses these (max_prologue = 16)
        self.assertEqual(long_rows[0][:2], ('sector_walk', 0x0042c8a9))
        self.assertEqual(long_rows[1][:2], ('range_window', 0x0042c915))  # the first the Run 189 build refused
        for name, va, raw in long_rows:
            with self.subTest(window=name):
                changed = self.text()
                changed[va - TEXT_VA + len(raw) - 1] ^= 0x01
                self.assertEqual(self.check(changed), 'mismatch name=%s va=0x%08x readable=1 expected=%s got=%s'
                                 % (name, va, raw[:8].hex(), raw[:8].hex()))

    def test_first_mismatch_is_named(self):
        text = self.text()
        self.claim(text, probe.RELATION_VA, 5)
        self.claim(text, probe.DISTANCE_VA, 5)
        relation = probe.window_bytes(dict((n, h) for n, _, h in probe.WINDOWS)['relation_body'])
        got = (b'\xe9' + struct.pack('<i', 0x041a0000 - (probe.RELATION_VA + 5)) + relation[5:8]).hex()
        self.assertEqual(self.check(text), 'mismatch name=relation_body va=0x00450890 readable=1 expected=%s got=%s'
                         % (relation[:8].hex(), got))
        site = self.text()
        site[probe.SITE_VA - TEXT_VA:probe.SITE_VA - TEXT_VA + 6] = b'\x90' * 6
        self.assertRegex(self.check(site), r'^mismatch name=site_window va=0x0042c965 ')
        caller = self.text()
        caller[probe.WALK_CALLER_VA - TEXT_VA + 1] ^= 0x10
        self.assertEqual(self.check(caller), 'mismatch name=walk_caller va=0x0041cdea readable=1 expected=e8a1fa0000 got=e8b1fa0000')
        self.assertEqual(self.check(self.text(), fail_va=probe.RELATION_VA + 4),
                         'mismatch name=relation_body va=0x00450890 readable=0 expected=%s got=0000000000000000' % relation[:8].hex())

    def test_installer_uses_the_core_compare_and_logs_the_window(self):
        text = MODULE.read_text()
        self.assertNotIn('verify_bytes(', text)
        self.assertIn('core::install_windows_match(', text)
        self.assertIn('engine_patch::read_code(at, out, n)', text)
        self.assertIn('window=%s window_name=%s expected=%s got=%s"', text)
        self.assertIn('mismatch_ = core::Mismatch{};', text)
        self.assertRegex((ROOT / 'src/proxy/engine_patch.h').read_text(), r'max_prologue = 16;')


class MusicAlertLaunch(unittest.TestCase):
    """--dry-run only; never a launch (subprocess.call is an assertion)."""

    @classmethod
    def setUpClass(cls):
        cls.module = load('music_alert_manage', ROOT / 'tools/manage.py')
        cls.directory = tempfile.TemporaryDirectory()
        game = Path(cls.directory.name) / 'game'
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        import hashlib
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        cls.game = game
        cls.wine = Path(cls.directory.name) / 'wine'
        cls.wine.touch()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def launch(self, *args, vanilla=False, inherited=None):
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(self.game), *args]
        environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
        environ.update(inherited or {})
        output, error = io.StringIO(), io.StringIO()
        module = self.module
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', self.wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, None, error.getvalue()
        return 0, json.loads(output.getvalue()), error.getvalue()

    def env(self, *args, **kw):
        code, data, error = self.launch(*args, **kw)
        self.assertEqual(code, 0, error)
        return data['env']

    def test_defaults_and_options(self):
        stale = {'X3M_MUSIC_COMBAT_TRIGGER': 'vanilla', 'X3M_MUSIC_COMBAT_HOLD_S': '5'}
        env = self.env(inherited=stale)
        self.assertEqual((env['X3M_MUSIC_COMBAT_TRIGGER'], env['X3M_MUSIC_COMBAT_HOLD_S']), ('engaged', '20'))
        env = self.env('--music-combat-trigger', 'vanilla', '--music-combat-hold-s', '45')
        self.assertEqual((env['X3M_MUSIC_COMBAT_TRIGGER'], env['X3M_MUSIC_COMBAT_HOLD_S']), ('vanilla', '45'))
        self.assertEqual(self.env('--music-combat-hold-s', '600')['X3M_MUSIC_COMBAT_HOLD_S'], '600')
        for args in (('--music-combat-hold-s', '0'), ('--music-combat-hold-s', '601'), ('--music-combat-hold-s', 'x'),
                     ('--music-combat-trigger', 'on')):
            with self.subTest(args=args):
                code, _, error = self.launch(*args)
                self.assertEqual(code, 2, error[-300:])
        # Player mode (--config): the defaults are the proxy's own; an explicit option is sent.
        self.assertFalse({'X3M_MUSIC_COMBAT_TRIGGER', 'X3M_MUSIC_COMBAT_HOLD_S'} & set(self.env('--config')))
        self.assertEqual(self.env('--config', '--music-combat-hold-s', '30').get('X3M_MUSIC_COMBAT_HOLD_S'), '30')
        self.assertEqual(self.env('--config', '--music-combat-trigger', 'vanilla').get('X3M_MUSIC_COMBAT_TRIGGER'), 'vanilla')

    def test_vanilla(self):
        env = self.env(vanilla=True, inherited={'X3M_MUSIC_COMBAT_TRIGGER': 'engaged', 'X3M_MUSIC_COMBAT_HOLD_S': '20'})
        self.assertFalse({'X3M_MUSIC_COMBAT_TRIGGER', 'X3M_MUSIC_COMBAT_HOLD_S'} & set(env))
        for args in (('--music-combat-trigger', 'engaged'), ('--music-combat-trigger', 'vanilla'), ('--music-combat-hold-s', '20')):
            with self.subTest(args=args):
                code, _, error = self.launch(*args, vanilla=True)
                self.assertEqual(code, 2)
                self.assertIn('cannot be combined with --vanilla', error)


if __name__ == '__main__':
    unittest.main()
