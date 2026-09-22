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


if __name__ == '__main__':
    unittest.main()
