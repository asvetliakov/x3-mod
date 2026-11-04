"""Route side of the sky stars (src/proxy/motion_output_sky_stars_inc.h; docs/architecture/sector-background-modern.md,
"Implementation A"): the real fragment and motion_output.h compiled on the host with a scripted SetRenderState and a
scripted pass (verification/probe/motion_sky_stars_fixture.cpp): a background draw with the star texture is masked,
forwarded, unmasked and followed by exactly one batch; outside the phase, without the HDR latch or with a second RT it
is never masked; a stale bind bit cannot survive an early-returning draw; a refused pass leaves the draw unmasked; a
failed restore quarantines until Reset; the table's refusals reach the row. The production order the fixture replays is
checked here in motion_output.cpp and capture.cpp. No D3D, no Wine.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, head):
    start = source.index(head)
    end = source.index('\n}\n', start)
    return source[start:end]


class MotionSkyStarsTests(unittest.TestCase):
    def test_route_fixture(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-motion-sky-stars-') as temporary:
            executable = Path(temporary) / 'route'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT / 'verification/probe/motion_sky_stars_stubs'),
                                    str(ROOT / 'verification/probe/motion_sky_stars_fixture.cpp'),
                                    str(ROOT / 'src/renderer/exposure.cpp'),
                                    str(ROOT / 'src/renderer/motion_row_history.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertRegex(run.stdout, r'^motion_sky_stars scenarios=9 checks=48 failures=0\n$')

    def test_production_order(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        before = function(source, 'MotionRoute MotionOutput::before_draw(')
        # F2: the bind bit is taken before any early return, so no path leaves it for the next draw.
        self.assertLess(before.index('sky_stars_draw_begin();'), before.index('return route;'))
        self.assertLess(before.index('evaluate_draw(call, route);'),
                        before.index('if (sky_stars_requested_) sky_stars_classify(call, route);'))
        self.assertLess(before.index('prepare_fog_card(call, route);'),
                        before.index('if (route.sky_stars && !route.fog_card_mask.masked) prepare_sky_stars(route);'))
        self.assertLess(before.index('prepare_sky_stars(route);'), before.index('prepare_composition(call, route);'))
        after = function(source, 'void MotionOutput::after_draw(')
        self.assertLess(after.index('if (route.sky_stars) finish_sky_stars(route, result);'),
                        after.index('if (route.fog_card_mask.masked) finish_fog_card(route, result);'))
        self.assertIn('sky_stars_before_reset();', function(source, 'void MotionOutput::before_reset('))
        # Every draw entry (the four draw hooks and their blocked paths) goes through before_draw.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('ctx.motion_output.before_draw('), 8)


if __name__ == '__main__':
    unittest.main()
