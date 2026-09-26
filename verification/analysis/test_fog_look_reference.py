"""The single stored-density fog look: the host look law and its constants against src/renderer/fog_look_math.h."""
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
CHROMA = (0.20072728, 1.0, 0.120704934)
PROGRAM = r'''
#include "fog_look_math.h"
#include <cstdio>
#include <initializer_list>
#include <limits>
int main() {
    using namespace x3m::renderer;
    const float chroma[3] = {0.20072728f, 1.0f, 0.120704934f}, radiance[3] = {1.f, .5f, 1.5f};
    for (unsigned phase : {3u, 67u}) {
        float rows[fog_look_rows][4];
        std::printf("%.9g", double(fog_look_constants(FogLookTuning{}, chroma, radiance, phase, rows)));
        for (auto& row : rows) for (float v : row) std::printf(" %.9g", double(v));
        std::printf("\n");
    }
    { // No temporal resolve: the shaft lookup offset is dropped, and the sample offset is always zero.
        float rows[fog_look_rows][4];
        fog_look_constants(FogLookTuning{}, chroma, radiance, 0, rows, false);
        std::printf("%.9g %.9g %.9g %.9g\n", double(rows[7][2]), double(rows[7][3]), double(rows[6][2]), double(rows[6][3]));
    }
    // Fade start fallback: cap - 1000 is taken as given, anything later becomes the last quarter of the cap.
    for (float start : {69000.f, 69001.f, 199000.f}) {
        FogLookTuning f; f.sky_cap = 70000.f; f.taper_start = start; float rows[fog_look_rows][4];
        fog_look_constants(f, chroma, radiance, 0, rows); std::printf("%.9g %.9g ", double(rows[10][1]), double(rows[10][2]));
    }
    std::printf("\n");
    // The X3M_FOG_LOOK_<NAME> overrides and their range table were removed on 2026-09-26 (the constants are baked).
    return 0;
}
'''


class Slab:
    """Density rising 0.6 to 0.9 across |x - 40000| < 6000 (both levels), 0 elsewhere; counts far-level taps."""
    def __init__(self): self.taps = 0
    def value(self, points):
        x = np.asarray(points)[:, 0] - 40000.; return np.where(np.abs(x) < 6000., .75 + x * 2.5e-5, 0.).astype(np.float32)
    def sample(self, points, distance, camera, counts=None): return self.value(points)
    def sample_level(self, name, points, camera): self.taps += len(points); return self.value(points)


class FogLookReference(unittest.TestCase):
    def test_constants_mirror_the_production_header(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'look.cpp'; source.write_text(PROGRAM); exe = Path(directory) / 'look'
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src/renderer'), str(source), '-o', str(exe)], check=True)
            lines = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.splitlines()
        # The phase only shifts the noise, modulo 64.
        for index, phase in enumerate((3, 67)):
            values = np.array(lines[index].split(), np.float64)
            rows, scale = ref.look_constants(CHROMA, (1., .5, 1.5), phase)
            self.assertEqual(values[0], scale)
            np.testing.assert_allclose(values[1:], rows.ravel().astype(np.float64), rtol=2e-7, atol=0, err_msg='phase %d' % phase)
        np.testing.assert_array_equal(np.array(lines[0].split(), np.float64), np.array(lines[1].split(), np.float64))
        # Fade 65000 -> 112500 units by default; a start that does not precede the cap falls back to its last quarter.
        rows, _ = ref.look_constants(CHROMA)
        np.testing.assert_allclose(rows[10, :3], (.04, 65000., 1. / 47500.), rtol=1e-6); self.assertEqual(rows[0, 3], 112500.)
        rows, _ = ref.look_constants(CHROMA, tuning=dict(ref.TUNING, sky_cap=70000., taper_start=70000.))
        np.testing.assert_allclose(rows[10, 1:3], (52500., 1. / 17500.), rtol=1e-6)
        np.testing.assert_allclose(np.array(lines[3].split(), np.float64), (69000., 1e-3, 52500., 1 / 17500., 52500., 1 / 17500.), rtol=1e-6)
        for start, expect in ((69000., 69000.), (69001., 52500.)):
            self.assertEqual(ref.look_constants(CHROMA, tuning=dict(ref.TUNING, sky_cap=70000., taper_start=start))[0][10, 1], expect)
        # Without a resolve the shaft lookup holds the bin centres; the retired L3 sample offset is always zero.
        self.assertEqual(lines[2].split(), ['0', '0', '0', '0'])
        np.testing.assert_array_equal(ref.look_constants(CHROMA, resolved=False)[0][6:8, 2:], np.zeros((2, 2)))
        np.testing.assert_array_equal(ref.look_constants(CHROMA)[0][7, 2:], (1., 1.))

    def march(self, store=None, **options):
        d = np.array([[1., 0., 0.], [-1., 0., 0.], [0., 1., 0.]])
        return ref.look_march(np.zeros(3), d, np.full(3, 200000.), False, CHROMA, store or Slab(), 3.75e-6, pixels=(np.arange(3), np.arange(3)), **options)

    def test_law_properties(self):
        S, T = self.march(); Sp, Tp = self.march(phase=4)
        # The slab is met by the first ray only: the others stay exactly empty.
        self.assertTrue((T[1:] == 1).all() and (S[1:] == 0).all()); self.assertLess(T[0], .9)
        # No occluder: the phase moves nothing at all (the retired L3 sample offset is gone).
        np.testing.assert_array_equal(Sp, S); np.testing.assert_array_equal(Tp, T)
        # Self-shadow and powder only remove sun light, never extinction: S stays positive under a lit slab.
        self.assertTrue((S[0] > 0).all())
        no_shadow = self.march(tuning=dict(ref.TUNING, self_shadow=0., powder=0.))[0]
        self.assertTrue((S[0] < no_shadow[0]).all())
        self.assertEqual(self.march(tuning=dict(ref.TUNING, self_shadow=0., powder=0.))[1][0], T[0])
        # A fully shadowed sample keeps coloured light: ambient plus the floors, a different hue from the lit one.
        Sd, Td = self.march(shadowed=True)
        self.assertEqual(Td[0], T[0]); self.assertTrue((Sd[0] > 0).all() and (Sd[0] < S[0]).all())
        self.assertGreater(Sd[0][1] / Sd[0][0], 1.2 * S[0][1] / S[0][0])
        # The column ends at the cap for sky and geometry alike: a hull behind the cap and the sky beside it agree.
        far = Slab(); far.value = lambda points: np.where(np.abs(np.asarray(points)[:, 0] - 120000.) < 6000., .9, 0.).astype(np.float32)
        _, Tsky = self.march(far)
        _, Tgeo = ref.look_march(np.zeros(3), np.array([[1., 0., 0.]]), np.array([200000.]), True, CHROMA, far, 3.75e-6)
        self.assertEqual(Tsky[0], 1.); self.assertEqual(Tgeo[0], 1.)
        Ssky, Tsky = self.march()
        Sgeo, Tgeo = ref.look_march(np.zeros(3), np.array([[1., 0., 0.]]), np.array([150000.]), True, CHROMA, Slab(), 3.75e-6)
        self.assertEqual(Tsky[0], Tgeo[0]); np.testing.assert_array_equal(Ssky[0], Sgeo[0])

    def test_shaft_lookup_offset_moves_only_the_shaft(self):
        # A penumbra along the first ray: visibility falls from 1 to 0 across the slab. Only the lookup position moves with the
        # pixel and phase; density, extinction and unshadowed rays are those of the bin-centre march.
        def edge(points, rays, ds): return np.clip((46000. - np.linalg.norm(points, axis=1)) / 12000., 0., 1.)
        centre = dict(ref.TUNING, shadow_jitter=0.)
        S0, T0 = self.march(visibility=edge, tuning=centre); Sa, Ta = self.march(visibility=edge, phase=1); Sb, _ = self.march(visibility=edge, phase=2)
        Slit, _ = self.march()
        self.assertEqual(Ta[0], T0[0]); self.assertFalse(np.array_equal(Sa[0], S0[0])); self.assertFalse(np.array_equal(Sa[0], Sb[0]))
        self.assertTrue((Sa[0] < Slit[0]).all()); np.testing.assert_array_equal(Sa[1:], S0[1:])
        # Without an occluder the offset changes nothing at all.
        np.testing.assert_array_equal(self.march(visibility=lambda p, r, ds: np.ones(len(p)), phase=1)[0], Slit)
        # Default amplitude one bin on the lookup, with the density offset zero; SHADOW_JITTER 0 holds bin centres.
        rows, _ = ref.look_constants(CHROMA); np.testing.assert_array_equal(rows[7, 2:], (1., 1.)); np.testing.assert_array_equal(rows[6, 2:], (0., 0.))
        np.testing.assert_array_equal(ref.look_constants(CHROMA, tuning=centre)[0][7, 2:], (0., 0.))
        self.assertTrue(ref.stripe_map().min() == 0 and ref.stripe_map()[[0, 7, 56, 63]].all())

    def test_remap_is_soft_and_empties_thin_density(self):
        thin = Slab(); thin.value = lambda points: np.full(len(points), .2, np.float32)  # below coverage - variation
        S, T = self.march(thin)
        self.assertTrue((T == 1).all() and (S == 0).all())
        self.assertGreaterEqual(ref.TUNING['exponent'], 1.5)  # zero-slope toe
        # One far-level tap per fogged sample (the sun-ward self-shadow), and none where the field is empty.
        dense, empty = Slab(), Slab(); empty.value = lambda points: np.zeros(len(points), np.float32)
        self.march(dense); self.march(empty)
        self.assertGreater(dense.taps, 0); self.assertEqual(empty.taps, 0)

    def test_noise_margin_flags_wrap_pixels(self):
        n, margin = ref.look_noise(np.arange(128), np.zeros(128), 5.588238 * 3)
        self.assertTrue(((n >= 0) & (n < 1)).all()); self.assertTrue((margin >= 0).all() and (margin <= .5).all())


if __name__ == '__main__':
    sys.exit(unittest.main())
