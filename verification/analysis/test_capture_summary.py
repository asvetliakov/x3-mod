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
        self.assertEqual(frame['draws'][0]['topology'], 4)
        self.assertEqual(frame['draws'][0]['targets'][0]['ptr'], '123')
        self.assertEqual(len(frame['draws'][0]['targets']), 1)

    def test_truncated_frame_is_not_reported_complete(self):
        result = summarize('draw frame=5 index=1 kind=up topology=4 primitives=1 vs=0 ps=0', {})
        self.assertFalse(result['frames']['5']['complete'])

    def test_typed_namespaces_device_frames_and_failed_draw(self):
        lines=[]
        for device in (1,2):
            lines.extend([
                f'frame_begin device={device} frame=1',
                f'draw device={device} frame=1 index=1 kind=indexed topology=4 primitives=1 vs=vs ps=ps',
                'constants kind=vs type=f count=256 result=00000000 encoding=sparse_zero',
                'constant kind=vs type=f reg=0 bits=80000000,00000000,00000000,00000000',
                'constant kind=vs type=i reg=0 values=0,-2,7,0',
                'constant kind=vs type=b reg=0 values=0',
                'geometry source=buffers',
                'stream slot=0 identity=1 offset=16 stride=20',
                'indices identity=2 result=00000000',
                'draw_result result=8876086c',
                f'frame_end device={device} frame=1 draws=1 capture=1 present=00000000',
            ])
        result=summarize('\n'.join(lines),{},include_floats=True)
        self.assertEqual(set(result['frames']),{'1:1','2:1'})
        for frame in result['frames'].values():
            self.assertTrue(frame['draw_count_matches'])
            draw=frame['draws'][0]
            self.assertEqual(draw['constants']['vs']['i']['0'],[0,-2,7,0])
            self.assertEqual(draw['constants']['vs']['b']['0'],[0])
            self.assertEqual(draw['constants']['vs']['f']['0'],'80000000,00000000,00000000,00000000')
            self.assertEqual(draw['draw_result']['result'],'8876086c')

    def test_empty_frame_and_missing_draws_are_explicit(self):
        trace='frame_begin device=1 frame=4\nframe_end device=1 frame=4 draws=0 capture=1 present=00000000'
        frame=summarize(trace,{})['frames']['1:4']
        self.assertTrue(frame['complete'] and frame['draw_count_matches'])
        frame=summarize(trace.replace('draws=0','draws=1'),{})['frames']['1:4']
        self.assertFalse(frame['draw_count_matches'])

    def test_ordered_clear_is_not_attached_to_previous_draw(self):
        trace='\n'.join([
            'frame_begin device=1 frame=3',
            'capture_event device=1 frame=3 seq=1 after_draw=0 op=draw_begin result=00000000',
            'draw device=1 frame=3 index=1 kind=up topology=4 primitives=1 vs=0 ps=0',
            'surface role=rt0 identity=3',
            'draw_result result=00000000',
            'capture_event device=1 frame=3 seq=2 after_draw=1 op=clear result=00000000',
            'clear flags=2 color=0 z=1 stencil=0 rect_count=0',
            'surface role=clear_depth identity=4',
            'frame_end device=1 frame=3 draws=1 capture=1 present=00000000'])
        frame=summarize(trace,{})['frames']['1:3']
        self.assertTrue(frame['event_sequence_contiguous'])
        self.assertEqual(len(frame['draws'][0]['targets']),1)
        self.assertEqual(frame['events'][1]['details'][1]['identity'],'4')
        self.assertNotIn('depth',frame['draws'][0])
        frame=summarize(trace.replace('seq=2','seq=3'),{})['frames']['1:3']
        self.assertFalse(frame['event_sequence_contiguous'])


if __name__ == '__main__':
    unittest.main()
