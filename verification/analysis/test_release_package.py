"""The release zip (tools/release/package.py, docs/architecture/config-file.md section 6): its manifest, its README's
build line, its determinism and its refusals. The zip is exactly ENTRIES (d3d9.dll, x3m.ini, x3m-regenerate.exe, the 16
committed density fonts under f/, the two OFL texts, README.txt); the host x3m-regenerate binary and the CrossOver voice
decoder never ship. Host only: fake DLL and regenerate binaries, the real committed fonts."""
import contextlib
import hashlib
import importlib.util
import io
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
FONTS = [f'{stem}.{ext}' for stem in ('Harrier48', 'Harrier72', 'Tahoma26', 'Tahoma39', 'Zekton52', 'Zekton78', 'ZektonES52',
                                      'ZektonES78') for ext in ('abc', 'tga')]
ENTRIES = ['d3d9.dll', 'x3m.ini', 'x3m-regenerate.exe', *(f'f/{name}' for name in FONTS), 'OFL-NotoSans.txt', 'OFL-Exo2.txt',
           'README.txt']


def load_package():
    spec = importlib.util.spec_from_file_location('release_package', ROOT / 'tools/release/package.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleasePackage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = load_package()

    def build(self, directory, *, windows=True, mac=True, debug=None, **patches):
        base = Path(directory)
        dll = base / 'd3d9.dll'
        dll.write_bytes(b'MZ fake proxy')
        regenerate = base / 'dist'
        regenerate.mkdir(exist_ok=True)
        if windows:
            (regenerate / 'x3m-regenerate.exe').write_bytes(b'MZ fake regenerate')
        if mac:
            (regenerate / 'x3m-regenerate').write_bytes(b'\xcf\xfa\xed\xfe fake')
        out = base / 'out'
        with contextlib.ExitStack() as stack:
            for name, value in patches.items():
                stack.enter_context(mock.patch.object(self.package, name, value))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
            code = self.package.main(['--dll', str(dll), '--out', str(out), '--regenerate-dir', str(regenerate),
                                      *(['--debug-file', str(debug)] if debug else [])])
        return code, out / f'x3m-{self.package.generate.version()}.zip'

    def test_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            code, path = self.build(directory, source_commit=lambda: 'f' * 40)
            self.assertEqual(code, 0)
            with zipfile.ZipFile(path) as archive:
                # the host (macOS) binary sits in the dist directory but never ships; no voice decoder tree either
                self.assertEqual(archive.namelist(), ENTRIES)
                self.assertEqual(self.package.ZIP_ENTRIES, ENTRIES)
                self.assertEqual(archive.read('x3m.ini'), (ROOT / 'assets/x3m.ini').read_bytes())
                for name in FONTS:
                    self.assertEqual(archive.read(f'f/{name}'), (ROOT / 'assets/fonts/generated/F' / name).read_bytes(), name)
                for name in ('OFL-NotoSans.txt', 'OFL-Exo2.txt'):
                    self.assertEqual(archive.read(name), (ROOT / 'assets/fonts' / name).read_bytes())
                    self.assertIn(b'SIL OPEN FONT LICENSE', archive.read(name).upper())
                self.assertEqual(archive.read('d3d9.dll'), b'MZ fake proxy')
                readme = archive.read('README.txt').decode()
                self.assertIn(hashlib.sha256(b'MZ fake proxy').hexdigest(), readme)
                self.assertIn('source commit ' + 'f' * 40, readme)
                self.assertIn('\r\n', readme)
                for word in ('macOS', 'voice-decoder', 'Speech', 'stripped'):
                    self.assertNotIn(word, readme)
                for text in ('ui_scale', 'text_density', 'Both are auto', 'next to X3AP.exe', 'SIL Open Font',
                             'OFL-NotoSans.txt, OFL-Exo2.txt'):
                    self.assertIn(text, readme)
                self.assertEqual(archive.getinfo('x3m-regenerate.exe').external_attr >> 16, 0o755)
            first = path.read_bytes()
            self.assertEqual(self.build(directory, source_commit=lambda: 'f' * 40)[0], 0)
            self.assertEqual(path.read_bytes(), first)  # fixed timestamps: the same inputs give the same zip

    def test_debug_file_line(self):
        with tempfile.TemporaryDirectory() as directory:
            debug = Path(directory) / 'd3d9.debug'
            debug.write_bytes(b'split debug')
            code, path = self.build(directory, mac=False, debug=debug, source_commit=lambda: 'unknown')
            self.assertEqual(code, 0)
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.namelist(), ENTRIES)
                self.assertIn('d3d9.dll is stripped of debug information; the developer keeps the matching d3d9.debug '
                              f'(SHA-256 {hashlib.sha256(b"split debug").hexdigest()}).', archive.read('README.txt').decode())
            code, _ = self.build(directory, debug=Path(directory) / 'absent.debug', source_commit=lambda: 'unknown')
            self.assertEqual(code, 1)

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            code, path = self.build(directory, windows=False, source_commit=lambda: 'unknown')
            self.assertEqual(code, 1)
            self.assertFalse(path.exists())
        stale = {ROOT / 'assets/x3m.ini': 'not the template\n'}
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(self.package.generate, 'outputs', return_value=stale):
            code, path = self.build(directory, source_commit=lambda: 'unknown')
            self.assertEqual(code, 1)
            self.assertFalse(path.exists())

    def test_font_set_is_exact(self):
        # A fonts directory with one file missing or one stray file refuses the zip.
        committed = ROOT / 'assets/fonts/generated/F'
        for change in ('missing', 'extra'):
            with tempfile.TemporaryDirectory() as directory:
                fonts = Path(directory) / 'F'
                fonts.mkdir()
                for name in FONTS[1:] if change == 'missing' else FONTS:
                    (fonts / name).write_bytes((committed / name).read_bytes())
                if change == 'extra':
                    (fonts / 'Tahoma52.tga').write_bytes(b'stray')
                code, path = self.build(directory, FONTS_DIR=fonts, source_commit=lambda: 'unknown')
                self.assertEqual(code, 1, change)
                self.assertFalse(path.exists(), change)


if __name__ == '__main__':
    unittest.main()
