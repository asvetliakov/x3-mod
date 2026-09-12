"""Tests for tools/analysis/analyze_iteration10.py.

The blur-floor cases are regression tests against the numbers published in
docs/verification/iteration-09-run2.md section 4, recomputed from the tracked
run-2 summary JSON: if the arithmetic in analyze_iteration10.blur_floor ever
drifts, the published table stops reproducing.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))

import analyze_iteration10 as it10  # noqa: E402

RUN2_SUMMARY = ROOT / 'verification' / 'results' / 'iteration-09-run2-summary.json'


def write(text):
    handle = tempfile.NamedTemporaryFile('w', suffix='.log', delete=False)
    handle.write(text)
    handle.close()
    return Path(handle.name)


class LineKindTests(unittest.TestCase):
    def test_counts_first_token_only(self):
        path = write('draw device=1 frame=2\ndraw device=1 frame=3\nframe_end device=1\n\n')
        try:
            counts = it10.line_kinds(path)
        finally:
            path.unlink()
        self.assertEqual(counts['draw'], 2)
        self.assertEqual(counts['frame_end'], 1)
        self.assertNotIn('', counts)

    def test_diff_reports_added_and_dropped(self):
        new = it10.line_kinds(write('a x=1\nb x=1\nmotion_output_scene_hook_disagreement x=1\n'))
        old = it10.line_kinds(write('a x=1\nb x=1\nmesh_cache_metric x=1\n'))
        diff = it10.kind_diff(new, [old])
        self.assertEqual([item['kind'] for item in diff['new_kinds']],
                         ['motion_output_scene_hook_disagreement'])
        self.assertEqual([item['kind'] for item in diff['missing_kinds']],
                         ['mesh_cache_metric'])
        self.assertTrue(diff['missing_kinds'][0]['configuration'])
        self.assertEqual(diff['kinds_new_run'], 3)
        self.assertEqual(diff['kinds_baseline_union'], 3)

    def test_diff_flags_a_non_configuration_absence(self):
        new = it10.line_kinds(write('a x=1\n'))
        old = it10.line_kinds(write('a x=1\ncamera_state x=1\n'))
        diff = it10.kind_diff(new, [old])
        self.assertEqual(diff['missing_kinds'], [{'kind': 'camera_state',
                                                  'configuration': False}])


class MetricTableTests(unittest.TestCase):
    LOG = ('telemetry_metric device=1 name=route_gate count=10 failures=0 total_us=100.0\n'
           'telemetry_metric device=1 name=route_gate count=10 failures=1 total_us=300.0\n'
           'telemetry_metric device=1 name=route_gate count=0 failures=0 total_us=0.0\n'
           'telemetry_metric device=1 name=taa_run count=2 failures=0 total_us=600.0\n'
           'telemetry_metric device=1 name=ignored count=5 failures=0 total_us=5.0\n')

    def test_median_and_mean_per_call(self):
        path = write(self.LOG)
        try:
            table = it10.metric_table(path)
        finally:
            path.unlink()
        gate = table['route_gate']
        self.assertEqual(gate['windows'], 2)          # the zero-count window is skipped
        self.assertEqual(gate['calls'], 20)
        self.assertAlmostEqual(gate['median_us_per_call'], 20.0)   # median of 10 and 30
        self.assertAlmostEqual(gate['mean_us_per_call'], 20.0)     # 400 us over 20 calls
        self.assertEqual(gate['failures'], 1)
        self.assertAlmostEqual(table['taa_run']['median_us_per_call'], 300.0)
        self.assertNotIn('ignored', table)

    def test_malformed_metric_line_raises(self):
        path = write('telemetry_metric device=1 name=route_gate count=x total_us=1.0\n')
        try:
            with self.assertRaises(it10.Malformed):
                it10.metric_table(path)
        finally:
            path.unlink()

    def test_comparison_ratio_direction(self):
        tables = {'new': {'route_draw': {'median_us_per_call': 5.0, 'mean_us_per_call': 5.0,
                                         'calls': 10, 'windows': 1, 'failures': 0}},
                  'old': {'route_draw': {'median_us_per_call': 10.0, 'mean_us_per_call': 10.0,
                                         'calls': 10, 'windows': 1, 'failures': 0}}}
        comparison = it10.metric_comparison(tables, 'new', 'old')
        self.assertEqual(len(comparison['rows']), 1)
        self.assertAlmostEqual(comparison['rows'][0]['ratio'], 0.5)

    def test_comparison_skips_metrics_absent_from_one_run(self):
        tables = {'new': {'route_draw': {'median_us_per_call': 5.0, 'mean_us_per_call': 5.0,
                                         'calls': 1, 'windows': 1, 'failures': 0}},
                  'old': {}}
        self.assertEqual(it10.metric_comparison(tables, 'new', 'old')['rows'], [])


class SpeedupTests(unittest.TestCase):
    REPORT = {'comparisons': [{
        'primary': 'run5', 'baseline': 'run3', 'role': 'x',
        'attribution': {'route_attributed_us_per_draw': 0.0},
        'normalised': {'scene': {
            'origin_us_per_draw': {'median': -56.8},
            'sign_test': {'positive': 0, 'negative': 2, 'p_two_sided': 0.5},
            'pairs': [
                {'draws': 100.0, 'primary_ms': 8.0, 'baseline_ms': 16.0,
                 'primary_windows': 4, 'baseline_windows': 4},
                {'draws': 300.0, 'primary_ms': 8.0, 'baseline_ms': 24.0,
                 'primary_windows': 5, 'baseline_windows': 6},
            ]}}}]}

    def test_factor_per_bin(self):
        table = it10.speedup_table(self.REPORT, 'run5', 'run3')
        self.assertEqual([row['factor'] for row in table['rows']], [2.0, 3.0])
        self.assertEqual(table['factor']['min'], 2.0)
        self.assertEqual(table['factor']['max'], 3.0)
        self.assertEqual(table['factor']['median'], 2.5)
        self.assertEqual(table['bins'], 2)

    def test_missing_comparison_raises(self):
        with self.assertRaises(it10.Malformed):
            it10.speedup_table(self.REPORT, 'run6', 'run3')


class BlurFloorTests(unittest.TestCase):
    """docs/verification/iteration-09-run2.md section 4, recomputed."""

    @classmethod
    def setUpClass(cls):
        if not RUN2_SUMMARY.exists():
            raise unittest.SkipTest(f'{RUN2_SUMMARY} is not present')
        cls.report = json.loads(RUN2_SUMMARY.read_text())
        cls.table = it10.blur_floor_table(cls.report)

    def test_only_stationary_bursts_are_selected(self):
        self.assertEqual([burst['frames'][0] for burst in self.table['bursts']], [629, 1427])
        for burst in self.table['bursts']:
            self.assertEqual(burst['motion'], 'stationary')

    def test_burst_629_reproduces_the_published_row(self):
        burst = self.table['bursts'][0]
        self.assertAlmostEqual(burst['measured'], 0.5050, places=4)
        self.assertAlmostEqual(burst['four_phase_average'], 0.6497, places=4)
        self.assertAlmostEqual(burst['analytic_four_phase'], 0.7615, places=3)
        self.assertAlmostEqual(burst['analytic_resolve_kernel'], 0.6651, places=4)
        self.assertAlmostEqual(burst['scale_to_resolve_kernel'], 0.8735, places=4)
        self.assertAlmostEqual(burst['ideal_floor'], 0.5675, places=4)
        self.assertAlmostEqual(burst['measured_over_ideal'], 0.890, places=3)
        self.assertAlmostEqual(burst['share_of_loss_that_is_supersampling'], 0.874, places=3)

    def test_burst_1427_reproduces_the_published_row(self):
        burst = self.table['bursts'][1]
        self.assertAlmostEqual(burst['measured'], 0.6277, places=4)
        self.assertAlmostEqual(burst['ideal_floor'], 0.6556, places=4)
        self.assertAlmostEqual(burst['measured_over_ideal'], 0.958, places=3)
        self.assertAlmostEqual(burst['share_of_loss_that_is_supersampling'], 0.925, places=3)

    def test_iteration8_comparable_ratios_are_the_published_four(self):
        self.assertEqual(self.table['bursts'][0]['iteration8_comparable_ratio'],
                         [0.6994, 0.725, 0.7097, 0.6997])
        self.assertAlmostEqual(self.table['bursts'][0]['iteration8_comparable_mean'],
                               0.7085, places=3)

    def test_share_of_loss_summary_brackets_the_published_range(self):
        self.assertAlmostEqual(self.table['share_of_loss']['min'], 0.874, places=3)
        self.assertAlmostEqual(self.table['share_of_loss']['max'], 0.925, places=3)

    def test_box_filter_reference_is_carried(self):
        self.assertAlmostEqual(self.table['box_filter_reference'], 0.6158, places=4)

    def test_unevaluated_burst_is_reported_not_dropped(self):
        entry = {'status': 'evaluated', 'route': {'motion': 'stationary'},
                 'per_frame': [], 'captured_phase_average': {}}
        self.assertEqual(it10.blur_floor(entry)['status'], 'unavailable')


class HealthTableTests(unittest.TestCase):
    def report(self, label='runX', records=10, routed=100, matched=99):
        return {
            'source': {'log': f'/tmp/{label}.log'},
            'witnesses': {},
            'health': {'frame_records': records,
                       'totals': {'draws': 200, 'routed': routed, 'matched': matched,
                                  'gate1': 0, 'gate2': 1, 'gate3': 2, 'gate4': 3,
                                  'gate5': 0, 'gate6': 4},
                       'taa_attempted': 8, 'taa_resolved': 8, 'taa_history': 7,
                       'taa_skip': {'0': 8, '2': 2}, 'taa_result': {'00000000': 8},
                       'resolved_without_history': [1], 'apply_failures': 0,
                       'restore_failures': 0,
                       'state_shadow': {'rs_resyncs': 24},
                       'scene_end_source': {'hook': 8, 'none': 2},
                       'scene_end_check': {'1': 8, '4': 2},
                       'camera_policy': {'2': 8}, 'camera_valid': {'1': 8},
                       'cut_events': [1, 2], 'camera_cut_events': [],
                       'routed_frames_without_camera': []},
            'camera': {'records': 4, 'valid': 4, 'invalid_frames': [],
                       'projections': [{'p00': 0.8, 'p11': 1.333333, 'records': 4}],
                       'row_norm_deviation_max': 1.8e-4,
                       'rotation_deg': {'max': 1.0, 'median': 0.5}},
        }

    def test_match_fraction_and_ordering(self):
        rows = it10.health_table({'runB': self.report('runB'), 'runA': self.report('runA')})
        self.assertEqual([row['label'] for row in rows], ['runA', 'runB'])
        self.assertAlmostEqual(rows[0]['match_fraction'], 0.99)
        self.assertEqual(rows[0]['state_shadow']['rs_resyncs'], 24)
        self.assertEqual(rows[0]['cut_events'], 2)

    def test_route_off_run_is_skipped(self):
        off = self.report('run5', records=0)
        rows = it10.health_table({'run5': off, 'run6': self.report('run6')})
        self.assertEqual([row['label'] for row in rows], ['run6'])


class PrecisionTableTests(unittest.TestCase):
    def it09(self, residual, projections):
        return {'source': {'log': '/tmp/x.log'}, 'witnesses': {}, 'health': {},
                'camera': {'records': 5, 'valid': 5,
                           'row_norm_deviation_max': residual,
                           'projections': projections}}

    def test_residual_and_projections_are_carried(self):
        table = it10.precision_table({
            'old': self.it09(2.254e-4, [{'p00': 0.8, 'p11': 1.333333, 'records': 38}]),
            'new': self.it09(1.810e-4, [{'p00': 0.8, 'p11': 1.333333, 'records': 72},
                                        {'p00': 0.8000767, 'p11': 1.333461, 'records': 5}]),
        })
        rows = {row['label']: row for row in table['rows']}
        self.assertLess(rows['new']['view_row_norm_deviation_max'],
                        rows['old']['view_row_norm_deviation_max'])
        self.assertEqual(len(rows['new']['projections']), 2)

    def test_route_off_run_has_no_row(self):
        report = self.it09(None, [])
        report['camera']['records'] = 0
        self.assertEqual(it10.precision_table({'run5': report})['rows'], [])

    def test_cross_check_and_readback_are_folded_in(self):
        camera = {'checks': [{'name': 'draw_constants_agree', 'status': 'fail',
                              'draws': 100, 'agreeing': 99,
                              'max_rotation_deviation': 1.98,
                              'max_projection_deviation': 1.1e-4}],
                  'capture_frames': [{'valid': True, 'max_rotation_deviation': 1e-7,
                                      'max_translation_deviation': 2e-7,
                                      'max_projection_deviation': 3e-7},
                                     {'valid': False, 'max_rotation_deviation': 9.0,
                                      'max_translation_deviation': 9.0,
                                      'max_projection_deviation': 9.0}]}
        readback = {'checks': {'row_consistency': {'status': 'pass', 'max_error_px': 0.059,
                                                   'unexplained_fraction': 0.0,
                                                   'sampled_pixels': 10},
                               'depth': {'status': 'fail', 'max_error': 0.15,
                                         'worst_within_fraction': 0.33,
                                         'compared_pixels': 10},
                               'readback_integrity': {'status': 'pass'}}}
        table = it10.precision_table({'new': self.it09(1e-4, [])},
                                     {'new': camera}, {'new': readback})
        row = table['rows'][0]
        self.assertAlmostEqual(row['draw_constants']['fraction'], 0.99)
        self.assertEqual(row['capture_frame_deviations']['frames'], 1)
        self.assertAlmostEqual(row['capture_frame_deviations']['rotation_max'], 1e-7)
        self.assertEqual(row['motion_row_consistency']['status'], 'pass')
        self.assertEqual(row['depth_cross_check']['status'], 'fail')
        self.assertEqual(row['readback_integrity'], 'pass')


class SplitTests(unittest.TestCase):
    def test_label_value(self):
        self.assertEqual(it10._split('run6=/tmp/a.log'), ('run6', '/tmp/a.log'))
        self.assertEqual(it10._split('run6:run5', separator=':'), ('run6', 'run5'))

    def test_missing_separator_raises(self):
        with self.assertRaises(it10.Malformed):
            it10._split('run6')


class BuildTests(unittest.TestCase):
    class Args:
        def __init__(self, **kwargs):
            self.run = []
            self.new_run = None
            self.new_run_peer = []
            self.it09 = []
            self.analyze = []
            self.camera = []
            self.readback = []
            self.cost = None
            self.speedup = []
            self.metric_pair = []
            self.taa = None
            self.taa_baseline = None
            self.kind_counts = False
            self.device = '1'
            self.output = None
            self.text = None
            for key, value in kwargs.items():
                setattr(self, key, value)

    def test_build_from_logs_only(self):
        new = write('a x=1\ntelemetry_metric device=1 name=route_gate count=2 '
                    'failures=0 total_us=20.0\nnew_kind x=1\n')
        old = write('a x=1\ntelemetry_metric device=1 name=route_gate count=2 '
                    'failures=0 total_us=40.0\n')
        try:
            args = self.Args(run=[f'run6={new}', f'run2={old}'], new_run='run6',
                             metric_pair=['run6:run2'])
            report = it10.build(args)
        finally:
            new.unlink()
            old.unlink()
        self.assertEqual([item['kind'] for item in report['line_kinds']['diff']['new_kinds']],
                         ['new_kind'])
        self.assertIsNone(report['line_kinds']['counts'])
        self.assertAlmostEqual(report['metrics']['comparisons'][0]['rows'][0]['ratio'], 0.5)
        self.assertIn('limits', report)
        text = it10.render_text(report)
        self.assertIn('line kinds', text)
        self.assertIn('route_gate', text)

    def test_speedup_without_cost_raises(self):
        args = self.Args(speedup=['run6:run5'])
        with self.assertRaises(it10.Malformed):
            it10.build(args)

    def test_peer_run_is_not_a_kind_baseline(self):
        new = write('a x=1\nonly_new x=1\n')
        peer = write('a x=1\nonly_new x=1\n')
        old = write('a x=1\n')
        try:
            args = self.Args(run=[f'run6={new}', f'run5={peer}', f'run2={old}'],
                             new_run='run6', new_run_peer=['run5'])
            report = it10.build(args)
        finally:
            for path in (new, peer, old):
                path.unlink()
        self.assertEqual(report['line_kinds']['baselines'], ['run2'])
        self.assertEqual([item['kind'] for item in report['line_kinds']['diff']['new_kinds']],
                         ['only_new'])


if __name__ == '__main__':
    unittest.main()
