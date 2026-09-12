"""Synthetic fixtures for tools/analysis/analyze_iteration09.py.

No game data.  A small log is written with the exact record formats the proxy
emits, chosen so every quantity the iteration-9 analyzer reports is known in
closed form:

* three `motion_output_frame` records - one menu frame (`taa_skip=2`,
  `routed=0`, `camera_valid=0`), one routed frame and one routed frame with a
  cut - so the skip histogram, the cut list, the `scene_end_source` histogram,
  the state-shadow totals and the "routed frame without a camera read" list all
  have single-value expectations;
* three `camera_state` records: one invalid, one with an exactly orthonormal
  rotation (whose self-rotation floor must be 0) and one whose rows are scaled
  by 1 - 1e-5 (whose floor is then the analytic `acos(1 - 3e-5 ... )`), plus a
  90-degree/60-degree projection whose derived field of view is exact;
* two one-second `frame_normal` telemetry windows, one in each regime, so the
  bins and the two regime medians are exact;
* three capture draw blocks - a routed one, a gate-rejected one and one with no
  `motion_route` record at all - each with a different sampler signature, so the
  census attributes every draw to the right class and the "not recorded" list
  names `MIPMAPLODBIAS`;
* the low-pass model is checked against its own closed form (Catmull-Rom
  weights sum to one, the Nyquist null at a half-texel offset, the geometric
  steady state) and against the two amplitude ratios the GPU fixture measured
  in docs/verification/temporal-resolve.md.
"""
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
import analyze_iteration09 as tool  # noqa: E402

FRAME_TEMPLATE = (
    'motion_output_frame device=1 frame={frame} latched={latched} filled={filled} fill_result=00000000'
    ' fill_restore=00000000 draws={draws} routed={routed} matched={matched} gate1=0 gate2={gate2} gate3=0'
    ' gate4=0 gate5=0 gate6=0 apply_failures=0 restore_failures=0 history_previous=0 history_current=0'
    ' committed={filled} selector_state=9 present=00000000 depth=1 depth_routed={routed} jitter=1'
    ' jitter_index=0 jitter_x=0.125000 jitter_y=0.277778 jitter_previous_x=0.000000 jitter_previous_y=0.000000'
    ' jittered={routed} cut={cut} cut_median_px={cut_px} cut_missing=0.0100 cut_samples=200 taa=1'
    ' taa_attempted={attempted} taa_resolved={attempted} taa_history={history} taa_skip={skip}'
    ' taa_result={result} taa_restore=00000000 taa_copy={result} scene_open=0 active_queries=0'
    ' taa_references=12 camera_valid={camera_valid} camera_background_valid={camera_valid} camera_reads=1'
    ' camera_policy={policy} camera_reason={reason} camera_cut=0 camera_rotation_deg={rotation}'
    ' rt_mode=perdraw timing=cpu_qpc set_rt={set_rt} lazy_flushes=0 jitter_writes={routed} readbacks={readbacks}'
    ' gate_us={gate_us} route_draw_us={draw_us} set_rt_us=100.0 lazy_flush_us=0.0 jitter_us=10.0 fill_us=20.0'
    ' taa_run_us={taa_us} taa_capture_us=1.0 taa_copy_color_us=1.0 taa_copy_depth_us=1.0 taa_draw_us=1.0'
    ' taa_apply_us=1.0 taa_copy_back_us=1.0 readback_us={readback_us} state_shadow=1 rs_queries={rs}'
    ' rs_hits={rs} rs_gets=10 rs_resyncs=0 scene_hook=0 scene_end_source={source} scene_end_check={check}'
    ' hook_signals=0 hook_outside_scene=0 hook_state=0 draws_after_hook=0 bloom_copy_seen={filled}')

CAMERA_TEMPLATE = (
    'camera_state device=1 frame={frame} status=active reads=1 valid={valid} read_failure=0 failure=0'
    ' projection={projection} view={view} p00={p00} p11={p11} p20=0 p21=0'
    ' r00={r[0]} r01={r[1]} r02={r[2]} r10={r[3]} r11={r[4]} r12={r[5]} r20={r[6]} r21={r[7]} r22={r[8]}'
    ' t=1,2,3 background_valid={valid} background_p00={p00} background_p11={p11}'
    ' background_rotation_deg=0.0000 history_view_valid={valid} history_view_frame={frame}'
    ' rotation_deg={rotation} policy={policy} reason={reason} camera_cut={cut} mode=0 cut_deg=20.00')

# A yaw of exactly 30 degrees, orthonormal to double precision.
COS30, SIN30 = math.cos(math.radians(30.0)), math.sin(math.radians(30.0))
ORTHONORMAL = [COS30, 0.0, -SIN30, 0.0, 1.0, 0.0, SIN30, 0.0, COS30]
SHRINK = 1.0 - 1e-5
SHRUNK = [v * SHRINK for v in ORTHONORMAL]


def window(name, count, total_us, low, high):
    return ('telemetry_metric device=1 name=%s count=%d failures=0 total_us=%.3f min_us=%.3f max_us=%.3f'
            ' bytes=0 buckets=0,0,0,%d,0,0' % (name, count, total_us, low, high, count))


def draw_block(frame, index, vs, samplers, route=None):
    """One capture draw snapshot: the `draw` header, its sampler state and,
    optionally, the `motion_route` verdict that closes the block."""
    lines = ['draw device=1 frame=%d index=%d kind=indexed topology=4 primitives=8 vs=%s ps=deadbeefdeadbeef'
             % (frame, index, vs)]
    for stage, states in sorted(samplers.items()):
        for state, value in sorted(states.items()):
            lines.append('sampler stage=%d state=%d value=%d' % (stage, state, value))
    lines.append('draw_result device=1 frame=%d index=%d result=00000000' % (frame, index))
    if route is not None:
        lines.append('motion_route device=1 frame=%d index=%d gate=%d routed=%d matched=%d depth=%d jittered=1'
                     ' vs=%s ps=deadbeefdeadbeef result=00000000'
                     % (frame, index, route['gate'], route['routed'], route['matched'], route['routed'], vs))
    return lines


MATERIAL_SAMPLER = {0: {5: 2, 6: 3, 7: 2, 10: 16, 11: 0},   # LINEAR mag, ANISOTROPIC min, LINEAR mip, 16x
                    1: {5: 2, 6: 2, 7: 0, 10: 4, 11: 0}}    # LINEAR/LINEAR/no mips, 4x
REJECTED_SAMPLER = {0: {5: 1, 6: 1, 7: 0, 10: 1, 11: 0}}    # POINT/POINT/no mips
OVERLAY_SAMPLER = {0: {5: 2, 6: 2, 7: 0, 10: 1, 11: 1}}     # LINEAR/LINEAR/no mips, sRGB on


def write_log(path):
    lines = [
        'motion_output_mode requested=1 scope=live_same_draw_diagnostic taa=1 taa_debug=0 jitter=1'
        ' jitter_samples=8 rt_mode=perdraw state_shadow=1 scene_hook=0 hdr=0',
        'motion_output_device device=1 enabled=1 reason=ok jitter=1 taa=1 taa_reason=ok rt_mode=perdraw',
        'motion_output_target device=1 width=1280 height=768 create=00000000 depth=1 depth_create=00000000',
        'object_trace active=1 status=active recovery_required=0',
        'object_lifetime active=1 status=active_without_baseline recovery_required=0',
        'telemetry_summary device=1 frame=10 reason=interval qpc=1 since_start_us=1 interval_us=1',
        window('frame_normal', 30, 30 * 30000.0, 29000.0, 31000.0),        # fast regime: 30 ms
        # the menu frame: nothing latched, nothing routed, the camera unread
        FRAME_TEMPLATE.format(frame=10, latched=0, filled=0, draws=600, routed=0, matched=0, gate2=600,
                              cut=0, cut_px='0.0000', attempted=0, history=0, skip=2, result='00000001',
                              camera_valid=0, policy=1, reason=1, rotation='0.0000', set_rt=0, readbacks=0,
                              gate_us='0.0', draw_us='0.0', taa_us='0.0', readback_us='0.0', rs=0,
                              source='none', check=0),
        CAMERA_TEMPLATE.format(frame=10, valid=0, projection='00000000', view='00000000', p00=0, p11=0,
                               r=[0] * 9, rotation='0.0000', policy=1, reason=1, cut=0),
        'telemetry_summary device=1 frame=20 reason=interval qpc=2 since_start_us=2 interval_us=1',
        window('frame_normal', 20, 20 * 60000.0, 58000.0, 62000.0),        # slow regime: 60 ms
        # a routed frame with an orthonormal rotation
        FRAME_TEMPLATE.format(frame=20, latched=1, filled=1, draws=400, routed=200, matched=200, gate2=100,
                              cut=0, cut_px='0.1000', attempted=1, history=1, skip=0, result='00000000',
                              camera_valid=1, policy=2, reason=0, rotation='0.0000', set_rt=800, readbacks=0,
                              gate_us='3000.0', draw_us='1000.0', taa_us='300.0', readback_us='0.0', rs=1000,
                              source='stretchrect', check=3),
        CAMERA_TEMPLATE.format(frame=20, valid=1, projection='0372ec68', view='0372ec18', p00=1.0,
                               p11=math.sqrt(3.0), r=['%.9g' % v for v in ORTHONORMAL],
                               rotation='0.0000', policy=2, reason=0, cut=0),
        # a routed frame the cut detector rejected, with shrunken engine rows
        FRAME_TEMPLATE.format(frame=21, latched=1, filled=1, draws=400, routed=200, matched=190, gate2=100,
                              cut=1, cut_px='70.0000', attempted=1, history=0, skip=0, result='00000000',
                              camera_valid=1, policy=2, reason=0, rotation='6.0000', set_rt=800, readbacks=2,
                              gate_us='5000.0', draw_us='2000.0', taa_us='500.0', readback_us='25000.0', rs=1000,
                              source='stretchrect', check=3),
        CAMERA_TEMPLATE.format(frame=21, valid=1, projection='0372ec68', view='0372ec18', p00=1.0,
                               p11=math.sqrt(3.0), r=['%.9g' % v for v in SHRUNK],
                               rotation='6.0000', policy=2, reason=0, cut=0),
    ]
    lines += draw_block(21, 0, 'aaaa', MATERIAL_SAMPLER, {'gate': 0, 'routed': 1, 'matched': 1})
    lines += draw_block(21, 1, 'bbbb', REJECTED_SAMPLER, {'gate': 4, 'routed': 0, 'matched': 0})
    lines += draw_block(21, 2, 'cccc', OVERLAY_SAMPLER, None)   # closed by the end of the file
    path.write_text('\n'.join(lines) + '\n')


class Iteration09Log(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.log = Path(cls.directory.name) / 'session.log'
        write_log(cls.log)
        cls.report = tool.analyze(cls.log, width=1280, height=768)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_serializable(self):
        json.loads(json.dumps(self.report))

    def test_health_counts(self):
        h = self.report['health']
        self.assertEqual(h['frame_records'], 3)
        self.assertEqual(h['taa_attempted'], 2)
        self.assertEqual(h['taa_resolved'], 2)
        self.assertEqual(h['taa_history'], 1)
        self.assertEqual(h['taa_skip'], {'0': 2, '2': 1})
        self.assertEqual(h['scene_end_source'], {'none': 1, 'stretchrect': 2})
        self.assertEqual(h['rt_mode'], {'perdraw': 3})
        self.assertEqual(h['selector_state'], {'9': 3})
        self.assertEqual(h['camera_valid'], {'0': 1, '1': 2})
        self.assertEqual(h['state_shadow'], {'rs_queries': 2000, 'rs_hits': 2000, 'rs_gets': 30, 'rs_resyncs': 0})
        self.assertEqual(h['totals']['routed'], 400)
        self.assertEqual(h['totals']['matched'], 390)

    def test_cut_and_camera_lists(self):
        h = self.report['health']
        self.assertEqual([c['frame'] for c in h['cut_events']], [21])
        self.assertAlmostEqual(h['cut_events'][0]['cut_median_px'], 70.0)
        self.assertEqual(h['cut_events'][0]['taa_history'], 0)
        self.assertEqual(h['resolved_without_history'], [21])
        # the only frame with camera_valid=0 routed nothing, so the list is empty
        self.assertEqual(h['routed_frames_without_camera'], [])
        self.assertEqual(h['camera_cut_events'], [])

    def test_route_cost_groups(self):
        costs = self.report['health']['route_costs']
        self.assertEqual(costs['capture']['frames'], 1)
        self.assertEqual(costs['normal']['frames'], 1)
        # only gate/route_draw/fill/lazy_flush may be added (they exclude each other)
        self.assertAlmostEqual(costs['normal']['route_exclusive_us']['mean'], 3000.0 + 1000.0 + 20.0, places=6)
        self.assertAlmostEqual(costs['capture']['route_exclusive_us']['mean'], 5000.0 + 2000.0 + 20.0, places=6)
        self.assertAlmostEqual(costs['capture']['readback_us']['mean'], 25000.0, places=6)

    def test_timing_regimes(self):
        regimes = self.report['timing_regimes']['1:frame_normal']
        self.assertEqual(regimes['windows'], 2)
        self.assertEqual(regimes['stall_windows'], 0)
        self.assertEqual(regimes['fast'], {'windows': 1, 'median_us': 30000.0})
        self.assertEqual(regimes['slow'], {'windows': 1, 'median_us': 60000.0})
        self.assertEqual(regimes['bins']['30-40ms'], 1)
        self.assertEqual(regimes['bins']['60-70ms'], 1)
        # the window after the menu frame is `other`, the one after a routed frame `scene`
        self.assertEqual(regimes['by_regime']['other']['windows'], 1)
        self.assertEqual(regimes['by_regime']['scene']['windows'], 1)

    def test_camera_fov_and_policy(self):
        c = self.report['camera']
        self.assertEqual(c['records'], 3)
        self.assertEqual(c['valid'], 2)
        self.assertEqual(c['policy'], {1: 1, 2: 2})
        self.assertEqual(c['reason'], {'camera_path': 2, 'switch_off': 1})
        self.assertEqual(c['invalid_frames'], [10])
        self.assertEqual(c['camera_cuts'], [])
        self.assertEqual(c['read_failures'], {})
        projection = [p for p in c['projections'] if p['records'] == 2][0]
        self.assertAlmostEqual(projection['fov']['horizontal_deg'], 90.0, places=6)
        self.assertAlmostEqual(projection['fov']['vertical_deg'], 60.0, places=6)

    def test_rotation_floor(self):
        """The floor is an artefact of non-unit engine rows, not a rotation."""
        rows = {r['frame']: r for r in self.report['camera']['records_detail']}
        self.assertAlmostEqual(rows[20]['rotation_floor_deg'], 0.0, places=6)
        expected = math.degrees(math.acos(min(1.0, (3.0 * SHRINK ** 2 - 1.0) * 0.5)))
        # the log records nine significant digits, so the floor is reproduced to
        # about 1e-5 degrees, not exactly
        self.assertAlmostEqual(rows[21]['rotation_floor_deg'], expected, places=4)
        self.assertGreater(rows[21]['rotation_floor_deg'], 0.0)
        self.assertAlmostEqual(self.report['camera']['row_norm_deviation_max'], 1e-5, places=9)

    def test_rotation_displacement_prediction(self):
        rows = {r['frame']: r for r in self.report['camera']['records_detail']}
        horizontal, vertical = rows[21]['predicted_displacement_px']
        # 6 degrees at p00 = 1 over a 1280-wide target
        self.assertAlmostEqual(horizontal, math.radians(6.0) * 640.0, places=6)
        self.assertAlmostEqual(vertical, math.radians(6.0) * math.sqrt(3.0) * 384.0, places=6)

    def test_sampler_census_classes(self):
        s = self.report['samplers']
        self.assertEqual(s['draw_blocks'], {'no_route_record': 1, 'rejected_gate4': 1, 'routed_matched': 1})
        routed = s['per_stage']['routed_matched']
        self.assertEqual(routed['0']['MINFILTER'], {'ANISOTROPIC': 1})
        self.assertEqual(routed['0']['MIPFILTER'], {'LINEAR': 1})
        self.assertEqual(routed['0']['MAXANISOTROPY'], {'16': 1})
        self.assertEqual(routed['1']['MAXANISOTROPY'], {'4': 1})
        # the rejected draw leaves every stage at the D3D9 default, so it is
        # listed instead of tabulated
        self.assertEqual(s['per_stage']['rejected_gate4'], {})
        self.assertEqual(s['default_stages']['rejected_gate4'], [0])
        self.assertEqual(s['default_stages']['routed_matched'], [])
        self.assertEqual(s['per_stage']['no_route_record']['0']['SRGBTEXTURE'], {'1': 1})

    def test_sampler_missing_states(self):
        s = self.report['samplers']
        self.assertIn('MIPMAPLODBIAS', s['states_not_recorded'])
        self.assertEqual(s['sharpness_states_missing'], ['MIPMAPLODBIAS'])
        self.assertEqual(sorted(s['states_recorded']),
                         ['MAGFILTER', 'MAXANISOTROPY', 'MINFILTER', 'MIPFILTER', 'SRGBTEXTURE'])

    def test_witnesses(self):
        self.assertEqual(self.report['witnesses']['motion_output_target'], 1)
        self.assertEqual(self.report['witnesses']['object_lifetime'], 1)
        self.assertNotIn('motion_output_reset', self.report['witnesses'])


class ResolveLowPassModel(unittest.TestCase):
    def test_catmull_rom_partition_of_unity(self):
        for t in (0.0, 0.125, 0.25, 0.5, 0.75, 1.0):
            self.assertAlmostEqual(sum(tool.catmull_rom_weights(t)), 1.0, places=12)

    def test_zero_offset_is_lossless(self):
        weights = tool.catmull_rom_weights(0.0)
        self.assertEqual(weights, [0.0, 1.0, 0.0, 0.0])
        for period in (2, 4, 8):
            self.assertAlmostEqual(tool.transfer(weights, period), 1.0, places=12)
            self.assertAlmostEqual(tool.steady_state(1.0, 0.9), 1.0, places=12)

    def test_half_offset_nulls_nyquist(self):
        """At a half-texel offset the four-tap kernel cancels a 2-px pattern."""
        self.assertAlmostEqual(tool.transfer(tool.catmull_rom_weights(0.5), 2), 0.0, places=12)
        self.assertAlmostEqual(tool.steady_state(0.0, 0.9), 0.1, places=12)

    def test_steady_state_closed_form(self):
        self.assertAlmostEqual(tool.steady_state(0.5, 0.9), 0.1 / (1.0 - 0.45), places=12)
        self.assertIsNone(tool.steady_state(1.0 / 0.9, 0.9))

    def test_monotone_in_period(self):
        table = tool.resolve_lowpass()['table']
        rows = sorted((r for r in table if r['offset'] == 0.75), key=lambda r: r['period_px'])
        values = [r['catmull_rom_steady'] for r in rows]
        self.assertEqual(values, sorted(values))
        self.assertLess(values[0], 0.35)     # 2-px detail loses most of its amplitude
        self.assertGreater(values[-1], 0.99)  # 32-px detail is untouched

    def test_agrees_with_the_gpu_fixture(self):
        """The two amplitude ratios docs/verification/temporal-resolve.md
        measured on the GPU fixture ("Resampling blur"), which the model must
        reproduce to better than 0.05 before it is used to extrapolate."""
        reference = tool.resolve_lowpass()['reference']
        self.assertLess(reference['model_catmull_rom_error'], 0.05)
        self.assertLess(reference['model_bilinear_error'], 0.05)
        self.assertGreater(reference['model_catmull_rom_amplitude'], reference['model_bilinear_amplitude'])

    def test_bilinear_is_worse_than_catmull_rom(self):
        for period in (3, 4, 6, 8, 16):
            self.assertLess(tool.transfer(tool.bilinear_weights(0.75), period),
                            tool.transfer(tool.catmull_rom_weights(0.75), period))


if __name__ == '__main__':
    unittest.main()
