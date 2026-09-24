"""Host tests of the chase camera's pose pipeline (src/proxy/chase_camera_math.h).

The production header is compiled with the host compiler through
verification/probe/chase_camera_host.cpp and driven frame by frame with
synthetic ship/camera poses in the engine's own convention (basis rows =
right/up/forward axes, row-vector products, int32 positions). Covered:
first-frame snap, the critically damped closed form against its analytic
solution, frame-rate (SETA) independence, lag clamps, the snap conditions
(teleport, ship, sector, mode), pass-through verdicts (internal view, views
that are not behind the ship, scripted connect modes), NaN/non-orthonormal
input guards, distance scaling, the below-centre pitch, and the
camera = view_rel * ship angular identity. These math controls do not establish
cursor-fire admission or finite camera-to-muzzle convergence in the game.
Review 31 additions: the verbatim-basis pass-through (connect mode 3,
+0x1a0 & 4), snap coalescing, back-view hysteresis, the compiled defaults and
the combat-tightness scaling of the time constants.
"""
import math
import contextlib
import hashlib
import io
import json
import os
import sys
from unittest import mock
import random
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1]
DEFAULT_TUNABLES = dict(rot_tau=0.20, pos_tau=0.30, offset_y=0.0, pitch_down_deg=0.0, distance_scale=1.0, lag_clamp_deg=90.0, pos_lag_clamp=1.0, max_dt=0.1, snap_ratio=20.0, fov_compensate=0)


def yaw(theta):
    """Basis rows (right, up, forward) of a ship yawed by theta about world Y (left-handed, det +1)."""
    c, s = math.cos(theta), math.sin(theta)
    return [c, 0, -s, 0, 1, 0, s, 0, c]


def pitch(theta):
    c, s = math.cos(theta), math.sin(theta)
    return [1, 0, 0, 0, c, -s, 0, s, c]


def roll(phi):
    c, s = math.cos(phi), math.sin(phi)
    return [c, s, 0, -s, c, 0, 0, 0, 1]


def mat_mul(a, b):
    return [sum(a[3 * i + k] * b[3 * k + j] for k in range(3)) for i in range(3) for j in range(3)]


def vec_mat(v, b):
    return [sum(v[k] * b[3 * k + j] for k in range(3)) for j in range(3)]


def transpose(a):
    return [a[3 * j + i] for i in range(3) for j in range(3)]


def rotation_angle_deg(a, b):
    w = mat_mul(transpose(a), b)
    c = max(-1.0, min(1.0, (w[0] + w[4] + w[8] - 1) / 2))
    return math.degrees(math.acos(c))


class Driver:
    def __init__(self, exe):
        self.proc = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(self, line):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()
        return self.proc.stdout.readline().split()

    def tunables(self, **kw):
        t = {**DEFAULT_TUNABLES, 'combat_tightness': 0.0, 'snap_coalesce_frames': 3, **kw}
        out = self.send('T {rot_tau} {pos_tau} {offset_y} {distance_scale} {lag_clamp_deg} {pos_lag_clamp} {max_dt} {snap_ratio} {combat_tightness} {snap_coalesce_frames} {pitch_down_deg} {fov_compensate:g}'.format(**t))
        assert out[0] == 'T'
        return int(out[1]) == 1

    def defaults(self):
        out = self.send('D')
        assert out[0] == 'D', out
        names = ['rot_tau', 'pos_tau', 'offset_y', 'distance_scale', 'lag_clamp_deg', 'pos_lag_clamp', 'combat_tightness', 'max_dt', 'snap_ratio', 'snap_coalesce_frames', 'pitch_down_deg', 'fov_compensate']
        return dict(zip(names, map(float, out[1:])))

    def fov_factor(self, half_vfov_tan, enabled=True):
        out = self.send(f'V {half_vfov_tan!r} {int(enabled)}')
        assert out[0] == 'V', out
        return float(out[1])

    def reset(self):
        self.send('R')

    def long_run(self, steps, dt, yaw_rate, roll_rate):
        out = self.send(f'L {steps} {dt!r} {yaw_rate!r} {roll_rate!r}')
        assert out[0] == 'L', out
        return dict(applied=int(out[1]), refused=int(out[2]), snaps=int(out[3]), max_ortho=float(out[4]),
                    max_lag_deg=float(out[5]), max_identity=float(out[6]), lag_deg=float(out[7]))

    def exp_log(self, v):
        out = self.send('X ' + ' '.join(map(repr, v)))
        return [float(x) for x in out[1:4]], float(out[4])

    def frame(self, dt, ship_pos=(0, 0, 0), ship=IDENTITY, boom=(0, 40, -200), view_rel=IDENTITY, mode=2, connect=0, ref=1, sector=1, half_vfov_tan=0.75, flags=0, locked=False):
        line = ' '.join(['F', repr(dt), str(mode), str(connect), str(ref), str(sector), *map(repr, ship_pos), *map(repr, ship), *map(repr, boom), *map(repr, view_rel), repr(half_vfov_tan),
                         str(flags), str(int(locked))])
        out = self.send(line)
        assert out[0] == 'F', out
        r = dict(verdict=int(out[1]), snapped=bool(int(out[2])), coalesced=bool(int(out[3])), locked=bool(int(out[4])), snap_reason=int(out[5]),
                 lag_deg=float(out[6]), pos_lag=float(out[7]), distance=float(out[8]))
        if r['verdict'] == 0:
            r['pos'] = [float(x) for x in out[9:12]]
            r['basis'] = [float(x) for x in out[12:21]]
            r['view_rel'] = [float(x) for x in out[21:30]]
            r['ortho_error'] = float(out[30])
        return r

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        self.proc.stdout.close()


class ChaseCameraPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('no host C++ compiler')
        cls.directory = tempfile.mkdtemp(prefix='x3-chase-camera-')
        cls.exe = Path(cls.directory) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        str(ROOT / 'verification/probe/chase_camera_host.cpp'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def setUp(self):
        self.d = Driver(self.exe)
        self.assertTrue(self.d.tunables())

    def tearDown(self):
        self.d.close()

    # --- basics -------------------------------------------------------------
    def test_snap_broadcast_is_seen_once_by_each_device(self):
        self.assertEqual(self.d.send('E'), ['E', '0', '1', '0', '1', '0', '1', '1'])

    def test_leaving_applied_pose_cuts_once_then_reentry_cuts(self):
        self.assertEqual(self.d.send('C'), ['C', '0', '1', '0', '1', '0', '1', '0', '1', '0'])

    def test_native_anchor_branches_pointer_guards_and_translation_invariance(self):
        result = self.d.send('N')
        self.assertEqual(result[:3], ['N', '16', '0'])
        self.assertLess(float(result[3]), 1e-9)
        self.assertGreater(float(result[4]), 50)  # old anchor visibly displaces the camera
        self.assertLess(float(result[5]), 1e-9)
        self.assertEqual(result[6], '0')

    def test_scene_fix_uses_current_vanilla_view_and_preserves_shake(self):
        result = self.d.send('J')
        self.assertLess(float(result[1]), 1e-12)
        self.assertGreater(float(result[2]), 0.1)  # previous-frame reference is demonstrably wrong

    def test_new_default_frames_centred_anchor_at_75_percent(self):
        offset_y = self.d.defaults()['offset_y']
        self.d.tunables(offset_y=offset_y)
        result = self.d.frame(1 / 60, ship_pos=(0, 0, 0), boom=(0, 0, -200), half_vfov_tan=0.75)
        camera_space = vec_mat([-x for x in result['pos']], transpose(result['basis']))
        screen_y = 0.5 - 0.5 * camera_space[1] / (camera_space[2] * 0.75)
        self.assertAlmostEqual(screen_y, 0.5 + 0.5 * offset_y, places=12)
        self.assertAlmostEqual(screen_y, 0.75, places=12)

    def test_elevated_default_projects_low_while_looking_down_and_preserves_distance(self):
        # The 2026-09-16 defaults: near-parallel 0.5 degrees down with the
        # anchor at 75% of screen height (chase-hud-reticle-survey.md).
        defaults = self.d.defaults()
        pitch_down_deg, offset_y = defaults['pitch_down_deg'], defaults['offset_y']
        self.assertEqual((pitch_down_deg, offset_y), (0.5, 0.50))
        self.d.tunables(**defaults)
        for vfov in (0.25, 0.5625, 0.75, 1.5):
            self.d.reset()
            r = self.d.frame(1 / 60, boom=(0, 40, -200), half_vfov_tan=vfov)
            self.assertEqual(r['verdict'], 0)
            ray = vec_mat([-x for x in r['pos']], transpose(r['basis']))
            self.assertAlmostEqual(0.5 - ray[1] / ray[2] / vfov / 2, 0.5 + 0.5 * offset_y, places=12)
            self.assertAlmostEqual(r['basis'][7], -math.sin(math.radians(pitch_down_deg)), places=12)
            self.assertGreater(r['pos'][1], 0)
            self.assertLess(r['pos'][2], 0)
            factor = min(max(0.75 / vfov, 0.5), 2.0)   # the compiled default compensates the FOV
            self.assertAlmostEqual(r['distance'], 1.05 * factor * math.hypot(40, 200), places=10)
            alpha = math.radians(pitch_down_deg) + math.atan(offset_y * vfov)
            self.assertAlmostEqual(math.atan2(r['pos'][1], -r['pos'][2]), alpha, places=12)

    def test_elevated_framing_handles_native_pitch_yaw_roll_and_ship_orientation(self):
        offset_y = self.d.defaults()['offset_y']
        self.d.tunables(**{**self.d.defaults(), 'distance_scale': 1.7, 'pitch_down_deg': 10})
        ship = mat_mul(roll(0.8), yaw(1.2))
        native = mat_mul(roll(0.12), mat_mul(pitch(0.2), yaw(0.15)))
        boom, position = (15, 70, -200), (150000, -200000, 123000)
        r = self.d.frame(1 / 60, ship=ship, ship_pos=position, view_rel=native, boom=boom)
        self.assertEqual(r['verdict'], 0)
        ray = vec_mat([a-b for a,b in zip(position,r['pos'])], transpose(r['basis']))
        native_ray = vec_mat([-x for x in boom], transpose(native))
        self.assertAlmostEqual(ray[0] / ray[2], native_ray[0] / native_ray[2], places=10)
        self.assertAlmostEqual(ray[1] / ray[2], -offset_y * 0.75, places=10)
        relative = mat_mul(r['basis'], transpose(ship))
        self.assertAlmostEqual(relative[7], -math.sin(math.radians(10)), places=12)
        self.assertAlmostEqual(relative[1], 0, places=12)  # native local roll removed
        self.assertAlmostEqual(r['distance'], math.sqrt(sum(x*x for x in boom))*1.7, places=9)
        self.assertLess(rotation_angle_deg(mat_mul(r['view_rel'], ship), r['basis']), 0.00001)

    def test_zero_pitch_restores_exact_legacy_geometry(self):
        self.d.tunables(offset_y=0.45, pitch_down_deg=0, distance_scale=1.4, rot_tau=0.15, pos_tau=0.20)
        ship, native, boom = yaw(0.4), mat_mul(roll(0.1), pitch(-0.1)), (20, 50, -200)
        r = self.d.frame(1 / 60, ship=ship, view_rel=native, boom=boom)
        expected_pos = vec_mat([x*1.4 for x in boom], ship)
        expected_basis = mat_mul(pitch(math.atan(0.45*0.75)), mat_mul(native, ship))
        for a, b in zip(r['pos'], expected_pos):
            self.assertAlmostEqual(a, b, places=10)
        for a, b in zip(r['basis'], expected_basis):
            self.assertAlmostEqual(a, b, places=12)

    def test_elevated_translation_remains_invariant_and_reentry_snaps(self):
        self.d.tunables(**self.d.defaults())
        first = self.d.frame(1 / 60)
        for n in range(1, 101):
            p = (n*10, n*20, n*100)
            r = self.d.frame(1 / 60, ship_pos=p)
            self.assertFalse(r['snapped'])
            self.assertAlmostEqual(r['pos_lag'], 0, places=10)
            for actual, origin, offset in zip(r['pos'], p, first['pos']):
                self.assertAlmostEqual(actual-origin, offset, places=10)
        self.assertEqual(self.d.frame(1 / 60, connect=4)['verdict'], 3)
        self.assertTrue(self.d.frame(1 / 60)['snapped'])
        self.assertEqual(self.d.frame(1 / 60, ship_pos=(100000, 0, 0))['snap_reason'], 16)

    def test_elevated_tunables_and_frame_geometry_fail_closed(self):
        for value in (-0.1, 30.01, 89, 90, float('inf'), float('nan')):
            self.assertFalse(self.d.tunables(pitch_down_deg=value), value)
            self.assertEqual(self.d.frame(1/60)['verdict'], 4)
        self.assertTrue(self.d.tunables(pitch_down_deg=30, offset_y=1))
        for vfov in (10, 1e308, float('inf'), float('nan')):
            self.assertEqual(self.d.frame(1/60, half_vfov_tan=vfov)['verdict'], 4)
        self.assertTrue(self.d.tunables(pitch_down_deg=1, offset_y=-1))
        self.assertEqual(self.d.frame(1/60, half_vfov_tan=10)['verdict'], 4)
        self.assertTrue(self.d.tunables(pitch_down_deg=30, offset_y=-1))
        r = self.d.frame(1/60, half_vfov_tan=0.75)
        self.assertEqual(r['verdict'], 0)
        ray = vec_mat([-x for x in r['pos']], transpose(r['basis']))
        self.assertAlmostEqual(ray[1]/ray[2], 0.75, places=12)

    def test_elevated_front_side_internal_scripted_gates_are_preserved(self):
        self.d.tunables(**self.d.defaults())
        for kwargs, verdict in (({'boom': (0,0,200)},2), ({'boom':(200,0,-100)},2),
                                ({'mode':1},1), ({'connect':3},7), ({'connect':4},3), ({'flags':4},7)):
            self.assertEqual(self.d.frame(1/60, **kwargs)['verdict'], verdict)
        self.assertTrue(self.d.frame(1/60)['snapped'])

    def test_elevated_long_turn_is_stable_with_original_bounds(self):
        self.d.tunables(**self.d.defaults())
        r = self.d.long_run(100000, 1/60, 1.1, 0.5)
        self.assertEqual((r['applied'], r['refused'], r['snaps']), (100000, 0, 1))
        self.assertLess(r['max_ortho'], 1e-12)
        self.assertLess(r['max_identity'], 1e-12)
        self.assertLessEqual(r['max_lag_deg'], 8.000001)

    def test_softer_default_settles_more_slowly_without_increasing_bounds(self):
        lags = []
        for rot_tau, pos_tau in ((0.22, 0.30), (0.28, 0.38)):
            self.d.reset()
            self.d.tunables(**{**self.d.defaults(), 'rot_tau':rot_tau, 'pos_tau':pos_tau})
            self.d.frame(1/60)
            for _ in range(30):
                r = self.d.frame(1/60, ship=yaw(math.radians(5)))
            lags.append((r['lag_deg'], r['pos_lag']))
        self.assertGreater(lags[1][0], lags[0][0])
        self.assertGreater(lags[1][1], lags[0][1])

    def test_first_frame_snaps_to_the_vanilla_pose(self):
        r = self.d.frame(1 / 60, ship_pos=(1000, -500, 250000), boom=(0, 40, -200))
        self.assertEqual(r['verdict'], 0)
        self.assertTrue(r['snapped'])
        self.assertEqual(r['snap_reason'], 1)
        self.assertEqual(r['pos'], [1000, -460, 249800])
        self.assertEqual(r['basis'], IDENTITY)
        self.assertEqual(r['view_rel'], IDENTITY)
        self.assertAlmostEqual(r['distance'], math.hypot(40, 200), places=9)
        self.assertEqual(r['lag_deg'], 0)

    def test_tunables_are_validated(self):
        self.assertFalse(self.d.tunables(rot_tau=0))
        self.assertFalse(self.d.tunables(lag_clamp_deg=91))
        self.assertFalse(self.d.tunables(distance_scale=-1))
        self.assertTrue(self.d.tunables())

    def test_output_basis_is_orthonormal_and_consistent_with_view_rel(self):
        random.seed(7)
        ship = IDENTITY
        self.d.frame(1 / 60, ship=ship)
        for i in range(120):
            ship = mat_mul(roll(0.01 * i), yaw(0.02 * i))
            r = self.d.frame(1 / 60, ship=ship, ship_pos=(10 * i, 0, 300 * i))
            self.assertEqual(r['verdict'], 0)
            self.assertLess(r['ortho_error'], 1e-9)
            recomposed = mat_mul(r['view_rel'], ship)
            self.assertTrue(all(abs(a - b) < 1e-9 for a, b in zip(recomposed, r['basis'])), 'camera != view_rel * ship')

    # --- spring dynamics ----------------------------------------------------
    def test_step_response_matches_the_closed_form(self):
        tau, dt, step_deg = 0.2, 1 / 60, 20.0
        self.d.tunables(rot_tau=tau)
        self.d.frame(dt)
        target = yaw(math.radians(step_deg))
        for n in range(1, 61):
            r = self.d.frame(dt, ship=target)
            t = n * dt
            expected = step_deg * (1 + t / tau) * math.exp(-t / tau)
            self.assertAlmostEqual(r['lag_deg'], expected, places=6, msg=f'frame {n}')
            self.assertAlmostEqual(rotation_angle_deg(r['basis'], target), expected, places=6)
        self.assertLess(r['lag_deg'], 1.0)  # 20 * 6 e^-5 = 0.81 deg after 1 s
        for _ in range(240):
            r = self.d.frame(dt, ship=target)
        self.assertLess(r['lag_deg'], 1e-6)

    def test_spring_closed_form_direct(self):
        out = self.d.send('S 1.0 0.0 0.25 0.05 10')
        t, tau = 0.5, 0.25
        self.assertAlmostEqual(float(out[1]), (1 + t / tau) * math.exp(-t / tau), places=10)
        self.assertAlmostEqual(float(out[2]), -(t / tau ** 2) * math.exp(-t / tau), places=10)

    def test_frame_rate_independence_of_the_step_response(self):
        target = yaw(math.radians(15))
        results = {}
        for hz in (30, 120):
            self.d.reset()
            self.d.frame(1 / hz)
            for _ in range(hz // 2):
                r = self.d.frame(1 / hz, ship=target)
            results[hz] = r['lag_deg']
        self.assertAlmostEqual(results[30], results[120], places=9)

    def test_seta_like_time_compression_does_not_enter(self):
        # The same wall-clock history sampled at 30 and 120 Hz while the ship
        # turns at a constant rate ends within a few percent of each other.
        results = {}
        for hz in (30, 120):
            self.d.reset()
            self.d.frame(1 / hz)
            for n in range(hz):
                r = self.d.frame(1 / hz, ship=yaw(math.radians(30) * (n + 1) / hz))
            results[hz] = r['lag_deg']
        self.assertGreater(results[120], 1.0)
        self.assertLess(abs(results[30] - results[120]) / results[120], 0.05)

    def test_zero_dt_keeps_the_state(self):
        self.d.frame(1 / 60)
        target = yaw(math.radians(10))
        a = self.d.frame(1 / 60, ship=target)['lag_deg']
        b = self.d.frame(0.0, ship=target)['lag_deg']
        self.assertAlmostEqual(a, b, places=12)

    def test_large_dt_is_clamped(self):
        self.d.tunables(max_dt=0.1, rot_tau=0.2)
        self.d.frame(1 / 60)
        r = self.d.frame(5.0, ship=yaw(math.radians(20)))
        expected = 20 * (1 + 0.5) * math.exp(-0.5)
        self.assertAlmostEqual(r['lag_deg'], expected, places=6)

    def test_orientation_lag_clamp_bounds_the_ship_window(self):
        self.d.tunables(lag_clamp_deg=10)
        self.d.frame(1 / 60)
        r = self.d.frame(1 / 60, ship=yaw(math.radians(60)))
        self.assertLessEqual(r['lag_deg'], 10 + 1e-9)
        self.assertGreater(r['lag_deg'], 9.99)
        for _ in range(300):
            r = self.d.frame(1 / 60, ship=yaw(math.radians(60)))
        self.assertLess(r['lag_deg'], 1e-6)

    def test_roll_is_followed_with_lag(self):
        self.d.frame(1 / 60)
        r = self.d.frame(1 / 60, ship=roll(math.radians(30)))
        self.assertGreater(r['lag_deg'], 29)  # one frame in: the camera has barely started rolling
        self.assertGreater(rotation_angle_deg(r['basis'], IDENTITY), 0.05)
        for _ in range(120):
            r = self.d.frame(1 / 60, ship=roll(math.radians(30)))
        self.assertLess(rotation_angle_deg(r['basis'], roll(math.radians(30))), 0.02)  # 30 * 11 e^-10 after 2 s

    def test_boom_swings_and_settles_within_the_position_clamp(self):
        self.d.tunables(pos_lag_clamp=0.2)
        boom = (0, 0, -200)
        self.d.frame(1 / 60, boom=boom)
        r = self.d.frame(1 / 60, ship=yaw(math.radians(90)), boom=boom)
        self.assertGreater(r['pos_lag'], 1.0)
        self.assertLessEqual(r['pos_lag'], 40 + 1e-9)
        for _ in range(600):
            r = self.d.frame(1 / 60, ship=yaw(math.radians(90)), boom=boom)
        self.assertLess(r['pos_lag'], 1e-6)
        self.assertTrue(all(abs(a - b) < 1e-6 for a, b in zip(r['pos'], vec_mat(boom, yaw(math.radians(90))))))

    def test_constant_velocity_produces_no_lag(self):
        self.d.frame(1 / 60)
        for n in range(1, 60):
            r = self.d.frame(1 / 60, ship_pos=(0, 0, 500 * n))
            self.assertEqual(r['pos_lag'], 0)
            self.assertEqual(r['lag_deg'], 0)
            self.assertEqual(r['pos'], [0, 40, 500 * n - 200])

    # --- geometry -----------------------------------------------------------
    def test_distance_scale_multiplies_the_vanilla_boom(self):
        self.d.tunables(distance_scale=2.0)
        r = self.d.frame(1 / 60, boom=(0, 40, -200))
        self.assertEqual(r['pos'], [0, 80, -400])
        self.assertAlmostEqual(r['distance'], 2 * math.hypot(40, 200), places=9)

    def test_offset_y_puts_the_ship_below_centre(self):
        self.d.tunables(offset_y=0.2)
        r = self.d.frame(1 / 60, boom=(0, 0, -200), half_vfov_tan=0.75)
        forward = r['basis'][6:9]
        self.assertGreater(forward[1], 0)  # tilted toward up
        self.assertAlmostEqual(math.degrees(math.atan2(forward[1], forward[2])), math.degrees(math.atan(0.2 * 0.75)), places=9)
        relative = vec_mat([-p for p in r['pos']], transpose(r['basis']))  # ship in camera coordinates
        self.assertGreater(relative[2], 0)
        self.assertAlmostEqual(relative[1] / relative[2], -0.2 * 0.75, places=9)  # 20 % of the half height below centre

    def test_external_views_not_behind_the_ship_pass_through(self):
        self.assertEqual(self.d.frame(1 / 60, boom=(0, 40, 200))['verdict'], 2)      # front view
        self.assertEqual(self.d.frame(1 / 60, boom=(300, 0, -100))['verdict'], 2)    # side view
        self.assertEqual(self.d.frame(1 / 60, boom=(0, 40, -200), view_rel=yaw(math.pi))['verdict'], 2)  # looking away
        r = self.d.frame(1 / 60, boom=(0, 40, -200))
        self.assertEqual((r['verdict'], r['snapped'], r['snap_reason']), (0, True, 1))

    def test_internal_view_and_scripted_connect_modes_pass_through(self):
        self.assertEqual(self.d.frame(1 / 60, mode=1)['verdict'], 1)
        for connect in (1, 2, 4, 5, 6, 7, 8, 9):
            self.assertEqual(self.d.frame(1 / 60, connect=connect)['verdict'], 3, connect)
        self.assertEqual(self.d.frame(1 / 60, connect=0)['verdict'], 0)

    def test_verbatim_basis_frames_pass_through(self):
        # Review 31 A2: connect mode 3 and +0x1a0 & 4 make the engine write the
        # basis verbatim (derived ship basis = identity): no write, and the
        # state is dropped so the return snaps.
        self.d.frame(1 / 60)
        self.d.frame(1 / 60, ship=yaw(0.3))
        self.assertEqual(self.d.frame(1 / 60, connect=3)['verdict'], 7)
        self.assertEqual(self.d.frame(1 / 60, flags=0x4)['verdict'], 7)
        self.assertEqual(self.d.frame(1 / 60, flags=0x1f)['verdict'], 7)
        r = self.d.frame(1 / 60, ship=yaw(0.3), flags=0xb)  # bit 2 clear: ordinary follow, back with a snap
        self.assertEqual((r['verdict'], r['snapped'], r['snap_reason'] & 1), (0, True, 1))

    def test_back_view_hysteresis_keeps_a_tracked_view_through_the_boundary(self):
        # Review 31 A6: enter at |x| <= 0.6 z, forward > 0.7; leave only past
        # |x| > 0.8 z or forward < 0.5, so a transition wobbling around the
        # entry threshold cannot refuse (and re-snap) every other frame.
        self.assertEqual(self.d.frame(1 / 60, boom=(140, 0, -200))['verdict'], 2)  # 0.7 z: not entered
        self.assertEqual(self.d.frame(1 / 60, boom=(0, 0, -200))['verdict'], 0)
        r = self.d.frame(1 / 60, boom=(140, 0, -200))
        self.assertEqual((r['verdict'], r['snapped']), (0, False))  # kept while tracked
        self.assertEqual(self.d.frame(1 / 60, boom=(180, 0, -200))['verdict'], 2)  # 0.9 z: left
        self.assertEqual(self.d.frame(1 / 60, boom=(140, 0, -200))['verdict'], 2)  # and stays out until the entry threshold
        self.assertTrue(self.d.frame(1 / 60, boom=(0, 0, -200))['snapped'])
        fwd_enter, fwd_keep = yaw(math.radians(50)), yaw(math.radians(55))  # cos 50 = 0.64 < 0.7; cos 55 = 0.57 > 0.5
        self.assertEqual(self.d.frame(1 / 60, view_rel=fwd_enter)['verdict'], 0)  # tracked: 0.64 > 0.5 keeps it
        self.assertEqual(self.d.frame(1 / 60, view_rel=fwd_keep)['verdict'], 0)
        self.assertEqual(self.d.frame(1 / 60, view_rel=yaw(math.radians(65)))['verdict'], 2)  # cos 65 = 0.42: left
        self.assertEqual(self.d.frame(1 / 60, view_rel=fwd_enter)['verdict'], 2)  # not re-entered below 0.7

    def test_compiled_defaults_match_review_and_first_flight_framing(self):
        d = self.d.defaults()
        self.assertEqual(d['rot_tau'], 0.28)
        self.assertEqual(d['pos_tau'], 0.38)
        self.assertEqual(d['pitch_down_deg'], 0.5)
        self.assertEqual(d['lag_clamp_deg'], 8.0)
        self.assertEqual(d['pos_lag_clamp'], 0.10)
        self.assertEqual(d['offset_y'], 0.50)
        self.assertEqual(d['distance_scale'], 1.05)   # 0.90 before 2026-09-23
        self.assertEqual(d['combat_tightness'], 0.0)
        self.assertEqual(d['max_dt'], 0.10)
        self.assertEqual(d['snap_coalesce_frames'], 3)
        self.assertEqual(d['fov_compensate'], 1)

    # --- FOV compensation (docs/verification/field-of-view.md, "Chase camera compensation") ---
    def test_fov_compensation_factor(self):
        self.assertEqual(self.d.fov_factor(0.75), 1.0)                          # vanilla F 0x4000
        self.assertAlmostEqual(self.d.fov_factor(0.5625), 4 / 3, places=15)     # --fov default 90 (F 0x3470)
        self.assertEqual(self.d.fov_factor(0.3), 2.0)                           # clamp: 2.5 -> 2
        self.assertEqual(self.d.fov_factor(0.375), 2.0)                         # exactly at the upper bound
        self.assertEqual(self.d.fov_factor(1.5), 0.5)                           # exactly at the lower bound
        self.assertEqual(self.d.fov_factor(3.0), 0.5)                           # clamp: 0.25 -> 0.5
        for value in (0.3, 0.5625, 0.75, 3.0):
            self.assertEqual(self.d.fov_factor(value, enabled=False), 1.0)      # off
        for value in (0.0, -1.0, float('inf'), float('nan')):
            self.assertEqual(self.d.fov_factor(value), 1.0)

    def test_fov_compensation_scales_the_boom_on_top_of_distance_scale(self):
        boom = (0, 40, -200)
        for pitch_down in (0.0, 0.5):
            for enabled, vfov, factor in ((1, 0.75, 1.0), (1, 0.5625, 4 / 3), (1, 0.3, 2.0), (1, 3.0, 0.5), (0, 0.5625, 1.0)):
                with self.subTest(pitch_down=pitch_down, enabled=enabled, vfov=vfov):
                    self.assertTrue(self.d.tunables(distance_scale=1.05, pitch_down_deg=pitch_down, offset_y=0.5,
                                                    fov_compensate=enabled))
                    self.d.reset()
                    r = self.d.frame(1 / 60, boom=boom, half_vfov_tan=vfov)
                    self.assertEqual(r['verdict'], 0)
                    self.assertAlmostEqual(r['distance'], 1.05 * factor * math.hypot(*boom), places=9)
        # A later FOV change (the in-game menu, zoom) moves the target; the spring follows without a snap.
        self.d.tunables(**{**self.d.defaults(), 'pos_lag_clamp': 1.0})
        self.d.reset()
        first = self.d.frame(1 / 60, half_vfov_tan=0.5625)
        changed = [self.d.frame(1 / 60, half_vfov_tan=0.75) for _ in range(240)]
        self.assertFalse(any(r['snapped'] for r in changed))
        self.assertAlmostEqual(first['distance'], 1.05 * 4 / 3 * math.hypot(40, 200), places=9)
        self.assertAlmostEqual(changed[-1]['distance'], 1.05 * math.hypot(40, 200), delta=0.1)   # settled within 0.05%

    def test_fov_step_at_the_default_lag_clamp(self):
        # At the compiled pos_lag_clamp (0.10 of the target boom) a FOV step whose boom change exceeds
        # 10 % jumps in its first frame to the clamp edge; the spring carries the rest without a snap.
        # A smaller step stays inside the clamp and moves smoothly.
        defaults = self.d.defaults()
        self.assertEqual(defaults['pos_lag_clamp'], 0.10)
        vanilla = 1.05 * math.hypot(40, 200)
        for new_vfov, jumps in ((0.75, True), (0.62, True), (0.60, False)):
            with self.subTest(new_vfov=new_vfov):
                self.d.tunables(**defaults)
                self.d.reset()
                first = self.d.frame(1 / 60, half_vfov_tan=0.5625)
                self.assertAlmostEqual(first['distance'], vanilla * 4 / 3, places=9)
                target = vanilla * 0.75 / new_vfov
                step = self.d.frame(1 / 60, half_vfov_tan=new_vfov)
                self.assertFalse(step['snapped'])
                self.assertLessEqual(step['pos_lag'], 0.10 * target + 1e-9)
                if jumps:
                    self.assertAlmostEqual(step['pos_lag'], 0.10 * target, places=9)       # clamped: the jump
                    if new_vfov == 0.75:   # a large step: most of the change lands in the first frame
                        self.assertLess(step['distance'], first['distance'] - 0.5 * (first['distance'] - target))
                else:
                    self.assertLess(step['pos_lag'], 0.10 * target)                       # inside the clamp
                    self.assertGreater(step['distance'], first['distance'] - 0.1 * (first['distance'] - target))
                rest = [self.d.frame(1 / 60, half_vfov_tan=new_vfov) for _ in range(240)]
                self.assertFalse(any(r['snapped'] for r in rest))
                self.assertAlmostEqual(rest[-1]['distance'], target, delta=0.1)

    def test_combat_tightness_scales_the_time_constants_while_locked(self):
        # Review 31 A9: with a lock the springs run at tau * (1 - tightness);
        # without a lock (or with tightness 0) nothing changes.
        tau, dt, step_deg = 0.2, 1 / 60, 20.0
        for tightness, locked, effective in ((0.5, True, 0.1), (0.5, False, 0.2), (0.0, True, 0.2), (0.75, True, 0.05)):
            self.d.reset()
            self.d.tunables(rot_tau=tau, pos_tau=tau, combat_tightness=tightness)
            self.d.frame(dt, locked=locked)
            target = yaw(math.radians(step_deg))
            for n in range(1, 31):
                r = self.d.frame(dt, ship=target, locked=locked)
                self.assertEqual(r['locked'], locked)
                t = n * dt
                self.assertAlmostEqual(r['lag_deg'], step_deg * (1 + t / effective) * math.exp(-t / effective), places=6, msg=(tightness, locked, n))
        self.d.reset()
        self.d.tunables(rot_tau=tau, combat_tightness=1.0)  # rigid follow at the limit
        self.d.frame(dt, locked=True)
        r = self.d.frame(dt, ship=yaw(math.radians(30)), locked=True)
        self.assertEqual((r['verdict'], r['lag_deg'], r['pos_lag']), (0, 0.0, 0.0))
        self.assertFalse(self.d.tunables(combat_tightness=1.5))

    # --- snaps --------------------------------------------------------------
    def test_snap_reasons(self):
        self.d.tunables(snap_coalesce_frames=0)  # every snap is a cut here; coalescing has its own test
        self.d.frame(1 / 60)
        self.assertEqual(self.d.frame(1 / 60, ref=2)['snap_reason'], 2)
        self.assertEqual(self.d.frame(1 / 60, ref=2, sector=9)['snap_reason'], 4)
        self.assertEqual(self.d.frame(1 / 60, ref=2, sector=9, mode=3)['snap_reason'], 8)
        self.assertEqual(self.d.frame(1 / 60, ref=2, sector=9, mode=5)['snap_reason'], 8)
        r = self.d.frame(1 / 60, ref=2, sector=9, mode=5, ship_pos=(0, 0, 10 ** 6))
        self.assertEqual(r['snap_reason'], 16)
        self.assertTrue(r['snapped'])
        r = self.d.frame(1 / 60, ref=2, sector=9, mode=5, ship_pos=(0, 0, 10 ** 6 + 100))
        self.assertFalse(r['snapped'])

    def test_gate_jump_coalesces_the_sector_snap_into_the_teleport_snap(self):
        # Review 31 A4: the ship node moves (16), +0x1fc follows a frame later
        # (4). The second re-seats the springs but is not a second TAA cut.
        self.d.frame(1 / 60)
        self.d.frame(1 / 60, ship=yaw(0.2))
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 10 ** 6))
        self.assertEqual((r['snapped'], r['coalesced'], r['snap_reason']), (True, False, 16))
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 10 ** 6 + 100), sector=2)
        self.assertEqual((r['snapped'], r['coalesced'], r['snap_reason'], r['lag_deg']), (False, True, 4, 0.0))
        for n in range(3):
            r = self.d.frame(1 / 60, ship=yaw(0.2 + 0.01 * n), ship_pos=(0, 0, 10 ** 6 + 200 + 100 * n), sector=2)
            self.assertFalse(r['snapped'] or r['coalesced'])
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 10 ** 6 + 600), sector=3)  # 3 applied frames later: a real snap again
        self.assertEqual((r['snapped'], r['coalesced'], r['snap_reason']), (True, False, 4))
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 10 ** 6 + 700), sector=3, mode=4)  # a mode change right after: the view moved, always a cut
        self.assertEqual((r['snapped'], r['coalesced'], r['snap_reason']), (True, False, 8))
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 3 * 10 ** 6), sector=5, mode=4)  # sector + teleport together: a cut
        self.assertEqual((r['snapped'], r['coalesced'], r['snap_reason']), (True, False, 20))
        self.d.tunables(snap_coalesce_frames=0)
        r = self.d.frame(1 / 60, ship=yaw(0.2), ship_pos=(0, 0, 3 * 10 ** 6 + 100), sector=6, mode=4)
        self.assertEqual((r['snapped'], r['coalesced']), (True, False))  # window 0: never coalesced

    def test_snap_after_a_refused_frame(self):
        self.d.frame(1 / 60)
        self.d.frame(1 / 60, ship=yaw(0.3))
        self.assertEqual(self.d.frame(1 / 60, mode=1)['verdict'], 1)
        r = self.d.frame(1 / 60, ship=yaw(0.3))
        self.assertTrue(r['snapped'])
        self.assertEqual(r['lag_deg'], 0)

    # --- guards -------------------------------------------------------------
    def test_nan_and_non_orthonormal_input_is_refused(self):
        self.d.frame(1 / 60)
        self.assertEqual(self.d.frame(1 / 60, ship_pos=(float('nan'), 0, 0))['verdict'], 4)
        self.assertEqual(self.d.frame(float('inf'))['verdict'], 4)
        self.assertEqual(self.d.frame(1 / 60, ship=[1.1 * v for v in IDENTITY])['verdict'], 4)
        self.assertEqual(self.d.frame(1 / 60, ship=[-1, 0, 0, 0, 1, 0, 0, 0, 1])['verdict'], 4)  # mirror (det -1)
        self.assertEqual(self.d.frame(1 / 60, half_vfov_tan=0)['verdict'], 4)
        self.assertEqual(self.d.frame(1 / 60, boom=(0, 0, 0))['verdict'], 5)
        r = self.d.frame(1 / 60)
        self.assertEqual((r['verdict'], r['snapped']), (0, True))

    def test_rotation_exp_log_round_trip_and_fixed_point(self):
        random.seed(3)
        for _ in range(200):
            v = [random.uniform(-1, 1) for _ in range(3)]
            n = math.sqrt(sum(x * x for x in v))
            scale = random.uniform(0, 3.0) / n
            v = [x * scale for x in v]
            out = self.d.send('X ' + ' '.join(map(repr, v)))
            back = [float(x) for x in out[1:4]]
            self.assertTrue(all(abs(a - b) < 1e-7 for a, b in zip(back, v)), (v, back))
            self.assertLess(float(out[4]), 1e-12)
            self.assertEqual(out[5], '1')
            self.assertLessEqual(float(out[6]), 0.5 / 65536 + 1e-12)

    def test_rotation_vector_at_pi_takes_the_diagonal_axis_branch(self):
        # |r| = pi makes the matrix symmetric, so log_rotation's antisymmetric
        # part vanishes and it must recover the axis from the diagonal. The sign
        # is genuinely ambiguous there: r and -r are the same rotation.
        for axis in ([1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0], [1, 2, -3]):
            norm = math.sqrt(sum(x * x for x in axis))
            for angle in (math.pi, math.pi - 1e-9, math.pi - 1e-4):
                v = [x * angle / norm for x in axis]
                back, ortho = self.d.exp_log(v)
                self.assertLess(ortho, 1e-12, (axis, angle))
                self.assertAlmostEqual(math.sqrt(sum(x * x for x in back)), angle, places=6, msg=(axis, angle))
                parallel = sum(a * b for a, b in zip(back, v)) / (angle * angle)
                self.assertAlmostEqual(abs(parallel), 1.0, places=6, msg=(axis, angle, back))

    def test_rotation_vector_beyond_pi_comes_back_the_short_way(self):
        # exp of a 200 deg rotation is the same matrix as -160 deg about -k;
        # log must return the shorter one, so nothing can accumulate past pi.
        v = [0.0, math.radians(200), 0.0]
        back, ortho = self.d.exp_log(v)
        self.assertLess(ortho, 1e-12)
        self.assertAlmostEqual(math.degrees(math.sqrt(sum(x * x for x in back))), 160.0, places=6)
        self.assertLess(sum(a * b for a, b in zip(back, v)), 0)

    def test_half_turn_of_the_ship_stays_within_the_clamp_and_converges(self):
        self.d.tunables(lag_clamp_deg=45)
        self.d.frame(1 / 60)
        r = self.d.frame(1 / 60, ship=yaw(math.radians(179)))
        self.assertEqual(r['verdict'], 0)
        self.assertLessEqual(r['lag_deg'], 45 + 1e-9)
        self.assertLess(r['ortho_error'], 1e-9)
        for _ in range(600):
            r = self.d.frame(1 / 60, ship=yaw(math.radians(179)))
        self.assertLess(r['lag_deg'], 1e-6)
        self.assertLess(rotation_angle_deg(r['basis'], yaw(math.radians(179))), 1e-6)

    # --- dt edges -----------------------------------------------------------
    def test_negative_dt_is_treated_as_no_time(self):
        self.d.frame(1 / 60)
        target = yaw(math.radians(10))
        a = self.d.frame(1 / 60, ship=target)['lag_deg']
        b = self.d.frame(-0.5, ship=target)['lag_deg']
        self.assertAlmostEqual(a, b, places=12)

    def test_pause_then_resume_clamps_the_first_step_and_keeps_tracking(self):
        # A menu/alt-tab gap: the handler keeps running, dt is huge. The step is
        # clamped to max_dt (no snap, no NaN) and the spring keeps converging.
        self.d.tunables(max_dt=0.1, rot_tau=0.2)
        self.d.frame(1 / 60)
        self.d.frame(1 / 60, ship=yaw(math.radians(40)))
        r = self.d.frame(30.0, ship=yaw(math.radians(40)))
        self.assertEqual(r['verdict'], 0)
        self.assertFalse(r['snapped'])
        self.assertGreater(r['lag_deg'], 0)
        self.assertLess(r['ortho_error'], 1e-9)
        for _ in range(600):
            r = self.d.frame(1 / 60, ship=yaw(math.radians(40)))
        self.assertLess(r['lag_deg'], 1e-6)

    # --- long run -----------------------------------------------------------
    def test_orthonormality_and_the_identity_hold_over_100k_steps(self):
        # The basis is rebuilt each frame as target * exp(x) from freshly read
        # engine state, so nothing can drift; 10^5 frames (~28 min at 60 Hz) of
        # continuous yaw and roll must leave it a proper rotation and keep
        # camera = view_rel * ship exact for the mouse-aim ray.
        self.d.tunables(lag_clamp_deg=10, rot_tau=0.2, pos_tau=0.3, pos_lag_clamp=0.2)
        run = self.d.long_run(100000, 1 / 60, 0.7, 0.3)
        self.assertEqual(run['refused'], 0)
        self.assertEqual(run['snaps'], 1)  # only the first frame
        self.assertEqual(run['applied'], 100000)
        self.assertLess(run['max_ortho'], 1e-9)
        self.assertLessEqual(run['max_lag_deg'], 10 + 1e-9)
        self.assertGreater(run['max_lag_deg'], 1.0)  # the ship really is turning
        self.assertLess(run['max_identity'], 1e-9)




class ChaseCameraLaunchOptions(unittest.TestCase):
    def invoke(self, *args):
        from tools import manage
        output = io.StringIO()
        with tempfile.TemporaryDirectory(prefix='x3-chase-launch-') as directory:
            wine = Path(directory) / 'wine'
            wine.touch()
            (Path(directory) / 'X3AP.exe').touch()
            argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', directory, *args]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(manage, 'WINE', wine), mock.patch.object(manage, 'VOICE_DECODER_REPO', None), \
                    mock.patch.object(manage.subprocess, 'call', side_effect=AssertionError('must never launch')) as call, \
                    contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                try:
                    manage.main()
                except SystemExit as error:
                    return error.code, None
                call.assert_not_called()
        return 0, json.loads(output.getvalue())

    def test_pitch_down_is_forwarded_by_dry_run(self):
        for value in ('0', '10', '13', '20', '25', '30'):
            code, output = self.invoke('--camera', 'chase', '--chase-pitch-down-deg', value)
            self.assertEqual(code, 0)
            self.assertEqual(float(output['env']['X3M_CHASE_PITCH_DOWN_DEG']), float(value))

    def test_pitch_down_requires_chase_mode(self):
        self.assertEqual(self.invoke('--chase-pitch-down-deg', '10')[0], 2)

    def test_invalid_pitch_down_is_rejected_before_launch(self):
        for value in ('-1', '30.1', '90', 'nan', 'inf'):
            self.assertEqual(self.invoke('--camera', 'chase', '--chase-pitch-down-deg', value)[0], 2)

    def test_hud_anchor_defaults_to_forward_in_chase_mode_and_clears_stale_environment(self):
        # Default forward on a chase launch since 2026-09-23; centre elsewhere and when asked.
        for camera, stale, expected in (('vanilla', 'forward', 'centre'), ('chase', 'centre', 'forward')):
            with mock.patch.dict(os.environ, {'X3M_CHASE_HUD_ANCHOR': stale}):
                code, output = self.invoke('--camera', camera)
            self.assertEqual(code, 0)
            self.assertEqual(output['env']['X3M_CHASE_HUD_ANCHOR'], expected)
        with mock.patch.dict(os.environ, {'X3M_CHASE_HUD_ANCHOR': 'forward'}):
            code, output = self.invoke('--camera', 'chase', '--chase-hud-anchor', 'centre')
        self.assertEqual(code, 0)
        self.assertEqual(output['env']['X3M_CHASE_HUD_ANCHOR'], 'centre')
        self.assertEqual(self.invoke('--chase-hud-anchor', 'centre')[0], 0)

    def test_distance_scale_default_is_forwarded_and_overridable(self):
        with mock.patch.dict(os.environ, {'X3M_CHASE_DISTANCE_SCALE': '0.9'}):
            code, output = self.invoke('--camera', 'chase')
        self.assertEqual(code, 0)
        self.assertEqual(output['env']['X3M_CHASE_DISTANCE_SCALE'], '1.05')
        code, output = self.invoke('--camera', 'chase', '--chase-distance-scale', '0.9')
        self.assertEqual(code, 0)
        self.assertEqual(float(output['env']['X3M_CHASE_DISTANCE_SCALE']), 0.9)
        for value in ('0', '10.5', 'nan'):
            self.assertEqual(self.invoke('--camera', 'chase', '--chase-distance-scale', value)[0], 2)

    def modded(self, *args, inherited=None):
        # A modded (non --vanilla) dry run: the launcher wants an installed proxy that matches its manifest.
        from tools import manage
        output, error = io.StringIO(), io.StringIO()
        with tempfile.TemporaryDirectory(prefix='x3-chase-launch-') as directory:
            wine = Path(directory) / 'wine'
            wine.touch()
            (Path(directory) / 'X3AP.exe').touch()
            (Path(directory) / 'd3d9.dll').write_bytes(b'proxy')
            (Path(directory) / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
            argv = ['manage.py', 'launch', '--dry-run', '--game-dir', directory, *args]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(manage, 'WINE', wine), mock.patch.object(manage, 'VOICE_DECODER_REPO', None), \
                    mock.patch.dict(os.environ, inherited or {}), \
                    mock.patch.object(manage.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                    contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
                try:
                    manage.main()
                except SystemExit as exit_error:
                    return exit_error.code, error.getvalue()
        return 0, json.loads(output.getvalue())

    def test_fov_compensate_default_on_off_and_vanilla(self):
        name = 'X3M_CHASE_FOV_COMPENSATE'
        for args, stale, expected in ((('--camera', 'chase'), '0', '1'),
                                      (('--camera', 'chase', '--chase-fov-compensate', 'on'), '0', '1'),
                                      (('--camera', 'chase', '--chase-fov-compensate', 'off'), '1', '0'),
                                      ((), '0', '1')):                                     # inert without chase, still explicit
            with self.subTest(args=args):
                code, output = self.modded(*args, inherited={name: stale})
                self.assertEqual(code, 0, output)
                self.assertEqual(output['env'][name], expected)
        with mock.patch.dict(os.environ, {name: '1'}):
            code, output = self.invoke('--camera', 'chase')   # --vanilla: nothing is sent, a stale value is dropped
        self.assertEqual(code, 0)
        self.assertNotIn(name, output['env'])
        for value in ('on', 'off'):
            self.assertEqual(self.invoke('--camera', 'chase', '--chase-fov-compensate', value)[0], 2)
        self.assertEqual(self.modded('--camera', 'chase', '--chase-fov-compensate', 'yes')[0], 2)
        for value in ('on', 'off'):   # an explicit value requires --camera chase, like the other --chase-* tunables
            code, error = self.modded('--chase-fov-compensate', value)
            self.assertEqual(code, 2)
            self.assertIn('requires --camera chase', error)

    def test_forward_hud_anchor_is_forwarded_in_chase_mode(self):
        code, output = self.invoke('--camera', 'chase', '--chase-hud-anchor', 'forward')
        self.assertEqual(code, 0)
        self.assertEqual(output['env']['X3M_CHASE_HUD_ANCHOR'], 'forward')

    def test_view_restore_defaults_off_and_is_forwarded_in_chase_mode(self):
        with mock.patch.dict(os.environ, {'X3M_CHASE_VIEW_RESTORE': '1'}):
            code, output = self.invoke('--camera', 'chase')
        self.assertEqual(code, 0)
        self.assertEqual(output['env']['X3M_CHASE_VIEW_RESTORE'], '0')
        code, output = self.invoke('--camera', 'chase', '--chase-view-restore')
        self.assertEqual(code, 0)
        self.assertEqual(output['env']['X3M_CHASE_VIEW_RESTORE'], '1')
        self.assertEqual(self.invoke('--chase-view-restore')[0], 2)

    def test_forward_hud_anchor_requires_chase_mode(self):
        self.assertEqual(self.invoke('--chase-hud-anchor', 'forward')[0], 2)
        self.assertEqual(self.invoke('--camera', 'chase', '--chase-hud-anchor', 'invalid')[0], 2)


if __name__ == '__main__':
    unittest.main()
