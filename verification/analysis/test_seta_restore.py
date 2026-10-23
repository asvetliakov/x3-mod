"""SETA restore after a gate transit (X3M_SETA_RESTORE, docs/architecture/seta-restore.md): the arm/cancel state machine
and the return scan of src/proxy/seta_restore_core.h compiled on the host (verification/probe/seta_restore_host.cpp), the
launcher flag on hermetic `manage.py launch --dry-run` runs (nothing launched), the config entry and its generated views,
and the production wiring (the read site, the audited roots, the build). No game, no Wine."""
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import verification.analysis.test_launcher_defaults as launcher_defaults

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402


class Core(unittest.TestCase):
    def test_state_machine_and_return_scan(self):
        compiler = shutil.which('c++') or shutil.which('clang++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-seta-restore-') as d:
            exe = Path(d) / 'seta_restore_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/seta_restore_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('checks PASS', run.stdout)
            self.assertGreaterEqual(int(re.search(r'checks=(\d+)', run.stdout).group(1)), 70)


class Launcher(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = launcher_defaults.load_manage()
        cls.directory = tempfile.TemporaryDirectory()
        game = Path(cls.directory.name) / 'game'
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
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
        return 0, json.loads(output.getvalue())['env'], error.getvalue()

    def test_default_on_and_opt_out(self):
        code, env, error = self.launch()
        self.assertEqual(code, 0, error)
        self.assertEqual(env['X3M_SETA_RESTORE'], '1')
        self.assertEqual(self.launch('--seta-restore')[1]['X3M_SETA_RESTORE'], '1')
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--no-seta-restore')[1])
        # a stale shell value never decides: on unless opted out, dropped when opted out
        self.assertEqual(self.launch(inherited={'X3M_SETA_RESTORE': '0'})[1]['X3M_SETA_RESTORE'], '1')
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--no-seta-restore', inherited={'X3M_SETA_RESTORE': '1'})[1])

    def test_not_forwarded_under_vanilla(self):
        code, env, error = self.launch(vanilla=True, inherited={'X3M_SETA_RESTORE': '1'})
        self.assertEqual(code, 0, error)
        self.assertNotIn('X3M_SETA_RESTORE', env)
        code, _, error = self.launch('--seta-restore', vanilla=True)
        self.assertEqual(code, 2)
        self.assertIn('--seta-restore cannot be combined with --vanilla', error)

    def test_player_mode(self):
        # --config: the DLL's default (1) stands in; the opt-out is sent empty, which the read site takes as off.
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--config')[1])
        self.assertEqual(self.launch('--config', '--no-seta-restore')[1]['X3M_SETA_RESTORE'], '')


class Config(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['seta_restore']
        self.assertEqual((e['env'], e['type'], e['default'], e['launcher'], e['developer'], e['env_only']),
                         ('X3M_SETA_RESTORE', 'bool', '1', '--seta-restore', False, False))
        # the section of the flight options (the ship steering and gate approach live in [engine])
        self.assertEqual(e['section'], schema.BY_KEY['ship_steering']['section'])
        self.assertEqual(e['section'], 'engine')

    def test_generated_views(self):
        header = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertRegex(header, r'\{"X3M_SETA_RESTORE", "seta_restore", Type::Bool, "1",')
        self.assertIn('constexpr bool seta_restore = true;', header)
        self.assertRegex((ROOT / 'assets/x3m.ini').read_text(), r'(?m)^;seta_restore = 1$')

    def test_read_site(self):
        source = (ROOT / 'src/proxy/seta_restore.cpp').read_text()
        self.assertIn('x3m::config::get(L"X3M_SETA_RESTORE", setting, 4)', source)
        self.assertIn("length == 1 && setting[0] == L'1'", source)  # anything but exactly 1 (an empty opt-out too) is off


class Wiring(unittest.TestCase):
    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/seta_restore.cpp').read_text()
        # every window of the core header is compared before the claim
        for window in ('hook_bytes', 'case_rest', 'dispatch_head', 'thunk_body', 'table_entry', 'native_args', 'native_ip',
                       'return_scan', 'wrapper_body', 'stop_call'):
            self.assertRegex(source, r'verify_bytes\([a-z_]+, %s,' % window, window)
        self.assertLess(source.index('windows_match()) {'), source.index('engine_patch::claim(hook_site_, spec)'))
        self.assertLess(source.index('engine_patch::claim(hook_site_, spec)'), source.index('inject_attach(&consumer_'))
        self.assertIn('engine_patch::restore(hook_site_)', source)  # rollback of the hook when the attach fails
        roots = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        for symbol in ('_x3m_seta_restore_ti_stub', '_x3m_seta_restore_factor_write'):
            self.assertIn(f"'{symbol}'", roots)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/seta_restore.cpp'), 2)  # the DLL and the -fno-exceptions list
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('seta_restore::initialize();', capture)
        self.assertIn('seta_restore::present(ctx.frame);', capture)
        chase = (ROOT / 'src/proxy/chase_transition.cpp').read_text()
        # the injection site is claimed once for both consumers
        self.assertEqual(chase.count('restore_sites_install(restore_specs)'), 1)
        self.assertIn('inject_acquire(&inject_status)', chase)


if __name__ == '__main__':
    unittest.main()
