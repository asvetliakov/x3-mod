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


def launch(directory, *args, inherited=None):
    spec = importlib.util.spec_from_file_location('depth_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.dict(module.os.environ, inherited or {}), \
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

    def test_bounds_objects(self):
        # The fixture's bounds objects under its own camera terms (m00 0.8, m11 4/3), the 8-unit cascade
        # centred on the camera and the fixture's sun: L's origin (rows translation 204.8 -> view x 256)
        # lies beyond the origin rule's 250 units (the run-36 station: origin far, deck under the ship),
        # yet L covers map texels; F's origin (300 units) and vertices (225 units) lie outside both rules
        # and it rasterizes nothing. The runner expects the cap = casters to drop the last caster on the
        # frames where L is admitted (draw order L, casters, F).
        try:
            import numpy  # noqa: F401
        except ImportError:
            self.skipTest('numpy')
        camera = {'m00': 0.8, 'm11': 4 / 3, 'r': [1, 0, 0, 0, 1, 0, 0, 0, 1], 't': [0.0, 0.0, 0.0]}
        sun = (1 / 11 ** .5, 3 / 11 ** .5, -1 / 11 ** .5)
        f = tuple(-s for s in sun)
        hint = (1, 0, 0) if abs(f[1]) > .99 else (0, 1, 0)
        right = (hint[1] * f[2] - hint[2] * f[1], hint[2] * f[0] - hint[0] * f[2], hint[0] * f[1] - hint[1] * f[0])
        norm = sum(v * v for v in right) ** .5; right = tuple(v / norm for v in right)
        up = (f[1] * right[2] - f[2] * right[1], f[2] * right[0] - f[0] * right[2], f[0] * right[1] - f[1] * right[0])
        basis = {'right': right, 'up': up, 'forward': f, 'center': (0, 0, 0), 'extent': 8.0, 'depth_half': 16.0}
        large = {'caster': 2, 'shape': 'L', 't': 204.8, 'p': 0.004, 'zo': 0.1}
        far = {'caster': 3, 'shape': 'F', 't': 240.0, 'p': 0.125, 'zo': 0.15}
        origin = lambda d: (sum(c * c for c in ((d['t'] / camera['m00']), 0.0, 1.0))) ** .5  # noqa: E731  origin_distance of rows(t, p, zo)
        self.assertGreater(origin(large), 250.0)                                # beyond the origin rule (run 36)
        self.assertGreater(origin(far), 250.0)                                  # F: outside both rules
        for vertex in depth.shape_vertices('L'):                                 # L's vertices within the box's depth range, w > 0
            w = large['p'] * vertex[0] + 1
            self.assertGreater(w, 0); self.assertGreater((0.5 + large['zo']) / w, 1.0)  # beyond the far plane on screen
        for vertex in depth.shape_vertices('F'):                                 # every F vertex far outside the box
            nx, ny, d = depth.project_vertex(vertex, depth.rows_matrix(far['t'], far['p'], far['zo']), camera, basis)
            self.assertTrue(abs(nx) > 1 or abs(ny) > 1 or d < 0 or d > 1)
        large_map, _ = depth.expected_map([large], camera, basis, 64)
        far_map, _ = depth.expected_map([far], camera, basis, 64)
        self.assertGreater(int((large_map < 1.0).sum()), 100)
        self.assertEqual(int((far_map < 1.0).sum()), 0)
        both = depth.expected_map([large, far], camera, basis, 64)[0]
        self.assertTrue(depth.compare_map(both.reshape(-1).tolist(), [large], camera, basis, 64)['ok'])


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

    def test_cascade_box_options(self):
        # --shadow-replay-extent / --shadow-replay-depth-half: the defaults are
        # explicit (an inherited value cannot leak), the values are carried,
        # the ranges and the --shadow-replay-depth prerequisite are enforced.
        with tempfile.TemporaryDirectory() as directory:
            base = ['--motion-output', '--ownership', '--shadow-replay-depth']
            code, output, _ = launch(directory, *base, inherited={'X3M_SHADOW_REPLAY_EXTENT': '3000', 'X3M_SHADOW_REPLAY_DEPTH_HALF': '9', 'X3M_SHADOW_REPLAY_CAP': '3'})
            self.assertEqual(code, 0); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_REPLAY_EXTENT'], env['X3M_SHADOW_REPLAY_DEPTH_HALF'], env['X3M_SHADOW_REPLAY_CAP'], env['X3M_SHADOW_REPLAY_SIZE']), ('250.0', '512.0', '512', '1024'))
            code, output, _ = launch(directory, *base, '--shadow-replay-extent', '1500', '--shadow-replay-depth-half', '3000', '--shadow-replay-size', '4096', '--shadow-replay-cap', '1024')
            self.assertEqual(code, 0); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SHADOW_REPLAY_EXTENT'], env['X3M_SHADOW_REPLAY_DEPTH_HALF'], env['X3M_SHADOW_REPLAY_SIZE'], env['X3M_SHADOW_REPLAY_CAP']), ('1500.0', '3000.0', '4096', '1024'))
            for option, value, message in (('--shadow-replay-extent', '49', '--shadow-replay-extent must be within [50, 4000]'),
                                           ('--shadow-replay-extent', '4001', '--shadow-replay-extent must be within [50, 4000]'),
                                           ('--shadow-replay-extent', 'nan', '--shadow-replay-extent must be within [50, 4000]'),
                                           ('--shadow-replay-depth-half', '127', '--shadow-replay-depth-half must be within [128, 8192]'),
                                           ('--shadow-replay-depth-half', '8193', '--shadow-replay-depth-half must be within [128, 8192]'),
                                           ('--shadow-replay-cap', '0', '--shadow-replay-cap must be within [1, 1024]'),
                                           ('--shadow-replay-cap', '1025', '--shadow-replay-cap must be within [1, 1024]')):
                code, _, error = launch(directory, *base, option, value)
                self.assertNotEqual(code, 0, (option, value)); self.assertIn(message, error)
            for option, value in (('--shadow-replay-extent', '1000'), ('--shadow-replay-depth-half', '2048')):
                code, _, error = launch(directory, '--motion-output', '--ownership', option, value)
                self.assertNotEqual(code, 0, option); self.assertIn('--shadow-replay-extent and --shadow-replay-depth-half require --shadow-replay-depth', error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-cap', '8')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-replay-cap requires --shadow-replay-candidates or --shadow-replay-depth', error)
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-candidates', '--shadow-replay-cap', '8')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_REPLAY_CAP'], '8')

    def test_bias_units_option(self):
        # --sun-shadow-bias-units rides --sun-shadow-apply (which needs the
        # lane and the replay); the default is the value that resolves to the
        # former 0.001 / 0.01 at the default cascade.
        with tempfile.TemporaryDirectory() as directory:
            apply = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa', '--hdr', '--sun-shadow-lane', '--shadow-replay-depth', '--sun-shadow-apply']
            code, output, error = launch(directory, *apply, inherited={'X3M_SUN_SHADOW_BIAS_UNITS': '7'})
            self.assertEqual(code, 0, error); env = json.loads(output)['env']
            self.assertEqual((env['X3M_SUN_SHADOW_APPLY'], env['X3M_SUN_SHADOW_BIAS_UNITS']), ('1', '0.53571875'))
            code, output, error = launch(directory, *apply, '--sun-shadow-bias-units', '2.5')
            self.assertEqual(code, 0, error); self.assertEqual(json.loads(output)['env']['X3M_SUN_SHADOW_BIAS_UNITS'], '2.5')
            for value in ('-1', '1001', 'inf'):
                code, _, error = launch(directory, *apply, '--sun-shadow-bias-units', value)
                self.assertNotEqual(code, 0, value); self.assertIn('--sun-shadow-bias-units must be within [0, 1000]', error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth', '--sun-shadow-bias-units', '1')
            self.assertNotEqual(code, 0); self.assertIn('--sun-shadow-bias-units requires --sun-shadow-apply', error)
            # The clamp in texels: default explicit (the former 0.01 at the default cascade), value carried, range and prerequisite.
            code, output, error = launch(directory, *apply, inherited={'X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS': '3'})
            self.assertEqual(code, 0, error); self.assertEqual(json.loads(output)['env']['X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS'], '20.97152')
            code, output, error = launch(directory, *apply, '--sun-shadow-bias-clamp-texels', '4')
            self.assertEqual(code, 0, error); self.assertEqual(json.loads(output)['env']['X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS'], '4.0')
            for value in ('0.5', '65', 'nan'):
                code, _, error = launch(directory, *apply, '--sun-shadow-bias-clamp-texels', value)
                self.assertNotEqual(code, 0, value); self.assertIn('--sun-shadow-bias-clamp-texels must be within [1, 64]', error)
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-depth', '--sun-shadow-bias-clamp-texels', '4')
            self.assertNotEqual(code, 0); self.assertIn('--sun-shadow-bias-clamp-texels requires --sun-shadow-apply', error)


if __name__ == '__main__':
    unittest.main()
