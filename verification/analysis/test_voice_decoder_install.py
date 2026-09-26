"""Host tests of `tools/manage.py voice-decoder` (tools/voice_decoder_files.py): the drop-in copy
<game>/x3m/voice-decoder is reported missing, installed, verified against artifact-sha256.txt, left alone when
identical and reported stale after a change; a tampered source, a symlinked destination and a directory without
X3AP.exe are refused. Synthetic trees only: no game, no Wine, no bottle write."""
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load_manage():
    spec = importlib.util.spec_from_file_location('voice_decoder_install_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def source_tree(base):
    source = Path(base) / 'source'
    files = {'runtime/plugins/libgstlibav.dylib': b'plugin', 'runtime/lib/libx3wma-avutil.59.dylib': b'avutil',
             'runtime/licenses/COPYING': b'licence', 'README.md': b'readme', 'fix.patch': b'patch'}
    for name, data in files.items():
        (source / name).parent.mkdir(parents=True, exist_ok=True)
        (source / name).write_bytes(data)
    listed = [n for n in files if n.endswith(('.dylib', '.patch'))]
    lines = [f'{hashlib.sha256(files[n]).hexdigest()}  {n}' for n in listed] + [f'{"0" * 64}  src/not-shipped.c']
    (source / 'artifact-sha256.txt').write_text('\n'.join(lines) + '\n')
    (source / 'registry').mkdir()
    (source / 'registry/x3-arm64.bin').write_bytes(b'cache')  # never shipped
    return source


class VoiceDecoderInstall(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_manage()

    def run_tool(self, *args):
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(self.module.media_package, 'assert_game_closed'), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                code = self.module.voice_decoder_command(list(args))
            except SystemExit as exit_error:
                code = exit_error.code
        return code, output.getvalue(), error.getvalue()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = source_tree(self.directory.name)
        self.game = Path(self.directory.name) / 'game'
        self.game.mkdir()
        (self.game / 'X3AP.exe').touch()
        self.dest = self.game / 'x3m/voice-decoder'
        self.common = ('--game-dir', str(self.game), '--source', str(self.source))

    def test_missing_install_idempotent_stale(self):
        code, output, _ = self.run_tool(*self.common)
        self.assertEqual((code, output.split()[2]), (1, 'missing'))
        code, output, error = self.run_tool('--install', *self.common)
        self.assertEqual(code, 0, error)
        self.assertIn('6 copied, 0 unchanged; 3 artifact hashes verified, 1 listed but not shipped', output)
        self.assertIn('voice decoder: valid', output)
        self.assertTrue((self.dest / 'registry').is_dir())
        self.assertFalse((self.dest / 'registry/x3-arm64.bin').exists())
        self.assertEqual((self.dest / 'runtime/plugins/libgstlibav.dylib').read_bytes(), b'plugin')
        self.assertEqual(oct((self.dest / 'README.md').stat().st_mode & 0o777), oct(0o644))
        code, output, _ = self.run_tool('--install', *self.common)
        self.assertEqual(code, 0)
        self.assertIn('0 copied, 6 unchanged', output)
        (self.dest / 'runtime/plugins/libgstlibav.dylib').write_bytes(b'other')
        code, output, _ = self.run_tool('--check', *self.common)
        self.assertEqual(code, 1)
        self.assertIn('stale', output)
        self.assertIn('runtime/plugins/libgstlibav.dylib differs', output)
        self.assertEqual(self.run_tool('--install', *self.common)[0], 0)  # repaired
        self.assertEqual((self.dest / 'runtime/plugins/libgstlibav.dylib').read_bytes(), b'plugin')

    def test_refusals(self):
        (self.source / 'fix.patch').write_bytes(b'tampered')
        code, _, error = self.run_tool('--install', *self.common)
        self.assertEqual(code, 2)
        self.assertIn('fails its artifact-sha256.txt', error)
        self.assertFalse(self.dest.exists())
        (self.source / 'fix.patch').write_bytes(b'patch')
        elsewhere = Path(self.directory.name) / 'elsewhere'
        elsewhere.mkdir()
        os.symlink(elsewhere, self.game / 'x3m')
        code, _, error = self.run_tool('--install', *self.common)
        self.assertEqual(code, 2)
        self.assertIn('link/reparse path', error)
        self.assertEqual(list(elsewhere.iterdir()), [])  # nothing written through the link
        (self.game / 'X3AP.exe').unlink()
        code, _, error = self.run_tool('--install', *self.common)
        self.assertEqual(code, 2)
        self.assertIn('no X3AP.exe', error)
        self.assertEqual(self.run_tool('--inst', *self.common)[0], 2)  # no abbreviations

    def test_repository_tree_verifies(self):
        files_module = self.module.voice_decoder_files
        shipped = files_module.shipped(self.module.VOICE_DECODER_REPO)
        self.assertIn('runtime/plugins/libgstlibav.dylib', shipped)
        self.assertFalse(any(name.startswith('registry') for name in shipped))
        verified, not_shipped, problems = files_module.verify_hashes(self.module.VOICE_DECODER_REPO, shipped)
        self.assertEqual((verified, not_shipped, problems), (7, 1, []))


if __name__ == '__main__':
    unittest.main()
