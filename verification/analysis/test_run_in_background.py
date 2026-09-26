"""Host checks of the run-in-background patch (src/proxy/run_in_background_sites.h, docs/reverse-engineering/run-in-background.md).

The site header compiled on the host (window, site, target, slot and bit, refusal of a changed or already redirected
window, the one-write decision, the X3M_RUN_IN_BACKGROUND parser), its constants against the site verifier's twin,
the site verifier on the installed executable and its refusal on a changed copy, the row parser, the production wiring,
the schema entry and the launcher option (--dry-run only, never a launch). The Wine fixture is
verification/probe/run_run_in_background_patch.py. No Wine here.
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
import verify_run_in_background_site as verifier  # noqa: E402
from source_text import source_text  # noqa: E402

EXE = Path(verifier.DEFAULT_EXE)

HARNESS = r'''
#include "run_in_background_sites.h"
#include <cstdio>
#include <cstring>
using namespace x3m::run_in_background::sites;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    for (unsigned i = 0; i < window_length; ++i) std::printf("%02x", expected_window[i]);
    std::printf("\n%08lx %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)window_va, (unsigned long)site_va, (unsigned long)target_va,
                (unsigned long)return_va, (unsigned long)input_block_slot_va, (unsigned long)run_in_background_bit);
    unsigned char copy[window_length];
    check(plan(expected_window) == nullptr, "engine window accepted");
    for (unsigned at = 0; at < window_length; ++at) {
        std::memcpy(copy, expected_window, window_length); copy[at] ^= 0x01;
        const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "a changed window byte is refused");
    }
    std::memcpy(copy, expected_window, window_length); copy[site_offset + 1] = 0x00; copy[site_offset + 4] = 0x6f;
    { const char* r = plan(copy); check(r && !std::strcmp(r, "bytes_mismatch"), "an already redirected call is refused"); }
    const Outcome clear = apply(0x70201107u), set = apply(0x70205107u);
    check(!std::strcmp(clear.status, "patched") && clear.write && clear.before == 0x70201107u && clear.after == 0x70205107u, "clear bit: one write");
    check(!std::strcmp(set.status, "already") && !set.write && set.before == set.after && set.after == 0x70205107u, "set bit: nothing written");
    check(apply(0).after == run_in_background_bit && apply(0xffffffffu).after == 0xffffffffu, "only bit 0x4000 changes");
    check(parse_setting("1", 1) == Setting::on && parse_setting("0", 1) == Setting::off && parse_setting("", 0) == Setting::off &&
          parse_setting(static_cast<const char*>(nullptr), 0) == Setting::off && parse_setting(L"1", 1) == Setting::on, "1, 0, unset, wide");
    const char* rejected[] = {"on", "off", "2", "11", "10", " 1", "1 ", "yes", "-"};
    for (const char* t : rejected) check(parse_setting(t, unsigned(std::strlen(t))) == Setting::invalid, t);
    std::printf("run_in_background_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('run_in_background_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_schema():
    spec = importlib.util.spec_from_file_location('run_in_background_schema', ROOT / 'tools/config/schema.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RunInBackgroundCore(unittest.TestCase):
    def test_core_compiled_window_decision_and_parser(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-run-in-background-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'run_in_background_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = run.stdout.splitlines()
            self.assertEqual(lines[-1], 'run_in_background_core checks_failed=0')
            self.assertEqual(bytes.fromhex(lines[0]), verifier.WINDOW)
            self.assertEqual([int(v, 16) for v in lines[1].split()],
                             [verifier.WINDOW_VA, verifier.SITE_VA, verifier.TARGET_VA, verifier.RETURN_VA, verifier.SLOT_VA, verifier.BIT])

    def test_python_twin(self):
        self.assertEqual(verifier.source_constants(source_text(ROOT / 'src/proxy/run_in_background_sites.h')), verifier.EXPECTED_CONSTANTS)
        self.assertEqual(verifier.WINDOW[verifier.SITE_VA - verifier.WINDOW_VA], 0xe8)
        self.assertEqual(len(verifier.WINDOW), verifier.RETURN_VA - verifier.WINDOW_VA)
        self.assertEqual(verifier.SITE_VA // 8, (verifier.RETURN_VA - 1) // 8)  # the five call bytes: one lock cmpxchg8b

    def test_row_parser(self):
        install = verifier.parse_log_line('00:01 run_in_background site=0x004033c9 status=armed reason=ok setting=1 value_before=- value_after=- '
                                          'write=atomic handler=0x6fbf5100')
        self.assertEqual((install['kind'], install['status'], install['write'], install['handler'], install['before']),
                         ('install', 'armed', 'atomic', 0x6fbf5100, None))
        site = verifier.parse_log_line('run_in_background site=0x004033c9 status=patched reason=ok setting=1 value_before=0 value_after=1 '
                                       'flags_before=0x70201107 flags_after=0x70205107')
        self.assertEqual((site['kind'], site['status'], site['before'], site['after'], site['flags_before'], site['flags_after']),
                         ('site', 'patched', 0, 1, 0x70201107, 0x70205107))
        refused = verifier.parse_log_line('run_in_background site=0x004033c9 status=refused reason=no_input_block setting=1 value_before=- '
                                          'value_after=- flags_before=- flags_after=-')
        self.assertEqual((refused['status'], refused['flags_before']), ('refused', None))
        self.assertIsNone(verifier.parse_log_line('run_in_background site=0x004033c9 status=maybe reason=ok setting=1 value_before=0 '
                                                  'value_after=1 flags_before=0x0 flags_after=0x1'))

    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('run_in_background::initialize();'), 1)
        self.assertLess(capture.index('music_keep::initialize();'), capture.index('run_in_background::initialize();'))
        self.assertIn('x3m::run_in_background::shutdown();', source_text(ROOT / 'src/proxy/loader.cpp'))
        self.assertIn('src/proxy/run_in_background.cpp', source_text(ROOT / 'CMakeLists.txt'))
        module = source_text(ROOT / 'src/proxy/run_in_background.cpp')
        for needle in ('L"X3M_RUN_IN_BACKGROUND"', 'install_window_open()', 'executable_verified()', 'sites::plan(current)', 'pin_self()',
                       'engine_patch::claim_call(site_, a.window + sites::site_offset, a.target,', 'engine_patch::restore_call(site_)',
                       'InterlockedExchange(&armed_, 0) == 1', 'InterlockedOr(', 'SetLastError(error);', '"rollback_failed"',
                       'pushfd\n    pushad\n    cld\n    call _x3m_run_in_background_apply\n    popad\n    popfd\n'
                       '    jmp dword ptr [_x3m_run_in_background_continue]',
                       'log("run_in_background site=0x%08lx status=%s reason=%s setting=%s value_before=- value_after=- write=%s "',
                       '"run_in_background site=0x%08lx status=%s reason=ok setting=1 value_before=%u value_after=%u "'):
            self.assertIn(needle, module)
        self.assertNotIn('windows.h', source_text(ROOT / 'src/proxy/run_in_background_sites.h'))
        for forbidden in ('float ', 'double ', 'new ', 'malloc', 'std::string', 'std::vector'):
            self.assertNotIn(forbidden, module)

    def test_schema_entry(self):
        entry = next(e for e in load_schema().SETTINGS if e['key'] == 'run_in_background')
        self.assertEqual((entry['env'], entry['type'], entry['section'], entry['default'], entry['launcher'], entry['developer']),
                         ('X3M_RUN_IN_BACKGROUND', 'bool', 'window', '1', '--run-in-background', False))
        self.assertIn('-runinbg', entry['description'])
        self.assertIn(';run_in_background = 1', source_text(ROOT / 'assets/x3m.ini'))


@unittest.skipUnless(EXE.is_file(), 'installed executable not present')
class RunInBackgroundSite(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_run_in_background_site.py'), '--exe', str(exe)],
                              capture_output=True, text=True, timeout=300, cwd=ROOT / 'verification/probe')

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout[-3000:] + run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'PASS')
        self.assertEqual(report['site_bytes'], 'e8b2f10c00')
        self.assertEqual((report['local_uses'], report['target_callers'], report['interior_branches'], report['overlapping_claims']),
                         (['0x00402d09', '0x00402d16', '0x00403398'], ['0x004033c9'], [], []))
        self.assertEqual([(a['string'], a['value']) for a in report['arguments']],
                         [('/runinbg', 1), ('-runinbg', 1), ('/noruninbg', 0), ('-noruninbg', 0)])

    def test_changed_copy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'X3AP.exe'
            copy.write_bytes(verifier.patched_image(EXE.read_bytes()))
            report = json.loads(self.run_verifier(copy).stdout)
            self.assertEqual(report['result'], 'FAIL')
            self.assertTrue(report['checks']['exe_identity'])
            self.assertFalse(report['checks']['window_bytes'])


class RunInBackgroundLaunchOption(unittest.TestCase):
    NAME = 'X3M_RUN_IN_BACKGROUND'

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
            self.assertEqual(self.value(directory), '1')
            self.assertEqual(self.value(directory, inherited={self.NAME: '0'}), '1')   # a stale value never travels
            self.assertEqual(self.value(directory, '--run-in-background', 'off', inherited={self.NAME: '1'}), '0')
            self.assertEqual(self.value(directory, '--run-in-background', 'on'), '1')
            self.assertIsNone(self.value(directory, vanilla=True, inherited={self.NAME: '1'}))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--run-in-background', 'on'), True, 'cannot be combined with --vanilla'),
                                           (('--run-in-background', 'off'), True, 'cannot be combined with --vanilla'),
                                           (('--run-in-background', 'yes'), False, 'invalid choice')):
                code, _, error = self.launch(directory, *args, vanilla=vanilla)
                self.assertEqual(code, 2, args)
                self.assertIn(message, error)


if __name__ == '__main__':
    unittest.main()
