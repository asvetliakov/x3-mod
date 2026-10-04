"""Host checks of the engine-effects call redirects (src/proxy/engine_effects_sites.h, engine_effects_patch.cpp).

The site header compiled on the host (both windows, refusal of changed or already redirected windows, the
X3M_ENGINE_EFFECTS parser), the bytes against the site verifier's Python twin, the verifier on the installed
executable and its refusal on corrupted copies, the engine_effects_patch row parser, the production wiring (load
order, detach, CMake, x87 roots, verifier registration) and the Wine fixture's record bound to the production sources
it linked. No Wine.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_engine_effects_sites as verifier  # noqa: E402
from source_text import source_text

EXE = Path(verifier.DEFAULT_EXE)
RECORD = ROOT / 'verification/results/bottle-X3/engine-effects-patch.json'

HARNESS = r'''
#include "engine_effects_sites.h"
#include <cstdio>
#include <cstring>
using namespace x3m::engine_effects_patch::sites;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    hex(expected_window_a, window_a_length); hex(expected_window_b, window_b_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)window_a_va, (unsigned long)a_site_va, (unsigned long)target_a_va,
                (unsigned long)window_b_va, (unsigned long)b_site_va, (unsigned long)target_b_va);
    check(plan_a(expected_window_a) == nullptr && plan_b(expected_window_b) == nullptr, "engine windows accepted");
    unsigned char a[window_a_length], b[window_b_length];
    for (unsigned at = 0; at < window_a_length; ++at) {
        std::memcpy(a, expected_window_a, window_a_length); a[at] ^= 0x01;
        const char* r = plan_a(a); check(r && !std::strcmp(r, "bytes_mismatch_a"), "a changed A byte is refused");
    }
    for (unsigned at = 0; at < window_b_length; ++at) {
        std::memcpy(b, expected_window_b, window_b_length); b[at] ^= 0x80;
        const char* r = plan_b(b); check(r && !std::strcmp(r, "bytes_mismatch_b"), "a changed B byte is refused");
    }
    std::memcpy(a, expected_window_a, window_a_length); a[site_a_offset + 4] = 0x10;   // A already redirected
    std::memcpy(b, expected_window_b, window_b_length); b[site_b_offset + 4] = 0x10;   // B already redirected
    check(plan_a(a) && plan_b(b), "redirected windows are refused");
    Mode m = Mode::plumes;
    check(parse_mode("native", &m) && m == Mode::native && parse_mode("off", &m) && m == Mode::off &&
          parse_mode("plumes", &m) && m == Mode::plumes, "the three modes");
    check(parse_mode(L"off", &m) && m == Mode::off && parse_mode(L"plumes", &m) && m == Mode::plumes, "wide");
    m = Mode::native;
    check(!parse_mode(L"Off", &m) && !parse_mode(L"PLUMES", &m) && !parse_mode("oFf", &m) && m == Mode::native, "mixed case refused");
    check(!parse_mode("Off", 3, &m) && parse_mode("off", 3, &m) && m == Mode::off, "the draw-path form agrees");
    const char* rejected[] = {"", "Native", "OFF", " off", "off ", "plume", "plumess", "on", "0", "1", "none", "off,plumes", "native\n"};
    for (const char* t : rejected) { m = Mode::plumes; check(!parse_mode(t, &m) && m == Mode::plumes, t); }
    check(!parse_mode(static_cast<const char*>(nullptr), &m), "null");
    check(!std::strcmp(mode_name(Mode::native), "native") && !std::strcmp(mode_name(Mode::off), "off") &&
          !std::strcmp(mode_name(Mode::plumes), "plumes") && default_mode == Mode::plumes, "names and default");
    std::printf("engine_effects_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


class EngineEffectsCore(unittest.TestCase):
    def test_core_compiled_windows_refusals_and_parser(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-engine-effects-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'engine_effects_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'engine_effects_core checks_failed=0')
            self.assertEqual([bytes.fromhex(line) for line in lines[:2]], [verifier.WINDOW_A, verifier.WINDOW_B])
            self.assertEqual([int(v, 16) for v in lines[2].split()], [verifier.WINDOW_A_VA, verifier.SITE_A_VA, verifier.TARGET_A_VA,
                                                                       verifier.WINDOW_B_VA, verifier.SITE_B_VA, verifier.TARGET_B_VA])

    def test_python_twin(self):
        self.assertEqual(verifier.source_constants(source_text(ROOT / 'src/proxy/engine_effects_sites.h')), verifier.EXPECTED_CONSTANTS)

    def test_log_row_parser(self):
        row = verifier.parse_log_line('t=1 engine_effects_patch site=B va=0041482c state=active reason=ok mode=plumes setting=plumes write=plain')
        self.assertEqual((row['site'], row['va'], row['state'], row['mode'], row['write'], row['active']),
                         ('B', 0x41482c, 'active', 'plumes', 'plain', True))
        row = verifier.parse_log_line('engine_effects_patch site=A va=004147eb state=invalid_setting reason=invalid_setting mode=- setting=on write=none')
        self.assertEqual((row['mode'], row['setting'], row['active']), ('-', 'on', False))
        for bad in ('engine_effects_patch site=C va=004147eb state=active reason=ok mode=off setting=off write=atomic',
                    'engine_effects_patch site=A va=004147eb state=active reason=ok mode=on setting=on write=atomic',
                    'engine_effects_patch site=A va=004147eb state=active reason=ok mode=off setting=off write=lock',
                    'engine_effects_patch site=A va=4147eb state=active reason=ok mode=off setting=off write=atomic'):
            self.assertIsNone(verifier.parse_log_line(bad), bad)

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('engine_effects_patch::initialize();'), 1)
        self.assertLess(capture.index('lod_occlusion::initialize();'), capture.index('engine_effects_patch::initialize();'))
        self.assertLess(capture.index('engine_effects_patch::initialize();'), capture.index('fov::initialize();'))
        self.assertIn('if (reserved == nullptr) x3m::engine_effects_patch::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/engine_effects_patch.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/engine_effects_patch.cpp')
        for needle in ('L"X3M_ENGINE_EFFECTS"', 'executable_verified()', 'engine_patch::install_window_open()', 'GET_MODULE_HANDLE_EX_FLAG_PIN',
                       'engine_patch::claim_call(site_[0]', 'engine_patch::claim_call(site_[1]', 'engine_patch::restore_call(site_[0])',
                       'cmp word ptr [eax+0x48], 7', 'mov eax, dword ptr [esp+0x14]', 'mov eax, dword ptr [esp+0x10]', 'ret 0x10',
                       'jmp dword ptr [_x3m_engine_effects_continue_a]', 'jmp dword ptr [_x3m_engine_effects_continue_b]', 'SetLastError(error)'):
            self.assertIn(needle, module, needle)
        # B is claimed only after A's read-back; a B failure restores A.
        self.assertLess(module.index('confirm(site_[0])'), module.index('engine_patch::claim_call(site_[1]'))
        self.assertLess(module.index('engine_patch::claim_call(site_[1]'), module.index('engine_patch::restore_call(site_[0])'))
        x87 = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_engine_effects_stub_a', '_x3m_engine_effects_stub_b'", x87)
        registry = (ROOT / 'verification/results/executable-identity/run_verifiers.py').read_text()
        self.assertIn("'verify_engine_effects_sites']", registry)
        self.assertIn("'verify_engine_effects_sites': 0x004147ec, 'verify_engine_effects_sites@b': 0x0041482d", registry)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class EngineEffectsVerifier(unittest.TestCase):
    def test_installed_executable_passes(self):
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', {k: v for k, v in report['checks'].items() if not v})
        self.assertEqual(report['dword_refs'], [])
        self.assertEqual(report['raw_branch_hits_not_interior'], [])

    def test_corrupted_copies_fail(self):
        import exe_identity
        sys.path.insert(0, str(ROOT / 'verification/results/executable-identity'))
        import run_verifiers
        data = EXE.read_bytes()
        for va, failed in ((0x4147ec, {'window_a_bytes', 'site_a_call'}), (0x41482d, {'window_b_bytes', 'site_b_call'}),
                           (0x4147cb, {'window_a_bytes'})):
            copy = run_verifiers.patched(data, va)
            self.assertTrue(exe_identity.identity_ok(copy))
            report = verifier.verify(copy)
            self.assertEqual(report['result'], 'FAIL')
            self.assertEqual({k for k, ok in report['checks'].items() if not ok}, failed, hex(va))


@unittest.skipUnless(RECORD.is_file(), 'fixture record not present')
class EngineEffectsFixtureRecord(unittest.TestCase):
    def setUp(self):
        self.record = json.loads(RECORD.read_text())

    def test_passed_in_x3(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertFalse(r['game_launched'])
        self.assertEqual(r['bottle']['name'], 'X3')
        self.assertEqual((r['exit_status'], r['check_count'], r['pass_count']), (0, len(r['checks']), len(r['checks'])))
        self.assertEqual(r['result'], {'checks': len(r['checks']), 'failures': 0})

    def test_bound_to_its_production_sources(self):
        import run_engine_effects_patch as runner
        source = self.record['source']
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_engine_effects_patch.py')

    def test_paths_and_rows(self):
        names = {c['name'] for c in self.record['checks']}
        for name in ('ship_skips_both', 'non_ship_forwarded_unchanged', 'vanilla_after_restore', 'b_target_mismatch_rolls_back_a',
                     'b_protect_failed_rolls_back_a', 'a_rollback_failed_registered', 'foreign_bytes_not_restored', 'late_claim_refused',
                     'changed_window_a_refused', 'changed_window_b_refused', 'installed_a_atomic_b_plain'):
            self.assertIn(name, names)
        active = {(row['site'], row['write'], row['mode']) for row in self.record['rows'] if row['active']}
        self.assertEqual(active, {('A', 'atomic', 'off'), ('B', 'plain', 'off'), ('A', 'atomic', 'plumes'), ('B', 'plain', 'plumes')})
        self.assertIn('unset', {run['phase'] for run in self.record['runs']})  # unset = the default plumes, installed
        ships = [run for run in self.record['runs'] if run['object'] == 'ship' and run['phase'] in ('off', 'plumes', 'unset')]
        self.assertTrue(ships and all(run['calls_a'] == '0' and run['calls_b'] == '0' and run['exit_esp_delta'] == '0' for run in ships))


if __name__ == '__main__':
    unittest.main()
