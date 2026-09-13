"""Execute production write_back control flow with scripted device outcomes.

The real header, function body and exposure implementation are compiled by the
host compiler. Only D3D/copy_draw/bind are fakes: this does not qualify GPU
pixels, restoration correctness or the native Windows ABI. No Wine is needed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HdrDisplaySnapshotTests(unittest.TestCase):
    def test_writeback_contract_and_null_output_parity(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        start = source.index('HdrWriteback HdrPass::write_back(')
        end = source.index('\n#ifdef X3M_MOTION_OUTPUT_FIXTURE\nHRESULT HdrPass::fixture_readback', start)
        with tempfile.TemporaryDirectory(prefix='x3-hdr-display-') as temporary:
            directory = Path(temporary)
            (directory / 'hdr_writeback_under_test_inc.h').write_text(source[start:end])
            executable = directory / 'display'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I', str(ROOT / 'verification/probe/hdr_display_snapshot_stubs'),
                       '-I', str(directory),
                       str(ROOT / 'verification/probe/hdr_display_snapshot_fixture.cpp'),
                       str(ROOT / 'src/renderer/exposure.cpp'), '-o', str(executable)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            run = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertRegex(run.stdout, r'^hdr_display_snapshot scenarios=52 checks=\d+ failures=0\n$')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
