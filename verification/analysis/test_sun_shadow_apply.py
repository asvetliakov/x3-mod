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
        self.assertEqual(far['darkening'], {})  # every receiver of the flat synthetic scene is ambiguous: no strict pair


if __name__ == '__main__':
    unittest.main()
