#!/usr/bin/env python3
"""Tests for tools/analysis/analyze_iteration13.py.

The first groups run the derivations on synthetic analyze_iteration12 /
analyze_motion_readback / analyze_loading_profile / analyze_iteration11
reports, so every table the iteration-13 document prints has its arithmetic
covered without reading a capture. The last group pins the published summary
(verification/results/iteration-13.json): the present readback's agreement
with the RCAS reference, the neighbourhood contract at every sharpen value,
the monotonicity of the A/B in the sharpen value, and where the readback
certification's suspicious displacement pixels sit.
"""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))

import analyze_iteration13 as it13  # noqa: E402

SUMMARY = ROOT / 'verification' / 'results' / 'iteration-13.json'


def edge(rise, mtf, above=0.1, edges=100):
    return {'edges': edges, 'rise_10_90_px': rise, 'mtf50_cycles_per_px': mtf,
            'overshoot': {'above_one': above, 'below_zero': 0.0, 'window_px': 2.0, 'edges': edges}}


def per_frame(number, source='present_readback', escapes=0, changed=0.2, max_change=10,
              mean_change=0.1, gradients=(1.1, 0.66, 0.6), raw=(0.8, 1.2), resolved=(1.2, 0.3),
              presented=(1.0, 0.35), strong=(1000, 1.02, 0), error=None):
    item = {
        'frame': number, 'source': source, 'escapes_3x3': escapes,
        'change_vs_unsharpened': {'changed_pixels': 1, 'max_code_change': max_change,
                                  'mean_abs_code_change': mean_change, 'changed_fraction': changed},
        'gradient': {'routed_interior': {'presented_over_resolved': gradients[0],
                                         'presented_over_raw': gradients[1],
                                         'resolved_over_raw': gradients[2]},
                     'all': {}, 'routed_edge': {}},
        'edge_spread': {'raw': edge(*raw, above=0.5), 'resolved': edge(*resolved, above=0.2),
                        'presented': edge(*presented, above=0.25)},
        'strong_edges': {'threshold_luma_range': 0.2, 'pixels': strong[0],
                         'local_range_ratio': strong[1], 'channel_escapes_3x3': strong[2]},
    }
    if error is not None:
        item['readback_vs_model'] = {'max_code_error': error[0], 'mean_code_error': error[1]}
    return item


def burst(first=100, motion='stationary', cut=0.0, frames=2, flicker_energy=1.08, **kwargs):
    numbers = list(range(first, first + frames))
    classes = {'sentinel': 800000, 'routed_interior': 100000, 'routed_edge': 80000}
    return {
        'frames': numbers, 'status': 'evaluated',
        'route': {'motion': motion, 'cut_median_px_peak': cut, 'camera_rotation_deg': [1.0, 1.5]},
        'classes': classes, 'sources': ['present_readback'],
        'escapes_3x3_total': 0,
        'per_frame': [per_frame(n, **kwargs) for n in numbers],
        'flicker': {
            'presented_over_resolved_energy': flicker_energy,
            'stationary_gate_0_01_px': cut <= 0.01,
            'classes': {'routed_interior': {'pixels': 100000,
                                            'aggregate_ratio': {'resolved': 0.05, 'presented': 0.055},
                                            'presented_over_resolved': 1.1}},
        },
    }


def report(sharpen=1.0, gain=1.0, bursts=None, mip=None, features=None):
    return {
        'features': features or {'taa_sharpen': sharpen, 'mip_bias': -0.5},
        'log': '/tmp/x/session.log',
        'line_kinds': 87, 'failure_kinds': [],
        'sharpen': {'sharpened_resolved_frames': 10, 'sharpen_failed_lines': 0, 'taa_failed_lines': 0,
                    'taa_copy_S_FALSE_records': 10, 'taa_copy_back_us_median': 0.0,
                    'taa_run_us_median': 370.0},
        'mip_bias': mip or {'bias_values': ['-0.5'], 'biased_draws': 100, 'routed_draws': 100,
                            'failures': 0, 'biased_now_nonzero_records': 0},
        'bursts': [{'frames': b['frames'], 'route': b['route']} for b in (bursts or [])],
        'presented': {'status': 'evaluated', 'sharpen': sharpen, 'gain': gain,
                      'bursts': bursts if bursts is not None else []},
    }


class HelpersTest(unittest.TestCase):
    def test_stat_span_skips_none(self):
        self.assertEqual(dict(it13.stat_span([1.0, None, 3.0])), {'min': 1.0, 'max': 3.0, 'mean': 2.0})
        self.assertIsNone(it13.stat_span([None]))

    def test_recovery_share_signs(self):
        # rise: the resolve raises it (0.8 -> 1.2), the sharpen gives half back.
        self.assertAlmostEqual(it13.recovery_share(0.8, 1.2, 1.0, 0.05), 0.5)
        # mtf50: the resolve lowers it (1.0 -> 0.3), a quarter recovered.
        self.assertAlmostEqual(it13.recovery_share(1.0, 0.3, 0.475, 0.02), 0.25)
        self.assertAlmostEqual(it13.recovery_share(0.8, 1.2, 1.2, 0.05), 0.0)

    def test_recovery_share_needs_a_loss(self):
        self.assertIsNone(it13.recovery_share(1.0, 1.01, 1.2, 0.05))
        self.assertIsNone(it13.recovery_share(None, 1.2, 1.0, 0.05))


class ModelAgreementTest(unittest.TestCase):
    def test_half_a_code_passes_with_the_rounding_epsilon(self):
        # A reference value on the .5 quantiser boundary reads half a code out,
        # and floating point can put the difference a hair above 0.5.
        agreement = it13.model_agreement(report(bursts=[
            burst(error=(0.5 + 4e-6, 0.1)), burst(first=200, error=(0.5, 0.12))]))
        self.assertEqual(agreement['status'], 'pass')
        self.assertEqual(agreement['frames_outside_tolerance'], 0)
        self.assertEqual(agreement['frames_measured'], 4)
        self.assertEqual(agreement['frames_modelled_only'], 0)
        self.assertAlmostEqual(agreement['worst_max_code_error'], 0.5 + 4e-6)
        self.assertAlmostEqual(agreement['worst_excess_over_half_code'], 4e-6)
        self.assertAlmostEqual(agreement['mean_code_error']['max'], 0.12)

    def test_a_real_disagreement_fails(self):
        agreement = it13.model_agreement(report(bursts=[burst(error=(0.9, 0.3))]))
        self.assertEqual(agreement['status'], 'fail')
        self.assertEqual(agreement['frames_outside_tolerance'], 2)
        self.assertEqual(agreement['bursts'][0]['frames_outside_tolerance'], 2)

    def test_a_modelled_capture_is_unavailable(self):
        agreement = it13.model_agreement(report(bursts=[burst(source='rcas_model')]))
        self.assertEqual(agreement['status'], 'unavailable')
        self.assertEqual(agreement['frames_modelled_only'], 2)
        self.assertIsNone(agreement['worst_max_code_error'])


class BurstRowTest(unittest.TestCase):
    def test_row_condenses_the_per_frame_metrics(self):
        row = it13.burst_row(burst(frames=2, error=(0.5, 0.1)))
        self.assertEqual(row['status'], 'evaluated')
        self.assertEqual(row['frames'], [100, 101])
        self.assertEqual(row['frames_n'], 2)
        self.assertEqual(row['routed_interior_px'], 100000)
        self.assertEqual(row['max_code_change'], 10)
        self.assertEqual(row['escapes_3x3_total'], 0)
        self.assertEqual(row['strong_edge_channel_escapes'], 0)
        self.assertAlmostEqual(row['gradient_presented_over_resolved']['mean'], 1.1)
        self.assertAlmostEqual(row['raw_rise_10_90_px']['mean'], 0.8)
        self.assertAlmostEqual(row['presented_mtf50_cycles_per_px']['mean'], 0.35)
        self.assertAlmostEqual(row['presented_esf_excursion']['mean'], 0.25)
        # rise 0.8 -> 1.2 -> 1.0 is half the loss; mtf 1.2 -> 0.3 -> 0.35 is 1/18.
        self.assertAlmostEqual(row['rise_recovery_share']['mean'], 0.5)
        self.assertAlmostEqual(row['mtf50_recovery_share']['mean'], 0.05 / 0.9)
        self.assertAlmostEqual(row['flicker']['presented_over_resolved_energy'], 1.08)
        self.assertTrue(row['flicker']['stationary_gate_0_01_px'])

    def test_unevaluated_burst_keeps_its_status(self):
        row = it13.burst_row({'frames': [5, 6], 'status': 'missing_readbacks',
                              'route': {'motion': 'slow', 'cut_median_px_peak': 2.0},
                              'missing': ['a', 'b']})
        self.assertEqual(row['status'], 'missing_readbacks')
        self.assertEqual(row['missing'], ['a', 'b'])
        self.assertNotIn('gradient_presented_over_resolved', row)

    def test_image_source_follows_the_burst_sources(self):
        rows = it13.ab_rows({'measured': report(bursts=[burst(error=(0.5, 0.1))])})
        self.assertEqual(rows['measured']['100']['image_source'], 'present_readback')
        modelled = report(sharpen=0.5, gain=0.5, bursts=[burst(source='rcas_model')])
        modelled['presented']['bursts'][0]['sources'] = ['rcas_model']
        rows = it13.ab_rows({'model': modelled})
        self.assertEqual(rows['model']['100']['image_source'], 'rcas_model')
        self.assertEqual(rows['model']['100']['sharpen'], 0.5)


class SameSceneTest(unittest.TestCase):
    def rows(self):
        low = report(sharpen=0.5, gain=0.5, bursts=[
            burst(gradients=(1.05, 0.63, 0.6), presented=(1.1, 0.32), strong=(1000, 1.015, 0),
                  flicker_energy=1.03)])
        high = report(sharpen=1.0, gain=1.0, bursts=[
            burst(gradients=(1.15, 0.69, 0.6), presented=(0.95, 0.38), strong=(1000, 1.035, 0),
                  flicker_energy=1.12)])
        return it13.ab_rows({'low': low, 'high': high})

    def test_deltas_are_high_minus_low(self):
        rows = self.rows()
        same = it13.same_scene(rows['low'], rows['high'])
        entry = same['100']
        self.assertEqual((entry['sharpen_low'], entry['sharpen_high']), (0.5, 1.0))
        g = entry['gradient_presented_over_resolved']
        self.assertAlmostEqual(g['low'], 1.05)
        self.assertAlmostEqual(g['high'], 1.15)
        self.assertAlmostEqual(g['delta'], 0.1)
        self.assertAlmostEqual(entry['strong_edge_local_range_ratio']['delta'], 0.02)
        self.assertAlmostEqual(entry['presented_rise_10_90_px']['delta'], -0.15)
        energy = entry['flicker_presented_over_resolved_energy']
        self.assertAlmostEqual(energy['low'], 1.03)
        self.assertAlmostEqual(energy['high'], 1.12)
        self.assertTrue(energy['gate_met'])
        self.assertEqual(entry['escapes_3x3_total'], {'low': 0, 'high': 0})

    def test_unpaired_bursts_are_skipped(self):
        rows = self.rows()
        self.assertEqual(it13.same_scene({}, rows['high']), {})


class OversharpeningTest(unittest.TestCase):
    def test_square_law_and_excursions(self):
        rows = it13.ab_rows({'high': report(bursts=[
            burst(gradients=(1.2, 0.7, 0.6), flicker_energy=1.5)])})
        table = it13.oversharpening(rows)['high']['100']
        self.assertAlmostEqual(table['square_law_prediction'], 1.44)
        self.assertAlmostEqual(table['energy_over_square_law'], 1.5 / 1.44)
        # presented 0.25, resolved 0.2, raw 0.5 in the synthetic edge profiles.
        self.assertAlmostEqual(table['esf_excursion_over_resolved'], 0.05)
        self.assertAlmostEqual(table['esf_excursion_under_raw'], 0.25)
        self.assertEqual(table['escapes_3x3'], 0)
        self.assertTrue(table['flicker_gate_met'])


class MipBiasIsolationTest(unittest.TestCase):
    def test_raw_spans_use_the_stationary_and_slow_bursts_only(self):
        blur = {'runD': [
            {'status': 'evaluated', 'frames': [100, 103], 'motion': 'stationary',
             'routed_interior_px': 1000, 'raw_rise_10_90_px': [0.8, 0.9],
             'raw_mtf50_cycles_per_px': [1.0, 1.2], 'resolved_rise_10_90_px': [1.2, 1.3],
             'gradient_energy_ratio_interior': {'mean': 0.5}},
            {'status': 'evaluated', 'frames': [200, 203], 'motion': 'turning',
             'routed_interior_px': 100, 'raw_rise_10_90_px': [5.0, 6.0],
             'raw_mtf50_cycles_per_px': [0.1, 0.2], 'resolved_rise_10_90_px': [2.0, 3.0],
             'gradient_energy_ratio_interior': {'mean': 0.7}},
            {'status': 'missing_readbacks', 'frames': [300, 303]},
        ]}
        out = it13.mip_bias_isolation(blur, {'runD': report()})['runD']
        self.assertEqual(out['mip_bias_announced'], -0.5)
        self.assertTrue(out['all_routed_draws_biased'])
        self.assertEqual(out['failures'], 0)
        self.assertEqual(list(out['bursts']), ['100', '200'])
        span = out['stationary_or_slow_raw_rise_px']
        self.assertEqual((span['min'], span['max']), (0.8, 0.9))
        self.assertAlmostEqual(span['mean'], 0.85)
        self.assertEqual(out['stationary_or_slow_raw_mtf50']['max'], 1.2)


class HealthTest(unittest.TestCase):
    def test_counters_and_derived_fractions(self):
        health = {'health': {'frame_records': 10, 'taa_attempted': 9, 'taa_resolved': 9, 'taa_history': 8,
                             'apply_failures': 0, 'restore_failures': 0,
                             'state_shadow': {'rs_queries': 1000, 'rs_hits': 999},
                             'totals': {'routed': 200, 'matched': 190}, 'cut_events': [],
                             'scene_end_check': {'1': 9}},
                  'camera': {'records': 5, 'valid': 5, 'camera_cuts': [],
                             'rotation_deg': {'max': 2.0}, 'rotation_floor_deg': {'max': 1.6}},
                  'timing_regimes': {'1:frame_normal': {'windows': 7, 'fast': {'median_us': 10000.0}}}}
        out = it13.health_summary(health, report())
        self.assertTrue(out['attempted_equals_resolved'])
        self.assertAlmostEqual(out['history_match_fraction'], 0.95)
        self.assertAlmostEqual(out['rs_hit_fraction'], 0.999)
        self.assertEqual(out['camera_rotation_deg_max'], 2.0)
        self.assertEqual(out['sharpen_failed_lines'], 0)
        self.assertEqual(out['line_kinds'], 87)

    def test_displacement_is_attributed_to_the_bursts(self):
        readback = {'checks': {'displacement': {'status': 'fail', 'bound_px': 64.0,
                                                'suspicious_pixels': 30, 'suspicious_fraction': 0.01,
                                                'max_displacement_px': 200.0}},
                    'frames': [
                        {'frame': 100, 'pixels': {'suspicious_over_bound': 0,
                                                  'displacement_px': {'max': 1.0, 'p99': 0.5}}},
                        {'frame': 101, 'pixels': {'suspicious_over_bound': 0,
                                                  'displacement_px': {'max': 2.0, 'p99': 1.5}}},
                        {'frame': 200, 'pixels': {'suspicious_over_bound': 30,
                                                  'displacement_px': {'max': 200.0, 'p99': 180.0}}},
                    ]}
        bursts = [{'frames': [100, 101], 'route': {'motion': 'stationary', 'cut_median_px_peak': 0.0}},
                  {'frames': [200, 201], 'route': {'motion': 'turning', 'cut_median_px_peak': 80.0}}]
        out = it13.displacement_attribution(readback, bursts)
        self.assertEqual(out['status'], 'fail')
        self.assertEqual(out['bursts_with_suspicious_pixels'], ['200'])
        self.assertEqual(out['attributed_pixels'], 30)
        self.assertEqual(out['bursts']['100']['suspicious_pixels'], 0)
        self.assertEqual(out['bursts']['200']['frames_with_suspicious'], 1)
        self.assertEqual(out['bursts']['200']['max_displacement_px'], 200.0)

    def test_loads_and_frame_time(self):
        profile = {'source': {'name': 'session.log'},
                   'gaps': [{'gap_seconds': 8.0, 'end_frame': 30, 'end_seconds': 13.0,
                             'hooked': {'exclusive_seconds': 4.5}},
                            {'gap_seconds': 34.0, 'end_frame': 640, 'end_seconds': 54.0, 'hooked': {}}]}
        table = it13.load_table({'runD': profile})['runD']
        self.assertAlmostEqual(table['total_seconds'], 42.0)
        self.assertEqual(table['gaps'][0]['hooked_exclusive_seconds'], 4.5)
        self.assertEqual(table['log'], 'session.log')
        cost = {'comparisons': {'a:b': {'role': 'noise',
                                        'regimes': {'fast': {'primary': {'mean_ms': 10.5, 'draws_per_frame': 281.0},
                                                             'baseline': {'mean_ms': 10.4, 'draws_per_frame': 275.5},
                                                             'delta_mean_ms': 0.1, 'delta_mean_percent': 0.9}},
                                        'normalised': {'scene': {'bins': 8, 'draws_span': [84.5, 815.2],
                                                                 'delta_ms': {'median': -0.008, 'min': -3.0, 'max': 2.1},
                                                                 'origin_us_per_draw': {'median': -0.2},
                                                                 'sign_test': {'p_two_sided': 1.0}}}}}}
        frame = it13.frame_time(cost, 'a:b')
        self.assertEqual(frame['matched_bins'], 8)
        self.assertEqual(frame['sign_test_p'], 1.0)
        self.assertAlmostEqual(frame['matched_delta_ms_median'], -0.008)
        self.assertEqual(frame['matched_delta_ms_span'], [-3.0, 2.1])


class PublishedSummaryTests(unittest.TestCase):
    """The tracked iteration-13 summary must keep reproducing its own tables."""

    @classmethod
    def setUpClass(cls):
        if not SUMMARY.exists():
            raise unittest.SkipTest(f'{SUMMARY} not present')
        cls.report = json.loads(SUMMARY.read_text())

    def test_the_present_readback_matches_the_rcas_reference(self):
        a = self.report['model_agreement']
        self.assertEqual(a['status'], 'pass')
        self.assertEqual(a['frames_measured'], 16)
        self.assertEqual(a['frames_modelled_only'], 0)
        self.assertEqual(a['frames_outside_tolerance'], 0)
        self.assertLess(a['worst_excess_over_half_code'], 1e-4)
        self.assertEqual(a['gain'], 1.0)
        for burst in a['bursts']:
            self.assertEqual(burst['sources'], ['present_readback'])
            self.assertLess(burst['mean_code_error']['max'], 0.2)

    def test_no_sharpen_value_leaves_the_neighbourhood(self):
        for label, rows in self.report['sharpen_ab'].items():
            for key, row in rows.items():
                if row.get('status') != 'evaluated':
                    continue
                self.assertEqual(row['escapes_3x3_total'], 0, f'{label} {key}')
                self.assertEqual(row['strong_edge_channel_escapes'], 0, f'{label} {key}')
                self.assertGreater(row['gradient_presented_over_resolved']['min'], 1.0)

    def test_the_ab_is_monotone_in_the_sharpen_value(self):
        low = self.report['sharpen_ab']['runD_0.5']
        mid = self.report['sharpen_ab']['runD_0.75']
        high = self.report['sharpen_ab']['runD_1.0']
        self.assertEqual(set(low), set(high))
        self.assertEqual(set(mid), set(high))
        for key in high:
            for metric in ('gradient_presented_over_resolved', 'strong_edge_local_range_ratio',
                           'changed_fraction', 'mtf50_recovery_share'):
                a, b, c = (rows[key][metric]['mean'] for rows in (low, mid, high))
                self.assertLess(a, b, f'{key} {metric}')
                self.assertLess(b, c, f'{key} {metric}')
            for rows, sharpen in ((low, 0.5), (mid, 0.75), (high, 1.0)):
                self.assertEqual(rows[key]['sharpen'], sharpen)

    def test_the_same_scene_ab_compares_the_measured_image(self):
        same = self.report['same_scene']
        self.assertEqual(len(same), 4)
        for key, entry in same.items():
            self.assertEqual(entry['image_source_high'], 'present_readback')
            self.assertEqual(entry['image_source_low'], 'rcas_model')
            self.assertEqual((entry['sharpen_low'], entry['sharpen_high']), (0.5, 1.0))
            for metric in ('gradient_presented_over_resolved', 'strong_edge_local_range_ratio'):
                item = entry[metric]
                self.assertAlmostEqual(item['delta'], item['high'] - item['low'])
            energy = entry['flicker_presented_over_resolved_energy']
            self.assertGreater(energy['high'], energy['low'])
        gated = [entry for entry in same.values()
                 if entry['flicker_presented_over_resolved_energy']['gate_met']]
        self.assertEqual(len(gated), 1)

    def test_the_mip_bias_is_the_same_in_both_runs(self):
        isolation = self.report['mip_bias_isolation']
        for label, entry in isolation.items():
            self.assertEqual(entry['mip_bias_announced'], -0.5, label)
            self.assertEqual(entry['bias_values'], ['-0.5'], label)
            self.assertTrue(entry['all_routed_draws_biased'], label)
            self.assertEqual((entry['failures'], entry['biased_now_nonzero_records']), (0, 0), label)
            self.assertIsNotNone(entry['stationary_or_slow_raw_rise_px'])

    def test_health_counters_and_the_displacement_attribution(self):
        counters = self.report['health']['counters']
        self.assertTrue(counters['attempted_equals_resolved'])
        self.assertEqual((counters['apply_failures'], counters['restore_failures']), (0, 0))
        self.assertEqual(counters['sharpen_failed_lines'], 0)
        self.assertEqual(counters['taa_failed_lines'], 0)
        self.assertEqual(counters['failure_kinds'], [])
        self.assertEqual(counters['camera_cuts'], [])
        self.assertGreater(counters['history_match_fraction'], 0.99)
        displacement = self.report['health']['displacement']
        self.assertEqual(displacement['status'], 'fail')
        self.assertEqual(displacement['bursts_with_suspicious_pixels'], ['7921'])
        self.assertEqual(displacement['attributed_pixels'], displacement['suspicious_pixels'])
        turning = displacement['bursts']['7921']
        self.assertGreater(turning['cut_median_px_peak'], displacement['bound_px'])

    def test_frame_time_is_indistinguishable_from_the_previous_run(self):
        frame = self.report['health']['frame_time']
        self.assertEqual(frame['matched_bins'], 8)
        self.assertEqual(frame['sign_test_p'], 1.0)
        self.assertLess(abs(frame['matched_delta_ms_median']), 0.5)


if __name__ == '__main__':
    unittest.main()
