"""Host tests of --lod-scale in tools/manage.py: range refusal, the variable
absent without the option, and the --dry-run value. No game, no Wine."""
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
    spec = importlib.util.spec_from_file_location('lod_scale_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LodScaleLaunchOption(unittest.TestCase):
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

    def test_absent_option_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_LOD_SCALE': '3'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_LOD_SCALE', json.loads(output)['env'])

    def test_dry_run_carries_the_factor(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--lod-scale', '2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']}, {'X3M_LOD_SCALE': '2.0'})
            self.assertEqual(json.loads(self.launch(directory, '--lod-scale', '1')[1])['env']['X3M_LOD_SCALE'], '1.0')
            self.assertEqual(json.loads(self.launch(directory, '--lod-scale', '4')[1])['env']['X3M_LOD_SCALE'], '4.0')
            self.assertEqual(json.loads(self.launch(directory, '--lod-scale', '0.5')[1])['env']['X3M_LOD_SCALE'], '0.5')
            self.assertEqual(json.loads(self.launch(directory, '--lod-scale', '0.25')[1])['env']['X3M_LOD_SCALE'], '0.25')

    def test_out_of_range_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('0.2', '0.24', '4.5', '0', '-2', 'nan', 'inf'):
                code, _, error = self.launch(directory, '--lod-scale', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--lod-scale out of range', error)
            self.assertEqual(self.launch(directory, '--lod-scale', 'two')[0], 2)


if __name__ == '__main__':
    unittest.main()
