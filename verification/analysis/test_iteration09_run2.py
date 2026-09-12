"""Synthetic checks for `tools/analysis/analyze_iteration09_run2.py`; no game content.

Every input here is constructed so the expected output is known in closed form
or by construction.  The iteration-9 run-2 report is only as good as these
helpers, so each is pinned separately:

* the streaming log reader - scene-hook verdicts, mesh-cache counters,
  adjacency windows and the per-draw sampler snapshot, including that a
  `motion_route` record inside a draw block classifies that draw as routed,
* the incoming-FPU decoder against `mesh_adjacency_cache.cpp:supported_fp`,
* the adjacency phase grouping,
* the analytic box-prefilter gradient factor, against an independent quadrature,
* the per-class gradient/Laplacian energy on images with a known answer,
* the reconstruction taps (bilinear and the resolve's Catmull-Rom, against the
  shader's own weights), the resampler and the ideal-supersampling reference -
  including that averaging sub-pixel shifts of a sine removes exactly the
  predicted energy and that negating the kernel changes nothing,
* the resampling-free captured-phase average,
* the spectral band fraction and the edge-spread/MTF estimator.
"""
import array
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import analyze_iteration08_taa as it08
import analyze_iteration09_run2 as run2

FRAME_TEMPLATE = (
    'motion_output_frame device=1 frame={frame} latched={latched} draws={draws} '
    'routed={routed} matched={routed} jitter=1 jitter_index={index} jitter_x={jx} '
    'jitter_y={jy} cut=0 cut_median_px={cut} cut_missing=0.0000 cut_samples=10 taa=1 '
    'taa_attempted={att} taa_resolved={res} taa_history={att} taa_skip={skip} '
    'camera_valid=1 camera_policy=2 camera_reason=0 camera_cut=0 '
    'camera_rotation_deg={rot} scene_end_source={source} scene_end_check={check} '
    'hook_signals={signals} hook_outside_scene={outside} hook_state={state} '
    'draws_after_hook={after} bloom_copy_seen={copy} gate1=0 gate2=1 gate3=0 gate4=0 '
    'gate5=0 gate6=0 apply_failures=0 restore_failures=0 taa_run_us=1.0 gate_us=1.0 '
    'route_draw_us=1.0'
)


def frame_line(frame, latched=1, check=1, source='hook', signals=1, outside=0, state=0,
               after=0, copy=1, index=0, jx=0.0, jy=0.0, cut=0.1, rot=0.5, draws=10,
               routed=8, att=1, res=1, skip=0):
    return FRAME_TEMPLATE.format(frame=frame, latched=latched, draws=draws, routed=routed,
                                 index=index, jx=jx, jy=jy, cut=cut, att=att, res=res,
                                 skip=skip, rot=rot, source=source, check=check,
                                 signals=signals, outside=outside, state=state, after=after,
                                 copy=copy)


def make_image(width, height, fn):
    return array.array('f', [fn(x, y) for y in range(height) for x in range(width)])


class Base(unittest.TestCase):
    def scan(self, lines):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.log'
            path.write_text('\n'.join(lines) + '\n')
            return run2.scan_log(path)


class SceneHookTest(Base):
    def test_verdicts_and_unlatched_frames(self):
        lines = [
            'telemetry_start schema=1 qpc_frequency=1000 qpc=1000 anchor=proxy_initialize',
            'scene_hook active=1 status=active',
            frame_line(0, latched=0, check=0, source='none', signals=0, copy=0, routed=0,
                       att=0, res=0, skip=2),
            frame_line(60, latched=0, check=0, source='none', signals=1, outside=1,
                       state=9, copy=0, routed=0, att=0, res=0, skip=2),
            frame_line(120, check=1, source='hook'),
            frame_line(180, check=4, source='stretchrect', signals=2, after=3),
            'motion_output_scene_hook_disagreement device=1 frame=180 installed=1 '
            'signals=2 outside_scene=0 selector_state=0 draws_after_hook=3 '
            'bloom_copy_seen=1 hook_scene_end=1',
            'scene_hook_shutdown restored=1 status=disabled',
        ]
        report = run2.scene_hook_report(self.scan(lines))
        self.assertEqual(report['check_distribution'],
                         {'None': 2, 'Agree': 1, 'Disagree': 1})
        self.assertEqual(report['source_distribution'],
                         {'none': 2, 'hook': 1, 'stretchrect': 1})
        # Counted only where a resolve actually ran, and by where it ran.
        self.assertEqual(report['resolved_by_source'], {'hook': 1, 'stretchrect': 1})
        self.assertEqual(report['latched_frames'], 2)
        self.assertFalse(report['latched_all_agree'])
        self.assertEqual(report['latched_draws_after_hook_max'], 3)
        self.assertEqual(len(report['disagreement_records']), 1)
        self.assertEqual([row['frame'] for row in report['unlatched_frames']], [0, 60])
        # An out-of-scene signal's selector state is decoded, not left numeric.
        self.assertEqual(report['unlatched_frames'][1]['hook_state'], 'CameraState')
        self.assertEqual(report['shutdown_records'][0]['restored'], '1')
        # The patch-site constants travel with the report so the byte claim
        # behind `status=active` can be re-checked against the source.
        self.assertEqual(report['patch_site']['expected_bytes'], 'e8 9a 25 05 00')

    def test_all_agree_run(self):
        lines = ['scene_hook active=1 status=active'] + [frame_line(f)
                                                         for f in (60, 120, 180)]
        report = run2.scene_hook_report(self.scan(lines))
        self.assertTrue(report['latched_all_agree'])
        self.assertEqual(report['check_distribution'], {'Agree': 3})
        self.assertEqual(report['disagreement_records'], [])


class MeshCacheTest(Base):
    LINES = [
        'telemetry_start schema=1 qpc_frequency=1000 qpc=0 anchor=proxy_initialize',
        'mesh_cache ready=1 buffer_contract_required=1 entries=512',
        'mesh_hook table=0 installed=1 owned_slots=3 scope=shared_native_vtable',
        'mesh_cache_bypass cumulative=1 reason=floating_point count=5',
        'mesh_cache_metric cumulative=1 qpc=1000 dispatch_enabled=1 '
        'buffer_contract=public_systemmem_readonly faulted=0 calls=10 hits=0 misses=0 '
        'bypasses=10 contention=0 admissions=0 evictions=0 native_calls=10 '
        'native_failures=0 retained_entries=0 gate_rejections=0 rejected_fp=0 '
        'native_ticks=2000 gate_ticks=100',
        'mesh_cache_bypass cumulative=1 reason=floating_point count=10',
        'mesh_cache_fp_first control=0000027f status=00000000 tag=0000ffff mxcsr=00009fe0',
    ]

    def test_counters(self):
        report = run2.mesh_cache_report(self.scan(self.LINES))
        self.assertEqual(report['bypass_reasons'], {'floating_point': 10})
        self.assertEqual(report['single_reason'], 'floating_point')
        self.assertTrue(report['effect'].startswith('none'))
        self.assertEqual(report['fractions'], {'hit': 0.0, 'miss': 0.0, 'bypass': 1.0})
        self.assertAlmostEqual(report['native_seconds'], 2.0)
        self.assertAlmostEqual(report['mean_native_ms'], 200.0)
        self.assertAlmostEqual(report['gate_seconds'], 0.1)
        self.assertEqual(report['hook'][0]['installed'], '1')
        self.assertEqual(report['metric_records'], 1)
        self.assertEqual(report['bypass_records'], 2)

    def test_decode_observed_game_fpu_state(self):
        decoded = run2.decode_fp({'control': '0000027f', 'status': '00000000',
                                  'tag': '0000ffff', 'mxcsr': '00009fe0'})
        self.assertFalse(decoded['supported'])
        self.assertEqual({f['field'] for f in decoded['failures']},
                         {'x87 control word', 'MXCSR'})
        control = next(f for f in decoded['failures'] if f['field'] == 'x87 control word')
        self.assertIn('53-bit double', control['detail'])
        mxcsr = next(f for f in decoded['failures'] if f['field'] == 'MXCSR')
        self.assertIn('FZ (flush-to-zero) set', mxcsr['detail'])
        self.assertIn('DAZ (denormals-are-zero) set', mxcsr['detail'])
        # Only the six sticky exception flags are ignored by the comparison.
        self.assertEqual(mxcsr['observed_compared'], '0x9fc0')

    def test_decode_supported_state(self):
        decoded = run2.decode_fp({'control': '0000007f', 'status': '00000000',
                                  'tag': '0000ffff', 'mxcsr': '00001fa0'})
        self.assertTrue(decoded['supported'])
        self.assertEqual(decoded['failures'], [])


class AdjacencyTimelineTest(Base):
    def test_phase_grouping(self):
        lines = ['telemetry_start schema=1 qpc_frequency=1000 qpc=0 '
                 'anchor=proxy_initialize']
        for index in range(4):
            lines.append(f'loading_metric op=ID3DXMesh::GenerateAdjacency '
                         f'qpc={1000 * index} count=100 failures=0 pending=0 ambiguous=0 '
                         f'bytes=0 exclusive_us=500000.000 max_us=1000.000')
        for index in range(3):
            lines.append(f'loading_metric op=ID3DXMesh::GenerateAdjacency '
                         f'qpc={40000 + 1000 * index} count=50 failures=0 pending=0 '
                         f'ambiguous=0 bytes=0 exclusive_us=250000.000 max_us=2000.000')
        lines.append('loading_metric op=ReadFile qpc=90000 count=9 failures=0 pending=0 '
                     'ambiguous=0 bytes=0 exclusive_us=1.000 max_us=1.000')
        timeline = run2.adjacency_timeline(self.scan(lines), quiet_gap_s=3.0,
                                           phase_min_calls=100)
        self.assertEqual(timeline['windows'], 7)  # ReadFile is not adjacency
        self.assertEqual(timeline['total_calls'], 550)
        self.assertAlmostEqual(timeline['total_adjacency_seconds'], 2.75)
        self.assertEqual(len(timeline['phases']), 2)
        first, second = timeline['phases']
        self.assertEqual((first['start_seconds'], first['end_seconds']), (0.0, 3.0))
        self.assertEqual(first['calls'], 400)
        self.assertAlmostEqual(first['mean_call_ms'], 5.0)
        self.assertAlmostEqual(second['start_seconds'], 40.0)
        self.assertEqual(second['calls'], 150)
        self.assertAlmostEqual(second['max_call_seconds'], 0.002)


class SamplerTest(Base):
    @staticmethod
    def draw_block(frame, index, routed, minfilter, aniso):
        block = [f'draw device=1 frame={frame} index={index} kind=indexed topology=4 '
                 f'primitives=2 vs=a ps=b',
                 'texture stage=0 ptr=deadbeef type=3 identity=1',
                 f'sampler stage=0 state=6 value={minfilter}',
                 'sampler stage=0 state=5 value=2',
                 'sampler stage=0 state=7 value=2',
                 f'sampler stage=0 state=10 value={aniso}',
                 'sampler stage=0 state=11 value=0',
                 # A stage with no bound texture must not appear at all.
                 'texture stage=1 ptr=00000000 type=0 identity=0',
                 'sampler stage=1 state=6 value=1']
        if routed:
            block.append(f'motion_route device=1 frame={frame} index={index} gate=0 '
                         f'routed=1 matched=1')
        block.append(f'draw_result device=1 frame={frame} index={index} result=00000000')
        return block

    def test_routed_split_and_missing_states(self):
        lines = (self.draw_block(1, 1, True, 3, 16) + self.draw_block(1, 2, True, 3, 16)
                 + self.draw_block(1, 3, False, 2, 1))
        report = run2.sampler_report(self.scan(lines))
        self.assertEqual(report['captured_draws'], {'routed': 2, 'unrouted': 1})
        self.assertEqual(report['routed'], [{'minfilter': 'ANISOTROPIC',
                                             'magfilter': 'LINEAR',
                                             'mipfilter': 'LINEAR',
                                             'maxanisotropy': 16, 'srgbtexture': 0,
                                             'mipmaplodbias': None, 'maxmiplevel': None,
                                             'stage_samples': 2}])
        self.assertEqual(report['unrouted'][0]['minfilter'], 'LINEAR')
        self.assertEqual(report['unrouted'][0]['maxanisotropy'], 1)
        # A log that predates the mip-bias capture: the two states are missing.
        self.assertIn('D3DSAMP_MIPMAPLODBIAS', report['states_not_recorded'])
        self.assertIn('D3DSAMP_MAXMIPLEVEL', report['states_not_recorded'])
        self.assertEqual(report['states_recorded'],
                         ['magfilter', 'maxanisotropy', 'minfilter', 'mipfilter', 'srgbtexture'])
        self.assertIn('predates', report['note'])
        self.assertEqual(list(report['routed_per_stage']), ['0'])

    def test_lod_bias_recorded(self):
        # capture.cpp since the mip-bias work: state 8 raw plus bias=<float>
        # (-0.5 is 0xbf000000 = 3204448256 as a DWORD), state 9 raw.
        block = self.draw_block(1, 1, True, 3, 16)
        block[6:6] = ['sampler stage=0 state=8 value=3204448256 bias=-0.5',
                      'sampler stage=0 state=9 value=0']
        other = self.draw_block(1, 2, False, 2, 1)
        other[6:6] = ['sampler stage=0 state=8 value=0 bias=0', 'sampler stage=0 state=9 value=2']
        report = run2.sampler_report(self.scan(block + other))
        self.assertEqual(report['routed'][0]['mipmaplodbias'], -0.5)
        self.assertEqual(report['routed'][0]['maxmiplevel'], 0)
        self.assertEqual(report['unrouted'][0]['mipmaplodbias'], 0.0)
        self.assertEqual(report['unrouted'][0]['maxmiplevel'], 2)
        self.assertNotIn('D3DSAMP_MIPMAPLODBIAS', report['states_not_recorded'])
        self.assertEqual(report['states_not_recorded'], ['D3DSAMP_ADDRESSU', 'D3DSAMP_ADDRESSV'])
        self.assertIn('mipmaplodbias', report['states_recorded'])
        self.assertIn('restores its own bias', report['note'])
        # Without the bias field the raw DWORD is reinterpreted.
        raw = self.draw_block(1, 3, True, 3, 16)
        raw[6:6] = ['sampler stage=0 state=8 value=3204448256']
        self.assertEqual(run2.sampler_report(self.scan(raw))['routed'][0]['mipmaplodbias'], -0.5)


class JitterTableTest(Base):
    def test_complete_cycle_required(self):
        complete = [frame_line(60 * i, index=i, jx=0.1 * i, jy=-0.1 * i)
                    for i in range(4)]
        table = run2.jitter_table(self.scan(complete))
        self.assertEqual(len(table), 4)
        self.assertAlmostEqual(table[2][0], 0.2)
        self.assertAlmostEqual(table[2][1], -0.2)
        partial = [frame_line(60, index=0), frame_line(120, index=2)]
        self.assertEqual(run2.jitter_table(self.scan(partial)), [])


class BoxFilterReferenceTest(unittest.TestCase):
    def test_matches_independent_quadrature(self):
        result = run2.box_filter_gradient_factor(samples=4096)

        def sinc2(f):
            return 1.0 if f == 0.0 else (math.sin(math.pi * f) / (math.pi * f)) ** 2

        def simpson(fn, n=2000):
            h = 1.0 / n
            total = fn(-0.5) + fn(0.5)
            for i in range(1, n):
                total += (4 if i % 2 else 2) * fn(-0.5 + i * h)
            return total * h / 3.0

        a = simpson(lambda f: math.sin(2.0 * math.pi * f) ** 2 * sinc2(f))
        b = simpson(sinc2)
        self.assertAlmostEqual(result['gradient_energy_ratio'], 2.0 * a * b, places=4)
        self.assertTrue(0.5 < result['gradient_energy_ratio'] < 0.8)


class EnergyTest(unittest.TestCase):
    def test_ramp_has_exactly_the_slope_squared(self):
        width = height = 40
        slope = 0.01
        image = make_image(width, height, lambda x, y: slope * x)
        classes = bytearray([it08.INTERIOR] * (width * height))
        for y in range(height):
            classes[y * width] = it08.SENTINEL  # inside the margin
        result = run2.class_gradient_energy(image, classes, width, height, margin=4)
        self.assertAlmostEqual(result['routed_interior']['mean'], slope * slope)
        self.assertEqual(result['routed_interior']['pixels'],
                         (width - 8) * (height - 8))
        self.assertEqual(result['sentinel']['pixels'], 0)
        flat = make_image(width, height, lambda x, y: 0.25)
        self.assertEqual(run2.class_gradient_energy(flat, classes, width, height,
                                                    4)['all']['mean'], 0.0)
        laplacian = run2.class_gradient_energy(image, classes, width, height, 4,
                                               'laplacian')
        self.assertAlmostEqual(laplacian['all']['mean'], 0.0)  # a ramp is harmonic


class ResampleTest(unittest.TestCase):
    def test_shift_is_exact_on_a_ramp(self):
        width = height = 16
        image = make_image(width, height, lambda x, y: float(x))
        shifted = run2.shift_image(image, width, height, 0.25, 0.0)
        for x in range(2, width - 2):
            self.assertAlmostEqual(shifted[8 * width + x], x - 0.25, places=4)
        integer = run2.shift_image(image, width, height, 2.0, 0.0)
        self.assertAlmostEqual(integer[8 * width + 8], 6.0, places=4)
        vertical = run2.shift_image(image, width, height, 0.0, 0.5)
        self.assertAlmostEqual(vertical[8 * width + 8], 8.0, places=4)

    def test_history_weights(self):
        weights = run2.ideal_history_weights(4, 0.5)
        self.assertAlmostEqual(sum(weights), 1.0)
        self.assertAlmostEqual(weights[1] / weights[0], 0.5)
        self.assertEqual(run2.ideal_history_weights(1, 0.9), [1.0])

    def test_axis_taps_match_the_shader(self):
        # Bilinear: two taps, linear in the fraction.
        self.assertEqual(run2.axis_taps(0.5, 'bilinear'), [(-1, 0.5), (0, 0.5)])
        # Catmull-Rom at a zero fraction takes the shader's exact-texel branch.
        self.assertEqual(run2.axis_taps(0.0, 'catmull_rom'), [(0, 1.0)])
        self.assertEqual(run2.axis_taps(-2.0, 'catmull_rom'), [(2, 1.0)])
        # At half a pixel the four Catmull-Rom weights are the standard
        # (-1/16, 9/16, 9/16, -1/16), summing to one with negative lobes.
        taps = run2.axis_taps(0.5, 'catmull_rom')
        self.assertEqual([n for n, _ in taps], [-2, -1, 0, 1])
        self.assertAlmostEqual(sum(w for _, w in taps), 1.0)
        self.assertAlmostEqual(taps[0][1], -1.0 / 16.0)
        self.assertAlmostEqual(taps[1][1], 9.0 / 16.0)
        self.assertAlmostEqual(taps[2][1], 9.0 / 16.0)
        self.assertAlmostEqual(taps[3][1], -1.0 / 16.0)
        with self.assertRaises(ValueError):
            run2.axis_taps(0.5, 'lanczos')

    def test_effective_kernel_tap_sets(self):
        kernel = run2.effective_kernel([(0.5, 0.0)], [1.0], 'bilinear')
        self.assertEqual(sorted(kernel), [(-1, 0), (0, 0)])
        self.assertAlmostEqual(kernel[(-1, 0)], 0.5)
        self.assertAlmostEqual(kernel[(0, 0)], 0.5)
        # A zero offset is one tap under both filters.
        self.assertEqual(run2.effective_kernel([(0.0, 0.0)], [1.0], 'bilinear'),
                         {(0, 0): 1.0})
        self.assertEqual(run2.effective_kernel([(0.0, 0.0)], [1.0], 'catmull_rom'),
                         {(0, 0): 1.0})
        for kind in ('bilinear', 'catmull_rom'):
            mixed = run2.effective_kernel([(0.0, 0.0), (0.25, -0.5)], [0.5, 0.5], kind)
            self.assertAlmostEqual(sum(mixed.values()), 1.0)

    def test_kernel_gradient_factor_bounds(self):
        # The identity kernel changes nothing; a two-tap average is a real
        # attenuation; both are between 0 and 1.
        self.assertAlmostEqual(run2.kernel_gradient_factor({(0, 0): 1.0}), 1.0, places=6)
        average = run2.kernel_gradient_factor({(0, 0): 0.5, (1, 0): 0.5})
        self.assertTrue(0.3 < average < 0.9)

    def test_supersampling_attenuates_a_sine_as_predicted(self):
        width = height = 64
        period = 8.0
        frequency = 1.0 / period
        image = make_image(width, height,
                           lambda x, y: 0.5 + 0.4 * math.sin(2.0 * math.pi * x / period))
        offsets = [(0.0, 0.0), (0.5, 0.0), (-0.5, 0.0), (0.25, 0.0)]
        weights = [0.25] * 4
        ideal = run2.ideal_supersampled(image, width, height, offsets, weights,
                                        'bilinear')
        # The prediction uses the discrete kernel the resampler really applies
        # (reconstruction taps included), which `effective_kernel` returns.
        kernel = run2.effective_kernel(offsets, weights, 'bilinear')
        transfer = sum(w * complex(math.cos(-2.0 * math.pi * frequency * nx),
                                   math.sin(-2.0 * math.pi * frequency * nx))
                       for (nx, _), w in kernel.items())
        classes = bytearray([it08.INTERIOR] * (width * height))
        raw = run2.class_gradient_energy(image, classes, width, height, 6)['all']['mean']
        out = run2.class_gradient_energy(ideal, classes, width, height, 6)['all']['mean']
        self.assertAlmostEqual(out / raw, abs(transfer) ** 2, places=3)
        mirrored = run2.ideal_supersampled(image, width, height,
                                           [(-dx, -dy) for dx, dy in offsets], weights,
                                           'bilinear')
        out_mirrored = run2.class_gradient_energy(mirrored, classes, width, height,
                                                  6)['all']['mean']
        self.assertAlmostEqual(out_mirrored / out, 1.0, places=3)
        # Catmull-Rom keeps more of the same frequency than bilinear does.
        catmull = run2.ideal_supersampled(image, width, height, offsets, weights,
                                          'catmull_rom')
        out_catmull = run2.class_gradient_energy(catmull, classes, width, height,
                                                 6)['all']['mean']
        self.assertGreater(out_catmull, out)

    def test_phase_average_is_the_plain_mean(self):
        width = height = 8
        first = make_image(width, height, lambda x, y: float(x))
        second = make_image(width, height, lambda x, y: float(x) + 2.0)
        average = run2.burst_phase_average([first, second])
        self.assertEqual(len(average), width * height)
        for i in range(width * height):
            self.assertAlmostEqual(average[i], first[i] + 1.0, places=5)
        weighted = run2.burst_phase_average([first, second], [0.25, 0.75])
        self.assertAlmostEqual(weighted[0], 1.5, places=5)

    def test_relative_offsets_put_the_current_frame_at_zero(self):
        table = [(0.0, -0.1), (-0.25, 0.2), (0.25, -0.4), (-0.375, 0.05)]
        offsets = run2.relative_offsets(table, 2)
        self.assertEqual(offsets[0], (0.0, 0.0))
        self.assertAlmostEqual(offsets[1][0], table[1][0] - table[2][0])
        self.assertAlmostEqual(offsets[1][1], table[1][1] - table[2][1])
        self.assertEqual(len(offsets), len(table))
        wrapped = run2.relative_offsets(table, 0)[1]
        self.assertAlmostEqual(wrapped[0], table[-1][0] - table[0][0])


class SpectralTest(unittest.TestCase):
    def test_band_fraction_localizes_a_known_frequency(self):
        width = height = 64
        fine = make_image(width, height,
                          lambda x, y: 0.5 + 0.4 * math.sin(2.0 * math.pi * x * 0.4))
        coarse = make_image(width, height,
                            lambda x, y: 0.5 + 0.4 * math.sin(2.0 * math.pi * x * 0.02))
        # 0.4 cycles/px is above a quarter of Nyquist (0.125); 0.02 is far below.
        self.assertGreater(run2.spectral_band_fraction(fine, width, (0, 0), 64,
                                                       0.25)['high_fraction'], 0.95)
        self.assertLess(run2.spectral_band_fraction(coarse, width, (0, 0), 64,
                                                    0.25)['high_fraction'], 0.05)


class EdgeSpreadTest(unittest.TestCase):
    def test_step_and_blur(self):
        width = height = 48
        step = make_image(width, height, lambda x, y: 0.9 if x >= 24 else 0.1)
        classes = bytearray([it08.INTERIOR] * (width * height))
        sharp = run2.edge_spread(step, classes, width, height, it08.INTERIOR, margin=8)
        self.assertEqual(sharp['status'], 'evaluated')
        self.assertLess(sharp['rise_10_90_px'], 1.2)

        def blurred(x, y):
            return sum(step[y * width + min(max(x + k, 0), width - 1)]
                       for k in (-1, 0, 1)) / 3.0

        soft = run2.edge_spread(make_image(width, height, blurred), classes, width,
                                height, it08.INTERIOR, margin=8)
        self.assertEqual(soft['status'], 'evaluated')
        self.assertGreater(soft['rise_10_90_px'], sharp['rise_10_90_px'])
        self.assertLess(soft['mtf50_cycles_per_px'], sharp['mtf50_cycles_per_px'])
        flat = make_image(width, height, lambda x, y: 0.5)
        self.assertEqual(run2.edge_spread(flat, classes, width, height,
                                          it08.INTERIOR)['status'],
                         'insufficient_edges')


class MeshCacheTextTest(Base):
    def test_text_without_mesh_cache_lines(self):
        # Iterations 10-12 hit `TypeError: unsupported format string passed to
        # NoneType.__format__` here: the frequency is known, so the seconds are
        # 0.0, but with no calls the per-call mean is None.
        lines = ['telemetry_start schema=1 qpc_frequency=1000 qpc=0 anchor=proxy_initialize',
                 frame_line(60)]
        report = run2.mesh_cache_report(self.scan(lines))
        self.assertEqual(report['native_seconds'], 0.0)
        self.assertIsNone(report['mean_native_ms'])
        text = run2.render_mesh_cache(report)
        self.assertEqual(text[0], '== mesh adjacency cache')
        self.assertTrue(any('no calls' in line for line in text))

    def test_text_with_mesh_cache_lines(self):
        report = run2.mesh_cache_report(self.scan(MeshCacheTest.LINES))
        text = run2.render_mesh_cache(report)
        self.assertTrue(any('200.00 ms/call' in line for line in text))


class BurstTest(Base):
    @staticmethod
    def record(cut, rotation):
        return {'cut_median_px': str(cut), 'camera_rotation_deg': str(rotation),
                'cut': '0', 'cut_missing': '0.0', 'camera_policy': '2',
                'camera_cut': '0'}

    def test_motion_bands(self):
        still = run2.classify_burst([self.record(0.03, 0.5), self.record(0.22, 0.5)])
        self.assertEqual(still['motion'], 'stationary')
        slow = run2.classify_burst([self.record(1.7, 0.8), self.record(8.6, 1.2)])
        self.assertEqual(slow['motion'], 'slow')
        turning = run2.classify_burst([self.record(25.7, 2.2), self.record(78.4, 5.8)])
        self.assertEqual(turning['motion'], 'turning')
        self.assertAlmostEqual(turning['cut_median_px_peak'], 78.4)

    def test_grouping_consecutive_records(self):
        frames = [{'frame': str(f)} for f in (60, 120, 629, 630, 631, 632, 900, 903, 904)]
        groups = run2.burst_frames(frames)
        self.assertEqual([[run2.number(r['frame']) for r in g] for g in groups],
                         [[629, 630, 631, 632], [903, 904]])

    def test_grouping_keeps_a_periodic_record_out_of_an_adjacent_burst(self):
        # Iteration 12, run 10: burst 9536-9539 followed by the frame_log record
        # of frame 9540 (9540 % 60 == 0), which wrote no readback. Grouped by
        # frame number alone it joined the burst and the burst reported
        # missing_readbacks; restricted to the frames with readbacks it does not.
        frames = [{'frame': str(f)} for f in (9480, 9536, 9537, 9538, 9539, 9540, 9600)]
        merged = run2.burst_frames(frames)
        self.assertEqual([[run2.number(r['frame']) for r in g] for g in merged],
                         [[9536, 9537, 9538, 9539, 9540]])
        split = run2.burst_frames(frames, readback_frames={9536, 9537, 9538, 9539})
        self.assertEqual([[run2.number(r['frame']) for r in g] for g in split],
                         [[9536, 9537, 9538, 9539]])
        # No readback information at all keeps the old behaviour.
        self.assertEqual(run2.burst_frames(frames, readback_frames=None), merged)

    def test_scan_collects_the_readback_frames(self):
        lines = [frame_line(9539), frame_line(9540),
                 'motion_output_readback device=1 frame=9539 file=motion_1_9539.rgba32f '
                 'width=1280 height=768 format=rgba32f_row_major result=00000000 bytes=15728640',
                 'motion_output_readback device=2 frame=9540 file=motion_2_9540.rgba32f '
                 'width=1280 height=768 format=rgba32f_row_major result=00000000 bytes=15728640']
        scan = self.scan(lines)
        self.assertEqual(scan['readback_frames'], {9539})


if __name__ == '__main__':
    unittest.main()
