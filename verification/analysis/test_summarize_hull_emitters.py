"""Host test of the per-model hull-emitter join (tools/analysis/summarize_hull_emitters.py)."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from summarize_hull_emitters import PROGRAMS, render, summarize  # noqa: E402


class ProgramTableTests(unittest.TestCase):
    def test_programs_equal_the_twelve_hull_profiles_of_the_transformer(self):
        # The tool's hash table mirrors hull_profiles[] in order; parse the source so a drift is caught.
        source = (ROOT / 'src/renderer/linear_emission.cpp').read_text()
        block = source[source.index('constexpr HullProfile hull_profiles[] = {'):]
        block = block[:block.index('};')]
        rows = re.findall(r'\{0x([0-9a-f]{16})ull,\d+,\d+,\d+,(?:true|false)\}', block)
        self.assertEqual(len(rows), 12)
        self.assertEqual(tuple(rows), PROGRAMS)

DRAW = ('hull_emission_draw device=1 frame={frame} index={index} vs=494fe349b8bc12ec ps=7c83ed50c9894e44 program=7 routed=0 gain=2 '
        'known={known} node=0x1100 node_handle={handle} node_serial=12 camera_handle=9 model={model} lod={lod} load_epoch=3 registry_epoch=5 '
        'primitives={prims} vertices=3 indexed=0 origin_known={origin_known} origin_px={origin} origin_w=4')
FRAME = ('hull_emission_frame device=1 frame={frame} gain=2 admitted={admitted} refused_blend={blend} refused_variant=0 programs={programs} '
         'refused_other=0 refused_routed=0 refused_unknown=0 refused_state=0 bind_failures=0 opaque={opaque} alpha={alpha} toggled={toggled}')


class SummarizeHullEmitters(unittest.TestCase):
    def test_joins_draws_per_model_and_lod_with_frame_totals(self):
        lines = [
            'motion_route device=1 frame=1 index=1 routed=1 ps=7c83ed50c9894e44',  # ignored
            DRAW.format(frame=1, index=2, known=1, handle=8, model='00000012', lod='00000002', prims=40, origin_known=1, origin='100.0,200.0'),
            DRAW.format(frame=1, index=3, known=1, handle=8, model='00000012', lod='00000002', prims=2, origin_known=1, origin='110.0,190.0'),
            DRAW.format(frame=2, index=2, known=1, handle=21, model='00000012', lod='00000002', prims=40, origin_known=0, origin='0.0,0.0'),
            DRAW.format(frame=2, index=5, known=0, handle=0, model='00000000', lod='00000000', prims=6, origin_known=0, origin='0.0,0.0'),
            FRAME.format(frame=1, admitted=2, blend=5, programs='080', opaque=4, alpha=1, toggled=1),
            FRAME.format(frame=2, admitted=2, blend=3, programs='880', opaque=3, alpha=0, toggled=1),
            FRAME.format(frame=3, admitted=0, blend=1, programs='000', opaque=1, alpha=0, toggled=0),
        ]
        summary = summarize(lines)
        self.assertEqual(summary['capture_frames'], [1, 2])
        self.assertEqual([(r['model'], r['lod'], r['draws'], r['primitives'], r['frames'], r['nodes'], r['node_handles'], r['unknown_scope'])
                          for r in summary['models']],
                         [('00000012', '00000002', 3, 82, 2, 1, [8, 21], 0), ('00000000', '00000000', 1, 6, 1, 0, [], 1)])
        self.assertEqual(summary['models'][0]['program_hashes'], ['7c83ed50c9894e44'])
        self.assertEqual(summary['models'][0]['origin_bounds'], [100.0, 190.0, 110.0, 200.0])
        self.assertIsNone(summary['models'][1]['origin_bounds'])
        totals = summary['frame_totals']
        self.assertEqual((totals['lines'], totals['admitted'], totals['refused_blend'], totals['opaque'], totals['alpha'],
                          totals['toggled_off_frames'], totals['programs'], totals['program_hashes']),
                         (3, 4, 9, 8, 1, 1, '880', ['7c83ed50c9894e44', 'e70adc744a38ca59']))
        text = render(summary)
        self.assertIn('00000012   00000002        3         82      2      1.5 7', text)
        self.assertIn('admitted=4 refused_blend=9 (opaque=8 alpha=1)', text)

    def test_empty_log_is_an_empty_table(self):
        summary = summarize([])
        self.assertEqual((summary['capture_frames'], summary['models'], summary['frame_totals']['lines']), ([], [], 0))
        self.assertIn('capture frames with hull emitter draws: 0', render(summary))


if __name__ == '__main__':
    unittest.main()
