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
    def test_window_statistics_buckets_and_live_accumulation(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-frame-timing-') as temporary:
            executable = Path(temporary) / 'frame_timing_host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                # The probe compiles src/proxy/frame_timing.cpp itself against
                # the Win32 stand-in, so the accumulation is executed here.
                '-I', str(ROOT / 'verification/probe/frame_timing_standin'),
                str(ROOT / 'verification/probe/frame_timing_host.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'frame_timing_host checks=88 failures=0\n')
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
        # Bucket scopes: the two shared dispatch guards carry the scope of the
        # hooked entry point, with the lock member declared first so the stamps
        # are taken under the hook mutex; the draw hooks and the scene-end path
        # override the default State bucket.
        for guard, held in (('struct HookGuard {', 'HeldHookLock held;'), ('struct PlainHookGuard {', 'std::lock_guard<std::recursive_mutex> lock{mutex};')):
            body = capture[capture.index(guard):]
            body = body[:body.index('\n};')]
            self.assertLess(body.index(held), body.index('frame_timing::Scope timing;'), guard)
            self.assertIn('frame_timing::Bucket bucket=frame_timing::Bucket::State', body)
            self.assertIn('const char* entry=__builtin_FUNCTION()', body)
        self.assertEqual(capture.count('HookGuard lock(frame_timing::Bucket::Draw);'), 4)
        self.assertEqual(capture.count('HookGuard lock(frame_timing::Bucket::Scene);'), 2)
        for entry in ('scene_end_signal', 'compositor_pre', 'compositor_post'):
            self.assertIn(f'frame_timing::Scope timing(frame_timing::Bucket::Scene,"{entry}");', capture)
        # The native-draw stamps keep the Present pattern: begin ahead of
        # before_original, end after after_original, outside the CPU envelope.
        self.assertEqual(capture.count('frame_timing::draw_native_begin();'), 4)
        self.assertEqual(capture.count('frame_timing::draw_native_end();'), 4)
        for piece in capture.split('frame_timing::draw_native_begin();')[1:]:
            head = piece[:piece.index('frame_timing::draw_native_end();')]
            self.assertLess(head.index('cpu.before_original();'), head.index('cpu.after_original();'))
            self.assertNotIn('cpu.after_original();', piece[piece.index('frame_timing::draw_native_end();'):][:40])
        self.assertIn('frame_timing::initialize();', capture)
        self.assertEqual(capture.count('log("frame_end device=%llu frame=%llu draws=%llu'), 1)
        source = (ROOT / 'src/proxy/frame_timing.cpp').read_text()
        self.assertIn('frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu '
                      'draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu '
                      'present_max_us=%llu', source)
        self.assertIn('" draw_p50_us=%llu draw_p95_us=%llu draw_max_us=%llu draw_native_p50_us=%llu draw_native_max_us=%llu"', source)
        self.assertIn('" scene_p50_us=%llu scene_p95_us=%llu scene_max_us=%llu state_p50_us=%llu state_p95_us=%llu state_max_us=%llu"', source)
        self.assertIn('" draw_calls_p50=%llu scene_calls_p50=%llu state_calls_p50=%llu slow=%u"', source)
        self.assertIn('frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu', source)
        self.assertIn('" draw_us=%llu draw_native_us=%llu scene_us=%llu state_us=%llu"', source)
        self.assertIn('" draw_calls=%llu scene_calls=%llu state_calls=%llu slow_call=%s slow_call_us=%llu"', source)
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
