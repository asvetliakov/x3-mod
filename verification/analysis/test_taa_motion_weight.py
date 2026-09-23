"""Host tests of --taa-motion-weight (X3M_TAA_MOTION_WEIGHT, docs/architecture/taa-motion-history-weight.md):
always resolved and forwarded with --taa (0.7,2,8 with an age program and a camera policy other than
--taa-sentinel 1 since Run 70 A, else 0), the DLL's own fallback matches; 0 is the opt-out; an inherited shell
value never survives a launch. No game, no Wine."""
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
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
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

    def test_omitted_resolves_to_the_run70_default_or_off(self):
        # Run 70 A (2026-09-23, run262/run263): 0.7,2,8 with an age program under a policy that can reach 2, else 0;
        # always forwarded with --taa, so an inherited value never survives.
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, *TAA, *AGE)['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            self.assertEqual(self.env(directory, *TAA, *AGE, inherited={'X3M_TAA_MOTION_WEIGHT': '0'})['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97')['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-sentinel', '2')['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            # Off: the explicit opt-out, no age program (never an error), camera policy forced to 1.
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-motion-weight', '0')['X3M_TAA_MOTION_WEIGHT'], '0,2,8')
            self.assertEqual(self.env(directory, *TAA, inherited={'X3M_TAA_MOTION_WEIGHT': '0.8'})['X3M_TAA_MOTION_WEIGHT'], '0')
            self.assertEqual(self.env(directory, *TAA, '--taa-far-stabiliser', '0')['X3M_TAA_MOTION_WEIGHT'], '0')
            self.assertEqual(self.env(directory, *TAA, *AGE, '--taa-sentinel', '1', inherited={'X3M_TAA_MOTION_WEIGHT': '0.8,2,8'})['X3M_TAA_MOTION_WEIGHT'], '0')
            # Without --taa nothing is forwarded and an inherited value is dropped.
            self.assertNotIn('X3M_TAA_MOTION_WEIGHT', self.env(directory, '--motion-output', inherited={'X3M_TAA_MOTION_WEIGHT': '0.8,2,8'}))

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

    def test_dll_fallback_and_option_names(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_motion_weight[3] = {0.f, 2.f, 8.f};', capture)
        self.assertIn('L"X3M_TAA_MOTION_WEIGHT"', capture)
        self.assertIn('motion_weight=%.3f,%g,%g', capture)
        self.assertIn('taa_motion_weight_setting invalid=1 reason=too_long', capture)
        # Absent only (an invalid or oversized value stays off, logged): 0.7 with TAA, a policy other than 1 and an age program.
        fallback = 'else if(taa_requested&&taa_sentinel_mode!=x3m::renderer::SentinelMode::CurrentOnly&&(taa_far[0]>0.f||taa_far[1]>0.f||taa_thin_region[0]>0.f))\n        taa_motion_weight[0]=.7f;'
        self.assertIn(fallback, capture)
        parse = capture.index('GetEnvironmentVariableW(L"X3M_TAA_MOTION_WEIGHT"')
        self.assertLess(capture.index('GetEnvironmentVariableW(L"X3M_TAA_SENTINEL"'), parse)
        self.assertLess(capture.index('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION"'), parse)
        self.assertLess(parse, capture.index(fallback))
        manage = load_manage()
        self.assertEqual(manage.TAA_MOTION_WEIGHT_DEFAULT, '0.7,2,8')
        resolve = (ROOT / 'src/temporal/resolve.h').read_text()
        self.assertIn('kMotionWeightMin = .5f', resolve)


if __name__ == '__main__':
    unittest.main()
