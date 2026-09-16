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


if __name__ == '__main__':
    unittest.main()
