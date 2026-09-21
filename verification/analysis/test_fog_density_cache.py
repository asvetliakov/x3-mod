"""Stored-density fog cache manager (src/fog/fog_density_cache.cpp): window, recentre,
readiness and upload-budget logic on the deterministic stepped worker, then the real
worker-thread handoff, shutdown and invalidation. Native clang++ build; no Wine, no D3D.
"""
import re, shutil, subprocess, tempfile, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / 'verification/probe/fog_density_cache_host.cpp', ROOT / 'src/fog/fog_density_cache.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-pthread']


def fields(text, tag):
    line = re.search(r'^%s (.*)$' % tag, text, re.M).group(1)
    return {k: float(v) for k, v in re.findall(r'(\w+)=([-0-9.e+]+)', line)}


class FogDensityCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++')
        if compiler is None: raise RuntimeError('clang++ is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='x3-fog-cache-')
        cls.tool = Path(cls.temporary.name) / 'cache-host'
        subprocess.run([compiler] + FLAGS + [str(s) for s in SOURCES] + ['-o', str(cls.tool)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_mode(self, mode):
        done = subprocess.run([str(self.tool), mode], capture_output=True, timeout=600)  # a deadlock is a timeout
        text = done.stdout.decode()
        failed = re.findall(r'^CHECK (\S+) FAIL', text, re.M)
        self.assertEqual((done.returncode, failed), (0, []), text[-2000:])
        self.assertRegex(text, r'RESULT PASS checks=\d+ failures=0')
        return text

    def test_window_recentre_readiness_logic(self):
        text = self.run_mode('logic')
        fill = fields(text, 'FILL')
        self.assertEqual(fill['nodes'], 2 * 128 ** 3)  # every node once, no refill
        self.assertLess(fill['far_resident_frame'], fill['fine_resident_frame'])
        self.assertLessEqual(fill['max_upload_bytes'], 8 * 129 * 129 * 8)
        self.assertLessEqual(fields(text, 'TIGHT_BUDGET')['max_upload_bytes'], 129 * 129 * 8)  # the cap includes the first rectangle
        for name in ('has_work_false_mid_fill_is_not_idle', 'committed_but_not_uploaded_is_not_resident', 'camera_limit_fills_and_equals_static'):
            self.assertIn('CHECK %s PASS' % name, text)
        moves = re.findall(r'^RECENTRE move=\d nodes=(\d+) expected=(\d+)', text, re.M)
        self.assertEqual(len(moves), 5)
        self.assertTrue(all(a == b == str(128 ** 3 - 126 ** 3) for a, b in moves))
        self.assertNotRegex(text, r'differing_bytes=[1-9]')
        self.assertGreaterEqual(len(re.findall(r'^CHECK ', text, re.M)), 60)

    def test_worker_thread_handoff_and_shutdown(self):
        text = self.run_mode('threaded')
        t = fields(text, 'THREADED')
        self.assertEqual(t['nodes'], 2 * 128 ** 3)
        self.assertLessEqual(t['max_upload_bytes'], 8 * 129 * 129 * 8)
        self.assertLess(fields(text, 'SHUTDOWN')['worst_stop_ms'], 500)
        self.assertNotRegex(text, r'differing_bytes=[1-9]')

    def test_device_release_path_detaches_the_fog_pass(self):
        # The worker, both caches and the DEFAULT atlases must go with the other passes, not with ~MotionOutput.
        text = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        body = text[text.index('void MotionOutput::release_resources() noexcept {'):]
        body = body[:body.index('\n}\n')]
        self.assertIn('fog_->detach();', body)
        self.assertIn('fog_.reset();', body)
        header = (ROOT / 'src/renderer/fog_pass.h').read_text()
        self.assertIn('DLL_PROCESS_DETACH', header[:header.index('void abandon_density_worker()')])


if __name__ == '__main__':
    unittest.main()
