"""Original diagnostic metadata: preserve scope/status/bit evidence without matching objects."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from summarize_capture import summarize

DRAW = 'draw device=1 frame=7 index=1 kind=indexed topology=4 primitives=2 vs=abc ps=def'
CONTEXT = ('object_context device=1 frame=7 index=1 scoped=1 valid=127 session=2 scope_depth=1 '
           'mesh=01000000 node=02000000 node_handle=43 camera=03000000 camera_handle=44 '
           'registry=04000000 engine=05000000 model=0000000a lod=00000001 flags12c=80000000 flags130=00000000')
BITS = '80000000,7fc01234,7f800000,ff800000'


def draw_summary(records, include_floats=False):
    result = summarize('\n'.join(['frame_begin device=1 frame=7', DRAW, *records,
                                 'draw_result device=1 frame=7 index=1 result=00000000',
                                 'frame_end device=1 frame=7 draws=1 capture=1 present=00000000']),
                       {}, include_floats)
    return result['frames']['1:7']['draws'][0]


class CaptureObjectSummaryTests(unittest.TestCase):
    def test_valid_context_all_matrix_roles_position_basis_and_scale_are_raw(self):
        records = [CONTEXT]
        for role in ('world', 'world_basis', 'view', 'projection', 'scale'):
            for row in range(1 if role == 'scale' else 4):
                records.append(f'object_matrix role={role} row={row} bits={BITS}')
        records += ['object_position bits=80000000,00000001,7fc01234']
        records += [f'object_basis row={row} bits=3f800000,00000000,80000000' for row in range(3)]
        draw = draw_summary(records)
        self.assertTrue(draw['object_context_matches_draw'])
        self.assertEqual(draw['object_context']['valid'], '127')
        self.assertEqual(draw['object_context']['node_handle'], '43')
        self.assertEqual(draw['object_context']['model'], '0000000a')
        self.assertEqual(len(draw['object_matrix']), 17)
        self.assertEqual(draw['object_matrix'][-1], dict(role='scale', row='0', bits=BITS))
        self.assertEqual(draw['object_position'][0]['bits'], '80000000,00000001,7fc01234')
        self.assertEqual(draw['object_basis'][-1]['row'], '2')
        self.assertNotIn('object_scale', draw)
        # No NaN canonicalization or float normalization during JSON output.
        self.assertEqual(json.loads(json.dumps(draw, allow_nan=False)), draw)

    def test_missing_diagnostics_are_not_fabricated(self):
        draw = draw_summary([])
        for name in ('object_context', 'object_context_matches_draw', 'object_matrix',
                     'object_position', 'object_basis', 'buffer_content'):
            self.assertNotIn(name, draw)

    def test_scoped_false_and_unknown_valid_mask_remain_explicit(self):
        for scoped, valid in (('0', '0'), ('1', '0'), ('1', '128')):
            draw = draw_summary([CONTEXT.replace('scoped=1 valid=127', f'scoped={scoped} valid={valid}')])
            self.assertTrue(draw['object_context_matches_draw'])
            self.assertEqual(draw['object_context']['scoped'], scoped)
            self.assertEqual(draw['object_context']['valid'], valid)
            self.assertNotIn('object_matrix', draw)
            self.assertNotIn('object_position', draw)

    def test_context_device_frame_and_draw_coordinates_must_match_explicitly(self):
        for old, new in (('device=1', 'device=2'), ('frame=7', 'frame=8'), ('index=1', 'index=2'),
                         ('device=1 ', ''), ('frame=7 ', ''), ('index=1 ', '')):
            with self.subTest(field=old):
                draw = draw_summary([CONTEXT.replace(old, new)])
                self.assertFalse(draw['object_context_matches_draw'])
                self.assertIn('object_context', draw)  # Retained for diagnosis, not accepted identity.

    def test_rows_without_context_and_unknown_roles_are_retained_without_validity_inference(self):
        draw = draw_summary([f'object_matrix role=future_role row=9 bits={BITS}',
                             'object_basis row=0 bits=00000000,00000000,00000000'])
        self.assertNotIn('object_context', draw)
        self.assertNotIn('object_context_matches_draw', draw)
        self.assertEqual(draw['object_matrix'][0]['role'], 'future_role')
        self.assertNotIn('object_position', draw)

    def test_buffer_off_unknown_failed_pending_and_success_statuses_are_distinct(self):
        statuses = [
            'result=80070057 status=00000001 requested=0 known=0 ambiguous=0 revision=0 pending=0 flags=00000000',
            'result=00000000 status=00000001 requested=0 known=0 ambiguous=0 revision=0 pending=0 flags=00000000',
            'result=00000000 status=8876086c requested=1 known=0 ambiguous=1 revision=0 pending=0 flags=00000000',
            'result=00000000 status=00000001 requested=1 known=0 ambiguous=0 revision=9 pending=1 flags=00002000',
            'result=00000000 status=00000001 requested=1 known=0 ambiguous=1 revision=9 pending=0 flags=00000010',
            'result=00000000 status=00000000 requested=1 known=1 ambiguous=0 revision=0 pending=0 flags=00000000',
            'result=00000000 status=00000000 requested=1 known=1 ambiguous=0 revision=10 pending=0 flags=00001000',
        ]
        records = [f'buffer_content kind=vertex identity=12 {s}' for s in statuses]
        draw = draw_summary(records)
        self.assertEqual(len(draw['buffer_content']), len(statuses))
        for raw, parsed in zip(statuses, draw['buffer_content']):
            expected = dict(token.split('=') for token in raw.split())
            self.assertEqual(parsed, dict(kind='vertex', identity='12', **expected))
            self.assertNotIn('stable', parsed)

    def test_buffer_records_do_not_collapse_identity_or_kind(self):
        status = 'result=00000000 status=00000000 requested=1 known=1 ambiguous=0 revision=2 pending=0 flags=00000000'
        draw = draw_summary([f'buffer_content kind={kind} identity={identity} {status}'
                             for kind, identity in (('vertex', 12), ('vertex', 12), ('index', 13))])
        self.assertEqual([r['kind'] for r in draw['buffer_content']], ['vertex', 'vertex', 'index'])
        self.assertEqual([r['identity'] for r in draw['buffer_content']], ['12', '12', '13'])

    def test_records_are_draw_local_across_boundaries_and_reused_frame_indices(self):
        orphan = f'object_matrix role=world row=0 bits={BITS}'
        lines = [orphan, 'frame_begin device=1 frame=7', DRAW, CONTEXT,
                 'capture_event device=1 frame=7 seq=1 after_draw=1 op=clear result=00000000',
                 orphan, 'buffer_content kind=vertex identity=999 known=1',
                 'frame_end device=1 frame=7 draws=1 capture=1 present=00000000',
                 orphan, 'frame_begin device=2 frame=7', DRAW.replace('device=1', 'device=2'),
                 'frame_end device=2 frame=7 draws=1 capture=1 present=00000000']
        frames = summarize('\n'.join(lines), {})['frames']
        self.assertTrue(frames['1:7']['draws'][0]['object_context_matches_draw'])
        self.assertNotIn('object_context', frames['2:7']['draws'][0])
        for frame in frames.values():
            self.assertNotIn('object_matrix', frame['draws'][0])
            self.assertNotIn('buffer_content', frame['draws'][0])

    def test_next_draw_missing_context_does_not_inherit_it(self):
        trace = '\n'.join([DRAW, CONTEXT, f'object_matrix role=world row=0 bits={BITS}',
                           DRAW.replace('index=1', 'index=2')])
        draws = summarize(trace, {})['frames']['1:7']['draws']
        self.assertIn('object_context', draws[0])
        self.assertNotIn('object_context', draws[1])
        self.assertNotIn('object_matrix', draws[1])

    def test_include_floats_option_does_not_drop_object_bits_or_change_legacy_constant_rules(self):
        records = [CONTEXT, f'object_matrix role=projection row=0 bits={BITS}',
                   f'constant kind=vs type=f reg=0 bits={BITS}']
        without = draw_summary(records)
        with_floats = draw_summary(records, True)
        self.assertEqual(without['object_matrix'], with_floats['object_matrix'])
        self.assertNotIn('constants', without)
        self.assertEqual(with_floats['constants']['vs']['f']['0'], BITS)


if __name__ == '__main__':
    unittest.main()
