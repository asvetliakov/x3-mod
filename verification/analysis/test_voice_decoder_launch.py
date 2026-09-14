"""Host tests of the opt-in --voice-decoder delivery in tools/manage.py.

The option must reach the launched process with exactly the two versioned
GStreamer variables of docs/architecture/voice-decoder-adapter.md, create only
the registry directory, refuse an invalid directory, and leave the launch
environment byte-for-byte unchanged when it is absent. Dry-run only: no game,
no Wine, no bottle or application write.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = ('DYLD_LIBRARY_PATH', 'GST_PLUGIN_PATH', 'GST_REGISTRY', 'GST_PLUGIN_SYSTEM_PATH')


def load_manage():
    spec = importlib.util.spec_from_file_location('voice_decoder_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class VoiceDecoderLaunchOption(unittest.TestCase):
    def plugin_tree(self, directory, *, plugin=True, libs=True):
        root = Path(directory) / 'wma-plugin'
        (root / 'runtime/plugins').mkdir(parents=True)
        if plugin:
            (root / 'runtime/plugins/libgstlibav.dylib').touch()
        if libs:
            (root / 'runtime/lib').mkdir(parents=True)
        return root

    def launch(self, directory, *args):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_option_leaves_the_launch_environment_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory)
            self.assertEqual(code, 0)
            environment = json.loads(output)['env']
            self.assertTrue(all(name.startswith('X3M_') for name in environment), environment)

    def test_the_two_versioned_variables_and_nothing_else_are_delivered(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            added = {k: v for k, v in delivered['env'].items() if k not in baseline['env']}
            self.assertEqual(added, {'GST_PLUGIN_PATH_1_0': str(root / 'runtime/plugins'),
                                     'GST_REGISTRY_1_0': str(root / 'registry/x3-arm64.bin'),
                                     'X3M_VOICE_DMO_FALLBACK': '1'})
            self.assertEqual({k: v for k, v in delivered['env'].items() if k in baseline['env']}, baseline['env'])
            for name in FORBIDDEN:
                self.assertNotIn(name, delivered['env'])
            # Only the registry directory is created, nothing else.
            self.assertTrue((root / 'registry').is_dir())
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['registry', 'runtime'])
            self.assertEqual(sorted(p.name for p in (root / 'registry').iterdir()), [])

    def test_an_existing_registry_directory_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            (root / 'registry').mkdir()
            (root / 'registry/x3-arm64.bin').write_bytes(b'stale')
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['GST_REGISTRY_1_0'], str(root / 'registry/x3-arm64.bin'))
            self.assertEqual((root / 'registry/x3-arm64.bin').read_bytes(), b'stale')

    def test_an_invalid_directory_is_refused_with_a_message(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = {'missing': Path(directory) / 'does-not-exist',
                     'no plugin': self.plugin_tree(tempfile.mkdtemp(dir=directory), plugin=False),
                     'no lib': self.plugin_tree(tempfile.mkdtemp(dir=directory), libs=False)}
            for label, root in cases.items():
                with self.subTest(case=label):
                    code, _, error = self.launch(directory, '--voice-decoder', str(root))
                    self.assertEqual(code, 2)
                    self.assertIn('--voice-decoder', error)
            blocked = self.plugin_tree(tempfile.mkdtemp(dir=directory))
            os.chmod(blocked, 0o555)
            try:
                code, _, error = self.launch(directory, '--voice-decoder', str(blocked))
            finally:
                os.chmod(blocked, 0o755)
            self.assertEqual(code, 2)
            self.assertIn('registry', error)

    def test_the_option_is_launch_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            module = load_manage()
            error = io.StringIO()
            with mock.patch.object(sys, 'argv', ['manage.py', 'status', '--voice-decoder', str(root)]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(error):
                with self.assertRaises(SystemExit) as raised:
                    module.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn('launch only', error.getvalue())


if __name__ == '__main__':
    unittest.main()
