"""Host tests of the TAA image defaults (user decision after run 27, 2026-09-16):
tools/manage.py always forwards X3M_TAA_MIP_BIAS=-0.5 and X3M_TAA_SHARPEN=0.75 in
TAA mode, an explicit 0 still disables either, both are dropped outside TAA mode
even when inherited, and the DLL falls back to the same pair only with TAA on.
No game, no Wine."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TAA = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']


def load_manage():
    spec = importlib.util.spec_from_file_location('taa_defaults_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TaaImageDefaultsLaunch(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
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

    def env(self, directory, *args, inherited=None):
        code, output, error = self.launch(directory, *args, inherited=inherited)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_taa_mode_forwards_both_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA)
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-0.5')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.75')

    def test_defaults_override_a_stale_shell_value(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, inherited={'X3M_TAA_MIP_BIAS': '-3', 'X3M_TAA_SHARPEN': '1'})
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-0.5')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.75')

    def test_explicit_values_win_and_zero_disables(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-mip-bias', '0', '--taa-sharpen', '0')
            self.assertEqual(float(env['X3M_TAA_MIP_BIAS']), 0.0)
            self.assertEqual(float(env['X3M_TAA_SHARPEN']), 0.0)
            env = self.env(directory, *TAA, '--taa-mip-bias', '-1', '--taa-sharpen', '0.5')
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-1.0')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.5')

    def test_without_taa_both_are_dropped_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', inherited={'X3M_TAA_MIP_BIAS': '-0.5', 'X3M_TAA_SHARPEN': '0.75'})
            self.assertNotIn('X3M_TAA_MIP_BIAS', env)
            self.assertNotIn('X3M_TAA_SHARPEN', env)

    def test_options_still_require_taa(self):
        with tempfile.TemporaryDirectory() as directory:
            for option in ('--taa-mip-bias', '--taa-sharpen'):
                code, _, error = self.launch(directory, '--motion-output', option, '0.5')
                self.assertEqual(code, 2, option)
                self.assertIn(f'{option} requires --taa', error)

    def test_resolve_options_are_absent_unless_given(self):
        # --taa-current-filter / --taa-history-weight (run 139 A/B): forwarded
        # only when given, a stale shell value dropped, ranges enforced.
        names = ('X3M_TAA_CURRENT_FILTER', 'X3M_TAA_HISTORY_WEIGHT')
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, inherited={'X3M_TAA_CURRENT_FILTER': '2', 'X3M_TAA_HISTORY_WEIGHT': '0.5'})
            for name in names:
                self.assertNotIn(name, env)
            env = self.env(directory, *TAA, '--taa-current-filter', '1.0', '--taa-history-weight', '0.95')
            self.assertEqual([env[name] for name in names], ['1.0', '0.95'])
            for option, value in (('--taa-current-filter', '4.5'), ('--taa-current-filter', 'nan'), ('--taa-current-filter', '-1'),
                                  ('--taa-history-weight', '0.99'), ('--taa-history-weight', '0.4'), ('--taa-history-weight', 'nan')):
                code, _, error = self.launch(directory, *TAA, option, value)
                self.assertEqual(code, 2, (option, value))
                self.assertIn(f'{option} must be within', error)
            for option in ('--taa-current-filter', '--taa-history-weight'):
                code, _, error = self.launch(directory, '--motion-output', option, '0.9')
                self.assertEqual(code, 2, option)
                self.assertIn(f'{option} requires --taa', error)

    def test_taa_debug_accepts_32_capture_frames(self):
        # Run 139: the resolved-frame spectrum needs more than one jitter period.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-debug', '--capture-frames', '32')
            self.assertEqual((env['X3M_TAA_DEBUG'], env['X3M_CAPTURE_FRAMES']), ('1', '32'))
            self.assertEqual(self.launch(directory, *TAA, '--capture-frames', '65')[0], 2)
        self.assertIn('if(capture_count>64) capture_count=64;', (ROOT / 'src/proxy/capture.cpp').read_text())


class TaaImageDefaultsDll(unittest.TestCase):
    """The DLL's own fallback (a direct WINEDLLOVERRIDES start without the launcher)."""

    def test_capture_defaults_are_gated_on_taa(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('taa_mip_bias=taa_requested?-0.5f:0.f;', source)
        self.assertIn('taa_sharpen=taa_requested?0.75f:0.f;', source)
        # The env value is still parsed whole, so an explicit 0 disables either.
        for name in ('X3M_TAA_MIP_BIAS', 'X3M_TAA_SHARPEN', 'X3M_TAA_CURRENT_FILTER', 'X3M_TAA_HISTORY_WEIGHT'):
            line = next(l for l in source.splitlines() if f'GetEnvironmentVariableW(L"{name}"' in l)
            self.assertIn("*end==L'\\0'", line)


if __name__ == '__main__':
    unittest.main()
