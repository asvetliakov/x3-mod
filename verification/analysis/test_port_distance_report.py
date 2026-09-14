"""The port-distance runner's parser and ratio summary on a transcript excerpt.

Exercises run_port_distance.parse / summarize / conclusion / verdict on a
shortened transcript with the same line shapes the fixture prints. No Wine, no
build, no game programs or textures.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import run_port_distance as runner  # noqa: E402

SAMPLE = '''CAPS ps=ffff0300 vs=fffe0300 max_anisotropy=16 used_anisotropy=16 ps30_slots=512
PROGRAMS vs_bytes=2224 ps_bytes=5568
TEXTURE label=diffuse width=1024 height=1024 levels=11 format=DXT5 bytes=1398256 consumed=1398256
TEXTURE label=bump width=1024 height=1024 levels=11 format=DXT5 bytes=1398256 consumed=1398256
TEXTURE label=specular width=1024 height=1024 levels=11 format=DXT1 bytes=699192 consumed=699192
CONFIG name=head_on view_degrees=0.0 light_degrees=30.0 n_dot_l0=0.866025 n_dot_l1=-0.479592 mirror_dot_v=0.866025 l0_dot_l1=-0.375271
MEASURE config=head_on normal=real step=0 distance=301.1765 quad_px_w=170 quad_px_h=170 covered=28900 inner=27556 raw_mean_luma=0.200000000 raw_max_luma=2.000000000 raw_mean_alpha=0.800000000 raw_min_alpha=0.431000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.210000000 raw_all_mean_alpha=0.810000000 comp_mean_luma=0.160000000 comp_max_luma=1.500000000 comp_all_mean_luma=0.170000000
MEASURE config=head_on normal=flat step=0 distance=301.1765 quad_px_w=170 quad_px_h=170 covered=28900 inner=27556 raw_mean_luma=0.400000000 raw_max_luma=2.000000000 raw_mean_alpha=0.800000000 raw_min_alpha=0.431000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.410000000 raw_all_mean_alpha=0.810000000 comp_mean_luma=0.320000000 comp_max_luma=1.500000000 comp_all_mean_luma=0.330000000
MEASURE config=head_on normal=real step=1 distance=602.3529 quad_px_w=85 quad_px_h=85 covered=7225 inner=6561 raw_mean_luma=0.220000000 raw_max_luma=1.800000000 raw_mean_alpha=0.792000000 raw_min_alpha=0.440000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.230000000 raw_all_mean_alpha=0.800000000 comp_mean_luma=0.180000000 comp_max_luma=1.400000000 comp_all_mean_luma=0.190000000
MEASURE config=head_on normal=flat step=1 distance=602.3529 quad_px_w=85 quad_px_h=85 covered=7225 inner=6561 raw_mean_luma=0.410000000 raw_max_luma=1.800000000 raw_mean_alpha=0.792000000 raw_min_alpha=0.440000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.420000000 raw_all_mean_alpha=0.800000000 comp_mean_luma=0.330000000 comp_max_luma=1.400000000 comp_all_mean_luma=0.340000000
MEASURE config=head_on normal=real step=2 distance=1204.7059 quad_px_w=43 quad_px_h=43 covered=1849 inner=1521 raw_mean_luma=0.250000000 raw_max_luma=1.600000000 raw_mean_alpha=0.784000000 raw_min_alpha=0.450000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.260000000 raw_all_mean_alpha=0.790000000 comp_mean_luma=0.200000000 comp_max_luma=1.300000000 comp_all_mean_luma=0.210000000
MEASURE config=head_on normal=flat step=2 distance=1204.7059 quad_px_w=43 quad_px_h=43 covered=1849 inner=1521 raw_mean_luma=0.420000000 raw_max_luma=1.600000000 raw_mean_alpha=0.784000000 raw_min_alpha=0.450000000 raw_max_alpha=1.000000000 raw_all_mean_luma=0.430000000 raw_all_mean_alpha=0.790000000 comp_mean_luma=0.340000000 comp_max_luma=1.300000000 comp_all_mean_luma=0.350000000
RESULT PASS cases=6
'''


class PortDistanceReportTests(unittest.TestCase):
    def setUp(self):
        self.report = runner.parse(SAMPLE)
        self.summary = runner.summarize(self.report)

    def test_parse_collects_every_line_kind(self):
        self.assertEqual(self.report['caps']['used_anisotropy'], 16)
        self.assertEqual(self.report['programs']['ps_bytes'], 5568)
        self.assertEqual([t['label'] for t in self.report['textures']],
                         ['diffuse', 'bump', 'specular'])
        self.assertEqual([t['levels'] for t in self.report['textures']], [11, 11, 11])
        self.assertEqual(self.report['configurations'][0]['name'], 'head_on')
        self.assertEqual(len(self.report['measurements']), 6)
        self.assertEqual([m['step_label'] for m in self.report['measurements'][:3]],
                         ['1x', '1x', '2x'])
        self.assertEqual(self.report['result'], {'verdict': 'PASS', 'cases': 6})
        self.assertEqual(self.report['api_failures'], [])

    def test_ratios_are_relative_to_the_first_step(self):
        real = self.summary['head_on']['real']
        self.assertAlmostEqual(real['luma_2x'], 0.22 / 0.20)
        self.assertAlmostEqual(real['luma_4x'], 0.25 / 0.20)
        self.assertAlmostEqual(real['alpha_4x'], 0.784 / 0.800)
        self.assertAlmostEqual(real['composite_4x'], 0.20 / 0.16)
        self.assertAlmostEqual(real['max_luma_4x'], 1.6 / 2.0)
        self.assertAlmostEqual(real['luma_all_pixels_4x'], 0.26 / 0.21)
        self.assertEqual(real['raw_mean_luma'], [0.2, 0.22, 0.25])
        self.assertEqual(real['inner_pixels'], [27556, 6561, 1521])

    def test_signs_and_the_isolated_normal_channel(self):
        real = self.summary['head_on']['real']
        self.assertEqual((real['sign_luma_2x'], real['sign_luma_4x']), ('brighter', 'brighter'))
        self.assertEqual(real['sign_alpha_4x'], 'darker')
        # flat brightens 1.05x, real 1.25x, so the normal map alone contributes 1.19x.
        self.assertAlmostEqual(real['normal_channel_luma_4x'], (0.25 / 0.20) / (0.42 / 0.40))
        self.assertEqual(real['sign_normal_channel_luma_4x'], 'brighter')
        self.assertNotIn('normal_channel_luma_4x', self.summary['head_on']['flat'])

    def test_sign_of_uses_the_epsilon_band(self):
        self.assertEqual(runner.sign_of(1.0), 'unchanged')
        self.assertEqual(runner.sign_of(1 + runner.SIGN_EPSILON / 2), 'unchanged')
        self.assertEqual(runner.sign_of(1.02), 'brighter')
        self.assertEqual(runner.sign_of(0.98), 'darker')
        self.assertEqual(runner.sign_of(None), 'unknown')

    def test_conclusion_reports_one_sign_per_configuration(self):
        summary = runner.conclusion(self.summary)
        self.assertEqual(set(summary), {'head_on'})
        self.assertEqual(summary['head_on']['sign'], 'brighter')
        self.assertEqual(summary['head_on']['alpha_sign'], 'darker')

    def test_verdict_needs_three_configurations_and_both_modes(self):
        checks = runner.verdict(self.report, self.summary)
        self.assertTrue(checks['fixture_passed'])
        self.assertTrue(checks['no_api_failures'])
        self.assertTrue(checks['both_normal_modes'])
        self.assertTrue(checks['head_on_widths_as_designed'])
        self.assertTrue(checks['widths_halve_per_step'])
        self.assertTrue(checks['alpha_present'])
        self.assertFalse(checks['three_configurations'])  # the excerpt holds one

    def test_incomplete_groups_are_dropped(self):
        truncated = '\n'.join(line for line in SAMPLE.splitlines() if 'step=2' not in line)
        self.assertEqual(runner.summarize(runner.parse(truncated)), {})

    def test_recorded_input_identities(self):
        self.assertEqual(set(runner.TEXTURES), {'diffuse', 'bump', 'specular'})
        self.assertEqual(runner.TEXTURES['bump'][1],
                         '14f53a84b37b24b633c97565b2aa718c06b98e42920fb4a1510852ac3b263262')
        self.assertEqual(runner.PROGRAMS['ps'], 'ps_64bac8bb307eb896.bin')
        self.assertEqual(runner.STEP_LABELS, {0: '1x', 1: '2x', 2: '4x'})


if __name__ == '__main__':
    unittest.main()
