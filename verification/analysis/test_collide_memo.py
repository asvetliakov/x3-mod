"""Host checks of the no-contact collision memo (src/proxy/collide_memo_core.h).

The table (4-way sets, one-frame expiry, eviction of the entry touched longest ago) is compiled with the host compiler
and driven against a dictionary model over random keys, frames and gaps. The site verifier is run against the
installed executable when it is present and against corrupted copies of it; the log-line parsers, the runner's
acceptance rule, the production wiring, the x87 audit roots and the --collide-memo / --collide-memo-verify launcher
gates are checked from the sources. Equality with the engine is the Wine fixture's job
(verification/probe/run_collide_memo.py), not this module's.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_memo_site as probe  # noqa: E402
import run_collide_memo as runner  # noqa: E402

HARNESS = r'''
#include "collide_memo_core.h"
#include <cstdio>
#include <map>
#include <vector>
using namespace x3m::collide_memo::core;
static unsigned long long rng = 0x9e3779b97f4a7c15ull;
static unsigned rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return unsigned(rng >> 16); }
static Table table;   // static: about 430 KB
int main() {
    table.clear();
    unsigned failures = 0, hits = 0, stale = 0, evictions = 0, expired = 0;
    // The model: what was stored, with which outputs, and when it was last touched. A find may miss what the model
    // still holds (eviction), never the other way round, and never with other outputs.
    struct Seen { Outputs outputs; unsigned frame; };
    std::map<std::vector<unsigned>, Seen> model;
    unsigned frame = 0xfffffff0u;   // wraps during the run
    for (unsigned step = 0; step < 400000; ++step) {
        if (rnd() % 50 == 0) frame += 1 + (rnd() % 8 == 0 ? rnd() % 3 : 0);
        Key key{};
        const unsigned id = rnd() % (step < 200000 ? 300 : 5000);   // the second half overflows the table
        for (unsigned i = 0; i < key_words; ++i) key.words[i] = id * 2654435761u + i * 40503u;
        key.words[rnd() % key_words] ^= (rnd() % 16 == 0) ? 1u << (rnd() % 32) : 0u;   // now and then one bit of one word differs
        const std::vector<unsigned> name(key.words, key.words + key_words);
        const auto known = model.find(name);
        const bool may_hit = known != model.end() && frame - known->second.frame <= 1u;
        Entry* const entry = table.find(key, frame);
        if (entry != nullptr) {
            if (!may_hit || std::memcmp(&entry->outputs, &known->second.outputs, sizeof(Outputs)) != 0) { ++stale; ++failures; }
            else { ++hits; entry->frame = frame; known->second.frame = frame; }
        } else {
            if (known != model.end() && frame - known->second.frame > 1u) ++expired;
            Outputs outputs{};
            outputs.visits = rnd(); outputs.triangles = rnd() | 1u; outputs.tolerance_integer = id;   // a leaf was reached: the exact key only
            for (unsigned& w : outputs.root_block) w = rnd();
            if (table.store(key, outputs, frame)) ++evictions;
            model[name] = Seen{outputs, frame};
            if (table.find(key, frame) == nullptr) ++failures;   // what was just stored is found
        }
    }
    static_assert(key_words == 81 && sizeof(Key) == 324 && ways * sets == 1024, "table shape");
    // The running-minimum rule and the miss classes, one by one.
    table.clear();
    Key base{};
    for (unsigned i = 0; i < key_words; ++i) base.words[i] = 1000 + i;
    Outputs none{}, some{};
    some.triangles = 3;
    const auto with = [&](unsigned word, unsigned value) { Key k = base; k.words[word] = value; return k; };
    table.store(base, none, 10);
    if (table.find(with(minimum_value_word, 7), 10) == nullptr) ++failures;                 // no leaf reached: any minimum is answered
    if (table.find(with(29, 0), 10) != nullptr || table.find(with(26, 5), 10) != nullptr) ++failures;   // null-ness and tolerance still count
    if (table.find(with(minimum_value_word, 7), 12) != nullptr) ++failures;                 // and expiry still applies
    table.clear();
    table.store(base, some, 10);
    if (table.find(with(minimum_value_word, 7), 10) != nullptr || table.find(base, 10) == nullptr) ++failures;   // a leaf was reached: the exact minimum only
    const unsigned expected[8][2] = {{minimum_value_word, miss_min_value}, {13, miss_xform_b}, {24, miss_xform_b}, {12, miss_scale}, {25, miss_scale}, {27, miss_mode}, {40, miss_models}, {3, miss_xform_a}};
    for (const auto& e : expected) if (table.classify(with(e[0], 99)) != e[1]) ++failures;
    if (table.classify(base) != miss_expired) ++failures;
    Key other = base; other.words[2] = 1; other.words[20] = 1;
    if (table.classify(other) != miss_none_found || table.classify(with(model_words_begin, 5)) != miss_none_found) ++failures;
    std::printf("failures=%u hits=%u stale=%u evictions=%u expired=%u\n", failures, hits, stale, evictions, expired);
    return failures ? 1 : 0;
}
'''

SAMPLE = '''collide_memo requested=1 patched=1 verify=0 reason=ok site=0x0047f329 target=0x004e29f0 write=plain handler=0x00342a10 entries=1024
SCENARIO static queries=528 hits=427 contacts=40 differences=0 stale=0 hit_on_contact=0
SCENARIO one_step queries=59 hits=32 contacts=0 differences=0 stale=0 hit_on_contact=0
SCENARIO approach queries=422 hits=60 contacts=2 differences=0 stale=0 hit_on_contact=0
APPROACH contact_frames=2 parked_frames=60 parked_hits=59 first_contact_step=199
SCENARIO modes queries=50 hits=25 contacts=0 differences=0 stale=0 hit_on_contact=0
SCENARIO running_minimum queries=180 hits=79 contacts=49 differences=0 stale=0 hit_on_contact=0
MINIMUM no_leaf_hits=39 leaf_hits_on_repeat=40 contact_filtered_frames=11 contact_frames=49 tolerance=25
SCENARIO addresses queries=9 hits=2 contacts=0 differences=0 stale=0 hit_on_contact=0
SCENARIO expiry queries=4 hits=2 contacts=0 differences=0 stale=0 hit_on_contact=0
SCENARIO overflow queries=9000 hits=1538 contacts=0 differences=0 stale=0 hit_on_contact=0
SCENARIO random queries=48000 hits=42207 contacts=3470 differences=0 stale=0 hit_on_contact=0
SCENARIO guards queries=8 hits=3 contacts=0 differences=0 stale=0 hit_on_contact=0
COLLIDE MEMO BENCH visits=27085 run_ns=1228000 hit_ns=98.7 tiny_run_ns=96.8 tiny_miss_store_ns=273.5 tiny_hit_ns=98.4 miss_store_overhead_ns=176.7
WINDOW collide_memo device=1 frame=4800 frames=300 verify=0 queries=3600 hits=3100 misses=200 stored=190 contacts=300 ineligible=0 evictions=0 skipped_visits=123456 skipped_triangles=12 verified=0 verify_mismatches=0 foreign_thread=0 reentered=0 clears=1 stuck_busy=0 min_relaxed_hits=900 miss_none_found=1 miss_none_found_visits=2 miss_xform_a=3 miss_xform_a_visits=4 miss_xform_b=5 miss_xform_b_visits=6 miss_scale=0 miss_scale_visits=0 miss_mode=0 miss_mode_visits=0 miss_models=0 miss_models_visits=0 miss_min_value=7 miss_min_value_visits=80000 miss_expired=9 miss_expired_visits=10
VERIFY verified=140 injected_mismatches=1
SCENARIO verify queries=170 hits=0 contacts=0 differences=0 stale=0 hit_on_contact=0
SUMMARY queries=58242 hits=44293 contacts=3512 differences=0 stale_hits=0 hits_on_contact=0 register_differences=0 stored=10295 evictions=6452 ineligible=2 skipped_visits=197180493 verified=140 verify_mismatches=1
MISSES min_relaxed_hits=18622 none_found=2982 xform_a=1172 xform_b=25046 scale=369 mode=3 models=2 min_value=7218 expired=11 min_value_visits=7844036 xform_b_visits=985884
COLLIDE MEMO CPU checks=59 failures=0
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('collide_memo_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MemoTable(unittest.TestCase):
    def test_table_against_a_dictionary_model(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-collide-memo-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'memo_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(directory / 'harness.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        fields = {k: int(v) for k, v in (item.split('=') for item in run.stdout.split())}
        self.assertEqual((fields['failures'], fields['stale']), (0, 0))
        for name in ('hits', 'evictions', 'expired'):
            self.assertGreater(fields[name], 100, name)


class MemoSite(unittest.TestCase):
    def test_source_constants_and_parsers(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), probe.EXPECTED_CONSTANTS)
        row = probe.parse_install_line(SAMPLE.splitlines()[0])
        self.assertEqual((row['requested'], row['patched'], row['verify'], row['reason'], row['site'], row['target'], row['entries']), (True, True, False, 'ok', 0x47f329, 0x4e29f0, 1024))
        self.assertIsNone(probe.parse_install_line('collide_memo requested=1 patched=0 reason=body_mismatch'))
        window = probe.parse_window_line(next(l for l in SAMPLE.splitlines() if l.startswith('WINDOW '))[7:])
        self.assertEqual((window['hits'], window['skipped_visits'], window['verify_mismatches'], window['frames'], window['clears'], window['stuck_busy']), (3100, 123456, 0, 300, 1, 0))
        self.assertEqual((window['min_relaxed_hits'], window['miss_min_value'], window['miss_min_value_visits'], window['miss_xform_b_visits']), (900, 7, 80000, 6))
        self.assertIsNone(probe.parse_window_line('collide_memo device=1 frame=300'))
        module = (ROOT / 'src/proxy/collide_memo.cpp').read_text()
        for key in probe.WINDOW_KEYS:
            self.assertIn(f'{key}=', module, key)

    @unittest.skipUnless(Path(probe.sites.DEFAULT_EXE).is_file() and shutil.which(probe.common.OBJDUMP), 'installed X3AP.exe and objdump required')
    def test_installed_executable_and_corrupted_copies(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', [k for k, v in report['checks'].items() if not v])
        self.assertGreaterEqual(len(report['checks']), 25)
        data = Path(probe.sites.DEFAULT_EXE).read_bytes()
        for va, raw, failing in ((0x47f329, b'\xe9', 'site_whole_call'), (0x47f32a, b'\xc3', 'site_whole_call'), (0x47f32e, b'\x31', 'windows'), (0x47f1d3, b'\xd8', 'body_hashes'),
                                 (0x47f2fa, b'\x74', 'node_b_words'), (0x4e2a0e, b'\x30', 'target_reads_tenth_word'), (0x4e2300, b'\x90', 'body_hashes'), (0x4e2b00, b'\x90', 'body_hashes'),
                                 (0x4e3300, b'\x90', 'body_hashes'), (0x4e2000, b'\x90', 'body_hashes'), (0x4dfe70, b'\x90', 'body_hashes'), (0x52b5e0, b'\x90', 'body_hashes'),
                                 (0x47f1d7, b'\x56', 'tenth_word_is_pushed_edi')):
            image = bytearray(data)
            offset = va - 0x401000 + 0x400
            image[offset:offset + len(raw)] = raw
            with tempfile.NamedTemporaryFile(suffix='.exe') as f:
                f.write(image)
                f.flush()
                changed = probe.verify(f.name)
            self.assertEqual(changed['result'], 'FAIL', hex(va))
            self.assertTrue(changed['checks'].get(failing) is False or changed['checks'].get('decode') is False, (hex(va), failing))
        # The census's entry claims and the SAT module's rel32 sit in the hashed bodies' holes.
        patched = bytearray(data)
        for va, raw in ((0x4e2530, b'\xe9\x11\x22\x33\x44'), (0x4e25a4, b'\x55\x66\x77\x08'), (0x4e2190, b'\xe9\x01\x02\x03\x04')):
            offset = va - 0x401000 + 0x400
            patched[offset:offset + len(raw)] = raw
        self.assertEqual(probe.body_hashes(probe.common.Image(bytes(patched))), probe.HASHES)


class MemoWiring(unittest.TestCase):
    def test_production_wiring_and_audit_roots(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('collide_memo::initialize();'), 1)
        self.assertEqual(capture.count('collide_memo::present(ctx.id,ctx.frame,ctx.capture);'), 1)
        self.assertLess(capture.index('collide_sat_sse2::initialize();'), capture.index('collide_memo::initialize();'))
        self.assertLess(capture.index('collide_narrow_census::initialize();'), capture.index('collide_memo::initialize();'))
        self.assertIn('x3m::collide_memo::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/collide_memo.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/collide_memo.cpp').read_text()
        self.assertIn('L"X3M_COLLIDE_MEMO"', module)
        self.assertIn('L"X3M_COLLIDE_MEMO_VERIFY"', module)
        self.assertIn('if (contact) { ++counters_.contacts; return; }', module)   # a contact is never stored
        lookup = module[module.index('int __cdecl x3m_collide_memo_lookup'):module.index('void __cdecl x3m_collide_memo_store')]
        self.assertLess(lookup.index('if (owner != thread)'), lookup.index('busy_ = true;'))          # the thread gate comes before any memo state
        self.assertLess(lookup.index('busy_ = true;'), lookup.index('build_key('))                  # busy is set before the lookup proper
        self.assertLess(module.index('a.model_a == nullptr || a.model_b == nullptr'), module.index('a.model_a[model_state_word]'))
        present = module[module.index('void present('):]
        self.assertLess(present.index('GetLastError()'), present.index('log("collide_memo device'))
        self.assertLess(present.index('log("collide_memo device'), present.index('SetLastError(error)'))
        self.assertEqual(capture.count('collide_memo::device_reset();'), 1)
        for forbidden in ('float ', 'double ', '_mm_', 'xmmintrin'):
            self.assertNotIn(forbidden, module, forbidden)   # words only: no floating-point code on the engine's path
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_collide_memo_thunk', '_x3m_collide_memo_lookup', '_x3m_collide_memo_store'", audit)
        self.assertNotIn('0xd9,', (ROOT / 'verification/probe/collide_memo_fixture.cpp').read_text())   # no engine bytes in the tracked fixture

    def test_runner_accepts_only_a_clean_record(self):
        record = {**runner.parse(SAMPLE), 'exit_status': 0}
        self.assertTrue(runner.accepted(record))
        self.assertEqual((record['summary']['hits'], record['bench']['hit_ns'], record['bench']['miss_store_overhead_ns'], record['approach']['parked_hits']), (44293, 98.7, 176.7, 59))
        self.assertEqual((record['minimum']['no_leaf_hits'], record['misses']['min_relaxed_hits']), (39, 18622))
        for change in (('stale_hits=0', 'stale_hits=1'), ('differences=0 stale_hits', 'differences=3 stale_hits'), ('hits_on_contact=0 register', 'hits_on_contact=1 register'),
                       ('checks=59 failures=0', 'checks=59 failures=1'), ('checks=59 failures=0', 'checks=58 failures=0'), (' miss_store_overhead_ns=176.7', ''), ('SCENARIO running_minimum', 'SCENARIO other'), ('SCENARIO expiry', 'SCENARIO other'), ('SUMMARY queries=58242 hits=44293', 'SUMMARY queries=58242 hits=0')):
            self.assertFalse(runner.accepted({**runner.parse(SAMPLE.replace(*change)), 'exit_status': 0}), change)
        self.assertFalse(runner.accepted({**runner.parse(SAMPLE), 'exit_status': 1}))
        self.assertFalse(runner.accepted({**runner.parse(''), 'exit_status': 0}))


COLLIDE = ('X3M_COLLIDE_SAT_SSE2', 'X3M_COLLIDE_MEMO', 'X3M_COLLIDE_MEMO_VERIFY')


class MemoLaunchOption(unittest.TestCase):
    def collide_env(self, directory, *args, vanilla=False, inherited=None):
        code, output, error = self.launch(directory, *args, vanilla=vanilla, inherited=inherited)
        self.assertEqual(code, 0, error)
        return {k: v for k, v in json.loads(output)['env'].items() if k in COLLIDE}

    def test_modded_launch_defaults_and_their_off_switches(self):
        with tempfile.TemporaryDirectory() as directory:
            both = {'X3M_COLLIDE_SAT_SSE2': '1', 'X3M_COLLIDE_MEMO': '1'}
            self.assertEqual(self.collide_env(directory), both)
            self.assertEqual(self.collide_env(directory, '--collide-sat-sse2', '--collide-memo'), both)
            self.assertEqual(self.collide_env(directory, '--no-collide-sat-sse2'), {'X3M_COLLIDE_MEMO': '1'})
            self.assertEqual(self.collide_env(directory, '--no-collide-memo'), {'X3M_COLLIDE_SAT_SSE2': '1'})
            self.assertEqual(self.collide_env(directory, '--no-collide-sat-sse2', '--no-collide-memo', inherited=both), {})
            self.assertEqual(self.collide_env(directory, '--collide-memo-verify'), {**both, 'X3M_COLLIDE_MEMO_VERIFY': '1'})
            self.assertEqual(self.collide_env(directory, vanilla=True, inherited=both), {})
            self.assertEqual(self.collide_env(directory, '--collide-memo', vanilla=True), {'X3M_COLLIDE_MEMO': '1'})
            code, _, error = self.launch(directory, '--no-collide-memo', '--collide-memo-verify', vanilla=False)
            self.assertNotEqual(code, 0)
            self.assertIn('cannot be combined', error)

    def launch(self, directory, *args, inherited=None, vanilla=True):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:   # a modded launch wants an installed proxy that matches its manifest
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_options_drop_both_variables_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_COLLIDE_MEMO': '1', 'X3M_COLLIDE_MEMO_VERIFY': '1'})
            self.assertEqual(code, 0)
            env = json.loads(output)['env']
            self.assertNotIn('X3M_COLLIDE_MEMO', env)
            self.assertNotIn('X3M_COLLIDE_MEMO_VERIFY', env)

    def test_dry_run_carries_the_switches(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            added = lambda *args: {k: v for k, v in json.loads(self.launch(directory, *args)[1])['env'].items() if k not in baseline['env']}
            code, output, error = self.launch(directory, '--collide-memo')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['command'], baseline['command'])
            self.assertEqual(added('--collide-memo'), {'X3M_COLLIDE_MEMO': '1'})
            self.assertEqual(added('--collide-memo-verify'), {'X3M_COLLIDE_MEMO': '1', 'X3M_COLLIDE_MEMO_VERIFY': '1'})
            self.assertEqual(added('--collide-memo', '--collide-sat-sse2', '--collide-narrow-census', '--collide-box-cull'),
                             {'X3M_COLLIDE_MEMO': '1', 'X3M_COLLIDE_SAT_SSE2': '1', 'X3M_COLLIDE_NARROW_CENSUS': '1', 'X3M_COLLIDE_BOX_CULL': '1'})


if __name__ == '__main__':
    unittest.main()
