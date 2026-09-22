"""Host tests of --taa-sky-history (X3M_TAA_SKY_HISTORY, docs/architecture/seta-motion.md):
forwarded only when given, in TAA mode only; an inherited shell value never survives
a launch that did not ask for it. No game, no Wine."""
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
    spec = importlib.util.spec_from_file_location('sky_history_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SkyHistoryLaunch(unittest.TestCase):
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
            self.assertNotIn('X3M_TAA_SKY_HISTORY', self.env(directory, *TAA))
            self.assertNotIn('X3M_TAA_SKY_HISTORY', self.env(directory, *TAA, inherited={'X3M_TAA_SKY_HISTORY': 'strict'}))

    def test_strict_and_loose_are_forwarded_verbatim(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, *TAA, '--taa-sky-history', 'strict')['X3M_TAA_SKY_HISTORY'], 'strict')
            self.assertEqual(self.env(directory, *TAA, '--taa-sky-history', 'loose')['X3M_TAA_SKY_HISTORY'], 'loose')

    def test_requires_taa(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--motion-output', '--taa-sky-history', 'strict')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-sky-history requires --taa', error)

    # --taa-sky-history-band-px (X3M_TAA_SKY_HISTORY_BAND_PX; seta-motion.md section 4): the band term's
    # threshold in px/frame, forwarded only when given, within 1..16, in TAA mode only.
    def test_band_px_omitted_is_not_forwarded_and_drops_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_SKY_HISTORY_BAND_PX', self.env(directory, *TAA, '--taa-sky-history', 'strict'))
            self.assertNotIn('X3M_TAA_SKY_HISTORY_BAND_PX', self.env(directory, *TAA, inherited={'X3M_TAA_SKY_HISTORY_BAND_PX': '5'}))

    def test_band_px_is_forwarded_as_the_float_given(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-sky-history', 'strict', '--taa-sky-history-band-px', '2.5')
            self.assertEqual(float(env['X3M_TAA_SKY_HISTORY_BAND_PX']), 2.5)
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history-band-px', '16')['X3M_TAA_SKY_HISTORY_BAND_PX']), 16.0)
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history-band-px', '1')['X3M_TAA_SKY_HISTORY_BAND_PX']), 1.0)

    def test_band_px_bounds_and_taa_requirement(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('0.5', '16.5', '-3', 'nan'):
                code, _, error = self.launch(directory, *TAA, '--taa-sky-history-band-px', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-sky-history-band-px must be within 1..16', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-sky-history-band-px', '3')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-sky-history-band-px requires --taa', error)

    # --taa-sky-history-exit-px (X3M_TAA_SKY_HISTORY_EXIT_PX; seta-sky-hull-share-decay.md): the exit reset's parallax
    # floor, forwarded only when given, under strict with an age program, 0 or within 0.125..the band threshold.
    EXIT = ('--taa-sky-history', 'strict', '--taa-far-stabiliser', '0.985')

    def test_exit_px_omitted_is_not_forwarded_and_drops_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_SKY_HISTORY_EXIT_PX', self.env(directory, *TAA, *self.EXIT))
            self.assertNotIn('X3M_TAA_SKY_HISTORY_EXIT_PX', self.env(directory, *TAA, *self.EXIT, inherited={'X3M_TAA_SKY_HISTORY_EXIT_PX': '0.25'}))
            self.assertNotIn('X3M_TAA_SKY_HISTORY_EXIT_PX', self.env(directory, *TAA, inherited={'X3M_TAA_SKY_HISTORY_EXIT_PX': '0.25'}))

    def test_exit_px_is_forwarded_as_the_float_given_with_each_age_program(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(float(self.env(directory, *TAA, *self.EXIT, '--taa-sky-history-exit-px', '0.25')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.25)
            self.assertEqual(float(self.env(directory, *TAA, *self.EXIT, '--taa-sky-history-exit-px', '0')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.0)
            # 0 is the explicit off spelling: accepted and forwarded with --taa alone, without strict or an age program (the DLL accepts it the same way).
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history-exit-px', '0')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.0)
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history', 'loose', '--taa-sky-history-exit-px', '0')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.0)
            self.assertEqual(float(self.env(directory, *TAA, *self.EXIT, '--taa-sky-history-exit-px', '3')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 3.0)
            self.assertEqual(float(self.env(directory, *TAA, *self.EXIT, '--taa-sky-history-band-px', '4', '--taa-sky-history-exit-px', '3.5')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 3.5)
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history', 'strict', '--taa-thin-region', '0.97', '--taa-sky-history-exit-px', '0.5')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.5)
            self.assertEqual(float(self.env(directory, *TAA, '--taa-sky-history', 'strict', '--taa-thin-clip', '0.7', '--taa-adaptive-weight', '0.97', '--taa-sky-history-exit-px', '0.125')['X3M_TAA_SKY_HISTORY_EXIT_PX']), 0.125)

    def test_exit_px_requirements_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('0.1', '3.5', '-1', 'nan'):
                code, _, error = self.launch(directory, *TAA, *self.EXIT, '--taa-sky-history-exit-px', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-sky-history-exit-px must be 0 or within 0.125..the band threshold (3)', error)
            code, _, error = self.launch(directory, *TAA, *self.EXIT, '--taa-sky-history-band-px', '2', '--taa-sky-history-exit-px', '2.5')
            self.assertNotEqual(code, 0)
            self.assertIn('the band threshold (2)', error)
            for value in ('0.25', '0'):
                code, _, error = self.launch(directory, '--motion-output', '--taa-sky-history-exit-px', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-sky-history-exit-px requires --taa', error)
            code, _, error = self.launch(directory, *TAA, '--taa-sky-history-exit-px', '0.1')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-sky-history-exit-px must be 0 or within 0.125..the band threshold (3)', error)
            for args in (('--taa-far-stabiliser', '0.985'), ('--taa-sky-history', 'loose', '--taa-far-stabiliser', '0.985')):
                code, _, error = self.launch(directory, *TAA, *args, '--taa-sky-history-exit-px', '0.25')
                self.assertNotEqual(code, 0, args)
                self.assertIn('--taa-sky-history-exit-px requires --taa-sky-history strict', error)
            for args in (('--taa-sky-history', 'strict'), ('--taa-sky-history', 'strict', '--taa-far-stabiliser', '0'), ('--taa-sky-history', 'strict', '--taa-thin-region', '0')):
                code, _, error = self.launch(directory, *TAA, *args, '--taa-sky-history-exit-px', '0.25')
                self.assertNotEqual(code, 0, args)
                self.assertIn('--taa-sky-history-exit-px requires an age program', error)


if __name__ == '__main__':
    unittest.main()
