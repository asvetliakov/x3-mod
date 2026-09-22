"""Host unit test of tools/analysis/draw_accounting.py on a synthetic run directory.

A hand-written session log (object_bounds, draw, object_context, the depth
readback record and a frame_timing window) plus a 16x16 R32F depth image whose
written regions are placed so that every bucket is reached exactly once:
visible, occluded, partial (half the box covered), tiny, offscreen and the
no_box remainder; the tiny draw's row carries `alpha_tested=1` and is counted
in the header. Also checks the priorities (an occluded box below the tiny
area stays occluded), the margin (a pixel at exactly zmin does not cover), the
per-node table and the milliseconds taken from frame_timing. The `--ladder`
report on a second synthetic log (cull_census rows with and without the LOD
ladder fields, two views, a draw without a census row, another frame): per-model
count, thresholds, selected LODs, draws, `s` and the body name, sorted by
draws, with the `no_ladder`, `lod0_below_t1` and `unresolved` flags. No Wine, no D3D.
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


def bounds_line(index, node, model, box, zmin, zmax, inside, offscreen=False, near=False, alpha_tested=False, stale=False):
    return (f'object_bounds device=1 frame={FRAME} index={index} node={node} model={model} '
            f'sx0={box[0]:.1f} sy0={box[1]:.1f} sx1={box[2]:.1f} sy1={box[3]:.1f} '
            f'zmin={zmin:.6f} zmax={zmax:.6f} inside={inside}'
            + (' offscreen=1' if offscreen else '') + (' near=1' if near else '')
            + (' alpha_tested=1' if alpha_tested else '') + (' stale=1' if stale else ''))


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
            # The tiny draw is alpha-tested: bucketed like any other, counted in the header.
            bounds_line(5, '31000005', '00005005', (8.0, 8.0, 11.0, 11.0), 0.5, 0.6, 8, alpha_tested=True),
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
        self.assertEqual((result['alpha_tested_with_box'], result['stale_with_box']), (1, 0))
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
        self.assertEqual([row['index'] for row in result['rows'] if row['alpha_tested']], [5])
        # A row near=1 alpha_tested=1 keeps both flags (the field follows near=).
        row = self.module.BOUNDS_RE.search(bounds_line(9, '1', '2', (0.0, 0.0, 16.0, 16.0), 0.0, 0.1, 4, near=True, alpha_tested=True))
        self.assertEqual((row.group('near') is not None, row.group('alpha') is not None), (True, True))
        # Raising the threshold above the visible box's area moves it to tiny;
        # the occluded ones stay occluded (occlusion is decided first).
        raised = self.result(tiny_px=1000.0)
        self.assertEqual({row['index']: row['bucket'] for row in raised['rows']},
                         {1: 'tiny', 2: 'offscreen', 3: 'occluded', 4: 'tiny', 5: 'tiny', 6: 'occluded'})

    def test_stale_alpha_tested_row_is_counted(self):
        # An alpha-tested row whose box is an earlier revision's: bucketed as usual,
        # flagged on the row and counted in the header.
        with self.run.log.open('a') as stream:
            stream.write(bounds_line(8, '31000008', '00005008', (0.0, 0.0, 5.0, 5.0), 0.5, 0.6, 8,
                                     alpha_tested=True, stale=True) + '\n')
            stream.write(f'draw device=1 frame={FRAME} index=8 kind=indexed topology=4 primitives=800 vs=0 ps=0\n')
        result = self.result()
        self.assertEqual((result['alpha_tested_with_box'], result['stale_with_box']), (2, 1))
        row = next(row for row in result['rows'] if row['index'] == 8)
        self.assertEqual((row['alpha_tested'], row['stale'], row['bucket']), (True, True, 'visible'))
        self.assertEqual(result['buckets']['no_box']['draws'], 1)

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
        self.assertIn('with_box=6 alpha_tested=1 stale=0 ', text)
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


LADDER_FRAME = 5000


def census_line(node, model, s, lod, verdict, ladder=None, view='34766bf8', frame=LADDER_FRAME):
    return (f'cull_census device=1 frame={frame} view={view} node={node} model={model} s={s} measure={2 * s} d=100000 radius=500 '
            f'thr_1dc=0 thr_1d8=0 limit=0 flags_in=00001002 flags_out={"00001002" if verdict == "kept" else "00001000"} lod={lod} verdict={verdict}'
            + ('' if ladder is None else f' {ladder}'))


def context_draw(index, node, model, lod=0, frame=LADDER_FRAME):
    return [f'object_context device=1 frame={frame} index={index} scoped=1 valid=127 node={node} model={model} lod={lod:08x} flags12c=00001002',
            f'draw device=1 frame={frame} index={index} kind=indexed topology=4 primitives=10 vs=0 ps=0']


class LadderReport(unittest.TestCase):
    """`--ladder`: no depth readback, one census view joined with the frame's draws."""

    def setUp(self):
        self.module = load_script()
        self.temporary = tempfile.TemporaryDirectory(prefix='x3-draw-accounting-ladder-')
        rows = [f'frame_begin device=1 frame={LADDER_FRAME}']
        # A single-LOD body: three kept nodes at LOD 0, four draws.
        # The first row predates the body field; the model's name comes from the second.
        rows += [census_line('31000001', '000053a0', 20, 0, 'kept', 'lods=1 thr=0')]
        rows += [census_line('3100000%d' % i, '000053a0', s, 0, 'kept', 'lods=1 thr=0 body=stations\\docks\\argon_dock_center') for i, s in ((2, 26), (3, 30))]
        # A laddered body kept at LOD 0 with s below t1 (250): flagged; two draws.
        rows.append(census_line('31000010', '00005470', 26, 0, 'kept', 'lods=4 thr=0,250,150,80 body=v\\00043'))
        # A laddered body that switched normally (s 10 < t2 24): one draw, no flag.
        rows.append(census_line('31000020', '00005480', 10, 2, 'kept', 'lods=3 thr=0,50,24'))
        # A node culled before the model lookup (no ladder) and a pre-ladder row: unresolved, no draws.
        rows.append(census_line('31000030', '00005490', 1, 0, 'culled_size', 'lods=- thr=- body=-'))
        rows.append(census_line('31000040', '000054a0', 40, 0, 'kept'))
        # The env-map view and another frame must not enter the main-view table.
        rows.append(census_line('31000001', '000053a0', 5, 0, 'kept', 'lods=1 thr=0', view='01000000'))
        rows.append(census_line('31000050', '000054b0', 3, 0, 'kept', 'lods=1 thr=0', frame=1))
        for index, (node, model) in enumerate([('31000001', '000053a0')] * 2 + [('31000002', '000053a0'), ('31000003', '000053a0'),
                                               ('31000010', '00005470'), ('31000010', '00005470'), ('31000020', '00005480'),
                                               ('3100ffff', '0000ffff'), ('31000060', '000053a0')], start=1):
            rows += context_draw(index, node, model)
        rows += context_draw(1, '31000050', '000054b0', frame=1)
        self.log = Path(self.temporary.name) / 'session-20260922-130000-100.log'
        self.log.write_text('\n'.join(rows) + '\n')

    def tearDown(self):
        self.temporary.cleanup()

    def parsed(self):
        with self.log.open() as stream:
            return self.module.parse(stream, 1)

    def test_models_sorted_by_draws_with_flags(self):
        parsed = self.parsed()
        self.assertEqual(self.module.ladder_frame(parsed), LADDER_FRAME)
        result = self.module.ladder(parsed, LADDER_FRAME)
        self.assertEqual(result['view'], '34766bf8')
        # 9 draws: the 0000ffff draw has no census row, the 31000060 draw is a 000053a0 node without a main-view row.
        self.assertEqual((result['draws_in_frame'], result['joined_draws']), (9, 7))
        self.assertEqual([m['model'] for m in result['models']], ['000053a0', '00005470', '00005480', '00005490', '000054a0'])
        body, stuck, normal, culled, old = result['models']
        self.assertEqual((body['lods'], body['thr'], body['selected'], body['nodes'], body['kept'], body['draws'], body['s_min'], body['s_max']),
                         (1, [0], {0: 3}, 3, 3, 4, 20, 30))
        self.assertEqual(body['flags'], ['no_ladder'])
        self.assertEqual([m['body'] for m in result['models']], ['stations\\docks\\argon_dock_center', 'v\\00043', None, None, None])
        self.assertEqual((stuck['lods'], stuck['thr'], stuck['lod0_below_t1'], stuck['flags']), (4, [0, 250, 150, 80], 1, ['lod0_below_t1']))
        self.assertEqual((normal['selected'], normal['flags']), ({2: 1}, []))
        self.assertEqual((culled['lods'], culled['kept'], culled['draws'], culled['flags']), (None, 0, 0, ['unresolved']))
        self.assertEqual((old['lods'], old['thr'], old['flags']), (None, [], ['unresolved']))
        self.assertEqual((result['no_ladder'], result['no_ladder_draws'], result['lod0_below_t1'], result['unresolved']), (1, 4, 1, 2))
        env = self.module.ladder(parsed, LADDER_FRAME, view='01000000')
        # The env-map view has only node 31000001: its two draws, not the model's four main-view draws.
        self.assertEqual([(m['model'], m['s_min'], m['draws']) for m in env['models']], [('000053a0', 5, 2)])
        with self.assertRaises(self.module.MalformedInput):
            self.module.ladder(parsed, LADDER_FRAME, view='0badf00d')

    def test_cli_text_json_and_refusal(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(self.module.main([self.temporary.name, '--ladder']), 0)
        text = output.getvalue()
        self.assertIn(f'ladder frame {LADDER_FRAME} view=34766bf8 models=5 draws=9 joined_draws=7 no_ladder=1 (4 draws) lod0_below_t1=1 unresolved=2', text)
        lines = text.splitlines()
        self.assertTrue(lines[1].rstrip().endswith('flags                     body'), lines[1])
        self.assertTrue(lines[2].startswith('000053a0') and 'no_ladder' in lines[2] and lines[2].endswith('  stations\\docks\\argon_dock_center'), lines[2])
        self.assertIn('0,250,150,80', lines[3])
        self.assertTrue(lines[3].endswith('  v\\00043') and lines[4].endswith('  -'), lines[3:5])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(self.module.main([self.temporary.name, '--ladder', '--json', '--view', '1000000']), 0)
        self.assertEqual([m['model'] for m in json.loads(output.getvalue())['models']], ['000053a0'])
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(self.module.main([self.temporary.name, '--ladder', '--frame', '7']), 2)
        self.assertIn('no cull_census rows', error.getvalue())
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(self.module.main([self.temporary.name, '--ladder', '--view', 'badf00d']), 2)
        self.assertIn('no cull_census rows for view 0badf00d', error.getvalue())


if __name__ == '__main__':
    unittest.main()
