"""Route side of the sky stars (src/proxy/motion_output_sky_stars_inc.h; docs/architecture/sector-background-modern.md,
"Implementation A"): the real fragment and motion_output.h compiled on the host with a scripted SetRenderState and a
scripted pass (verification/probe/motion_sky_stars_fixture.cpp): a background draw with the star texture is masked,
forwarded, unmasked and followed by exactly one batch; outside the phase, without the HDR latch or with a second RT it
is never masked; a stale bind bit cannot survive an early-returning draw; a refused pass leaves the draw unmasked; a
failed restore quarantines until Reset; the table's refusals reach the row; c251 carries the wall clock modulo 128 s and
the twinkle amplitude; refused settings fall back to the defaults; the per-sector seed (a known key rebuilds once, the
same key is the cached build, unknown, no sector and loading hold the field, a load or stall drops the reader's cache,
one sky_stars_sector row per change, key= on the ready row) and the one-shot sky_stars_phase_draw rows. The production order the fixture replays is checked
here in motion_output.cpp and capture.cpp, and the five look settings across the schema, the core header and the
settings parse. No D3D, no Wine.
"""
from pathlib import Path
import re
import shutil
import sys
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
        self.assertRegex(run.stdout, r'^motion_sky_stars scenarios=17 checks=138 failures=0\n$')

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
        # Decision C2: the deferred batch is offered the Clear after restore_bindings() and the Clear's bindings, before
        # the native Clear (the hook forwards after before_clear returns); the confirmation is the phase end's.
        clear = function(source, 'void MotionOutput::before_clear(')
        self.assertLess(clear.index('restore_bindings();'), clear.index('bindings(pending_);'))
        self.assertLess(clear.index('bindings(pending_);'),
                        clear.index('if (sky_stars_frame_pending_) sky_stars_phase_end_draw(count, flags);'))
        # Every draw entry (the four draw hooks and their blocked paths) goes through before_draw.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('ctx.motion_output.before_draw('), 8)

    def test_twinkle_constant_production(self):
        # c251 from the QPC reading taken for the draw (wall time, never the game clock) and the configured amplitude,
        # saved and restored with c252-c255 in one five-register bracket.
        fragment = (ROOT / 'src/proxy/motion_output_sky_stars_inc.h').read_text()
        finish = function(fragment, 'void MotionOutput::finish_sky_stars(')
        self.assertIn('sky_stars::core::twinkle_constants(std::uint64_t(begin.QuadPart), engine_qpc_frequency(), '
                      'sky_stars_twinkle_,', finish)
        self.assertLess(finish.index('QueryPerformanceCounter(&begin);'), finish.index('twinkle_constants('))
        self.assertIn('sky_stars_->draw(constants, twinkle, &report);', finish)
        self.assertNotRegex(finish, r'game_time|seta|engine_clock_')
        source = (ROOT / 'src/renderer/sky_stars_pass.cpp').read_text()
        self.assertIn('constexpr UINT constants_first = 251, constants_count = 5;', source)
        # The in-place batch brackets c251-c255, the deferred one c250-c255 (Decision C2), in one Get/Set/restore each.
        self.assertIn('const UINT first = deferred ? constants_first - 1u : constants_first;', source)
        self.assertIn('const UINT count = deferred ? constants_count + 1u : constants_count;', source)
        self.assertEqual(source.count('(d, first, '), 3)  # save, set, restore
        self.assertEqual(source.count('(d, constants_first, '), 1)  # the attach probe
        self.assertNotIn('(d, 252,', source)
        # The deferred phase-end batch reads the same wall clock.
        fragment_end = function(fragment, 'void MotionOutput::sky_stars_phase_end_draw(')
        self.assertIn('sky_stars::core::twinkle_constants(std::uint64_t(begin.QuadPart), engine_qpc_frequency(), '
                      'sky_stars_twinkle_,', fragment_end)
        self.assertNotRegex(fragment_end, r'game_time|seta|engine_clock_')

    def test_settings_plumbing(self):
        sys.path.insert(0, str(ROOT / 'tools/config'))
        import schema
        entries = {e['key']: e for e in schema.SETTINGS}
        core = (ROOT / 'src/proxy/sky_stars_core.h').read_text()
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        ini = (ROOT / 'assets/x3m.ini').read_text()
        for name in ('density', 'brightness', 'size', 'min_size', 'twinkle', 'clustering'):
            key = 'sky_stars_' + name
            e = entries[key]
            m = re.search(r'constexpr float %s_default = ([0-9.]+)f, %s_min = ([0-9.]+)f, %s_max = ([0-9.]+)f;'
                          % (name, name, name), core)
            self.assertIsNotNone(m, name)
            default, low, high = (float(v) for v in m.groups())
            self.assertEqual(float(e['builtin']), default, key)
            self.assertEqual((e['range'][0][0], e['range'][0][1]), (low, high), key)
            self.assertEqual(e['requires'], ('sky_stars',), key)
            self.assertIn('L"X3M_SKY_STARS_%s", x3m::sky_stars::core::accept_%s' % (name.upper(), name),
                          capture, key)
            self.assertIn(' %s_setting=%%s' % name, capture, key)
            self.assertIn(';%s = %s' % (key, e['builtin']), ini, key)
        # Decision C2: k (user) and L0 (developer), the core's ranges and defaults, read once, tagged in the config row.
        for key, name, developer in (('sky_stars_nebula_dim', 'nebula_dim', False),
                                     ('sky_stars_nebula_dim_level', 'nebula_dim_level', True)):
            e = entries[key]
            m = re.search(r'constexpr float %s_default = ([0-9.]+)f, %s_min = ([0-9.]+)f, %s_max = ([0-9.]+)f;'
                          % (name, name, name), core)
            self.assertIsNotNone(m, name)
            default, low, high = (float(v) for v in m.groups())
            self.assertEqual(float(e['builtin']), default, key)
            self.assertEqual((e['range'][0][0], e['range'][0][1]), (low, high), key)
            self.assertEqual(e['developer'], developer, key)
            self.assertEqual(e['requires'], ('sky_stars',), key)
            self.assertIn('L"X3M_SKY_STARS_%s",' % name.upper(), capture, key)
            self.assertIn('x3m::sky_stars::core::accept_%s' % name, capture, key)
            self.assertIn(' %s_setting=%%s' % name, capture, key)
            self.assertEqual((';%s = %s' % (key, e['builtin'])) in ini, not developer, key)


if __name__ == '__main__':
    unittest.main()
