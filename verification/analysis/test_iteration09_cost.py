"""Synthetic fixtures for tools/analysis/analyze_iteration09_cost.py.

No game data.  Two small logs are written with the exact record formats the
proxy emits, chosen so every quantity the route-cost analyzer reports has a
closed form:

* both logs segment into the same five phases - a loading window, a **menu**
  phase of two windows at 640 and 650 draws/frame (narrow, flat: the main
  menu's signature), a second loading window, and a **scene** phase of three
  windows at 100, 400 and 700 draws/frame (wide: a sector's signature) plus one
  capture window that must be excluded from every median;
* the route-on log carries ``route_*``/``taa_*`` metrics whose totals are exact
  multiples of their counts, so every per-call and per-frame figure is a
  terminating decimal, and a ``route_gate`` sample of exactly 0.25 us per call
  in the menu, which is the telemetry-stamp cost the tool derives;
* three ordinary ``motion_output_frame`` records scale as k = 1, 2, 3 so the
  median of each derived ratio is the k = 2 record, and one capture record with
  two readbacks whose per-readback cost is exact;
* one ``profile_report`` delta of 1,000 ticks over 5 s gives 200 ticks/s, so the
  suspension share is exactly 200 x 123 us = 2.46% of wall;
* the paired draw bins of the two logs give slopes and intercepts that are
  computed here from the bin values themselves, independently of the tool.

The estimators (Theil-Sen, the origin-constrained per-draw estimate, the exact
binomial sign test, the loading segmentation and the menu/scene labelling) are
also checked on hand-built inputs where the answer is known analytically.
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
import analyze_iteration09_cost as tool  # noqa: E402

HEADER_ON = [
    'motion_output_mode requested=1 scope=live_same_draw_diagnostic'
    ' history_requires=object_trace,object_lifetime temporal_consumer=1 taa=1 taa_debug=0'
    ' jitter=1 jitter_samples=8 cut_median_px=48.000 cut_missing=0.250 rt_mode=perdraw'
    ' frame_log=60 sentinel=auto camera_cut_deg=20.00 camera_log=300 state_shadow=1'
    ' scene_hook=0 hdr=0',
    'mesh_cache requested=0 enabled=0 activation=await_public_mesh_and_buffer_contract'
    ' adjacency_metric_scope=hook_service restart_on_cleanup_failure=1',
    'mesh_cache ready=1 buffer_contract_required=1 identity=process_local generation=1',
    'telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 anchor=proxy_initialize cpu_only=1',
    'profile_start schema=1 enabled=1 qpc=1000 frequency=10000000 interval_us=2000 report_s=5'
    ' sampler_tid=1 init_tid=2 main_base=0x00400000 proxy_base=0x78ce0000 query_thread=1',
    'object_trace active=1 status=active recovery_required=0',
    'scene_hook active=0 status=disabled',
]
HEADER_OFF = [
    'motion_output_mode requested=0 scope=live_same_draw_diagnostic'
    ' history_requires=object_trace,object_lifetime temporal_consumer=0 taa=0 taa_debug=0'
    ' jitter=0 jitter_samples=8 cut_median_px=48.000 cut_missing=0.250 rt_mode=perdraw'
    ' frame_log=60 sentinel=auto camera_cut_deg=20.00 camera_log=300 state_shadow=1'
    ' scene_hook=0 hdr=0',
    'mesh_cache requested=0 enabled=0 activation=await_public_mesh_and_buffer_contract'
    ' adjacency_metric_scope=hook_service restart_on_cleanup_failure=1',
    'telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 anchor=proxy_initialize cpu_only=1',
    'object_trace active=0 status=disabled recovery_required=0',
]

FRAME_RECORD = (
    'motion_output_frame device=1 frame={frame} latched=0 filled=1 fill_result=00000000'
    ' fill_restore=00000000 draws={draws} routed={routed} matched={routed} gate1=0 gate2=0'
    ' gate3=0 gate4=0 gate5=0 gate6=0 apply_failures=0 restore_failures=0 history_previous=0'
    ' history_current=0 committed=1 selector_state=9 present=00000000 depth=1'
    ' depth_routed={routed} jitter=1 jitter_index=0 jitter_x=0.125000 jitter_y=0.277778'
    ' jitter_previous_x=0.000000 jitter_previous_y=0.000000 jittered={routed} cut=0'
    ' cut_median_px=0.0000 cut_missing=0.0000 cut_samples=0 taa=1 taa_attempted=1'
    ' taa_resolved=1 taa_history=1 taa_skip=0 taa_result=00000000 taa_restore=00000000'
    ' taa_copy=00000000 scene_open=0 active_queries=0 taa_references=12 camera_valid=1'
    ' camera_background_valid=1 camera_reads=1 camera_policy=0 camera_reason=0 camera_cut=0'
    ' camera_rotation_deg=0.5000 rt_mode=perdraw timing=cpu_qpc set_rt={set_rt}'
    ' lazy_flushes=0 jitter_writes={jitter_writes} readbacks={readbacks} gate_us={gate_us}'
    ' route_draw_us={route_draw_us} set_rt_us={set_rt_us} lazy_flush_us=0.0'
    ' jitter_us={jitter_us} fill_us={fill_us} taa_run_us=300.0 taa_capture_us=10.0'
    ' taa_copy_color_us=5.0 taa_copy_depth_us=5.0 taa_draw_us=260.0 taa_apply_us=15.0'
    ' taa_copy_back_us=5.0 readback_us={readback_us} state_shadow=1 rs_queries=0 rs_hits=0'
    ' rs_gets=0 rs_resyncs=0 scene_hook=0 scene_end_source=stretchrect scene_end_check=3'
    ' hook_signals=0 hook_outside_scene=0 hook_state=0 draws_after_hook=0 bloom_copy_seen=1')


def metric(name, count, total, low=None, high=None, device='1'):
    low = total / count if low is None else low
    high = total / count if high is None else high
    return (f'telemetry_metric device={device} name={name} count={count} failures=0'
            f' total_us={total:.3f} min_us={low:.3f} max_us={high:.3f} bytes=0'
            ' buckets=1,0,0,0,0,0')


def window(frame, since_us, frames, total_us, low_us, high_us, draws_per_frame,
           extra=()):
    """One one-second summary window with its metrics, in emission order."""
    lines = [f'telemetry_summary device=1 frame={frame} reason=interval qpc={since_us}'
             f' since_start_us={since_us:.3f} interval_us=1000000.000'
             ' position_suppressed=0 cursor_changes_suppressed=0',
             metric('frame_normal', frames, total_us, low_us, high_us),
             metric('present_normal', frames, 30.0 * frames),
             metric('log_flush', frames, 0.8 * frames),
             metric('stretch_backend', frames, 1.0 * frames)]
    draws = draws_per_frame * frames
    if draws:
        lines.append(metric('draw_backend', draws, 3.0 * draws))
    lines.extend(extra)
    return lines


def route_metrics(frames, draws_per_frame):
    """Per-draw route metrics with exact per-call costs.

    gate 2 us/draw, route_draw 2 us/routed draw (70% of draws routed), four
    SetRenderTarget per routed draw at 0.25 us, two jitter writes per routed
    draw at 0.25 us, one fill of 50 us and one resolve of 300 us per frame.
    """
    draws = draws_per_frame * frames
    routed = int(draws * 0.7)
    return [metric('route_gate', draws, 2.0 * draws),
            metric('route_draw', routed, 2.0 * routed),
            metric('route_set_rt', 4 * routed, 0.25 * 4 * routed),
            metric('route_jitter', 2 * routed, 0.25 * 2 * routed),
            metric('route_fill', frames, 50.0 * frames),
            metric('taa_run', frames, 300.0 * frames),
            metric('taa_resolve_draw', frames, 260.0 * frames)]


def write_on(path):
    lines = list(HEADER_ON)
    # Loading window: a 3 s frame interval closes the phase and is dropped.
    lines += window(1, 1_000_000, 1, 3_000_000.0, 3_000_000.0, 3_000_000.0, 0)
    # Menu: two windows, 640 and 650 draws/frame, gate at exactly 0.25 us/call.
    for frame, since, draws, mean in ((10, 5_000_000, 640, 40_000.0),
                                      (20, 6_000_000, 650, 41_000.0)):
        calls = draws * 10
        lines += window(frame, since, 10, mean * 10, mean - 2000.0, mean + 2000.0, draws,
                        extra=[metric('route_gate', calls, 0.25 * calls)])
    lines.append(FRAME_RECORD.format(frame=10, draws=640, routed=0, set_rt=0,
                                     jitter_writes=0, readbacks=0, gate_us=160.0,
                                     route_draw_us=0.0, set_rt_us=0.0, jitter_us=0.0,
                                     fill_us=0.0, readback_us=0.0))
    # Second loading window: the save load.
    lines += window(30, 7_000_000, 1, 50_000_000.0, 50_000_000.0, 50_000_000.0, 0)
    # Scene: 100, 400 and 700 draws/frame at 20, 30 and 60 ms.
    for frame, since, draws, mean in ((40, 60_000_000, 100, 20_000.0),
                                      (50, 61_000_000, 400, 30_000.0),
                                      (60, 62_000_000, 700, 60_000.0)):
        lines += window(frame, since, 10, mean * 10, mean - 1000.0, mean + 1000.0, draws,
                        extra=route_metrics(10, draws))
    # One capture window: excluded from every frame-time median.
    lines += window(70, 63_000_000, 10, 250_000.0, 24_000.0, 26_000.0, 250,
                    extra=route_metrics(10, 250) + [
                        metric('route_readback', 2, 40_000.0),
                        metric('frame_capture', 4, 2_000_000.0),
                        metric('present_capture', 4, 400.0),
                        metric('capture_cpu', 4, 1000.0)])
    # Ordinary routed frames scaling as k = 1, 2, 3; the medians are the k = 2 row.
    for k in (1, 2, 3):
        lines.append(FRAME_RECORD.format(
            frame=40 + k, draws=200 * k, routed=140 * k, set_rt=560 * k,
            jitter_writes=280 * k, readbacks=0, gate_us=400.0 * k,
            route_draw_us=280.0 * k, set_rt_us=70.0 * k, jitter_us=20.0 * k,
            fill_us=60.0, readback_us=0.0))
    # One capture frame: two readbacks of 20,000 us each.
    lines.append(FRAME_RECORD.format(frame=70, draws=400, routed=280, set_rt=1120,
                                     jitter_writes=560, readbacks=2, gate_us=800.0,
                                     route_draw_us=560.0, set_rt_us=140.0, jitter_us=40.0,
                                     fill_us=60.0, readback_us=40_000.0))
    # 1,000 ticks over 5 s inside the scene phase: 200 ticks/s.
    lines.append('profile_report scope=delta qpc=1 frequency=10000000'
                 ' since_start_us=61000000.000 elapsed_us=5000000.000 interval_us=2000'
                 ' samples=16000 threads=20 threads_unsampled=0 modules=66 ticks=1000'
                 ' tick_us_mean=1000.000 tick_us_max=2000.000 refresh_us=0.0 dropped=0')
    # Another device's summary and metrics must not enter this device's windows.
    lines.append('telemetry_summary device=2 frame=99 reason=interval qpc=1'
                 ' since_start_us=64000000.000 interval_us=1000000.000'
                 ' position_suppressed=0 cursor_changes_suppressed=0')
    lines.append(metric('frame_normal', 5, 5000.0, device='2'))
    lines.append(metric('route_gate', 5, 5000.0, device='2'))
    path.write_text('\n'.join(lines) + '\n')


def write_off(path):
    lines = list(HEADER_OFF)
    lines += window(1, 1_000_000, 1, 3_000_000.0, 3_000_000.0, 3_000_000.0, 0)
    for frame, since, draws, mean in ((10, 5_000_000, 640, 32_000.0),
                                      (20, 6_000_000, 650, 32_500.0)):
        lines += window(frame, since, 10, mean * 10, mean - 2000.0, mean + 2000.0, draws)
    lines += window(30, 7_000_000, 1, 50_000_000.0, 50_000_000.0, 50_000_000.0, 0)
    for frame, since, draws, mean in ((40, 60_000_000, 100, 15_000.0),
                                      (50, 61_000_000, 400, 22_000.0),
                                      (60, 62_000_000, 700, 45_000.0)):
        lines += window(frame, since, 10, mean * 10, mean - 1000.0, mean + 1000.0, draws)
    path.write_text('\n'.join(lines) + '\n')


class Estimators(unittest.TestCase):
    def test_theil_sen_recovers_an_exact_line(self):
        points = [(0.0, 1.0), (10.0, 3.0), (20.0, 5.0), (30.0, 7.0)]
        fit = tool.theil_sen(points)
        self.assertAlmostEqual(fit['slope'], 0.2)
        self.assertAlmostEqual(fit['intercept'], 1.0)
        self.assertAlmostEqual(fit['slope_p25'], 0.2)
        self.assertAlmostEqual(fit['slope_p75'], 0.2)
        self.assertEqual(fit['n_slopes'], 6)

    def test_theil_sen_ignores_a_minority_outlier(self):
        points = [(0.0, 0.0), (10.0, 1.0), (20.0, 2.0), (30.0, 3.0), (40.0, 400.0)]
        self.assertAlmostEqual(tool.theil_sen(points)['slope'], 0.1)

    def test_theil_sen_needs_two_distinct_abscissae(self):
        self.assertIsNone(tool.theil_sen([(5.0, 1.0), (5.0, 9.0)]))

    def test_sign_test_is_the_exact_binomial_tail(self):
        self.assertEqual(tool.sign_test([1.0, 2.0, 3.0])['p_two_sided'],
                         round(2 * (1 / 8), 5))
        self.assertEqual(tool.sign_test([1.0, -2.0])['p_two_sided'], 1.0)
        self.assertEqual(tool.sign_test([0.0, 0.0])['status'], 'unavailable')
        thirteen = tool.sign_test([1.0] * 13)
        self.assertEqual(thirteen['positive'], 13)
        self.assertAlmostEqual(thirteen['p_two_sided'], round(2 / 2 ** 13, 5))

    def test_quantile_interpolates(self):
        self.assertAlmostEqual(tool.quantile([0.0, 10.0], 0.5), 5.0)
        self.assertAlmostEqual(tool.quantile([0.0, 1.0, 2.0, 3.0], 0.1), 0.3)

    def test_segment_splits_at_a_loading_window(self):
        rows = [{'frame': 1, 'since_s': 0.0, 'max_ms': 10.0, 'frames': 10},
                {'frame': 2, 'since_s': 1.0, 'max_ms': 3000.0, 'frames': 1},
                {'frame': 3, 'since_s': 2.0, 'max_ms': 12.0, 'frames': 10}]
        phases, loading = tool.segment(rows, gap_us=2e6)
        self.assertEqual([[r['frame'] for r in p] for p in phases], [[1], [3]])
        self.assertEqual(loading[0]['gap_ms'], 3000.0)

    def test_label_phase_separates_the_menu_from_a_sector(self):
        menu = [{'draws_per_frame': value} for value in (640.0, 650.0, 645.0)]
        sector = [{'draws_per_frame': value} for value in (100.0, 400.0, 700.0)]
        narrow_but_low = [{'draws_per_frame': value} for value in (80.0, 82.0, 81.0)]
        self.assertEqual(tool.label_phase(menu)[0], 'menu')
        self.assertEqual(tool.label_phase(sector)[0], 'scene')
        self.assertEqual(tool.label_phase(narrow_but_low)[0], 'scene')


class SyntheticSession(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        root = Path(cls.directory.name)
        cls.on_path, cls.off_path = root / 'on.log', root / 'off.log'
        write_on(cls.on_path)
        write_off(cls.off_path)
        cls.report = tool.build([('on', cls.on_path), ('off', cls.off_path)],
                                baseline_label='off', min_bin_windows=1)
        cls.on, cls.off = cls.report['runs'][0], cls.report['runs'][1]
        cls.controlled = cls.report['comparisons'][0]

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    # ---- structure -----------------------------------------------------------------

    def test_configuration_keeps_the_announcement_not_the_later_reuse(self):
        self.assertEqual(self.on['configuration']['mesh_cache']['enabled'], '0')
        self.assertEqual(self.on['configuration']['motion_output']['taa'], '1')
        self.assertEqual(self.on['configuration']['status']['object_trace'], 'active')
        self.assertEqual(self.on['configuration']['profiler']['interval_us'], '2000')
        self.assertEqual(self.on['configuration']['clock_hz'], 10000000)

    def test_other_devices_are_not_folded_in(self):
        # Two loading, two menu, three scene and one capture window on device 1;
        # device 2's summary and its metrics contribute nothing.
        self.assertEqual(self.on['windows']['total'], 8)
        self.assertEqual(sum(p['windows'] for p in self.on['phases']), 6)

    def test_phases_and_their_labels(self):
        labels = [p['label'] for p in self.on['phases']]
        self.assertEqual(labels, ['menu', 'scene'])
        self.assertEqual(self.on['windows']['loading'], 2)
        self.assertEqual([g['gap_ms'] for g in self.on['loading_gaps']],
                         [3000.0, 50000.0])
        self.assertEqual(self.on['windows']['menu_usable'], 2)
        self.assertEqual(self.on['windows']['scene_usable'], 3)
        self.assertEqual(self.on['windows']['capture'], 1)

    def test_phase_labels_are_checked_against_the_route_records(self):
        self.assertEqual(self.on['phase_check']['status'], 'checked')
        menu = self.on['phase_check']['phases'][0]
        self.assertEqual(menu['label'], 'menu')
        self.assertEqual(menu['routed_records'], 0)
        # The route-off run has no records at all and must say so.
        self.assertEqual(self.off['phase_check']['status'], 'unavailable')
        self.assertTrue(self.off['route_off'])
        self.assertFalse(self.on['route_off'])

    def test_hdr_is_absent(self):
        self.assertEqual(self.on['hdr_metrics_present'], [])
        self.assertEqual(self.off['hdr_metrics_present'], [])

    # ---- regimes -------------------------------------------------------------------

    def test_regime_split_and_occupancy(self):
        scene = self.on['scene']
        self.assertEqual(scene['threshold_ms'], 45.0)
        self.assertEqual(scene['windows'], 3)
        self.assertAlmostEqual(scene['fast_occupancy'], 2 / 3, places=4)
        self.assertEqual(scene['fast']['windows'], 2)
        self.assertEqual(scene['slow']['windows'], 1)
        # fast holds the 20 ms and 30 ms windows, slow the 60 ms one.
        self.assertAlmostEqual(scene['fast']['mean_ms']['median'], 25.0)
        self.assertAlmostEqual(scene['slow']['mean_ms']['median'], 60.0)
        self.assertAlmostEqual(scene['fast']['draws_per_frame']['median'], 250.0)
        self.assertAlmostEqual(scene['slow']['draws_per_frame']['median'], 700.0)
        self.assertEqual(scene['histogram_10ms_bins'], {20: 1, 30: 1, 60: 1})

    def test_the_capture_window_is_excluded_from_the_medians(self):
        # 25 ms at 250 draws/frame would land in the fast regime and move both
        # medians if it were kept.
        self.assertEqual([w['frame'] for w in self.on['capture_windows']], [70])
        self.assertAlmostEqual(self.on['capture_frame_interval_ms']['median'], 500.0)
        # The fast regime keeps exactly the 20 ms and 30 ms windows: 20 frames,
        # not the 30 the capture window would add.
        self.assertEqual(self.on['scene']['fast']['frames'], 20)
        self.assertEqual(self.on['scene']['windows'], 3)
        self.assertEqual(self.on['scene_draw_bins'].keys(), {2, 8, 14})

    def test_route_metrics_per_frame_and_per_call(self):
        fast = self.on['scene']['fast']
        # 100 draws: gate 200, route_draw 140, jitter 35, fill 50 -> 425 us/frame.
        # 400 draws: gate 800, route_draw 560, jitter 140, fill 50 -> 1550 us/frame.
        self.assertAlmostEqual(fast['route_exclusive_us_per_frame']['median'],
                               (425.0 + 1550.0) / 2)
        self.assertAlmostEqual(fast['route_metrics_us_per_call']['route_gate']['median'], 2.0)
        self.assertAlmostEqual(fast['route_metrics_us_per_call']['route_set_rt']['median'], 0.25)
        self.assertAlmostEqual(fast['taa_run_us_per_frame']['median'], 300.0)
        self.assertAlmostEqual(fast['draw_backend_us']['median'], 3.0)
        self.assertAlmostEqual(fast['present_normal_us']['median'], 30.0)
        # route_set_rt is inside route_draw and must not be in the exclusive sum.
        self.assertNotIn('route_set_rt', tool.ROUTE_EXCLUSIVE)
        self.assertAlmostEqual(self.on['menu']['route_exclusive_us_per_frame']['median'],
                               round((0.25 * 640 + 0.25 * 650) / 2, 1))

    # ---- per-frame records ---------------------------------------------------------

    def test_frame_record_medians_are_the_middle_scaling(self):
        normal = self.on['frame_records']['normal']
        self.assertEqual(normal['records'], 3)
        self.assertAlmostEqual(normal['draws']['median'], 400.0)
        self.assertAlmostEqual(normal['routed']['median'], 280.0)
        self.assertAlmostEqual(normal['routed_fraction']['median'], 0.7)
        # exclusive(k) = 400k + 280k + 20k + 60; the median is k = 2.
        self.assertAlmostEqual(normal['route_exclusive_us']['median'], 700 * 2 + 60)
        self.assertAlmostEqual(normal['route_us_per_draw']['median'],
                               round((700 * 2 + 60) / 400.0, 3))
        self.assertAlmostEqual(normal['route_us_per_routed_draw']['median'],
                               round((700 * 2 + 60) / 280.0, 3))
        self.assertAlmostEqual(normal['gate_us_per_draw']['median'], 2.0)
        self.assertAlmostEqual(normal['route_draw_us_per_routed']['median'], 2.0)
        self.assertAlmostEqual(normal['set_rt_us_per_call']['median'], 0.125)
        self.assertAlmostEqual(normal['set_rt_per_routed_draw']['median'], 4.0)
        self.assertAlmostEqual(normal['jitter_us_per_write']['median'], 0.0714)
        self.assertAlmostEqual(normal['jitter_writes_per_routed']['median'], 2.0)
        self.assertEqual(normal['scene_end_source'], {'stretchrect': 3})
        self.assertEqual(normal['rt_mode'], {'perdraw': 3})
        self.assertEqual(normal['timing'], {'cpu_qpc': 3})

    def test_capture_records_are_kept_out_of_the_ordinary_estimate(self):
        capture = self.on['frame_records']['capture']
        self.assertEqual(capture['records'], 1)
        self.assertAlmostEqual(capture['readback_us']['median'], 40_000.0)
        self.assertAlmostEqual(capture['readback_us_per_readback']['median'], 20_000.0)
        self.assertAlmostEqual(self.on['frame_records']['normal']['readback_us']['median'], 0.0)

    # ---- overhead attribution ------------------------------------------------------

    def test_profiler_share_follows_the_achieved_tick_rate(self):
        share = self.on['profiler_share']['scene']
        self.assertEqual(share['status'], 'present')
        self.assertAlmostEqual(share['ticks_per_s'], 200.0)
        self.assertAlmostEqual(share['share_of_wall'], 200.0 * 123.0 / 1e6, places=6)
        # The median scene window mean is 30 ms.
        self.assertAlmostEqual(share['ms_per_frame_at_median'],
                               round(200.0 * 123.0 / 1e6 * 30.0, 3))
        self.assertEqual(self.off['profiler_share']['scene']['status'], 'absent')

    def test_stamp_cost_uses_the_menu_gate_sample(self):
        stamps = self.on['telemetry_stamps']
        self.assertAlmostEqual(stamps['menu_gate_us_per_call'], 0.25)
        self.assertAlmostEqual(stamps['stamp_us_assumed'], 0.25)
        # spans/frame = gate + route_draw + set_rt + jitter + fill + taa_run +
        # taa_resolve_draw; at 400 draws/frame that is 400+280+1120+560+1+1+1.
        self.assertAlmostEqual(stamps['spans_per_frame_median'], 2363.0)
        self.assertAlmostEqual(stamps['ms_per_frame'], round(2363.0 * 0.25 / 1000.0, 3))

    # ---- the comparison ------------------------------------------------------------

    def test_regime_comparison(self):
        fast = self.controlled['regimes']['fast']
        self.assertTrue(self.controlled['controlled'])
        self.assertAlmostEqual(fast['primary']['mean_ms'], 25.0)
        self.assertAlmostEqual(fast['baseline']['mean_ms'], 18.5)
        self.assertAlmostEqual(fast['delta_mean_ms'], 6.5)
        self.assertAlmostEqual(fast['delta_mean_percent'],
                               round((25.0 / 18.5 - 1) * 100.0, 2))
        slow = self.controlled['regimes']['slow']
        self.assertAlmostEqual(slow['delta_mean_ms'], 15.0)
        self.assertAlmostEqual(slow['route_exclusive_ms'], 2.675)

    def test_menu_slope_is_reported_as_ill_conditioned_but_the_origin_holds(self):
        menu = self.controlled['normalised']['menu']
        # Two bins only 10 draws apart: (8.5 - 8.0) / 10 = 0.05 ms/draw.
        self.assertAlmostEqual(menu['us_per_draw'], 50.0)
        self.assertEqual(menu['draws_span'], [640.0, 650.0])
        expected = tool.quantile([8000.0 / 640.0, 8500.0 / 650.0], 0.5)
        self.assertAlmostEqual(menu['origin_us_per_draw']['median'], round(expected, 3))

    def test_scene_slope_and_the_route_attribution(self):
        scene = self.controlled['normalised']['scene']
        self.assertEqual(scene['bins'], 3)
        # deltas 5, 8 and 15 ms at 100, 400 and 700 draws: pairwise slopes
        # 0.01, 0.0166667 and 0.0233333 ms/draw, median 0.0166667.
        self.assertAlmostEqual(scene['us_per_draw'], round(1000 * (15 - 5) / 600.0, 3))
        self.assertAlmostEqual(scene['fixed_ms_per_frame'],
                               round(15.0 - (15 - 5) / 600.0 * 700.0, 3))
        self.assertEqual(scene['sign_test']['positive'], 3)
        attribution = self.controlled['attribution']
        overhead = self.controlled['normalised']['menu']['origin_us_per_draw']['median']
        self.assertAlmostEqual(attribution['menu_overhead_us_per_draw'], overhead)
        self.assertAlmostEqual(attribution['route_attributed_us_per_draw'],
                               round(scene['us_per_draw'] - overhead, 3))
        self.assertAlmostEqual(attribution['draws_per_frame_median'], 400.0)
        self.assertAlmostEqual(attribution['route_metric_us_per_draw'],
                               round(1550.0 / 400.0, 3))

    def test_route_cost_per_regime_is_the_headline(self):
        costs = self.controlled['route_cost_per_regime']
        attributed = self.controlled['attribution']['route_attributed_us_per_draw']
        fast = costs['fast']
        self.assertAlmostEqual(fast['draws_per_frame'], 250.0)
        self.assertAlmostEqual(fast['route_attributed_ms'],
                               round(attributed * 250.0 / 1000.0, 3))
        self.assertAlmostEqual(fast['route_attributed_percent'],
                               round(attributed * 250.0 / 1000.0 / 25.0 * 100.0, 2))
        self.assertAlmostEqual(fast['route_metric_ms'], round(987.5 / 1000.0, 3))
        self.assertAlmostEqual(costs['slow']['route_attributed_ms'],
                               round(attributed * 700.0 / 1000.0, 3))

    def test_a_bin_with_too_few_windows_is_dropped(self):
        # Every synthetic bin holds exactly one window, so raising the floor to
        # two must empty the pairing rather than silently keep it.
        strict = tool.compare(self.on, self.off, min_bin_windows=2)
        self.assertEqual(strict['normalised']['scene']['bins'], 0)
        self.assertNotIn('attribution', strict)

    def test_two_route_on_runs_are_not_treated_as_a_control(self):
        noise = tool.compare(self.on, self.on, min_bin_windows=1)
        self.assertFalse(noise['controlled'])
        self.assertNotIn('attribution', noise)
        self.assertIsNone(noise['regimes']['fast']['route_share_of_delta'])

    # ---- levers --------------------------------------------------------------------

    def test_levers_are_ranked_by_saving_and_use_the_fixture_set_rt_ratio(self):
        levers = self.report['levers']
        self.assertEqual(levers['status'], 'ranked')
        savings = [item['saving_us_per_frame'] for item in levers['items']]
        self.assertEqual(savings, sorted(savings, reverse=True))
        self.assertEqual([item['rank'] for item in levers['items']],
                         list(range(1, len(levers['items']) + 1)))
        by_metric = {item['metric']: item for item in levers['items']}
        self.assertAlmostEqual(by_metric['route_gate']['now_us_per_frame'], 800.0)
        self.assertAlmostEqual(by_metric['route_gate']['saving_us_per_frame'], 400.0)
        # 20 -> 12 SetRenderTarget is 40% of the per-draw mode's set_rt cost.
        self.assertAlmostEqual(by_metric['route_set_rt']['now_us_per_frame'], 140.0)
        self.assertAlmostEqual(by_metric['route_set_rt']['saving_us_per_frame'],
                               round(140.0 * 8 / 20, 1))
        self.assertAlmostEqual(by_metric['route_jitter']['saving_us_per_frame'], 20.0)
        self.assertAlmostEqual(by_metric['taa_run']['saving_us_per_frame'], 0.0)
        self.assertAlmostEqual(levers['route_exclusive_us_per_frame_median'], 1460.0)

    # ---- output contract -----------------------------------------------------------

    def test_report_is_json_serializable_and_text_renders(self):
        text = tool.render_text(self.report)
        self.assertIn('route cost fast', text)
        self.assertIn('cost levers', text)
        payload = json.loads(json.dumps(tool.strip_rows(
            tool.build([('on', self.on_path), ('off', self.off_path)],
                       baseline_label='off', min_bin_windows=1))))
        self.assertNotIn('_rows', payload['runs'][0])
        self.assertEqual(len(payload['limits']), 6)

    def test_an_unknown_baseline_is_refused(self):
        with self.assertRaises(tool.Malformed):
            tool.build([('on', self.on_path)], baseline_label='missing')

    def test_malformed_records_are_counted_not_guessed(self):
        directory = tempfile.TemporaryDirectory()
        path = Path(directory.name) / 'bad.log'
        path.write_text('\n'.join([
            'telemetry_summary device=1 frame=x reason=interval qpc=1'
            ' since_start_us=1.0 interval_us=1.0',
            'telemetry_metric device=1 name=frame_normal count=0 failures=0 total_us=1.0'
            ' min_us=1.0 max_us=1.0 bytes=0 buckets=0,0,0,0,0,0',
            'hdr_frame device=1 frame=1 redirected=1',
            'profile_report scope=delta since_start_us=1.0 elapsed_us=0.0 ticks=1']) + '\n')
        result = tool.scan(path)
        self.assertEqual(result['rejected']['telemetry_summary'], 1)
        self.assertEqual(result['rejected']['hdr_frame'], 1)
        self.assertEqual(result['rejected']['profile_report'], 1)
        self.assertEqual(result['windows'], [])
        directory.cleanup()


if __name__ == '__main__':
    unittest.main()
