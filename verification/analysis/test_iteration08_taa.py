"""Synthetic fixtures for tools/analysis/analyze_iteration08_taa.py.

No game data.  A small capture log is written with the exact line formats the
proxy emits (the record builder is reused from the iteration-7 fixture), and the
four readback images of one burst are constructed so that every quantity the
iteration-8 analyzer reports is known in closed form:

* four vertical bands give one pixel class each -
  ``sentinel`` (RT2 depth -1 in every frame), ``routed_interior`` (depth valid,
  constant within the tolerance, motion alpha 1), ``routed_edge`` by a depth
  spread beyond the tolerance, and ``routed_edge`` again by a motion alpha that
  changes across the burst;
* the pre-resolve colour of every pixel oscillates by a fixed amount every
  frame, so its temporal variance is known;
* the resolved image equals the current colour everywhere except in the
  interior band, where it equals the temporal mean - the ideal outcome.  The
  analyzer must therefore report a variance ratio near 1 for the sentinel and
  edge classes and near 0 for the interior class;
* one 1-pixel bright line and one 1-pixel yellow line are drawn, the second
  present in only part of the burst, so the thin-feature overlay, the
  guide-line probe and its coverage-flip fraction have exact expectations.
"""
import array
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
import analyze_iteration08_taa as tool  # noqa: E402
import test_iteration07_taa as fixture07  # noqa: E402

W, H = 96, 64
FRAMES = [40, 41, 42, 43]
OSCILLATION = 8          # 8-bit levels added on even frames, subtracted on odd
BANDS = {'sentinel': (0, 24), 'interior': (24, 48), 'edge_depth': (48, 72),
         'edge_alpha': (72, 96)}
THIN_X = 30              # 1-px bright line inside the interior band
YELLOW_X = 60            # 1-px yellow line inside the depth-edge band
YELLOW_ONLY_BELOW = 32   # above this row the yellow line is missing in the last frame


def band_of(x):
    for name, (low, high) in BANDS.items():
        if low <= x < high:
            return name
    raise AssertionError(x)


def base_level(x, y):
    """Smooth, low-gradient background: no 1-px ridge the top-hat could find."""
    return 70 + int(20.0 * math.sin(x * 0.21) + 10.0 * math.cos(y * 0.17))


def colour_pixel(x, y, position):
    """(B, G, R) of the pre-resolve 8-bit colour for frame `position`."""
    swing = OSCILLATION if position % 2 == 0 else -OSCILLATION
    if x == YELLOW_X and not (position == len(FRAMES) - 1 and y < YELLOW_ONLY_BELOW):
        return 30, 200 + swing, 220 + swing
    level = base_level(x, y) + swing
    if x == THIN_X:
        level += 90
    return level, level, level


def luma(bgr):
    blue, green, red = bgr
    r, g, b = tool.LUMA
    return (r * red + g * green + b * blue) / 255.0


def depth_value(x, position):
    """RT2 device depth: sentinel band negative, depth-edge band past the
    resolve's 1e-4 absolute tolerance between frames."""
    if band_of(x) == 'sentinel':
        return -1.0
    if band_of(x) == 'edge_depth':
        return 0.5 + (5e-4 if position % 2 else 0.0)
    return 0.5


def alpha_value(x, position):
    if band_of(x) == 'sentinel':
        return -1.0
    if band_of(x) == 'edge_alpha':
        return 1.0 if position % 2 == 0 else -1.0
    return 1.0


def write_colour(path, position):
    data = bytearray()
    for y in range(H):
        for x in range(W):
            blue, green, red = colour_pixel(x, y, position)
            data.extend((max(0, min(255, blue)), max(0, min(255, green)),
                         max(0, min(255, red)), 255))
    path.write_bytes(bytes(data))


def resolved_luma(x, y, position):
    """Current-only everywhere except the interior band, where the resolve is
    assumed to have converged onto the temporal mean."""
    if band_of(x) == 'interior':
        values = [luma(colour_pixel(x, y, p)) for p in range(len(FRAMES))]
        return sum(values) / len(values)
    return luma(colour_pixel(x, y, position))


def write_resolved(path, position):
    fixture07.write_rgba16f(path, [resolved_luma(x, y, position)
                                   for y in range(H) for x in range(W)])


def write_depth(path, position):
    out = array.array('f', [depth_value(x, position) for _ in range(H) for x in range(W)])
    if sys.byteorder != 'little':
        out.byteswap()
    path.write_bytes(out.tobytes())


def write_motion(path, position):
    out = array.array('f')
    for y in range(H):
        for x in range(W):
            out.extend((x / W, y / H, 0.5, alpha_value(x, position)))
    if sys.byteorder != 'little':
        out.byteswap()
    path.write_bytes(out.tobytes())


def build_capture(directory, *, frames=FRAMES, images=True, cut_median=0.0,
                  frame_total_us=30000, extra_lines=()):
    lines = ['x3-modern-renderer version=0.4 schema=2 capture_start=999999 capture_frames=4 '
             'pointer_bits=32',
             'telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 anchor=proxy_initialize '
             'cpu_only=1',
             'motion_output_mode requested=1 scope=live_same_draw_diagnostic temporal_consumer=1 '
             'taa=1 taa_debug=1 jitter=1 jitter_samples=8 cut_median_px=48.000 cut_missing=0.250',
             'motion_output_device device=1 enabled=1 reason=ok taa=1 taa_reason=ok taa_debug=1',
             'telemetry_presentation phase=create_after device=0 width=%d height=%d windowed=1 '
             'interval=1' % (W, H)]
    qpc = 100000
    for position, frame in enumerate(frames):
        jitter = fixture07.JITTER[position % len(fixture07.JITTER)]
        previous = fixture07.JITTER[(position - 1) % len(fixture07.JITTER)] if position else (0.0, 0.0)
        for index in range(4):
            lines.append(f'motion_route device=1 frame={frame} index={index} gate=0 routed=1 '
                         f'matched=1 depth=1 jittered=1 vs=aaaa ps=bbbb result=00000000')
        for index in range(2):
            lines.append(f'motion_route device=1 frame={frame} index={10 + index} gate=4 routed=0 '
                         f'matched=0 depth=1 jittered=1 vs=aaaa ps=bbbb result=00000000')
        lines.append(f'motion_output_cut device=1 frame={frame} samples=4 '
                     f'median_px={cut_median:.4f} keyed=4 missing=0 missing_fraction=0.0000 '
                     f'bound_px=48.000 bound_missing=0.250 cut=0')
        lines.append(f'capture_event device=1 frame={frame} seq=1 after_draw=6 op=color_fill '
                     f'result=00000000 qpc={qpc}')
        lines.append(f'capture_event device=1 frame={frame} seq=2 after_draw=6 op=stretch_rect '
                     f'result=00000000 qpc={qpc + 2500}')
        qpc += 1000000
        for kind, name, suffix, size in (('color', 'color', 'bgra8', W * H * 4),
                                         ('taa', 'taa', 'rgba16f', W * H * 8),
                                         ('depth', 'depth', 'r32f', W * H * 4),
                                         ('', 'motion', 'rgba32f', W * H * 16)):
            event = f'motion_output_{kind}_readback' if kind else 'motion_output_readback'
            lines.append(f'{event} device=1 frame={frame} file={name}_1_{frame}.{suffix} '
                         f'width={W} height={H} format={suffix}_row_major result=00000000 '
                         f'bytes={size}')
        lines.append(fixture07.frame_line(frame, jitter_index=position, jitter=jitter,
                                          previous=previous, cut_median=cut_median))
        lines.append('telemetry_summary device=1 frame=%d reason=interval qpc=%d '
                     'since_start_us=%d interval_us=1000000.0 position_suppressed=0 '
                     'cursor_changes_suppressed=0' % (frame, qpc, 1000000 * (position + 1)))
        lines.append('telemetry_metric device=1 name=frame_normal count=30 failures=0 '
                     'total_us=%d.000 min_us=%d.000 max_us=%d.000 bytes=0 buckets=0,0,0,0,30,0'
                     % (frame_total_us + position * 300, frame_total_us // 30 - 100,
                        frame_total_us // 30 + 200))
        if images:
            write_colour(directory / f'color_1_{frame}.bgra8', position)
            write_resolved(directory / f'taa_1_{frame}.rgba16f', position)
            write_depth(directory / f'depth_1_{frame}.r32f', position)
            write_motion(directory / f'motion_1_{frame}.rgba32f', position)
    lines.extend(extra_lines)
    log = directory / 'session.log'
    log.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return log


OPTIONS = {'depth_tolerance': tool.DEPTH_TOLERANCE, 'variance_floor': tool.VARIANCE_FLOOR,
           'tophat_min': 0.06, 'contrast_min': 0.10, 'yellow_minimum': 60, 'yellow_gap': 40,
           'yellow_criterion': 'test'}


class CaptureCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.log = build_capture(self.dir)

    def tearDown(self):
        self.temp.cleanup()

    def burst(self):
        return tool.analyze_log(self.log)['bursts'][0]

    def flicker(self, **overrides):
        options = dict(OPTIONS)
        options.update(overrides)
        return tool.analyze_burst_flicker(self.burst(), self.dir, '1', W, H, options)


class HelperTests(unittest.TestCase):
    def test_temporal_variance_matches_the_definition(self):
        images = [array.array('f', [0.1, 0.5, 1.0]), array.array('f', [0.3, 0.5, 0.0]),
                  array.array('f', [0.2, 0.5, 0.5])]
        result = tool.temporal_variance(images)
        for index in range(3):
            values = [image[index] for image in images]
            mean = sum(values) / 3
            self.assertAlmostEqual(result[index], sum((v - mean) ** 2 for v in values) / 3,
                                   places=6)

    def test_separable_extreme_is_a_3x3_min_and_max(self):
        image = array.array('f', [0.0] * 25)
        image[12] = 1.0
        dilated = tool.separable_extreme(image, 5, 5, True)
        self.assertEqual([i for i, v in enumerate(dilated) if v > 0],
                         [6, 7, 8, 11, 12, 13, 16, 17, 18])
        eroded = tool.separable_extreme(dilated, 5, 5, False)
        self.assertEqual([i for i, v in enumerate(eroded) if v > 0], [12])

    def test_thin_feature_mask_finds_a_one_pixel_line_and_not_a_step_edge(self):
        width, height = 24, 12
        image = array.array('f', [0.0] * (width * height))
        for y in range(height):
            image[y * width + 5] = 0.8              # 1-px line: thin
            for x in range(12, width):
                image[y * width + x] = 0.8          # step edge: not thin
        mask = tool.thin_feature_mask(image, width, height, 0.06, 0.10)
        self.assertEqual(sorted({i % width for i, v in enumerate(mask) if v}), [5])

    def test_classification_of_the_four_bands(self):
        depths = [array.array('f', [depth_value(x, p) for x in range(W)])
                  for p in range(len(FRAMES))]
        alphas = [array.array('f', [alpha_value(x, p) for x in range(W)])
                  for p in range(len(FRAMES))]
        classes = tool.classify_pixels(depths, alphas)
        self.assertEqual([tool.CLASSES[classes[x]] for x in (5, 30, 60, 80)],
                         ['sentinel', 'routed_interior', 'routed_edge', 'routed_edge'])
        # A depth spread inside the tolerance stays interior.
        near = [array.array('f', [0.5 + (5e-5 if p else 0.0)]) for p in range(2)]
        self.assertEqual(tool.classify_pixels(near, [array.array('f', [1.0])] * 2)[0],
                         tool.INTERIOR)

    def test_yellow_mask_requires_a_blue_deficit(self):
        directory = Path(tempfile.mkdtemp())
        path = directory / 'c.bgra8'
        path.write_bytes(bytes([30, 200, 220, 255] + [200, 200, 200, 255]))
        mask = tool.yellow_mask_bgra8(path, 2, 1)
        self.assertEqual(list(mask), [1, 0])


class FlickerTests(CaptureCase):
    def test_class_pixel_counts(self):
        report = self.flicker()
        self.assertEqual(report['status'], 'evaluated')
        classes = report['classes']
        self.assertEqual(classes['sentinel']['pixels'], 24 * H)
        self.assertEqual(classes['routed_interior']['pixels'], 24 * H)
        self.assertEqual(classes['routed_edge']['pixels'], 48 * H)
        self.assertEqual(sum(classes[name]['pixels'] for name in tool.CLASSES), W * H)

    def test_variance_ratio_separates_the_classes(self):
        classes = self.flicker()['classes']
        for name in ('sentinel', 'routed_edge'):
            entry = classes[name]
            self.assertAlmostEqual(entry['aggregate_ratio'], 1.0, delta=0.05)
            self.assertAlmostEqual(entry['ratio_distribution']['p50'], 1.0, delta=0.05)
            self.assertGreater(entry['raw_variance']['rms_levels'], 4.0)
        interior = classes['routed_interior']
        self.assertLess(interior['aggregate_ratio'], 0.02)
        self.assertLess(interior['ratio_distribution']['p90'], 0.02)
        self.assertLess(interior['flicker_energy_share'], 0.02)
        self.assertAlmostEqual(sum(classes[name]['flicker_energy_share'] for name in tool.CLASSES),
                               1.0, places=5)
        # The raw oscillation is +-8 levels, so the raw temporal variance is 64
        # levels^2 wherever the colour only oscillates.
        self.assertAlmostEqual(classes['sentinel']['raw_variance']['rms_levels'], 8.0, delta=0.3)
        self.assertAlmostEqual(classes['sentinel']['resolved_variance']['rms_levels'], 8.0,
                               delta=0.4)
        self.assertEqual(classes['routed_interior']['visible_flicker_pixels']['resolved'], 0)
        self.assertGreater(classes['sentinel']['visible_flicker_pixels']['resolved'], 1000)

    def test_thin_feature_overlay_is_not_a_partition(self):
        thin = self.flicker()['classes']['thin_feature']
        self.assertTrue(thin['overlay'])
        # Both drawn lines, and nothing from the smooth background.
        self.assertEqual(thin['pixels'], 2 * H)
        self.assertEqual(thin['by_class']['routed_interior']['pixels'], H)   # the bright line
        self.assertEqual(thin['by_class']['routed_edge']['pixels'], H)       # the yellow line
        self.assertEqual(thin['by_class']['sentinel']['pixels'], 0)
        self.assertAlmostEqual(thin['by_class']['routed_edge']['aggregate_ratio'], 1.0, delta=0.1)

    def test_yellow_guide_lines(self):
        yellow = self.flicker()['yellow_lines']
        self.assertEqual(yellow['status'], 'present')
        self.assertEqual(yellow['union_pixels'], H)
        # The line is missing above row 32 in the last frame only.
        self.assertEqual(yellow['stable_pixels'], H - YELLOW_ONLY_BELOW)
        self.assertAlmostEqual(yellow['coverage_flip_fraction'], 0.5, places=6)
        self.assertEqual(yellow['class_counts'], {'routed_edge': H})
        self.assertEqual(yellow['pixels_with_rt2_depth_in_all_frames'], H)
        self.assertEqual(yellow['bounding_box']['x'], [YELLOW_X, YELLOW_X])
        self.assertAlmostEqual(yellow['union']['aggregate_ratio'], 1.0, delta=0.1)
        self.assertEqual(yellow['per_frame_pixels'], {str(FRAMES[0]): H, str(FRAMES[1]): H,
                                                      str(FRAMES[2]): H,
                                                      str(FRAMES[3]): H - YELLOW_ONLY_BELOW})

    def test_depth_tolerance_reclassifies_the_depth_edge_band(self):
        # With a tolerance above the band's 5e-4 spread it becomes interior.
        classes = self.flicker(depth_tolerance=1e-3)['classes']
        self.assertEqual(classes['routed_interior']['pixels'], 48 * H)
        self.assertEqual(classes['routed_edge']['pixels'], 24 * H)

    def test_missing_readbacks_are_reported_not_raised(self):
        (self.dir / f'depth_1_{FRAMES[2]}.r32f').unlink()
        report = self.flicker()
        self.assertEqual(report['status'], 'evaluated')
        self.assertEqual(report['frames'], [FRAMES[0], FRAMES[1], FRAMES[3]])
        self.assertEqual([m['frame'] for m in report['missing']], [FRAMES[2]])
        for frame in FRAMES:
            path = self.dir / f'depth_1_{frame}.r32f'
            if path.is_file():
                path.unlink()
        self.assertEqual(self.flicker()['status'], 'unavailable')


class SelectionAndTimingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_only_stationary_bursts_are_selected_by_default(self):
        moving = build_capture(self.dir, frames=[70, 71], images=False, cut_median=9.4)
        bursts = tool.analyze_log(moving)['bursts']
        self.assertEqual(tool.select_bursts(bursts, None), [])
        self.assertEqual(len(tool.select_bursts(bursts, [70])), 1)
        still = build_capture(self.dir, images=False)
        self.assertEqual(len(tool.select_bursts(tool.analyze_log(still)['bursts'], None)), 1)

    def test_run_b_window_comparison(self):
        run_a = tool.analyze_log(build_capture(self.dir, images=False, frame_total_us=60000))
        other = Path(tempfile.mkdtemp())
        run_b = tool.scan_telemetry(build_capture(other, images=False, frame_total_us=30000))
        comparison = tool.compare_window_timings(run_a['telemetry']['windows'], run_b['windows'],
                                                 'run_a', 'run_b')
        key = '1:frame_normal/scene'
        self.assertIn(key, comparison)
        row = comparison[key]
        self.assertAlmostEqual(row['mean_ratio'], 2.0, delta=0.05)
        self.assertGreater(row['min_ratio'], 1.0)
        self.assertEqual(row['run_a']['windows'], len(FRAMES))


class RegimeTests(unittest.TestCase):
    """A session that visits a light and a heavy part of the scene is bimodal;
    the median of its windows then reports occupancy, not frame cost."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def bimodal_log(self, path, fast_windows, slow_windows):
        lines = ['telemetry_start schema=1 qpc_frequency=10000000 qpc=1000 anchor=x cpu_only=1',
                 'motion_output_frame device=1 frame=1 routed=4 matched=4 draws=4 taa=1 '
                 'taa_attempted=1 taa_resolved=1 taa_skip=0 jitter=0']
        frame = 10
        # 30 frames per window at 30 ms and at 60 ms: the two regimes of the
        # real sessions, on either side of the 45 ms default split.
        for count, per_frame in ((fast_windows, 30000), (slow_windows, 60000)):
            for _ in range(count):
                lines.append('telemetry_summary device=1 frame=%d reason=interval qpc=0 '
                             'since_start_us=0 interval_us=1000000.0' % frame)
                lines.append('telemetry_metric device=1 name=frame_normal count=30 failures=0 '
                             'total_us=%d.000 min_us=%d.000 max_us=%d.000 bytes=0 '
                             'buckets=0,0,0,0,30,0' % (30 * per_frame, per_frame - 1000,
                                                       per_frame + 1000))
                frame += 60
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return path

    def test_mode_split_separates_the_two_regimes(self):
        log = self.bimodal_log(self.dir / 'a.log', 10, 30)
        windows = tool.stream_scene_windows(log)['1:frame_normal']
        self.assertEqual(len(windows), 40)
        self.assertTrue(all(w['regime'] == 'scene' for w in windows))
        split = tool.mode_split(windows, 45000.0)
        self.assertEqual(split['fast']['windows'], 10)
        self.assertEqual(split['slow']['windows'], 30)
        self.assertAlmostEqual(split['fast']['window_mean_us']['median'], 30000.0, places=6)
        self.assertAlmostEqual(split['slow']['window_mean_us']['median'], 60000.0, places=6)
        self.assertAlmostEqual(split['fast_fraction'], 0.25, places=6)

    def test_within_regime_ratio_is_one_when_only_occupancy_differs(self):
        left = tool.stream_scene_windows(self.bimodal_log(self.dir / 'a.log', 10, 30))
        right = tool.stream_scene_windows(self.bimodal_log(self.dir / 'b.log', 30, 10))
        rows = tool.compare_mode_splits(left, right, 'run_a', 'run_b', 45000.0)
        row = rows['1:frame_normal']
        # The pooled medians differ by 2x, the within-regime medians do not.
        self.assertAlmostEqual(row['fast_mean_ratio'], 1.0, places=6)
        self.assertAlmostEqual(row['slow_mean_ratio'], 1.0, places=6)
        self.assertEqual(row['run_a']['fast']['windows'], 10)
        self.assertEqual(row['run_b']['fast']['windows'], 30)
        self.assertEqual(sorted(int(k) for k in row['run_a']['histogram_10ms_bins']), [30, 60])


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_cli_writes_json_and_text(self):
        log = build_capture(self.dir)
        output = self.dir / 'summary.json'
        text = self.dir / 'summary.txt'
        code = tool.main([str(log), '--output', str(output), '--text', str(text),
                          '--captures', str(self.dir), '--no-images'])
        self.assertEqual(code, 0)
        summary = json.loads(output.read_text())
        self.assertEqual(summary['source']['captured_frames'], FRAMES)
        self.assertEqual(summary['image_analysis'], {'status': 'skipped'})
        self.assertEqual(summary['timing_comparison']['status'], 'pending')
        flicker = summary['flicker']
        self.assertEqual(flicker['status'], 'evaluated')
        classes = flicker['bursts'][0]['analysis']['classes']
        self.assertEqual(classes['sentinel']['pixels'], 24 * H)
        self.assertLess(classes['routed_interior']['aggregate_ratio'], 0.02)
        body = text.read_text()
        self.assertIn('flicker burst', body)
        self.assertIn('yellow guide lines', body)

    def test_cli_run_b_populates_the_timing_comparison(self):
        log = build_capture(self.dir, images=False, frame_total_us=60000)
        other = Path(tempfile.mkdtemp())
        run_b = build_capture(other, images=False, frame_total_us=30000)
        output = self.dir / 'summary.json'
        self.assertEqual(tool.main([str(log), '--output', str(output), '--no-images',
                                    '--no-flicker', '--run-b-log', str(run_b),
                                    '--run-b-label', 'taa_off']), 0)
        summary = json.loads(output.read_text())
        comparison = summary['timing_comparison']
        self.assertEqual(comparison['status'], 'evaluated')
        self.assertAlmostEqual(comparison['windows']['1:frame_normal/scene']['mean_ratio'], 2.0,
                               delta=0.05)
        self.assertEqual(summary['flicker'], {'status': 'skipped'})
        # The configuration/route profile of both sessions travels with the ratio.
        for side in ('primary', 'secondary'):
            profile = comparison[side]['profile']
            self.assertEqual(profile['mode']['taa'], '1')
            self.assertEqual(profile['route_totals']['routed'], 16)
            self.assertEqual(profile['captured_frames'], FRAMES)
        self.assertEqual(comparison['secondary']['label'], 'taa_off')
        self.assertAlmostEqual(comparison['metrics']['1:frame_normal']['mean_ratio'], 2.0,
                               delta=0.05)


if __name__ == '__main__':
    unittest.main()
