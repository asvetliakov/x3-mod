"""Host checks for the user-accepted motion-cut production defaults.

The launcher makes the defaults effective for the installed DLL immediately,
while explicit inherited values retain the diagnostic detector. The native
initializers mirror the launcher for future builds. No game and no Wine.
"""
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


def load_manage():
    spec = importlib.util.spec_from_file_location('motion_cut_defaults_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MotionCutDefaultsLaunch(unittest.TestCase):
    def env(self, directory, *, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        launch_env = dict(module.os.environ)
        launch_env.pop('X3M_MOTION_CUT_MEDIAN_PX', None)
        launch_env.pop('X3M_MOTION_CUT_MISSING', None)
        launch_env.update(inherited or {})
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game),
                '--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, launch_env, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                self.assertEqual(exit_error.code, 0, error.getvalue())
        return json.loads(output.getvalue())['env']

    def test_unset_launcher_defaults_disable_both_motion_cut_heuristics(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory)
            self.assertEqual(env['X3M_MOTION_CUT_MEDIAN_PX'], '1e30')
            self.assertEqual(env['X3M_MOTION_CUT_MISSING'], '1')
            # The separate camera cut stays at its established default (the sentinel
            # policy is always auto since 2026-09-25 and no longer sent); this change
            # only disables the two motion heuristics.
            self.assertEqual(env['X3M_CAMERA_CUT_DEG'], '20.0')
            self.assertNotIn('X3M_TAA_SENTINEL', env)

    def test_explicit_diagnostic_overrides_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, inherited={
                'X3M_MOTION_CUT_MEDIAN_PX': '48',
                'X3M_MOTION_CUT_MISSING': '.25',
            })
            self.assertEqual(env['X3M_MOTION_CUT_MEDIAN_PX'], '48')
            self.assertEqual(env['X3M_MOTION_CUT_MISSING'], '.25')

    def test_native_defaults_match_launcher_defaults(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('float motion_cut_median_px = 1e30f, motion_cut_missing = 1.f;', capture)
        self.assertIn('float cut_median_bound_ = 1e30f, cut_missing_bound_ = 1.f;', header)


if __name__ == '__main__':
    unittest.main()
