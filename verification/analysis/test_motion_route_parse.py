"""Parser checks for the `motion_route` draw-state fields; no Wine, no DLL build."""
from pathlib import Path
import re
import unittest

import motion_route

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/motion_output.cpp'

HEAD = ('motion_route device=1 frame=7 index={index} gate={gate} routed={routed} matched=0 depth=0'
        ' jittered=1 vs=1111111111111111 ps=2222222222222222 pass=1 rows_hash=0 result=00000000')
STATE = ' zwrite={zwrite} blend={blend} src={src} dst={dst} atest={atest} mask={mask} sepalpha={sepalpha} fog={fog}'
OPAQUE = dict(zwrite=1, blend=0, src=2, dst=1, atest=0, mask=15, sepalpha=0, fog=0)
SOURCE_OVER = dict(OPAQUE, blend=1, src=5, dst=6, zwrite=0)


def line(index=0, gate=4, routed=0, state=None):
    text = HEAD.format(index=index, gate=gate, routed=routed)
    return text + STATE.format(**state) if state else text


class MotionRouteFields(unittest.TestCase):
    def test_source_line_logs_every_state_field(self):
        """The DLL's capture line and the parser agree on the field names."""
        text = SOURCE.read_text()
        emitted = re.search(r'" zwrite=[^"]*"', text)
        self.assertIsNotNone(emitted, 'motion_route no longer appends the draw state')
        names = re.findall(r'(\w+)=%ld', emitted.group(0))
        self.assertEqual(names, list(motion_route.STATE_FIELDS))

    def test_states_parse_and_classify(self):
        r, = motion_route.parse([line(state=SOURCE_OVER)])
        self.assertEqual(r.gate_name, 'draw_state')
        self.assertEqual(r.state, dict(SOURCE_OVER))
        self.assertTrue(r.blended and r.source_over)
        self.assertFalse(r.alpha_tested)
        self.assertEqual(r.pair, ('1111111111111111', '2222222222222222'))

    def test_opaque_and_alpha_tested_are_not_source_over(self):
        opaque, = motion_route.parse([line(state=OPAQUE)])
        self.assertFalse(opaque.blended or opaque.source_over)
        tested, = motion_route.parse([line(state=dict(OPAQUE, atest=1, mask=7))])
        self.assertTrue(tested.alpha_tested)
        self.assertEqual(tested.state['mask'], 7)
        # Blended with another factor pair: blended, but not source-over.
        other, = motion_route.parse([line(state=dict(SOURCE_OVER, dst=2))])
        self.assertTrue(other.blended)
        self.assertFalse(other.source_over)

    def test_unknown_states_are_none_and_never_claimed(self):
        """-1 is the shadow's 'unknown'; it must not read as a classification."""
        unknown, = motion_route.parse([line(state=dict(SOURCE_OVER, src=-1, dst=-1, blend=-1, atest=-1))])
        self.assertIsNone(unknown.state['src'])
        self.assertIsNone(unknown.blended)
        self.assertIsNone(unknown.source_over)
        self.assertIsNone(unknown.alpha_tested)
        self.assertIn('blend=?', unknown.signature)
        # Blend known on, factors unknown: blended, source-over undecided.
        partial, = motion_route.parse([line(state=dict(SOURCE_OVER, dst=-1))])
        self.assertTrue(partial.blended)
        self.assertIsNone(partial.source_over)

    def test_older_lines_without_the_fields_still_parse(self):
        old, = motion_route.parse([line()])
        self.assertEqual(set(old.state.values()), {None})
        self.assertIsNone(old.blended)
        self.assertEqual(old.signature.count('?'), len(motion_route.STATE_FIELDS))

    def test_census_groups_gate4_by_pair_and_signature(self):
        lines = [line(index=0, state=SOURCE_OVER), line(index=1, state=SOURCE_OVER),
                 line(index=2, state=OPAQUE), line(index=3, gate=0, routed=1, state=OPAQUE),
                 'motion_output_frame device=1 frame=7 draws=4']
        records = motion_route.parse(lines)
        self.assertEqual(len(records), 4)
        self.assertEqual(sorted(motion_route.census(records).values()), [1, 2])
        self.assertEqual(sum(motion_route.census(records).values()), 3)
        self.assertEqual(list(motion_route.by_frame(records)), [(1, 7)])

    def test_malformed_gate_is_rejected(self):
        with self.assertRaises(ValueError):
            motion_route.parse([line(gate=9)])


if __name__ == '__main__':
    unittest.main()
