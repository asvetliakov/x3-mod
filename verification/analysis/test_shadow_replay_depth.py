"""Host contracts of the cascade-0 depth replay (docs/architecture/
shadow-replay-gates.md, "Implemented: cascade-0 depth replay fixture"): the
parser of the per-frame line and its identities, the CPU projection chain the
fixture oracle applies, the rasterizer on a small map, and the launcher gate.
No Wine, no game."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import shadow_replay_depth as depth  # noqa: E402

LINE = 'shadow_replay_depth device=1 frame={frame} replayed={replayed} skipped_lease={lease} skipped_state={state} skipped_caps={caps} draws={draws} us={us}'


def line(frame, replayed=2, lease=0, state=0, caps=0, draws=2, us=12.5):
    return LINE.format(frame=frame, replayed=replayed, lease=lease, state=state, caps=caps, draws=draws, us=us)


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('depth_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class Parser(unittest.TestCase):
    def test_lines_and_identities(self):
        text = '\n'.join(['motion_output_frame device=1 frame=0', line(0), line(1, replayed=0, lease=1, us=0),
                          'shadow_replay_depth_refused device=1 frame=1 reason=lease detail=bookends result=00000000 stage=0',
                          'shadow_replay_depth_target device=1 frame=0 size=256 map_format=114 depth_format=77 allocations=1',
                          line(2, replayed=0, draws=0, us=0), ''])
        rows, refused, targets = depth.parse_text(text)
        self.assertEqual([r['frame'] for r in rows], [0, 1, 2])
        self.assertEqual((rows[0]['replayed'], rows[0]['us']), (2, 12.5))
        self.assertEqual(refused, [dict(device='1', frame='1', reason='lease', detail='bookends', result='00000000', stage='0')])
        self.assertEqual(targets, [dict(device=1, frame=0, size=256, map_format=114, depth_format=77, allocations=1)])
        self.assertEqual(depth.us_summary(rows), {'frames': 1, 'min': 12.5, 'median': 12.5, 'max': 12.5})
        self.assertEqual(depth.us_summary([]), {'frames': 0})

    def test_malformed(self):
        for bad in (line(0, replayed=1),                      # replayed neither 0 nor draws
                    line(0, replayed=0, lease=3),             # a bucket beyond draws
                    line(0, replayed=2, state=1),             # replayed and skipped
                    line(0, replayed=0),                      # nothing replayed, nothing skipped
                    line(0).replace('draws=2', 'draws=x'),    # non-numeric
                    line(0) + ' extra=1',                     # unknown field
                    line(0).replace(' us=12.5', '')):         # missing field
            with self.assertRaises(depth.MalformedLine, msg=bad):
                depth.parse_depth_line(bad)


class Projection(unittest.TestCase):
    """The identity camera at the origin and a sun-space basis aligned with
    the world axes (light travelling +z): the chain reduces to view = world."""
    camera = {'m00': 1.0, 'm11': 1.0, 'r': [1, 0, 0, 0, 1, 0, 0, 0, 1], 't': [0, 0, 0]}
    basis = {'right': (1, 0, 0), 'up': (0, 1, 0), 'forward': (0, 0, 1), 'center': (0, 0, 0), 'extent': 4.0, 'depth_half': 8.0}

    def test_vertex_chain(self):
        # rows(t, p, zo): clip = (x + t, y, z + zo, p x + 1); view = (clip.x, clip.y, clip.w) at m00 = m11 = 1.
        rows = depth.rows_matrix(0.5, 0.25, 0.0)
        nx, ny, d = depth.project_vertex((1.0, -2.0, 0.5), rows, self.camera, self.basis)
        self.assertAlmostEqual(nx, 1.5 / 4.0); self.assertAlmostEqual(ny, -2.0 / 4.0)
        self.assertAlmostEqual(d, (1.25 + 8.0) / 16.0)
        self.assertEqual(depth.to_texels(-1.0, 1.0, 256), (0.0, 0.0)); self.assertEqual(depth.to_texels(1.0, -1.0, 256), (256.0, 256.0))
        # A translated, yawed camera: world = (view - t) R^T.
        camera = {'m00': 0.8, 'm11': 4 / 3, 'r': [0, 0, -1, 0, 1, 0, 1, 0, 0], 't': [12.5, -3.0, 1000.0]}
        rows = depth.rows_matrix(0.0, 0.0, 0.0)
        basis = dict(self.basis, center=(0, 0, 0))
        nx, ny, d = depth.project_vertex((0.0, 0.0, 0.5), rows, camera, basis)
        view = (0.0, 0.0, 1.0)
        world = [sum((view[j] - camera['t'][j]) * camera['r'][i * 3 + j] for j in range(3)) for i in range(3)]
        self.assertAlmostEqual(nx, world[0] / 4.0); self.assertAlmostEqual(ny, world[1] / 4.0); self.assertAlmostEqual(d, (world[2] + 8.0) / 16.0)

    def test_rasterizer_and_compare(self):
        try:
            import numpy  # noqa: F401
        except ImportError:
            self.skipTest('numpy')
        # Shape A (x >= -1, y <= 1, x - y <= 2 in object space) under the 4-unit extent at 64 texels: a
        # 32-texel right triangle, about 512 covered texels; the camera translation (0.3, 0.2) keeps every
        # edge off the integer sample grid so no sample is ambiguous.
        camera = dict(self.camera, t=[0.3, 0.2, 0.0])
        draws = [{'caster': 0, 'shape': 'A', 't': 0.0, 'p': 0.0, 'zo': 0.1}]
        cpu, ambiguous = depth.expected_map(draws, camera, self.basis, 64)
        covered = int((cpu < 1.0).sum())
        self.assertGreater(covered, 300); self.assertLess(covered, 700)
        self.assertEqual(int(ambiguous.sum()), 0)
        flat = cpu.reshape(-1).tolist()
        exact = depth.compare_map(flat, draws, camera, self.basis, 64)
        self.assertTrue(exact['ok']); self.assertEqual(exact['coverage_disagreements'], 0); self.assertEqual(exact['max_depth_error'], 0.0)
        shifted = [v + (2e-4 if v < 1.0 else 0.0) for v in flat]
        self.assertFalse(depth.compare_map(shifted, draws, camera, self.basis, 64)['ok'])
        missing = [1.0 if i < 64 * 40 else v for i, v in enumerate(flat)]
        self.assertFalse(depth.compare_map(missing, draws, camera, self.basis, 64)['ok'])


class LauncherGate(unittest.TestCase):
    def test_requires_motion_output_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (['--shadow-replay-depth'], ['--shadow-replay-depth', '--motion-output'], ['--shadow-replay-depth', '--ownership']):
                code, _, error = launch(directory, *args)
                self.assertNotEqual(code, 0, args); self.assertIn('--shadow-replay-depth requires --motion-output --ownership', error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-size', '512')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-replay-size requires --shadow-replay-depth', error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth', '--shadow-replay-size', '32')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-replay-size must be within [64, 4096]', error)
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth')
            self.assertEqual(code, 0)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_SHADOW_REPLAY_DEPTH'], '1'); self.assertEqual(env['X3M_SHADOW_REPLAY_SIZE'], '1024')
            self.assertEqual(env['X3M_SHADOW_REPLAY_CANDIDATES'], '1'); self.assertEqual(env['X3M_OWNERSHIP'], '1'); self.assertEqual(env['X3M_MOTION_OUTPUT'], '1')
            self.assertEqual(env.get('X3M_LINEAR_MATERIALS', '0'), '0'); self.assertEqual(env.get('X3M_TAA', '0'), '0')
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth', '--shadow-replay-size', '512')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_REPLAY_SIZE'], '512')
            code, output, _ = launch(directory, '--motion-output', '--ownership')
            self.assertEqual(code, 0); env = json.loads(output)['env']
            self.assertEqual(env['X3M_SHADOW_REPLAY_DEPTH'], '0'); self.assertEqual(env['X3M_SHADOW_REPLAY_CANDIDATES'], '0')


if __name__ == '__main__':
    unittest.main()
