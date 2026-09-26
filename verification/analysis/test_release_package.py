"""The release zip (tools/release/package.py, docs/architecture/config-file.md section 6): its manifest, its README's
build line, its determinism and its refusals. Host only: fake DLL and regenerate binaries."""
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


def load_package():
    spec = importlib.util.spec_from_file_location('release_package', ROOT / 'tools/release/package.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleasePackage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = load_package()

    def build(self, directory, *, windows=True, mac=True, **patches):
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
            code = self.package.main(['--dll', str(dll), '--out', str(out), '--regenerate-dir', str(regenerate)])
        return code, out / f'x3m-{self.package.generate.version()}.zip'

    def test_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            code, path = self.build(directory, source_commit=lambda: 'f' * 40)
            self.assertEqual(code, 0)
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.namelist(), ['d3d9.dll', 'x3m.ini', 'x3m-regenerate.exe', 'x3m-regenerate', 'README.txt'])
                self.assertEqual(archive.read('x3m.ini'), (ROOT / 'assets/x3m.ini').read_bytes())
                self.assertEqual(archive.read('d3d9.dll'), b'MZ fake proxy')
                readme = archive.read('README.txt').decode()
                self.assertIn(hashlib.sha256(b'MZ fake proxy').hexdigest(), readme)
                self.assertIn('source commit ' + 'f' * 40, readme)
                self.assertIn('\r\n', readme)
                self.assertEqual(archive.getinfo('x3m-regenerate.exe').external_attr >> 16, 0o755)
            first = path.read_bytes()
            self.assertEqual(self.build(directory, source_commit=lambda: 'f' * 40)[0], 0)
            self.assertEqual(path.read_bytes(), first)  # fixed timestamps: the same inputs give the same zip

    def test_macos_binary_optional(self):
        with tempfile.TemporaryDirectory() as directory:
            code, path = self.build(directory, mac=False, source_commit=lambda: 'unknown')
            self.assertEqual(code, 0)
            with zipfile.ZipFile(path) as archive:
                self.assertNotIn('x3m-regenerate', archive.namelist())

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


if __name__ == '__main__':
    unittest.main()
