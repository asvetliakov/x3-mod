"""Tests for tools/analysis/analyze_iteration12.py.

Synthetic inputs with closed-form answers for every derivation: the sharpen
and mip-bias decode of ``motion_output_frame`` records, the ordinary/capture
split (capture frames are those with a ``motion_output_readback`` line), the
interleave runs over ``motion_route`` lines, the condensed blur table, the
row-vectorised RCAS against the fixture runner's per-pixel double reference,
the 3x3 escape count and quantisation, the strong-edge and ESF halo measures
and the flicker class ratios. The last group pins the tracked summary
``verification/results/iteration-12.json`` to its own arithmetic.
"""

import json
import math
import random
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))

import analyze_iteration08_taa as it08  # noqa: E402
import analyze_iteration12 as it12       # noqa: E402

SUMMARY = ROOT / 'verification' / 'results' / 'iteration-12.json'

FRAME = ('motion_output_frame device={device} frame={frame} latched=1 filled=1 draws={draws} routed={routed} '
         'matched={routed} taa=1 taa_attempted={res} taa_resolved={res} taa_history={res} taa_skip=0 '
         'taa_copy={copy} taa_sharpen={sharpen} taa_copy_back_us={copy_back} taa_run_us=300.0 taa_draw_us=250.0 '
         'cut=0 cut_median_px={cut} cut_missing=0.0 camera_policy=2 camera_cut=0 camera_rotation_deg=1.0 '
         'jitter_index=0 jitter_x=0.0 jitter_y=0.0 mip_bias=-0.5 mip_bias_sets={sets} mip_bias_restores={restores} '
         'mip_bias_draws={routed} mip_bias_stages={stages} mip_bias_reads={reads} mip_bias_game_writes=0 '
         'mip_bias_game_writes_total={writes} mip_bias_failures={failures} mip_bias_biased_now={now}')


def frame(number, draws=10, routed=8, res=1, copy='00000001', sharpen=1, copy_back=0.0, cut=0.1,
          sets=4, restores=4, stages='000f', reads=0, writes=0, failures=0, now='0000', device=1):
    return FRAME.format(device=device, frame=number, draws=draws, routed=routed, res=res, copy=copy,
                        sharpen=sharpen, copy_back=copy_back, cut=cut, sets=sets, restores=restores,
                        stages=stages, reads=reads, writes=writes, failures=failures, now=now)


def readback(number):
    return (f'motion_output_readback device=1 frame={number} file=motion_1_{number}.rgba32f width=1280 '
            f'height=768 format=rgba32f_row_major result=00000000 bytes=15728640')


def route(number, index, routed):
    return f'motion_route device=1 frame={number} index={index} gate={0 if routed else 4} routed={int(routed)} matched={int(routed)}'


HEADER = [
    'motion_output_mode requested=1 taa=1 taa_debug=1 jitter=1 jitter_samples=8 rt_mode=perdraw scene_hook=1 '
    'hdr=0 taa_k=-1.00000 mip_bias=-0.5 taa_sharpen=0.500',
    'x3-modern-renderer version=0.4 schema=2 capture_start=999999 capture_frames=4 pointer_bits=32',
    'telemetry_start schema=1 qpc_frequency=10000000 qpc=0 anchor=proxy_initialize cpu_only=1 per_draw=0',
    'motion_output_taa device=1 initialize=00000000 references=2 sharpen=0.500',
]


def write(lines):
    handle = tempfile.NamedTemporaryFile('w', suffix='.log', delete=False)
    handle.write('\n'.join(lines) + '\n')
    handle.close()
    return Path(handle.name)


class LogSectionsTest(unittest.TestCase):
    def scan(self, lines):
        return it12.scan_log(write(HEADER + lines))

    def test_features_and_announcement(self):
        features, announcement = it12.configuration_report(self.scan([frame(1)]))
        self.assertEqual(dict(features), {'taa_sharpen': 0.5, 'mip_bias': -0.5, 'taa_debug': 1,
                                          'capture_frames': 4, 'per_draw': 0})
        self.assertEqual(announcement['taa'][0]['references'], '2')

    def test_sharpen_flags_and_copy(self):
        lines = [frame(1, copy_back=0.0), frame(2, copy_back=0.0), frame(3, res=0, sharpen=0, routed=0),
                 frame(4, sharpen=0, copy='00000000', copy_back=0.9),
                 'motion_output_sharpen_failed device=1 frame=4 result=80004005 failures=1 disabled=0']
        report = it12.sharpen_report(self.scan(lines))
        self.assertEqual((report['records'], report['resolves'], report['sharpened_frames']), (4, 3, 2))
        self.assertEqual(report['sharpened_resolved_frames'], 2)
        self.assertEqual(report['sharpened_unresolved_frames'], 0)
        self.assertEqual(report['taa_copy_S_FALSE_records'], 3)
        self.assertEqual(report['sharpen_failed_lines'], 1)
        # Median over the resolved records: 0.0, 0.0, 0.9.
        self.assertEqual(report['taa_copy_back_us_median'], 0.0)

    def test_other_devices_are_ignored(self):
        report = it12.sharpen_report(self.scan([frame(1), frame(2, device=2)]))
        self.assertEqual(report['records'], 1)

    def test_mip_bias_totals_and_split(self):
        lines = [frame(1, sets=3, restores=3, routed=6, stages='000f'),
                 frame(2, sets=5, restores=4, routed=6, stages='007f', reads=5, writes=3, now='0001'),
                 frame(3, res=0, routed=0, sets=0, restores=0, stages='0000'),
                 # The capture frame: every routed draw re-sets (4 stages each).
                 frame(10, sets=24, restores=24, routed=6, stages='000f', writes=7), readback(10),
                 'motion_output_mip_bias_game_write device=1 frame=4 stage=4 value=00000000 bias=0 writes=1',
                 'motion_output_mip_bias_game_write device=1 frame=4 stage=3 value=00000000 bias=0 writes=2',
                 'sampler stage=0 state=8 value=0 bias=0', 'sampler stage=1 state=8 value=0 bias=0',
                 'sampler stage=0 state=6 value=2', 'sampler stage=3 state=8 value=bf000000 bias=-0.5']
        report = it12.mip_bias_report(self.scan(lines))
        self.assertEqual((report['sets'], report['restores'], report['biased_draws']), (32, 31, 18))
        self.assertEqual(report['biased_now_nonzero_records'], 1)
        self.assertEqual((report['reads'], report['reads_frames']), (5, [2]))
        self.assertEqual(report['game_writes_total'], 7)
        self.assertEqual((report['game_write_lines'], report['game_write_stages'], report['game_write_values']),
                         (2, [3, 4], [0.0]))
        self.assertFalse(report['summary_line_present'])
        self.assertEqual(dict(report['stage_mask_histogram']), {'0000': 1, '000f': 2, '007f': 1})
        self.assertEqual((report['capture_sampler_state8_records'], report['capture_sampler_state8_nonzero_bias']), (3, 1))
        ordinary, capture = report['ordinary_frames'], report['capture_frames']
        self.assertEqual((ordinary['records'], ordinary['sets'], ordinary['restores'], ordinary['biased_draws']), (2, 8, 7, 12))
        self.assertAlmostEqual(ordinary['sets_per_biased_draw'], 8 / 12)
        self.assertAlmostEqual(ordinary['sets_plus_restores_per_biased_draw'], 15 / 12)
        self.assertAlmostEqual(ordinary['set_calls_per_frame'], 15 / 2)
        self.assertEqual((capture['records'], capture['sets']), (1, 24))
        self.assertAlmostEqual(capture['sets_per_biased_draw'], 4.0)

    def test_summary_line_is_recorded(self):
        report = it12.mip_bias_report(self.scan([frame(1), 'motion_output_mip_bias_summary device=1 sets=1 restores=1']))
        self.assertTrue(report['summary_line_present'])

    def test_interleave_runs(self):
        # Frame 10 (capture): routed, routed, unrouted, routed | frame 11 (capture): routed, routed;
        # frame 20 is not a capture frame and does not count.
        lines = [frame(10, routed=3, sets=12, restores=12), readback(10), frame(11, routed=2, sets=8, restores=8), readback(11),
                 frame(20, routed=2, sets=1, restores=1),
                 route(10, 1, True), route(10, 2, True), route(10, 3, False), route(10, 4, True),
                 route(11, 1, True), route(11, 2, True), route(20, 1, True), route(20, 2, True)]
        report = it12.mip_bias_report(self.scan(lines))
        interleave = report['interleave']
        self.assertEqual((interleave['draws'], interleave['routed'], interleave['runs_of_consecutive_routed']), (6, 5, 3))
        self.assertAlmostEqual(interleave['mean_routed_per_run'], 5 / 3)
        self.assertAlmostEqual(interleave['runs_per_frame'], 1.5)
        # Capture frames set 4 stages per routed draw; the ordinary frame set 0.5 per draw:
        # one set group every 8 draws against the 5/3-draw interleave bound.
        self.assertAlmostEqual(interleave['stages_per_set_group'], 4.0)
        self.assertAlmostEqual(interleave['ordinary_draws_per_set_group'], 8.0)
        self.assertAlmostEqual(interleave['excess_over_interleave_bound'], (5 / 3) / 8.0)

    def test_routed_runs_split_at_frame_boundaries(self):
        flags = [(1, 1, True), (1, 2, True), (2, 1, True), (2, 2, False), (2, 3, True), (3, 1, False)]
        self.assertEqual(it12.routed_runs(flags), 3)
        self.assertEqual(it12.routed_runs([]), 0)


class BlurTableTest(unittest.TestCase):
    @staticmethod
    def entry():
        def per_frame(interior, everything, raw_rise, res_rise, raw_mtf, res_mtf):
            return {'ratios': {'routed_interior': {'gradient_energy_ratio': interior},
                               'all': {'gradient_energy_ratio': everything}},
                    'edge_spread': {'raw': {'rise_10_90_px': raw_rise, 'mtf50_cycles_per_px': raw_mtf},
                                    'resolved': {'rise_10_90_px': res_rise, 'mtf50_cycles_per_px': res_mtf}}}
        return {'frames': [1380, 1381, 1382, 1383], 'status': 'evaluated',
                'route': {'motion': 'stationary', 'cut_median_px_peak': 0.0068, 'camera_rotation_deg': [1.0, 1.069, 0.9, 0.95]},
                'classes': {'routed_interior': 71519},
                'per_frame': [per_frame(0.52, 0.68, 0.80, 0.97, 1.34, 0.42), per_frame(0.54, 0.69, 0.83, 1.00, 1.39, 0.44)],
                'jitter_phase_spread': {'relative_spread_all': 0.0152, 'relative_spread_routed_interior': 0.0057},
                'captured_phase_average': {'ratios': {'routed_interior': {'gradient_energy_ratio': 0.64}},
                                           'scale_to_resolve_kernel': 0.5}}

    def test_condensed_row(self):
        row = it12.condense_burst(self.entry())
        self.assertEqual(row['frames'], [1380, 1383])
        self.assertEqual((row['motion'], row['routed_interior_px'], row['camera_rotation_deg_max']), ('stationary', 71519, 1.069))
        self.assertEqual(row['gradient_energy_ratio_interior'], {'min': 0.52, 'max': 0.54, 'mean': 0.53})
        self.assertAlmostEqual(row['gradient_energy_ratio_all_mean'], 0.685)
        self.assertEqual(row['raw_rise_10_90_px'], [0.80, 0.83])
        self.assertEqual(row['resolved_mtf50_cycles_per_px'], [0.42, 0.44])
        self.assertEqual(row['raw_phase_spread'], {'all': 0.0152, 'routed_interior': 0.0057})
        ideal = row['ideal']
        self.assertAlmostEqual(ideal['floor'], 0.32)
        self.assertAlmostEqual(ideal['measured_over_floor'], 0.53 / 0.32)
        self.assertAlmostEqual(ideal['share_of_loss_ideal'], 0.68 / 0.47)

    def test_unevaluated_burst_keeps_its_status(self):
        rows = it12.blur_tables({'run10': {'blur': [self.entry(), {'frames': [9536, 9540], 'status': 'missing_readbacks',
                                                                  'missing': ['x'], 'route': {'motion': 'slow', 'cut_median_px_peak': 2.7}}]}})
        self.assertEqual([r['status'] for r in rows['run10']], ['evaluated', 'missing_readbacks'])
        self.assertEqual(rows['run10'][1]['missing'], ['x'])
        self.assertNotIn('ideal', rows['run10'][1])

    def test_missing_edge_spread(self):
        entry = self.entry()
        for f in entry['per_frame']:
            del f['edge_spread']
        row = it12.condense_burst(entry)
        self.assertIsNone(row['raw_rise_10_90_px'])


def rows_of(values, width, height):
    return [list(values[y * width:(y + 1) * width]) for y in range(height)]


class RcasTest(unittest.TestCase):
    WIDTH, HEIGHT = 9, 7

    def image(self, seed=7):
        rng = random.Random(seed)
        pixels = []
        for y in range(self.HEIGHT):
            for x in range(self.WIDTH):
                base = 0.8 if (x // 3 + y // 2) % 2 else 0.2
                pixels.append(tuple(min(max(base + rng.uniform(-0.15, 0.15), 0.0), 1.0) for _ in range(3)))
        # A few extreme rings: black and white pixels (zero lobe in that channel) and a NaN.
        pixels[10] = (0.0, 0.0, 0.0)
        pixels[20] = (1.0, 1.0, 1.0)
        pixels[30] = (float('nan'), 0.5, 2.0)
        return pixels

    def channels(self, pixels):
        return tuple(rows_of([p[c] for p in pixels], self.WIDTH, self.HEIGHT) for c in range(3))

    def test_matches_the_runner_reference(self):
        try:
            import run_motion_output as runner
        except Exception as error:  # pragma: no cover - the runner imports the probe helpers
            raise unittest.SkipTest(f'run_motion_output not importable: {error}')
        pixels = self.image()
        # The runner's reference is not NaN-safe (Python's min/max propagate
        # it); the vectorised port saturates NaN to 0 like the shader's
        # saturate() on the verified backend, so the reference sees a 0 there.
        reference_pixels = [tuple(0.0 if c != c else c for c in p) for p in pixels]
        for gain in (1.0, 0.5, 0.354):
            expected = runner.rcas_reference(reference_pixels, self.WIDTH, self.HEIGHT, gain)
            out = it12.rcas_rows(self.channels(pixels), self.WIDTH, self.HEIGHT, gain)
            for y in range(self.HEIGHT):
                for x in range(self.WIDTH):
                    for c in range(3):
                        self.assertAlmostEqual(out[c][y][x], expected[y * self.WIDTH + x][c], places=12, msg=(gain, x, y, c))
        self.assertAlmostEqual(it12.rcas_gain(0.5), runner.sharpen_gain(0.5))

    def test_gain_and_flat_image(self):
        self.assertEqual(it12.rcas_gain(1.0), 1.0)
        self.assertEqual(it12.rcas_gain(0.5), 0.5)
        self.assertAlmostEqual(it12.rcas_gain(0.25), 2.0 ** -1.5)
        flat = tuple([[0.37] * 5 for _ in range(4)] for _ in range(3))
        out = it12.rcas_rows(flat, 5, 4, 1.0)
        self.assertEqual(out, flat)

    def test_sharpen_stays_inside_the_neighbourhood_and_changes_edges(self):
        pixels = self.image()
        channels = self.channels(pixels)
        out = it12.rcas_rows(channels, self.WIDTH, self.HEIGHT, 1.0)
        reference = tuple(it12.quantise_rows(c) for c in channels)
        codes = tuple(it12.quantise_rows(c) for c in out)
        escapes, rows = it12.escapes_3x3(codes, reference, self.WIDTH, self.HEIGHT)
        self.assertEqual(escapes, 0)
        self.assertEqual(sum(map(sum, rows)), 0)
        change = it12.code_change(codes, reference)
        self.assertGreater(change['changed_pixels'], 0)
        self.assertGreater(change['max_code_change'], 0)
        # NaN saturates to 0 and 2.0 to 1 before anything else.
        self.assertEqual(reference[0][3][3], 0)
        self.assertEqual(reference[2][3][3], 255)

    def test_quantisation_rounds_half_up_and_clamps(self):
        self.assertEqual(it12.quantise_rows([[0.0, 1.0, 0.5, 0.001, -1.0, 2.0]]), [[0, 255, 128, 0, 0, 255]])

    def test_code_error(self):
        codes = ([[128, 0]], [[255, 1]], [[0, 0]])
        values = ([[0.5, 0.0]], [[2.0, 1 / 255.0]], [[0.0, 0.0]])
        error = it12.code_error(codes, values)
        self.assertAlmostEqual(error['max_code_error'], 0.5)
        self.assertAlmostEqual(error['mean_code_error'], 0.5 / 6)

    def test_escapes_are_counted_per_channel(self):
        reference = ([[10, 10, 10], [10, 10, 10]], [[0, 0, 0], [0, 0, 0]], [[5, 5, 5], [5, 5, 5]])
        codes = ([[10, 11, 10], [9, 10, 10]], [[0, 0, 0], [0, 0, 0]], [[5, 5, 6], [5, 5, 5]])
        count, rows = it12.escapes_3x3(codes, reference, 3, 2)
        self.assertEqual(count, 3)
        self.assertEqual(rows, [[0, 1, 1], [1, 0, 0]])

    def test_neighbourhood_extremes(self):
        rows = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
        mins, maxs = it12.neighbourhood_extremes(rows, 3, 3)
        self.assertEqual(mins, [[1, 1, 2], [1, 1, 2], [4, 4, 5]])
        self.assertEqual(maxs, [[5, 6, 6], [8, 9, 9], [8, 9, 9]])


class HaloTest(unittest.TestCase):
    def test_strong_edge_amplification(self):
        width, height = 8, 5
        reference = [[0.1] * 4 + [0.9] * 4 for _ in range(height)]
        # The presented image keeps the edge inside the neighbourhood...
        inside = [[0.1, 0.1, 0.1, 0.05 + 0.05, 0.9, 0.9, 0.9, 0.9] for _ in range(height)]
        no_escapes = [[0] * width for _ in range(height)]
        stats = it12.strong_edge_amplification(reference, inside, width, height, no_escapes, threshold=0.5, margin=1)
        self.assertEqual(stats["pixels"], 2 * 3)  # columns 3 and 4 (3x3 range 0.8) in rows 1-3
        self.assertAlmostEqual(stats['local_range_ratio'], 1.0)
        self.assertEqual(stats['channel_escapes_3x3'], 0)
        # ...and one that rings past it raises the range and is counted where it escaped.
        halo = [row[:] for row in inside]
        halo[2][4] = 0.95
        escapes = [row[:] for row in no_escapes]
        escapes[2][4] = 2
        escapes[0][0] = 1  # outside the margin and not a strong edge: not counted
        stats = it12.strong_edge_amplification(reference, halo, width, height, escapes, threshold=0.5, margin=1)
        self.assertEqual(stats['channel_escapes_3x3'], 2)
        self.assertGreater(stats['local_range_ratio'], 1.0)
        # Without escape rows only the amplification is reported.
        stats = it12.strong_edge_amplification(reference, halo, width, height, threshold=0.5, margin=1)
        self.assertEqual(stats['channel_escapes_3x3'], 0)

    def test_esf_overshoot(self):
        self.assertIsNone(it12.esf_overshoot({}))
        knots = [[-4, 0.3], [-2, -0.05], [-1, 0.1], [0, 0.5], [1, 1.08], [2, 1.0], [4, 1.5]]
        over = it12.esf_overshoot({'edges': 12, 'esf_knots': knots})
        self.assertAlmostEqual(over['above_one'], 0.08)  # the far tails (|x| > 2 px) are excluded
        self.assertAlmostEqual(over['below_zero'], 0.05)
        self.assertEqual((over['edges'], over['window_px']), (12, 2.0))
        self.assertAlmostEqual(it12.esf_overshoot({'esf_knots': knots}, window=5.0)['above_one'], 0.5)


class FlickerTest(unittest.TestCase):
    def test_class_ratios(self):
        import array
        classes = bytearray([it08.SENTINEL, it08.INTERIOR, it08.INTERIOR, it08.EDGE])
        raw = array.array('f', [0.0, 0.04, 0.02, 0.01])
        resolved = array.array('f', [0.0, 0.004, 0.002, 0.005])
        presented = array.array('f', [0.0, 0.008, 0.004, 0.005])
        thin = bytearray([0, 1, 0, 0])
        report = it12.flicker_classes(raw, resolved, presented, classes, thin, {'variance_floor': 1e-5})
        interior = report['classes']['routed_interior']
        self.assertEqual(interior['pixels'], 2)
        self.assertAlmostEqual(interior['aggregate_ratio']['resolved'], 0.1, places=6)
        self.assertAlmostEqual(interior['aggregate_ratio']['presented'], 0.2, places=6)
        self.assertAlmostEqual(interior['presented_over_resolved'], 2.0, places=6)
        edge = report['classes']['routed_edge']
        self.assertAlmostEqual(edge['presented_over_resolved'], 1.0, places=6)
        self.assertEqual(report['classes']['thin_feature']['pixels'], 1)
        self.assertTrue(report['classes']['thin_feature']['overlay'])
        self.assertAlmostEqual(report['presented_over_resolved_energy'], 0.017 / 0.011, places=5)

    def test_baseline_extraction(self):
        evaluated = {'source': {'log': 'a.log'},
                     'flicker': {'status': 'evaluated', 'bursts': [
                         {'cut_median_px': [0.0], 'analysis': {'status': 'unavailable'}},
                         {'cut_median_px': [0.0, 0.0], 'analysis': {'status': 'evaluated', 'frames': [4754, 4755],
                                                                    'classes': {'sentinel': {'aggregate_ratio': 0.015},
                                                                                'routed_interior': {'aggregate_ratio': 0.0097}}}}]}}
        base = it12.baseline_flicker(evaluated)
        self.assertEqual(base['frames'], [4754, 4755])
        self.assertEqual(base['aggregate_ratio'], {'sentinel': 0.015, 'routed_interior': 0.0097})
        unavailable = it12.baseline_flicker({'source': {'log': 'b.log'}, 'flicker': {'status': 'unavailable', 'reason': 'none'}})
        self.assertEqual(unavailable['status'], 'unavailable')
        self.assertIsNone(it12.baseline_flicker(None))


class ReadbackChecksTest(unittest.TestCase):
    def test_scalars_only(self):
        checks = it12.readback_checks({'checks': {'depth': {'status': 'fail', 'max_error': 0.15, 'frames': [1, 2], 'detail': {'a': 1}, 'note': None}}})
        self.assertEqual(dict(checks['depth']), {'status': 'fail', 'max_error': 0.15, 'note': None})


class PublishedSummaryTests(unittest.TestCase):
    """The tracked iteration-12 summary must keep reproducing its own tables."""

    @classmethod
    def setUpClass(cls):
        if not SUMMARY.exists():
            raise unittest.SkipTest(f'{SUMMARY} not present')
        cls.report = json.loads(SUMMARY.read_text())

    def test_every_resolve_was_sharpened_and_no_copy_back_ran(self):
        s = self.report['sharpen']
        self.assertEqual(s['sharpened_resolved_frames'], s['resolves'])
        self.assertEqual(s['sharpened_unresolved_frames'], 0)
        self.assertEqual(s['taa_copy_S_FALSE_records'], s['records'])
        self.assertEqual(s['sharpen_failed_lines'], 0)
        self.assertEqual(s['taa_copy_back_us_median'], 0.0)

    def test_the_mip_bias_arithmetic(self):
        m = self.report['mip_bias']
        self.assertEqual(m['biased_draws'], m['routed_draws'])
        self.assertEqual((m['failures'], m['biased_now_nonzero_records']), (0, 0))
        for label in ('ordinary_frames', 'capture_frames'):
            t = m[label]
            self.assertAlmostEqual(t['sets_per_biased_draw'], t['sets'] / t['biased_draws'])
            self.assertAlmostEqual(t['set_calls_per_frame'], (t['sets'] + t['restores']) / t['records'])
        self.assertEqual(m['ordinary_frames']['sets'] + m['capture_frames']['sets'], m['sets'])
        i = m['interleave']
        self.assertAlmostEqual(i['mean_routed_per_run'], i['routed'] / i['runs_of_consecutive_routed'])
        self.assertEqual(m['capture_frames']['biased_draws'], i['routed'])
        self.assertEqual(sum(m['stage_mask_histogram'].values()), m['records'])
        self.assertEqual(m['capture_sampler_state8_nonzero_bias'], 0)

    def test_every_run10_burst_is_evaluated(self):
        rows = self.report['blur']['run10']
        self.assertEqual(len(rows), 6)
        self.assertTrue(all(r['status'] == 'evaluated' for r in rows))
        self.assertEqual([r['frames'][0] for r in rows], [1380, 1746, 2717, 3856, 8369, 9536])
        for r in rows:
            ideal = r['ideal']
            self.assertAlmostEqual(ideal['floor'], ideal['four_phase_ratio'] * ideal['scale_to_resolve_kernel'])
            self.assertAlmostEqual(ideal['measured'], r['gradient_energy_ratio_interior']['mean'])

    def test_the_presented_image_never_leaves_its_neighbourhood(self):
        p = self.report['presented']
        self.assertEqual(p['status'], 'evaluated')
        self.assertAlmostEqual(p['gain'], 0.5)
        for burst in p['bursts']:
            self.assertEqual(burst['status'], 'evaluated')
            self.assertEqual(burst['escapes_3x3_total'], 0)
            for item in burst['per_frame']:
                self.assertEqual(item['strong_edges']['channel_escapes_3x3'], 0)
                self.assertGreater(item['change_vs_unsharpened']['changed_pixels'], 0)
                g = item['gradient']['routed_interior']
                self.assertAlmostEqual(g['presented_over_resolved'], g['presented_over_raw'] / g['resolved_over_raw'], places=6)
            for name, cls in burst['flicker']['classes'].items():
                if cls['aggregate_ratio']['resolved']:
                    self.assertAlmostEqual(cls['presented_over_resolved'],
                                           cls['aggregate_ratio']['presented'] / cls['aggregate_ratio']['resolved'], places=6)


if __name__ == '__main__':
    unittest.main()
