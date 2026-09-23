"""Host checks of the pause-key-only patch (src/proxy/pause_key_only_core.h).

The core compiled on the host (window, original bytes, the replacement for the
default and a custom key, refusal of a changed or already patched window and
of out-of-range keys, the X3M_PAUSE_KEY parser), its bytes against the site
verifier's Python twin, the site verifier on the installed executable and its
refusal on a patched copy, the install-line parser, the production wiring and
the --pause-key-only / --pause-key launcher options (--dry-run only, never a
launch). No Wine.
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
VERIFIER_PATH = ROOT / 'verification/results/pause-dialog-input/verify_pause_sites.py'
_spec = importlib.util.spec_from_file_location('verify_pause_sites', VERIFIER_PATH)
verifier = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(verifier)
EXE = Path(verifier.DEFAULT)
CUSTOM_KEY = 0x2c

HARNESS = r'''
#include "pause_key_only_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::pause_key_only::core;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    unsigned char out[site_length], copy[window_length];
    // The engine window is accepted with the default and a custom key; the replacement is printed for the Python comparison.
    check(plan(window, default_key, out) == nullptr, "default key accepted on the engine window"); hex(out, site_length);
    check(plan(window, 0x2c, out) == nullptr, "custom key accepted"); hex(out, site_length);
    hex(original, site_length);
    // A changed site byte, a changed byte outside the site (the exit's mask), a changed branch and an already patched window are refused.
    const unsigned offsets[] = {site_offset, site_offset + 11, 1, 67, window_length - 1};
    for (unsigned at : offsets) {
        std::memcpy(copy, window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy, default_key, out);
        check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, window, window_length); encode_site(default_key, copy + site_offset);
    { const char* r = plan(copy, default_key, out); check(r && !std::strcmp(r, "bytes_mismatch"), "an already patched window is refused"); }
    // Keys: 0 (no key) and above 0x1fff are refused before the window is looked at.
    const std::uint32_t bad[] = {0u, 0x1000u, 0x2000u, 0xffffu, 0x101b5u};
    for (std::uint32_t k : bad) { const char* r = plan(window, k, out); check(r && !std::strcmp(r, "bad_key") && !encode_site(k, out), "bad key refused"); }
    check(plan(window, 1, out) == nullptr && plan(window, max_key, out) == nullptr && out[3] == 0xff && out[4] == 0x1f, "range ends accepted");
    // Parser: 0x-hex or decimal, nothing else, in [1, 0x1fff]; narrow and wide.
    std::uint32_t k = 0;
    check(parse_key("0x1b5", &k) && k == 0x1b5 && parse_key("0X1B5", &k) && k == 0x1b5 && parse_key("437", &k) && k == 0x1b5, "parse 0x1b5");
    check(parse_key(L"0x1b5", &k) && k == 0x1b5 && parse_key(L"44", &k) && k == 44, "parse wide");
    check(parse_key("0x1fff", &k) && k == 0x1fff && parse_key("1", &k) && k == 1 && parse_key("0x1001", &k) && k == 0x1001, "parse range ends");
    k = 7;
    const char* rejected[] = {"", "0x", "0", "0x0", "0x1000", "4096", "0x2000", "8192", "1b5", " 0x1b5", "0x1b5 ", "-1", "+5", "0x1g", "99999999999"};
    for (const char* t : rejected) check(!parse_key(t, &k) && k == 7, t);
    check(!parse_key(static_cast<const char*>(nullptr), &k), "null");
    std::printf("pause_key_only_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('pause_key_only_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PauseCore(unittest.TestCase):
    def test_core_compiled_bytes_refusals_and_parser(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-pause-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'pause_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'pause_key_only_core checks_failed=0')
            self.assertEqual(bytes.fromhex(lines[0]), bytes.fromhex(verifier.PATCH_NEW))
            self.assertEqual(bytes.fromhex(lines[0]), verifier.encode_patch(0x1b5))
            self.assertEqual(bytes.fromhex(lines[1]), verifier.encode_patch(CUSTOM_KEY))
            self.assertEqual(bytes.fromhex(lines[1]), bytes.fromhex('66 81 fe 2c 00 75 05 66 39 de 75 27'))
            self.assertEqual(bytes.fromhex(lines[2]), bytes.fromhex(verifier.PATCH_OLD))

    def test_python_twin(self):
        self.assertEqual(verifier.encode_patch(0x1b5), bytes.fromhex('66 81 fe b5 01 75 05 66 39 de 75 27'))
        self.assertEqual(verifier.encode_patch(0x11b5)[3:5], b'\xb5\x11')
        for bad in (0, 0x1000, 0x2000, -1):
            with self.assertRaises(ValueError):
                verifier.encode_patch(bad)

    def test_install_line_parser(self):
        row = verifier.parse_install_line('00:01 pause_key_only patched=1 key=0x1b5 reason=ok requested=1 site=0x004043a5 write=plain')
        self.assertEqual(row, {'patched': True, 'key': 0x1b5, 'reason': 'ok', 'requested': True, 'site': 0x4043a5, 'write': 'plain'})
        refused = verifier.parse_install_line('pause_key_only patched=0 key=0x1b5 reason=bytes_mismatch requested=1 site=0x004043a5 write=none')
        self.assertEqual((refused['patched'], refused['reason']), (False, 'bytes_mismatch'))
        self.assertEqual(verifier.parse_install_line('pause_key_only patched=0 key=0x0 reason=bad_key requested=1 site=0x004043a5 write=none')['key'], 0)
        self.assertIsNone(verifier.parse_install_line('pause_key_only patched=1 key=0x1b5 reason=ok'))

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('pause_key_only::initialize();'), 1)
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('pause_key_only::initialize();'))
        self.assertIn('x3m::pause_key_only::shutdown();', (ROOT / 'src/proxy/loader.cpp').read_text())
        self.assertIn('src/proxy/pause_key_only.cpp', (ROOT / 'CMakeLists.txt').read_text())
        module = (ROOT / 'src/proxy/pause_key_only.cpp').read_text()
        for needle in ('L"X3M_PAUSE_KEY_ONLY"', 'L"X3M_PAUSE_KEY"', "length == 1 && setting[0] == L'1'", 'install_window_open()', 'executable_verified()',
                       'plan(current, key, replacement)', 'memcmp(back, original, site_length)', 'patch_rolled_back', 'rollback_unprotected', 'rollback_failed', 'restore_not_owned', 'SetLastError(error);',
                       'log("pause_key_only patched=%u key=0x%lx reason=%s requested=%u site=0x%08lx write=%s"'):
            self.assertIn(needle, module)
        for forbidden in ('float ', 'double '):
            self.assertNotIn(forbidden, module)


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class PauseSites(unittest.TestCase):
    def run_verifier(self, exe, *args):
        return subprocess.run([sys.executable, str(VERIFIER_PATH), str(exe), *args], capture_output=True, text=True, timeout=300)

    def test_installed_executable_and_dll_bytes(self):
        run = self.run_verifier(EXE, '--key', hex(CUSTOM_KEY))
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['verdict'], 'PASS')
        dll = report['dll_patch']
        self.assertTrue(dll['window_matches_image'] and dll['original_matches_note'] and dll['default_key_bytes_match_note'])
        self.assertEqual(dll['keys'][hex(CUSTOM_KEY)]['decode'], ['cmp si,0x2c', 'jne 0x4043b1', 'cmp si,bx', 'jne 0x4043d8'])

    def test_patched_copy_and_bad_key_fail(self):
        data = bytearray(EXE.read_bytes())
        text = [s for s in verifier.sections(bytes(data)) if s[0] == '.text'][0]
        offset = verifier.PATCH_VA - text[1] + text[3]
        data[offset:offset + 12] = verifier.encode_patch(0x1b5)
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(bytes(data))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['verdict'], 'FAIL')
            self.assertFalse(report['dll_patch']['window_matches_image'])
            self.assertFalse(report['patch_old_matches'])
        run = self.run_verifier(EXE, '--key', '0x2000')
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(json.loads(run.stdout)['dll_patch']['keys']['0x2000']['refused'], 'bad_key')


class PauseLaunchOption(unittest.TestCase):
    NAMES = ('X3M_PAUSE_KEY_ONLY', 'X3M_PAUSE_KEY')

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

    def pause_env(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return {k: v for k, v in json.loads(output)['env'].items() if k in self.NAMES}

    def test_default_opt_out_vanilla_and_custom_key(self):
        stale = {'X3M_PAUSE_KEY_ONLY': '1', 'X3M_PAUSE_KEY': '0x99'}
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.pause_env(directory), {'X3M_PAUSE_KEY_ONLY': '1'})
            self.assertEqual(self.pause_env(directory, inherited=stale), {'X3M_PAUSE_KEY_ONLY': '1'})   # a stale key never travels
            self.assertEqual(self.pause_env(directory, '--pause-key-only'), {'X3M_PAUSE_KEY_ONLY': '1'})
            self.assertEqual(self.pause_env(directory, '--no-pause-key-only', inherited=stale), {})
            self.assertEqual(self.pause_env(directory, vanilla=True, inherited=stale), {})
            self.assertEqual(self.pause_env(directory, '--no-pause-key-only', vanilla=True, inherited=stale), {})
            self.assertEqual(self.pause_env(directory, '--pause-key', '0x2c'), {'X3M_PAUSE_KEY_ONLY': '1', 'X3M_PAUSE_KEY': '0x2c'})
            self.assertEqual(self.pause_env(directory, '--pause-key', '437'), {'X3M_PAUSE_KEY_ONLY': '1', 'X3M_PAUSE_KEY': '0x1b5'})
            self.assertEqual(self.pause_env(directory, '--pause-key-only', '--pause-key', '0x11b5'), {'X3M_PAUSE_KEY_ONLY': '1', 'X3M_PAUSE_KEY': '0x11b5'})

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--no-pause-key-only', '--pause-key', '0x2c'), False, 'needs the pause-key-only patch'),
                                           (('--pause-key', '0x2c'), True, 'cannot be combined with --vanilla'),
                                           (('--pause-key-only',), True, 'cannot be combined with --vanilla'),
                                           (('--pause-key-only', '--pause-key', '0x2c'), True, 'cannot be combined with --vanilla'),
                                           (('--pause-key', '0'), False, 'out of range'),
                                           (('--pause-key', '0x1000'), False, 'out of range'),
                                           (('--pause-key', '0x2000'), False, 'out of range'),
                                           (('--pause-key', 'pause'), False, 'not an integer')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            on = json.loads(self.launch(directory)[1])
            off = json.loads(self.launch(directory, '--no-pause-key-only')[1])
            self.assertEqual(on['command'], off['command'])
            self.assertEqual({k: v for k, v in on['env'].items() if k not in off['env']}, {'X3M_PAUSE_KEY_ONLY': '1'})


if __name__ == '__main__':
    unittest.main()
