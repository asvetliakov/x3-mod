"""Host tests of the default-off frame-time diagnostic: the window reduction in
src/proxy/frame_timing.h compiled and run on the host, and the --frame-timing
launcher option. No game, no Wine, no device."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FrameTimingWindow(unittest.TestCase):
    def test_window_statistics_slow_count_and_ring(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-frame-timing-') as temporary:
            executable = Path(temporary) / 'frame_timing_host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(ROOT / 'verification/probe/frame_timing_host.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'frame_timing_host checks=43 failures=0\n')
            self.assertEqual(run.stderr, '')

    def test_production_call_sites_and_schema(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # Per-frame sample outside the 300-frame frame_end cadence, timing
        # around the forwarded Present only, primitives from the draw path.
        self.assertIn('frame_timing::frame(ctx.frame,ctx.draws);', capture)
        # present_begin ahead of cpu.before_original (pre-call instrumentation
        # must not alter the native input state), present_end after after_original.
        present = capture.split('const HRESULT hr=fn(d,a,b,w,r);')
        self.assertEqual(len(present), 2)
        self.assertLess(present[0].rindex('frame_timing::present_begin();'), present[0].rindex('cpu.before_original();'))
        self.assertTrue(present[1].startswith('cpu.after_original();\n    frame_timing::present_end();'), present[1][:120])
        self.assertIn('frame_timing::draw(primitives);', capture)
        self.assertIn('frame_timing::initialize();', capture)
        self.assertEqual(capture.count('log("frame_end device=%llu frame=%llu draws=%llu'), 1)
        source = (ROOT / 'src/proxy/frame_timing.cpp').read_text()
        self.assertIn('frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu '
                      'draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu '
                      'present_max_us=%llu slow=%u', source)
        self.assertIn('frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu', source)
        self.assertIn('X3M_FRAME_TIMING', source)


class FrameTimingLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--frame-timing')
            self.assertEqual(code, 2)
            self.assertIn('--frame-timing requires --telemetry', error)
            code, output, error = helper.launch(directory, '--frame-timing', '--telemetry')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_TIMING'], '1')
            code, output, error = helper.launch(directory, inherited={'X3M_FRAME_TIMING': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_TIMING'], '0')


if __name__ == '__main__':
    unittest.main()
