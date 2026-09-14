"""The CPU reference of the ambient occlusion chain on the host.

Compiles verification/probe/ambient_occlusion_reference.h with the native
compiler and drives it on small synthetic depth images: the fp16 model, the
flat-plane identity (fronto-parallel and tilted: the stored occlusion term is
exactly 0 including the borders), the sentinel identity, a wall next to a
floor occluding the crease and not the far floor, the multiply factor's
endpoints, and the recorded plane darkening of XeGTAO's view-angle horizon. No Wine, no device.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "ambient_occlusion_reference.h"
#include <cstdio>
using namespace ao_reference;
int main() {
    Params p; p.m11 = 1.7320508; p.m00 = p.m11 * 96 / 160.; p.m22 = 1.0000030; p.m32 = -6.0000184; p.radius = 10; p.jitter_index = 3;
    const unsigned w = 160, h = 96;
    std::printf("fp16=%.10g,%.10g,%.10g,%.10g\n", fp16(1.0), fp16(1 - 1e-5), fp16(0.333333), fp16(1 - 3e-4));
    auto image = [&](auto depth_of) { std::vector<float> d(w * h, -1.f);
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            const double nx = 2 * (x + .5) / w - 1, ny = 1 - 2 * (y + .5) / h, z = depth_of(nx / p.m00, ny / p.m11);
            if (z >= 6) d[y * w + x] = float(p.m22 + p.m32 / z); }
        return d; };
    auto count = [&](const std::vector<double>& t, const std::vector<double>& z, double lo, double hi) { unsigned n = 0;
        for (std::size_t i = 0; i < t.size(); ++i) if (z[i] >= 0 && t[i] >= lo && t[i] <= hi) ++n; return n; };
    std::vector<double> z;
    auto plane = term(image([](double, double) { return 200.; }), w, h, p, &z);
    std::printf("plane_ones=%u of %zu\n", count(plane, z, 0, 0), plane.size());
    auto tilted = term(image([](double, double dy) { return dy < 0 ? -20 / dy : -1.; }), w, h, p, &z);
    unsigned covered = 0; for (double v : z) covered += v >= 0;
    std::printf("tilted_ones=%u of %u\n", count(tilted, z, 0, 0), covered);
    unsigned sentinel_ones = 0, sentinels = 0; for (std::size_t i = 0; i < z.size(); ++i) if (z[i] < 0) { ++sentinels; sentinel_ones += tilted[i] == 0; }
    std::printf("sentinel_ones=%u of %u\n", sentinel_ones, sentinels);
    // Corner at this small size: one half-res floor row spans ~12 units, so a 30-unit radius reaches the wall from the nearest rows.
    Params pc = p; pc.radius = 30;
    auto corner = term(image([](double, double dy) { const double t = dy < 0 ? -20 / dy : 1e9; return t < 100 ? t : 100.; }), w, h, pc, &z);
    double crease = 0, far = 0; unsigned nc = 0, nf = 0;
    for (unsigned y = 0; y < h / 2; ++y) for (unsigned x = 0; x < w / 2; ++x) {
        const double zz = z[y * (w / 2) + x]; if (zz < 0) continue;
        const double ny = 1 - 2 * (2 * y + .5) / h, py = ny / p.m11 * zz;
        if (std::fabs(py + 20) < 1e-6 && 100 - zz <= 15) { crease += corner[y * (w / 2) + x]; ++nc; }
        if (std::fabs(py + 20) < 1e-6 && 100 - zz >= 40 && 100 - zz <= 90) { far += corner[y * (w / 2) + x]; ++nf; } }
    std::printf("crease_mean=%.4f n=%u far_mean=%.4f n=%u\n", crease / nc, nc, far / nf, nf);
    // The recorded measurement behind the shader's tangent-plane horizon: the
    // same reference with XeGTAO's view-angle horizon on the fronto-parallel
    // plane at the fixture's size (1280x768, radius 10, z = 200); the term is
    // reported as visibility (1 - occlusion).
    { Params pv = p; pv.view_angle_horizon = true; pv.m00 = pv.m11 * 768 / 1280.; pv.jitter_index = 5;
      const unsigned W = 1280, H = 768; std::vector<float> dv(W * H, float(p.m22 + p.m32 / 200));
      std::vector<double> zv; const auto tv = term(dv, W, H, pv, &zv);
      double sum = 0, worst = 0; for (double v : tv) { sum += v; worst = std::max(worst, v); }
      std::printf("view_angle_plane_mean=%.4f min=%.4f\n", 1 - sum / tv.size(), 1 - worst); }
    std::vector<double> occ(4, .5), hz(4, 100.);
    const float d100 = float(p.m22 + p.m32 / 100);
    std::printf("factor=%.6f,%.6f,%.6f\n", factor(std::vector<double>(4, 0.), hz, 2, 2, 1, 1, d100, p), factor(occ, hz, 2, 2, 1, 1, d100, p), factor(occ, hz, 2, 2, 1, 1, -1.f, p));
    return 0;
}
'''


class AmbientOcclusionReferenceTests(unittest.TestCase):
    def test_identities_and_crease(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-ao-reference-') as temporary:
            source, executable = Path(temporary) / 'driver.cpp', Path(temporary) / 'driver'
            source.write_text(DRIVER)
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'verification/probe'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
        values = dict(line.split('=', 1) for line in run.stdout.splitlines())
        self.assertEqual(values['fp16'], '1,1,0.3332519531,0.9995117188')
        self.assertEqual(values['plane_ones'], '3840 of 3840')
        ones, _, covered = values['tilted_ones'].split()
        self.assertEqual(ones, covered)
        ones, _, sentinels = values['sentinel_ones'].split()
        self.assertEqual(ones, sentinels)
        self.assertGreater(int(sentinels), 0)
        crease = dict(part.split('=') for part in values['crease_mean'].replace('crease_mean=', 'crease=').split() if '=' in part)
        crease_mean, far_mean = float(values['crease_mean'].split()[0]), float(values['crease_mean'].split()[2].split('=')[1])
        self.assertGreater(crease_mean, 0.02)  # occlusion term: 0 is unoccluded
        self.assertEqual(far_mean, 0.0)
        self.assertGreater(int(crease['n']), 0)
        view_mean, view_min = float(values['view_angle_plane_mean'].split()[0]), float(values['view_angle_plane_mean'].split()[1].split('=')[1])
        self.assertLess(view_mean, 0.999)  # the view-angle horizon darkens a flat plane (recorded in the design note: 0.9969 / 0.8623)
        self.assertLess(view_min, 0.9)
        print('view_angle_plane mean=%.4f min=%.4f' % (view_mean, view_min))
        full, half, sentinel = (float(v) for v in values['factor'].split(','))
        self.assertEqual((full, sentinel), (1.0, 1.0))
        self.assertAlmostEqual(half, (1 - .5 * .5) ** (1 / 2.2), places=6)


if __name__ == '__main__':
    unittest.main()
