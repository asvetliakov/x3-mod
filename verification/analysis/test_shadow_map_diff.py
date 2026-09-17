"""Host contracts of tools/analysis/shadow_map_diff.py, the basis-aligned
comparison of consecutive F8 cascade depth maps
(docs/verification/directional-shadows.md, "Run 40 A (run117) diagnosis").
No Wine, no D3D, no game dumps: synthetic 64 x 64 maps and a synthetic log.

The construction is the run117 one in miniature: map B is map A slid by a whole
number of texels (the centre snap) with every depth shifted by the camera's
advance along the sun axis over the depth range (the unsnapped depth origin).
Aligned, the two must agree exactly: zero flips, zero changed texels. Compared
texel for texel, the same pair must show the churn the sliding box produces,
whose flip and change counts follow from the rectangle geometry.
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
import shadow_map_diff as smd  # noqa: E402

SIZE = 64
EXTENT = 32.0            # texel = 2 * extent / size = 1 world unit
DEPTH_LIGHT = 300000.0
DEPTH_BEHIND = 75000.0
RANGE = DEPTH_LIGHT + DEPTH_BEHIND
RIGHT = (1.0, 0.0, 0.0)
UP = (0.0, 1.0, 0.0)
FORWARD = (0.0, 0.0, 1.0)
RECT = (12, 44, 9, 41)   # r0, r1, c0, c1 of the occupied block
RAMP = 0.001             # depth step per column, above every eps used here
BASE_DEPTH = 0.25
EPS = 1e-6


def basis_line(frame, cascade, center, device=1, size=SIZE, extent=EXTENT,
               right=RIGHT, up=UP, forward=FORWARD, valid=1):
    vector = lambda v: '%.9g,%.9g,%.9g' % tuple(v)
    return ('shadow_replay_map_basis device=%d frame=%d cascade=%d cascades=5 replayed=7 replayed_frame=%d '
            'size=%d valid=%d right=%s up=%s forward=%s center=%s extent=%.9g depth_light=%.9g depth_behind=%.9g '
            'sun=%s sun_register=0 sun_verdict=sampled\n'
            % (device, frame, cascade, frame, size, valid, vector(right), vector(up), vector(forward),
               vector(center), extent, DEPTH_LIGHT, DEPTH_BEHIND, vector([-c for c in forward])))


def occupied_map(shift_x=0, shift_y=0, depth_offset=0.0):
    """The block of RECT translated by (shift_x, shift_y), depth ramp in x."""
    import numpy as np
    out = np.ones((SIZE, SIZE), dtype=np.float32)
    r0, r1, c0, c1 = RECT
    columns = np.arange(c0, c1, dtype=np.float64)
    row = np.float32(BASE_DEPTH + RAMP * columns - depth_offset)
    out[r0 + shift_y:r1 + shift_y, c0 + shift_x:c1 + shift_x] = row[None, :]
    return out


def write_map(directory, cascade, frame, array, device=1):
    path = Path(directory) / ('shadow_map%d_%d_%d.r32f' % (cascade, device, frame))
    path.write_bytes(array.astype('<f4').tobytes())
    return path


class ParseBasis(unittest.TestCase):
    def test_fields_and_derived(self):
        basis = smd.parse_basis_line(basis_line(14780, 3, (-57029.18, -245.25, -28373.55)))
        self.assertEqual((basis['device'], basis['frame'], basis['cascade'], basis['size'], basis['valid']),
                         (1, 14780, 3, SIZE, 1))
        self.assertEqual(basis['extent'], EXTENT)
        self.assertAlmostEqual(basis['texel'], 2 * EXTENT / SIZE)
        self.assertAlmostEqual(basis['depth_range'], RANGE)
        self.assertEqual(basis['right'], list(RIGHT))
        self.assertAlmostEqual(basis['center'][0], -57029.18, places=2)

    def test_rejects_malformed(self):
        for line in ('shadow_retention_frame device=1\n',
                     basis_line(1, 0, (0, 0, 0)).replace('extent=32 ', ''),
                     basis_line(1, 0, (0, 0, 0)).replace('center=0,0,0', 'center=0,0'),
                     basis_line(1, 0, (0, 0, 0)).replace('size=64', 'size=big'),
                     'shadow_replay_map_basis device=1 frame\n'):
            with self.assertRaises(smd.MalformedInput):
                smd.parse_basis_line(line)


class Alignment(unittest.TestCase):
    def _bases(self, shift_x, shift_y, advance, right=RIGHT, up=UP, forward=FORWARD):
        """Two bases whose centres differ by the given whole-texel slide and
        unsnapped advance along the sun axis."""
        texel = 2 * EXTENT / SIZE
        dx, dy, dz = -shift_x * texel, shift_y * texel, advance
        center_b = tuple(dx * right[i] + dy * up[i] + dz * forward[i] for i in range(3))
        return (smd.parse_basis_line(basis_line(100, 3, (0.0, 0.0, 0.0), right=right, up=up, forward=forward)),
                smd.parse_basis_line(basis_line(101, 3, center_b, right=right, up=up, forward=forward)))

    def test_whole_texel_shift_and_depth_offset(self):
        align = smd.alignment(*self._bases(3, -2, 150.0))
        self.assertEqual((align['shift_x'], align['shift_y']), (3, -2))
        self.assertLess(align['shift_residual'], 1e-6)
        self.assertAlmostEqual(align['depth_offset'], 150.0 / RANGE, places=12)
        self.assertAlmostEqual(align['basis_turn'], 0.0)

    def test_rotated_basis(self):
        s = 1.0 / math.sqrt(2.0)
        align = smd.alignment(*self._bases(-5, 7, -90.0, right=(s, s, 0.0), up=(-s, s, 0.0), forward=(0.0, 0.0, 1.0)))
        self.assertEqual((align['shift_x'], align['shift_y']), (-5, 7))
        self.assertLess(align['shift_residual'], 1e-6)
        self.assertAlmostEqual(align['depth_offset'], -90.0 / RANGE, places=12)

    def test_invalid_basis_and_geometry_change(self):
        a, b = self._bases(1, 1, 0.0)
        with self.assertRaises(smd.MalformedInput):
            smd.alignment(a, dict(b, valid=0))
        with self.assertRaises(smd.MalformedInput):
            smd.alignment(a, dict(b, extent=EXTENT * 2))


class Diff(unittest.TestCase):
    """A shifted box with a known depth offset: zero aligned churn, the
    rectangle's own churn unaligned."""
    SHIFT_X, SHIFT_Y, ADVANCE = 5, -3, 150.0

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.offset = self.ADVANCE / RANGE
        texel = 2 * EXTENT / SIZE
        center_b = (-self.SHIFT_X * texel, self.SHIFT_Y * texel, self.ADVANCE)
        self.log = self.dir / 'session-test.log'
        self.log.write_text(
            'frame_begin device=1 frame=100\n'
            + basis_line(100, 3, (0.0, 0.0, 0.0))
            + basis_line(100, 4, (0.0, 0.0, 0.0))          # another cascade
            + basis_line(100, 3, (9.0, 9.0, 9.0), device=2)  # another device
            + basis_line(101, 3, center_b)
            + basis_line(101, 4, (0.0, 0.0, 0.0))
            + 'shadow_retention_frame device=1 frame=101 mode=census\n')
        write_map(self.dir, 3, 100, occupied_map())
        write_map(self.dir, 3, 101, occupied_map(self.SHIFT_X, self.SHIFT_Y, self.offset))

    def test_aligned_is_clean_and_unaligned_shows_the_slide(self):
        records = smd.diff_burst(self.dir, self.log, [100, 101], [3], eps=EPS)
        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual((record['shift_x'], record['shift_y']), (self.SHIFT_X, self.SHIFT_Y))
        self.assertAlmostEqual(record['depth_offset'], self.offset, places=12)

        r0, r1, c0, c1 = RECT
        rows, cols = r1 - r0, c1 - c0
        block = rows * cols
        aligned = record['aligned']
        self.assertEqual(aligned['flips'], 0)
        self.assertEqual(aligned['changed'], 0)
        self.assertEqual(aligned['mean_abs_delta_changed'], 0.0)
        self.assertEqual(aligned['changed_fraction'], 0.0)
        # the overlap loses |shift| rows and columns of the map, not of the block
        self.assertEqual(aligned['overlap_texels'], (SIZE - abs(self.SHIFT_X)) * (SIZE - abs(self.SHIFT_Y)))
        self.assertEqual(aligned['occupied_a'], block)
        self.assertEqual(aligned['occupied_both'], block)

        overlap_rows = rows - abs(self.SHIFT_Y)
        overlap_cols = cols - abs(self.SHIFT_X)
        intersection = overlap_rows * overlap_cols
        unaligned = record['unaligned']
        self.assertEqual(unaligned['occupied_a'], block)
        self.assertEqual(unaligned['occupied_b'], block)
        self.assertEqual(unaligned['occupied_both'], intersection)
        self.assertEqual(unaligned['flips'], 2 * (block - intersection))
        # every shared texel differs by the ramp over the slide plus the origin drift
        self.assertEqual(unaligned['changed'], intersection)
        self.assertAlmostEqual(unaligned['changed_fraction'], intersection / block)
        expected = abs(RAMP * self.SHIFT_X + self.offset)
        self.assertAlmostEqual(unaligned['mean_abs_delta_changed'], expected, places=6)
        self.assertAlmostEqual(unaligned['p50_abs_delta_both'], expected, places=6)  # the same constant on every shared texel

    def test_large_eps_hides_the_unaligned_change(self):
        records = smd.diff_burst(self.dir, self.log, [100, 101], [3], eps=1.0)
        self.assertEqual(records[0]['unaligned']['changed'], 0)
        self.assertGreater(records[0]['unaligned']['flips'], 0)  # flips do not depend on eps

    def test_missing_basis_and_bad_map_size(self):
        with self.assertRaises(smd.MalformedInput):
            smd.diff_burst(self.dir, self.log, [100, 101], [4], eps=EPS)  # cascade 4 has no maps on disk
        with self.assertRaises(smd.MalformedInput):
            smd.diff_burst(self.dir, self.log, [100, 102], [3], eps=EPS)  # no basis line for 102
        truncated = self.dir / 'shadow_map3_1_101.r32f'
        truncated.write_bytes(truncated.read_bytes()[:-8])
        with self.assertRaises(smd.MalformedInput):
            smd.diff_burst(self.dir, self.log, [100, 101], [3], eps=EPS)

    def test_cli_json(self):
        out = self.dir / 'diff.json'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(smd.main(['--run', str(self.dir), '--frames', '100-101', '--cascade', '3',
                                       '--eps', str(EPS), '--json', str(out)]), 0)
        records = json.loads(out.read_text())
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]['cascade'], 3)
        self.assertEqual(records[0]['frame_a'], 100)
        self.assertEqual(records[0]['aligned']['changed'], 0)
        self.assertGreater(records[0]['unaligned']['changed'], 0)
        self.assertIn('shift=(+5, -3)', smd.format_record(records[0]))

    def test_frame_parsing_and_log_discovery(self):
        self.assertEqual(smd.parse_frames('14780-14783'), [14780, 14781, 14782, 14783])
        self.assertEqual(smd.parse_frames('7, 5 5-6'), [5, 6, 7])
        self.assertEqual(smd.find_log(self.dir), self.log)
        (self.dir / 'session-second.log').write_text('')
        with self.assertRaises(smd.MalformedInput):
            smd.find_log(self.dir)


if __name__ == '__main__':
    unittest.main()
