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


# The real layout of [EnvironmentVariables] in a CrossOver Preview cxbottle.conf (bottle X3, 2026-09-27): the
# section is last, a `;;` comment sits between entries. A section follows it here to cover the end of the body.
BOTTLE_CONF = ('[Bottle]\n"WineArch" = "win64"\n;; "Updater" = ""\n\n'
               '[EnvironmentVariables]\n"FEX_X87REDUCEDPRECISION" = "1"\n;;"PROMPT" = "$p$g"\n"WINEMSYNC" = "1"\n'
               '"CX_GRAPHICS_BACKEND" = "dxmt"\n"D3DM_ENABLE_METALFX" = "1"\n"DXMT_ENABLE_NVEXT" = "1"\n\n'
               '[Other]\n"Keep" = "1"\n')


class VoiceDecoderBottleEnv(unittest.TestCase):
    """`manage.py voice-decoder --bottle-env check|apply|remove` on a synthetic bottle directory."""
    setUpClass = VoiceDecoderInstall.__dict__['setUpClass']
    run_tool = VoiceDecoderInstall.run_tool

    def setUp(self):
        VoiceDecoderInstall.setUp(self)
        self.bottles = Path(self.directory.name) / 'bottles'
        (self.bottles / 'B').mkdir(parents=True)
        self.conf = self.bottles / 'B/cxbottle.conf'
        self.backup = self.bottles / 'B/cxbottle.conf.x3m-bak'
        patcher = mock.patch.object(self.module, 'CROSSOVER_BOTTLES', self.bottles)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.env = ('--bottle', 'B', '--game-dir', str(self.game), '--source', str(self.source))
        root = self.game.resolve() / 'x3m/voice-decoder'
        self.lines = [f'"GST_PLUGIN_PATH_1_0" = "{root}/runtime/plugins"', f'"GST_REGISTRY_1_0" = "{root}/registry/x3-arm64.bin"']
        self.assertEqual(self.run_tool('--install', *self.common)[0], 0)

    def test_apply_check_remove_both_line_endings(self):
        for eol in ('\n', '\r\n'):
            with self.subTest(eol=repr(eol)):
                self.backup.unlink(missing_ok=True)
                original = BOTTLE_CONF.replace('\n', eol).encode()
                self.conf.write_bytes(original)
                self.conf.chmod(0o600)
                code, output, _ = self.run_tool('--bottle-env', 'check', *self.env)
                self.assertEqual((code, output.count(': absent (expected')), (1, 2))
                code, output, error = self.run_tool('--bottle-env', 'apply', *self.env)
                self.assertEqual(code, 0, error)
                self.assertIn(f'sha256 before {hashlib.sha256(original).hexdigest()}', output)
                applied = self.conf.read_bytes()
                self.assertIn(f'sha256 after {hashlib.sha256(applied).hexdigest()}', output)
                self.assertEqual(output.count(': added'), 2)
                self.assertIn('backup', output)
                expected = original.replace(f'"DXMT_ENABLE_NVEXT" = "1"{eol}'.encode(),
                                            (f'"DXMT_ENABLE_NVEXT" = "1"{eol}' + ''.join(l + eol for l in self.lines)).encode())
                self.assertEqual(applied, expected)  # every other byte kept, entries in the file's line ending
                self.assertEqual(self.backup.read_bytes(), original)
                self.assertEqual(oct(self.conf.stat().st_mode & 0o777), oct(0o600))
                self.assertEqual(self.run_tool('--bottle-env', 'check', *self.env)[0], 0)
                code, output, _ = self.run_tool('--bottle-env', 'apply', *self.env)  # idempotent
                self.assertEqual(code, 0)
                self.assertIn('unchanged (both present)', output)
                self.assertNotIn('backup', output)
                self.assertEqual(self.conf.read_bytes(), applied)
                code, output, _ = self.run_tool('--bottle-env', 'remove', *self.env)
                self.assertEqual((code, output.count(': removed')), (0, 2))
                self.assertEqual(self.conf.read_bytes(), original)
                self.assertNotIn('backup', output)  # created once, on the first apply
                self.assertEqual(self.backup.read_bytes(), original)
                self.assertIn('unchanged (neither present)', self.run_tool('--bottle-env', 'remove', *self.env)[1])
                self.assertEqual(sorted(p.name for p in self.conf.parent.iterdir()), ['cxbottle.conf', 'cxbottle.conf.x3m-bak'])

    def test_differing_value_replaced_in_place(self):
        original = BOTTLE_CONF.replace('"WINEMSYNC" = "1"\n', f'"WINEMSYNC" = "1"\n"GST_REGISTRY_1_0" = "/old/x.bin"\r\n')
        self.conf.write_bytes(original.encode())
        code, output, _ = self.run_tool('--bottle-env', 'check', *self.env)
        self.assertEqual(code, 1)
        self.assertIn('GST_REGISTRY_1_0: differs: /old/x.bin', output)
        code, output, error = self.run_tool('--bottle-env', 'apply', *self.env)
        self.assertEqual(code, 0, error)
        self.assertIn('GST_REGISTRY_1_0: replaced "/old/x.bin"', output)
        self.assertIn('GST_PLUGIN_PATH_1_0: added', output)
        expected = original.replace('"/old/x.bin"\r\n', f'"{self.lines[1].split(" = ")[1][1:-1]}"\r\n')
        expected = expected.replace('"DXMT_ENABLE_NVEXT" = "1"\n', f'"DXMT_ENABLE_NVEXT" = "1"\n{self.lines[0]}\n')
        self.assertEqual(self.conf.read_bytes(), expected.encode())  # the replaced line keeps its own CRLF

    def test_refusals_leave_the_file(self):
        without = BOTTLE_CONF.replace('[EnvironmentVariables]\n', '')
        self.conf.write_text(without)
        code, _, error = self.run_tool('--bottle-env', 'apply', *self.env)
        self.assertEqual(code, 2)
        self.assertIn('0 [EnvironmentVariables] sections', error)
        self.assertEqual(self.conf.read_text(), without)
        self.conf.write_text(BOTTLE_CONF)
        (self.dest / 'runtime/plugins/libgstlibav.dylib').write_bytes(b'other')  # stale drop-in
        code, _, error = self.run_tool('--bottle-env', 'apply', *self.env)
        self.assertEqual(code, 2)
        self.assertIn('is stale', error)
        self.assertEqual(self.conf.read_text(), BOTTLE_CONF)
        self.assertFalse(self.backup.exists())
        (self.dest / 'runtime/plugins/libgstlibav.dylib').write_bytes(b'plugin')
        with mock.patch.object(self.module.media_package, 'assert_game_closed',
                               side_effect=self.module.media_package.PackageError('game running')):
            with contextlib.redirect_stderr(io.StringIO()):
                code = self.module.voice_decoder_command(['--bottle-env', 'apply', *self.env])
        self.assertEqual((code, self.conf.read_text()), (2, BOTTLE_CONF))
        self.assertEqual(self.run_tool('--bottle-env', 'apply', '--install', *self.env)[0], 2)  # one mode at a time

    def test_launcher_status(self):
        status = lambda: self.module.voice_bottle_env_status(self.bottles / 'B', self.game)
        self.assertEqual(status(), 'unset')  # no file
        self.conf.write_text(BOTTLE_CONF)
        self.assertEqual(status(), 'unset')
        self.assertEqual(self.run_tool('--bottle-env', 'apply', *self.env)[0], 0)
        self.assertEqual(status(), 'set')
        self.conf.write_text(self.conf.read_text().replace('x3-arm64.bin', 'other.bin'))
        self.assertEqual(status(), 'differs')


if __name__ == '__main__':
    unittest.main()
