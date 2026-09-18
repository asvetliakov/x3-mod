#!/usr/bin/env python3
"""Host contract of the cascade program's slope-scaled margin
(docs/verification/directional-shadows.md, "Run 40 B (run119) near flicker";
src/temporal/sun_shadow_cascade_apply_ps.hlsl, `slope`; the twin's
`slope_texels`). No Wine, no game.

The plate is run119's C1 class in miniature: a single flat plate 1.0-3.7 km
away, seen 65 degrees off its normal (and tilted 15 degrees sideways so that
neighbouring pixels quantise independently), lit by a sun 3.4 degrees above
its surface (cos 0.06), under the production C1 texel (1.645 u; extent / size
of the 3,370 u / 4096 cascade), depth range and bias law (2.18 u constant,
20.97-texel clamp), with the map's v axis along the plate's compressed
direction as run119's basis was for the sliver. Its map is the plate's own
analytic depth per texel; the receiver carries run119's measured RT2 error
(+-0.02 u of surface noise along the ray, then the fp32 quantisation of z/w:
0.013 u p50 off a local plane at 1.6 km). On such a plane the sun-depth slope
is 27 u per texel while a 2x2 pixel quad spans a few hundredths of a texel
across the compressed axis, so the plane term extrapolates that slope from a
sub-texel baseline and the receiver error lands on the +-1-texel taps as a
few units, above the constant bias: 5 % of the pixels re-roll under +-1 ULP
and a 0.2-u receiver shift (the per-frame offset run119 measured) re-rolls
13 % (run119 16528 C1: 10.6 % and 30 %). The margin `slope_texels` x
(|dz/du| + |dz/dv|) folded into the plane term takes them to 1.4 % and 5 %;
what remains are the taps whose plane term exceeds the 21-texel clamp
(1.4 texels x 27 u), which the fold cannot reach.
"""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import sun_shadow_apply as twin  # noqa: E402
import shadow_receiver_reroll as reroll  # noqa: E402

WIDTH, HEIGHT = 320, 192
Z0 = 1600.0                                 # plate distance on the centre ray
FOOTPRINT = 3.125                           # world units a pixel covers at Z0 (run119: 3.1)
M00 = 2.0 * Z0 / (WIDTH * FOOTPRINT)
M11 = M00 * WIDTH / HEIGHT
M22, M32 = 1.00000298, -6.0000186           # the production near-plane pair
VIEW_COS = 0.42                             # ray . normal on the centre ray (run119 grazing class p50)
SUN_COS = 0.06                              # |sun . normal| (run119: 0.058-0.066)
SIZE = 2048
EXTENT = 3369.63428 * SIZE / 4096.0         # run119 C1's texel, 1.645 u
DEPTH_LIGHT, DEPTH_BEHIND = 300000.0, 6739.26855
RANGE = DEPTH_LIGHT + DEPTH_BEHIND
BIAS = twin.resolve_bias(twin.BIAS_UNITS_DEFAULT, EXTENT, .5 * RANGE, SIZE, twin.BIAS_CLAMP_TEXELS)
SLOPE_TEXELS = 0.2                          # sun_shadow_bias_slope_texels_default
SIDE_TILT = 0.25                            # normal's x component: view depth also changes 1.9 u per pixel across (run119), so neighbouring pixels quantise independently
SUN_AZIMUTH_DEG = 165.0                     # the sun's in-plane direction, from the plate's horizontal towards its downslope (the class's worst azimuth in the twin's scan)
SURFACE_NOISE = 0.02                        # +-u along the ray on RT2 before fp32 quantisation: run119's RT2 sits 0.013 u p50 off a local plane (twice the quantum's share)
CENTRE = (0.0, 0.0, Z0)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(v):
    norm = math.sqrt(sum(x * x for x in v))
    return tuple(x / norm for x in v)


def configure(azimuth_deg=SUN_AZIMUTH_DEG, side_tilt=SIDE_TILT, sun_cos=SUN_COS):
    """Set the plate's normal and sun basis (module globals): the view-space
    normal (side_tilt, ., VIEW_COS), the sun's in-plane direction at
    `azimuth_deg` from the plate's horizontal, |sun . normal| = sun_cos, the
    light onto the front face, and the map basis (right, up) about it."""
    global NORMAL, IN_PLANE, SUN_FORWARD, SUN_RIGHT, SUN_UP
    NORMAL = (side_tilt, math.sqrt(1.0 - VIEW_COS * VIEW_COS - side_tilt * side_tilt), VIEW_COS)
    horizontal = _unit(tuple((1.0 if k == 0 else 0.0) - NORMAL[0] * NORMAL[k] for k in range(3)))
    downslope = _cross(NORMAL, horizontal)
    a = math.radians(azimuth_deg)
    IN_PLANE = tuple(math.cos(a) * horizontal[k] + math.sin(a) * downslope[k] for k in range(3))
    SUN_FORWARD = tuple(math.sqrt(1.0 - sun_cos * sun_cos) * IN_PLANE[k] - sun_cos * NORMAL[k] for k in range(3))
    # The map's v axis along the plate's compressed direction (its normal's projection), as run119's
    # C1 basis was for the sliver (dz/du = 0, dz/dv = -27 u per texel): u runs along the strip.
    SUN_RIGHT = _unit(_cross(NORMAL, SUN_FORWARD))
    SUN_UP = _cross(SUN_FORWARD, SUN_RIGHT)


configure()


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def light_rows():
    """The twelve `shadow_replay_light_rows` values of this cascade."""
    return tuple([SUN_RIGHT[i] / EXTENT for i in range(3)] + [-_dot(CENTRE, SUN_RIGHT) / EXTENT]
                 + [SUN_UP[i] / EXTENT for i in range(3)] + [-_dot(CENTRE, SUN_UP) / EXTENT]
                 + [SUN_FORWARD[i] / RANGE for i in range(3)] + [(-_dot(CENTRE, SUN_FORWARD) + DEPTH_LIGHT) / RANGE])


def plate_depth(dir_x, dir_y):
    """View depth of the plate along a ray direction (a plane through CENTRE with NORMAL)."""
    return Z0 * NORMAL[2] / (NORMAL[0] * dir_x + NORMAL[1] * dir_y + NORMAL[2])


def plate_capture(shift=0.0):
    """(d, s, map): RT2 device depth of the plate on the pixel grid, moved
    `shift` units along the ray (0: the plate itself), and the map as the
    plate's analytic depth at every texel's sample position (i, j) / N."""
    import numpy as np
    i = np.arange(WIDTH, dtype=np.float64)[None, :].repeat(HEIGHT, 0)
    j = np.arange(HEIGHT, dtype=np.float64)[:, None].repeat(WIDTH, 1)
    dir_x = ((i + .5) / WIDTH * 2.0 - 1.0) / M00
    dir_y = (1.0 - (j + .5) / HEIGHT * 2.0) / M11
    z = plate_depth(dir_x, dir_y) + shift
    z = z + np.random.default_rng(7).uniform(-SURFACE_NOISE, SURFACE_NOISE, z.shape)
    d = np.float32(M22 + M32 / z).astype(np.float64)
    s = np.ones_like(d)
    u = np.arange(SIZE, dtype=np.float64)[None, :] / SIZE
    v = np.arange(SIZE, dtype=np.float64)[:, None] / SIZE
    xs, ys = (u * 2.0 - 1.0) * EXTENT, (1.0 - 2.0 * v) * EXTENT
    origin = [CENTRE[k] + xs * SUN_RIGHT[k] + ys * SUN_UP[k] for k in range(3)]
    t = sum(NORMAL[k] * (CENTRE[k] - origin[k]) for k in range(3)) / _dot(NORMAL, SUN_FORWARD)
    sun_map = (t + DEPTH_LIGHT) / RANGE
    return d, s, np.where((sun_map > 0.0) & (sun_map < 1.0), sun_map, 1.0)


def plate_params(slope_texels=0.0):
    """(params, extra) as `parse_apply_cascade_params` would return them."""
    params = dict(m00=M00, m11=M11, m20=0.0, m21=0.0, m22=M22, m32=M32, exponent=1.0, planar_step=.05,
                  margin=.95, band=.1, jitter_index=5,
                  cascades=[dict(rows=light_rows(), bias_constant=BIAS['bias_constant'], bias_max=BIAS['bias_max'],
                                 valid=True, backface=False, slope_texels=slope_texels)])
    extra = dict(width=WIDTH, height=HEIGHT, jitter_px=(0.0, 0.0), bias_units=twin.BIAS_UNITS_DEFAULT,
                 clamp_texels=twin.BIAS_CLAMP_TEXELS, map=SIZE, backface_mask=0, pixel_centre=True,
                 cascades=[dict(map=SIZE, map_frame=1, texel_world=BIAS['texel_world'], extent=EXTENT,
                                depth_light=DEPTH_LIGHT, depth_behind=DEPTH_BEHIND, source=0)])
    return params, extra


def measure(slope_texels, shift=0.2):
    """The plate's figures under one margin: the +-1 ULP re-roll fraction of
    the owned pixels (`shadow_receiver_reroll`, z/w encoding, |df| >= 2/9), the
    fraction whose own f moves by >= 2/9 when the receiver shifts `shift` units
    along the ray, and the mean shade (the plate has no occluder: any shade is acne)."""
    import numpy as np
    d, s, sun_map = plate_capture()
    params, extra = plate_params(slope_texels)
    record = reroll.reroll_frame(d, s, [sun_map], params, extra, [0], reroll.DEFAULT_THRESHOLD, True, 1)
    entry = record['cascades'][0]['encodings']['zw']
    base = twin.expected_factor_cascades(d, s, [sun_map], params, coarse=True)
    moved = twin.expected_factor_cascades(plate_capture(shift)[0], s, [sun_map], params, coarse=True)
    owned = base['valid'] & (base['selected'] == 0) & moved['valid']
    changed = owned & (np.abs(moved['per_cascade'][0] - base['per_cascade'][0]) >= reroll.DEFAULT_THRESHOLD - 1e-12)
    return dict(owned=int(owned.sum()), ulp_flipped=entry['flipped_fraction'], ulp_step=entry['receiver_step_units_p50'],
                shift_changed=float(changed.sum() / max(1, owned.sum())),
                shade=float(1.0 - base['per_cascade'][0][owned].mean()))


class Geometry(unittest.TestCase):
    def test_plate_is_the_run119_class(self):
        import numpy as np
        d, s, sun_map = plate_capture()
        self.assertAlmostEqual(BIAS['texel_world'], 1.64532924, places=5)
        self.assertAlmostEqual(BIAS['bias_constant'] * RANGE, 2.181, places=2)
        self.assertAlmostEqual(abs(_dot(SUN_FORWARD, NORMAL)), SUN_COS, places=9)
        self.assertAlmostEqual(_dot(SUN_FORWARD, SUN_FORWARD), 1.0, places=9)
        z = M32 / (d - M22)
        self.assertTrue(900.0 < z.min() < 1200.0 and 2600.0 < z.max() < 4000.0)
        self.assertLess(np.median(z * z / 1e8), 0.05)                         # the z/w quantum along the ray
        params, extra = plate_params()
        base = twin.expected_factor_cascades(d, s, [sun_map], params, coarse=True)
        owned = base['valid'] & (base['selected'] == 0)
        self.assertGreater(owned.sum(), 0.9 * WIDTH * HEIGHT)
        # The map holds the plate itself at the receivers' texels (own surface, front face).
        sun = base['sun'][0]
        tu = np.clip(np.floor((sun[0] * .5 + .5) * SIZE + .5).astype(np.int64), 0, SIZE - 1)
        tv = np.clip(np.floor((.5 - sun[1] * .5) * SIZE + .5).astype(np.int64), 0, SIZE - 1)
        margin = (sun_map[tv, tu] - sun[2]) * RANGE
        self.assertLess(np.median(np.abs(margin[owned])), 8.0)
        # The sun-depth slope: about 27 u per texel of the map (1 / cos, times the texel).
        self.assertAlmostEqual(BIAS['texel_world'] / SUN_COS, 27.4, delta=0.5)


class Margin(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = measure(0.0)
        cls.after = measure(SLOPE_TEXELS)

    def test_plate_reproduces_the_flip_fraction(self):
        # run119 16528 C1: 10.6 % under +-1 ULP of z/w, 30 % frame to frame, 24 % shaded; the
        # plate: 5.2 %, 12.6 %, 8.2 % (twin, 2026-09-18).
        self.assertGreaterEqual(self.before['ulp_flipped'], .03)
        self.assertGreaterEqual(self.before['shift_changed'], .08)
        self.assertGreaterEqual(self.before['shade'], .05)
        self.assertLess(self.before['ulp_step'], .05)                         # the quantum is tiny; the geometry amplifies it

    def test_slope_margin_reduces_it(self):
        # 0.2 texels: 1.4 %, 5.2 %, 5.3 % on the plate (run119 16528 C1: 1.4 %, 15.9 %, 14 %).
        self.assertLessEqual(self.after['ulp_flipped'], .02)
        self.assertLessEqual(self.after['ulp_flipped'], .35 * self.before['ulp_flipped'])
        self.assertLessEqual(self.after['shift_changed'], .5 * self.before['shift_changed'])
        self.assertLess(self.after['shade'], self.before['shade'])
        self.assertEqual(self.after['owned'], self.before['owned'])

    def test_zero_margin_is_the_old_law(self):
        import numpy as np
        d, s, sun_map = plate_capture()
        params, extra = plate_params(0.0)
        with_key = twin.expected_factor_cascades(d, s, [sun_map], params, coarse=True)
        del params['cascades'][0]['slope_texels']
        without = twin.expected_factor_cascades(d, s, [sun_map], params, coarse=True)
        self.assertTrue(np.array_equal(with_key['factor'], without['factor']))


class ParamsLine(unittest.TestCase):
    def test_slope_field_is_optional(self):
        rows = ','.join('%.9g' % v for v in light_rows())
        base = ('sun_shadow_apply_params device=1 frame=1 m00=%.9g m11=%.9g jitter_x=0 jitter_y=0 m20=0 m21=0 m22=%.9g m32=%.9g '
                'planar_step=0.05 exponent=1 jitter_index=5 width=%d height=%d bias_units=%.9g clamp_texels=%.9g cascades=1 '
                'margin=0.95 band=0.1 valid0=1 map0=%d map_frame0=1 bias0=%.9g bias_max0=%.9g%s texel_world0=%.9g extent0=%.9g '
                'depth_light0=%.9g depth_behind0=%.9g source0=0 backface0=0 rows0=%s')
        args = (M00, M11, M22, M32, WIDTH, HEIGHT, twin.BIAS_UNITS_DEFAULT, twin.BIAS_CLAMP_TEXELS, SIZE,
                BIAS['bias_constant'], BIAS['bias_max'])
        tail = (BIAS['texel_world'], EXTENT, DEPTH_LIGHT, DEPTH_BEHIND, rows)
        params, _ = twin.parse_apply_params(twin.line_fields(base % (args + ('',) + tail)))
        self.assertEqual(params['cascades'][0]['slope_texels'], 0.0)
        params, _ = twin.parse_apply_params(twin.line_fields(base % (args + (' slope0=0.2',) + tail)))
        self.assertEqual(params['cascades'][0]['slope_texels'], 0.2)
        with self.assertRaises(ValueError):
            twin.parse_apply_params(twin.line_fields(base % (args + (' slope0=-1',) + tail)))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--print':
        for k in (0.0, 0.1, 0.2, 0.3):
            print(k, measure(k))
    else:
        unittest.main()
