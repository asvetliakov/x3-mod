"""Direct3DCreate9 factory wrapping, admission and return contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]

class FactoryTests(unittest.TestCase):
    def test_loader_delegate(self):
        source = (ROOT / 'src/proxy/loader.cpp').read_text()
        signature = 'extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk)'
        with tempfile.TemporaryDirectory(prefix='x3-loader-factory-') as directory:
            folder = Path(directory)
            (folder / 'media_startup_loader_under_test_inc.h').write_text(extract_function(source, signature))
            binary = folder / 'loader'
            subprocess.run([shutil.which('c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(folder), str(ROOT / 'verification/probe/media_startup_loader_fixture.cpp'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
