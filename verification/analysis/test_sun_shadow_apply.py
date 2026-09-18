"""Host contracts of the sun-shadow apply quad's CPU twin
(verification/probe/sun_shadow_apply.py; docs/architecture/
legacy-sun-application.md, section 3.3): the far map is the identity, a map
at depth 0 gives 1 - s on every valid pixel and leaves sentinel, share-free
and off-cascade pixels at 1, the exponent applies only to shadowed pixels,
the quad derivative helper, the FP16 code spacing, the analytic box hit and
the receiver-depth precision of the two RT2 encodings (docs/architecture/
shadow-receiver-depth.md, section 4). No Wine, no game."""
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


def d3d9_plane_scene(width=32, height=32, size=512, slope_x=.5, slope_z=.5):
    """A tilted plane y = slope_x x + slope_z (z - 6) - 1 rasterised as D3D9
    does: RT2 texel (i, j) holds the depth along the ray of NDC (2 i / W - 1,
    1 - 2 j / H) (jitter 0), the map texel (a, b) the plane's sun depth at map
    position (a, b) / N (PARAMS' sun space). Rays leaving the cascade's box
    are sentinels."""
    m00, m11, m22, m32 = PARAMS['m00'], PARAMS['m11'], PARAMS['m22'], PARAMS['m32']
    plane_y = lambda x, z: slope_x * x + slope_z * (z - 6.0) - 1.0
    i = np.arange(width, dtype=np.float64)[None, :].repeat(height, 0)
    j = np.arange(height, dtype=np.float64)[:, None].repeat(width, 1)
    dx, dy = (2.0 * i / width - 1.0) / m00, (1.0 - 2.0 * j / height) / m11
    denominator = dy - slope_x * dx - slope_z
    t = np.where(np.abs(denominator) > 1e-9, (-1.0 - 6.0 * slope_z) / np.where(np.abs(denominator) > 1e-9, denominator, 1.0), -1.0)
    x, y = dx * t, dy * t
    inside = (t > 2.5) & (t < 9.5) & (np.abs(x) < 3.9) & (np.abs(y) < 3.9)
    d = np.where(inside, m22 + m32 / np.where(inside, t, 1.0), -1.0)
    s = np.where(inside, .5, 0.0)
    a = np.arange(size, dtype=np.float64)[None, :].repeat(size, 0)
    b = np.arange(size, dtype=np.float64)[:, None].repeat(size, 1)
    sun_map = np.clip((4.0 - plane_y(4.0 * (2.0 * a / size - 1.0), 6.0 + 4.0 * (1.0 - 2.0 * b / size))) / 8.0, 0.0, 1.0)
    return d, s, sun_map


class PixelCentre(unittest.TestCase):
    """The receiver convention (quad_vertex_program.h, quad_pixel_centre_m20/m21;
    directional-shadows.md, "Run 39 A (run115) diagnosis"): RT2 texel (i, j)
    holds the depth at the D3D9 pixel centre 2 i / W - 1, the program
    reconstructs at (i + 1/2) / W * 2 - 1, so the latch carries (+1/W, -1/H)."""

    def test_pixel_centre_terms(self):
        self.assertEqual(apply.pixel_centre_terms(1280, 768), (1.0 / 1280, -1.0 / 768))

    def test_d3d9_rasterised_tilted_plane_is_lit_only_with_the_pixel_centre_term(self):
        # 32 x 32 receivers on a plane sloped in x and z, a 512-texel map of the same plane (half a texel of quantisation
        # is 0.0078 units of slope, under a quarter of the 0.024-unit bias). With the term the twin's own-surface residual
        # is within bias / 4 and every unambiguous receiver is lit; the pre-fix latch (m20 = m21 = 0 against a D3D9-
        # rasterised RT2) puts the receiver z / (W m00) = 0.09 units beside the sampled surface: 0.14 units of residual
        # along the sun, over the bias, and the plane shadows itself.
        d, s, sun_map = d3d9_plane_scene()
        centre = apply.pixel_centre_terms(*d.shape[::-1])
        bias_units = PARAMS['bias_constant'] * 8.0
        results = {}
        for label, (m20, m21) in (('fixed', centre), ('legacy', (0.0, 0.0))):
            params = dict(PARAMS, m20=m20, m21=m21)
            one_cascade = dict(params, cascades=[dict(rows=PARAMS['rows'], bias_constant=PARAMS['bias_constant'], bias_max=PARAMS['bias_max'], valid=True)])
            single = apply.expected_factor(d, s, sun_map, params)
            cascade = apply.expected_factor_cascades(d, s, [sun_map], one_cascade)
            good = single['valid'] & ~single['ambiguous'] & cascade['valid'] & ~cascade['ambiguous'] & (cascade['band'][0] == 0.0)  # the core: no fade to lit
            self.assertGreater(int(np.count_nonzero(good)), 200)
            residual = apply.own_surface_residual(cascade, [sun_map], one_cascade, [8.0])[0]
            results[label] = dict(lit=float(np.mean(single['f'][good] == 1.0)), lit_cascades=float(np.mean(cascade['f'][good] == 1.0)), median=residual['median'], over_bias=residual['over_bias'])
        self.assertLess(abs(results['fixed']['median']), bias_units / 4, results)
        self.assertEqual((results['fixed']['lit'], results['fixed']['lit_cascades']), (1.0, 1.0), results)
        self.assertGreater(results['legacy']['median'], bias_units, results)          # the half-pixel receiver error, over the bias
        self.assertGreater(results['legacy']['over_bias'], .9, results)
        self.assertLess(max(results['legacy']['lit'], results['legacy']['lit_cascades']), .5, results)  # the plane shadows itself
        line = apply.own_surface_residual_line([dict(cascade=0, bias_units=bias_units, own_surface=1, owned=1, median=results['legacy']['median'], over_bias=1.0, median_over_half_bias=True), None])
        self.assertTrue(line.startswith('median own-surface residual') and line.count('OVER') == 1 and line.endswith('absent'), line)

    def test_analytic_shadow_is_independent_of_the_latch(self):
        # The analytic reference evaluates the receiver at the D3D9 pixel centre from the raster's own jitter terms, so a
        # latch that reconstructs the receiver beside the sampled point (any m20/m21) changes nothing in it, while the
        # raster's jitter does: the receiver error cannot cancel between the twin and the reference.
        d, s = receivers(width=32, height=32)
        scene = dict(camera=(0.0, 0.0, 0.0), cam_right=(1.0, 0.0, 0.0), cam_up=(0.0, 1.0, 0.0), cam_forward=(0.0, 0.0, 1.0), sun=(0.0, -1.0, 0.0),
                     right=(1.0, 0.0, 0.0), up=(0.0, 0.0, 1.0), box=(-1.0, -6.0, 5.0, 1.0, -4.0, 7.0), boxes=[(-1.0, -6.0, 5.0, 1.0, -4.0, 7.0)], extent=4.0,
                     raster_m20=0.0, raster_m21=0.0)
        base = apply.analytic_shadow(d, s, PARAMS, scene, 64)
        shifted_latch = apply.analytic_shadow(d, s, dict(PARAMS, m20=1.0 / 32, m21=-1.0 / 32), scene, 64)
        self.assertTrue(np.array_equal(base['lit'], shifted_latch['lit']))
        self.assertTrue(np.allclose(base['world'][0], shifted_latch['world'][0]))
        jittered = apply.analytic_shadow(d, s, PARAMS, dict(scene, raster_m20=2.0 * .5 / 32), 64)
        self.assertFalse(np.allclose(base['world'][0], jittered['world'][0]))
        # Pixel (i, j)'s receiver lies on the ray of NDC (2 i / W - 1, 1 - 2 j / H): column 16 at depth 6 is x = 0 exactly.
        self.assertAlmostEqual(float(base['world'][0][4, 16]), 0.0)
        self.assertAlmostEqual(float(base['world'][0][4, 17]), 2.0 / 32 * 6.0 / PARAMS['m00'])
        self.assertGreater(int(np.count_nonzero(base['on_plane'])), 0)

    def test_fixture_lines_carry_the_raster_law(self):
        fields = dict(camera='1,2,3', cam_right='1,0,0', cam_up='0,1,0', cam_forward='0,0,1', sun='0,1,0', right='1,0,0', up='0,0,1', forward='0,-1,0', center='0,0,6',
                      extent='5', depth_half='8', box='-1,0,-1,1,2,1', m00='2', m11='2', m20='0.0078125', m21='-0.0078125', m22='1', m32='-1', exponent='1',
                      bias_constant='0.003', bias_max='0.01', planar_step='0.05', rows=','.join('0' for _ in range(12)), jitter_index='0',
                      raster_m20='0', raster_m21='0', legacy_latch='0')
        params, scene = apply.parse_params(fields)
        self.assertEqual((scene['raster_m20'], scene['raster_m21'], scene['legacy_latch']), (0.0, 0.0, False))
        self.assertEqual(apply.parse_params(dict(fields, legacy_latch='1'))[1]['legacy_latch'], True)
        with self.assertRaises(KeyError):  # an older fixture line without the raster's law is not accepted (the reference would depend on the latch)
            apply.parse_params({k: v for k, v in fields.items() if k != 'raster_m20'})


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
        self.assertEqual(set(params), {'m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'bias_constant', 'bias_max', 'planar_step', 'rows', 'jitter_index', 'depth_encoding'})
        self.assertEqual(params['depth_encoding'], 'device')  # a line before the wide RT2
        self.assertEqual(apply.parse_apply_params(apply.line_fields(self.LINE + ' depth_encoding=linear'))[0]['depth_encoding'], 'linear')
        self.assertAlmostEqual(params['m20'], -2 * .25 / 1280); self.assertAlmostEqual(params['m21'], -2 * .166667 / 768, places=7)
        self.assertEqual((params['jitter_index'], params['bias_constant'], len(params['rows'])), (1, .00100000005, 12))
        self.assertEqual((extra['width'], extra['height'], extra['map'], extra['jitter_px']), (1280, 768, 1024, (-.25, .166667)))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(self.LINE.replace('texel=0.0009765625', 'texel=0.001')))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(self.LINE.rsplit(',', 1)[0]))

    def test_latch_law_fields(self):
        # Since 2026-09-18 the line names the raster latch and pixel_centre=1;
        # an older line has neither (the CLI's --add-pixel-centre supplies the
        # term then); a line whose m20/m21 do not carry the term it claims is refused.
        old_params, old_extra = apply.parse_apply_params(apply.line_fields(self.LINE))
        self.assertFalse(old_extra['pixel_centre']); self.assertNotIn('raster_m20', old_extra)
        centre = apply.pixel_centre_terms(1280, 768)
        m20, m21 = -2 * .25 / 1280 + centre[0], -2 * .166667 / 768 + centre[1]
        new = self.LINE.replace('m20=-0.000390625 m21=-0.000434028637', 'm20=%.9g m21=%.9g' % (m20, m21)) + ' raster_m20=-0.000390625 raster_m21=-0.000434028637 pixel_centre=1'
        params, extra = apply.parse_apply_params(apply.line_fields(new))
        self.assertTrue(extra['pixel_centre']); self.assertEqual((extra['raster_m20'], extra['raster_m21']), (-0.000390625, -0.000434028637))
        self.assertAlmostEqual(params['m20'] - extra['raster_m20'], centre[0], places=9); self.assertAlmostEqual(params['m21'] - extra['raster_m21'], centre[1], places=9)
        with self.assertRaises(ValueError):  # claims the term but m20 is the raster's
            apply.parse_apply_params(apply.line_fields(self.LINE + ' raster_m20=-0.000390625 raster_m21=-0.000434028637 pixel_centre=1'))
        with self.assertRaises(ValueError):  # the claim without the raster fields
            apply.parse_apply_params(apply.line_fields(self.LINE + ' pixel_centre=1'))
        cascade = ('sun_shadow_apply_params device=1 frame=2 m00=0.800000012 m11=1.33333302 jitter_x=0 jitter_y=0 m20=%.9g m21=%.9g m22=1.00000298 m32=-6.00001812'
                   ' planar_step=0.05 exponent=1 jitter_index=0 width=1280 height=768 bias_units=0.53571875 clamp_texels=20.97152 cascades=1 margin=0.95 band=0.1'
                   ' raster_m20=0 raster_m21=0 pixel_centre=1 valid0=1 map0=1024 map_frame0=2 bias0=0.00100000005 bias_max0=0.00999999978 texel_world0=0.48828125 extent0=250'
                   ' depth_light0=512 depth_behind0=512 rows0=' + self.ROWS) % centre
        params, extra = apply.parse_apply_params(apply.line_fields(cascade))
        self.assertTrue(extra['pixel_centre']); self.assertEqual(len(params['cascades']), 1)
        params, extra = apply.parse_apply_params(apply.line_fields(cascade.replace(' raster_m20=0 raster_m21=0 pixel_centre=1', '')))
        self.assertFalse(extra['pixel_centre'])

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
            elif key == 'depth_encoding':
                self.assertEqual(expected[key], 'device')  # reconstruction predates the wide RT2
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
        self.assertEqual([c['source'] for c in extra['cascades']], [0, 1])  # no source<i>: the slot itself
        # The ratio guard's compaction (shadow-cascade-extents.md, section 5): slot 1 samples cascade 2's map.
        compact = line.replace(' rows1=', ' source1=2 rows1=')
        _, extra_compact = apply.parse_apply_params(apply.line_fields(compact))
        self.assertEqual([c['source'] for c in extra_compact['cascades']], [0, 2])
        with self.assertRaises(ValueError):  # slots sample cascades in ascending order
            apply.parse_apply_params(apply.line_fields(line.replace(' rows0=', ' source0=1 rows0=').replace(' rows1=', ' source1=1 rows1=')))
        with self.assertRaises(ValueError):  # a printed bias that is not the law's at that cascade's texel and range
            apply.parse_apply_params(apply.line_fields(line.replace('bias1=%.9g' % resolved[1]['bias_constant'], 'bias1=0.001')))
        with self.assertRaises(ValueError):
            apply.parse_apply_params(apply.line_fields(line.replace('rows1=' + rows, 'rows1=' + rows.rsplit(',', 1)[0])))


if __name__ == '__main__':
    unittest.main()


def receiver_depth_scene(range_units, half_extent, depth_half, size=4096, width=160, height=160, m00=5.45, amplitude=150.0, texels=3.0, seed=7):
    """A single-sided plate at `range_units` down the camera axis, corrugated
    like a girder (`amplitude` x 2 = 300 u of depth spread over `texels` map
    texels, the run117 p50), under one cascade of `half_extent` / `depth_half`
    / `size` with the sun tilted off the view axis. The map is the plate's own
    sun depth at the D3D9 texel positions; the receivers are the first hit of
    every pixel's camera ray (the pixel centre, m20 = m21 = 0) in float64.
    Returns (w, s, maps, params) with w the exact view depth."""
    rng = np.random.default_rng(seed)
    forward = np.array([.35, -.25, 1.0]); forward /= np.linalg.norm(forward)
    right = np.cross([0.0, 1.0, 0.0], forward); right /= np.linalg.norm(right)
    up = np.cross(forward, right)
    centre = np.array([0.0, 0.0, range_units])
    period = texels * 2.0 * half_extent / size

    def relief(u, v):  # sun-space height field along the sun (u, v in world units of the map plane)
        return amplitude * np.sin(2.0 * np.pi * u / period) * np.sin(2.0 * np.pi * v / period + .7)

    rows = (right[0] / half_extent, right[1] / half_extent, right[2] / half_extent, -np.dot(centre, right) / half_extent,
            up[0] / half_extent, up[1] / half_extent, up[2] / half_extent, -np.dot(centre, up) / half_extent,
            forward[0] / (2.0 * depth_half), forward[1] / (2.0 * depth_half), forward[2] / (2.0 * depth_half), (depth_half - np.dot(centre, forward)) / (2.0 * depth_half))
    a = np.arange(size, dtype=np.float64)
    mu = a[None, :] / size * 2.0 - 1.0; mv = 1.0 - a[:, None] / size * 2.0   # D3D9 texel (a, b) at map position (a, b) / N
    sun_map = (relief(mu * half_extent, mv * half_extent) + depth_half) / (2.0 * depth_half)
    i = np.arange(width, dtype=np.float64)[None, :].repeat(height, 0); j = np.arange(height, dtype=np.float64)[:, None].repeat(width, 1)
    dv = np.stack([((i + .5) / width * 2.0 - 1.0) / m00, (1.0 - (j + .5) / height * 2.0) / m00, np.ones_like(i)], -1)
    du, dvv, dw = dv @ right, dv @ up, dv @ forward
    cu, cv, cw = np.dot(centre, right), np.dot(centre, up), np.dot(centre, forward)

    def gap(z):  # sun depth of the ray point minus the plate's height there: zero on the surface
        return z * dw - cw - relief(z * du - cu, z * dvv - cv)
    # First root along every ray: a 1 u march over +-4 amplitude about the
    # flat plate's hit (the plate is tilted against the view), then bisection.
    lo = cw / dw - 4.0 * amplitude; found = np.zeros(i.shape, dtype=bool); bracket_lo = lo.copy(); bracket_hi = lo.copy()
    previous = gap(lo)
    for step in range(1, int(8.0 * amplitude) + 1):
        z = lo + float(step)
        current = gap(z)
        crossing = ~found & (np.sign(current) != np.sign(previous))
        bracket_lo = np.where(crossing, z - 1.0, bracket_lo); bracket_hi = np.where(crossing, z, bracket_hi); found |= crossing
        previous = current
    assert found.all(), 'every camera ray meets the plate'
    for _ in range(60):
        mid = .5 * (bracket_lo + bracket_hi)
        low_side = np.sign(gap(mid)) == np.sign(gap(bracket_lo))
        bracket_lo = np.where(low_side, mid, bracket_lo); bracket_hi = np.where(low_side, bracket_hi, mid)
    w = .5 * (bracket_lo + bracket_hi)
    s = rng.uniform(.3, .9, i.shape)
    bias = apply.resolve_bias(apply.BIAS_UNITS_DEFAULT, half_extent, depth_half, size)
    params = dict(m00=m00, m11=m00, m20=0.0, m21=0.0, m22=apply.PRODUCTION_M22, m32=apply.PRODUCTION_M32, exponent=1.0, planar_step=.05, jitter_index=3,
                  cascades=[dict(rows=rows, bias_constant=bias['bias_constant'], bias_max=bias['bias_max'], valid=True)])
    return w, s, [sun_map], params


class ReceiverDepthPrecision(unittest.TestCase):
    """docs/architecture/shadow-receiver-depth.md, section 4: the girder plate
    at C3 (37 km, 18.3 u texel) and C4 (92 km, 73.2 u texel) rows, the
    receiver encoded as fp32 z/w (`device`, the G32R32F lane) and as fp32 w
    (`linear`, RT2.b of the A32B32G32R32F lane), the twin at +-1 ULP of each:
    the device encoding re-rolls (|delta f| >= 2/9) at least 5 % of the owned
    pixels (the run117 class), the linear one at most 1 %, and the linear f
    equals the float64-exact f on at least 99.9 % of the pixels."""
    CASCADES = {'C3': (37000.0, 37500.0, 187500.0), 'C4': (92000.0, 150000.0, 300000.0)}

    @staticmethod
    def reroll(w, s, maps, params, encoding):
        exact = apply.expected_factor_cascades(w, s, maps, params, depth_encoding='linear')
        owned = exact['valid'] & (exact['selected'] == 0)
        stored = (params['m22'] + params['m32'] / w if encoding == 'device' else w).astype(np.float32)
        plus, minus = np.nextafter(stored, np.float32(np.inf)), np.nextafter(stored, np.float32(-np.inf))
        f = [apply.expected_factor_cascades(v.astype(np.float64), s, maps, params, depth_encoding=encoding)['f'] for v in (stored, plus, minus)]
        flips = np.abs(f[1] - f[2]) >= 2.0 / 9.0 - 1e-12
        return dict(owned=int(owned.sum()), flip_fraction=float(flips[owned].mean()), exact_fraction=float((f[0][owned] == exact['f'][owned]).mean()),
                    shadowed_fraction=float((exact['f'][owned] < 1.0).mean()))

    def test_linear_w_stops_the_one_ulp_reroll(self):
        for name, (range_units, half_extent, depth_half) in self.CASCADES.items():
            w, s, maps, params = receiver_depth_scene(range_units, half_extent, depth_half)
            device, linear = self.reroll(w, s, maps, params, 'device'), self.reroll(w, s, maps, params, 'linear')
            self.assertGreater(device['owned'], 20000, name)
            self.assertEqual(device['owned'], linear['owned'], name)
            self.assertGreater(device['shadowed_fraction'], .05, (name, 'the plate self-shadows its girders'))
            self.assertGreaterEqual(device['flip_fraction'], .05, (name, device))
            self.assertLessEqual(linear['flip_fraction'], .01, (name, linear))
            self.assertGreaterEqual(linear['exact_fraction'], .999, (name, linear))
            print('RECEIVER_DEPTH cascade=%s owned=%d device_flip=%.5f linear_flip=%.5f linear_exact=%.5f shadowed=%.4f'
                  % (name, device['owned'], device['flip_fraction'], linear['flip_fraction'], linear['exact_fraction'], device['shadowed_fraction']))

    def test_beyond_one_code_decides_by_exact_fp16_distance(self):
        def call(after, reference):
            a, r = np.array([[after]], dtype=np.float64), np.array([[reference]], dtype=np.float64)
            return bool(apply.beyond_one_code(a, r, np.abs(a - r) / apply.fp16_code(r))[0])
        one = lambda v, direction: float(np.nextafter(np.float16(v), np.float16(direction)))
        # The cascades-5 frame-6 witness: 1.000132 codes by the spacing at the reference, exactly one code across the exponent boundary.
        self.assertFalse(call(one(0.947709, 0.0), 0.947709))
        self.assertFalse(call(one(0.5, 0.0), 0.5)); self.assertFalse(call(one(0.5, 1.0), 0.5))  # both sides of a power of two
        self.assertTrue(call(one(one(0.5, 0.0), 0.0), 0.5)); self.assertTrue(call(one(one(0.5, 1.0), 1.0), 0.5))  # two codes either way
        # Where the spacing understates the step (the reference just below a power of two, the readback above it): one code is one code, two are beyond.
        below = one(1.0, 0.0)
        self.assertFalse(call(1.0, below)); self.assertTrue(call(one(1.0, 2.0), below))
        # Negative pairs: sign-magnitude patterns must not read as far apart.
        self.assertFalse(call(one(-0.25, -1.0), -0.25)); self.assertTrue(call(one(one(-0.25, -1.0), -1.0), -0.25))
        self.assertFalse(call(float(np.float16(-0.0)), 0.0))
        self.assertTrue(np.array_equal(apply.fp16_ordered_code(np.array([-1.0, -0.0, 0.0, 1.0])) < apply.fp16_ordered_code(np.array([-0.5, 0.0, 0.5, 2.0])), [True, False, True, True]))

    def test_unpack_rt2_lanes_and_encodings(self):
        wide = np.arange(2 * 3 * 4, dtype='<f4').tobytes(); narrow = np.arange(2 * 3 * 2, dtype='<f4').tobytes()
        d, s = apply.unpack_rt2(wide, 3, 2, 'linear'); self.assertEqual((d[0, 1], s[0, 1]), (6.0, 5.0))
        d, s = apply.unpack_rt2(wide, 3, 2); self.assertEqual((d[0, 1], s[0, 1]), (4.0, 5.0))
        d, s = apply.unpack_rt2(narrow, 3, 2); self.assertEqual((d[1, 0], s[1, 0]), (6.0, 7.0))
        with self.assertRaises(ValueError):
            apply.unpack_rt2(narrow, 3, 2, 'linear')
        with self.assertRaises(ValueError):
            apply.unpack_rt2(wide, 3, 2, 'wrong')
        self.assertEqual(apply.parse_depth_encoding({}), 'device'); self.assertEqual(apply.parse_depth_encoding({'depth_encoding': 'linear'}), 'linear')
        self.assertEqual((apply.rt2_suffix('device'), apply.rt2_suffix('linear')), ('rg32f', 'rgba32f'))
        params = dict(PARAMS, depth_encoding='linear')
        self.assertTrue(np.array_equal(apply.view_depth(np.array([6.0, 12.0]), params), [6.0, 12.0]))
        self.assertTrue(np.allclose(apply.view_depth(np.array([PARAMS['m22'] + PARAMS['m32'] / 6.0]), PARAMS), [6.0]))
        # The linear twin at the device twin's receivers: the same factors.
        d, s = receivers()
        w = np.where(d >= 0.0, PARAMS['m32'] / (d - PARAMS['m22']), -1.0)
        device_f = apply.expected_factor(d, s, np.zeros((64, 64)), PARAMS)
        linear_f = apply.expected_factor(w, s, np.zeros((64, 64)), params)
        self.assertTrue(np.allclose(device_f['factor'], linear_f['factor']) and np.array_equal(device_f['valid'], linear_f['valid']))
