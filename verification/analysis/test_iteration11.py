"""Tests for tools/analysis/analyze_iteration11.py.

The synthetic logs here are the smallest thing that exercises each derivation:
a ``per_draw=0`` run whose only draw-count evidence is ``motion_output_frame``,
a ``per_draw=1`` run that measures the same quantity (so the surrogate can be
checked against the measurement it replaces), hook records of each verdict, the
gz read-ahead counters and per-window ``loading_metric`` deltas.

The last group is a regression test against the numbers published in
docs/verification/iteration-11.md, recomputed from the tracked summary JSON: if
the arithmetic drifts, the published tables stop reproducing.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))

import analyze_iteration09_cost as it09cost  # noqa: E402
import analyze_iteration11 as it11           # noqa: E402

SUMMARY = ROOT / 'verification' / 'results' / 'iteration-11.json'


def write(text):
    handle = tempfile.NamedTemporaryFile('w', suffix='.log', delete=False)
    handle.write(text)
    handle.close()
    return Path(handle.name)


def window(frame, since_us, frames, mean_us, draw_count=None):
    """One report window: a summary, its frame_normal and optionally draw_backend."""
    lines = [f'telemetry_summary device=1 frame={frame} reason=interval qpc=0 '
             f'since_start_us={since_us} interval_us=1000000.0',
             f'telemetry_metric device=1 name=frame_normal count={frames} failures=0 '
             f'total_us={mean_us * frames} min_us={mean_us} max_us={mean_us} bytes=0']
    if draw_count is not None:
        lines.append(f'telemetry_metric device=1 name=draw_backend count={draw_count} '
                     f'failures=0 total_us={draw_count * 2.0} min_us=2.0 max_us=2.0 bytes=0')
    return '\n'.join(lines) + '\n'


def frame_record(frame, draws, routed=0, **overrides):
    fields = {
        'device': 1, 'frame': frame, 'latched': 1 if routed else 0, 'filled': 1,
        'draws': draws, 'routed': routed, 'matched': routed, 'gate1': 0, 'gate2': 0,
        'gate3': 0, 'gate4': 0, 'gate5': 0, 'gate6': 0, 'apply_failures': 0,
        'restore_failures': 0, 'taa_attempted': 1 if routed else 0,
        'taa_resolved': 1 if routed else 0, 'taa_history': 1 if routed else 0,
        'taa_skip': 0, 'readbacks': 0, 'rt_mode': 'perdraw', 'timing': 'cpu_qpc',
        'set_rt': 0, 'lazy_flushes': 0, 'jitter_writes': 0,
        'gate_us': 0.0, 'route_draw_us': 0.0, 'set_rt_us': 0.0, 'lazy_flush_us': 0.0,
        'jitter_us': 0.0, 'fill_us': 60.0, 'taa_run_us': 370.0, 'taa_capture_us': 8.0,
        'taa_copy_color_us': 3.0, 'taa_copy_depth_us': 2.0, 'taa_draw_us': 330.0,
        'taa_apply_us': 15.0, 'taa_copy_back_us': 1.0, 'readback_us': 0.0,
        'rs_queries': 100, 'rs_hits': 100, 'rs_gets': 2, 'rs_resyncs': 0,
        'scene_hook': 1, 'scene_end_source': 'hook' if routed else 'none',
        'scene_end_check': 1 if routed else 0, 'hook_signals': 1,
        'hook_outside_scene': 0, 'hook_state': 0, 'draws_after_hook': 0,
    }
    fields.update(overrides)
    return 'motion_output_frame ' + ' '.join(f'{k}={v}' for k, v in fields.items()) + '\n'


HEADER_NO_STAMPS = ('telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 '
                    'anchor=proxy_initialize cpu_only=1 per_draw=0\n')
HEADER_STAMPS = ('telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 '
                 'anchor=proxy_initialize cpu_only=1\n')


class SurrogateTests(unittest.TestCase):
    def test_injects_median_draw_count_of_the_frames_a_window_covers(self):
        log = HEADER_NO_STAMPS + frame_record(30, 200, routed=150) + \
            frame_record(90, 300, routed=150) + window(100, 1e6, 80, 12000.0)
        path = write(log)
        try:
            scan = it09cost.scan(path)
            result = it11.inject_surrogate_draws(scan)
            rows = [r for r in (it09cost.window_row(w) for w in scan['windows']) if r]
        finally:
            path.unlink()
        self.assertEqual(result['status'], 'injected')
        self.assertEqual(result['windows_injected'], 1)
        self.assertEqual(rows[0]['draws_per_frame'], 250.0)  # median of 200 and 300

    def test_leaves_a_measured_window_untouched(self):
        log = HEADER_STAMPS + frame_record(30, 999, routed=10) + \
            window(100, 1e6, 80, 12000.0, draw_count=80 * 400)
        path = write(log)
        try:
            scan = it09cost.scan(path)
            result = it11.inject_surrogate_draws(scan)
            rows = [r for r in (it09cost.window_row(w) for w in scan['windows']) if r]
        finally:
            path.unlink()
        self.assertEqual(result['status'], 'not_needed')
        self.assertEqual(result['windows_measured'], 1)
        self.assertEqual(rows[0]['draws_per_frame'], 400.0)

    def test_injected_window_reports_no_per_draw_time(self):
        log = HEADER_NO_STAMPS + frame_record(30, 200, routed=150) + \
            window(100, 1e6, 80, 12000.0)
        path = write(log)
        try:
            scan = it09cost.scan(path)
            it11.inject_surrogate_draws(scan)
            rows = [r for r in (it09cost.window_row(w) for w in scan['windows']) if r]
        finally:
            path.unlink()
        self.assertEqual(rows[0]['draw_backend_us'], 0.0)
        self.assertTrue(scan['windows'][0]['metrics']['draw_backend']['surrogate'])

    def test_window_without_a_route_sample_stays_unknown(self):
        log = HEADER_NO_STAMPS + frame_record(30, 200, routed=150) + \
            window(100, 1e6, 80, 12000.0) + window(200, 2e6, 80, 12000.0)
        path = write(log)
        try:
            scan = it09cost.scan(path)
            it11.inject_surrogate_draws(scan)
            rows = [r for r in (it09cost.window_row(w) for w in scan['windows']) if r]
        finally:
            path.unlink()
        self.assertEqual(rows[0]['draws_per_frame'], 200.0)
        self.assertIsNone(rows[1]['draws_per_frame'])

    def test_no_route_records_is_reported_unavailable(self):
        path = write(HEADER_NO_STAMPS + window(100, 1e6, 80, 12000.0))
        try:
            scan = it09cost.scan(path)
            result = it11.inject_surrogate_draws(scan)
        finally:
            path.unlink()
        self.assertEqual(result['status'], 'unavailable')
        self.assertEqual(result['windows_injected'], 0)

    def test_cross_check_measures_the_surrogate_against_the_measurement(self):
        log = HEADER_STAMPS
        for index in range(3):
            frame = 100 * (index + 1)
            log += frame_record(frame - 50, 400, routed=200)
            log += window(frame, 1e6 * (index + 1), 80, 12000.0, draw_count=80 * 400)
        path = write(log)
        try:
            check = it11.surrogate_cross_check(path)
        finally:
            path.unlink()
        self.assertEqual(check['status'], 'evaluated')
        self.assertEqual(check['windows'], 3)
        self.assertEqual(check['relative_error']['max'], 0.0)
        self.assertEqual(check['within_10_percent'], 1.0)

    def test_cross_check_unavailable_without_draw_backend(self):
        log = HEADER_NO_STAMPS + frame_record(30, 200, routed=150) + \
            window(100, 1e6, 80, 12000.0)
        path = write(log)
        try:
            check = it11.surrogate_cross_check(path)
        finally:
            path.unlink()
        self.assertEqual(check['status'], 'unavailable')


class PerDrawFlagTests(unittest.TestCase):
    def test_reads_the_flag_from_telemetry_start(self):
        path = write(HEADER_NO_STAMPS)
        try:
            self.assertEqual(it11.telemetry_per_draw(path), '0')
        finally:
            path.unlink()

    def test_absent_flag_is_reported_as_absent(self):
        path = write(HEADER_STAMPS)
        try:
            self.assertEqual(it11.telemetry_per_draw(path), 'absent')
        finally:
            path.unlink()


class AvailabilityTests(unittest.TestCase):
    def test_zero_per_draw_fields_are_reported_unmeasured(self):
        log = HEADER_NO_STAMPS + frame_record(60, 300, routed=200)
        path = write(log)
        try:
            report = it11.route_cost_availability(path)
        finally:
            path.unlink()
        self.assertEqual(report['telemetry_per_draw'], '0')
        self.assertTrue(report['per_draw_fields_unmeasured'])
        self.assertIn('draw_backend', report['per_draw_metrics_absent'])
        self.assertEqual(report['per_frame_fields_us']['fill_us']['median'], 60.0)
        self.assertEqual(report['routed_frame_records'], 1)

    def test_a_stamped_run_is_not_reported_unmeasured(self):
        log = HEADER_STAMPS + frame_record(60, 300, routed=200, gate_us=2500.0) + \
            ('telemetry_metric device=1 name=draw_backend count=300 failures=0 '
             'total_us=600.0 min_us=2.0 max_us=2.0 bytes=0\n')
        path = write(log)
        try:
            report = it11.route_cost_availability(path)
        finally:
            path.unlink()
        self.assertFalse(report['per_draw_fields_unmeasured'])
        self.assertEqual(report['per_draw_metrics_present']['draw_backend'], 300)
        self.assertNotIn('draw_backend', report['per_draw_metrics_absent'])


class SceneHookTests(unittest.TestCase):
    def test_verdicts_are_named_and_resolves_attributed(self):
        log = (frame_record(60, 300, routed=200) +
               frame_record(120, 300, routed=200, scene_end_check=4,
                            scene_end_source='stretch') +
               frame_record(180, 640, routed=0, hook_outside_scene=1, hook_state=9) +
               'motion_output_scene_hook_disagreement device=1 frame=120 reason=late\n')
        path = write(log)
        try:
            hook = it11.scene_hook(path)
        finally:
            path.unlink()
        self.assertEqual(hook['records'], 3)
        self.assertEqual(hook['check_distribution'], {'Agree': 1, 'Disagree': 1, 'None': 1})
        self.assertEqual(hook['resolved_by_source'], {'hook': 1, 'stretch': 1})
        self.assertEqual(hook['resolves'], 2)
        self.assertEqual(len(hook['disagreement_records']), 1)
        self.assertEqual(hook['state_shadow_totals']['rs_resyncs'], 0)
        self.assertEqual(hook['state_shadow_hit_rate'], 1.0)

    def test_a_latched_frame_without_a_verdict_is_listed(self):
        log = frame_record(0, 0, routed=0, latched=1)
        path = write(log)
        try:
            hook = it11.scene_hook(path)
        finally:
            path.unlink()
        self.assertEqual(hook['latched_records'], 1)
        self.assertEqual(hook['latched_frames_without_verdict'], [0])

    def test_route_failures_are_summed(self):
        log = frame_record(60, 300, routed=200, apply_failures=2, restore_failures=1)
        path = write(log)
        try:
            hook = it11.scene_hook(path)
        finally:
            path.unlink()
        self.assertEqual(hook['route_failures'], {'apply_failures': 2, 'restore_failures': 1})


class EngineReadTests(unittest.TestCase):
    def test_absent_when_no_engine_line_exists(self):
        self.assertEqual(it11.engine_reads({'draw': 3})['status'], 'absent')

    def test_present_when_a_future_build_emits_one(self):
        report = it11.engine_reads({'engine_memory': 4})
        self.assertEqual(report['status'], 'present')
        self.assertEqual(report['kinds_present'], {'engine_memory': 4})


class GzBufferTests(unittest.TestCase):
    LOG = (
        'telemetry_start schema=1 qpc_frequency=10000000 qpc=0 anchor=proxy_initialize\n'
        'gz_buffer requested=1 enabled=1 capacity_kb=256 telemetry=1 imports=1 rewind=1 slots=32\n'
        'gz_buffer_file slot=0 capacity=262144 calls=9 small_calls=8 served_bytes=92 '
        'real_reads=1 real_bytes=262144 direct_reads=0 getcs=0 tells=0 seeks=0 '
        'seeks_served=0 seeks_real=0 error=0 end=0 close=0\n'
        'gz_buffer_file slot=0 capacity=262144 calls=1000 small_calls=990 served_bytes=4000 '
        'real_reads=2 real_bytes=4000 direct_reads=0 getcs=0 tells=0 seeks=0 '
        'seeks_served=0 seeks_real=0 error=0 end=0 close=0\n'
        'loading_metric op=gzread qpc=100000000 count=100 failures=0 bytes=2000 '
        'inclusive_ticks=0 exclusive_ticks=0 max_ticks=0 wrapper_tail_ticks=0 '
        'total_us=1000.0 exclusive_us=1000.0 max_us=50.0 wrapper_tail_us=0.0\n'
        'loading_metric op=gzread qpc=200000000 count=75 failures=1 bytes=2000 '
        'inclusive_ticks=0 exclusive_ticks=0 max_ticks=0 wrapper_tail_ticks=0 '
        'total_us=500.0 exclusive_us=500.0 max_us=80.0 wrapper_tail_us=0.0\n'
        'loading_metric op=inflate qpc=100000000 count=5000 failures=0 bytes=0 '
        'inclusive_ticks=0 exclusive_ticks=0 max_ticks=0 wrapper_tail_ticks=0 '
        'total_us=90000.0 exclusive_us=90000.0 max_us=100.0 wrapper_tail_us=0.0\n')

    def test_rows_are_summed_because_they_are_deltas(self):
        path = write(self.LOG)
        try:
            report = it11.gz_buffer(path)
        finally:
            path.unlink()
        self.assertEqual(report['zlib_totals']['gzread']['count'], 175)
        self.assertEqual(report['zlib_totals']['gzread']['failures'], 1)
        self.assertEqual(report['zlib_totals']['gzread']['windows'], 2)
        self.assertEqual(report['zlib_totals']['gzread']['max_us'], 80.0)

    def test_the_savegame_file_is_the_one_with_the_most_calls(self):
        path = write(self.LOG)
        try:
            report = it11.gz_buffer(path)
        finally:
            path.unlink()
        main = report['savegame_file']
        self.assertEqual(main['calls'], 1000)
        self.assertEqual(main['small_call_fraction'], 0.99)
        self.assertEqual(main['calls_per_real_read'], 500.0)
        self.assertEqual(main['mean_served_bytes_per_call'], 4.0)
        self.assertTrue(main['real_bytes_equal_served'])

    def test_calls_are_attributed_to_the_load_they_fall_in(self):
        path = write(self.LOG)
        loads = {'gaps': [{'index': 0, 'kind': 'save_load', 'seconds': 4.0,
                           'ends_since_s': 12.0}]}
        try:
            report = it11.gz_buffer(path, loads)
        finally:
            path.unlink()
        # qpc 100000000 at 1e7 ticks/s is 10 s, inside [8, 12]; qpc 200000000 is 20 s.
        entry = report['zlib_by_load']['0:save_load']
        self.assertEqual(entry['window_s'], [8.0, 12.0])
        self.assertEqual(entry['ops']['gzread']['count'], 100)
        self.assertEqual(entry['ops']['inflate']['count'], 5000)

    def test_no_gz_buffer_file_still_reports_the_zlib_totals(self):
        path = write('loading_metric op=inflate qpc=1 count=7 failures=0 bytes=0 '
                     'total_us=1.0 exclusive_us=1.0 max_us=1.0\n')
        try:
            report = it11.gz_buffer(path)
        finally:
            path.unlink()
        self.assertEqual(report['status'], 'unavailable')
        self.assertEqual(report['zlib_totals']['inflate']['count'], 7)


class LoadLabelTests(unittest.TestCase):
    REPORT = {
        'loading_gaps': [{'frame': 28, 'since_s': 13.5, 'gap_ms': 8386.0, 'frames': 24},
                         {'frame': 872, 'since_s': 60.6, 'gap_ms': 35707.0, 'frames': 1},
                         {'frame': 9439, 'since_s': 175.1, 'gap_ms': 5944.0, 'frames': 16},
                         {'frame': 10228, 'since_s': 194.4, 'gap_ms': 7947.0, 'frames': 42}],
        'phases': [{'phase': 0, 'label': 'unknown', 'frames': [2, 4]},
                   {'phase': 1, 'label': 'menu', 'frames': [104, 871]},
                   {'phase': 2, 'label': 'scene', 'frames': [948, 9423]},
                   {'phase': 3, 'label': 'scene', 'frames': [9517, 10186]},
                   {'phase': 4, 'label': 'menu', 'frames': [10324, 10324]}],
    }

    def test_each_gap_is_named_from_the_phases_around_it(self):
        loads = it11.label_loads(self.REPORT)
        self.assertEqual([g['kind'] for g in loads['gaps']],
                         ['startup_to_menu', 'save_load', 'sector_change', 'return_to_menu'])
        self.assertEqual([g['seconds'] for g in loads['gaps']],
                         [8.386, 35.707, 5.944, 7.947])
        self.assertEqual(loads['total_seconds'], 57.984)

    def test_no_gaps_is_not_an_error(self):
        loads = it11.label_loads({'loading_gaps': [], 'phases': []})
        self.assertEqual(loads['gaps'], [])
        self.assertEqual(loads['total_seconds'], 0)


class TaaHealthTests(unittest.TestCase):
    def test_history_match_is_matched_over_routed(self):
        health = {'health': {'frame_records': 180, 'taa_attempted': 162, 'taa_resolved': 162,
                             'taa_history': 162, 'taa_skip': {'0': 162, '2': 18},
                             'resolved_without_history': [], 'apply_failures': 0,
                             'restore_failures': 0, 'cut_events': [],
                             'camera_cut_events': [],
                             'totals': {'draws': 59491, 'routed': 33805, 'matched': 33747}},
                  'camera': {'records': 43, 'valid': 38, 'camera_cuts': []}}
        out = it11.taa_health(health=health)
        self.assertEqual(out['history_match']['rate'], round(33747 / 33805, 6))
        self.assertEqual(out['resolves']['resolved'], 162)
        self.assertEqual(out['camera']['valid'], 38)

    def test_readback_check_statuses_are_carried(self):
        readback = {'status': 'FAIL', 'failed_checks': ['readback_integrity'],
                    'hard_errors': [], 'captured_frames': 8, 'frames_with_readback': 7,
                    'checks': {'readback_integrity': {'status': 'fail', 'frames': 8,
                                                      'clean_frames': 7},
                               'row_consistency': {'status': 'unavailable',
                                                   'max_error_px': None}}}
        out = it11.taa_health(readback=readback)
        self.assertEqual(out['readback']['checks']['readback_integrity']['clean_frames'], 7)
        self.assertEqual(out['readback']['checks']['row_consistency']['status'], 'unavailable')

    def test_empty_inputs_produce_an_empty_section(self):
        self.assertEqual(it11.taa_health(), {})


class RecoveryTests(unittest.TestCase):
    def comparison(self, attributed, slope, bins=10):
        return {'primary': 'p', 'baseline': 'b', 'controlled': True,
                'normalised': {'scene': {'status': 'fitted', 'bins': bins,
                                         'us_per_draw': slope,
                                         'origin_us_per_draw': {'median': slope},
                                         'sign_test': {'p_two_sided': 0.002}}},
                'attribution': {'route_attributed_us_per_draw': attributed},
                'route_cost_per_regime': {'fast': {'route_attributed_ms': 1.0,
                                                   'route_attributed_percent': 10.0}}}

    def report(self, draws, profiler_ms, stamp_ms):
        return {'scene': {'fast': {'windows': 10, 'draws_per_frame': {'median': draws},
                                   'mean_ms': {'median': 12.0},
                                   'route_exclusive_us_per_frame': {'median': 64.0}}},
                'profiler_share': {'scene': {'ms_per_frame_at_median': profiler_ms}},
                'telemetry_stamps': {'ms_per_frame': stamp_ms}}

    def test_recovery_subtracts_only_the_per_draw_stamp_cost(self):
        out = it11.route_recovery(self.comparison(11.887, 14.554),
                                  self.comparison(34.44, 37.771),
                                  self.report(275.0, 0.0, 0.002),
                                  self.report(358.4, 0.546, 0.343))
        self.assertEqual(out['diagnostics']['old']['stamp_us_per_draw'],
                         round(0.343 * 1000.0 / 358.4, 3))
        self.assertEqual(out['net_us_per_draw']['old'],
                         round(34.44 - 0.343 * 1000.0 / 358.4, 3))
        self.assertGreater(out['recovered_fraction'], 0.6)
        self.assertLess(out['recovered_fraction'], 0.7)

    def test_an_uncontrolled_pair_yields_no_recovery_number(self):
        out = it11.route_recovery({}, {}, self.report(275.0, 0.0, 0.0),
                                  self.report(358.4, 0.5, 0.3))
        self.assertNotIn('recovered_us_per_draw', out)


class PublishedSummaryTests(unittest.TestCase):
    """The tracked iteration-11 summary must keep reproducing its own tables."""

    @classmethod
    def setUpClass(cls):
        if not SUMMARY.exists():
            raise unittest.SkipTest(f'{SUMMARY} not present')
        cls.report = json.loads(SUMMARY.read_text())

    def test_the_route_on_run_had_no_per_draw_stamps(self):
        availability = self.report['route_cost_availability']
        self.assertEqual(availability['telemetry_per_draw'], '0')
        self.assertTrue(availability['per_draw_fields_unmeasured'])
        self.assertIn('route_gate', availability['per_draw_metrics_absent'])

    def test_the_attributed_route_cost_fell_against_run_6(self):
        recovery = self.report['route_recovery']
        new = recovery['attributed_us_per_draw']['new']
        old = recovery['attributed_us_per_draw']['old']
        self.assertLess(new, old)
        self.assertAlmostEqual(recovery['recovered_us_per_draw'],
                              round(recovery['net_us_per_draw']['old'] -
                                    recovery['net_us_per_draw']['new'], 3), places=3)
        self.assertAlmostEqual(recovery['recovered_fraction'],
                              round((recovery['net_us_per_draw']['old'] -
                                     recovery['net_us_per_draw']['new']) /
                                    recovery['net_us_per_draw']['old'], 4), places=4)

    def test_the_hook_carried_every_resolve_with_no_disagreement(self):
        hook = self.report['scene_hook']
        self.assertEqual(hook['check_distribution'].get('Disagree', 0), 0)
        self.assertEqual(hook['resolved_by_source'], {'hook': hook['resolves']})
        self.assertEqual(hook['draws_after_hook_distribution'], {'0': hook['records']})
        self.assertEqual(hook['rs_resyncs_distribution'], {'0': hook['records']})
        self.assertEqual(hook['route_failures'], {'apply_failures': 0, 'restore_failures': 0})

    def test_the_history_match_rate_is_matched_over_routed(self):
        match = self.report['taa_health']['history_match']
        self.assertEqual(match['rate'], round(match['matched'] / match['routed'], 6))
        self.assertGreater(match['rate'], 0.99)

    def test_the_gz_buffer_served_every_call_from_its_chunks(self):
        main = self.report['gz_buffer']['savegame_file']
        self.assertTrue(main['real_bytes_equal_served'])
        self.assertEqual(main['errors'], 0)
        self.assertGreater(main['calls_per_real_read'], 1000.0)
        self.assertAlmostEqual(main['small_call_fraction'],
                               round(main['small_calls'] / main['calls'], 6), places=6)

    def test_the_three_loads_are_labelled(self):
        kinds = [gap['kind'] for gap in self.report['loads']['gaps']]
        self.assertIn('save_load', kinds)
        self.assertIn('sector_change', kinds)
        self.assertEqual(kinds[0], 'startup_to_menu')

    def test_the_surrogate_was_validated_on_the_stamped_run(self):
        check = self.report['draws_surrogate_cross_check']['run6']
        self.assertEqual(check['status'], 'evaluated')
        self.assertLess(check['relative_error']['median'], 0.05)


if __name__ == '__main__':
    unittest.main()
