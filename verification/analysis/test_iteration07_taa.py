"""Synthetic fixtures for tools/analysis/analyze_iteration07_taa.py.

No game data.  A small capture log is generated with the exact line formats the
proxy writes, and the readback images are generated from one analytic, smooth
intensity function sampled at known sub-pixel offsets, so the true shift between
two frames is known in closed form and the analyzer's own estimator is never
used to build its own expectation.

Two image fixtures differ only in how the *resolved* frames are built:

* ``defect``  - history sampled at the previous position plus the previous
  jitter, so the accumulated image is dragged along with the jitter,
  ``d_n = (1-w) j_n + w (d_(n-1) + j_n - j_(n-1))``;
* ``correct`` - history on the unjittered grid,
  ``d_n = (1-w) j_n + w d_(n-1)``.

The analyzer must return ``tracks_jitter`` for the first and ``stable`` for the
second, from the same pre-resolve colour frames.
"""
import array
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
import analyze_iteration07_taa as tool  # noqa: E402

W, H = 160, 128
TILE = 64
WEIGHT = 0.9
# Halton-like centred offsets in pixels; only their differences matter here.
JITTER = [(-0.375, -0.0555556), (0.125, 0.2777778), (-0.125, -0.2777778), (0.375, 0.0555556)]
FRAMES = [10, 11, 12, 13]


def _components(count=24, seed=12345):
    """Deterministic broadband component set: a fixed LCG picks the frequencies,
    orientations and phases, so the pattern is analytic (it can be sampled at any
    sub-pixel offset exactly) yet has energy in every band the estimators use."""
    state = seed
    out = []
    for _ in range(count):
        values = []
        for _ in range(3):
            state = (1103515245 * state + 12345) % (1 << 31)
            values.append(state / float(1 << 31))
        radius = 0.03 + 0.11 * values[0]
        angle = 2.0 * math.pi * values[1]
        out.append((radius * math.cos(angle), radius * math.sin(angle),
                    2.0 * math.pi * values[2], 1.0 / (1.0 + 12.0 * radius)))
    scale = 0.32 / sum(abs(c[3]) for c in out)
    return [(fx, fy, phase, weight * scale) for fx, fy, phase, weight in out]


COMPONENTS = _components()


def pattern(x, y):
    """Broadband, deterministic, analytic intensity in roughly [0.18, 0.82]."""
    total = 0.5
    for fx, fy, phase, weight in COMPONENTS:
        total += weight * math.sin(2.0 * math.pi * (fx * x + fy * y) + phase)
    return total


def shifted_image(dx, dy):
    """Content displaced by (+dx, +dy): pixel (x, y) shows the point (x-dx, y-dy)."""
    return [pattern(x - dx, y - dy) for y in range(H) for x in range(W)]


def half_bits(value):
    """binary32 -> binary16 bits, round to nearest even (normal range only)."""
    raw = struct.unpack('<I', struct.pack('<f', value))[0]
    sign = (raw >> 16) & 0x8000
    exponent = ((raw >> 23) & 255) - 127 + 15
    mantissa = raw & 0x7fffff
    if value == 0:
        return sign
    rounded = mantissa + 0xfff + ((mantissa >> 13) & 1)
    return sign | ((exponent << 10) + (rounded >> 13))


def write_bgra8(path, image):
    data = bytearray()
    for value in image:
        level = max(0, min(255, int(round(value * 255.0))))
        data.extend((level, level, level, 255))
    path.write_bytes(bytes(data))


def write_rgba16f(path, image):
    raw = array.array('H')
    for value in image:
        bits = half_bits(value)
        raw.extend((bits, bits, bits, half_bits(1.0)))
    if sys.byteorder != 'little':
        raw.byteswap()
    path.write_bytes(raw.tobytes())


VALID_RECT = (16, W - 16, 12, H - 12)


def write_motion(path, valid_rect=VALID_RECT):
    """RGBA32F readback: alpha 1 inside the rectangle (routed scene pixels), -1 outside."""
    x0, x1, y0, y1 = valid_rect
    out = array.array('f')
    for y in range(H):
        for x in range(W):
            inside = x0 <= x < x1 and y0 <= y < y1
            out.extend((x / W, y / H, 0.5, 1.0) if inside else (0.0, 0.0, 0.0, -1.0))
    if sys.byteorder != 'little':
        out.byteswap()
    path.write_bytes(out.tobytes())


def frame_line(frame, *, routed=4, matched=4, taa_skip=0, jitter_index=0, jitter=(0.0, 0.0),
               previous=(0.0, 0.0), gates=(0, 1, 0, 2, 0, 0), cut_median=0.0, references=12):
    resolved = 1 if taa_skip == 0 else 0
    return ('motion_output_frame device=1 frame={f} latched=1 filled=1 fill_result=00000000 '
            'fill_restore=00000000 draws={d} routed={r} matched={m} gate1={g1} gate2={g2} gate3={g3} '
            'gate4={g4} gate5={g5} gate6={g6} apply_failures=0 restore_failures=0 history_previous=0 '
            'history_current=0 committed=1 selector_state=9 present=00000000 depth=1 depth_routed={r} '
            'jitter=1 jitter_index={ji} jitter_x={jx:.6f} jitter_y={jy:.6f} '
            'jitter_previous_x={px:.6f} jitter_previous_y={py:.6f} jittered={r} cut=0 '
            'cut_median_px={cm:.4f} cut_missing=0.0000 cut_samples={m} taa=1 taa_attempted={res} '
            'taa_resolved={res} taa_history={res} taa_skip={skip} taa_result={tr} taa_restore=00000000 '
            'taa_copy={tr} scene_open=0 active_queries=0 taa_references={ref}').format(
        f=frame, d=routed + sum(gates), r=routed, m=matched,
        g1=gates[0], g2=gates[1], g3=gates[2], g4=gates[3], g5=gates[4], g6=gates[5],
        ji=jitter_index, jx=jitter[0], jy=jitter[1], px=previous[0], py=previous[1],
        cm=cut_median, res=resolved, skip=taa_skip,
        tr='00000000' if resolved else '00000001', ref=references)


def build_log(directory, *, mode='defect', extra_lines=()):
    lines = ['x3-modern-renderer version=0.4 schema=2 capture_start=999999 capture_frames=4 pointer_bits=32',
             'telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 anchor=proxy_initialize cpu_only=1',
             'motion_output_mode requested=1 scope=live_same_draw_diagnostic temporal_consumer=1 taa=1 '
             'taa_debug=1 jitter=1 jitter_samples=8 cut_median_px=48.000 cut_missing=0.250',
             'motion_output_device device=1 enabled=1 reason=ok taa=1 taa_reason=ok taa_debug=1',
             'motion_output_taa device=1 initialize=00000000 references=1',
             'telemetry_presentation phase=create_after device=0 width=%d height=%d windowed=1 interval=1'
             % (W, H)]
    # One periodic, non-capture frame where the selector never reached the copy.
    lines.append(frame_line(0, routed=0, matched=0, taa_skip=2, gates=(0, 5, 0, 0, 0, 0),
                            references=0))
    qpc = 100000
    for position, number in enumerate(FRAMES):
        previous = JITTER[position - 1] if position else (0.0, 0.0)
        for index in range(4):
            lines.append(f'motion_route device=1 frame={number} index={index} gate=0 routed=1 matched=1 '
                         f'depth=1 jittered=1 vs=aaaa ps=bbbb result=00000000')
        for index in range(2):
            lines.append(f'motion_route device=1 frame={number} index={10 + index} gate=4 routed=0 '
                         f'matched=0 depth=1 jittered=1 vs=aaaa ps=bbbb result=00000000')
        lines.append(f'motion_output_cut device=1 frame={number} samples=4 median_px=0.0000 keyed=4 '
                     f'missing=0 missing_fraction=0.0000 bound_px=48.000 bound_missing=0.250 cut=0')
        lines.append(f'capture_event device=1 frame={number} seq=1 after_draw=6 op=color_fill '
                     f'result=00000000 qpc={qpc}')
        lines.append(f'capture_event device=1 frame={number} seq=2 after_draw=6 op=stretch_rect '
                     f'result=00000000 qpc={qpc + 2500}')
        qpc += 1000000
        for kind, name, suffix, size in (('color', 'color', 'bgra8', W * H * 4),
                                         ('taa', 'taa', 'rgba16f', W * H * 8),
                                         ('depth', 'depth', 'r32f', W * H * 4),
                                         ('', 'motion', 'rgba32f', W * H * 16)):
            event = f'motion_output_{kind}_readback' if kind else 'motion_output_readback'
            lines.append(f'{event} device=1 frame={number} file={name}_1_{number}.{suffix} width={W} '
                         f'height={H} format={suffix}_row_major result=00000000 bytes={size}')
        lines.append(frame_line(number, jitter_index=position, jitter=JITTER[position],
                                previous=previous))
        lines.append('telemetry_summary device=1 frame=%d reason=interval qpc=%d since_start_us=%d '
                     'interval_us=1000000.0 position_suppressed=0 cursor_changes_suppressed=0'
                     % (number, qpc, 1000000 * (position + 1)))
        lines.append('telemetry_metric device=1 name=frame_normal count=30 failures=0 '
                     'total_us=%d.000 min_us=%d.000 max_us=%d.000 bytes=0 buckets=0,0,0,0,30,0'
                     % (30000 + position * 300, 900 + position * 10, 1200 + position * 10))
        lines.append('telemetry_metric device=1 name=present_normal count=30 failures=0 total_us=300.000 '
                     'min_us=5.000 max_us=40.000 bytes=0 buckets=0,30,0,0,0,0')
    lines.extend(extra_lines)

    # Images: the pre-resolve colour is the jittered raster of a stationary scene.
    displacement = 0.0, 0.0
    for position, number in enumerate(FRAMES):
        jx, jy = JITTER[position]
        write_bgra8(directory / f'color_1_{number}.bgra8', shifted_image(jx, jy))
        previous = JITTER[position - 1] if position else (0.0, 0.0)
        if mode == 'defect':
            displacement = ((1 - WEIGHT) * jx + WEIGHT * (displacement[0] + jx - previous[0]),
                            (1 - WEIGHT) * jy + WEIGHT * (displacement[1] + jy - previous[1]))
        else:
            displacement = ((1 - WEIGHT) * jx + WEIGHT * displacement[0],
                            (1 - WEIGHT) * jy + WEIGHT * displacement[1])
        write_rgba16f(directory / f'taa_1_{number}.rgba16f', shifted_image(*displacement))
        write_motion(directory / f'motion_1_{number}.rgba32f')
    (directory / 'session.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return directory / 'session.log'


def analyse_images(directory, burst, **overrides):
    options = {'tile': TILE, 'tile_step': 16, 'max_samples': 8000, 'history_weight': WEIGHT}
    options.update(overrides)
    return tool.analyze_burst_images(burst, directory, '1', W, H, options)


class LogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_taa_status_jitter_and_bursts(self):
        log = build_log(self.dir)
        report = tool.analyze_log(log)
        taa = report['taa']
        self.assertEqual(taa['records'], len(FRAMES) + 1)
        self.assertEqual(taa['resolved'], len(FRAMES))
        self.assertEqual(taa['failed'], 0)
        self.assertEqual(taa['skip_histogram'], {'0': 4, '2': 1})
        self.assertTrue(taa['captured_frames_all_resolved'])
        self.assertEqual(taa['references_range'], [0, 12])
        captured = [e for e in taa['frames'] if e['captured']]
        self.assertEqual([e['frame'] for e in captured], FRAMES)
        for position, entry in enumerate(captured):
            self.assertAlmostEqual(entry['jitter_px'][0], JITTER[position][0], places=6)
            self.assertAlmostEqual(entry['jitter_px'][1], JITTER[position][1], places=6)
        self.assertEqual(len(report['bursts']), 1)
        burst = report['bursts'][0]
        self.assertEqual(burst['frames'], FRAMES)
        self.assertTrue(burst['stationary'])
        self.assertEqual(burst['cut_verdicts'], [0, 0, 0, 0])

    def test_route_counters_and_readbacks(self):
        report = tool.analyze_log(build_log(self.dir))
        route = report['route']
        self.assertEqual(route['per_draw_gate_histogram'], {'0': 16, '4': 8})
        self.assertEqual(route['captured']['routed'], 16)
        self.assertEqual(route['captured']['matched'], 16)
        self.assertTrue(route['counter_consistency_pass'])
        for kind in ('color', 'taa', 'depth', 'motion'):
            self.assertEqual(report['readbacks'][kind]['records'], len(FRAMES))
            self.assertTrue(report['readbacks'][kind]['all_zero_result'])
        self.assertEqual(report['diagnostics']['failure_records'], [])
        self.assertEqual(report['diagnostics']['nonzero_results'], [])
        self.assertEqual(report['diagnostics']['shutdown_records'], [])

    def test_counter_inconsistency_and_failures_are_reported(self):
        extra = ['motion_output_taa_failed device=1 frame=99 stage=resolve result=8876086c',
                 'motion_output_readback device=1 frame=99 file=motion_1_99.rgba32f width=%d height=%d '
                 'format=rgba32f_row_major result=8876086a bytes=%d' % (W, H, W * H * 16)]
        report = tool.analyze_log(build_log(self.dir, extra_lines=extra))
        self.assertTrue(any(r['event'] == 'motion_output_taa_failed'
                            for r in report['diagnostics']['failure_records']))
        self.assertEqual(len(report['diagnostics']['nonzero_results']), 1)

    def test_shutdown_and_reset_records(self):
        extra = ['motion_output_reset device=1 result=00000000 generation=2',
                 'motion_output_release device=1 references=0',
                 'device_destroy device=1 references=0']
        report = tool.analyze_log(build_log(self.dir, extra_lines=extra))
        self.assertEqual(len(report['diagnostics']['reset_records']), 1)
        self.assertEqual([r['event'] for r in report['diagnostics']['shutdown_records']],
                         ['motion_output_release', 'device_destroy'])

    def test_telemetry_windows_and_rejection(self):
        report = tool.analyze_log(build_log(self.dir))
        metrics = report['telemetry']['metrics']
        self.assertEqual(metrics['1:frame_normal']['count'], 30 * len(FRAMES))
        self.assertEqual(metrics['1:frame_normal']['buckets'], [0, 0, 0, 0, 30 * len(FRAMES), 0])
        windows = report['telemetry']['windows']['1:frame_normal']['all']
        self.assertEqual(windows['windows'], len(FRAMES))
        # per-window mean = total/count; the four totals are 30000..30900 over 30 frames.
        self.assertAlmostEqual(windows['window_mean_us']['min'], 1000.0, places=6)
        self.assertAlmostEqual(windows['window_mean_us']['max'], 1030.0, places=6)
        self.assertAlmostEqual(windows['window_min_us']['median'], 915.0, places=6)
        store = {}
        self.assertFalse(tool.telemetry_accumulate(store, dict(
            device='1', name='x', count='4', failures='0', total_us='10', min_us='1', max_us='4',
            bytes='0', buckets='1,1,1,0,0,0')))   # buckets sum to 3, not 4
        self.assertFalse(tool.telemetry_accumulate(store, dict(
            device='1', name='x', count='2', failures='0', total_us='1', min_us='3', max_us='2',
            bytes='0', buckets='2,0,0,0,0,0')))   # min > max
        self.assertEqual(store, {})

    def test_boundary_bracket(self):
        report = tool.analyze_log(build_log(self.dir))
        entry = tool.bracket_report(report['_brackets'], report['telemetry']['clock_hz'], 'note')
        self.assertEqual(entry['samples'], len(FRAMES))
        self.assertEqual(entry['preceding_events'], ['color_fill'])
        self.assertAlmostEqual(entry['median_us'], 250.0, places=6)


class SignalTests(unittest.TestCase):
    def test_fft_round_trip(self):
        size = 16
        values = [complex(math.sin(i * 0.37) + 0.5 * math.cos(i * 0.11), 0.0)
                  for i in range(size * size)]
        back = tool.ifft2(tool.fft2(list(values), size), size)
        for original, restored in zip(values, back):
            self.assertAlmostEqual(original.real, restored.real, places=9)
            self.assertAlmostEqual(restored.imag, 0.0, places=9)

    def test_lucas_kanade_sign_and_accuracy(self):
        first = array.array('f', shifted_image(0.0, 0.0))
        second = array.array('f', shifted_image(0.4, -0.25))
        result = tool.lucas_kanade_shift(first, second, W, H, max_samples=6000)
        self.assertAlmostEqual(result['dx_px'], 0.4, delta=0.02)
        self.assertAlmostEqual(result['dy_px'], -0.25, delta=0.02)
        self.assertGreater(result['used'], 1000)

    def test_phase_correlation_sign_and_magnitude(self):
        """An independent cross-check, not a precise estimator: on a 64-pixel tile
        it reproduces the sign and most of the magnitude, a few percent low."""
        first = array.array('f', shifted_image(0.0, 0.0))
        for dx, dy in ((3.0, 0.0), (0.0, -2.0), (0.5, 0.3333), (-0.25, -0.5556)):
            second = array.array('f', shifted_image(dx, dy))
            result = tool.phase_correlation_shift(first, second, W, H, (32, 32), TILE)
            self.assertAlmostEqual(result['dx_px'], dx, delta=0.16 + 0.12 * abs(dx))
            self.assertAlmostEqual(result['dy_px'], dy, delta=0.16 + 0.12 * abs(dy))
            if abs(dx) > 0.2:
                self.assertGreater(result['dx_px'] * dx, 0.0)
            if abs(dy) > 0.2:
                self.assertGreater(result['dy_px'] * dy, 0.0)

    def test_blur_reduces_gradient_and_high_frequency_energy(self):
        sharp = array.array('f', shifted_image(0.0, 0.0))
        soft = tool.binomial_blur(sharp, W, H, 3)
        self.assertLess(tool.gradient_energy(soft, W, H)['mean_squared_gradient'],
                        tool.gradient_energy(sharp, W, H)['mean_squared_gradient'])
        self.assertLess(tool.spectral_high_fraction(soft, W, (32, 32), TILE)['high_fraction'],
                        tool.spectral_high_fraction(sharp, W, (32, 32), TILE)['high_fraction'])

    def test_erode_and_tile_mask_fraction(self):
        mask = bytearray(W * H)
        for y in range(10, 20):
            for x in range(10, 20):
                mask[y * W + x] = 1
        eroded = tool.erode(mask, W, H, 2)
        self.assertEqual(sum(eroded), 6 * 6)
        self.assertAlmostEqual(tool.tile_mask_fraction(mask, W, (0, 0), TILE), 100 / (TILE * TILE))


class ImageVerdictTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def burst(self):
        report = tool.analyze_log(self.log)
        return report['bursts'][0]

    def test_defect_fixture_is_reported_as_tracking_the_jitter(self):
        self.log = build_log(self.dir, mode='defect')
        analysis = analyse_images(self.dir, self.burst())
        self.assertEqual(analysis['status'], 'evaluated')
        self.assertEqual(analysis['verdict']['pairs'], 3)
        self.assertEqual(analysis['verdict']['tracks_jitter'], 3)
        # The colour frames must reproduce the logged jitter step almost exactly:
        # that is the measurement's own control.
        self.assertLess(analysis['verdict']['colour_max_error_vs_logged_jitter_px'], 0.03)
        for pair in analysis['pairs']:
            model = pair['model']
            self.assertLess(model['residual_to_jitter_tracking_px'], 0.05)
            self.assertGreater(model['residual_to_stable_px'], 0.2)

    def test_correct_fixture_is_reported_as_stable(self):
        self.log = build_log(self.dir, mode='correct')
        analysis = analyse_images(self.dir, self.burst())
        self.assertEqual(analysis['verdict']['tracks_jitter'], 0)
        self.assertEqual(analysis['verdict']['stable'], 3)
        self.assertLess(analysis['verdict']['colour_max_error_vs_logged_jitter_px'], 0.03)
        for pair in analysis['pairs']:
            resolved = pair['resolved']['lucas_kanade']
            self.assertLess(math.hypot(resolved['dx_px'], resolved['dy_px']),
                            pair['model']['stable_resolve_bound_px'] + 0.02)

    def test_mask_restricts_the_samples_to_routed_pixels(self):
        self.log = build_log(self.dir, mode='defect')
        analysis = analyse_images(self.dir, self.burst())
        x0, x1, y0, y1 = VALID_RECT
        expected = (x1 - x0 - 4) * (y1 - y0 - 4)
        for pair in analysis['pairs']:
            self.assertEqual(pair['mask_pixels'], expected)
            self.assertLess(pair['resolved']['phase_correlation']['tile_masked_fraction'], 1.0001)

    def test_missing_readbacks_are_reported_not_raised(self):
        self.log = build_log(self.dir, mode='defect')
        (self.dir / f'taa_1_{FRAMES[2]}.rgba16f').unlink()
        (self.dir / f'taa_1_{FRAMES[3]}.rgba16f').unlink()
        analysis = analyse_images(self.dir, self.burst())
        self.assertEqual(analysis['status'], 'evaluated')
        self.assertEqual(analysis['frames'], FRAMES[:2])
        self.assertEqual([m['frame'] for m in analysis['missing']], FRAMES[2:])
        for path in (self.dir / f'taa_1_{FRAMES[0]}.rgba16f',
                     self.dir / f'taa_1_{FRAMES[1]}.rgba16f'):
            path.unlink()
        self.assertEqual(analyse_images(self.dir, self.burst())['status'], 'unavailable')


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_cli_writes_json_and_text(self):
        log = build_log(self.dir, mode='defect')
        output = self.dir / 'summary.json'
        text = self.dir / 'summary.txt'
        code = tool.main([str(log), '--output', str(output), '--text', str(text),
                          '--captures', str(self.dir), '--tile', str(TILE), '--tile-step', '16',
                          '--max-shift-samples', '8000'])
        self.assertEqual(code, 0)
        summary = json.loads(output.read_text())
        self.assertEqual(summary['source']['captured_frames'], FRAMES)
        self.assertEqual(summary['image_analysis']['status'], 'evaluated')
        self.assertEqual(summary['image_analysis']['bursts'][0]['analysis']['verdict']['tracks_jitter'], 3)
        self.assertIn('resolved', text.read_text())
        self.assertFalse(summary['boundary_stretchrect']['telemetry_metric_present'])

    def test_cli_without_images(self):
        log = build_log(self.dir, mode='defect')
        output = self.dir / 'summary.json'
        self.assertEqual(tool.main([str(log), '--output', str(output), '--no-images']), 0)
        self.assertEqual(json.loads(output.read_text())['image_analysis'], {'status': 'skipped'})


if __name__ == '__main__':
    unittest.main()
