"""Nebula part deband, Decision C1 (src/proxy/sky_nebula_core.h, src/proxy/motion_output_sky_nebula_inc.h,
src/effects/sky_nebula_{vs,ps}.hlsl; docs/architecture/sector-background-modern.md "Decision C"). The core on the host
(verification/probe/sky_nebula_check.cpp: the c200-c201 / c255 packing, the identity at threshold 0, the settings'
ranges, the tap pattern field static in texture space and continuous); the programs' constant registers (the engine's
VS c0-c5 / PS c0 as set, our c255 and c200-c201 only, no time or frame input); the route's production order in
motion_output.cpp; the four settings across the schema, the core, capture.cpp and the template. No D3D, no Wine.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, head):
    start = source.index(head)
    end = source.index('\n}\n', start)
    return source[start:end]


class SkyNebulaTests(unittest.TestCase):
    def test_core(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-sky-nebula-') as temporary:
            executable = Path(temporary) / 'check'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/sky_nebula_check.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertRegex(run.stdout, r'sky_nebula_check checks=13 failures=0 ')

    def test_fixture_record(self):
        # The tracked native record (verification/results/sky-nebula/run_fixture.py): green, and the accepted
        # shortfalls of the default configuration are exactly the two recorded in the Decision C1 paragraph.
        out = (ROOT / 'verification/results/sky-nebula/fixture_out.txt').read_text()
        self.assertRegex(out, r'\nRESULT PASS checks=21 warnings=2 failures=0\n$')
        self.assertNotIn('\nFAIL ', out)
        self.assertEqual(re.findall(r'^WARN (\S+) ', out, re.M), ['edge_halo_beyond_1_texel', 'taa_sd_fast_pan'])
        # Each warning carries its hard ceiling (above it the run fails): the halo 2.0/255, the fast pan 1.25 x the
        # engine pair's figure.
        halo = re.search(r'^WARN edge_halo_beyond_1_texel measured=([0-9.]+) bound=1\.0000 ceiling=2\.0000 ', out, re.M)
        self.assertIsNotNone(halo)
        pan = re.search(r'^WARN taa_sd_fast_pan measured=([0-9.]+) bound=([0-9.]+) ceiling=([0-9.]+) speed=10\.0 '
                        r'engine_sd_max=([0-9.]+) ', out, re.M)
        self.assertIsNotNone(pan)
        engine = float(pan.group(4))
        self.assertAlmostEqual(float(pan.group(3)), max(.1, 1.25 * engine), delta=1e-3)
        self.assertLessEqual(float(pan.group(1)), float(pan.group(3)))
        self.assertLessEqual(float(halo.group(1)), 2.0)
        self.assertIn('PASS alpha_edge_no_colour_bleed ', out)

    def test_program_registers(self):
        vs = (ROOT / 'src/effects/sky_nebula_vs.hlsl').read_text()
        ps = (ROOT / 'src/effects/sky_nebula_ps.hlsl').read_text()
        self.assertEqual(re.findall(r'register\((c\d+)\)', vs), ['c0', 'c4', 'c255'])
        self.assertEqual(re.findall(r'register\(([cs]\d+)\)', ps), ['s0', 'c0', 'c200', 'c201'])
        # The engine's math: w forced to 1, four row dot products, the affine UV of c4/c5's .xyz.
        self.assertIn('float4 p = float4(i.position.xyz, 1);', vs)
        self.assertIn('o.position.xy += jitter.xy * o.position.w;', vs)
        self.assertIn('o.uv = float2(dot(t, tex_matrix[0].xyz), dot(t, tex_matrix[1].xyz));', vs)
        self.assertIn('centre.a * clamp(alpha.x, -1, 1)', ps)
        for text in (vs, ps):
            self.assertNotRegex(re.sub(r'//.*', '', text), r'\b(time|frame|seed|vPos|VPOS)\b')
        core = (ROOT / 'src/proxy/sky_nebula_core.h').read_text()
        self.assertIn('constexpr std::uint64_t part_vs = 0x7b6393fe2d3e1d85ull;', core)
        self.assertIn('constexpr std::uint64_t part_ps = 0x6109cf64c03529ddull;', core)
        self.assertIn('constexpr unsigned vs_constant_jitter = 255;', core)
        self.assertIn('constexpr unsigned ps_constant_first = 200, ps_constant_count = 2;', core)

    def test_production_order(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        before = function(source, 'MotionRoute MotionOutput::before_draw(')
        self.assertLess(before.index('evaluate_draw(call, route);'),
                        before.index('if (sky_nebula_requested_) sky_nebula_classify(call, route);'))
        # The swap is the last preparation of the submit branch, never on a composed, additive or masked draw.
        self.assertLess(before.index('prepare_screen_additive(call, route);'), before.index('prepare_sky_nebula(route);'))
        self.assertIn('if (route.sky_nebula && route.submit && !route.composition && !route.screen_additive &&\n'
                      '                !route.fog_card_mask.masked)\n                prepare_sky_nebula(route);', before)
        after = function(source, 'void MotionOutput::after_draw(')
        self.assertLess(after.index('if (route.sky_nebula_swapped) finish_sky_nebula(route, result);'),
                        after.index('if (route.sky_stars) finish_sky_stars(route, result);'))
        self.assertLess(after.index('finish_sky_nebula(route, result);'), after.index('if (!enabled_ || !route.evaluated) return;'))
        self.assertIn('sky_nebula_before_reset();', function(source, 'void MotionOutput::before_reset('))
        self.assertIn('if (sky_nebula_requested_) sky_nebula_phase_end();', source)
        self.assertIn('if (sky_nebula_requested_) sky_nebula_latch();', source)
        self.assertIn('release_sky_nebula();', source)
        fragment = (ROOT / 'src/proxy/motion_output_sky_nebula_inc.h').read_text()
        classify = function(fragment, 'void MotionOutput::sky_nebula_classify(')
        # The pair first (one compare for every other draw), then the blend/z/mask states, the star mark, the size.
        self.assertLess(classify.index('part_vs'), classify.index('D3DRS_ALPHABLENDENABLE'))
        self.assertLess(classify.index('D3DRS_COLORWRITEENABLE'), classify.index('sky_stars_stage0_'))
        self.assertLess(classify.index('sky_stars_stage0_'), classify.index('part_width_min'))
        self.assertLess(classify.index('part_width_min'), classify.index('++sky_nebula_outside_'))
        for why in ('"routed"', '"queries"', '"composition"', '"targets"', '"fog"', '"projected"', '"attach"'):
            self.assertIn(why, classify)

    def test_settings_plumbing(self):
        sys.path.insert(0, str(ROOT / 'tools/config'))
        import schema
        entries = {e['key']: e for e in schema.SETTINGS}
        core = (ROOT / 'src/proxy/sky_nebula_core.h').read_text()
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        ini = (ROOT / 'assets/x3m.ini').read_text()
        self.assertEqual(entries['sky_nebula_deband']['builtin'], '1')
        self.assertEqual(entries['sky_nebula_deband']['requires'], ('taa', 'hdr'))
        self.assertIn(';sky_nebula_deband = 0', ini)
        self.assertTrue(entries['sky_nebula_jitter']['developer'])
        self.assertIn('L"X3M_SKY_NEBULA_JITTER"', capture)
        # The hash rotation: developer, default off (one fixed tap pattern), reaching c200.w through pixel_constants.
        e = entries['sky_nebula_deband_hash']
        self.assertTrue(e['developer'])
        self.assertEqual((e['type'], e['builtin'], e['requires']), ('bool', '0', ('sky_nebula_deband',)))
        self.assertIn('L"X3M_SKY_NEBULA_DEBAND_HASH"', capture)
        self.assertIn('sky_nebula_hash = h == 1 && value[0] == L\'1\';', capture)
        self.assertIn('bool hash = false) noexcept {', core)
        fragment = (ROOT / 'src/proxy/motion_output_sky_nebula_inc.h').read_text()
        self.assertIn('sky_nebula_texture_height_, pixel, sky_nebula_hash_)', fragment)
        self.assertIn('sincos(1.57079633 * h * deband.w, d.y, d.x);', (ROOT / 'src/effects/sky_nebula_ps.hlsl').read_text())
        self.assertIn('L"X3M_SKY_NEBULA_DEBAND"', capture)
        for name in ('threshold', 'radius'):
            key = 'sky_nebula_deband_' + name
            e = entries[key]
            m = re.search(r'constexpr float %s_default = ([0-9.]+)f, %s_min = ([0-9.]+)f, %s_max = ([0-9.]+)f;'
                          % (name, name, name), core)
            self.assertIsNotNone(m, name)
            default, low, high = (float(v) for v in m.groups())
            self.assertEqual(float(e['builtin']), default, key)
            self.assertEqual((e['range'][0][0], e['range'][0][1]), (low, high), key)
            self.assertEqual(e['requires'], ('sky_nebula_deband',), key)
            self.assertIn('L"X3M_SKY_NEBULA_DEBAND_%s", x3m::sky_nebula::core::accept_%s' % (name.upper(), name), capture)
            self.assertIn(' %s_setting=%%s' % name, capture, key)
            self.assertIn(';%s = %s' % (key, e['builtin']), ini, key)


if __name__ == '__main__':
    unittest.main()
