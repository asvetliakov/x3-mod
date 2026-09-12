"""Host unit test of src/renderer/camera_reprojection.h against an independent oracle.

The header is compiled natively (no Wine, no D3D) into a small driver that
prints, for one (projection, view) pair per frame, the validation result, the
far-plane clip_to_previous matrix, the header's own oracle and the policy
decision. The expectations here are computed from camera BASIS VECTORS
(right/up/forward in world space), never from the header's matrix algebra, so
the row-vector, left-handed convention is asserted explicitly: view = world * V
with V's columns the camera basis, D3D NDC with +y up, w_clip = z_view.
"""
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/renderer/camera_reprojection.h'
DRIVER = r'''
#include "camera_reprojection.h"
#include <cstdio>
#include <cstring>
using namespace x3m::renderer;
int main() {
    float p0[16], v0[16], p1[16], v1[16]; double x, y; float cut; unsigned mode;
    while (true) {
        for (int i = 0; i < 16; ++i) if (std::scanf("%f", &p0[i]) != 1) return 0;
        for (int i = 0; i < 16; ++i) std::scanf("%f", &v0[i]);
        for (int i = 0; i < 16; ++i) std::scanf("%f", &p1[i]);
        for (int i = 0; i < 16; ++i) std::scanf("%f", &v1[i]);
        std::scanf("%lf %lf %f %u", &x, &y, &cut, &mode);
        CameraState c, p; CameraFailure fc, fp;
        camera_state_from_matrices(p0, v0, c, &fc); camera_state_from_matrices(p1, v1, p, &fp);
        float m[16] = {}; const bool built = camera_far_plane_reprojection(c, p, m);
        double px = 0, py = 0; const bool oracle = camera_far_plane_previous_ndc(c, p, x, y, px, py);
        const SentinelDecision d = camera_sentinel_policy(static_cast<SentinelMode>(mode), c, p, cut);
        std::printf("%u %u %u %u %u", c.valid, p.valid, unsigned(fc), unsigned(fp), built);
        for (int i = 0; i < 16; ++i) std::printf(" %.9g", m[i]);
        std::printf(" %u %.12g %.12g %.9g %u %u %u %u", oracle, px, py, camera_rotation_degrees(c, p), d.policy, d.cut, d.transform, unsigned(d.reason));
        for (int i = 0; i < 16; ++i) std::printf(" %.9g", d.matrix[i]);
        std::printf("\n");
    }
}
'''
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def projection(m00, m11, m20=0.0, m21=0.0):
    p = [0.0] * 16
    p[0], p[5], p[8], p[9], p[10], p[11], p[14] = m00, m11, m20, m21, 1.000003, 1.0, -6.0000184
    return p


def basis(yaw=0.0, pitch=0.0, roll=0.0):
    """Camera basis vectors in world space for a left-handed camera: yaw about +Y,
    then pitch about the camera's right axis, then roll about its forward axis."""
    def rot_y(v, a):
        return (v[0] * math.cos(a) + v[2] * math.sin(a), v[1], -v[0] * math.sin(a) + v[2] * math.cos(a))
    def rot_axis(v, axis, a):
        c, s = math.cos(a), math.sin(a)
        d = sum(x * y for x, y in zip(v, axis))
        cross = (axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0])
        return tuple(v[i] * c + cross[i] * s + axis[i] * d * (1 - c) for i in range(3))
    right, up, forward = (1, 0, 0), (0, 1, 0), (0, 0, 1)
    right, up, forward = rot_y(right, yaw), rot_y(up, yaw), rot_y(forward, yaw)
    up, forward = rot_axis(up, right, pitch), rot_axis(forward, right, pitch)
    right, up = rot_axis(right, forward, roll), rot_axis(up, forward, roll)
    return right, up, forward


def view(b, position=(0.0, 0.0, 0.0)):
    """Row-vector view matrix: view = world * V, V's columns are the basis."""
    right, up, forward = b
    v = list(IDENTITY)
    for i in range(3):
        v[i * 4 + 0], v[i * 4 + 1], v[i * 4 + 2] = right[i], up[i], forward[i]
    v[12] = -sum(position[i] * right[i] for i in range(3))
    v[13] = -sum(position[i] * up[i] for i in range(3))
    v[14] = -sum(position[i] * forward[i] for i in range(3))
    return v


def oracle(pc, bc, pp, bp, x, y):
    """Previous NDC of the current NDC direction (x, y) through basis vectors."""
    dv = ((x - pc[8]) / pc[0], (y - pc[9]) / pc[5], 1.0)
    world = tuple(dv[0] * bc[0][i] + dv[1] * bc[1][i] + dv[2] * bc[2][i] for i in range(3))
    prev = tuple(sum(world[i] * bp[k][i] for i in range(3)) for k in range(3))
    if prev[2] <= 0:
        return None
    return prev[0] * pp[0] / prev[2] + pp[8], prev[1] * pp[5] / prev[2] + pp[9]


def apply(matrix, x, y, z=1.0):
    clip = [sum(matrix[r * 4 + c] * v for c, v in enumerate((x, y, z, 1.0))) for r in range(4)]
    return clip


class CameraReprojection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('no host C++ compiler')
        cls.directory = tempfile.mkdtemp(prefix='x3-camera-reprojection-')
        source = Path(cls.directory) / 'driver.cpp'
        source.write_text(DRIVER)
        cls.exe = Path(cls.directory) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent),
                        str(source), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def run_driver(self, pc, vc, pp, vp, x=0.0, y=0.0, cut=20.0, mode=0):
        line = ' '.join(f'{v:.9g}' for v in list(pc) + list(vc) + list(pp) + list(vp)) + f' {x} {y} {cut} {mode}\n'
        out = subprocess.run([str(self.exe)], input=line, capture_output=True, text=True, check=True).stdout.split()
        values = [float(v) for v in out]
        return dict(valid_current=int(values[0]), valid_previous=int(values[1]), failure_current=int(values[2]),
                    failure_previous=int(values[3]), built=int(values[4]), matrix=values[5:21], oracle_valid=int(values[21]),
                    oracle=(values[22], values[23]), rotation=values[24], policy=int(values[25]), cut=int(values[26]),
                    transform=int(values[27]), reason=int(values[28]), policy_matrix=values[29:45])

    def check_direction(self, pc, bc, pp, bp, x, y, places=6):
        r = self.run_driver(pc, view(bc), pp, view(bp), x, y)
        self.assertEqual((r['valid_current'], r['valid_previous'], r['built']), (1, 1, 1))
        expected = oracle(pc, bc, pp, bp, x, y)
        clip = apply(r['matrix'], x, y)
        if expected is None:
            self.assertLessEqual(clip[3], 0.0)
            self.assertEqual(r['oracle_valid'], 0)
            return
        self.assertGreater(clip[3], 0.0)
        self.assertAlmostEqual(clip[0] / clip[3], expected[0], places=places)
        self.assertAlmostEqual(clip[1] / clip[3], expected[1], places=places)
        self.assertAlmostEqual(clip[2] / clip[3], 1.0, places=9)  # far plane
        self.assertEqual(r['oracle_valid'], 1)
        self.assertAlmostEqual(r['oracle'][0], expected[0], places=places)
        self.assertAlmostEqual(r['oracle'][1], expected[1], places=places)
        # The current z column is zero: the map depends on the direction only.
        self.assertEqual(apply(r['matrix'], x, y, 0.25), clip)

    def test_identity_to_identity(self):
        p = projection(0.8, 4 / 3)
        r = self.run_driver(p, IDENTITY, p, IDENTITY, 0.3, -0.2)
        expected = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1]
        self.assertEqual(r['matrix'], expected)
        self.assertEqual((r['policy'], r['cut'], r['transform'], r['reason']), (2, 0, 1, 0))
        self.assertEqual(r['policy_matrix'], expected)
        self.assertAlmostEqual(r['rotation'], 0.0, places=5)

    def test_yaw_pitch_roll_against_basis_oracle(self):
        pc = projection(0.8, 4 / 3)
        for yaw, pitch, roll in ((0.05, 0, 0), (-0.2, 0, 0), (0, 0.1, 0), (0, 0, 0.3), (0.15, -0.08, 0.05), (1.2, 0.4, -0.6)):
            bc = basis(yaw, pitch, roll)
            bp = basis(yaw * 0.5 + 0.02, pitch * 0.5 - 0.01, roll * 0.5)
            for x, y in ((0, 0), (0.7, -0.4), (-0.9, 0.9), (0.25, 0.5)):
                self.check_direction(pc, bc, pc, bp, x, y)

    def test_yaw_moves_the_background_the_documented_way(self):
        # Camera yawed LEFT (negative yaw about +Y, left-handed) by a small angle
        # since the previous frame: the world direction at the current center
        # was LEFT of the previous center, so its previous NDC x is negative:
        # the content moved from the left towards the center (rightward on screen).
        pc = projection(1.0, 1.0)
        r = self.run_driver(pc, view(basis(yaw=-0.1)), pc, view(basis(yaw=0.0)), 0.0, 0.0)
        clip = apply(r['matrix'], 0.0, 0.0)
        self.assertLess(clip[0] / clip[3], 0.0)
        self.assertAlmostEqual(clip[0] / clip[3], -math.tan(0.1), places=6)
        self.assertAlmostEqual(r['rotation'], math.degrees(0.1), places=4)

    def test_projection_change_scales_the_direction(self):
        pc, pp = projection(0.8, 4 / 3), projection(1.6, 2.0)
        for x, y in ((0.5, 0.25), (-0.3, 0.8)):
            self.check_direction(pc, basis(), pp, basis(), x, y)
            r = self.run_driver(pc, IDENTITY, pp, IDENTITY, x, y)
            clip = apply(r['matrix'], x, y)
            self.assertAlmostEqual(clip[0] / clip[3], x * 2.0, places=6)
            self.assertAlmostEqual(clip[1] / clip[3], y * 1.5, places=6)

    def test_off_center_terms(self):
        pc, pp = projection(0.8, 4 / 3, 0.01, -0.02), projection(0.8, 4 / 3, -0.005, 0.003)
        for x, y in ((0.0, 0.0), (0.4, -0.6)):
            self.check_direction(pc, basis(0.03), pp, basis(-0.02, 0.01), x, y)

    def test_translation_is_ignored(self):
        pc = projection(0.8, 4 / 3)
        a = self.run_driver(pc, view(basis(0.1), (0, 0, 0)), pc, view(basis(0.05), (0, 0, 0)), 0.2, 0.1)
        b = self.run_driver(pc, view(basis(0.1), (12345, -9876, 3.5)), pc, view(basis(0.05), (-1, 2, 5e5)), 0.2, 0.1)
        self.assertEqual(a['matrix'], b['matrix'])

    def test_behind_the_previous_camera_is_invalid(self):
        pc = projection(1.0, 1.0)
        # Rotated by 180 degrees: every current direction is behind the previous camera.
        self.check_direction(pc, basis(yaw=math.pi), pc, basis(yaw=0.0), 0.0, 0.0)
        self.check_direction(pc, basis(yaw=math.pi), pc, basis(yaw=0.0), 0.5, 0.5)
        # Rotated by 100 degrees: the center is behind, the far edge is not.
        r = self.run_driver(pc, view(basis(yaw=math.radians(100))), pc, view(basis()), 0.0, 0.0)
        self.assertLessEqual(apply(r['matrix'], 0, 0)[3], 0.0)
        self.assertEqual(r['oracle_valid'], 0)
        self.assertEqual((r['policy'], r['cut'], r['reason']), (1, 1, 4))

    def test_validation_failures(self):
        p, v = projection(0.8, 4 / 3), view(basis(0.2))
        bad_scale = list(p); bad_scale[0] = -0.8
        bad_w = list(p); bad_w[11] = 0.0
        nonfinite = list(v); nonfinite[5] = float('nan')
        skewed = list(v); skewed[0] = 1.01
        affine = list(v); affine[15] = 0.5
        for pc, vc, failure in ((bad_scale, v, 3), (bad_w, v, 4), (p, nonfinite, 2), (p, skewed, 5), (p, affine, 6)):
            r = self.run_driver(pc, vc, p, v)
            self.assertEqual((r['valid_current'], r['failure_current'], r['built']), (0, failure, 0), (failure, r))
            self.assertEqual((r['policy'], r['transform'], r['reason']), (1, 0, 2))
            self.assertEqual(r['policy_matrix'], IDENTITY)
        r = self.run_driver(p, v, bad_w, v)
        self.assertEqual((r['valid_previous'], r['failure_previous'], r['policy'], r['reason']), (0, 4, 1, 3))
        # Nearly orthonormal (the captured 3.6e-5 residual) passes.
        nearly = list(v); nearly[0] += 3e-5
        self.assertEqual(self.run_driver(p, nearly, p, v)['valid_current'], 1)

    def test_policy_switch_and_rotation_cut(self):
        p = projection(0.8, 4 / 3)
        vc, vp = view(basis(math.radians(25))), view(basis())
        r = self.run_driver(p, vc, p, vp, cut=20.0, mode=0)
        self.assertEqual((r['policy'], r['cut'], r['transform'], r['reason']), (1, 1, 0, 4))
        self.assertAlmostEqual(r['rotation'], 25.0, places=3)
        r = self.run_driver(p, vc, p, vp, cut=30.0, mode=0)
        self.assertEqual((r['policy'], r['cut'], r['transform'], r['reason']), (2, 0, 1, 0))
        self.assertEqual(r['policy_matrix'], r['matrix'])
        r = self.run_driver(p, vc, p, vp, cut=30.0, mode=1)
        self.assertEqual((r['policy'], r['cut'], r['transform'], r['reason']), (1, 0, 0, 1))
        self.assertEqual(r['policy_matrix'], IDENTITY)
        r = self.run_driver(p, vc, p, vp, cut=30.0, mode=2)
        self.assertEqual((r['policy'], r['reason']), (2, 0))
        r = self.run_driver(p, vc, p, vp, cut=0.0, mode=0)
        self.assertEqual((r['policy'], r['cut'], r['reason']), (1, 1, 4))


if __name__ == '__main__':
    unittest.main()
