"""Capture boundary/ambiguity tests using synthetic metadata, no game assets."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from summarize_capture import summarize


class CaptureSummaryTests(unittest.TestCase):
    def test_matches_preserve_effect_ambiguity_and_ignore_binding_events(self):
        index = {'effects': [
            {'path': 'shader/3_0/gui2d.fb', 'shaders': [{'fnv1a64': 'aaa'}]},
            {'path': 'shader/3_0/nebula.fb', 'shaders': [{'fnv1a64': 'aaa'}]},
        ]}
        trace = '\n'.join([
            'shader kind=ps id=aaa bytes=20 dumped=1',
            'draw frame=20 index=1 kind=indexed topology=4 primitives=2 vs=bbb ps=aaa',
            'surface role=rt0 ptr=123 width=640 height=480 format=21',
            'state id=7 value=1',
            'set_rt index=0 result=00000000',
            'surface role=binding ptr=456 width=320 height=240 format=21',
            'frame_end frame=20 draws=1 capture=1 present=00000000',
        ])
        result = summarize(trace, index)
        self.assertEqual(len(result['shaders']['aaa']['effect_candidates']), 2)
        frame = result['frames']['20']
        self.assertTrue(frame['complete'])
        self.assertEqual(frame['draws'][0]['targets'][0]['ptr'], '123')
        self.assertEqual(len(frame['draws'][0]['targets']), 1)

    def test_truncated_frame_is_not_reported_complete(self):
        result = summarize('draw frame=5 index=1 kind=up topology=4 primitives=1 vs=0 ps=0', {})
        self.assertFalse(result['frames']['5']['complete'])


if __name__ == '__main__':
    unittest.main()
