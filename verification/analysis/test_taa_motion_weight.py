"""Host tests of --taa-motion-weight (X3M_TAA_MOTION_WEIGHT, docs/architecture/taa-motion-history-weight.md):
forwarded only when given, in TAA mode with an age program; the launcher and the DLL default off; an
inherited shell value never survives a launch that did not ask for it. No game, no Wine."""
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
AGE = ('--taa-far-stabiliser', '0.985')


def load_manage():
    spec = importlib.util.spec_from_file_location('motion_weight_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MotionWeightLaunch(unittest.TestCase):
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

    def test_omitted_is_not_forwarded_and_drops_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_MOTION_WEIGHT', self.env(directory, *TAA, *AGE))
            self.assertNotIn('X3M_TAA_MOTION_WEIGHT', self.env(directory, *TAA, *AGE, inherited={'X3M_TAA_MOTION_WEIGHT': '0.8,2,8'}))
            self.assertNotIn('X3M_TAA_MOTION_WEIGHT', self.env(directory, *TAA, inherited={'X3M_TAA_MOTION_WEIGHT': '0.8'}))

    def test_forwarded_as_the_triple_with_each_age_program(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-motion-weight', '0.8,2,8')['X3M_TAA_MOTION_WEIGHT'], '0.8,2,8')
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-motion-weight', '0.7')['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-motion-weight', '0.5,0,64')['X3M_TAA_MOTION_WEIGHT'], '0.5,0,64')
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-motion-weight', '0.9999999,2,7.9999999')['X3M_TAA_MOTION_WEIGHT'], '0.9999999,2,7.9999999')  # forwarded unrounded: the DLL refuses F >= 1 and V1 <= V0
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-motion-weight', '0.8,3,12')['X3M_TAA_MOTION_WEIGHT'], '0.8,3,12')
            # 0 is the explicit off spelling: accepted and forwarded with --taa alone (the DLL accepts it the same way).
            self.assertEqual(self.env(directory, *TAA, '--taa-motion-weight', '0')['X3M_TAA_MOTION_WEIGHT'], '0,2,8')

    def test_requirements_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('0.4', '1', '1.5', '-0.8', 'nan', '0.8,8,2', '0.8,-1,8', '0.8,2,65', '0.8,2'):
                code, _, error = self.launch(directory, *TAA, *AGE, '--taa-motion-weight', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-motion-weight', error)
            for value in ('0.8', '0'):
                code, _, error = self.launch(directory, '--motion-output', '--taa-motion-weight', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-motion-weight requires --taa', error)
            for args in ((), ('--taa-far-stabiliser', '0'), ('--taa-thin-region', '0'), ('--taa-sky-history', 'loose')):
                code, _, error = self.launch(directory, *TAA, *args, '--taa-motion-weight', '0.8,2,8')
                self.assertNotEqual(code, 0, args)
                self.assertIn('--taa-motion-weight requires an age program', error)

    def test_dll_default_off_and_option_names(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_motion_weight[3] = {0.f, 2.f, 8.f};', capture)
        self.assertIn('L"X3M_TAA_MOTION_WEIGHT"', capture)
        self.assertIn('motion_weight=%.3f,%g,%g', capture)
        self.assertIn('taa_motion_weight_setting invalid=1 reason=too_long', capture)
        resolve = (ROOT / 'src/temporal/resolve.h').read_text()
        self.assertIn('kMotionWeightMin = .5f', resolve)


if __name__ == '__main__':
    unittest.main()
