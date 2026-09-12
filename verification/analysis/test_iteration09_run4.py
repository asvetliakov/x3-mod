"""Synthetic checks for `tools/analysis/analyze_iteration09_run4.py`; no game content.

Every input is constructed so the expected output is known in closed form or by
construction, so the run-4 report's helpers are pinned independently of the
session it was written for:

* the write-back's 8-bit conversion table, against exact rational arithmetic
  over *every* binary16 bit pattern (both round-to-nearest tie rules), so the
  identity section cannot silently use a wrong quantizer,
* the per-frame identity comparison, on an image with injected differences of
  known size, channel and pixel class, including that the B/G/R/A byte order of
  the 8-bit readback is paired with the R/G/B/A order of the FP16 readback,
* the 4-connected component grouping (a diagonal contact must not join two
  components) and the headroom measurement on an image whose over-1 pixels,
  octaves, classes and bounding boxes are placed by hand,
* the streaming log reader and the HDR health report: field distributions, the
  anomaly list with its `motion_output_frame` context, and every verdict flag
  including the ones an `hdr_unwind` or `hdr_recheck` record must clear,
* the TAA and cost sections (the ordinary/capture split of the per-frame
  timings and the cumulative metric folding),
* the focus/cursor episode grouping of change-only records.
"""
import array
import json
import math
import sys
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import analyze_iteration07_taa as it07  # noqa: E402
import analyze_iteration08_taa as it08  # noqa: E402
import analyze_iteration09_run4 as run4  # noqa: E402

# One bit pattern per exactly representable value the tests use.
BITS = {}
for _bits, _value in enumerate(it07.HALF_TABLE):
    if _value == _value and _value not in BITS:
        BITS[_value] = _bits

HDR_FRAME = (
    'hdr_frame device=1 frame={frame} hdr=1 redirected={redirected} end={end} '
    'writebacks={writebacks} flushes={flushes} writeback_source={source} '
    'unwind={unwind} unwind_reason={reason} blocked=0 recheck=none suspended={suspended} '
    'resumed={resumed} dirty_at_present={dirty} refused_msaa=0 '
    'target_create=00000000 latch_bind=00000000 target=1280x768 target_bytes=7864320 '
    'caps=ok stretch_conversion=00000000 timing=cpu_qpc redirect_us={redirect} '
    'writeback_us={writeback} writeback_draw_us={writeback} writeback_stretch_us=0.0 '
    'bind_us=0.0 recheck_us=0.0 tonemap=identity tonemapped=0 fallback=0'
)
MOTION_FRAME = (
    'motion_output_frame device=1 frame={frame} latched={latched} draws={draws} '
    'routed={routed} matched={routed} selector_state={state} taa=1 '
    'taa_attempted={att} taa_resolved={att} taa_history={history} taa_skip={skip} '
    'camera_policy={policy} camera_reason={reason} camera_cut=0 '
    'camera_rotation_deg=0.5000 scene_end_source={source} scene_end_check={check} '
    'hook_signals={signals} hook_outside_scene=0 hook_state=0 draws_after_hook=0 '
    'bloom_copy_seen=0 restore_failures=0 apply_failures=0 cut=0'
)


def hdr_frame(frame, redirected=1, end='hook', writebacks=1, flushes=0, source='shader',
              unwind=0, reason='none', suspended=0, resumed=0, dirty=0, redirect=6.0,
              writeback=70.0):
    return HDR_FRAME.format(frame=frame, redirected=redirected, end=end,
                            writebacks=writebacks, flushes=flushes, source=source,
                            unwind=unwind, reason=reason, suspended=suspended,
                            resumed=resumed, dirty=dirty, redirect=redirect,
                            writeback=writeback)


def motion_frame(frame, latched=1, draws=200, routed=150, state=8, att=1, history=1,
                 skip=0, policy=2, reason=0, source='hook', check=1, signals=1):
    return MOTION_FRAME.format(frame=frame, latched=latched, draws=draws, routed=routed,
                               state=state, att=att, history=history, skip=skip,
                               policy=policy, reason=reason, source=source, check=check,
                               signals=signals)


def write_log(directory, lines):
    path = Path(directory) / 'session.log'
    path.write_text('\n'.join(lines) + '\n')
    return path


def pack_halves(pixels):
    """(r, g, b, a) tuples of exactly representable values -> the readback bytes."""
    out = array.array('H')
    for r, g, b, a in pixels:
        out.extend((BITS[r], BITS[g], BITS[b], BITS[a]))
    if sys.byteorder != 'little':
        out.byteswap()
    return out


def pack_bgra(pixels):
    data = bytearray()
    for b, g, r, a in pixels:
        data.extend((b, g, r, a))
    return bytes(data)


class Conversion(unittest.TestCase):
    """The 8-bit conversion the write-back draw ends with, against exact arithmetic."""

    def test_named_values(self):
        for value, expected in ((0.0, 0), (1.0, 255), (2.0, 255), (65504.0, 255),
                                (-1.0, 0), (-0.0, 0), (0.5, 128), (0.25, 64),
                                (float('nan'), 0), (math.inf, 255), (-math.inf, 0)):
            self.assertEqual(run4.unorm8(value), expected, value)

    def test_table_against_exact_rounding(self):
        """Both tables, on every bit pattern, against Fraction arithmetic.

        A binary16 value is an exact dyadic rational, so `v * 255` is exact and
        the two tie rules are decidable without any floating point.
        """
        up, even = run4.writeback_table('half_up'), run4.writeback_table('half_even')
        self.assertEqual(len(up), 65536)
        ties = 0
        for bits in range(65536):
            value = it07.HALF_TABLE[bits]
            if value != value:
                self.assertEqual((up[bits], even[bits]), (0, 0))
                continue
            if value <= 0:
                self.assertEqual((up[bits], even[bits]), (0, 0))
                continue
            if value >= 1:
                self.assertEqual((up[bits], even[bits]), (255, 255))
                continue
            scaled = Fraction(value) * 255
            low = scaled.numerator // scaled.denominator
            remainder = scaled - low
            if remainder > Fraction(1, 2):
                expected_up = expected_even = low + 1
            elif remainder < Fraction(1, 2):
                expected_up = expected_even = low
            else:
                ties += 1
                self.assertEqual(value, 0.5)
                expected_up = low + 1
                expected_even = low + (1 if low % 2 else 0)
            self.assertEqual(up[bits], expected_up, value)
            self.assertEqual(even[bits], expected_even, value)
        # Exactly one binary16 value in (0, 1) is a rounding tie: v*255 is a half
        # integer only when v = odd/2, and 0.5 is the only such value below 1.
        self.assertEqual(ties, 1)

    def test_the_tie_rule_cannot_matter_for_binary16(self):
        """The two tables are identical, so the identity comparison does not
        depend on which round-to-nearest rule the backend uses: the only tie is
        0.5 (127.5 -> 128 under both rules, since 127 is odd)."""
        up, even = run4.writeback_table('half_up'), run4.writeback_table('half_even')
        self.assertEqual(up, even)
        self.assertEqual(run4.unorm8(0.5, 'half_up'), 128)
        self.assertEqual(run4.unorm8(0.5, 'half_even'), 128)
        # The rules do differ in general, which is why both are implemented.
        self.assertEqual(run4.unorm8(2.5 / 255.0, 'half_up'), 3)
        self.assertEqual(run4.unorm8(2.5 / 255.0, 'half_even'), 2)


class Identity(unittest.TestCase):
    """`compare_frame` on an image with differences placed by hand."""

    def setUp(self):
        self.tables = (run4.writeback_table('half_up'), run4.writeback_table('half_even'))
        # Eight pixels, all exactly representable and none on a rounding tie.
        self.values = [(0.25, 0.5, 0.75, 1.0), (0.0, 0.0, 0.0, 1.0),
                       (1.0, 1.0, 1.0, 0.5), (2.0, 0.25, 0.5, 1.0),
                       (0.5, 0.5, 0.5, 1.0), (0.75, 0.25, 0.0, 0.25),
                       (0.125, 0.25, 0.375, 1.0), (0.5, 0.25, 0.125, 1.0)]
        self.halves = pack_halves(self.values)
        up = self.tables[0]
        self.colour = bytearray()
        for r, g, b, a in self.values:
            self.colour.extend((up[BITS[b]], up[BITS[g]], up[BITS[r]], up[BITS[a]]))
        self.classes = bytearray([it08.SENTINEL, it08.SENTINEL, it08.INTERIOR,
                                  it08.INTERIOR, it08.INTERIOR, it08.EDGE,
                                  it08.EDGE, it08.EDGE])

    def test_exact_image(self):
        result = run4.compare_frame(self.halves, bytes(self.colour), self.classes,
                                    self.tables)
        self.assertEqual(result['pixels'], 8)
        self.assertEqual(result['exact_half_up'], 8)
        self.assertEqual(result['exact_fraction_half_up'], 1.0)
        self.assertEqual(result['max_difference'], 0)
        self.assertEqual(result['difference_histogram'], {'0': 8})
        self.assertEqual(result['channel_differences'], {'b': 0, 'g': 0, 'r': 0, 'a': 0})
        self.assertEqual(result['classes']['sentinel'],
                         {'pixels': 2, 'exact_half_up': 2, 'max_difference': 0,
                          'differing': 0})
        self.assertEqual(result['classes']['routed_interior']['pixels'], 3)
        self.assertEqual(result['classes']['routed_edge']['pixels'], 3)
        # No value used here is a tie, so the two rules agree on every pixel.
        self.assertEqual(result['tie_candidates'], 0)
        self.assertEqual(result['exact_half_even'], 8)

    def test_injected_differences(self):
        colour = bytearray(self.colour)
        colour[0 * 4 + 2] += 1        # pixel 0 (sentinel), red, +1 code
        colour[3 * 4 + 1] -= 2        # pixel 3 (interior), green, -2 codes
        colour[5 * 4 + 3] += 1        # pixel 5 (edge), alpha 0.25 -> 64, +1 code
        result = run4.compare_frame(self.halves, bytes(colour), self.classes, self.tables)
        self.assertEqual(result['exact_half_up'], 5)
        self.assertEqual(result['max_difference'], 2)
        self.assertEqual(result['difference_histogram'], {'0': 5, '1': 2, '2': 1})
        self.assertEqual(result['channel_differences'], {'b': 0, 'g': 1, 'r': 1, 'a': 1})
        self.assertEqual(result['channel_max_difference'],
                         {'b': 0, 'g': 2, 'r': 1, 'a': 1})
        self.assertEqual(result['classes']['sentinel']['differing'], 1)
        self.assertEqual(result['classes']['routed_interior']['max_difference'], 2)
        self.assertEqual(result['classes']['routed_edge']['differing'], 1)

    def test_channel_order_is_not_symmetric(self):
        """Swapping the 8-bit red and blue bytes must be detected: the readbacks
        are B,G,R,A against R,G,B,A and a symmetric comparison would miss it."""
        colour = bytearray(self.colour)
        for index in range(len(self.values)):
            base = index * 4
            colour[base], colour[base + 2] = colour[base + 2], colour[base]
        result = run4.compare_frame(self.halves, bytes(colour), self.classes, self.tables)
        # Pixels whose red and blue codes differ are now wrong.
        expected = sum(1 for r, g, b, a in self.values
                       if run4.unorm8(r) != run4.unorm8(b))
        self.assertEqual(result['pixels'] - result['exact_half_up'], expected)
        self.assertGreater(expected, 0)


class Components(unittest.TestCase):
    def test_four_connectivity(self):
        width = 5
        # Row 0: x=0,1 ; row 1: x=1 (joins) ; row 2: x=3 (diagonal to nothing here)
        indices = [0, 1, width + 1, 2 * width + 3]
        groups = sorted(run4.components_of(indices, width), key=len, reverse=True)
        self.assertEqual([len(g) for g in groups], [3, 1])

    def test_diagonal_does_not_join(self):
        width = 4
        indices = [0, width + 1]
        self.assertEqual(len(run4.components_of(indices, width)), 2)

    def test_row_wrap_does_not_join(self):
        """The last pixel of a row and the first of the next are neighbours in
        the flat array but not in the image."""
        width = 4
        indices = [3, 4]
        self.assertEqual(len(run4.components_of(indices, width)), 2)


class Headroom(unittest.TestCase):
    def test_known_image(self):
        width, height = 4, 3
        background = (0.5, 0.25, 0.125, 1.0)
        pixels = [background] * (width * height)
        # One two-pixel component at (0,0)-(1,0) peaking at 1.5 (octave 0) and one
        # single-pixel component at (3,2) peaking at 4.0 (octave 2).
        pixels[0] = (1.5, 0.5, 0.25, 1.0)
        pixels[1] = (0.25, 1.25, 0.5, 1.0)
        pixels[11] = (0.5, 0.5, 4.0, 0.5)
        # One pixel exactly at 1.0 must not count as over-1.
        pixels[5] = (1.0, 0.25, 0.25, 1.0)
        # One negative channel (a sentinel leak would look like this).
        pixels[6] = (-0.5, 0.25, 0.25, 0.0)
        halves = pack_halves(pixels)
        classes = bytearray([it08.INTERIOR] * (width * height))
        classes[0] = it08.EDGE
        classes[11] = it08.SENTINEL
        result = run4.headroom_frame(halves, classes, width, components=4)
        self.assertEqual(result['pixels'], width * height)
        self.assertEqual(result['over_one'], 3)
        self.assertEqual(result['over_one_fraction'], 3 / 12)
        self.assertEqual(result['max'], 4.0)
        self.assertEqual(result['octave_histogram'], {'0': 2, '2': 1})
        self.assertEqual(result['negative_channel'], 1)
        self.assertEqual(result['alpha'], {'one': 10, 'zero': 1, 'other': 1})
        self.assertEqual(result['peak_histogram']['1.0_exact'], 1)
        self.assertEqual(result['components'], 2)
        largest = result['largest_components'][0]
        self.assertEqual(largest['pixels'], 2)
        self.assertEqual((largest['x'], largest['y']), ([0, 1], [0, 0]))
        self.assertEqual(largest['max'], 1.5)
        self.assertEqual(largest['classes'], {'routed_edge': 1, 'routed_interior': 1})
        smallest = result['largest_components'][1]
        self.assertEqual((smallest['pixels'], smallest['max']), (1, 4.0))
        self.assertEqual(smallest['classes'], {'sentinel': 1})
        self.assertEqual(result['classes']['sentinel'],
                         {'pixels': 1, 'over_one': 1, 'max': 4.0, 'over_one_fraction': 1.0})
        self.assertEqual(result['classes']['routed_edge']['over_one'], 1)
        self.assertEqual(result['classes']['routed_interior']['over_one'], 1)

    def test_clamped_image_has_no_headroom(self):
        pixels = [(1.0, 1.0, 1.0, 1.0)] * 4
        result = run4.headroom_frame(pack_halves(pixels),
                                     bytearray([it08.INTERIOR] * 4), 2)
        self.assertEqual(result['over_one'], 0)
        self.assertEqual(result['max'], 1.0)
        self.assertEqual(result['largest_components'], [])


class LogReport(unittest.TestCase):
    def scan(self, lines):
        with tempfile.TemporaryDirectory() as directory:
            return run4.scan_log(write_log(directory, lines))

    def healthy_lines(self):
        return [
            'hdr_device device=1 enabled=1 reason=ok main_format=21 self_test_targets=3 '
            'stretch_conversion=00000000',
            'hdr_tonemap device=1 enabled=1 requested=identity tonemap=0',
            'hdr_target device=1 frame=0 width=1280 height=768 '
            'format=A16B16G16R16F bytes=7864320 create=00000000 meter=0',
            'telemetry_first_present device=1 frame=0 reset_count=0',
            hdr_frame(60), motion_frame(60),
            hdr_frame(120), motion_frame(120),
            hdr_frame(180, redirected=0, end='none', writebacks=0, source='none',
                      redirect=0.0, writeback=0.0),
            motion_frame(180, latched=0, state=9, att=0, history=0, skip=2, policy=1,
                         reason=1, source='none', check=0),
            hdr_frame(240, end='present', writebacks=2, flushes=2, redirect=8.5,
                      writeback=279.0),
            motion_frame(240, draws=0, routed=0, state=9, att=0, history=0, skip=2,
                         policy=1, reason=1, source='none', check=0, signals=0),
            'hdr_readback device=1 frame=120 file=hdr_1_120.rgba16f width=1280 '
            'height=768 format=rgba16f_row_major result=00000000 bytes=7864320',
        ]

    def test_distributions_and_anomalies(self):
        scan = self.scan(self.healthy_lines())
        self.assertEqual(len(scan['hdr_frames']), 4)
        report = run4.hdr_report(scan, {120})
        self.assertEqual(report['lines'], 4)
        self.assertEqual(report['latched'], 3)
        self.assertEqual(report['distributions']['end'],
                         {'hook': 2, 'none': 1, 'present': 1})
        self.assertEqual(report['distributions']['writeback_source'],
                         {'shader': 3, 'none': 1})
        self.assertEqual(report['distributions']['writebacks'], {'1': 2, '0': 1, '2': 1})
        self.assertEqual(report['distributions']['dirty_at_present'], {'0': 4})
        self.assertTrue(report['capture_frames_all_latched'])
        self.assertEqual([a['frame'] for a in report['anomalies']], [180, 240])
        present = report['anomalies'][1]
        self.assertEqual(present['end'], 'present')
        self.assertEqual(present['writebacks'], 2)
        self.assertEqual(present['flushes'], 2)
        self.assertEqual(present['dirty_at_present'], 0)
        self.assertEqual(present['context']['draws'], '0')
        self.assertEqual(present['context']['selector_state_name'], 'rejected')
        self.assertEqual(present['context']['scene_end_source'], 'none')
        self.assertEqual(present['context']['hook_signals'], '0')
        self.assertEqual(present['context']['taa_skip'], 'not_reached')
        verdict = report['verdict']
        self.assertTrue(verdict['every_latched_frame_wrote_back_with_the_shader'])
        self.assertTrue(verdict['no_unwind'])
        self.assertTrue(verdict['no_recheck'])
        self.assertTrue(verdict['no_content_pending_at_present'])
        self.assertTrue(verdict['no_suspend'])
        self.assertEqual(verdict['reset_count'], 0)

    def test_unwind_and_recheck_clear_the_verdict(self):
        lines = self.healthy_lines() + [
            hdr_frame(300, unwind=1, reason='draw', source='stretch'),
            motion_frame(300),
            'hdr_unwind=draw device=1 frame=300 source=stretch draw=88760868 '
            'restore=00000000 stretch=00000000 bind=00000000 write=1 final=main',
            'hdr_recheck device=1 frame=360 passed=0 stage=compare',
        ]
        report = run4.hdr_report(self.scan(lines), set())
        self.assertEqual(report['unwind_frames'], [300])
        self.assertEqual(len(report['unwind_records']), 1)
        self.assertEqual(report['unwind_records'][0]['device'], '1')
        self.assertEqual(len(report['recheck_records']), 1)
        self.assertFalse(report['verdict']['no_unwind'])
        self.assertFalse(report['verdict']['no_recheck'])
        self.assertFalse(report['verdict']['every_latched_frame_wrote_back_with_the_shader'])

    def test_dirty_at_present_is_reported(self):
        lines = self.healthy_lines() + [hdr_frame(300, end='present', dirty=1),
                                        motion_frame(300)]
        report = run4.hdr_report(self.scan(lines), set())
        self.assertFalse(report['verdict']['no_content_pending_at_present'])
        self.assertEqual(report['distributions']['dirty_at_present'], {'0': 4, '1': 1})

    def test_suspend_and_resume(self):
        lines = self.healthy_lines() + [hdr_frame(300, suspended=1, resumed=1, writebacks=2),
                                        motion_frame(300)]
        report = run4.hdr_report(self.scan(lines), set())
        self.assertFalse(report['verdict']['no_suspend'])
        self.assertEqual(report['distributions']['suspended'], {'0': 4, '1': 1})

    def test_taa_report(self):
        report = run4.taa_report(self.scan(self.healthy_lines()))
        self.assertEqual((report['logged_frames'], report['attempted'],
                          report['resolved'], report['used_history']), (4, 2, 2, 2))
        self.assertEqual(report['skip'], {'none': 2, 'not_reached': 2})
        self.assertEqual(report['scene_end_source'], {'hook': 2, 'none': 2})
        self.assertEqual(report['scene_end_check'], {'agree': 2, 'none': 2})
        self.assertEqual(report['camera_reason'], {'camera_path': 2, 'switch_off': 2})
        self.assertEqual(report['camera_cut'], {'0': 4})

    def test_cost_report_splits_capture_frames(self):
        lines = self.healthy_lines() + [
            'telemetry_metric device=1 name=hdr_writeback count=2 failures=0 '
            'total_us=100.0 min_us=40.0 max_us=60.0 bytes=0 buckets=0,2,0,0,0,0',
            'telemetry_metric device=1 name=hdr_writeback count=1 failures=0 '
            'total_us=70.0 min_us=70.0 max_us=70.0 bytes=0 buckets=0,1,0,0,0,0',
            'telemetry_metric device=1 name=hdr_redirect count=3 failures=0 '
            'total_us=18.0 min_us=6.0 max_us=6.0 bytes=0 buckets=3,0,0,0,0,0',
        ]
        report = run4.cost_report(self.scan(lines), {120})
        self.assertEqual(report['hdr_frame_timings']['capture']['frames'], 1)
        self.assertEqual(report['hdr_frame_timings']['ordinary']['frames'], 2)
        # Frame 240 (end=present) is an ordinary frame and carries the 279 us tail.
        self.assertEqual(report['hdr_frame_timings']['ordinary']['writeback_us']['max'],
                         279.0)
        self.assertEqual(report['hdr_frame_timings']['capture']['writeback_us']['median'],
                         70.0)
        self.assertEqual(report['hdr_frame_timings']['ordinary']['total_us']['max'], 287.5)
        metrics = report['telemetry_metrics']
        self.assertEqual(metrics['hdr_writeback']['count'], 3)
        self.assertEqual(metrics['hdr_writeback']['total_us'], 170.0)
        self.assertEqual(metrics['hdr_writeback']['max_us'], 70.0)
        self.assertEqual(metrics['hdr_writeback']['windows'], 2)
        self.assertEqual(metrics['hdr_redirect']['count'], 3)

    def test_cost_report_folds_an_external_cost_json(self):
        cost = {'runs': [{'label': 'run4',
                          'configuration': {'motion_output': {'hdr': '1', 'taa': '1'},
                                            'profiler': {'enabled': None}},
                          'scene': {'fast': {'windows': 3, 'mean_ms': {'median': 30.0},
                                             'min_ms': {'median': 27.0},
                                             'draws_per_frame': {'median': 230.0}}},
                          'menu': {'windows': 2, 'mean_ms': {'median': 38.0},
                                   'min_ms': {'median': 36.0},
                                   'draws_per_frame': {'median': 600.0}}}],
                'comparisons': [{'primary': 'run4', 'baseline': 'run3', 'role': 'route',
                                 'controlled': True,
                                 'regimes': {'fast': {'delta_mean_ms': 6.5,
                                                      'delta_mean_percent': 27.0,
                                                      'delta_min_ms': 5.8}}}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'cost.json'
            path.write_text(json.dumps(cost))
            report = run4.cost_report(self.scan(self.healthy_lines()), set(), path)
        frame_time = report['frame_time']
        self.assertEqual(frame_time['runs']['run4']['flags'], {'hdr': '1', 'taa': '1'})
        self.assertEqual(frame_time['runs']['run4']['regimes']['fast']['mean_ms'], 30.0)
        self.assertEqual(frame_time['runs']['run4']['regimes']['menu']['windows'], 2)
        self.assertEqual(frame_time['comparisons'][0]['regimes']['fast']['delta_mean_ms'],
                         6.5)


class Window(unittest.TestCase):
    def test_episode_grouping(self):
        lines = [
            'telemetry_presentation phase=create_before device=0 focus_window=016f015e '
            'device_window=016f015e width=1280 height=768',
            'telemetry_window device=1 frame=0 window=016f015e foreground=016f015e '
            'thread_focus=016f015e iconic=0 visible=1 window_rect=1917,82,3203,882',
            'telemetry_window_context device=1 frame=0 gui_active=016f015e '
            'gui_focus=016f015e gui_capture=00000000 clip=1920,111,3200,879',
            'telemetry_cursor_poll device=1 frame=1 flags=0 cursor=00000000 x=1 y=2',
            'telemetry_window device=1 frame=10 window=016f015e foreground=00030020 '
            'thread_focus=00000000 iconic=0 visible=1 window_rect=1917,82,3203,882',
            'telemetry_window_context device=1 frame=10 gui_active=00000000 '
            'gui_focus=00000000 gui_capture=00000000 clip=-1512,0,5120,1440',
            'telemetry_cursor_poll device=1 frame=10 flags=1 cursor=00010022 x=5 y=6',
            'telemetry_cursor_poll device=1 frame=12 flags=1 cursor=00010022 x=5 y=7',
            'telemetry_window device=1 frame=14 window=016f015e foreground=016f015e '
            'thread_focus=016f015e iconic=0 visible=1 window_rect=1917,82,3203,882',
            'telemetry_window_context device=1 frame=14 gui_active=016f015e '
            'gui_focus=016f015e gui_capture=00000000 clip=1920,111,3200,879',
            'telemetry_cursor_poll device=1 frame=20 flags=0 cursor=00000000 x=5 y=7',
            hdr_frame(60), motion_frame(60),
        ]
        with tempfile.TemporaryDirectory() as directory:
            scan = run4.scan_log(write_log(directory, lines))
        report = run4.window_report(scan)
        self.assertEqual(report['device_window'], '016f015e')
        self.assertEqual(len(report['episodes']), 1)
        episode = report['episodes'][0]
        self.assertEqual(episode['departure']['frame'], 10)
        self.assertEqual(episode['departure']['clip'], '-1512,0,5120,1440')
        self.assertEqual(episode['return']['frame'], 14)
        self.assertEqual([c['frame'] for c in episode['cursor']], [10, 12])
        self.assertEqual([c['frame'] for c in report['cursor_visible_records']], [10, 12])
        self.assertFalse(report['iconic_seen'])
        # The redirect latched at frame 60, outside the +/-60 frame window of the
        # episode's return, so the episode reports no latched frame nearby.
        self.assertEqual(episode['redirect_latched_within_60_frames'], [60])


class Bursts(unittest.TestCase):
    def test_grouping(self):
        self.assertEqual(run4.burst_groups([1092, 1093, 1094, 1095, 1284, 1285]),
                         [[1092, 1093, 1094, 1095], [1284, 1285]])
        self.assertEqual(run4.burst_groups([5]), [[5]])
        self.assertEqual(run4.burst_groups([]), [])

    def test_describe(self):
        stats = run4.describe([1.0, 2.0, 3.0, 4.0])
        self.assertEqual((stats['count'], stats['min'], stats['max'], stats['total']),
                         (4, 1.0, 4.0, 10.0))
        self.assertEqual(stats['median'], 2.5)
        self.assertEqual(run4.describe([]), {'count': 0})


if __name__ == '__main__':
    unittest.main()
