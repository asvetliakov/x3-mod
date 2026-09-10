"""Synthetic motion keys reject aliasing and do not depend on draw order."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from analyze_camera import identity
from analyze_motion import candidate_key, compare_candidates, camera_delta


def observation(index, world_x=0):
    w = identity()
    w[0][3] = world_x
    return dict(draw=dict(index=index, states={'7': 1}, viewport=dict(x=0,y=0,w=100,h=100)),
                world=w, wvp=identity())


def buffered_draw():
    return dict(kind='indexed', topology='4', vs='shader', ps='pixel', primitives=3,
                geometry={'source': 'buffers'}, device=1,
                stream=[dict(slot='0', identity='12', offset='0', stride='32', frequency='1',
                             result='00000000', frequency_result='00000000')],
                indices=dict(identity='13', result='00000000'), draw_args=dict(start_index='0'))


class MotionAnalysisTests(unittest.TestCase):
    def test_reordered_draws_match_without_index_identity(self):
        r = compare_candidates({'a':[observation(9)]}, {'a':[observation(19)]})
        self.assertEqual(r['unchanged_world_matches'], 1)
        self.assertEqual(r['changed_draw_index_matches'], 1)

    def test_duplicate_instance_keys_are_excluded(self):
        r = compare_candidates({'a':[observation(1),observation(2)]}, {'a':[observation(3)]})
        self.assertFalse(r['unique_matches'])
        self.assertEqual(r['ambiguous_shared_keys'][0]['previous_count'], 2)

    def test_world_change_is_separate_from_camera_motion(self):
        r = compare_candidates({'a':[observation(1)]}, {'a':[observation(2, 5)]})
        self.assertEqual(r['changed_world_matches'], 1)
        self.assertEqual(r['unique_matches'][0]['world_translation_delta'], [5,0,0])
        a=identity(); b=identity()
        b[0][0]=b[2][2]=0; b[0][2]=1; b[2][0]=-1
        self.assertAlmostEqual(camera_delta(a,b)['forward_axis_angle_degrees'],90)

    def test_resource_range_device_and_topology_distinguish_keys(self):
        a=buffered_draw()
        for mutate in [lambda b:b.update(device=2), lambda b:b.update(topology='5'),
                       lambda b:b['stream'][0].update(identity='14'),
                       lambda b:b['draw_args'].update(start_index='3')]:
            b=copy.deepcopy(a); mutate(b)
            self.assertNotEqual(candidate_key(a),candidate_key(b))

    def test_up_and_failed_stream_queries_are_not_candidates(self):
        a=buffered_draw(); a['geometry']['source']='up'
        self.assertIsNone(candidate_key(a))
        a=buffered_draw(); a['stream'][0]['result']='8876086c'
        self.assertIsNone(candidate_key(a))
        a=buffered_draw(); a['stream'][0]['frequency_result']='8876086c'
        self.assertIsNone(candidate_key(a))


if __name__=='__main__':
    unittest.main()
