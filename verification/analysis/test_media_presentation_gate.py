"""Compile and exercise actual portable encoder/admission/transaction code."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class MediaPresentationGateTests(unittest.TestCase):
    def test_production_gate_contract(self):
        with tempfile.TemporaryDirectory(prefix='x3-media-gate-host-') as directory:
            binary = Path(directory) / 'contract'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                            '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'verification/probe/media_presentation_gate_host_fixture.cpp'),
                            str(ROOT / 'src/proxy/media_presentation_gate.cpp'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('failures=0', result.stdout)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
