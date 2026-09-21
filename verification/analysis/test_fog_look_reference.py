"""Stored-density fog look presets: the host look law and its constants against src/renderer/fog_look_math.h."""
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
    for (unsigned look = 0; look < fog_look_count; ++look) {
        float rows[fog_look_rows][4];
        std::printf("%.9g", double(fog_look_constants(look, FogLookTuning{}, chroma, radiance, 3 + 64 * look, rows)));
        for (auto& row : rows) for (float v : row) std::printf(" %.9g", double(v));
        std::printf("\n");
    }
    // Fade start fallback: cap - 1000 is taken as given, anything later becomes the last quarter of the cap.
    for (float start : {69000.f, 69001.f, 199000.f}) {
        FogLookTuning f; f.sky_cap = 70000.f; f.taper_start = start; float rows[fog_look_rows][4];
        fog_look_constants(1, f, chroma, radiance, 0, rows); std::printf("%.9g %.9g ", double(rows[10][1]), double(rows[10][2]));
    }
    std::printf("\n");
    FogLookTuning t; unsigned taken = 0;
    for (const auto& field : fog_look_fields) { taken += fog_look_set(t, field, field.minimum); taken += fog_look_set(t, field, field.maximum + 1.f); taken += fog_look_set(t, field, std::numeric_limits<float>::quiet_NaN()); }
    std::printf("%u %u %u %u\n", taken, unsigned(sizeof fog_look_fields / sizeof fog_look_fields[0]), fog_look_next(3), fog_look_next(0));
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
        for look in range(4):
            values = np.array(lines[look].split(), np.float64)
            rows, scale = ref.look_constants(look, CHROMA, (1., .5, 1.5), 3 + 64 * look)
            self.assertEqual(values[0], scale if look else 1.)
            np.testing.assert_allclose(values[1:], rows.ravel().astype(np.float64), rtol=2e-7, atol=0, err_msg='look %d' % look)
        # Fade 65000 -> 112500 units by default; a start that does not precede the cap falls back to its last quarter.
        rows, _ = ref.look_constants(1, CHROMA)
        np.testing.assert_allclose(rows[10, :3], (.04, 65000., 1. / 47500.), rtol=1e-6); self.assertEqual(rows[0, 3], 112500.)
        rows, _ = ref.look_constants(1, CHROMA, tuning=dict(ref.TUNING, sky_cap=70000., taper_start=70000.))
        np.testing.assert_allclose(rows[10, 1:3], (52500., 1. / 17500.), rtol=1e-6)
        # Every field takes its minimum and refuses out-of-range and NaN; the hotkey wraps 3 -> 0.
        np.testing.assert_allclose(np.array(lines[4].split(), np.float64), (69000., 1e-3, 52500., 1 / 17500., 52500., 1 / 17500.), rtol=1e-6)
        for start, expect in ((69000., 69000.), (69001., 52500.)):
            self.assertEqual(ref.look_constants(1, CHROMA, tuning=dict(ref.TUNING, sky_cap=70000., taper_start=start))[0][10, 1], expect)
        taken, fields, wrap, step = map(int, lines[5].split())
        self.assertEqual((taken, wrap, step), (fields, 0, 1)); self.assertEqual(fields, len(ref.TUNING))

    def march(self, look, store=None, **options):
        d = np.array([[1., 0., 0.], [-1., 0., 0.], [0., 1., 0.]])
        return ref.look_march(np.zeros(3), d, np.full(3, 200000.), False, CHROMA, store or Slab(), look, 3.75e-6, pixels=(np.arange(3), np.arange(3)), **options)

    def test_law_properties(self):
        S1, T1 = self.march(1); S2, T2 = self.march(2); S3, T3 = self.march(3, phase=3); S3b, _ = self.march(3, phase=4)
        # The slab is met by the first ray only: the others stay exactly empty.
        self.assertTrue((T1[1:] == 1).all() and (S1[1:] == 0).all()); self.assertLess(T1[0], .9)
        # Self-shadow and powder only remove sun light; extinction is theirs unchanged; the offset moves samples per phase.
        self.assertEqual(T2[0], T1[0]); self.assertTrue((S2[0] < S1[0]).all() and (S2[0] > 0).all())
        self.assertFalse(np.array_equal(S3, S2)); self.assertFalse(np.array_equal(S3, S3b))
        # A fully shadowed sample keeps coloured light: ambient plus the floors, a different hue from the lit one.
        Sd, Td = self.march(1, shadowed=True)
        self.assertEqual(Td[0], T1[0]); self.assertTrue((Sd[0] > 0).all() and (Sd[0] < S1[0]).all())
        self.assertGreater(Sd[0][1] / Sd[0][0], 1.2 * S1[0][1] / S1[0][0])
        # The column ends at the cap for sky and geometry alike: a hull behind the cap and the sky beside it agree.
        far = Slab(); far.value = lambda points: np.where(np.abs(np.asarray(points)[:, 0] - 120000.) < 6000., .9, 0.).astype(np.float32)
        _, Tsky = self.march(1, far)
        _, Tgeo = ref.look_march(np.zeros(3), np.array([[1., 0., 0.]]), np.array([200000.]), True, CHROMA, far, 1, 3.75e-6)
        self.assertEqual(Tsky[0], 1.); self.assertEqual(Tgeo[0], 1.)
        Ssky, Tsky = self.march(1)
        Sgeo, Tgeo = ref.look_march(np.zeros(3), np.array([[1., 0., 0.]]), np.array([150000.]), True, CHROMA, Slab(), 1, 3.75e-6)
        self.assertEqual(Tsky[0], Tgeo[0]); np.testing.assert_array_equal(Ssky[0], Sgeo[0])

    def test_remap_is_soft_and_empties_thin_density(self):
        thin = Slab(); thin.value = lambda points: np.full(len(points), .2, np.float32)  # below coverage - variation
        S, T = self.march(1, thin)
        self.assertTrue((T == 1).all() and (S == 0).all())
        self.assertGreaterEqual(ref.TUNING['exponent'], 1.5)  # zero-slope toe
        # One far-level tap per fogged sample under L2, none under L1.
        one, two = Slab(), Slab(); self.march(1, one); self.march(2, two)
        self.assertEqual(one.taps, 0); self.assertGreater(two.taps, 0)

    def test_noise_margin_flags_wrap_pixels(self):
        n, margin = ref.look_noise(np.arange(128), np.zeros(128), 5.588238 * 3)
        self.assertTrue(((n >= 0) & (n < 1)).all()); self.assertTrue((margin >= 0).all() and (margin <= .5).all())


if __name__ == '__main__':
    sys.exit(unittest.main())
