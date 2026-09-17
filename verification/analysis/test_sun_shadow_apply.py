"""Host contracts of the sun-shadow apply quad's CPU twin
(verification/probe/sun_shadow_apply.py; docs/architecture/
legacy-sun-application.md, section 3.3): the far map is the identity, a map
at depth 0 gives 1 - s on every valid pixel and leaves sentinel, share-free
and off-cascade pixels at 1, the exponent applies only to shadowed pixels,
the quad derivative helper, the FP16 code spacing and the analytic box hit.
No Wine, no game."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import numpy as np  # noqa: E402
import sun_shadow_apply as apply  # noqa: E402

# A camera at the origin looking down +z with the sun straight down: view
# (x, y, z) -> sun NDC (x / 4, (z - 6) / 4), depth (4 - y) / 8 (cascade centre at
# (0, 0, 6), half-extent 4, depth half-range 4, sun travelling -y).
PARAMS = dict(m00=2.0, m11=2.0, m20=0.0, m21=0.0, m22=50.0 / 49.0, m32=-50.0 / 49.0,
              rows=(.25, 0, 0, 0, 0, 0, .25, -1.5, 0, -.125, 0, .5), jitter_index=0, exponent=1.0,
              bias_constant=.003, bias_max=.01, planar_step=.05)


def receivers(width=8, height=8, depth=6.0):
    """Every pixel a receiver at view depth `depth` (device z/w by the AO law) with share 0.5; column 3 share-free, row 0 sentinel."""
    d = np.full((height, width), PARAMS['m22'] + PARAMS['m32'] / depth)
    s = np.full((height, width), .5)
    s[:, 3] = 0.0
    d[0, :] = -1.0; s[0, :] = 0.0
    return d, s


class ExpectedFactor(unittest.TestCase):
    def test_far_map_is_identity(self):
        d, s = receivers()
        out = apply.expected_factor(d, s, np.ones((64, 64)), PARAMS)
        self.assertTrue(np.all(out['factor'] == 1.0))
        self.assertEqual(int(np.count_nonzero(out['valid'])), 7 * 7)

    def test_zero_map_is_one_minus_share(self):
        d, s = receivers()
        out = apply.expected_factor(d, s, np.zeros((64, 64)), PARAMS)
        valid = out['valid']
        self.assertEqual(int(np.count_nonzero(valid)), 7 * 7)
        self.assertTrue(np.all(out['f'][valid] == 0.0))
        self.assertTrue(np.allclose(out['factor'][valid], .5))
        self.assertTrue(np.all(out['factor'][~valid] == 1.0))
        exponent = dict(PARAMS, exponent=1 / 2.2)
        out2 = apply.expected_factor(d, s, np.zeros((64, 64)), exponent)
        self.assertTrue(np.allclose(out2['factor'][valid], .5 ** (1 / 2.2)))
        self.assertTrue(np.all(out2['factor'][~valid] == 1.0))

    def test_off_cascade_receiver_is_untouched(self):
        d, s = receivers(depth=40.0)  # view z 40 -> sun depth (4 - y) / 8 with y ~ +-10: outside [0, 1]
        out = apply.expected_factor(d, s, np.zeros((64, 64)), PARAMS)
        self.assertFalse(np.any(out['valid'][1:, :2]))

    def test_half_texel_lookup_is_unbiased_against_a_d3d9_rasterized_map(self):
        # A map as the replay's rasterizer fills it: texel i holds the sample at map position i / N, covered
        # (depth 0) when that position is left of the caster's edge e. 64 receivers across 12 texels of a
        # 16-texel map; the f >= 0.5 crossing against e, averaged over ten sub-texel edge positions, is the
        # lookup's bias: 0 for the half-texel rule (nearest texel round(u N)), +0.5 texel for the former floor.
        size = 16
        d, s = receivers(width=64, height=8)
        s[:, :] = .5; d[0, :] = PARAMS['m22'] + PARAMS['m32'] / 6.0
        position = (.5 + .375 * ((np.arange(64) + .5) / 64 * 2 - 1)) * size  # map position of every column in texels
        bias = {}
        for legacy in (False, True):
            offsets = []
            for edge in np.linspace(6.05, 6.95, 10):
                sun_map = np.where(np.arange(size)[None, :] < edge, 0.0, 1.0).repeat(size, 0).reshape(size, size)
                f = apply.expected_factor(d, s, sun_map, PARAMS, legacy_floor=legacy)['f'][4]
                crossing = position[int(np.argmax(f >= .5))] - .5 * (position[1] - position[0])
                offsets.append(crossing - edge)
            bias[legacy] = float(np.mean(offsets))
        self.assertLess(abs(bias[False]), .25, bias); self.assertGreater(bias[True], .35, bias)

    def test_quad_derivative(self):
        value = np.arange(16, dtype=np.float64).reshape(4, 4) * 3.0
        self.assertTrue(np.all(apply._quad_derivative(value, 1) == 3.0))
        self.assertTrue(np.all(apply._quad_derivative(value, 0) == 12.0))
        self.assertTrue(np.all(apply._coarse(value, 1) == 3.0))

    def test_fp16_code(self):
        self.assertEqual(float(apply.fp16_code(np.array(1.0))), 2.0 ** -10)
        self.assertEqual(float(apply.fp16_code(np.array(.3))), 2.0 ** -12)
        self.assertEqual(float(apply.fp16_code(np.array(0.0))), 2.0 ** -24)

    def test_box_hit(self):
        o = [np.array([0.0, 5.0]), np.array([5.0, 5.0]), np.array([0.0, 0.0])]
        direction = [np.zeros(2), np.full(2, -1.0), np.zeros(2)]
        t = apply._hit(o, direction, (-1, 0, -1), (1, 2, 1))
        self.assertAlmostEqual(float(t[0]), 3.0)  # the box top at y = 2
        self.assertAlmostEqual(float(t[1]), 5.0)  # the plane beside the box


class RunInputs(unittest.TestCase):
    """The capture-frame `sun_shadow_apply_params` line (motion_output.cpp
    run_sun_shadow_apply) and the reconstruction from the older lines feed the
    same params dict; the report on real-shaped inputs is finite."""
    ROWS = '-0.00333094015,0.0012562772,-0.00182387815,0.233906448,0.00183137401,0.00341484277,-0.000992501737,0.127228171,0.000304036046,-0.000405657483,-0.000834669627,0.606838644'
    LINE = ('sun_shadow_apply_params device=1 frame=8979 m00=0.800000012 m11=1.33333302 jitter_x=-0.250000 jitter_y=0.166667 m20=-0.000390625'
            ' m21=-0.000434028637 m22=1.00000298 m32=-6.00001812 texel=0.0009765625 bias=0.00100000005 bias_max=0.00999999978 planar_step=0.0500000007'
            ' exponent=1.000000 jitter_index=1 map=1024 width=1280 height=768 rows=' + ROWS)

    def test_params_line(self):
        params, extra = apply.parse_apply_params(apply.line_fields(self.LINE))
        self.assertEqual(set(params), {'m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'bias_constant', 'bias_max', 'planar_step', 'rows', 'jitter_index'})
        self.assertAlmostEqual(params['m20'], -2 * .25 / 1280); self.assertAlmostEqual(params['m21'], -2 * .166667 / 768, places=7)
        self.assertEqual((params['jitter_index'], params['bias_constant'], len(params['rows'])), (1, .00100000005, 12))
        self.assertEqual((extra['width'], extra['height'], extra['map'], extra['jitter_px']), (1280, 768, 1024, (-.25, .166667)))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(self.LINE.replace('texel=0.0009765625', 'texel=0.001')))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(self.LINE.rsplit(',', 1)[0]))

    def test_resolved_bias(self):
        # The world-unit bias (sun_shadow_apply_bias): the default resolves to
        # exactly the former float32 constants at 250 / 512 / 1024, and the
        # texel term scales with the map's world texel.
        default = apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, 250.0, 512.0, 1024)
        self.assertEqual((default['bias_constant'], default['bias_max']), (float(np.float32(.001)), float(np.float32(.01))))
        self.assertEqual(default['texel_world'], .48828125)
        wide = apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, 1000.0, 2048.0, 2048)
        self.assertAlmostEqual(wide['texel_world'], .9765625)
        self.assertAlmostEqual(wide['bias_constant'] * 4096, apply.BIAS_UNITS_DEFAULT + .9765625, places=5)
        self.assertAlmostEqual(wide['bias_max'] * 4096, apply.BIAS_CLAMP_TEXELS * .9765625, places=5)
        narrow = apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, 32.0, 64.0, 512)  # the live fixture's cascade: clamp 2.62 units, under its 4-unit occluder gap
        self.assertAlmostEqual(narrow['bias_max'] * 128, apply.BIAS_CLAMP_TEXELS * .125, places=5)
        four = apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, 1000.0, 2048.0, 2048, 4.0)  # --sun-shadow-bias-clamp-texels 4 at the wide setting
        self.assertAlmostEqual(four['bias_max'] * 4096, 4 * .9765625, places=5); self.assertEqual(four['bias_constant'], wide['bias_constant'])
        line4 = self.LINE.replace(' rows=', ' bias_units=0.53571875 clamp_texels=4 texel_world=0.48828125 extent=250 depth_half=512 rows=').replace('bias_max=0.00999999978', 'bias_max=0.0019073486')
        params4, extra4 = apply.parse_apply_params(apply.line_fields(line4))
        self.assertEqual((extra4['clamp_texels'], params4['bias_max']), (4.0, .0019073486))
        self.assertGreater(apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, 1000.0, 2048.0, 1024)['bias_constant'], wide['bias_constant'])
        line = self.LINE.replace(' rows=', ' bias_units=0.53571875 texel_world=0.48828125 extent=250 depth_half=512 rows=')
        params, extra = apply.parse_apply_params(apply.line_fields(line))
        self.assertEqual((extra['bias_units'], extra['extent'], extra['depth_half'], extra['texel_world']), (.53571875, 250.0, 512.0, .48828125))
        self.assertEqual(params['bias_constant'], .00100000005)
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(line.replace('extent=250', 'extent=1000')))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(line.replace('bias=0.00100000005', 'bias=0.003')))
        with self.assertRaises(KeyError):
            apply.parse_apply_params(apply.line_fields(line.replace(' extent=250', '')))
        old_params, old_extra = apply.parse_apply_params(apply.line_fields(self.LINE))  # a pre-tunable line still parses
        self.assertEqual(old_params['bias_constant'], .00100000005); self.assertNotIn('bias_units', old_extra)

    def test_reconstruction_matches_params_line(self):
        camera = apply.line_fields('camera_state device=1 frame=8979 p00=0.8 p11=1.333333 p20=0 p21=0')
        frame = apply.line_fields('motion_output_frame device=1 frame=8979 jitter=1 jitter_index=1 jitter_x=-0.250000 jitter_y=0.166667')
        basis = apply.line_fields('shadow_replay_map_basis device=1 frame=8979 size=1024 valid=1 extent=250 depth_half=512 rows=' + self.ROWS)
        expected, _ = apply.parse_apply_params(apply.line_fields(self.LINE))
        got = apply.reconstruct_params(camera, frame, basis, 1280, 768)
        for key in expected:
            if key == 'rows':
                self.assertEqual(got[key], expected[key])
            else:
                self.assertAlmostEqual(got[key], expected[key], places=6, msg=key)

    def test_frame_params_prefers_the_line(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'session.log'
            log.write_text('camera_state device=1 frame=8979 p00=0.8 p11=1.333333 p20=0 p21=0\n'
                           'motion_output_frame device=1 frame=8979 jitter=1 jitter_index=1 jitter_x=-0.250000 jitter_y=0.166667\n'
                           'motion_output_depth_readback device=1 frame=8979 file=depth_1_8979.rg32f width=1280 height=768\n'
                           'shadow_replay_map_basis device=1 frame=8979 size=1024 valid=1 rows=' + self.ROWS + '\n')
            params, extra, source = apply.frame_params(log, 8979)
            self.assertEqual((source, extra['map'], extra['width']), ('reconstructed', 1024, 1280))
            with self.assertRaises(ValueError):
                apply.frame_params(log, 8980)
            log.write_text(log.read_text() + self.LINE + '\n')
            params2, extra2, source2 = apply.frame_params(log, 8979)
            self.assertEqual(source2, 'params_line')
            self.assertAlmostEqual(params2['m21'], params['m21'], places=7)

    def test_frame_report(self):
        d, s = receivers()
        luminance = np.full(d.shape, .4)
        report = apply.frame_report(d, s, np.zeros((64, 64)), PARAMS, luminance, region=(0, 8, 0, 8), columns=8, lines=8)
        self.assertEqual(report['valid'], 49)
        self.assertEqual(report['f_below_0_9'], 1.0)
        self.assertAlmostEqual(report['factor_mean'], .5)
        self.assertEqual(len(report['mask']), 8)
        self.assertTrue(all(c in ' #' for row in report['mask'] for c in row))
        self.assertEqual(report['darkening'].get('control'), None)  # no lit pixel to pair with
        far = apply.frame_report(d, s, np.ones((64, 64)), PARAMS, luminance)
        self.assertEqual((far['f_below_0_9'], far['factor_mean']), (0.0, 1.0))
        # Lit receivers pair with lit neighbours at ratio 1 and nothing is shadowed (with the half-texel lookup the
        # synthetic receivers sit on texel centres; under the former floor rule they sat on texel boundaries, all ambiguous).
        self.assertEqual(set(far['darkening']), {'control'}); self.assertEqual(far['darkening']['control']['ratio_p25_p50_p75'], [1.0, 1.0, 1.0])
        legacy = apply.frame_report(d, s, np.ones((64, 64)), PARAMS, luminance, legacy_floor=True)
        self.assertEqual(legacy['darkening'], {})


def cascade_rows(extent):
    """PARAMS' sun space at another half-extent: NDC (x / E, (z - 6) / E), depth (4 - y) / 8."""
    return (1.0 / extent, 0, 0, 0, 0, 0, 1.0 / extent, -6.0 / extent, 0, -.125, 0, .5)


def cascade_params(cascades):
    shared = {k: PARAMS[k] for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'jitter_index', 'exponent', 'planar_step')}
    return dict(shared, cascades=[dict(rows=cascade_rows(extent), bias_constant=.003, bias_max=.01, valid=valid) for extent, valid in cascades])


class CascadeFactor(unittest.TestCase):
    """The cascade twin (docs/architecture/shadow-cascades.md, section 2). The
    8 x 8 receivers at view depth 6 lie at view x = +-0.375, 1.125, 1.875, 2.625:
    with a 2-unit cascade 0 that is sun |x| = 0.1875, 0.5625 (core), 0.9375
    (band, t = 0.875) and 1.3125 (beyond the 0.95 margin: the next cascade)."""
    COLUMN_REACH = (1.3125, .9375, .5625, .1875, .1875, .5625, .9375, 1.3125)

    def test_single_cascade_core_equals_the_single_map_twin(self):
        d, s = receivers()
        single = apply.expected_factor(d, s, np.zeros((64, 64)), PARAMS)
        out = apply.expected_factor_cascades(d, s, [np.zeros((64, 64))], cascade_params([(4.0, True)]))
        self.assertTrue(np.array_equal(out['factor'], single['factor']) and np.array_equal(out['valid'], single['valid']))
        self.assertTrue(np.all(out['selected'][1:, :] == 0))

    def test_selection_band_and_next_cascade(self):
        d, s = receivers()
        out = apply.expected_factor_cascades(d, s, [np.zeros((64, 64)), np.ones((64, 64))], cascade_params([(2.0, True), (8.0, True)]))
        self.assertEqual([int(v) for v in out['selected'][4]], [1, 0, 0, 0, 0, 0, 0, 1])  # the first cascade containing the pixel
        f = out['f'][4]
        self.assertTrue(np.allclose(f, [1.0, .875, 0.0, 0.0, 0.0, 0.0, .875, 1.0]))      # shadowed core, lerp(0, 1, t) in the band, the lit next cascade
        self.assertTrue(np.allclose(out['factor'][4], [1.0, .9375, .5, 1.0, .5, .5, .9375, 1.0]))  # 1 - (1 - f) s with s = 0.5; column 3 is share-free
        swapped = apply.expected_factor_cascades(d, s, [np.ones((64, 64)), np.zeros((64, 64))], cascade_params([(2.0, True), (8.0, True)]))
        self.assertTrue(np.allclose(swapped['f'][4], [0.0, .125, 1.0, 1.0, 1.0, 1.0, .125, 0.0]))  # monotone through the band from the other side

    def test_last_cascade_fades_to_lit(self):
        d, s = receivers()
        out = apply.expected_factor_cascades(d, s, [np.zeros((64, 64))], cascade_params([(2.0, True)]))
        self.assertTrue(np.allclose(out['f'][4], [1.0, .875, 0.0, 0.0, 0.0, 0.0, .875, 1.0]))
        self.assertEqual([bool(v) for v in out['valid'][4]], [False, True, True, False, True, True, True, False])  # beyond the margin: no cascade, untouched

    def test_absent_cascade_is_lit_and_keeps_its_pixels(self):
        d, s = receivers()
        out = apply.expected_factor_cascades(d, s, [None, np.zeros((64, 64))], cascade_params([(2.0, False), (8.0, True)]))
        # Cascade 0's core pixels stay lit (the coarser map's shadow does not reappear); its band blends into cascade 1; beyond it cascade 1 shadows.
        self.assertTrue(np.allclose(out['f'][4], [0.0, .125, 1.0, 1.0, 1.0, 1.0, .125, 0.0]))
        far_absent = apply.expected_factor_cascades(d, s, [np.zeros((64, 64)), None], cascade_params([(2.0, True), (8.0, False)]))
        self.assertTrue(np.allclose(far_absent['f'][4], [1.0, .875, 0.0, 0.0, 0.0, 0.0, .875, 1.0]))  # an absent far cascade: lit, and the band fades into it

    def test_cascade_params_line(self):
        rows = ','.join('%.9g' % v for v in cascade_rows(250.0))
        resolved = [apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, e, .5 * (15000.0 + b), 4096) for e, b in ((250.0, 512.0), (1500.0, 3000.0))]
        per = ''.join(' valid%d=%d map%d=4096 map_frame%d=%d bias%d=%.9g bias_max%d=%.9g texel_world%d=%.9g extent%d=%g depth_light%d=15000 depth_behind%d=%g rows%d=%s'
                      % (c, 1 - c, c, c, 77 - c if c == 0 else -1, c, resolved[c]['bias_constant'], c, resolved[c]['bias_max'], c, resolved[c]['texel_world'], c, e, c, c, b, c, rows)
                      for c, (e, b) in enumerate(((250.0, 512.0), (1500.0, 3000.0))))
        line = ('sun_shadow_apply_params device=1 frame=77 m00=0.8 m11=1.33333337 jitter_x=-0.250000 jitter_y=0.166667 m20=-0.000390625 m21=-0.000434028637 m22=1.00000298 m32=-6.00001812'
                ' planar_step=0.0500000007 exponent=1.000000 jitter_index=1 width=1280 height=768 bias_units=0.53571875 clamp_texels=20.97152 cascades=2 margin=0.949999988 band=0.100000001' + per)
        params, extra = apply.parse_apply_params(apply.line_fields(line))
        self.assertEqual([c['valid'] for c in params['cascades']], [True, False])
        self.assertEqual([(c['map'], c['map_frame'], c['extent'], c['depth_behind']) for c in extra['cascades']], [(4096, 77, 250.0, 512.0), (4096, -1, 1500.0, 3000.0)])
        self.assertAlmostEqual(params['margin'], .95, places=6); self.assertAlmostEqual(params['band'], .10, places=6)
        with self.assertRaises(ValueError):  # a printed bias that is not the law's at that cascade's texel and range
            apply.parse_apply_params(apply.line_fields(line.replace('bias1=%.9g' % resolved[1]['bias_constant'], 'bias1=0.001')))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(line.replace('rows1=' + rows, 'rows1=' + rows.rsplit(',', 1)[0])))


if __name__ == '__main__':
    unittest.main()
