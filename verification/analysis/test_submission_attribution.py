"""Host-only coverage of paired view attribution and exhaustive HDR CPU buckets."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

class SubmissionAttribution(unittest.TestCase):
    def test_host_partitions_and_clock_failures(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            exe = str(pathlib.Path(directory) / 'attribution')
            built = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'verification/probe/submission_attribution_host.cpp'), '-o', exe], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([exe], text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertRegex(run.stdout, r'checks=\d+ failures=0')

    def test_readback_failure_paths_close_every_bucket(self):
        text = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        body = text[text.index('HdrFrameBegin HdrPass::begin_frame'):text.index('// The must-unwind ladder.')]
        # Unconditional adjacent boundaries cover skipped work after any HRESULT
        # failure. None occurs within a success arm; UnlockRect remains guarded.
        for name in ('TransferLock', 'ExtractUnlock', 'StatisticsAdapt'):
            self.assertEqual(body.count(f'        r.readback_timing.end(ReadbackTiming::{name}, stamp(timing));'), 1)
        self.assertLess(body.index('GetRenderTargetData)(device_'), body.index('->LockRect('))
        self.assertLess(body.index('->LockRect('), body.index('ReadbackTiming::TransferLock'))
        self.assertLess(body.index('ReadbackTiming::TransferLock'), body.index('->UnlockRect('))
        self.assertLess(body.index('->UnlockRect('), body.index('ReadbackTiming::ExtractUnlock'))
        self.assertLess(body.index('ReadbackTiming::ExtractUnlock'), body.index('const MeterStatistics m = meter_statistics('))
        self.assertLess(body.index('exposure_.step('), body.index('ReadbackTiming::StatisticsAdapt'))
        self.assertIn('r.ticks_readback = r.readback_timing.total();', body)
        self.assertIn('if (SUCCEEDED(r.readback)) {\n            // Non-finite', body)

if __name__ == '__main__':
    unittest.main()
