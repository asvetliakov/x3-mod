"""Host unit test of tools/analysis/draw_accounting.py on a synthetic run directory.

A hand-written session log (object_bounds, draw, object_context, the depth
readback record and a frame_timing window) plus a 16x16 R32F depth image whose
written regions are placed so that every bucket is reached exactly once:
visible, occluded, partial (half the box covered), tiny, offscreen and the
no_box remainder. Also checks the priorities (an occluded box below the tiny
area stays occluded), the margin (a pixel at exactly zmin does not cover), the
per-node table and the milliseconds taken from frame_timing. No Wine, no D3D.
"""
import array
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WIDTH = HEIGHT = 16
FRAME = 4242
SENTINEL = -1.0


def load_script():
    spec = importlib.util.spec_from_file_location('draw_accounting', ROOT / 'tools/analysis/draw_accounting.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bounds_line(index, node, model, box, zmin, zmax, inside, offscreen=False, near=False):
    return (f'object_bounds device=1 frame={FRAME} index={index} node={node} model={model} '
            f'sx0={box[0]:.1f} sy0={box[1]:.1f} sx1={box[2]:.1f} sy1={box[3]:.1f} '
            f'zmin={zmin:.6f} zmax={zmax:.6f} inside={inside}'
            + (' offscreen=1' if offscreen else '') + (' near=1' if near else ''))


class SyntheticRun:
    """A run directory with one capture frame: six draws, one per bucket."""

    def __init__(self, directory):
        self.directory = Path(directory)
        depth = array.array('f', [SENTINEL] * (WIDTH * HEIGHT))

        def fill(x0, y0, x1, y1, value):
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    depth[y * WIDTH + x] = value

        # index 1 visible: nothing written over its box.
        # index 2 offscreen (flagged, no box).
        # index 3 occluded: every pixel of (8,0)-(13,5) is nearer than zmin 0.5;
        #   its area (25 px) is above the tiny threshold.
        fill(8, 0, 13, 5, 0.1)
        # index 4 partial: the left three columns of (0,8)-(5,13) are nearer,
        #   one pixel sits at exactly zmin (0.5) and must not count as covering.
        fill(0, 8, 2, 13, 0.1)
        depth[9 * WIDTH + 4] = 0.5
        # index 5 tiny: (8,8)-(11,11), 9 px, nothing written.
        # index 6 occluded below the tiny area: (14,14)-(15,15), 1 px box, nearer depth.
        fill(14, 14, 15, 15, 0.2)
        (self.directory / f'depth_1_{FRAME}.r32f').write_bytes(depth.tobytes())
        rows = [
            'frame_begin device=1 frame=%d' % FRAME,
            bounds_line(1, '31000001', '00005001', (0.0, 0.0, 5.0, 5.0), 0.5, 0.6, 8),
            f'draw device=1 frame={FRAME} index=1 kind=indexed topology=4 primitives=100 vs=0 ps=0',
            f'object_context device=1 frame={FRAME} index=1 scoped=1 valid=127 node=31000001 model=00005001 lod=00000000',
            bounds_line(2, '31000002', '00005002', (0.0, 0.0, 0.0, 0.0), 0.0, 0.0, 0, offscreen=True),
            f'draw device=1 frame={FRAME} index=2 kind=indexed topology=4 primitives=200 vs=0 ps=0',
            bounds_line(3, '31000003', '00005003', (8.0, 0.0, 13.0, 5.0), 0.5, 0.6, 8),
            f'draw device=1 frame={FRAME} index=3 kind=indexed topology=4 primitives=300 vs=0 ps=0',
            bounds_line(4, '31000004', '00005004', (0.0, 8.0, 5.0, 13.0), 0.5, 0.6, 8),
            f'draw device=1 frame={FRAME} index=4 kind=indexed topology=4 primitives=400 vs=0 ps=0',
            bounds_line(5, '31000005', '00005005', (8.0, 8.0, 11.0, 11.0), 0.5, 0.6, 8),
            f'draw device=1 frame={FRAME} index=5 kind=indexed topology=4 primitives=500 vs=0 ps=0',
            bounds_line(6, '31000005', '00005005', (14.0, 14.0, 15.0, 15.0), 0.5, 0.6, 8),
            f'draw device=1 frame={FRAME} index=6 kind=indexed topology=4 primitives=600 vs=0 ps=0',
            # A draw of the frame with no object_bounds row: the no_box remainder.
            f'draw device=1 frame={FRAME} index=7 kind=indexed topology=4 primitives=700 vs=0 ps=0',
            # Another frame's rows must never be mixed in.
            bounds_line(1, '31000009', '00005009', (0.0, 0.0, 16.0, 16.0), 0.1, 0.2, 8).replace(f'frame={FRAME}', 'frame=1'),
            f'draw device=1 frame=1 index=1 kind=indexed topology=4 primitives=900 vs=0 ps=0',
            f'motion_output_depth_readback device=1 frame={FRAME} file=depth_1_{FRAME}.r32f width={WIDTH} '
            f'height={HEIGHT} format=r32f_row_major result=00000000 bytes={WIDTH * HEIGHT * 4}',
            # 20000 us over 400 draws = 50 us per draw.
            f'frame_timing qpc=1 frame={FRAME} frames=300 dt_p50_us=20000 dt_p95_us=1 dt_max_us=1 draws_p50=400 draws_max=1 '
            'present_p50_us=1 present_p95_us=1 present_max_us=1',
        ]
        self.log = self.directory / 'session-20260922-120000-100.log'
        self.log.write_text('\n'.join(rows) + '\n')


class DrawAccounting(unittest.TestCase):
    def setUp(self):
        self.module = load_script()
        self.temporary = tempfile.TemporaryDirectory(prefix='x3-draw-accounting-')
        self.run = SyntheticRun(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def result(self, **kwargs):
        with self.run.log.open() as stream:
            parsed = self.module.parse(stream, 1)
        frame = kwargs.pop('frame', self.module.default_frame(parsed))
        return self.module.account(parsed, frame, self.temporary.name, **kwargs)

    def test_every_bucket_is_reached_with_its_draws_primitives_and_ms(self):
        result = self.result()
        self.assertEqual(result['frame'], FRAME)
        self.assertEqual((result['draws_in_frame'], result['draws_with_box']), (7, 6))
        self.assertAlmostEqual(result['us_per_draw'], 50.0)
        counts = {name: result['buckets'][name]['draws'] for name in self.module.BUCKETS}
        self.assertEqual(counts, {'offscreen': 1, 'occluded': 2, 'tiny': 1, 'partial': 1, 'visible': 1, 'no_box': 1})
        primitives = {name: result['buckets'][name]['primitives'] for name in self.module.BUCKETS}
        self.assertEqual(primitives, {'offscreen': 200, 'occluded': 900, 'tiny': 500, 'partial': 400,
                                      'visible': 100, 'no_box': 700})
        # 50 us per draw: one draw is 0.05 ms, the two occluded draws 0.10 ms.
        self.assertAlmostEqual(result['buckets']['visible']['ms'], 0.05)
        self.assertAlmostEqual(result['buckets']['occluded']['ms'], 0.10)
        self.assertEqual(sum(counts.values()), result['draws_in_frame'])
        # Every on-screen box below the area threshold, whatever bucket it landed in.
        self.assertEqual(result['tiny_total'], 2)

    def test_partial_coverage_fraction_and_the_margin(self):
        result = self.result()
        partial = next(row for row in result['rows'] if row['bucket'] == 'partial')
        # 6x6 sampled pixels, the left three columns covered; the pixel at exactly
        # zmin is not closer than zmin - margin, so it does not count.
        self.assertEqual((partial['sampled'], partial['covered']), (36, 18))
        self.assertAlmostEqual(partial['covered_fraction'], 0.5)
        self.assertAlmostEqual(result['buckets']['partial']['covered_fraction'], 0.5)
        self.assertEqual(result['buckets']['occluded']['covered_fraction'], 1.0)
        self.assertEqual(result['buckets']['visible']['covered_fraction'], 0.0)
        # A margin wider than the depth gap leaves nothing covered.
        wide = self.result(margin=0.5)
        self.assertEqual({name: wide['buckets'][name]['draws'] for name in ('occluded', 'partial', 'visible', 'tiny')},
                         {'occluded': 0, 'partial': 0, 'visible': 3, 'tiny': 2})

    def test_priority_and_the_tiny_threshold(self):
        result = self.result()
        by_index = {row['index']: row['bucket'] for row in result['rows']}
        self.assertEqual(by_index, {1: 'visible', 2: 'offscreen', 3: 'occluded', 4: 'partial', 5: 'tiny', 6: 'occluded'})
        # Raising the threshold above the visible box's area moves it to tiny;
        # the occluded ones stay occluded (occlusion is decided first).
        raised = self.result(tiny_px=1000.0)
        self.assertEqual({row['index']: row['bucket'] for row in raised['rows']},
                         {1: 'tiny', 2: 'offscreen', 3: 'occluded', 4: 'tiny', 5: 'tiny', 6: 'occluded'})

    def test_per_node_table_joins_the_draws(self):
        result = self.result()
        nodes = {entry['node']: entry for entry in result['nodes']}
        self.assertEqual(len(nodes), 5)
        shared = nodes['31000005']  # the tiny box and the tiny occluded one
        self.assertEqual((shared['draws'], shared['primitives'], shared['tiny'], shared['occluded']), (2, 1100, 1, 1))
        self.assertAlmostEqual(shared['ms'], 0.10)
        self.assertEqual(result['nodes'][0]['node'], '31000005')  # sorted by draws

    def test_sampling_stride_is_bounded(self):
        # Four samples at most over a 36-pixel box: the stride still finds the
        # covered half.
        result = self.result(max_samples=4)
        partial = next(row for row in result['rows'] if row['bucket'] == 'partial')
        self.assertLessEqual(partial['sampled'], 9)
        self.assertGreater(partial['covered'], 0)
        self.assertLess(partial['covered'], partial['sampled'])

    def test_cli_text_and_json(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = self.module.main([self.temporary.name])
        self.assertEqual(code, 0)
        text = output.getvalue()
        self.assertIn(f'frame {FRAME} {WIDTH}x{HEIGHT}', text)
        self.assertIn('occluded', text)
        self.assertIn('31000005', text)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(self.module.main([self.temporary.name, '--json', '--frame', str(FRAME)]), 0)
        report = json.loads(output.getvalue())
        self.assertEqual(report['buckets']['occluded']['draws'], 2)
        self.assertNotIn('rows', report)

    def test_refusals(self):
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(self.module.main([self.temporary.name, '--frame', '1']), 2)
        self.assertIn('no motion_output_depth_readback', error.getvalue())
        with tempfile.TemporaryDirectory() as empty:
            error = io.StringIO()
            with contextlib.redirect_stderr(error):
                self.assertEqual(self.module.main([empty]), 2)
            self.assertIn('no session log', error.getvalue())
        # The depth file must be next to the log and the right size.
        (Path(self.temporary.name) / f'depth_1_{FRAME}.r32f').write_bytes(b'\x00' * 8)
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(self.module.main([self.temporary.name]), 2)
        self.assertIn('size 8', error.getvalue())


if __name__ == '__main__':
    unittest.main()
