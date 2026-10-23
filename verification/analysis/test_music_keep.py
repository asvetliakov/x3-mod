"""Host checks of the sector-music keep and trace (src/proxy/music_keep*.{h,cpp}).

The writes verifier on the installed executable and its refusals on patched
copies (every window, the claim sites, the callers, an interior branch), the
source constants and caller tables, the decision logic compiled from the core
header on the host (stop classifier, seek rule, hold table), the assembly
stubs' shape, the line parsers, the production wiring and the --music-keep /
--music-trace launcher gates (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('verify_music_keep_writes', ROOT / 'verification/results/music-restart/verify_music_keep_writes.py')
CORE = ROOT / 'src/proxy/music_keep_core.h'
MODULE = ROOT / 'src/proxy/music_keep.cpp'

HARNESS = r'''
#include "music_keep_core.h"
#include <cstdio>
#include <cstring>
#include <map>
using namespace x3m::music_keep::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    // Caller table.
    check(stop_mode(0x004d36c2) == StopMode::skip_all && stop_mode(0x00404561) == StopMode::keep_running && stop_mode(0x00407069) == StopMode::keep_running, "preserve callers");
    check(!std::strcmp(mode_name(StopMode::skip_all), "skip_all") && unsigned(StopMode::skip_all) == 3 && unsigned(StopMode::keep_running) == 1, "mode values the A stub compares");
    check(stop_mode(0x00404cf2) == StopMode::vanilla && stop_mode(0x00497bbb) == StopMode::vanilla && stop_mode(0x0040387d) == StopMode::vanilla && stop_mode(0x12345678) == StopMode::vanilla, "vanilla callers");
    check(!std::strcmp(stop_caller_name(0x004d36c2), "alt_tab") && !std::strcmp(stop_caller_name(0), "unknown") && !std::strcmp(play_caller_name(0x00499824), "MOV_PlayMovie")
          && !std::strcmp(play_caller_name(0x00499987), "MOV_PlayMovieFrom") && !std::strcmp(stop_movie_caller_name(0x00499886), "MOV_StopMovie"), "names");
    // Patch A: a music record through save/pause is held and keeps running; through alt-tab it is left untouched (skip_all, not held); non-music never; vanilla drops holds.
    Holds holds;
    StopDecision s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    check(s.mode == StopMode::keep_running && s.held && holds.count == 1 && holds.slots[0].record == 0x1000 && holds.slots[0].id == 2004 && holds.slots[0].media == 0x2000, "save keeps running and holds");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00407069, true);
    check(s.mode == StopMode::keep_running && holds.count == 1, "pause: same record replaces its hold");
    s = decide_stop(holds, 0x1040, 0x12, 5, 0x2100, 0x00404561, true);
    check(s.mode == StopMode::vanilla && !s.held && holds.count == 1, "non-music record under save: vanilla, not held");
    s = decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x004d36c2, true);
    check(s.mode == StopMode::skip_all && s.held && holds.count == 2 && holds.slots[0].record == 0x1000 && holds.slots[1].mode == StopMode::skip_all,
          "alt-tab, DirectSound music record: skip_all, held with the skip_all kind, other holds untouched");
    s = decide_stop(holds, 0x1080, 0x92, 8404, 0x2200, 0x004d36c2, true);
    check(s.mode == StopMode::skip_all && s.held && holds.count == 2, "alt-tab, DirectShow music record: skip_all, same record replaces its hold");
    s = decide_stop(holds, 0x10c0, 0x52, 7, 0x2280, 0x004d36c2, true);
    check(s.mode == StopMode::vanilla && !s.held && holds.count == 2, "alt-tab, non-music record (speech, DirectSound): vanilla, holds untouched");
    s = decide_stop(holds, 0x10c0, 0x12, 7, 0x2280, 0x004d36c2, true);
    check(s.mode == StopMode::vanilla && !s.held, "alt-tab, non-music record: vanilla");
    s = decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x004d36c2, false);
    check(s.mode == StopMode::paused && s.held && holds.count == 2 && holds.slots[1].mode == StopMode::paused, "alt-tab without Patch D: the flown paused mode");
    s = decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x00404561, true);
    check(s.mode == StopMode::paused && s.held && holds.count == 2 && holds.slots[1].mode == StopMode::paused, "DirectSound save: paused and held");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404cf2, true);
    check(s.mode == StopMode::vanilla && !s.held && holds.count == 0, "load drops every hold");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00000000, true);
    check(s.mode == StopMode::vanilla && holds.count == 0, "unknown caller: vanilla");
    // Hold capacity: the oldest is replaced.
    for (unsigned i = 0; i < hold_capacity + 1; ++i) holds.add(Hold{0x3000 + i * 0x40, 100 + i, 0x4000 + i, StopMode::keep_running});
    check(holds.count == hold_capacity && holds.find(0x3000, 100, 0x4000) == nullptr && holds.find(0x3000 + hold_capacity * 0x40, 100 + hold_capacity, 0x4000 + hold_capacity) != nullptr, "hold capacity replaces the oldest");
    holds.clear();
    // Patch C. Live-list model: record -> (flags, context) or absent.
    std::map<std::uint32_t, LiveRecord> live;
    auto lookup = [&](std::uint32_t record, std::uint32_t, std::uint32_t, LiveRecord& out) { auto it = live.find(record); if (it == live.end()) return false; out = it->second; return true; };
    SeekDecision d = decide_seek(holds, 0x1040, 0x12, 5, 0x2100, 0, lookup);
    check(d.action == SeekAction::vanilla, "non-music: vanilla");
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::vanilla, "music, no hold, not playing (natural end): vanilla seek to 0");
    d = decide_seek(holds, 0x1000, 0x92, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::skip && d.hold_id == 2004, "music still playing, start 0: skip");
    d = decide_seek(holds, 0x1000, 0x92, 2004, 0x2000, 1500, lookup);
    check(d.action == SeekAction::vanilla, "positioned play (PlayMovieFrom) of the playing track: vanilla");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);   // save: keep-running hold, flag 2 then cleared by the engine
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::skip && d.hold_id == 2004 && holds.count == 0, "held after save, replayed at 0: skip and drop the holds");
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00404561, true);   // DirectSound save: paused hold
    d = decide_seek(holds, 0x1000, 0xd0, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::skip && holds.count == 0, "paused hold after a save, replayed at 0: skip (Run resumes)");
    // Alt-tab skip_all: a skip_all hold, ignored by the seek rule; the record keeps flag 2, so a replay at 0 still skips;
    // after its natural end (flag 2 cleared by the pump) the next start-0 play of the same record seeks to 0 as vanilla.
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, true);
    check(holds.count == 1 && holds.slots[0].mode == StopMode::skip_all, "alt-tab holds with the skip_all kind");
    check(decide_seek(holds, 0x1000, 0xd2, 2004, 0x2000, 0, lookup).action == SeekAction::skip, "after alt-tab, still playing, replayed at 0: skip");
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, true);
    check(decide_seek(holds, 0x1000, 0xd0, 2004, 0x2000, 0, lookup).action == SeekAction::vanilla, "after alt-tab and the natural end: vanilla seek to 0");
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00404561, true);
    d = decide_seek(holds, 0x1000, 0xd0, 2004, 0x2100, 0, lookup);
    check(d.action == SeekAction::vanilla && holds.count == 0, "same record, other media object: no match, vanilla, holds dropped");
    // Another id while a keep-running hold is still linked, unclaimed and not playing: pause it first.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    live[0x1000] = LiveRecord{0x90, 0};
    d = decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup);
    check(d.action == SeekAction::pause_then_vanilla && d.pause_record == 0x1000 && d.hold_id == 2004 && holds.count == 0, "other id with a running hold: pause it, then vanilla");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    live[0x1000] = LiveRecord{0x92, 0};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record playing again: no pause");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    live[0x1000] = LiveRecord{0x90, 0x77};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record owned by a script task again: no pause");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    live.erase(0x1000);
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record no longer linked: no pause");
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00407069, true);
    live[0x1000] = LiveRecord{0xd0, 0};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "paused (DirectSound pause) hold and another id: vanilla, already paused");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    d = decide_seek(holds, 0x1040, 0x12, 5, 0x2100, 0, lookup);
    check(d.action == SeekAction::vanilla && holds.count == 1, "a non-music play leaves the holds alone");
    holds.clear();
    // F1: a DirectSound-path music record (0x40) is paused, not kept running, under save and pause.
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00404561, true);
    check(s.mode == StopMode::paused && s.held && holds.slots[0].mode == StopMode::paused, "DirectSound-path record under save: paused and held");
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00407069, true);
    check(s.mode == StopMode::paused, "DirectSound-path record under pause: paused");
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, true);
    check(s.mode == StopMode::skip_all && s.held && holds.count == 1 && holds.slots[0].mode == StopMode::skip_all, "DirectSound-path record under alt-tab: skip_all");
    holds.clear();
    // Patch D: answer "playing" only for a skip_all-held music record still marked playing, engine inactive, RunInBackground clear.
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, true);
    check(decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 0, 0x70200107), "skip_all + inactive + bit clear (this bottle's InputFlags): playing");
    check(!decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 1, 0x70200107), "skip_all + active: forward");
    check(!decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 0, 0x70204107), "skip_all + inactive + RunInBackground set: forward (the engine's answer is genuine)");
    check(!decide_status(holds, 0x1000, 0xd0, 2004, 0x2000, 0, 0), "skip_all hold but flag 2 clear: forward");
    check(!decide_status(holds, 0x1000, 0xd2, 2004, 0x2100, 0, 0) && !decide_status(holds, 0x1040, 0xd2, 2004, 0x2000, 0, 0), "other media or record: forward");
    check(!decide_status(holds, 0x1000, 0x52, 2004, 0x2000, 0, 0), "non-music: forward");
    holds.clear();
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00404561, true);   // paused hold (DirectSound save)
    check(!decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 0, 0), "paused hold: forward");
    holds.clear();
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);   // keep_running hold
    check(!decide_status(holds, 0x1000, 0x92, 2004, 0x2000, 0, 0), "keep_running hold: forward");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404cf2, true);   // load: vanilla, drops every hold
    check(!decide_status(holds, 0x1000, 0x92, 2004, 0x2000, 0, 0), "vanilla (holds dropped): forward");
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, false);   // alt-tab without Patch D: paused hold
    check(!decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 0, 0), "alt-tab paused fallback: forward");
    holds.clear();
    // Every music play drops the skip_all hold, so the gate ends with the next track.
    decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2, true);
    decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup);
    check(holds.count == 0 && !decide_status(holds, 0x1000, 0xd2, 2004, 0x2000, 0, 0), "a music play drops the skip_all hold");
    check(d_site_va + call_length == d_return_va && d_caller_window[5] == 0xe8 && d_caller_va + 5 == d_site_va && status_head[13] == 0xdc && status_head[26] == 0xf7
          && status_head[29] == 0x40 && status_head[32] == 0x0f && status_head[33] == 0x84 && status_ended_window[2] == 0x02, "Patch D constants");
    // F2: at any stop-all, orphaned keep-running holds (linked, not playing, context 0) are paused one by one; others dropped; paused holds stay.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);   // keep_running, will be orphaned and live
    decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x00404561, true);   // paused (DirectSound save): stays
    decide_stop(holds, 0x1100, 0x92, 8509, 0x2300, 0x00407069, true);   // keep_running, replayed meanwhile (context set): dropped, not paused
    decide_stop(holds, 0x1180, 0x92, 8600, 0x2400, 0x00407069, true);   // keep_running, live and orphaned
    live[0x1000] = LiveRecord{0x90, 0}; live[0x1100] = LiveRecord{0x92, 0x55}; live[0x1180] = LiveRecord{0x90, 0};
    std::uint32_t first = stale_hold_to_pause(holds, lookup);
    std::uint32_t second = stale_hold_to_pause(holds, lookup);
    std::uint32_t third = stale_hold_to_pause(holds, lookup);
    check(first == 0x1000 && second == 0x1180 && third == 0 && holds.count == 1 && holds.slots[0].record == 0x1080 && holds.slots[0].mode == StopMode::paused,
          "orphaned keep-running holds paused in order, replayed one dropped, paused hold kept");
    holds.clear();
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    live.erase(0x1000);
    check(stale_hold_to_pause(holds, lookup) == 0 && holds.count == 0, "unlinked keep-running hold: dropped without a pause");
    check(stale_hold_to_pause(holds, lookup) == 0, "empty table: nothing");
    // F4: MOV_StopMovie drops the id's holds only.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561, true);
    decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x00404561, true);
    check(drop_holds_by_id(holds, 2004) == 1 && holds.count == 1 && holds.slots[0].id == 8404 && drop_holds_by_id(holds, 2004) == 0, "stop-movie drops the id's hold only");
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::vanilla, "after the script's own stop, the replay seeks to 0 as vanilla");
    check(list_walk_limit >= 1024 && flag_directsound == 0x40, "walk limit and flags");
    check(a_site_va < a_next_record_va && a_next_record_va < stop_all_end_va && a_not_playing_je_va + 2 + 0x7e == a_next_record_va && a_window[a_not_playing_je_va - a_window_va] == 0x74
          && a_window[a_not_playing_je_va - a_window_va + 1] == 0x7e && a_next_record_va + 9 - 0x92 == a_loop_head_va && a_next_record_window[3] == 0x0f && a_next_record_window[4] == 0x85, "skip_all target is the loop continue");
    // Shared entry sites: either feature may install first. Image model: the three trace heads; a fake engine_patch
    // (claim compares and writes e9 rel32, push chains, restore puts the original back only over its own patch).
    std::map<std::uint32_t, unsigned char> image;
    auto seed = [&](std::uint32_t at, const unsigned char* bytes, unsigned n) { for (unsigned i = 0; i < n; ++i) image[at + i] = bytes[i]; };
    seed(stop_all_va, stop_all_head, stop_all_head_length); seed(play_va, play_head, play_head_length); seed(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length);
    const std::map<std::uint32_t, unsigned char> pristine = image;
    auto read = [&](std::uint32_t at, unsigned char* out, unsigned n) { for (unsigned i = 0; i < n; ++i) { auto it = image.find(at + i); if (it == image.end()) return false; out[i] = it->second; } return true; };
    struct FakeSite { std::uint32_t address = 0; unsigned char original[16]{}, patched[5]{}; bool live = false; std::uint32_t head = 0; const char* status = "unclaimed"; };
    struct FakeSpec { std::uint32_t address; const unsigned char* expected; unsigned length; };
    struct FakeOps {
        std::map<std::uint32_t, unsigned char>* image; bool fail_push = false;
        bool claim(FakeSite& s, const FakeSpec& spec) {
            for (unsigned i = 0; i < spec.length; ++i) if ((*image)[spec.address + i] != spec.expected[i]) { s.status = "bytes_mismatch"; return false; }
            s.address = spec.address;
            for (unsigned i = 0; i < spec.length; ++i) s.original[i] = spec.expected[i];
            const std::uint32_t dispatcher = 0x0a100000 + (spec.address & 0xffff), rel = dispatcher - (spec.address + 5);
            s.patched[0] = 0xe9; for (unsigned i = 0; i < 4; ++i) s.patched[1 + i] = (unsigned char)(rel >> (8 * i));
            for (unsigned i = 0; i < 5; ++i) (*image)[spec.address + i] = s.patched[i];
            s.live = true; s.head = 0x0b000000 + (spec.address & 0xffff); s.status = "ok";   // head = the tail
            return true;
        }
        const char* status(const FakeSite& s) { return s.status; }
        bool push(FakeSite& s, std::uint32_t stub, std::uint32_t& continuation) { if (fail_push) return false; continuation = s.head; s.head = stub; return true; }
        bool restore(FakeSite& s) {
            if (!s.live) return true;
            for (unsigned i = 0; i < 5; ++i) if ((*image)[s.address + i] != s.patched[i]) return false;
            for (unsigned i = 0; i < 5; ++i) (*image)[s.address + i] = s.original[i];
            s.live = false; return true;
        }
        bool live(const FakeSite& s) { return s.live; }
    };
    FakeOps ops{&image};
    SharedSite<FakeSite> entry, stop_movie;
    auto claims_of = [&](LiveClaim* out) {
        unsigned n = 0;
        for (auto* s : {&entry, &stop_movie}) if (s->site.live) out[n++] = LiveClaim{s->site.address, s->site.original, s->site.patched, 5};
        return n;
    };
    auto matches = [&](std::uint32_t at, const unsigned char* expected, unsigned n) { LiveClaim c[2]{}; const unsigned count = claims_of(c); return window_matches(read, at, expected, n, c, count); };
    const FakeSpec entry_spec{stop_all_va, stop_all_head, t_stop_all_length}, stop_movie_spec{t_stop_movie_site_va, stop_movie_head, t_stop_movie_length};
    std::uint32_t keep_entry_cont = 0, keep_sm_cont = 0, trace_entry_cont = 0, trace_sm_cont = 0;
    const std::uint32_t keep_entry_stub = 0x0c000001, keep_sm_stub = 0x0c000002, trace_entry_stub = 0x0c000011, trace_sm_stub = 0x0c000012;
    // The two installers' shared parts, in their production order (keep: entry then stop-movie; trace: stop-movie then entry), with rollback.
    auto install_keep = [&](const char** reason) {
        if (!matches(stop_all_va, stop_all_head, stop_all_head_length) || !matches(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length)) { *reason = "bytes_mismatch"; return false; }
        if (!acquire_shared(ops, entry, entry_spec, keep_entry_stub, keep_entry_cont, reason)) return false;
        if (!acquire_shared(ops, stop_movie, stop_movie_spec, keep_sm_stub, keep_sm_cont, reason)) { release_shared(ops, entry); return false; }
        return true;
    };
    auto install_trace = [&](const char** reason) {
        if (!matches(stop_all_va, stop_all_head, stop_all_head_length) || !matches(play_va, play_head, play_head_length)
            || !matches(t_stop_movie_site_va, stop_movie_head, stop_movie_head_length)) { *reason = "bytes_mismatch"; return false; }
        if (!acquire_shared(ops, stop_movie, stop_movie_spec, trace_sm_stub, trace_sm_cont, reason)) return false;
        if (!acquire_shared(ops, entry, entry_spec, trace_entry_stub, trace_entry_cont, reason)) { release_shared(ops, stop_movie); return false; }
        return true;
    };
    const std::uint32_t entry_tail = 0x0b000000 + (stop_all_va & 0xffff), sm_tail = 0x0b000000 + (t_stop_movie_site_va & 0xffff);
    for (int order = 0; order < 2; ++order) {
        image = pristine; entry = {}; stop_movie = {}; ops.fail_push = false;
        const char* r1 = "unset"; const char* r2 = "unset";
        const bool first = order == 0 ? install_keep(&r1) : install_trace(&r1);
        check(first && entry.users == 1 && stop_movie.users == 1 && image != pristine, order == 0 ? "keep first: installs" : "trace first: installs");
        LiveClaim none[1]{};
        check(!window_matches(read, stop_all_va, stop_all_head, stop_all_head_length, none, 0), "a plain read after the first install sees the claim (the run271 bytes_mismatch)");
        const bool second = order == 0 ? install_trace(&r2) : install_keep(&r2);
        check(second && entry.users == 2 && stop_movie.users == 2, order == 0 ? "keep first: trace installs beside it" : "trace first: keep installs beside it");
        if (order == 0)
            check(entry.site.head == trace_entry_stub && trace_entry_cont == keep_entry_stub && keep_entry_cont == entry_tail && stop_movie.site.head == trace_sm_stub
                  && trace_sm_cont == keep_sm_stub && keep_sm_cont == sm_tail, "keep first: trace chained in front of keep, keep in front of the tail");
        else
            check(entry.site.head == keep_entry_stub && keep_entry_cont == trace_entry_stub && trace_entry_cont == entry_tail && stop_movie.site.head == keep_sm_stub
                  && keep_sm_cont == trace_sm_stub && trace_sm_cont == sm_tail, "trace first: keep chained in front of trace, trace in front of the tail");
        // Release in either order: the bytes come back only with the last user.
        for (int release = 0; release < 2; ++release) {
            check(release_shared(ops, entry) && release_shared(ops, stop_movie), "release");
            check(release == 0 ? (image != pristine && entry.site.live && stop_movie.site.live) : (image == pristine && !entry.site.live && !stop_movie.site.live),
                  release == 0 ? "first release keeps the claims" : "last release restores the original bytes");
        }
    }
    // Fail closed: a foreign write over a live claim, or a changed byte behind it, refuses the second feature.
    image = pristine; entry = {}; stop_movie = {};
    const char* reason = "unset";
    check(install_keep(&reason), "keep installs");
    image[stop_all_va + 1] ^= 0xff;
    check(!install_trace(&reason) && !std::strcmp(reason, "bytes_mismatch") && entry.users == 1, "foreign byte in the claimed span: trace refused, keep's claim kept");
    image[stop_all_va + 1] ^= 0xff;
    image[stop_all_va + 6] ^= 0xff;
    check(!install_trace(&reason) && !std::strcmp(reason, "bytes_mismatch"), "changed byte behind the claim: trace refused");
    image[stop_all_va + 6] ^= 0xff;
    {   // overlay_claims: a foreign claim reports false but does not stop the other claims from being overlaid.
        const unsigned char orig_a[5] = {1, 2, 3, 4, 5}, patch_a[5] = {0xe9, 9, 9, 9, 9}, orig_b[5] = {6, 7, 8, 9, 10}, patch_b[5] = {0xe9, 8, 8, 8, 8};
        unsigned char span[10] = {0xe9, 9, 9, 9, 0x77, 0xe9, 8, 8, 8, 8};   // claim A overwritten at byte 4, claim B intact
        const LiveClaim two[2] = {{0x100, orig_a, patch_a, 5}, {0x105, orig_b, patch_b, 5}};
        check(!overlay_claims(0x100, span, 10, two, 2) && span[4] == 0x77 && span[0] == 0xe9 && span[5] == 6 && span[9] == 10, "foreign claim: false, left as read; the other claim still overlaid");
        unsigned char clean[10] = {0xe9, 9, 9, 9, 9, 0xe9, 8, 8, 8, 8};
        check(overlay_claims(0x100, clean, 10, two, 2) && clean[0] == 1 && clean[4] == 5 && clean[5] == 6 && clean[9] == 10, "two own claims: both overlaid");
    }
    // A second feature whose push fails leaves the first's claim and chain as they were; a push that fails on a fresh claim restores it.
    ops.fail_push = true;
    check(!install_trace(&reason) && !std::strcmp(reason, "chain_failed") && entry.users == 1 && stop_movie.users == 1 && entry.site.live && entry.site.head == keep_entry_stub, "second feature's failed push: first untouched");
    ops.fail_push = false;
    check(release_shared(ops, entry) && release_shared(ops, stop_movie) && image == pristine, "keep released alone: original bytes");
    ops.fail_push = true;
    check(!install_trace(&reason) && !std::strcmp(reason, "chain_failed") && stop_movie.users == 0 && !stop_movie.site.live && image == pristine, "failed push on a fresh claim: restored");
    ops.fail_push = false;
    {   // A live claim with no users (an unrestored rollback) is refused, never reset and forgotten; release restores it.
        SharedSite<FakeSite> orphan;
        std::uint32_t cont = 0; const char* why = "unset";
        check(acquire_shared(ops, orphan, entry_spec, keep_entry_stub, cont, &why) && orphan.users == 1, "orphan setup");
        orphan.users = 0;   // as a failed rollback leaves it
        check(!acquire_shared(ops, orphan, entry_spec, trace_entry_stub, cont, &why) && !std::strcmp(why, "site_live") && orphan.site.live, "live site without users: refused, kept");
        check(release_shared(ops, orphan) && !orphan.site.live && image == pristine, "release restores the orphaned claim");
    }
    // Music through the save. Patch A: save + DirectSound music + a free, bound pump -> keep_running with the pump armed;
    // pause, a busy or unbound pump, or DirectShow keep their former answers.
    holds.clear();
    s = decide_stop(holds, 0x1000, 0xd2, 8404, 0x2000, save_return_va, true, true);
    check(s.mode == StopMode::keep_running && s.held && s.pump && holds.count == 1 && holds.slots[0].mode == StopMode::keep_running, "save + DirectSound + pump free: keep_running, pump armed");
    s = decide_stop(holds, 0x1000, 0xd2, 8404, 0x2000, 0x00407069, true, true);
    check(s.mode == StopMode::paused && !s.pump, "pause + DirectSound: paused even with the pump free");
    s = decide_stop(holds, 0x1000, 0xd2, 8404, 0x2000, save_return_va, true, false);
    check(s.mode == StopMode::paused && !s.pump, "save + DirectSound, pump unbound or busy: paused");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, save_return_va, true, true);
    check(s.mode == StopMode::keep_running && !s.pump, "save + DirectShow: keep_running, no pump");
    s = decide_stop(holds, 0x1040, 0x52, 7, 0x2100, save_return_va, true, true);
    check(s.mode == StopMode::vanilla && !s.pump, "save + DirectSound non-music: vanilla, no pump");
    s = decide_stop(holds, 0x1000, 0xd2, 8404, 0x2000, 0x004d36c2, true, true);
    check(s.mode == StopMode::skip_all && !s.pump, "alt-tab unchanged with the pump free");
    check(stop_caller(save_return_va) && !std::strcmp(stop_caller(save_return_va)->name, "save"), "save_return_va is the save row");
    holds.clear();
    // The episode: arm, the clock stride, the 250 ms cadence, the thread, the checks, the returns.
    const std::uint64_t f = 10000000;   // 10 MHz QPC
    SavePump pump;
    check(!pump.active(), "idle pump");
    pump.arm(0x1000, 8404, 0x2000, 77, 1000, f);
    check(pump.active() && pump.pumping && pump.kept && !pump.run_marker && pump.interval == f / 4 && pump.bound == f * 120, "arm");
    check(pump.sample_due(), "first gzwrite samples the clock");
    check(pump.pump_due(1000 + 10, 77), "first sample pumps");
    check(!pump.pump_due(1000 + 10, 78), "other thread: no pump");
    pump.note_call(1, 1010, 1020);
    check(pump.calls == 1 && pump.returns[1] == 1 && pump.pumping && pump.first_call_at == 1010, "return 1 keeps pumping");
    unsigned sampled = 0;
    for (unsigned i = 0; i < pump_poll_stride * 3; ++i) sampled += pump.sample_due() ? 1u : 0u;
    check(sampled == 3, "the clock is read every pump_poll_stride calls");
    check(!pump.pump_due(1020 + f / 4 - 1, 77) && pump.pump_due(1020 + f / 4, 77), "at most one call per 250 ms");
    {   // A 3 s save writing every 10 us: 12 or 13 calls, no gap above 250 ms + one stride of writes.
        SavePump q;
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        std::uint64_t t = 0;
        for (unsigned i = 0; i < 300000; ++i, t += 100) {
            if (!q.sample_due()) continue;
            if (q.pump_due(t, 1)) q.note_call(1, t, t + 50);
        }
        q.stop(PumpEnd::gzclose, t);
        check(q.calls >= 12 && q.calls <= 13 && q.returns[1] == q.calls, "cadence over a 3 s save");
        check(q.max_gap <= f / 4 + pump_poll_stride * 100 + 50 && q.writes == 300000, "largest unpumped span bounded by the cadence");
        check(!q.pumping && q.kept && q.pump_end == PumpEnd::gzclose, "gzclose stops pumping, the Run skip stays eligible");
        check(!q.pump_due(t + f, 1), "no pump after gzclose");
    }
    {   // 0 and 2 stop pumping and the Run skip; a refused check too; the wall bound.
        SavePump q;
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(0, 10, 20);
        check(!q.pumping && !q.kept && q.pump_end == PumpEnd::failed && q.returns[0] == 1, "return 0: failed, no Run skip");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(2, 10, 20);
        check(!q.pumping && !q.kept && q.pump_end == PumpEnd::ended && q.returns[2] == 1, "return 2: ended, no Run skip");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(7, 10, 20);
        check(!q.pumping && q.returns[3] == 1 && q.pump_end == PumpEnd::failed, "unexpected return: failed");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.refuse(5);
        check(!q.pumping && !q.kept && q.pump_end == PumpEnd::refused && q.active(), "refused check: no call, no Run skip, the episode stays open for its line");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(!q.pump_due(f * 120, 1) && !q.pumping && !q.kept && q.pump_end == PumpEnd::bound && q.fallback_due() && q.fallback_reason == PumpEnd::bound,
              "wall bound stops pumping, cancels the Run skip and asks for the pause");
    }
    {   // Disarm at plays: the replay of the kept record (seek skipped) marks the Run; Patch E skips it once, only with
        // the buffer playing; any other play ends the episode at once.
        SavePump q;
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.stop(PumpEnd::gzclose, 100);
        check(!q.on_play(0x1000, 8404, 0x2000, true, 200) && q.run_marker && !q.pumping && !q.kept && q.end == PumpEnd::replay, "replay: Run marker");
        check(q.skip_run(0x1000, 0x2000, buffer_playing, 200) && !q.run_marker && !q.active(), "Patch E skips the marked Run");
        check(!q.skip_run(0x1000, 0x2000, buffer_playing, 200), "the marker is one shot");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(!q.on_play(0x1000, 8404, 0x2000, true, 200) && !q.skip_run(0x1000, 0x2000, buffer_draining, 200) && !q.active(), "draining buffer: Run vanilla, marker consumed");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(!q.on_play(0x1000, 8404, 0x2000, true, 200) && !q.skip_run(0x1080, 0x2000, buffer_playing, 200) && !q.active(), "another record's Run: vanilla");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(q.on_play(0x1080, 80412, 0x2200, false, 200) && !q.active() && q.end == PumpEnd::play && q.pump_end == PumpEnd::play, "another id plays while pumping: episode ends, pump stops");
        check(!q.skip_run(0x1000, 0x2000, buffer_playing, 200), "no Run skip after another play");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(q.on_play(0x1000, 8404, 0x2000, false, 200) && !q.run_marker, "same record not skipped (seeked): episode ends, no marker");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(0, 10, 20);
        check(q.on_play(0x1000, 8404, 0x2000, true, 200) && !q.run_marker, "failed pump: the replay does not mark the Run");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(q.on_play(0x1000, 8404, 0x2100, true, 200) && !q.run_marker, "other media object: no marker");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(q.end_now(PumpEnd::stop_movie, 50) && !q.active() && q.pump_end == PumpEnd::stop_movie && q.end == PumpEnd::stop_movie, "MOV_StopMovie of the id ends the episode");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        check(q.end_now(PumpEnd::stop_all, 50) && !q.active() && !q.end_now(PumpEnd::stop_all, 60), "the next stop-all ends it, once");
        SavePump idle;
        check(!idle.on_play(0x1000, 8404, 0x2000, true, 1) && !idle.skip_run(0x1000, 0x2000, buffer_playing, 200) && !idle.end_now(PumpEnd::stop_all, 1), "idle: nothing");
        check(ticks_to_ms(f * 3, f) == 3000 && ticks_to_ms(5, 0) == 0, "ticks to ms");
    }
    {   // Review fixes: every early stop cancels the Run skip and asks for the fallback pause (once); gzclose does not; an
        // overdue replay (2.5 s after the last pump, armed thread) and a stale Run at Patch E do.
        SavePump q;
        const std::uint64_t lead = f * run_skip_bound_ms / 1000;
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.refuse(5);
        check(!q.kept && q.fallback_due() && q.fallback_reason == PumpEnd::refused && q.fallback_at == 5, "refusal: pause due");
        q.note_fallback(true);
        check(q.fallback == Fallback::paused && !q.fallback_due(), "fallback recorded as paused");
        q.cancel_skip(PumpEnd::bound, 9);
        check(q.fallback == Fallback::paused && q.fallback_reason == PumpEnd::refused, "the fallback is taken once per episode");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(0, 10, 20);
        check(q.fallback_due() && q.fallback_reason == PumpEnd::failed, "return 0: pause due");
        q.note_fallback(false);
        check(q.fallback == Fallback::unlinked, "nothing to pause: unlinked");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(2, 10, 20);
        check(q.fallback_due() && q.fallback_reason == PumpEnd::ended, "return 2: pause due");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.stop(PumpEnd::gzclose, 100);
        check(q.kept && !q.fallback_due() && q.fallback == Fallback::none, "the save's gzclose keeps the Run skip, no pause");
        check(!q.replay_overdue(lead - 1, 1) && q.replay_overdue(lead, 1) && !q.replay_overdue(lead, 2), "replay overdue 2.5 s after the last pump, armed thread only");
        q.cancel_skip(PumpEnd::overdue, lead);
        check(!q.kept && q.fallback_due() && q.fallback_reason == PumpEnd::overdue && q.active() && !q.replay_overdue(lead + 1, 1), "overdue: pause due, the episode stays open");
        check(q.on_play(0x1000, 8404, 0x2000, true, lead + 2) && !q.run_marker, "the late replay after the fallback: no Run marker (vanilla Run resumes)");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(1, 10, 20);
        q.stop(PumpEnd::gzclose, 30);
        check(!q.on_play(0x1000, 8404, 0x2000, true, 40) && !q.skip_run(0x1000, 0x2000, buffer_playing, 20 + lead) && q.fallback_reason == PumpEnd::stale && !q.active(),
              "Patch E: last pump older than 2.5 s, Run vanilla (stale)");
        q.arm(0x1000, 8404, 0x2000, 1, 0, f);
        q.note_call(1, 10, 20);
        q.stop(PumpEnd::gzclose, 30);
        check(!q.on_play(0x1000, 8404, 0x2000, true, 40) && q.skip_run(0x1000, 0x2000, buffer_playing, 20 + lead - 1), "Patch E: a recent pump, Run skipped");
        check(!q.replay_overdue(0, 1), "idle: never overdue");
        check(run_skip_bound_ms < 3000 && pump_interval_ms < run_skip_bound_ms, "the Run-skip bound sits under the 3 s minimum lead");
        Holds h;
        h.add(Hold{0x1000, 8404, 0x2000, StopMode::keep_running});
        check(h.set_mode(0x1000, 8404, 0x2000, StopMode::paused) && h.slots[0].mode == StopMode::paused && !h.set_mode(0x1000, 8404, 0x2100, StopMode::paused),
              "the fallback turns the hold into a paused one");
        auto absent = [&](std::uint32_t, std::uint32_t, std::uint32_t, LiveRecord&) { return false; };
        check(decide_seek(h, 0x1000, 0xd0, 8404, 0x2000, 0, absent).action == SeekAction::skip, "the replay after the fallback pause still skips the seek (Run resumes)");
        check(save_gzclose_call_va + call_length == save_gzclose_return_va && gzclose_wrapper[31] == 0xe8 && gzclose_wrapper_va + 36 == gzclose_wrapper_return_va
              && gzclose_thunk[2] == (gzclose_iat_va & 0xff), "the save's gzclose path constants");
        check(trace_cells[0] == 6 && trace_cells[3] == 12 && task_ip == 0x1c && task_index == 0x18 && cell_size == 5, "trace cell constants");
    }
    // Resume after combat.
    check(!combat_id(1000) && combat_id(1001) && combat_id(1042) && !combat_id(1043) && !combat_id(8404), "combat ids 1001..1042");
    check(seek_resume_bit == 4 && (seek_resume_bit & (unsigned(SeekAction::skip) | unsigned(SeekAction::pause_then_vanilla))) == 0, "resume bit distinct from the seek actions");
    {
        ResumeMemory m;
        std::uint32_t ms = 0;
        const std::uint32_t native = stop_movie_native_return_va;
        check(resume_on_stop(m, native, 8404, 0xd2, true, 63000) == ResumeAction::remember && m.valid && m.id == 8404 && m.ms == 63000, "sector stop: remember");
        check(resume_on_play(m, 1017, 0, SeekAction::vanilla, ms) == ResumeAction::keep && m.valid, "combat play: keep");
        check(resume_on_stop(m, native, 1017, 0xd2, true, 9000) == ResumeAction::none && m.id == 8404 && m.ms == 63000, "combat stop: memory untouched");
        check(resume_on_play(m, 1033, 0, SeekAction::vanilla, ms) == ResumeAction::keep, "another combat track: keep");
        check(resume_on_play(m, 8404, 0, SeekAction::vanilla, ms) == ResumeAction::seek && ms == 63000 && !m.valid, "post-combat sector play at 0: seek to the position, memory used");
        ms = 0;
        check(resume_on_play(m, 8404, 0, SeekAction::vanilla, ms) == ResumeAction::none && ms == 0, "one use: the next play seeks to 0");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_play(m, 80412, 0, SeekAction::vanilla, ms) == ResumeAction::clear_sector && !m.valid, "another sector id: cleared");
        check(resume_on_play(m, 8404, 0, SeekAction::vanilla, ms) == ResumeAction::none, "after the sector change nothing seeks");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        resume_on_play(m, 1017, 0, SeekAction::vanilla, ms);
        check(resume_on_play(m, 2003, 0, SeekAction::vanilla, ms) == ResumeAction::clear_sector && !m.valid, "combat, then a dock id: cleared");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_play(m, 8404, 0, SeekAction::skip, ms) == ResumeAction::clear_played && !m.valid, "same id still playing (seek skipped): cleared, no seek");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_play(m, 8404, 1500, SeekAction::vanilla, ms) == ResumeAction::clear_played && !m.valid, "positioned play of the id: cleared");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_play(m, 8404, 0, SeekAction::pause_then_vanilla, ms) == ResumeAction::seek && ms == 63000, "with a pause of another record: still seeks");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_stop_all(m, 0x00404cf2) == ResumeAction::clear_stop_all && !m.valid, "load clears");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_stop_all(m, 0x0040387d) == ResumeAction::clear_stop_all && !m.valid, "session start clears");
        resume_on_stop(m, native, 8404, 0xd2, true, 63000);
        check(resume_on_stop_all(m, save_return_va) == ResumeAction::none && resume_on_stop_all(m, 0x00407069) == ResumeAction::none
              && resume_on_stop_all(m, 0x004d36c2) == ResumeAction::none && m.valid, "save, pause and alt-tab keep the memory");
        check(resume_on_stop(m, 0x0045c282, 80412, 0xd2, true, 5000) == ResumeAction::none && m.id == 8404, "another MOV_StopMovie caller: ignored");
        check(resume_on_stop(m, native, 80412, 0x52, true, 5000) == ResumeAction::none && m.id == 8404, "non-music record: ignored");
        check(resume_on_stop(m, native, 80412, 0xd0, true, 5000) == ResumeAction::none && m.id == 8404, "record not playing: ignored");
        check(resume_on_stop(m, native, 80412, 0xd2, false, 0) == ResumeAction::forget && !m.valid, "sector stop without a readable position: the old memory goes");
        check(resume_on_stop(m, native, 80412, 0xd2, true, 0) == ResumeAction::forget && resume_on_stop(m, native, 80412, 0xd2, true, position_limit_ms) == ResumeAction::forget, "zero or out-of-range position: forget");
        check(resume_on_play(m, 80412, 0, SeekAction::vanilla, ms) == ResumeAction::none, "no memory: nothing");
        check(!std::strcmp(resume_action_name(ResumeAction::seek), "seek") && !std::strcmp(pump_end_name(PumpEnd::gzclose), "gzclose"), "names");
    }
    check(e_site_va == c_run_call_va && e_target_va == run_va && e_site_va + call_length == e_return_va && (e_site_va & 7) + call_length <= 8
          && e_post_window[0] == 0x85 && c_post_window[c_post_length - call_length] == 0xe8 && c_post_va + c_post_length == e_return_va, "Patch E constants");
    check(pump_head[14] == 0x39 && pump_head[16] == media_buffer_state && status_ds_tail[2] == media_flags && status_ds_tail[7] == 0x08 && status_ds_tail[11] == media_ds_value
          && std::uint32_t(status_ds_tail_va + 28u + (status_ds_tail[24] | status_ds_tail[25] << 8 | status_ds_tail[26] << 16 | std::uint32_t(status_ds_tail[27]) << 24)) == pump_va
          && status_ds_tail[23] == 0xe9 && gzwrite_thunk[2] == (gzwrite_iat_va & 0xff) && gzwrite_thunk[3] == ((gzwrite_iat_va >> 8) & 0xff), "pump and IAT constants");
    check(a_site_va + a_site_length == a_next_va && c_site_va + call_length == c_return_va && a_window_va + a_site_offset == a_site_va && stop_caller_count == 6 && play_caller_count == 3 && stop_movie_caller_count == 3, "address relations");
    std::printf("music_keep_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    return load('music_keep_manage', ROOT / 'tools/manage.py')


def patched_report(data, changes):
    image = bytearray(data)
    for va, raw in changes:
        offset = va - 0x401000 + 0x400
        image[offset:offset + len(raw)] = raw
    return probe.inspect(bytes(image), source_text(CORE))


class MusicSites(unittest.TestCase):
    @unittest.skipUnless(probe.EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['failures'])
        self.assertEqual(report['check_count'], 69)
        self.assertEqual(report['gzclose_import_slot'], '0x5323f0')
        self.assertEqual(report['written']['music_keep_e']['writes_example_thunk_0x0a400000'][:2], 'e8')
        self.assertEqual(report['written']['music_keep_e']['qword_offset'], 1)
        self.assertEqual(report['raw_branch_into_site_interior']['0x498d71'], ['0x498d4e'])   # the rejected operand byte of section 7.5
        self.assertEqual(report['pump_direct_entries'], ['0x4d17cc'])
        self.assertEqual((report['gzwrite_import_slot'], report['gzwrite_thunks'], report['pump_literal_refs']), ('0x5323e0', 1, 0))
        self.assertEqual(report['written']['music_keep_d']['writes_example_thunk_0x0a300000'][:2], 'e8')
        self.assertEqual(report['direct_callers']['0x4d14e0'], ['0x4983d9'])
        self.assertEqual(sorted(report['direct_callers']['0x4982b0']), sorted(hex(a) for a, _, _, _ in probe.STOP_CALLERS))
        self.assertEqual(report['written']['music_keep_a']['instructions'], [3, 3])
        self.assertEqual(report['written']['music_trace_play']['instructions'], [1, 1, 4])
        self.assertEqual(report['written']['music_keep_a']['writes_example_dispatcher_0x0a100000'][:2], 'e9')
        self.assertEqual(report['written']['music_keep_a']['writes_example_dispatcher_0x0a100000'][-2:], '04')   # the sixth site byte stays
        self.assertEqual(report['written']['music_keep_c']['writes_example_thunk_0x0a200000'][:2], 'e8')
        self.assertEqual(report['raw_branch_into_site_interior']['0x498c90'], ['0x498c6f'])   # the rejected operand-byte hit

    @unittest.skipUnless(probe.EXE.is_file(), 'installed executable not present')
    def test_changed_bytes_refused(self):
        data = probe.EXE.read_bytes()
        rel = lambda at, target: b'\xe9' + struct.pack('<i', target - (at + 5)) + b'\x90'
        cases = {
            'window_a_window': (0x4982c0, b'\x53'),                     # push ebx instead of push ebp: the [esp+0x10] depth would change
            'claim_music_keep_a': (0x4982db + 1, b'\x7f'),              # mov edi,[edi+0x24]
            'window_a_skip_window': (0x498322 + 6, b'\x81'),
            'window_a_next_record_window': (0x498359 + 5, b'\x6f'),     # jne into 0x4982d1
            'a_next_record_is_loop_continue': (0x4982d9 + 1, b'\x7d'),  # the not-playing je lands on 0x498358
            'window_c_post_window': (0x498d59, b'\x83\xc4\x08'),         # caller pops 8
            'call_music_keep_c': (0x498d54 + 1, b'\x00'),
            'window_pause_body': (0x4d1810 + 3, b'\x28'),                # [eax+0x28]
            'window_seek_head': (0x4d0430, b'\x53'),
            'window_run_head': (0x4d1870 + 2, b'\x60'),
            'window_stop_all_head': (0x4982b0 + 1, b'\x48'),
            'claim_music_trace_stop_all': (0x4982b0 + 1, b'\x48'),
            'window_play_head': (0x498c90 + 5, b'\x18'),
            'claim_music_trace_play': (0x498c90 + 5, b'\x18'),
            'window_stop_movie_head': (0x498810 + 2, b'\x48'),
            'stop_callers': (0x4d36bd + 1, b'\xef'),                     # the alt-tab call targets 0x4982b1
            'play_callers': (0x49981f + 1, b'\x6d'),
            'stop_movie_callers': (0x499881 + 1, b'\x8b'),
            'stop_movie_calls_pause': (0x49885f + 1, b'\xad'),
            'interior_4982db': (0x498400, rel(0x498400, 0x4982de)),
            'interior_4982b0': (0x498400, rel(0x498400, 0x4982b2)),
            'interior_498d54': (0x498400, rel(0x498400, 0x498d56)),
            'interior_498810': (0x498400, rel(0x498400, 0x498813)),
            'callers_4982b0': (0x498400, b'\xe8' + struct.pack('<i', 0x4982b0 - 0x498405)),
            'stop_all_callers_are_the_table': (0x498400, b'\xe8' + struct.pack('<i', 0x4982b0 - 0x498405)),
            'callers_4d1810': (0x498400, b'\xe8' + struct.pack('<i', 0x4d1810 - 0x498405)),
            'window_d_caller_window': (0x4983d4 + 15, b'\x02'),             # cmp ax,2 where the caller tests for 1
            'window_status_head': (0x4d14e0 + 29, b'\x41'),
            'status_inactive_arm': (0x4d14e0 + 29, b'\x41'),                 # a different input-flags bit
            'window_status_ended_window': (0x4d15a0 + 2, b'\x01'),          # mov ax,1
            'call_music_keep_d': (0x4983d9 + 1, b'\x00'),
            'interior_4983d9': (0x498400, rel(0x498400, 0x4983db)),
            'callers_4d14e0': (0x498400, b'\xe8' + struct.pack('<i', 0x4d14e0 - 0x498405)),
            'sha256': (0x498400, b'\x90'),
            'window_pump_head': (0x4d0700 + 15, b'\x7e\x4c'),            # cmp [esi+0x4c]
            'pump_contract': (0x4d0700 + 15, b'\x7e\x4c'),
            'window_pump_end': (0x4d0c0c + 29, b'\xb8\x01'),             # mov ax,1 on the drain exit
            'window_status_ds_tail': (0x4d17b5 + 11, b'\x20'),           # test al,0x20
            'pump_ds_gate': (0x4d17b5 + 11, b'\x20'),
            'window_e_post_window': (0x498d76 + 7, b'\x31'),             # xor ecx,ecx spelled differently
            'call_music_keep_e': (0x498d71 + 1, b'\x00'),
            'interior_498d71': (0x498400, rel(0x498400, 0x498d73)),
            'pump_only_entry_is_status_ds_tail': (0x498400, b'\xe8' + struct.pack('<i', 0x4d0700 - 0x498405)),
            'pump_no_literal_refs': (0x498400, struct.pack('<I', 0x4d0700)),
            'window_gzwrite_thunk': (0x4fae46 + 2, b'\xe4'),             # jmp [0x5323e4]
            'gzwrite_iat_slot': (0x4fae46 + 2, b'\xe4'),
            'window_position_reader': (0x4d0600 + 27, b'\x28'),          # vtbl +0x28
            'position_reader_contract': (0x4d0600 + 27, b'\x28'),
            'window_gzclose_wrapper': (0x4e9360 + 32, b'\xcf'),           # the wrapper's call lands one byte later
            'save_gzclose_path': (0x4e9360 + 32, b'\xcf'),
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = patched_report(data, [change])
                self.assertFalse(report['checks'][failed], failed)
                self.assertEqual(report['result'], 'FAIL')

    def test_source_constants_and_tables(self):
        consts = probe.source_constants(source_text(CORE))
        for name, value in probe.EXPECTED.items():
            self.assertEqual(consts.get(name), value, name)
        stops, plays = probe.source_callers(source_text(CORE))
        self.assertEqual(stops, probe.STOP_CALLERS)
        self.assertEqual(plays, probe.PLAY_CALLERS + probe.STOP_MOVIE_CALLERS)
        self.assertEqual([m for _, _, _, m in stops], ['skip_all', 'keep_running', 'keep_running', 'vanilla', 'vanilla', 'vanilla'])
        windows = probe.source_windows(source_text(CORE))
        self.assertEqual(windows['a_next_record_window'][:9], bytes.fromhex('395d000f856effffff'))   # cmp [ebp],ebx; jne 0x4982d0
        self.assertEqual(windows['a_window'][27:33], bytes.fromhex('8b7e24395f04'))
        self.assertEqual(windows['a_window'][:1] + windows['a_window'][12:14], bytes.fromhex('555657'))   # the pushes behind the [esp+0x10] depth (push ebx is in stop_all_head)
        self.assertEqual(windows['stop_all_head'][:5], bytes.fromhex('a1446f6000'))
        self.assertEqual(windows['play_head'][:6], bytes.fromhex('51538b5c2414'))
        self.assertEqual(windows['stop_movie_head'][:6], bytes.fromhex('8b0d446f6000'))
        self.assertEqual(len(windows['pause_body']), 0x57)
        self.assertEqual(probe.decode_span(bytes.fromhex('8b7e24395f04')), [3, 3])
        self.assertIsNone(probe.decode_span(bytes.fromhex('8b7e24395f')))
        self.assertIsNone(probe.decode_span(bytes.fromhex('90')))
        patched, tail = probe.written_bytes(0x4982db, 6, bytes.fromhex('8b7e24395f04'), 0x0a100000)
        self.assertEqual(patched, b'\xe9' + struct.pack('<i', 0x0a100000 - 0x4982e0) + b'\x04')
        self.assertEqual(tail, bytes.fromhex('8b7e24395f04e9'))

    def test_core_compiled_decisions(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-music-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'music_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(directory / 'harness.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.strip().splitlines()[-1], 'music_keep_core checks_failed=0')

    def test_stub_shape(self):
        # The five stubs: EFLAGS and EAX/ECX/EDX saved first and restored last, ESP back to the entry value, no x87/SSE
        # opcode, the A stub's return-address slot and the C thunk's start slot at the entry offsets plus the 16 saved bytes.
        text = source_text(MODULE)
        asm = text[text.index('asm(R"('):text.index('.att_syntax')]
        stubs = re.split(r'\n\s*\.globl _', asm)[1:]
        self.assertEqual([s.split(':', 1)[0].split('\n')[0].strip() for s in stubs],
                         ['x3m_music_keep_a_stub', 'x3m_music_keep_c_thunk', 'x3m_music_keep_status_thunk', 'x3m_music_keep_run_thunk', 'x3m_music_keep_entry_stub',
                          'x3m_music_keep_stop_movie_stub', 'x3m_music_trace_stop_stub', 'x3m_music_trace_play_stub', 'x3m_music_trace_stop_movie_stub'])
        for stub in stubs:
            lines = [l.strip() for l in stub.split('\n')[1:] if l.strip() and not l.strip().startswith('.')]
            body = [re.sub(r'^\d+:\s*', '', l) for l in lines[1:]]
            self.assertEqual(body[:4], ['pushfd', 'push eax', 'push ecx', 'push edx'], stub[:40])
            self.assertNotRegex(stub, r'\b(f\w+|movups|movaps|xmm\d)\b')
            exits = [i for i, l in enumerate(body) if l.startswith('jmp dword ptr') or l == 'ret']
            self.assertTrue(exits, stub[:40])
            for i in exits:
                window = body[max(0, i - 8):i]   # the A stub's two exits share their pops before the je
                self.assertIn('popfd', window, stub[:40])
                self.assertEqual([l for l in window if l.startswith('pop ')][-3:], ['pop edx', 'pop ecx', 'pop eax'], stub[:40])
        consts = probe.source_constants(source_text(CORE))
        self.assertIn('push dword ptr [esp+0x%x]' % (consts['a_return_slot'] + 16), stubs[0])
        self.assertIn('push dword ptr [esp+0x14]', stubs[1])      # [esp+4] at entry + 16 saved bytes
        self.assertIn('call dword ptr [_x3m_music_pause_fn]', stubs[1])
        self.assertIn('mov eax, 1', stubs[1])
        self.assertIn('mov edi, dword ptr [esi+0x24]', stubs[0])   # the displaced load, replayed on the keep_running exit
        a_exits = [l.strip() for l in stubs[0].split('\n') if 'jmp dword ptr' in l]
        self.assertEqual(a_exits, ['jmp dword ptr [_x3m_music_stop_all_continue]', 'jmp dword ptr [_x3m_music_stop_all_skip]', 'jmp dword ptr [_x3m_music_stop_all_next]'])
        self.assertEqual(stubs[0].count('mov edi, dword ptr [esi+0x24]'), 1)   # skip_all: EDI is dead at 0x00498359, not loaded
        a_lines = [re.sub(r'\s+', ' ', l.strip()) for l in stubs[0].split('\n')[1:] if l.strip()]
        # The mode dispatch: exactly `cmp eax, 1 / je 1f` then `cmp eax, 3 / je 3f`, both before the first pop.
        self.assertEqual(a_lines[a_lines.index('add esp, 8') + 1:a_lines.index('add esp, 8') + 5], ['cmp eax, 1', 'je 1f', 'cmp eax, 3', 'je 3f'])
        self.assertLess(a_lines.index('je 3f'), a_lines.index('pop edx'))
        # Label 1 (keep_running) ends with the jump through _skip; label 3 (skip_all) is pops, popfd, then the jump through _next.
        one = a_lines.index('1: pop edx')
        self.assertEqual(a_lines[one:one + 6], ['1: pop edx', 'pop ecx', 'pop eax', 'popfd', 'mov edi, dword ptr [esi+0x24]', 'jmp dword ptr [_x3m_music_stop_all_skip]'])
        three = a_lines.index('3: pop edx')
        self.assertEqual(a_lines[three:three + 5], ['3: pop edx', 'pop ecx', 'pop eax', 'popfd', 'jmp dword ptr [_x3m_music_stop_all_next]'])
        # Patch D thunk: the record (ECX) is the handler's one argument; 1 -> `mov eax, 1; ret` (ESP as at entry), else the
        # tail jump to 0x004d14e0 with EAX/ECX/EDX/EFLAGS restored.
        status = [re.sub(r'\s+', ' ', l.strip()) for l in stubs[2].split('\n')[2:] if l.strip() and not l.strip().startswith('.')]
        self.assertEqual(status, ['pushfd', 'push eax', 'push ecx', 'push edx', 'push ecx', 'call _x3m_music_keep_status', 'add esp, 4', 'cmp eax, 1', 'je 1f',
                                  'pop edx', 'pop ecx', 'pop eax', 'popfd', 'jmp dword ptr [_x3m_music_status_original]',
                                  '1: pop edx', 'pop ecx', 'pop eax', 'popfd', 'mov eax, 1', 'ret'])
        # Patch E thunk: the record (EAX) is the handler's one argument; 1 -> `mov eax, 1; ret` (no Run), else the tail jump
        # to 0x004d1870 with EAX/ECX/EDX/EFLAGS restored.
        run = [re.sub(r'\s+', ' ', l.strip()) for l in stubs[3].split('\n')[2:] if l.strip() and not l.strip().startswith('.')]
        self.assertEqual(run, ['pushfd', 'push eax', 'push ecx', 'push edx', 'push eax', 'call _x3m_music_keep_run', 'add esp, 4', 'cmp eax, 1', 'je 1f',
                               'pop edx', 'pop ecx', 'pop eax', 'popfd', 'jmp dword ptr [_x3m_music_run_original]',
                               '1: pop edx', 'pop ecx', 'pop eax', 'popfd', 'mov eax, 1', 'ret'])
        # Patch C thunk: bit 4 rewrites the seek's start argument ([esp+4] at entry + 16 saved bytes) before the optional pause.
        seek = [re.sub(r'\s+', ' ', l.strip()) for l in stubs[1].split('\n')[2:] if l.strip() and not l.strip().startswith('.')]
        at = seek.index('test eax, 4')
        self.assertEqual(seek[at - 2:at + 6], ['cmp eax, 1', 'je 1f', 'test eax, 4', 'jz 4f', 'mov ecx, dword ptr [_x3m_music_keep_resume_ms]',
                                               'mov dword ptr [esp+0x14], ecx', '4: test eax, 2', 'jz 2f'])
        # The pump and pause shims (second asm block): EBX/ESI/EDI/EBP saved, EAX = the argument at [esp+4] + 16; the pump's
        # AX zero-extended.
        shim = text[text.index('_x3m_music_keep_call_pump:'):]
        shim = [l.strip() for l in shim[:shim.index('.att_syntax')].split('\n')[1:] if l.strip() and not l.strip().startswith('.')]
        self.assertEqual(shim, ['push ebx', 'push esi', 'push edi', 'push ebp', 'mov eax, dword ptr [esp+0x14]', 'call dword ptr [_x3m_music_pump_fn]',
                                'movzx eax, ax', 'pop ebp', 'pop edi', 'pop esi', 'pop ebx', 'ret',
                                '_x3m_music_keep_call_pause:', 'push ebx', 'push esi', 'push edi', 'push ebp', 'mov eax, dword ptr [esp+0x14]',
                                'call dword ptr [_x3m_music_pause_fn]', 'pop ebp', 'pop edi', 'pop esi', 'pop ebx', 'ret'])
        for stub in stubs[4:7]:
            self.assertIn('push esp', stub)
        # The play and stop-movie trace stubs pass the saved block and ESI (the calling KC task), then pop both.
        for stub, handler in ((stubs[7], 'x3m_music_trace_play'), (stubs[8], 'x3m_music_trace_stop_movie')):
            body = [re.sub(r'\s+', ' ', l.strip()) for l in stub.split('\n')[2:] if l.strip() and not l.strip().startswith('.')]
            self.assertEqual(body[4:9], ['mov eax, esp', 'push esi', 'push eax', 'call _' + handler, 'add esp, 8'])
        # The entry stub loops on the handler until it returns 0, pausing each record through the engine's 0x004d1810.
        entry = stubs[4]
        self.assertLess(entry.index('call _x3m_music_keep_stop_all_entry'), entry.index('test eax, eax'))
        self.assertLess(entry.index('test eax, eax'), entry.index('call dword ptr [_x3m_music_pause_fn]'))
        self.assertIn('jmp 1b', entry)

    def test_line_parsers(self):
        row = probe.parse_line('00:12 music_trace_stop frame=4210 seq=3 qpc=1234567 caller=0x004d36c2 name=alt_tab')
        self.assertEqual(row, {'kind': 'music_trace_stop', 'frame': 4210, 'seq': 3, 'qpc': 1234567, 'caller': 0x4d36c2, 'name': 'alt_tab'})
        play = probe.parse_line('music_trace_play frame=4211 seq=4 qpc=1234999 id=2004 start_ms=0 caller=0x00499824 name=MOV_PlayMovie record=0x0a3f1240 flags=0x90')
        self.assertEqual((play['id'], play['start_ms'], play['name'], play['flags']), (2004, 0, 'MOV_PlayMovie', 0x90))
        seek = probe.parse_line('music_keep_seek frame=4211 seq=5 qpc=1235000 record=0x0a3f1240 id=2004 start_ms=0 flags=0x90 action=skip hold_id=2004 pause_record=0x00000000 holds_before=1')
        self.assertEqual((seek['kind'], seek['action'], seek['holds_before']), ('music_keep_seek', 'skip', 1))
        self.assertIsNone(probe.parse_line('collide_census frame=1'))
        orphan = probe.parse_line('music_keep_orphan frame=4300 seq=9 qpc=5 caller=0x00404cf2 name=load record=0x0a3f1240 id=2004 action=pause holds=0')
        self.assertEqual((orphan['kind'], orphan['name'], orphan['action'], orphan['id']), ('music_keep_orphan', 'load', 'pause', 2004))
        cut = probe.parse_line('music_walk_cut frame=1 seq=2 qpc=3 walk=find_record limit=4096 cuts=1')
        self.assertEqual((cut['kind'], cut['walk'], cut['limit']), ('music_walk_cut', 'find_record', 4096))
        self.assertEqual(probe.parse_line('music_keep_stop_movie frame=1 seq=2 qpc=3 id=2004 caller=0x00499886 name=MOV_StopMovie dropped=1 holds=0')['dropped'], 1)
        pump = probe.parse_line('music_keep_save_pump frame=900 seq=40 qpc=1 record=0x0a3f1240 id=8404 calls=12 ret0=0 ret1=12 ret2=0 ret_other=0 writes=812345 samples=25386 '
                                'span_ms=2890 first_call_ms=31 max_gap_ms=262 end_ms=3010 pump_end=gzclose close_caller=0x004e9365 end=replay run=skipped buffer_state=2 interval_ms=250')
        self.assertEqual((pump['kind'], pump['calls'], pump['ret1'], pump['span_ms'], pump['pump_end'], pump['run']), ('music_keep_save_pump', 12, 12, 2890, 'gzclose', 'skipped'))
        resume = probe.parse_line('music_keep_resume frame=1 seq=2 qpc=3 id=8404 saved_ms=63000 action=seek at=play other_id=8404')
        self.assertEqual((resume['kind'], resume['id'], resume['saved_ms'], resume['action']), ('music_keep_resume', 8404, 63000, 'seek'))
        hook = probe.parse_line('music_keep_save_pump_hook reason=ok save_mode=keep_running iat=0x005323e0 thunk=0x004fae46 write_row=1 close_row=1')
        self.assertEqual((hook['kind'], hook['reason'], hook['iat']), ('music_keep_save_pump_hook', 'ok', 0x5323e0))
        install = probe.parse_line('music_keep requested=1 patched=1 reason=ok site_a=0x004982db site_c=0x00498d54 write_a=atomic write_c=atomic')
        self.assertEqual((install['patched'], install['reason'], install['site_a']), (1, 'ok', 0x4982db))
        rows = [probe.parse_line(l) for l in (
            'music_trace_play frame=100 seq=1 qpc=1 id=2004 start_ms=0 caller=0x00499824 name=MOV_PlayMovie record=0x1 flags=0x90',
            'music_trace_stop frame=4210 seq=2 qpc=2 caller=0x004d36c2 name=alt_tab',
            'music_keep_stop frame=4210 seq=3 qpc=3 caller=0x004d36c2 name=alt_tab record=0x1 id=2004 flags=0x92 mode=paused held=1 holds=1',
            'music_keep_seek frame=4211 seq=4 qpc=4 record=0x1 id=2004 start_ms=0 flags=0x90 action=skip hold_id=2004 pause_record=0x00000000 holds_before=1',
            'music_trace_play frame=4211 seq=5 qpc=5 id=2004 start_ms=0 caller=0x00499824 name=MOV_PlayMovie record=0x1 flags=0x90',
            'music_trace_stop frame=5000 seq=6 qpc=6 caller=0x00404561 name=save',
            'music_trace_play frame=5001 seq=7 qpc=7 id=8404 start_ms=0 caller=0x00499824 name=MOV_PlayMovie record=0x2 flags=0x90')]
        pairs = probe.restart_pairs(rows)
        self.assertEqual(pairs, [{'stop_caller': 'alt_tab', 'stop_seq': 2, 'play_seq': 5, 'id': 2004, 'start_ms': 0, 'same_id': True, 'seek_skipped': True},
                                 {'stop_caller': 'save', 'stop_seq': 6, 'play_seq': 7, 'id': 8404, 'start_ms': 0, 'same_id': False, 'seek_skipped': False}])

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('music_keep::initialize();'), 1)
        self.assertEqual(capture.count('music_keep::present(ctx.id,ctx.frame,ctx.capture);'), 1)
        self.assertLess(capture.index('collide_memo::initialize();'), capture.index('music_keep::initialize();'))
        self.assertIn('x3m::music_keep::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/music_keep.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(MODULE)
        for needle in ('L"X3M_MUSIC_KEEP"', 'L"X3M_MUSIC_TRACE"', "length == 1 && setting[0] == L'1'", 'install_window_open()', 'executable_verified()', 'bytes_mismatch',
                       'callers_mismatch', 'pin_self()', 'engine_patch::restore_call(c_site_)', 'unhook_site(a_site_)', 'rollback_failed', 'LightCallBoundary',
                       'call_preserved', 'trace_line_cap', 'SetLastError(error)', 'acquire_shared', 'release_shared', 'stale_hold_to_pause(holds_, live_record)',
                       'drop_holds_by_id(holds_, id)', 'music_walk_cut', 'bytes_match(stop_all_va, stop_all_head, stop_all_head_length)',
                       'bytes_match(a_next_record_va, a_next_record_window, a_next_record_length)', 'x3m_music_stop_all_next = a_next_record_va',
                       'core::acquire_shared(ops, s, spec, stub, continuation, reason)', 'core::release_shared(ops, s)', 'window_matches(', 'LiveClaim{'):
            self.assertIn(needle, module)
        # Every handler is inert unless its feature's install completed (armed_), so a stub left chained by a failed rollback behaves as vanilla.
        self.assertEqual(module.count('if (!keep_armed_'), 10)   # the eight handlers, the save pump's bind and Present's active-flag sample
        # Patch D is optional: its refusal disables skip_all (alt-tab paused) instead of refusing the keep; rollback and shutdown restore it.
        for needle in ('status_windows_match()', 'engine_patch::claim_call(d_site_, d_site_va, d_target_va', 'engine_patch::restore_call(d_site_)',
                       'decide_stop(holds_, record, flags, id, media, return_va, status_gate_, save_pump_free)', 'decide_status(holds_, record, flags, id, media, 0, input)',
                       'music_keep_active', 'alt_tab_mode=%s', 'release_for(entry_site_, keep_entry_acquired_)', 'release_for(stop_movie_site_, trace_stop_movie_acquired_)'):
            self.assertIn(needle, module)
        self.assertEqual(module.count('engine_patch::restore_call(d_site_)'), 2)
        # Music through the save and the resume: Patch E optional (its refusal keeps the save paused), restored by rollback
        # and shutdown; the pump binds only on the verified IAT slot through loading_trace's existing import rows.
        for needle in ('save_windows_match()', 'engine_patch::claim_call(e_site_, e_site_va, e_target_va', 'set_save_write_observer(observer)',
                       'iat_slot_mismatch', 'original_mismatch', 'thunk_mismatch', 'rows_not_live', 'late_claim', 'GetProcAddress(zlib, "gzwrite")',
                       'pump_.arm(record, id, media, GetCurrentThreadId(), qpc(), frequency_)', 'pump_.pump_due(now, GetCurrentThreadId())',
                       'pump_.on_play(record, id, media, d.action == SeekAction::skip, qpc())', 'pump_.skip_run(record, media, state, qpc())',
                       'pump_.end_now(PumpEnd::stop_all, qpc())', 'pump_.end_now(PumpEnd::stop_movie, qpc())', 'pump_.stop(PumpEnd::gzclose, qpc())',
                       'resume_on_play(resume_, id, start_ms, d.action, resume_ms)', 'resume_on_stop(resume_, return_va, id, flags, read, ms)',
                       'resume_on_stop_all(resume_, return_va)', 'bytes_match(position_reader_va, position_reader, position_reader_length)',
                       'music_keep_save_pump ', 'music_keep_resume ', 'music_keep_save_pump_hook',
                       # review fixes: the fallback pause, the save's own gzclose, the overdue replay at Present, the boundary behind the fast paths
                       'run_fallback(cpu)', 'x3m_music_keep_call_pause(pump_.record)', 'holds_.set_mode(pump_.record, pump_.id, pump_.media, StopMode::paused)',
                       'return_va != gzclose_wrapper_return_va', 'caller_return_va != save_gzclose_return_va', 'music_keep_replay_check()',
                       'return music_keep_run_marked(record);', 'music_keep_gz_write_sampled();', 'fallback=%s fallback_reason=%s',
                       'call_targets(save_gzclose_call_va, gzclose_wrapper_va)', 'std::uintptr_t(close_slot) != gzclose_iat_va',
                       # the trace's task fields at T2 / T3
                       'read_task(task, t)', 'read_cell(t, trace_cells[i], tags[i], values[i])', 'alert_record_ptr_va', 'a1_slot=%lu a2_task=%lu',
                       'c6=%u:0x%08lx c8=%u:0x%08lx'):
            self.assertIn(needle, module)
        self.assertEqual(module.count('engine_patch::restore_call(e_site_)'), 2)
        self.assertEqual(module.count('cpu.before_original();'), 3)   # the pump call, the fallback pause and the position read run on the engine's MXCSR / LastError
        self.assertLess(capture.index('loading_trace::initialize();'), capture.index('music_keep::save_pump_report();'))
        self.assertIn('loading_trace::save_write_observer_requested()', capture)
        tracer = source_text(ROOT / 'src/proxy/loading_trace.cpp')
        self.assertIn('{"zlib1.dll","gzwrite",reinterpret_cast<PVOID>(gz_write)}', tracer)
        self.assertIn('reinterpret_cast<uintptr_t>(&file) + sizeof file', tracer)   # the caller's return above the argument
        self.assertEqual(tracer.count('bind_save_observer();'), 2)   # both install paths (telemetry and rows alone)
        self.assertIn('(music && save_write_row(hook))', tracer)
        self.assertNotIn('music_keep', tracer.replace('"music_keep"', ''))   # no link dependency of the loading fixture on the keep
        self.assertEqual(module.count('if (!trace_armed_) return;'), 3)
        # Every window check sees through the live shared claims (the run271 refusal): bytes_match is the only comparison, and it overlays both shared sites.
        body = module[module.index('bool bytes_match('):module.index('bool call_targets(')]
        self.assertIn('&entry_site_, &stop_movie_site_', body)
        self.assertNotIn('memcmp', module)
        # The stop-all classifier keeps EDI's displaced load on the skip exit and jumps to the bookkeeping label only.
        self.assertIn('x3m_music_stop_all_skip = a_skip_va', module)
        audit = source_text(ROOT / 'verification/probe/check_no_x87.py')
        for symbol in ('_x3m_music_keep_a_stub', '_x3m_music_keep_c_thunk', '_x3m_music_keep_entry_stub', '_x3m_music_keep_stop_movie_stub', '_x3m_music_keep_stop_all',
                       '_x3m_music_keep_seek', '_x3m_music_keep_stop_all_entry', '_x3m_music_keep_stop_movie', '_x3m_music_trace_stop', '_x3m_music_trace_play', '_x3m_music_trace_stop_movie',
                       '_x3m_music_keep_run_thunk', '_x3m_music_keep_run', '_x3m_music_keep_gz_write', '_x3m_music_keep_gz_close', '_x3m_music_keep_call_pump',
                       '_x3m_music_keep_call_pause'):
            self.assertIn(f"'{symbol}'", audit)


class MusicLaunchOptions(unittest.TestCase):
    def launch(self, directory, *args, inherited=None, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, **kw):
        code, output, error = self.launch(directory, *args, **kw)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_keep_default_on_a_modded_launch(self):
        # --music-keep is a launcher default since 2026-09-25 (accepted run273); --music-trace stays opt-in.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory)
            self.assertEqual(env['X3M_MUSIC_KEEP'], '1')
            self.assertNotIn('X3M_MUSIC_TRACE', env)
            self.assertEqual(env, self.env(directory, '--music-keep'), 'the explicit flag is redundant')

    def test_opt_out_and_trace_switch(self):
        # The trace is part of --debug since the logging tiers (2026-09-26): the DLL reads X3M_MUSIC_TRACE or X3M_DEBUG.
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.env(directory, '--no-music-keep')
            self.assertNotIn('X3M_MUSIC_KEEP', baseline)
            self.assertEqual({k: v for k, v in self.env(directory).items() if k not in baseline}, {'X3M_MUSIC_KEEP': '1'})
            self.assertEqual({k: v for k, v in self.env(directory, '--no-music-keep', '--debug').items() if k not in baseline}, {'X3M_DEBUG': '1'})
            self.assertEqual({k: v for k, v in self.env(directory, '--debug').items() if k not in baseline}, {'X3M_MUSIC_KEEP': '1', 'X3M_DEBUG': '1'})
            code, _, error = self.launch(directory, '--music-trace')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)
        self.assertIn('requested(L"X3M_MUSIC_TRACE", &trace_present) || log_tier::debug();', source_text(ROOT / 'src/proxy/music_keep.cpp'))

    def test_refused_under_vanilla_and_inherited_values_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--music-keep', vanilla=True)
            self.assertEqual(code, 2)
            self.assertIn('--music-keep cannot be combined with --vanilla', error)
            env = self.env(directory, '--no-music-keep', inherited={'X3M_MUSIC_KEEP': '1', 'X3M_MUSIC_TRACE': '1'})
            self.assertNotIn('X3M_MUSIC_KEEP', env)
            self.assertNotIn('X3M_MUSIC_TRACE', env)
            env = self.env(directory, inherited={'X3M_MUSIC_KEEP': '1', 'X3M_MUSIC_TRACE': '1'}, vanilla=True)
            self.assertNotIn('X3M_MUSIC_KEEP', env)
            self.assertNotIn('X3M_MUSIC_TRACE', env)

if __name__ == '__main__':
    unittest.main()
