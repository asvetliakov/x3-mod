"""Host tests of --d3dx in tools/manage.py: the default command is unchanged,
builtin appends the d3dx9_37 override to this child's --dll string (plain and
--vanilla), and --dry-run reports it. No game, no Wine."""
import contextlib
import hashlib
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
    spec = importlib.util.spec_from_file_location('d3dx_override_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class D3dxOverrideLaunchOption(unittest.TestCase):
    def launch(self, directory, *args):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        # A fake installed proxy plus its manifest, so the non-vanilla launch
        # passes the ownership check without touching a real bottle.
        dll = game / 'd3d9.dll'
        dll.write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    @staticmethod
    def dll_value(command):
        return command[command.index('--dll') + 1]

    def test_default_command_is_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            baseline = json.loads(output)['command']
            self.assertEqual(self.dll_value(baseline), 'd3d9=n,b')
            # The explicit default produces the identical command.
            self.assertEqual(json.loads(self.launch(directory, '--d3dx', 'native')[1])['command'], baseline)
            vanilla = json.loads(self.launch(directory, '--vanilla')[1])['command']
            self.assertEqual(self.dll_value(vanilla), 'd3d9=b')

    def test_builtin_appends_the_d3dx_override(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory, '--d3dx', 'builtin')
            self.assertEqual(code, 0, error)
            command = json.loads(output)['command']
            self.assertEqual(self.dll_value(command), 'd3d9=n,b;d3dx9_37=b')
            baseline = json.loads(self.launch(directory)[1])['command']
            self.assertEqual([a for a in command if a != 'd3d9=n,b;d3dx9_37=b'],
                             [a for a in baseline if a != 'd3d9=n,b'])
            self.assertEqual(command.count('--dll'), 1)

    def test_vanilla_plus_builtin(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory, '--vanilla', '--d3dx', 'builtin')
            self.assertEqual(code, 0, error)
            self.assertEqual(self.dll_value(json.loads(output)['command']), 'd3d9=b;d3dx9_37=b')

    def test_dry_run_json_carries_the_override_and_no_new_variable(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            delivered = json.loads(self.launch(directory, '--d3dx', 'builtin')[1])
            self.assertIn('d3dx9_37=b', ' '.join(delivered['command']))
            self.assertEqual(delivered['env'], baseline['env'])

    def test_unknown_value_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--d3dx', 'wine')
            self.assertEqual(code, 2)
            self.assertIn('--d3dx', error)


if __name__ == '__main__':
    unittest.main()
