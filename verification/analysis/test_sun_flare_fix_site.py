"""Host checks of the lens-flare collector fix (src/proxy/sun_flare_fix_sites.h).

The site header compiled on the host (window, claimed span, stub bytes and
encoding, refusal of a changed or already patched window, the saturation model
against the engine's truncation, the X3M_SUN_FLARE_FIX parser), the bytes
against the site verifier's Python twin, the site verifier on the installed
executable and its refusal on a patched copy, the install/restore line
parsers, the production wiring and the --sun-flare-fix launcher option
(--dry-run only, never a launch). No Wine.
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
import verify_sun_flare_site as verifier  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "sun_flare_fix_sites.h"
#include <cstdio>
#include <cstring>
using namespace x3m::sun_flare_fix::sites;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    hex(expected_window, window_length); hex(expected_site, site_length); hex(stub_code, stub_code_length);
    std::printf("%08lx %08lx %08lx %08lx %08lx\n", (unsigned long)window_va, (unsigned long)site_va, (unsigned long)jge_va, (unsigned long)y_test_va, (unsigned long)off_screen_va);
    unsigned char copy[window_length];
    check(plan(expected_window) == nullptr, "engine window accepted");
    for (unsigned at = 0; at < window_length; ++at) {
        std::memcpy(copy, expected_window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, expected_window, window_length); copy[site_offset] = 0xe9;
    { const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "an already claimed window is refused"); }
    unsigned char stub[stub_length];
    encode_stub(0x12345678u, stub);
    check(!std::memcmp(stub, stub_code, stub_code_length) && stub[20] == 0x78 && stub[21] == 0x56 && stub[22] == 0x34 && stub[23] == 0x12, "stub + little-endian slot");
    // The model: identical whenever (p >> 16) fits int32 or the product is negative; 0x7fffffff when it reaches 2^31.
    const long long cases[] = {0, 0x8000, 0x7fffffffLL << 16, (0x7fffffffLL << 16) + 0xffff, 0x80000000LL << 16, 0x7fffffffffffLL, 0x7fffffffffffffffLL,
                               -1, -(0x80000000LL << 16), -(0x80000000LL << 16) - 1, (long long)(-0x7fffffffffffffffLL - 1), 1234567890123LL, -1234567890123LL};
    for (long long p : cases) {
        const long long shifted = p >> 16;
        const bool fits = shifted >= -2147483648LL && shifted <= 2147483647LL;
        if (fits || p < 0) check(fixed_bound(p) == vanilla_bound(p), "no change without a positive overflow");
        else check(fixed_bound(p) == 0x7fffffff, "a positive overflow saturates");
        if (fits) check(vanilla_bound(p) == (int)shifted, "the engine's SHRD is exact while it fits");
    }
    check(vanilla_bound(0x80000000LL << 16) < 0, "the engine wraps negative at 2^31 (the bug)");
    Mode m = Mode::off;
    check(parse_mode("on", &m) && m == Mode::on && parse_mode("off", &m) && m == Mode::off, "on/off");
    check(parse_mode(L"on", &m) && m == Mode::on, "wide");
    const char* rejected[] = {"", "On", "OFF", " on", "on ", "1", "0", "true", "yes", "onn", "o"};
    for (const char* t : rejected) { m = Mode::on; check(!parse_mode(t, &m) && m == Mode::on, t); }
    check(!parse_mode(static_cast<const char*>(nullptr), &m), "null");
    check(default_mode == Mode::off && !std::strcmp(mode_name(Mode::on), "on") && !std::strcmp(mode_name(Mode::off), "off") && setting_capacity == 32, "default, names, capacity");
    check(!std::strcmp(claim_spec.name, "lens_collector_x_bound") && claim_spec.address == site_va && claim_spec.length == 6 && claim_spec.ret_pop == 0 &&
          claim_spec.rel32_offset == 0 && !std::memcmp(claim_spec.expected, expected_site, 6), "claim spec");
    std::printf("sun_flare_fix_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('sun_flare_fix_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SunFlareFixCore(unittest.TestCase):
    def test_core_compiled_bytes_model_and_parser(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-sun-flare-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'sun_flare_fix_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'sun_flare_fix_core checks_failed=0')
            self.assertEqual([bytes.fromhex(line) for line in lines[0:3]], [verifier.WINDOW, verifier.SITE, verifier.STUB_CODE])
            self.assertEqual([int(v, 16) for v in lines[3].split()],
                             [verifier.WINDOW_VA, verifier.SITE_VA, verifier.JGE_VA, verifier.Y_TEST_VA, verifier.OFF_SCREEN_VA])

    def test_python_twin_and_vectors(self):
        self.assertEqual(verifier.source_constants((ROOT / 'src/proxy/sun_flare_fix_sites.h').read_text()), verifier.EXPECTED_CONSTANTS)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA:][:6], verifier.SITE)
        self.assertEqual(verifier.SITE_VA // 8, (verifier.SITE_VA + 4) // 8)   # the five patch bytes: one lock cmpxchg8b
        self.assertTrue(all(row['ok'] for row in verifier.emulate()))
        # Run309 at 32:9, F 0x471c: the bound wraps from z_crit = 2^32 / (W tan) ~ 1.35e9 (field-of-view.md section 9).
        w, t = verifier.W_32_9, verifier.tan16(0x471c)
        self.assertEqual(verifier.gate(w, t, 0, 0, 1_340_000_000)[0], 'on')
        self.assertEqual(verifier.gate(w, t, 0, 0, 1_360_000_000)[0], 'off_x')
        self.assertEqual(verifier.gate(w, t, 0, 0, 1_360_000_000, fixed=True)[0], 'on')

    def test_line_parsers(self):
        row = verifier.parse_log_line('00:01 sun_flare_fix site=0047e391 status=patched reason=ok mode=on setting=on write=atomic stub=00a300d0')
        self.assertEqual(row, {'site': 0x47e391, 'status': 'patched', 'reason': 'ok', 'mode': 'on', 'setting': 'on', 'write': 'atomic', 'stub': 0xa300d0, 'patched': True})
        off = verifier.parse_log_line('sun_flare_fix site=0047e391 status=off reason=off mode=off setting=- write=none stub=00000000')
        self.assertEqual((off['patched'], off['status'], off['setting']), (False, 'off', '-'))
        self.assertEqual(verifier.parse_log_line('sun_flare_fix site=0047e391 status=refused reason=too_long mode=- setting=? write=none stub=00000000')['mode'], '-')
        self.assertIsNone(verifier.parse_log_line('sun_flare_fix site=0047e391 status=patched reason=ok mode=on setting=on write=atomic'))
        self.assertIsNone(verifier.parse_log_line('sun_flare_fix site=0047e391 status=maybe reason=ok mode=on setting=on write=atomic stub=00000000'))
        restore = verifier.parse_restore_line('sun_flare_fix_restore site=0047e391 status=restored found=e9321d5b00c8 registered=0')
        self.assertEqual(restore, {'site': 0x47e391, 'status': 'restored', 'found': bytes.fromhex('e9321d5b00c8'), 'registered': False})
        self.assertIsNone(verifier.parse_restore_line('sun_flare_fix_restore site=0047e391 status=restore_not_owned found=-- registered=1')['found'])
        self.assertIsNone(verifier.parse_restore_line('sun_flare_fix_restore site=0047e391 status=restored found=e932 registered=0'))

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('sun_flare_fix::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('sun_flare_fix::initialize();'))
        self.assertIn('if (reserved == nullptr) x3m::sun_flare_fix::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/sun_flare_fix.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/sun_flare_fix.cpp').read_text()
        for needle in ('L"X3M_SUN_FLARE_FIX"', '"too_long"', '"invalid_setting"', 'install_window_open()', 'executable_verified()', 'sites::plan(current)',
                       'engine_patch::claim(site_, spec)', 'engine_patch::store_pointer(slot, *site_.entry)', 'engine_patch::push_front(site_',
                       'take_back("chain_failed")', 'return stub_ != 0;', 'take_back("readback_mismatch")', '"rollback_failed"', '"patched_unverified"', '"restore_not_owned"',
                       'log_handle()', 'WriteFile(handle', 'SetLastError(error);',
                       'log("sun_flare_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx"',
                       '"sun_flare_fix_restore site=%08lx status=%s found=%s registered=%u\\n"'):
            self.assertIn(needle, module)
        self.assertNotIn('windows.h', (ROOT / 'src/proxy/sun_flare_fix_sites.h').read_text())
        for forbidden in ('float ', 'double ', 'GetModuleHandleEx'):   # no FP, and no pin needed: the stub holds no pointer into the DLL
            self.assertNotIn(forbidden, module)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class SunFlareFixSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_sun_flare_site.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=300, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(report['site_bytes'], '0facd0103bc8')
        self.assertEqual((report['raw_branch_hits_not_interior'], report['incoming_window_branches'], report['dword_refs'], report['overlapping_claims']),
                         ([], [], 0, []))
        self.assertEqual(report['gate_branch_targets'], ['0x47e315', '0x47e354', '0x47e356', '0x47e3b9', '0x47e3bb', '0x47e5b6'])

    def test_patched_copy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'])
            self.assertFalse(report['checks']['window_bytes'])


class SunFlareFixLaunchOption(unittest.TestCase):
    NAME = 'X3M_SUN_FLARE_FIX'

    def launch(self, directory, *args, inherited=None, vanilla=False):
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

    def value(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env'].get(self.NAME)

    def test_default_explicit_and_vanilla(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.value(directory), 'on')
            self.assertEqual(self.value(directory, inherited={self.NAME: 'off'}), 'on')   # a stale value never travels
            self.assertEqual(self.value(directory, '--sun-flare-fix', 'off', inherited={self.NAME: 'on'}), 'off')
            self.assertEqual(self.value(directory, '--sun-flare-fix', 'on'), 'on')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: 'on'}))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--sun-flare-fix', 'on'), True, 'cannot be combined with --vanilla'),
                                           (('--sun-flare-fix', 'off'), True, 'cannot be combined with --vanilla'),
                                           (('--sun-flare-fix', 'On'), False, 'invalid choice'),
                                           (('--sun-flare-fix', '1'), False, 'invalid choice')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            on = json.loads(self.launch(directory, '--sun-flare-fix', 'on')[1])
            off = json.loads(self.launch(directory, '--sun-flare-fix', 'off')[1])
            self.assertEqual(on['command'], off['command'])
            self.assertEqual({k: v for k, v in off['env'].items() if on['env'].get(k) != v}, {self.NAME: 'off'})


if __name__ == '__main__':
    unittest.main()
