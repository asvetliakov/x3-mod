"""Production startup controller and retained loader delegate host checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]

class StartupTests(unittest.TestCase):
    def test_controller(self):
        with tempfile.TemporaryDirectory(prefix='x3-startup-host-') as directory:
            binary = Path(directory) / 'startup'
            subprocess.run([shutil.which('c++'), '-std=c++17', '-O2', '-Wall',
                            '-Wextra', '-Werror',
                            str(ROOT / 'verification/probe/media_startup_host_fixture.cpp'),
                            str(ROOT / 'src/proxy/media_startup.cpp'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_loader_delegate(self):
        source = (ROOT / 'src/proxy/loader.cpp').read_text()
        signature = 'extern "C" IDirect3D9* WINAPI x3m_direct3d_create9_body(UINT sdk)'
        with tempfile.TemporaryDirectory(prefix='x3-startup-loader-') as directory:
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
