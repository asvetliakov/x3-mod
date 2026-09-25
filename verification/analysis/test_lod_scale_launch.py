"""--lod-scale was removed on 2026-09-25 (user decision; the merged-LOD overlay replaced it): the option is
refused and an inherited X3M_LOD_SCALE is dropped. LodScaleLaunchOption.launch is the shared hermetic
--vanilla dry-run helper of the launcher tests (no game, no Wine)."""
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

    def test_option_removed_and_inherited_variable_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--lod-scale', '2')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)
            code, output, error = self.launch(directory, inherited={'X3M_LOD_SCALE': '3'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_LOD_SCALE', json.loads(output)['env'])


if __name__ == '__main__':
    unittest.main()
