"""The replay's camera path (tools/analysis/taa_resolve_replay.py camera_previous_ndc).

A synthetic static point cloud with the camera translating forward AND yawing between the two
frames: the depth- and translation-aware path must land on the analytically projected previous
position, and the rotation-only path (the matrix the installed build uploads,
camera_far_plane_reprojection in src/renderer/camera_reprojection.h) must miss it by the parallax.
"""
import ast
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tools/analysis/taa_resolve_replay.py"
FRAME_W, FRAME_H = 1280, 768
P22, P32 = 1.000003, -6.000018          # the game's projection rows, as the replay has them
PROJ = (0.8, 1.333333, 0.0, 0.0)        # p00, p11, p20, p21 of run209


def load():
    """Compile valid() and camera_previous_ndc() out of the replay without running its CLI."""
    source = SOURCE.read_text()
    tree = ast.parse(source, filename=str(SOURCE))
    wanted = {"valid", "camera_previous_ndc"}
    functions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in wanted]
    assert {n.name for n in functions} == wanted, "replay camera-path contract changed"
    ns = {"np": np, "FAR_P22": P22, "FAR_P32": P32}
    exec(compile(ast.fix_missing_locations(ast.Module(body=functions, type_ignores=[])),
                 str(SOURCE), "exec"), ns)
    return ns["camera_previous_ndc"]


def yaw(degrees):
    """view = world * R with R orthonormal: a yaw about world Y, the engine's row-vector view."""
    a = np.radians(degrees)
    return np.array([[np.cos(a), 0.0, np.sin(a)], [0.0, 1.0, 0.0], [-np.sin(a), 0.0, np.cos(a)]])


def project(world, R, centre, proj=PROJ):
    """Current/previous NDC and the R32F depth of world points for a camera (R, centre)."""
    p00, p11, p20, p21 = proj
    view = (world - centre) @ R
    nx = view[..., 0] / view[..., 2] * p00 + p20
    ny = view[..., 1] / view[..., 2] * p11 + p21
    return nx, ny, P22 + P32 / view[..., 2], view[..., 2]


def translation(R, centre):
    return -centre @ R


class CameraPath(unittest.TestCase):
    def setUp(self):
        self.path = load()
        rng = np.random.default_rng(29)
        # A static cloud spread over 300 .. 40000 world units ahead of the camera: the run209 burst's
        # lattice sits at a few thousand, the nebula background at the far plane.
        z = np.exp(rng.uniform(np.log(300.0), np.log(40000.0), 4096))
        self.world = np.stack([rng.uniform(-.55, .55, z.size) * z,
                               rng.uniform(-.35, .35, z.size) * z, z], -1)
        # Forward flight of 136 units/frame (the burst's measured camera translation) plus 0.6 deg yaw.
        self.Rc, self.Rp = yaw(3.4), yaw(2.8)
        self.Cp = np.zeros(3)
        self.Cc = self.Cp + self.Rp[:, 2] * 136.0

    def cam(self, scale=1.0):
        centre = self.Cc + (self.Cp - self.Cc) * scale
        return dict(tc=translation(self.Rc, self.Cc), tp=translation(self.Rp, centre), Pp=PROJ), centre

    def errors(self, mode, scale=1.0):
        cam, centre = self.cam(scale)
        nx, ny, d, _ = project(self.world, self.Rc, self.Cc)
        px, py, ok = self.path(nx, ny, d, self.Rc, self.Rp, PROJ, 1, cam, mode)
        ax, ay, _, _ = project(self.world, self.Rp, centre)
        self.assertTrue(ok.all())
        return np.hypot((px - ax) * FRAME_W / 2, (py - ay) * FRAME_H / 2)

    def test_full_path_matches_the_analytic_previous_projection(self):
        self.assertLess(self.errors('full').max(), 0.01)

    def test_full_path_matches_with_an_off_centre_projection(self):
        # p20/p21 non-zero (an injected jitter in the projection): both ends must carry the terms.
        proj = (0.8, 1.333333, 0.013, -0.021)
        cam, centre = self.cam()
        cam['Pp'] = proj
        nx, ny, d, _ = project(self.world, self.Rc, self.Cc, proj)
        px, py, ok = self.path(nx, ny, d, self.Rc, self.Rp, proj, 1, cam, 'full')
        ax, ay, _, _ = project(self.world, self.Rp, centre, proj)
        self.assertTrue(ok.all())
        self.assertLess(np.hypot((px - ax) * FRAME_W / 2, (py - ay) * FRAME_H / 2).max(), 0.01)

    def test_rotation_only_path_carries_the_parallax_error(self):
        # The installed matrix ignores the translation: the error is the parallax, tens of pixels at
        # 300 units and negligible at the far plane.
        err = self.errors('rotation')
        nx, ny, d, z = project(self.world, self.Rc, self.Cc)
        self.assertGreater(np.median(err), 1.0)
        self.assertGreater(err[z < 400].min(), 5.0)
        self.assertLess(self.errors('full')[z < 400].max(), 0.01)

    def test_far_plane_agrees_between_the_two_paths(self):
        # A sentinel depth is taken at d = 1 (2e6 units): no parallax, so both paths coincide.
        cam, centre = self.cam()
        nx, ny, _, _ = project(self.world, self.Rc, self.Cc)
        sentinel = np.full(nx.shape, -1.0)
        full = self.path(nx, ny, sentinel, self.Rc, self.Rp, PROJ, 1, cam, 'full')
        rot = self.path(nx, ny, sentinel, self.Rc, self.Rp, PROJ, 1, cam, 'rotation')
        self.assertLess(np.hypot((full[0] - rot[0]) * FRAME_W / 2,
                                 (full[1] - rot[1]) * FRAME_H / 2).max(), 0.05)

    def test_translation_scaling_multiplies_the_residual(self):
        # SETA x N: N frames of camera translation in one frame. The parallax of the same cloud grows
        # about linearly while it stays small.
        one = np.median(self.errors('rotation', 1.0))
        for n in (2, 6, 10):
            got = np.median(self.errors('rotation', float(n)))
            self.assertAlmostEqual(got / one, n, delta=0.35 * n)

    def test_points_behind_the_previous_camera_are_refused(self):
        cam, _ = self.cam()
        behind = np.array([[0.0, 0.0, 50.0]])
        far = self.Rp[:, 2] * 4000.0
        nx, ny, d, _ = project(behind + far, self.Rc, self.Cc)
        # The previous camera sits 4000 units ahead of the point: its previous view z is negative.
        cam['tp'] = translation(self.Rp, self.Cc + self.Rp[:, 2] * 4000.0)
        _, _, ok = self.path(nx, ny, d, self.Rc, self.Rp, PROJ, 1, cam, 'full')
        self.assertFalse(ok.any())

    def test_no_camera_state_keeps_the_rotation_only_path(self):
        nx, ny, d, _ = project(self.world, self.Rc, self.Cc)
        a = self.path(nx, ny, d, self.Rc, self.Rp, PROJ, 1, None, 'full')
        b = self.path(nx, ny, d, self.Rc, self.Rp, PROJ, 1, self.cam()[0], 'rotation')
        np.testing.assert_array_equal(a[0], b[0])
        np.testing.assert_array_equal(a[1], b[1])


if __name__ == '__main__':
    unittest.main()
