"""Host contracts of tools/analysis/shadow_receiver_reroll.py, the +-1 ULP
receiver re-roll witness (docs/architecture/shadow-receiver-depth.md section 4;
docs/verification/directional-shadows.md, "Run 40 A (run117) diagnosis"
section 2). No Wine, no D3D, no game dumps: a synthetic plate, a 256 x 256 map
and a synthetic `sun_shadow_apply_params` line.

The plate is the diagnosis' class in miniature: a single-sided girder surface at
37 km whose stripes step the view depth by 300 u at the pixel scale, seen under
a 2.9-texel pixel footprint on an 18.3-u cascade texel, with the map filled from
the plate's own supersampled depth, so the map holds the receiver's own surface
(median margin a few units, as at run117). One ULP of the stored z/w there is
13.7 world units - half a texel across the sun - and re-rolls the 3x3 compare on
a fifth of the owned pixels; one ULP of fp32 view depth is 3.9e-3 u and moves
none of them.
"""
import contextlib
import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import shadow_receiver_reroll as srr  # noqa: E402
import sun_shadow_apply as twin  # noqa: E402

WIDTH, HEIGHT = 64, 32
SIZE = 256                    # map texels per side
TEXEL = 18.3                  # world units per texel (run117 C3)
EXTENT = TEXEL * SIZE / 2.0
DEPTH_LIGHT, DEPTH_BEHIND = 300000.0, 75000.0
RANGE = DEPTH_LIGHT + DEPTH_BEHIND
Z0 = 37000.0                  # plate distance
FOOTPRINT = 53.0              # world units a pixel covers at Z0 (2.9 texels)
M00 = 2.0 * Z0 / (WIDTH * FOOTPRINT)
M11 = M00 * WIDTH / HEIGHT
M22, M32 = 1.00000298, -6.0000186      # the production near-plane pair
AMPLITUDE = 300.0             # girder depth step
SUN_ALONG = 0.7               # ray . sun
SUN_ACROSS = math.sqrt(1.0 - SUN_ALONG * SUN_ALONG)
SUN_FORWARD = (SUN_ACROSS, 0.0, SUN_ALONG)
SUN_RIGHT = (-SUN_ALONG, 0.0, SUN_ACROSS)
SUN_UP = (0.0, 1.0, 0.0)
CENTRE = (0.0, 0.0, Z0)
BIAS_UNITS, CLAMP_TEXELS = twin.BIAS_UNITS_DEFAULT, twin.BIAS_CLAMP_TEXELS
BIAS = twin.resolve_bias(BIAS_UNITS, EXTENT, .5 * RANGE, SIZE, CLAMP_TEXELS)
SUPERSAMPLE = 6               # map samples per pixel per axis


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def light_rows():
    """The twelve `shadow_replay_light_rows` values of this cascade."""
    return tuple([SUN_RIGHT[i] / EXTENT for i in range(3)] + [-_dot(CENTRE, SUN_RIGHT) / EXTENT]
                 + [SUN_UP[i] / EXTENT for i in range(3)] + [-_dot(CENTRE, SUN_UP) / EXTENT]
                 + [SUN_FORWARD[i] / RANGE for i in range(3)] + [(-_dot(CENTRE, SUN_FORWARD) + DEPTH_LIGHT) / RANGE])


def surface(dir_x, dir_y):
    """View depth of the girder plate along a ray direction: stripes one pixel
    (2.9 texels) wide stepping the depth by AMPLITUDE."""
    import numpy as np
    gx = np.floor(dir_x * M00 * WIDTH / 2.0).astype(np.int64)
    gy = np.floor(dir_y * M11 * HEIGHT / 2.0).astype(np.int64)
    return Z0 + AMPLITUDE * ((gx % 2) ^ (gy % 2)).astype(np.float64)


def _positions(dir_x, dir_y):
    z = surface(dir_x, dir_y)
    return [dir_x * z, dir_y * z, z]


def _sun(position, rows):
    return [rows[k * 4] * position[0] + rows[k * 4 + 1] * position[1] + rows[k * 4 + 2] * position[2] + rows[k * 4 + 3]
            for k in range(3)]


def plate_capture():
    """(d, s, map) of the plate: RT2 device depth on the pixel grid, and the
    map as the plate's own nearest depth per texel (empty texels hold 1)."""
    import numpy as np
    rows = light_rows()
    i = np.arange(WIDTH, dtype=np.float64)[None, :].repeat(HEIGHT, 0)
    j = np.arange(HEIGHT, dtype=np.float64)[:, None].repeat(WIDTH, 1)
    dir_x = ((i + .5) / WIDTH * 2.0 - 1.0) / M00
    dir_y = (1.0 - (j + .5) / HEIGHT * 2.0) / M11
    z = _positions(dir_x, dir_y)[2]
    d = np.float32(M22 + M32 / z).astype(np.float64)
    s = np.ones_like(d)
    sx = (np.arange(WIDTH * SUPERSAMPLE) + .5) / (WIDTH * SUPERSAMPLE) * 2.0 - 1.0
    sy = 1.0 - (np.arange(HEIGHT * SUPERSAMPLE) + .5) / (HEIGHT * SUPERSAMPLE) * 2.0
    gx, gy = np.meshgrid(sx / M00, sy / M11)
    sun = _sun(_positions(gx, gy), rows)
    tu = np.clip(np.floor((sun[0] * .5 + .5) * SIZE).astype(np.int64), 0, SIZE - 1)
    tv = np.clip(np.floor((.5 - sun[1] * .5) * SIZE).astype(np.int64), 0, SIZE - 1)
    sun_map = np.ones((SIZE, SIZE))
    np.minimum.at(sun_map, (tv.ravel(), tu.ravel()), sun[2].ravel())
    return d, s, sun_map


def plate_params():
    """(params, extra) as `parse_apply_cascade_params` would return them."""
    params = dict(m00=M00, m11=M11, m20=0.0, m21=0.0, m22=M22, m32=M32, exponent=1.0, planar_step=.05,
                  margin=.95, band=.1, jitter_index=3,
                  cascades=[dict(rows=light_rows(), bias_constant=BIAS['bias_constant'],
                                 bias_max=BIAS['bias_max'], valid=True, backface=False)])
    extra = dict(width=WIDTH, height=HEIGHT, jitter_px=(0.0, 0.0), bias_units=BIAS_UNITS, clamp_texels=CLAMP_TEXELS,
                 map=SIZE, backface_mask=0, pixel_centre=True,
                 cascades=[dict(map=SIZE, map_frame=1, texel_world=BIAS['texel_world'], extent=EXTENT,
                                depth_light=DEPTH_LIGHT, depth_behind=DEPTH_BEHIND, source=0)])
    return params, extra


def params_line(frame=1, device=1):
    centre = twin.pixel_centre_terms(WIDTH, HEIGHT)
    return ('sun_shadow_apply_params device=%d frame=%d m00=%.9g m11=%.9g jitter_x=0.000000 jitter_y=0.000000 '
            'm20=%.9g m21=%.9g m22=%.9g m32=%.9g planar_step=0.05 exponent=1.000000 jitter_index=3 '
            'width=%d height=%d bias_units=%.9g clamp_texels=%.9g cascades=1 margin=0.95 band=0.1 '
            'raster_m20=%.9g raster_m21=%.9g pixel_centre=1 backface_mask=0 '
            'valid0=1 map0=%d map_frame0=%d bias0=%.9g bias_max0=%.9g texel_world0=%.9g extent0=%.9g '
            'depth_light0=%.9g depth_behind0=%.9g source0=0 backface0=0 rows0=%s\n'
            % (device, frame, M00, M11, 0.0, 0.0, M22, M32, WIDTH, HEIGHT, BIAS_UNITS, CLAMP_TEXELS,
               -centre[0], -centre[1], SIZE, frame, BIAS['bias_constant'], BIAS['bias_max'], BIAS['texel_world'],
               EXTENT, DEPTH_LIGHT, DEPTH_BEHIND, ','.join('%.9g' % v for v in light_rows())))


def write_capture(directory, frame=1, device=1):
    import numpy as np
    d, s, sun_map = plate_capture()
    rt2 = np.stack([d, s], axis=-1).astype('<f4')
    (Path(directory) / ('depth_%d_%d.rg32f' % (device, frame))).write_bytes(rt2.tobytes())
    (Path(directory) / ('shadow_map0_%d_%d.r32f' % (device, frame))).write_bytes(sun_map.astype('<f4').tobytes())
    (Path(directory) / 'session-test.log').write_text('frame_begin device=1 frame=%d\n' % frame
                                                      + params_line(frame, device)
                                                      + 'shadow_retention_frame device=1 frame=%d\n' % frame)


class PerturbedDepth(unittest.TestCase):
    """The two encodings' quanta on a row of device depths."""

    def setUp(self):
        import numpy as np
        self.np = np
        self.params = dict(m22=M22, m32=M32)
        z = np.array([[6.5, 250.0, 21000.0, Z0, 92000.0]])
        self.z = z
        self.d = np.float32(M22 + M32 / z).astype(np.float64)
        # what the apply actually reconstructs from the stored fp32 d: up to
        # 1.6e-4 of z away from the ideal, which is the quantum under test
        self.stored_z = M32 / (self.d - M22)

    def _steps(self, encoding):
        base, plus, minus = srr.perturbed_depth(self.d, self.params, encoding)
        view = lambda value: M32 / (value - M22)
        return view(base), view(plus), view(minus)

    def test_zw_quantum_is_the_stored_ulp(self):
        base, plus, minus = srr.perturbed_depth(self.d, self.params, 'zw')
        self.np.testing.assert_array_equal(base, self.d)
        self.assertTrue((plus > base).all() and (minus < base).all())
        for a, b in ((plus, base), (base, minus)):
            gap = self.np.float32(a) - self.np.float32(b)
            self.np.testing.assert_allclose(gap, self.np.spacing(self.np.float32(b)), rtol=1e-6)

    def test_zw_view_step_is_z_squared_over_1e8(self):
        base, plus, _ = self._steps('zw')
        self.np.testing.assert_allclose(base, self.stored_z, rtol=1e-12)
        self.np.testing.assert_allclose(base, self.z, rtol=2e-4)
        # z^2 / 1e8 is the law where d is near 1; close to the near plane d is
        # near 0 and its ULP is far finer, so the law is checked from 21 km out
        far = self.z >= 1000.0
        self.np.testing.assert_allclose(self.np.abs(plus - base)[far], (self.z ** 2 / 1e8)[far], rtol=.2)
        self.assertLess(float(self.np.abs(plus - base)[0, 0]), 1e-6)

    def test_w_quantum_is_one_ulp_of_view_depth(self):
        base, plus, minus = self._steps('w')
        quantized = self.np.float32(self.stored_z)
        self.np.testing.assert_allclose(base, quantized.astype(self.np.float64), rtol=1e-9)
        self.np.testing.assert_allclose(self.np.abs(plus - base), self.np.spacing(quantized), rtol=1e-5)
        self.np.testing.assert_allclose(self.np.abs(minus - base), self.np.abs(self.np.spacing(-quantized)), rtol=1e-5)
        # at 37 km: 13.7 u under z/w, 3.9e-3 u under w
        self.assertAlmostEqual(float(self.np.abs(plus - base)[0, 3]), 0.00390625, delta=1e-7)

    def test_sentinel_and_unknown_encoding(self):
        d = self.np.array([[-1.0, 0.5]])
        for encoding in ('w',):
            base, plus, minus = srr.perturbed_depth(d, self.params, encoding)
            self.assertEqual(base[0, 0], -1.0)
            self.assertEqual(plus[0, 0], -1.0)
            self.assertEqual(minus[0, 0], -1.0)
        with self.assertRaises(srr.MalformedInput):
            srr.perturbed_depth(d, self.params, 'linear')


class Plate(unittest.TestCase):
    """The witness: at 37 km the z/w quantum re-rolls the compare, the w
    quantum does not."""

    @classmethod
    def setUpClass(cls):
        params, extra = plate_params()
        d, s, sun_map = plate_capture()
        cls.record = srr.reroll_frame(d, s, [sun_map], params, extra, frame=1)
        cls.entry = cls.record['cascades'][0]

    def test_geometry_is_the_diagnosis_class(self):
        self.assertAlmostEqual(2.0 * EXTENT / SIZE, TEXEL, places=9)
        self.assertEqual(self.entry['owned'], WIDTH * HEIGHT)
        self.assertEqual(self.record['valid'], WIDTH * HEIGHT)

    def test_zw_flips_and_w_does_not(self):
        zw, w = self.entry['encodings']['zw'], self.entry['encodings']['w']
        self.assertAlmostEqual(zw['receiver_step_units_p50'], Z0 ** 2 / 1e8, delta=1.0)
        self.assertAlmostEqual(w['receiver_step_units_p50'], 0.00390625, delta=1e-7)
        self.assertGreaterEqual(zw['flipped_fraction'], .05)        # the run117 class reproduced
        self.assertLessEqual(w['flipped_fraction'], .01)            # the design's target
        self.assertEqual(w['flipped'], 0)
        self.assertLess(w['changed_fraction'], .05 * zw['changed_fraction'])

    def test_map_holds_the_receivers_own_surface(self):
        margin = self.entry['encodings']['zw']['margin_units_p25_p50_p75']
        self.assertEqual(len(margin), 3)
        self.assertLessEqual(margin[0], margin[1])
        self.assertLessEqual(margin[1], margin[2])
        self.assertLess(abs(margin[1]), 4.0 * TEXEL)   # within a few texels of the receiver: no back face between them
        self.assertIsNone(self.entry['encodings']['w']['margin_units_p25_p50_p75'])

    def test_threshold_orders_the_classes(self):
        params, extra = plate_params()
        d, s, sun_map = plate_capture()
        loose = srr.reroll_frame(d, s, [sun_map], params, extra, threshold=1.0 / 9.0, frame=1)
        tight = srr.reroll_frame(d, s, [sun_map], params, extra, threshold=8.0 / 9.0, frame=1)
        zw = self.entry['encodings']['zw']
        self.assertGreaterEqual(loose['cascades'][0]['encodings']['zw']['flipped'], zw['flipped'])
        self.assertLessEqual(tight['cascades'][0]['encodings']['zw']['flipped'], zw['flipped'])
        self.assertLessEqual(zw['flipped'], zw['changed'])


class Files(unittest.TestCase):
    """Loading, the CLI and the malformed cases."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        write_capture(self.dir)

    def test_reads_the_params_line_and_dumps(self):
        log = srr.find_log(self.dir)
        params, extra = srr.read_apply_params(log, [1])[1]
        self.assertEqual((extra['width'], extra['height']), (WIDTH, HEIGHT))
        self.assertEqual(len(params['cascades']), 1)
        self.assertAlmostEqual(params['cascades'][0]['bias_constant'], BIAS['bias_constant'])
        d, s, maps, w = srr.load_frame(self.dir, 1, 1, params, extra)
        self.assertEqual(d.shape, (HEIGHT, WIDTH))
        self.assertEqual(maps[0].shape, (SIZE, SIZE))
        self.assertTrue((s == 1.0).all())
        self.assertIsNone(w)

    def test_wide_dump_reads_the_stored_view_depth(self):
        # The receiver-depth option's 16 B/px dump (depth_encoding=linear on the params line): .r is
        # still the z/w lane the tool perturbs, .b the stored fp32 view depth that seeds the `w` encoding.
        import numpy as np
        log = srr.find_log(self.dir)
        text = log.read_text()
        log.write_text(text.replace('pixel_centre=1 ', 'pixel_centre=1 depth_encoding=linear '))
        params, extra = srr.read_apply_params(log, [1])[1]
        self.assertEqual(params['depth_encoding'], 'linear')
        d0, s0, _ = plate_capture()
        view = np.float32(M32 / (d0 - M22))
        wide = np.stack([d0, s0, view, view], axis=-1).astype('<f4')
        (self.dir / 'depth_1_1.rgba32f').write_bytes(wide.tobytes())
        d, s, maps, w = srr.load_frame(self.dir, 1, 1, params, extra)
        self.assertTrue(np.array_equal(d, d0) and np.array_equal(w, view.astype(np.float64)))
        base, plus, minus = srr.perturbed_depth(d, params, 'w', w)
        step = np.abs(M32 / (plus - M22) - M32 / (base - M22))
        ulp = np.ldexp(1.0, (np.floor(np.log2(view.astype(np.float64))) - 23).astype(np.int64))
        self.assertTrue(np.allclose(step, ulp, rtol=1e-3))
        record = srr.reroll_frame(d, s, maps, params, extra, frame=1, w=w)
        self.assertEqual(record['cascades'][0]['encodings']['w']['flipped'], 0)
        self.assertGreaterEqual(record['cascades'][0]['encodings']['zw']['flipped_fraction'], .05)
        (self.dir / 'depth_1_1.rgba32f').write_bytes(wide.tobytes()[:-16])
        with self.assertRaises(srr.MalformedInput):
            srr.load_frame(self.dir, 1, 1, params, extra)

    def test_cli_json(self):
        out = self.dir / 'reroll.json'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(srr.main(['--run', str(self.dir), '--frames', '1', '--cascade', '0',
                                       '--json', str(out)]), 0)
        records = json.loads(out.read_text())
        self.assertEqual(len(records), 1)
        entry = records[0]['cascades'][0]
        self.assertEqual(records[0]['frame'], 1)
        self.assertEqual(entry['owned'], WIDTH * HEIGHT)
        self.assertGreaterEqual(entry['encodings']['zw']['flipped_fraction'], .05)
        self.assertEqual(entry['encodings']['w']['flipped'], 0)
        self.assertIn('c0 owned', srr.format_record(records[0]))

    def test_missing_frame_map_and_truncated_rt2(self):
        log = srr.find_log(self.dir)
        with self.assertRaises(srr.MalformedInput):
            srr.read_apply_params(log, [2])
        with self.assertRaises(srr.MalformedInput):
            srr.read_apply_params(log, [1], device=2)
        params, extra = srr.read_apply_params(log, [1])[1]
        with self.assertRaises(srr.MalformedInput):
            srr.reroll_frame(*plate_capture()[:2], [None], params, extra, cascades=[3], frame=1)
        rt2 = self.dir / 'depth_1_1.rg32f'
        rt2.write_bytes(rt2.read_bytes()[:-8])
        with self.assertRaises(srr.MalformedInput):
            srr.load_frame(self.dir, 1, 1, params, extra)
        rt2.unlink()
        with self.assertRaises(srr.MalformedInput):
            srr.load_frame(self.dir, 1, 1, params, extra)

    def test_frame_parsing_and_log_discovery(self):
        self.assertEqual(srr.parse_frames('14780-14782'), [14780, 14781, 14782])
        self.assertEqual(srr.parse_frames('24624, 14780 14780'), [14780, 24624])
        self.assertEqual(srr.find_log(self.dir), self.dir / 'session-test.log')
        (self.dir / 'session-second.log').write_text('')
        with self.assertRaises(srr.MalformedInput):
            srr.find_log(self.dir)


if __name__ == '__main__':
    unittest.main()
