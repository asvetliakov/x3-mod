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
    check(stop_mode(0x004d36c2) == StopMode::paused && stop_mode(0x00404561) == StopMode::keep_running && stop_mode(0x00407069) == StopMode::keep_running, "preserve callers");
    check(stop_mode(0x00404cf2) == StopMode::vanilla && stop_mode(0x00497bbb) == StopMode::vanilla && stop_mode(0x0040387d) == StopMode::vanilla && stop_mode(0x12345678) == StopMode::vanilla, "vanilla callers");
    check(!std::strcmp(stop_caller_name(0x004d36c2), "alt_tab") && !std::strcmp(stop_caller_name(0), "unknown") && !std::strcmp(play_caller_name(0x00499824), "MOV_PlayMovie")
          && !std::strcmp(play_caller_name(0x00499987), "MOV_PlayMovieFrom") && !std::strcmp(stop_movie_caller_name(0x00499886), "MOV_StopMovie"), "names");
    // Patch A: a music record through save/pause is held and keeps running; through alt-tab it is held and paused; non-music never; vanilla drops holds.
    Holds holds;
    StopDecision s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    check(s.mode == StopMode::keep_running && s.held && holds.count == 1 && holds.slots[0].record == 0x1000 && holds.slots[0].id == 2004 && holds.slots[0].media == 0x2000, "save keeps running and holds");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00407069);
    check(s.mode == StopMode::keep_running && holds.count == 1, "pause: same record replaces its hold");
    s = decide_stop(holds, 0x1040, 0x12, 5, 0x2100, 0x00404561);
    check(s.mode == StopMode::vanilla && !s.held && holds.count == 1, "non-music record under save: vanilla, not held");
    s = decide_stop(holds, 0x1080, 0xd2, 8404, 0x2200, 0x004d36c2);
    check(s.mode == StopMode::paused && s.held && holds.count == 2 && holds.slots[1].mode == StopMode::paused, "alt-tab: paused and held");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404cf2);
    check(s.mode == StopMode::vanilla && !s.held && holds.count == 0, "load drops every hold");
    s = decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00000000);
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
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);   // save: keep-running hold, flag 2 then cleared by the engine
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::skip && d.hold_id == 2004 && holds.count == 0, "held after save, replayed at 0: skip and drop the holds");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x004d36c2);   // alt-tab: paused hold
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::skip && holds.count == 0, "held after alt-tab, replayed at 0: skip (Run resumes)");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x004d36c2);
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2100, 0, lookup);
    check(d.action == SeekAction::vanilla && holds.count == 0, "same record, other media object: no match, vanilla, holds dropped");
    // Another id while a keep-running hold is still linked, unclaimed and not playing: pause it first.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    live[0x1000] = LiveRecord{0x90, 0};
    d = decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup);
    check(d.action == SeekAction::pause_then_vanilla && d.pause_record == 0x1000 && d.hold_id == 2004 && holds.count == 0, "other id with a running hold: pause it, then vanilla");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    live[0x1000] = LiveRecord{0x92, 0};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record playing again: no pause");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    live[0x1000] = LiveRecord{0x90, 0x77};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record owned by a script task again: no pause");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    live.erase(0x1000);
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "held record no longer linked: no pause");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x004d36c2);
    live[0x1000] = LiveRecord{0x90, 0};
    check(decide_seek(holds, 0x1080, 0x90, 8404, 0x2200, 0, lookup).action == SeekAction::vanilla, "paused (alt-tab) hold and another id: vanilla, already paused");
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    d = decide_seek(holds, 0x1040, 0x12, 5, 0x2100, 0, lookup);
    check(d.action == SeekAction::vanilla && holds.count == 1, "a non-music play leaves the holds alone");
    holds.clear();
    // F1: a DirectSound-path music record (0x40) is paused, not kept running, under save and pause.
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00404561);
    check(s.mode == StopMode::paused && s.held && holds.slots[0].mode == StopMode::paused, "DirectSound-path record under save: paused and held");
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x00407069);
    check(s.mode == StopMode::paused, "DirectSound-path record under pause: paused");
    s = decide_stop(holds, 0x1000, 0xd2, 2004, 0x2000, 0x004d36c2);
    check(s.mode == StopMode::paused, "DirectSound-path record under alt-tab: paused");
    holds.clear();
    // F2: at any stop-all, orphaned keep-running holds (linked, not playing, context 0) are paused one by one; others dropped; paused holds stay.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);   // keep_running, will be orphaned and live
    decide_stop(holds, 0x1080, 0x92, 8404, 0x2200, 0x004d36c2);   // paused: stays
    decide_stop(holds, 0x1100, 0x92, 8509, 0x2300, 0x00407069);   // keep_running, replayed meanwhile (context set): dropped, not paused
    decide_stop(holds, 0x1180, 0x92, 8600, 0x2400, 0x00407069);   // keep_running, live and orphaned
    live[0x1000] = LiveRecord{0x90, 0}; live[0x1100] = LiveRecord{0x92, 0x55}; live[0x1180] = LiveRecord{0x90, 0};
    std::uint32_t first = stale_hold_to_pause(holds, lookup);
    std::uint32_t second = stale_hold_to_pause(holds, lookup);
    std::uint32_t third = stale_hold_to_pause(holds, lookup);
    check(first == 0x1000 && second == 0x1180 && third == 0 && holds.count == 1 && holds.slots[0].record == 0x1080 && holds.slots[0].mode == StopMode::paused,
          "orphaned keep-running holds paused in order, replayed one dropped, paused hold kept");
    holds.clear();
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    live.erase(0x1000);
    check(stale_hold_to_pause(holds, lookup) == 0 && holds.count == 0, "unlinked keep-running hold: dropped without a pause");
    check(stale_hold_to_pause(holds, lookup) == 0, "empty table: nothing");
    // F4: MOV_StopMovie drops the id's holds only.
    decide_stop(holds, 0x1000, 0x92, 2004, 0x2000, 0x00404561);
    decide_stop(holds, 0x1080, 0x92, 8404, 0x2200, 0x004d36c2);
    check(drop_holds_by_id(holds, 2004) == 1 && holds.count == 1 && holds.slots[0].id == 8404 && drop_holds_by_id(holds, 2004) == 0, "stop-movie drops the id's hold only");
    d = decide_seek(holds, 0x1000, 0x90, 2004, 0x2000, 0, lookup);
    check(d.action == SeekAction::vanilla, "after the script's own stop, the replay seeks to 0 as vanilla");
    check(list_walk_limit >= 1024 && flag_directsound == 0x40, "walk limit and flags");
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
    return probe.inspect(bytes(image), CORE.read_text())


class MusicSites(unittest.TestCase):
    @unittest.skipUnless(probe.EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['failures'])
        self.assertEqual(report['check_count'], 39)
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
            'sha256': (0x498400, b'\x90'),
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = patched_report(data, [change])
                self.assertFalse(report['checks'][failed], failed)
                self.assertEqual(report['result'], 'FAIL')

    def test_source_constants_and_tables(self):
        consts = probe.source_constants(CORE.read_text())
        for name, value in probe.EXPECTED.items():
            self.assertEqual(consts.get(name), value, name)
        stops, plays = probe.source_callers(CORE.read_text())
        self.assertEqual(stops, probe.STOP_CALLERS)
        self.assertEqual(plays, probe.PLAY_CALLERS + probe.STOP_MOVIE_CALLERS)
        self.assertEqual([m for _, _, _, m in stops], ['paused', 'keep_running', 'keep_running', 'vanilla', 'vanilla', 'vanilla'])
        windows = probe.source_windows(CORE.read_text())
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
        text = MODULE.read_text()
        asm = text[text.index('asm(R"('):text.index('.att_syntax')]
        stubs = re.split(r'\n\s*\.globl _', asm)[1:]
        self.assertEqual([s.split(':', 1)[0].split('\n')[0].strip() for s in stubs],
                         ['x3m_music_keep_a_stub', 'x3m_music_keep_c_thunk', 'x3m_music_keep_entry_stub', 'x3m_music_keep_stop_movie_stub',
                          'x3m_music_trace_stop_stub', 'x3m_music_trace_play_stub', 'x3m_music_trace_stop_movie_stub'])
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
        consts = probe.source_constants(CORE.read_text())
        self.assertIn('push dword ptr [esp+0x%x]' % (consts['a_return_slot'] + 16), stubs[0])
        self.assertIn('push dword ptr [esp+0x14]', stubs[1])      # [esp+4] at entry + 16 saved bytes
        self.assertIn('call dword ptr [_x3m_music_pause_fn]', stubs[1])
        self.assertIn('mov eax, 1', stubs[1])
        self.assertIn('mov edi, dword ptr [esi+0x24]', stubs[0])   # the displaced load, replayed on the keep_running exit
        for stub in stubs[2:]:
            self.assertIn('push esp', stub)
        # The entry stub loops on the handler until it returns 0, pausing each record through the engine's 0x004d1810.
        entry = stubs[2]
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
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('music_keep::initialize();'), 1)
        self.assertEqual(capture.count('music_keep::present(ctx.id,ctx.frame,ctx.capture);'), 1)
        self.assertLess(capture.index('collide_memo::initialize();'), capture.index('music_keep::initialize();'))
        self.assertIn('x3m::music_keep::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/music_keep.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = MODULE.read_text()
        for needle in ('L"X3M_MUSIC_KEEP"', 'L"X3M_MUSIC_TRACE"', "length == 1 && setting[0] == L'1'", 'install_window_open()', 'executable_verified()', 'bytes_mismatch',
                       'callers_mismatch', 'pin_self()', 'engine_patch::restore_call(c_site_)', 'unhook_site(a_site_)', 'rollback_failed', 'LightCallBoundary',
                       'call_preserved', 'trace_line_cap', 'SetLastError(error)', 'acquire_shared', 'release_shared', 'stale_hold_to_pause(holds_, live_record)',
                       'drop_holds_by_id(holds_, id)', 'music_walk_cut', 'bytes_match(stop_all_va, stop_all_head, stop_all_head_length)'):
            self.assertIn(needle, module)
        # Every handler is inert unless its feature's install completed (armed_), so a stub left chained by a failed rollback behaves as vanilla.
        self.assertEqual(module.count('if (!keep_armed_'), 4)
        self.assertEqual(module.count('if (!trace_armed_) return;'), 3)
        # The stop-all classifier keeps EDI's displaced load on the skip exit and jumps to the bookkeeping label only.
        self.assertIn('x3m_music_stop_all_skip = a_skip_va', module)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        for symbol in ('_x3m_music_keep_a_stub', '_x3m_music_keep_c_thunk', '_x3m_music_keep_entry_stub', '_x3m_music_keep_stop_movie_stub', '_x3m_music_keep_stop_all',
                       '_x3m_music_keep_seek', '_x3m_music_keep_stop_all_entry', '_x3m_music_keep_stop_movie', '_x3m_music_trace_stop', '_x3m_music_trace_play', '_x3m_music_trace_stop_movie'):
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

    def test_default_off_on_a_modded_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory)
            self.assertNotIn('X3M_MUSIC_KEEP', env)
            self.assertNotIn('X3M_MUSIC_TRACE', env)

    def test_opt_in_switches(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.env(directory)
            self.assertEqual({k: v for k, v in self.env(directory, '--music-keep').items() if k not in baseline}, {'X3M_MUSIC_KEEP': '1'})
            self.assertEqual({k: v for k, v in self.env(directory, '--music-trace').items() if k not in baseline}, {'X3M_MUSIC_TRACE': '1'})
            self.assertEqual({k: v for k, v in self.env(directory, '--music-keep', '--music-trace').items() if k not in baseline}, {'X3M_MUSIC_KEEP': '1', 'X3M_MUSIC_TRACE': '1'})

    def test_refused_under_vanilla_and_inherited_values_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--music-keep',), ('--music-trace',), ('--music-keep', '--music-trace')):
                code, _, error = self.launch(directory, *args, vanilla=True)
                self.assertEqual(code, 2, args)
                self.assertIn('--music-keep/--music-trace cannot be combined with --vanilla', error)
            env = self.env(directory, inherited={'X3M_MUSIC_KEEP': '1', 'X3M_MUSIC_TRACE': '1'})
            self.assertNotIn('X3M_MUSIC_KEEP', env)
            self.assertNotIn('X3M_MUSIC_TRACE', env)
            env = self.env(directory, inherited={'X3M_MUSIC_KEEP': '1'}, vanilla=True)
            self.assertNotIn('X3M_MUSIC_KEEP', env)


if __name__ == '__main__':
    unittest.main()
