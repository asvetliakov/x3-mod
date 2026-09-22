"""Host checks of the object bounds log (--object-bounds-log, X3M_OBJECT_BOUNDS_LOG).

Three parts, none of which needs Wine or D3D:

1. src/renderer/object_bounds_projection.h compiled natively into a driver and
   run against analytic expectations on known matrices (a D3D left-handed
   perspective projection times a world translation, so the pixel box of a box
   at a known distance is computed by hand), plus the clipping, corner-count,
   eye-plane and nonfinite cases.
2. The production wiring: the log line's fields, the capture-frame and
   X3M_OBJECT_BOUNDS_LOG gates and the prerequisites the mode line records.
3. The launcher gate: --object-bounds-log maps to X3M_OBJECT_BOUNDS_LOG=1, is
   dropped when absent even if inherited, and is refused without its
   prerequisites (--dry-run only, never a launch).
"""
import contextlib
import importlib.util
import io
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/renderer/object_bounds_projection.h'
DRIVER = r'''
#include "object_bounds_projection.h"
#include <cstdio>
using namespace x3m::renderer;
int main() {
    float rows[16], lo[3], hi[3]; unsigned width, height;
    while (true) {
        for (int i = 0; i < 16; ++i) if (std::scanf("%f", &rows[i]) != 1) return 0;
        for (int i = 0; i < 3; ++i) std::scanf("%f", &lo[i]);
        for (int i = 0; i < 3; ++i) std::scanf("%f", &hi[i]);
        std::scanf("%u %u", &width, &height);
        ObjectScreenBox box{};
        const bool ok = object_screen_box(rows, lo, hi, width, height, box);
        std::printf("%u %.6f %.6f %.6f %.6f %.7f %.7f %u %u %u\n", ok, double(box.x0), double(box.y0), double(box.x1), double(box.y1),
                    double(box.zmin), double(box.zmax), box.inside, box.offscreen, box.crosses_near);
        std::fflush(stdout);
    }
}
'''
WIDTH, HEIGHT = 1280, 720
NEAR, FAR = 1.0, 100000.0
M00, M11 = 2.0, 2.0 * WIDTH / HEIGHT  # a wide-ish frustum; m11 keeps square pixels


def rows_for(tx, ty, tz, m00=M00, m11=M11, near=NEAR, far=FAR):
    """Clip rows of (world translation) x (D3D LH perspective), row-major:
    clip.x = rows[0..3] . (x, y, z, 1) and so on, w_clip = z_view."""
    q = far / (far - near)
    return [m00, 0.0, 0.0, m00 * tx,
            0.0, m11, 0.0, m11 * ty,
            0.0, 0.0, q, q * (tz - near),
            0.0, 0.0, 1.0, tz]


def load_manage():
    spec = importlib.util.spec_from_file_location('manage_for_bounds', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProjectionCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.directory = tempfile.TemporaryDirectory(prefix='x3-object-bounds-')
        path = Path(cls.directory.name)
        (path / 'driver.cpp').write_text(DRIVER)
        cls.executable = path / 'object_bounds_driver'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                                '-I', str(ROOT / 'src/renderer'), str(path / 'driver.cpp'), '-o', str(cls.executable)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(build.stdout + build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def project(self, cases):
        """One driver run over (rows, lo, hi, width, height) cases."""
        text = ''
        for rows, lo, hi, width, height in cases:
            text += ' '.join(repr(float(v)) for v in list(rows) + list(lo) + list(hi)) + f' {width} {height}\n'
        run = subprocess.run([str(self.executable)], input=text, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, run.stderr)
        out = []
        for line in run.stdout.splitlines():
            f = line.split()
            out.append({'ok': f[0] == '1', 'x0': float(f[1]), 'y0': float(f[2]), 'x1': float(f[3]), 'y1': float(f[4]),
                        'zmin': float(f[5]), 'zmax': float(f[6]), 'inside': int(f[7]),
                        'offscreen': f[8] == '1', 'near': f[9] == '1'})
        self.assertEqual(len(out), len(cases))
        return out

    def test_known_matrix_gives_the_analytic_pixel_box_and_depth_range(self):
        # A 2r cube centred on the view axis at distance d: its near face projects
        # to +-m00 * r / (d - r) in NDC x, its far face to +-m00 * r / (d + r); the
        # screen box takes the wider (near) one. Device depth of a face at z is
        # q * (z - near) / z.
        r, d = 50.0, 5000.0
        rows = rows_for(0.0, 0.0, d)
        [result] = self.project([(rows, (-r, -r, -r), (r, r, r), WIDTH, HEIGHT)])
        q = FAR / (FAR - NEAR)
        half_x = M00 * r / (d - r) * 0.5 * WIDTH
        half_y = M11 * r / (d - r) * 0.5 * HEIGHT
        self.assertTrue(result['ok'])
        self.assertFalse(result['offscreen'] or result['near'])
        self.assertEqual(result['inside'], 8)
        self.assertAlmostEqual(result['x0'], WIDTH / 2 - half_x, places=3)
        self.assertAlmostEqual(result['x1'], WIDTH / 2 + half_x, places=3)
        self.assertAlmostEqual(result['y0'], HEIGHT / 2 - half_y, places=3)
        self.assertAlmostEqual(result['y1'], HEIGHT / 2 + half_y, places=3)
        self.assertAlmostEqual(result['zmin'], q * (d - r - NEAR) / (d - r), places=6)
        self.assertAlmostEqual(result['zmax'], q * (d + r - NEAR) / (d + r), places=6)

    def test_offset_box_lands_where_the_projection_puts_it(self):
        r, d, tx, ty = 20.0, 1000.0, 300.0, -120.0
        rows = rows_for(tx, ty, d)
        [result] = self.project([(rows, (-r, -r, -r), (r, r, r), WIDTH, HEIGHT)])
        # Screen x and y of each of the eight corners straight from the matrix
        # the rows were built from: pixel = ((m * (t +- r)) / (d +- r) * .5 + .5) * size.
        xs = [(M00 * (tx + sx * r) / (d + sz * r) * 0.5 + 0.5) * WIDTH
              for sx in (-1, 1) for sz in (-1, 1)]
        ys = [(0.5 - M11 * (ty + sy * r) / (d + sz * r) * 0.5) * HEIGHT
              for sy in (-1, 1) for sz in (-1, 1)]
        self.assertAlmostEqual(result['x0'], min(xs), places=3)
        self.assertAlmostEqual(result['x1'], max(xs), places=3)
        self.assertAlmostEqual(result['y0'], min(ys), places=3)
        self.assertAlmostEqual(result['y1'], max(ys), places=3)
        self.assertEqual(result['inside'], 8)

    def test_clipping_partial_corner_count_and_offscreen(self):
        r, d = 100.0, 500.0
        # Straddling the left edge: x of the left face is beyond -w, the box is
        # clipped to 0 and only the four right-hand corners stay inside.
        edge_tx = -(d / M00) - r * 0.5      # the box centre just left of the frustum's left plane at z = d
        cases = [(rows_for(edge_tx, 0.0, d), (-r, -r, 0.0), (r, r, 0.0), WIDTH, HEIGHT),
                 (rows_for(0.0, 0.0, -2000.0), (-r, -r, -r), (r, r, r), WIDTH, HEIGHT),   # wholly behind the eye
                 (rows_for(20000.0, 0.0, d), (-r, -r, -r), (r, r, r), WIDTH, HEIGHT)]     # far off to the right
        clipped, behind, right = self.project(cases)
        self.assertEqual(clipped['x0'], 0.0)
        self.assertFalse(clipped['offscreen'])
        self.assertEqual(clipped['inside'], 4)
        self.assertTrue(behind['ok'])
        self.assertTrue(behind['offscreen'])
        self.assertEqual((behind['inside'], behind['x0'], behind['x1']), (0, 0.0, 0.0))
        self.assertTrue(behind['near'])  # every corner is at or behind the eye plane
        self.assertTrue(right['offscreen'])
        self.assertEqual(right['inside'], 0)

    def test_box_straddling_the_eye_plane_covers_the_viewport(self):
        r = 200.0
        [result] = self.project([(rows_for(0.0, 0.0, 10.0), (-r, -r, -r), (r, r, r), WIDTH, HEIGHT)])
        self.assertTrue(result['ok'] and result['near'])
        self.assertEqual((result['x0'], result['y0'], result['x1'], result['y1']), (0.0, 0.0, float(WIDTH), float(HEIGHT)))
        self.assertEqual(result['zmin'], 0.0)
        self.assertGreater(result['zmax'], 0.0)
        self.assertLess(result['inside'], 8)

    def test_refusals(self):
        r = 10.0
        rows = rows_for(0.0, 0.0, 500.0)
        nonfinite = list(rows)
        nonfinite[3] = float('inf')
        cases = [(nonfinite, (-r, -r, -r), (r, r, r), WIDTH, HEIGHT),
                 (rows, (-r, -r, -r), (r, r, r), 0, HEIGHT),
                 (rows, (-r, -r, -r), (r, r, r), WIDTH, 0)]
        for result in self.project(cases):
            self.assertFalse(result['ok'])

    def test_matches_an_independent_double_precision_oracle(self):
        cases, expected = [], []
        for tx, ty, tz, r in ((0.0, 0.0, 300.0, 40.0), (900.0, 400.0, 2000.0, 300.0), (-40.0, 0.0, 120.0, 30.0),
                              (0.0, 0.0, 60000.0, 5000.0), (150.0, -80.0, 400.0, 100.0)):
            rows = rows_for(tx, ty, tz)
            cases.append((rows, (-r, -r, -r * 0.5), (r, r * 0.5, r), WIDTH, HEIGHT))
            expected.append(oracle(rows, (-r, -r, -r * 0.5), (r, r * 0.5, r), WIDTH, HEIGHT))
        for result, want in zip(self.project(cases), expected):
            self.assertEqual((result['ok'], result['inside'], result['offscreen'], result['near']),
                             (want['ok'], want['inside'], want['offscreen'], want['near']))
            for field in ('x0', 'y0', 'x1', 'y1'):
                self.assertAlmostEqual(result[field], want[field], places=2, msg=field)
            for field in ('zmin', 'zmax'):
                self.assertAlmostEqual(result[field], want[field], places=6, msg=field)


def oracle(rows, lo, hi, width, height):
    """Independent float64 reimplementation of the header's contract."""
    xs, ys, zs = [], [], []
    inside = 0
    near = False
    for corner in range(8):
        p = ((hi if corner & 1 else lo)[0], (hi if corner & 2 else lo)[1], (hi if corner & 4 else lo)[2])
        c = [sum(rows[row * 4 + k] * p[k] for k in range(3)) + rows[row * 4 + 3] for row in range(4)]
        if not all(math.isfinite(v) for v in c):
            return {'ok': False}
        if -c[3] <= c[0] <= c[3] and -c[3] <= c[1] <= c[3] and 0.0 <= c[2] <= c[3]:
            inside += 1
        if c[3] <= 1e-6:
            near = True
            continue
        xs.append((c[0] / c[3] * 0.5 + 0.5) * width)
        ys.append((0.5 - c[1] / c[3] * 0.5) * height)
        zs.append(c[2] / c[3])
    if not xs:
        return {'ok': True, 'x0': 0.0, 'y0': 0.0, 'x1': 0.0, 'y1': 0.0, 'zmin': 0.0, 'zmax': 0.0,
                'inside': inside, 'offscreen': True, 'near': near}
    x_lo, x_hi, y_lo, y_hi = (0.0, float(width), 0.0, float(height)) if near else (min(xs), max(xs), min(ys), max(ys))
    zmin = 0.0 if near else min(zs)
    box = (max(0.0, x_lo), max(0.0, y_lo), min(float(width), x_hi), min(float(height), y_hi))
    offscreen = box[2] < box[0] or box[3] < box[1]
    return {'ok': True, 'x0': 0.0 if offscreen else box[0], 'y0': 0.0 if offscreen else box[1],
            'x1': 0.0 if offscreen else box[2], 'y1': 0.0 if offscreen else box[3],
            'zmin': zmin, 'zmax': max(zs), 'inside': inside, 'offscreen': offscreen, 'near': near}


class ProductionWiring(unittest.TestCase):
    def test_the_line_and_its_gates(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('object_bounds device=%llu frame=%llu index=%lu node=%p model=%08lx '
                      'sx0=%.1f sy0=%.1f sx1=%.1f sy1=%.1f zmin=%.6f zmax=%.6f inside=%u%s%s', source)
        # The line is written only on capture frames, only with the option, and
        # only for a draw whose object box the route already computed.
        self.assertIn('if (object_bounds_log_ && capture_ && e->state == shadow_replay::ExtentState::Known) '
                      'log_object_bounds(route, draw_rows(), e->lo, e->hi);', source)
        self.assertIn('#include "../renderer/object_bounds_projection.h"', source)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('void configure_object_bounds_log(bool requested) noexcept { object_bounds_log_ = requested; }', header)
        self.assertIn('bool object_bounds_log_=false;', header)

    def test_the_mode_line_records_both_prerequisites(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('X3M_OBJECT_BOUNDS_LOG', capture)
        self.assertIn('object_bounds_mode requested=1 enabled=%u candidates=%u object_trace=%u', capture)
        # Enabled only with the candidate route and the verified submission identity.
        self.assertIn('const bool bounds_enabled=enabled&&traced;', capture)
        self.assertIn('const bool traced=object_trace::active();', capture)


class LaunchOption(unittest.TestCase):
    # --motion-output needs --object-trace with --object-lifetime (or neither),
    # --object-lifetime needs --ownership, --shadow-replay-candidates needs both.
    PREREQUISITES = ('--object-trace', '--object-lifetime', '--shadow-replay-candidates', '--motion-output', '--ownership')

    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_option_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory, inherited={'X3M_OBJECT_BOUNDS_LOG': '1'})
            self.assertEqual(code, 0)
            self.assertNotIn('X3M_OBJECT_BOUNDS_LOG', json.loads(output)['env'])

    def test_dry_run_carries_the_switch(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory, *self.PREREQUISITES)[1])
            code, output, error = self.launch(directory, *self.PREREQUISITES, '--object-bounds-log')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']},
                             {'X3M_OBJECT_BOUNDS_LOG': '1'})

    def test_prerequisites_are_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            # Without the candidate route there is no object box to project.
            kept = [option for option in self.PREREQUISITES if option != '--shadow-replay-candidates']
            code, _, error = self.launch(directory, *kept, '--object-bounds-log')
            self.assertEqual(code, 2)
            self.assertIn('--object-bounds-log requires', error)
            # Sentinel-only motion output (no verified submission identity) has no node/model.
            code, _, error = self.launch(directory, '--motion-output', '--ownership',
                                         '--shadow-replay-candidates', '--object-bounds-log')
            self.assertEqual(code, 2)
            self.assertIn('--object-bounds-log requires', error)
            # --shadow-replay-depth implies the candidate counter and is accepted.
            code, _, error = self.launch(directory, *[o for o in self.PREREQUISITES if o != '--shadow-replay-candidates'],
                                         '--shadow-replay-depth', '--object-bounds-log')
            self.assertEqual(code, 0, error)


if __name__ == '__main__':
    unittest.main()
