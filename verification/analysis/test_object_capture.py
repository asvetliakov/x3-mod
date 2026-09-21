"""Capture-only target, ancestor and distance fields: no game or GPU execution."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]

class ObjectCaptureTests(unittest.TestCase):
    def test_checked_read_bounds_identity_and_frame_cache(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-object-capture-') as temporary:
            executable = Path(temporary) / 'host'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT / 'src/proxy'), str(ROOT / 'verification/probe/object_capture_host.cpp'),
                                     '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r'^object_capture_host checks=\d+ failures=0 cache_bytes=\d+\n$')
            self.assertEqual(result.stderr, '')

    def test_actual_context_preserves_last_error_and_emits_once(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='x3-object-context-') as temporary:
            directory = Path(temporary)
            (directory / 'object_capture_context_under_test_inc.h').write_text(
                extract_function(source, 'void object_context('))
            executable = directory / 'host'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT / 'src/proxy'), '-I', str(directory),
                                     str(ROOT / 'verification/probe/object_capture_context_host.cpp'), '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout, 'object_capture_context_host checks=9 failures=0\n')
            self.assertEqual(result.stderr, '')

    def test_capture_gate_reset_and_fixed_function_state(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        snapshot = source[source.index('void snapshot('):source.index('void snapshot(')+1800]
        self.assertLess(snapshot.index('if (!ctx.capture) return;'), snapshot.index('object_context(ctx);'))
        reset = source[source.index('HRESULT reset_common('):source.index('HRESULT reset_common(')+1200]
        self.assertLess(reset.index('ctx.object_evidence.invalidate();'), reset.index('if(ctx.bloom_busy || ctx.motion_output.composition_operation_active())'))
        self.assertIn('D3DRS_FOGENABLE,D3DRS_ZENABLE', source)
        self.assertIn('if(matrices){out->parent=node[0x18/4];out->alpha13c=node[0x13c/4];}',
                      (ROOT / 'src/proxy/object_trace.cpp').read_text())

if __name__ == '__main__':
    unittest.main()
