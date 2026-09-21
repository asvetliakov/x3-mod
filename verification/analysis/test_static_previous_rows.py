"""Host unit test of src/renderer/static_previous_rows.h and MotionRowHistory::classify_miss.

The header is compiled natively (no Wine, no D3D). The expectations are built
from camera BASIS VECTORS and an object placement (never from the header's
algebra): a world point is X = A pos + T, its view coordinates are the dot
products of X - C with the camera's right/up/forward, clip.x = m00 vx + m20 vz,
clip.y = m11 vy + m21 vz, clip.w = vz and clip.z = a vz + b with a per-draw
depth law (a, b) that differs from the camera latch's P[10]/P[14].
"""
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "static_previous_rows.h"
#include "motion_row_history.h"
#include <cstdio>
#include <cstring>
using namespace x3m::renderer;
static RigidDrawKey key(std::uint64_t serial, std::uint64_t node, std::uint32_t lod, std::uint64_t vb) {
    RigidDrawKey k; k.object_lifetime = serial; k.camera_lifetime = 7; k.draw_domain = 1; k.node = node; k.camera = 9; k.lod = lod;
    k.vertex_buffer = vb; k.declaration = 3; k.position_program = 4; k.stride = 24; k.primitives = 1; k.pass = PassMainScene; return k;
}
int main(int argc, char**) {
    if (argc > 1) {
        MotionRowHistory h(16); SubmittedMatrix rows{}, previous{}; rows[15] = 1;
        const MotionRowFrame frame{1, 64, 64};
        h.begin_frame(frame);
        std::printf("%u", h.classify_miss(key(1, 100, 0, 5)));                     // no previous frame: 0
        h.lookup_and_record(key(1, 100, 0, 5), rows, previous); h.lookup_and_record(key(1, 100, 0, 6), rows, previous);
        h.lookup_and_record(key(2, 200, 0, 5), rows, previous); h.lookup_and_record(key(2, 200, 0, 5), rows, previous); // duplicate: poisoned
        h.commit(true); h.begin_frame(frame);
        std::printf(" %u", h.lookup_and_record(key(1, 100, 0, 5), rows, previous)); // matched: 1
        std::printf(" %u", h.classify_miss(key(1, 100, 1, 8)));                     // new key, object present: 2
        std::printf(" %u", h.classify_miss(key(3, 300, 0, 5)));                     // new key, new object: 1
        std::printf(" %u", h.classify_miss(key(3, 100, 0, 5)));                     // same node pointer, other lifetime serial: 1
        std::printf(" %u", h.classify_miss(key(2, 200, 0, 5)));                     // poisoned entry present: 0
        std::printf(" %u", h.classify_miss(key(1, 100, 0, 5)));                     // consumed entry present: 0
        std::printf(" %u", h.classify_miss(RigidDrawKey{}));                        // invalid key: 0
        std::printf(" %u", h.classify_miss(key(0xffffffffffffffffull, 1, 0, 5)));   // past the last entry: 1
        std::printf("\n");
        return 0;
    }
    float p0[16], v0[16], p1[16], v1[16], rows[16];
    while (true) {
        for (int i = 0; i < 16; ++i) if (std::scanf("%f", &p0[i]) != 1) return 0;
        for (int i = 0; i < 16; ++i) std::scanf("%f", &v0[i]);
        for (int i = 0; i < 16; ++i) std::scanf("%f", &p1[i]);
        for (int i = 0; i < 16; ++i) std::scanf("%f", &v1[i]);
        for (int i = 0; i < 16; ++i) std::scanf("%f", &rows[i]);
        CameraState c, p; camera_state_from_matrices(p0, v0, c); camera_state_from_matrices(p1, v1, p);
        float out[16] = {}; const bool ok = static_previous_rows(c, p, rows, out);
        std::printf("%u", ok);
        for (int i = 0; i < 16; ++i) std::printf(" %.9g", out[i]);
        std::printf("\n");
    }
}
'''


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def basis(yaw, pitch):
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    right = (cy, 0.0, -sy)
    forward = (sy * cp, -sp, cy * cp)
    up = (forward[1] * right[2] - forward[2] * right[1], forward[2] * right[0] - forward[0] * right[2], forward[0] * right[1] - forward[1] * right[0])
    return right, up, forward


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


class Camera:
    def __init__(self, yaw, pitch, position, m00=1.3, m11=1.73, m20=0.0, m21=0.0):
        self.axes = basis(yaw, pitch)
        self.position, self.m00, self.m11, self.m20, self.m21 = position, m00, m11, m20, m21

    def matrices(self):
        p = [0.0] * 16
        p[0], p[5], p[8], p[9], p[10], p[11], p[14] = self.m00, self.m11, self.m20, self.m21, 1.000003, 1.0, -6.0000184
        v = [0.0] * 16
        for j, axis in enumerate(self.axes):
            for i in range(3):
                v[i * 4 + j] = axis[i]
            v[12 + j] = -dot(self.position, axis)
        v[15] = 1.0
        return p, v

    def rows(self, linear, translation, a, b):
        """Object->clip rows from the basis vectors; `linear` is three world columns."""
        view = []
        for axis in self.axes:
            view.append([dot(axis, column) for column in linear] + [dot(axis, [t - c for t, c in zip(translation, self.position)])])
        x = [self.m00 * view[0][k] + self.m20 * view[2][k] for k in range(4)]
        y = [self.m11 * view[1][k] + self.m21 * view[2][k] for k in range(4)]
        z = [a * view[2][k] + (b if k == 3 else 0.0) for k in range(4)]
        return x + y + z + view[2]


def project(rows, point):
    clip = [dot(rows[4 * i:4 * i + 4], list(point) + [1.0]) for i in range(4)]
    return clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3], clip[3]


class StaticPreviousRows(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('no host C++ compiler')
        cls.directory = tempfile.TemporaryDirectory()
        source = Path(cls.directory.name) / 'driver.cpp'
        source.write_text(DRIVER)
        cls.binary = Path(cls.directory.name) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O1', '-I', str(ROOT / 'src/renderer'), str(source),
                        str(ROOT / 'src/renderer/motion_row_history.cpp'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_rows(self, current, previous, rows):
        values = []
        for camera in (current, previous):
            p, v = camera.matrices()
            values += p + v
        values += rows
        out = subprocess.run([str(self.binary)], input=' '.join(repr(f32(x)) for x in values) + '\n', capture_output=True, text=True, check=True).stdout.split()
        return out[0] == '1', [float(x) for x in out[1:]]

    def check(self, current, previous, linear, translation, a, b, pixels):
        ok, got = self.run_rows(current, previous, current.rows(linear, translation, a, b))
        self.assertTrue(ok)
        expected = previous.rows(linear, translation, a, b)
        worst = 0.0
        for point in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (-3, 2, 5), (4, -4, -2)):
            ex, ey, ez, ew = project(expected, point)
            gx, gy, gz, gw = project(got, point)
            self.assertGreater(ew, 0)
            worst = max(worst, abs(gx - ex) * 640, abs(gy - ey) * 360)
            self.assertLess(abs(gz - ez), 2e-5)
            self.assertLess(abs(gw - ew) / ew, 1e-4)
        self.assertLess(worst, pixels)
        return worst

    def test_rotation_translation_and_per_draw_depth_law(self):
        current = Camera(0.30, -0.10, (3.0, -2.0, 1.0))
        previous = Camera(0.27, -0.08, (2.0, -2.5, -4.0))
        linear = [(2.0, 0.0, 0.5), (0.0, 2.0, 0.0), (-0.5, 0.0, 2.0)]  # uniform-ish rotation with scale
        self.check(current, previous, linear, (10.0, 4.0, 90.0), 1.0002, -12.5, 1e-3)

    def test_off_centre_projection_terms(self):
        current = Camera(0.1, 0.05, (0.0, 0.0, 0.0), m20=0.004, m21=-0.003)
        previous = Camera(0.12, 0.04, (1.0, 0.5, -2.0), m00=1.31, m20=-0.002, m21=0.001)
        self.check(current, previous, [(1, 0, 0), (0, 1, 0), (0, 0, 1)], (2.0, -1.0, 40.0), 1.000003, -6.0000184, 1e-3)

    def test_game_scale_coordinates_stay_subpixel(self):
        # run209-like magnitudes: camera tens of thousands of units from the origin, 250 units per frame.
        current = Camera(1.2, 0.16, (-40338.7, 4663.6, 28840.6))
        previous = Camera(1.194, 0.165, (-40492.3, 4514.2, 28646.9))
        forward = current.axes[2]
        target = tuple(c + 6000.0 * f for c, f in zip(current.position, forward))
        self.check(current, previous, [(30, 0, 0), (0, 30, 0), (0, 0, 30)], target, 1.00001, -50.0, 0.05)

    def test_identical_cameras_return_the_rows(self):
        camera = Camera(0.4, 0.2, (5.0, 6.0, 7.0))
        rows = camera.rows([(1, 0, 0), (0, 1, 0), (0, 0, 1)], (5.0, 6.0, 30.0), 1.0001, -3.0)
        ok, got = self.run_rows(camera, camera, rows)
        self.assertTrue(ok)
        for g, e in zip(got, rows):
            self.assertAlmostEqual(g, e, delta=2e-4 * max(1.0, abs(e)))

    def test_refusals(self):
        current, previous = Camera(0.0, 0.0, (0, 0, 0)), Camera(0.01, 0.0, (0, 0, 0))
        good = current.rows([(1, 0, 0), (0, 1, 0), (0, 0, 1)], (0.0, 0.0, 10.0), 1.0, -1.0)
        self.assertTrue(self.run_rows(current, previous, good)[0])
        flat = list(good); flat[12:15] = [0.0, 0.0, 0.0]          # no view-z direction (the synthetic fixtures' screen-space rows)
        self.assertFalse(self.run_rows(current, previous, flat)[0])
        skew = list(good); skew[8] += 0.5                         # depth row not a*w + b
        self.assertFalse(self.run_rows(current, previous, skew)[0])
        nan = list(good); nan[3] = float('nan')
        self.assertFalse(self.run_rows(current, previous, nan)[0])

    def test_classify_miss(self):
        out = subprocess.run([str(self.binary), 'classify'], capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out, ['0', '1', '2', '1', '1', '0', '0', '0', '1'])


if __name__ == '__main__':
    unittest.main()
