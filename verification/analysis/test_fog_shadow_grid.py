"""The sun-visibility slice grid (docs/architecture/fog-shadow-pass.md): src/renderer/fog_shadow_grid.h compiled natively
against its host twin in tools/analysis/fog_density_shader_reference.py (slice distances, tile addressing, cross-fade
weights, penumbra radius, the constant rows c36..c41), and the twin's own invariants."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('fog_density_shader_reference', ROOT / 'tools/analysis/fog_density_shader_reference.py')
ref = importlib.util.module_from_spec(spec); spec.loader.exec_module(ref)
PROGRAM = r'''
#include "fog_shadow_grid.h"
#include <cstdio>
#include <initializer_list>
#include <limits>
int main() {
    using namespace x3m::renderer;
    for (unsigned w : {1280u, 2560u, 31u, 256u}) std::printf("%u ", fog_grid_extent(w));
    std::printf("\n");
    for (float cap : {112500.f, 70000.f}) {
        for (unsigned j = 0; j < 64; ++j) std::printf("%.9g %.9g ", double(fog_grid_slice_start(j, cap)), double(fog_grid_slice_width(j, cap)));
        std::printf("\n");
        for (float s : {0.f, 250.f, 499.9f, 500.f, 11999.f, 12000.f, 12001.f, 40000.f, 69999.f, 112499.f, 200000.f}) std::printf("%u ", fog_grid_slice_of(s, cap));
        std::printf("\n");
    }
    for (unsigned j = 0; j < 64; ++j) { unsigned x, y, l; fog_grid_tile(j, x, y, l); std::printf("%u %u %u ", x, y, l); }
    std::printf("\n");
    for (float m : {0.f, .85f, .9f, .95f, .96f}) for (float z : {-.1f, .5f, 1.f}) std::printf("%.9g ", double(fog_grid_blend(m, z, 1.f)));
    std::printf("\n");
    FogLookTuning t;
    for (float gap : {-.1f, 0.f, .01f, .05f, .5f, 1.f}) std::printf("%.9g ", double(fog_grid_penumbra_radius(gap, 200000.f / 36.6f, t)));
    std::printf("\n");
    FogGridCascade c[3]; c[0].texel_world = 36.6f; c[0].range_world = 1000.f; c[2].texel_world = 7.3f; c[2].range_world = 30000.f;
    for (unsigned phase : {5u, 69u}) for (bool resolved : {true, false}) {
        float rows[fog_grid_rows][4]; fog_grid_constants(t, 112500.f, 256, 144, c, phase, resolved, rows);
        for (auto& row : rows) for (float v : row) std::printf("%.9g ", double(v));
        std::printf("\n");
    }
    std::printf("%llu %llu\n", fog_grid_pass_fetches(1280, 768), fog_grid_pass_fetches(2560, 1440));
    for (float cap : {12000.f, 12039.f, 12040.f, 20000.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        float rows[fog_grid_rows][4]; rows[3][3] = 1.f;
        std::printf("%u %u %.9g ", unsigned(fog_grid_valid_cap(cap)), unsigned(fog_grid_constants(t, cap, 256, 144, c, 0, true, rows)), double(rows[3][3]));
    }
    std::printf("\n");
    return 0;
}
'''


class FogShadowGrid(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'grid.cpp'; source.write_text(PROGRAM); exe = Path(directory) / 'grid'
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src/renderer'), str(source), '-o', str(exe)], check=True)
            cls.lines = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.splitlines()

    def test_slice_distances_and_extents_match_the_twin(self):
        self.assertEqual(self.lines[0].split(), ['320', '640', '8', '64'])
        self.assertEqual([ref.grid_extent(w) for w in (1280, 2560, 31, 256)], [320, 640, 8, 64])
        for index, cap in enumerate((112500., 70000.)):
            values = np.array(self.lines[1 + 2 * index].split(), np.float64).reshape(64, 2)
            j = np.arange(64)
            np.testing.assert_allclose(values[:, 0], ref.grid_slice_start(j, cap), rtol=1e-6)
            np.testing.assert_allclose(values[:, 1], ref.grid_slice_width(j, cap), rtol=1e-6)
            # The slices tile [0, cap): starts plus widths chain, the last one ends at the cap.
            np.testing.assert_allclose(values[:-1, 0] + values[:-1, 1], values[1:, 0], rtol=1e-6)
            self.assertAlmostEqual(values[-1, 0] + values[-1, 1], cap, delta=1e-2)
            probes = [0., 250., 499.9, 500., 11999., 12000., 12001., 40000., 69999., 112499., 200000.]
            self.assertEqual([int(v) for v in self.lines[2 + 2 * index].split()], ref.grid_slice_of(probes, cap).tolist())
            # Every slice start lies in its own slice; a distance just below the next start too.
            self.assertEqual(ref.grid_slice_of(values[:, 0] + 1e-3, cap).tolist(), j.tolist())
            self.assertEqual(ref.grid_slice_of(values[:, 0] + values[:, 1] - 1e-2, cap).tolist(), j.tolist())
        self.assertEqual(ref.grid_slice_of([-5., 1e9], 112500.).tolist(), [0, 63])

    def test_tile_addressing_is_a_bijection(self):
        triples = np.array(self.lines[5].split(), int).reshape(64, 3)
        tx, ty, lane = ref.grid_tile(np.arange(64))
        np.testing.assert_array_equal(triples, np.stack((tx, ty, lane), 1))
        self.assertEqual(len({tuple(t) for t in triples}), 64)
        self.assertTrue((triples[:, :2] < 4).all() and (triples[:, 2] < 4).all())
        # Slice j reads tile j div 4, lane j mod 4: the pass writes tile t's four lanes as slices 4t..4t+3.
        np.testing.assert_array_equal(4 * (ty * 4 + tx) + lane, np.arange(64))

    def test_cross_fade_weights(self):
        values = np.array(self.lines[6].split(), np.float64).reshape(5, 3)
        expect = np.array([[ref.grid_blend(m, z, 1.) for z in (-.1, .5, 1.)] for m in (0., .85, .9, .95, .96)])
        np.testing.assert_allclose(values, expect, atol=1e-6)
        # 1 inside the band start, half way through the band .5, 0 at the margin and beyond; outside the depth range 0.
        self.assertEqual(values[0, 1], 1.); self.assertEqual(values[1, 1], 1.); self.assertAlmostEqual(values[2, 1], .5, places=5)
        self.assertAlmostEqual(values[3, 1], 0., places=5); self.assertEqual(values[4, 1], 0.); self.assertTrue((values[:, 0] == 0).all())  # float32 at the margin: 3.6e-7

    def test_penumbra_radius_law(self):
        values = np.array(self.lines[7].split(), np.float64)
        ratio = 200000. / 36.6
        expect = [np.clip(g * ratio * ref.GRID_SUN_ANGLE, 1., 16.) for g in (-.1, 0., .01, .05, .5, 1.)]
        np.testing.assert_allclose(values, expect, rtol=1e-5)
        # Lit (no gap) keeps the minimum kernel; a blocker 10 km (0.05 x 200000) in front gives 0.0093 x 10000 / 36.6 texels.
        self.assertEqual(values[0], 1.); self.assertEqual(values[1], 1.); self.assertAlmostEqual(values[3], .0093 * 10000 / 36.6, delta=.01)
        self.assertEqual(values[4], 16.); self.assertEqual(values[5], 16.)  # the clamp

    def test_constant_rows_mirror_the_twin(self):
        cascades = [dict(texel=36.6, range=1000.), dict(texel=0., range=0.), dict(texel=7.3, range=30000.)]
        for index, (phase, resolved) in enumerate(((5, True), (5, False), (69, True), (69, False))):
            values = np.array(self.lines[8 + index].split(), np.float64).reshape(6, 4)
            rows = ref.grid_constants(256, 144, cascades, phase, resolved)
            np.testing.assert_allclose(values, rows.astype(np.float64), rtol=2e-7, atol=0, err_msg=f'phase {phase} resolved {resolved}')
        # Layout: cascade rows (texel, range, range/texel, 0; unknown -> 0), slices (500, far, reciprocals), grid (64 x 36 tiles of a
        # 256x144 atlas), penumbra (half angle x 1, 1, 16, frame term: golden ratio x phase mod 64 while resolved, 0 held).
        rows = ref.grid_constants(256, 144, cascades, 5, True)
        np.testing.assert_allclose(rows[0], (36.6, 1000., 1000. / 36.6, 0.), rtol=1e-6); self.assertTrue((rows[1] == 0).all())
        np.testing.assert_allclose(rows[3], (500., 2512.5, 1 / 500., 1 / 2512.5), rtol=1e-6)
        np.testing.assert_allclose(rows[4], (64., 36., 1 / 256., 1 / 144.), rtol=1e-6)
        np.testing.assert_allclose(rows[5, :3], (.0093, 1., 16.), rtol=1e-6); self.assertAlmostEqual(rows[5, 3], (5 * .618034) % 1, places=5)
        self.assertEqual(ref.grid_constants(256, 144, cascades, 5, False)[5, 3], 0.)
        self.assertEqual(ref.grid_constants(256, 144, cascades, 69, True)[5, 3], ref.grid_constants(256, 144, cascades, 5, True)[5, 3])
        # Fetch counts per frame: the pass reads 16 per slice of every grid texel (the note's table); atlas bytes.
        self.assertEqual(self.lines[12].split(), [str(16 * 64 * 320 * 192), str(16 * 64 * 640 * 360)])
        self.assertEqual((16 * 64 * 320 * 192, 16 * 64 * 640 * 360), (62914560, 235929600))
        self.assertEqual((1280 * 768 * 4, 2560 * 1440 * 4), (3932160, 14745600))

    def test_atlas_twin_invariants(self):
        # No cascade inside: every slice lit. A dark map at depth 0: every slice fully shadowed but the disc taps that leave the map (CLAMP keeps them dark).
        dark = [dict(rows=np.array([[0, 0, 1e-9, 0], [0, 0, 1e-9, 0], [0, 0, 1e-9, .5]]), N=64, bias=.001, map=np.zeros((64, 64), np.float32), texel=0., range=0.)]
        atlas = ref.grid_atlas(16, 9, dark, 0, True)
        self.assertEqual(atlas.shape, (20, 32, 4)); self.assertTrue((atlas == 0).all())  # 32x18 full: 8x5 tiles
        lit = [dict(dark[0], map=np.ones((64, 64), np.float32))]
        self.assertTrue((ref.grid_atlas(16, 9, lit, 0, True) == 255).all())
        # The seam case: v .3 (cascade 0) before the hand-over, .8 (cascade 1) after, a ramp of at most the slice increment between.
        seam = ref.grid_atlas(16, 9, ref.grid_cascades(3), 0, True, ref.grid_tuning(3)).astype(float) / 255.
        tx, ty, lane = ref.grid_tile(np.arange(64)); column = np.array([seam[ty[j] * 5 + 2, tx[j] * 8 + 4, lane[j]] for j in range(64)])
        self.assertAlmostEqual(column[:24].mean(), .3, delta=2 / 255); self.assertAlmostEqual(column[-1], .8, delta=2 / 255)
        self.assertLessEqual(np.abs(np.diff(column)).max(), .5 * 2512.5 / 1e4 + 2 / 255)
        # The reader: at a texel centre the bilinear lane read returns the texel; the slice comes from the bin distance.
        reader = ref.grid_reader(seam * 255, 16, 9, [4.5], [2.5])
        points = np.array([[0., 0., ref.grid_slice_start(30, ref.TUNING['sky_cap']) + 1.]])
        self.assertAlmostEqual(float(reader(points, np.array([0]), np.array([1.]))[0]), column[30], places=9)

    def test_column_cap_boundary(self):
        # The far slice width must be finite and at least one unit (cap >= 12040): below or non-finite, the header refuses
        # (rows zero, no reciprocal of zero) and the twin raises; the look's own SKY_CAP floor (20000) is not relied on.
        values = self.lines[13].split()
        self.assertEqual(values, ['0', '0', '0', '0', '0', '0', '1', '1', '1', '1', '1', '0.00499999989', '0', '0', '0', '0', '0', '0'])
        for cap in (12000., 12039.9, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                ref.grid_constants(256, 144, [], cap=cap)
        self.assertEqual(ref.grid_constants(256, 144, [], cap=12040.)[3, 3], np.float32(1.))
        self.assertEqual(ref.GRID_NEAR_WIDTH, 500.)

    def test_tuning_fields_and_env_names(self):
        header = (ROOT / 'src/renderer/fog_look_math.h').read_text()
        for name in ('PENUMBRA', 'PENUMBRA_MIN', 'PENUMBRA_MAX'):
            self.assertIn('"%s"' % name, header)
        self.assertEqual((ref.TUNING['penumbra'], ref.TUNING['penumbra_min'], ref.TUNING['penumbra_max']), (1., 1., 16.))


if __name__ == '__main__':
    sys.exit(unittest.main())
