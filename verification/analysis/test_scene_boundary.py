"""Replay actual derived metadata plus adversarial events against production C++."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from replay_scene_boundary import ROOT, build, expand, replay


class SceneBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.executable=Path(cls.temp.name)/'replay'
        build(cls.executable)
        cls.fixture=json.loads((ROOT/'verification/fixtures/station-scene-boundaries.json').read_text())
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):
        self.frame=deepcopy(self.fixture['frames'][0]);self.events=expand(self.fixture,self.frame,True)
        self.clear_indices=[i for i,e in enumerate(self.events) if e['kind']==0]
        self.boundary=self.clear_indices[2]
        self.copy=next(i for i,e in enumerate(self.events) if e['kind']==4)
        self.bloom=[i for i,e in enumerate(self.events) if e['kind']==1 and i>self.copy][:4]
    def run_events(self,events=None,extra=None):
        return replay(self.executable,self.frame,self.events if events is None else events,extra)
    def rejected(self):
        result=self.run_events();self.assertFalse(any(s['confirmed'] for s in result['selections']))
        self.assertEqual(result['state'],9)
    @staticmethod
    def renumber(events):
        for index,e in enumerate(events,1):e['sequence']=index

    def test_twelve_actual_frames_conditional_selection_matches_derived_boundaries(self):
        expected=json.loads((ROOT/'verification/results/game-station-session-boundaries.json').read_text())['frames']
        for frame in self.fixture['frames']:
            with self.subTest(frame=frame['frame']):
                result=replay(self.executable,frame,expand(self.fixture,frame,True))
                self.assertEqual(len(result['selections']),1)
                s=result['selections'][0];self.assertEqual((s['candidate'],s['confirmed'],s['epoch']),(1,1,2))
                actual_after=frame['after_draw'][s['sequence']-1]
                target=expected[f"{frame['device']}:{frame['frame']}"]['bloom_chains'][0]['next_depth_clear']['after_draw']
                self.assertEqual(actual_after,target)

    def test_observed_only_frames_fail_on_missing_clear_viewport_and_mrt_status(self):
        for frame in self.fixture['frames']:
            result=replay(self.executable,frame,expand(self.fixture,frame,False))
            self.assertEqual(result['selections'],[])

    def test_no_fixed_draw_count_or_resource_ids(self):
        events=deepcopy(self.events)
        # Duplicate a scene geometry draw, shifting every later event/draw index.
        events.insert(self.clear_indices[1]+2,deepcopy(events[self.clear_indices[1]+1]))
        for e in events:
            for role in ('rt','depth','source','destination'):
                for key in ('identity','container'):
                    if e.get(role,{}).get(key):e[role][key]+=1000
            if e.get('texture0'):e['texture0']+=1000
        self.renumber(events);result=self.run_events(events)
        self.assertEqual(len(result['selections']),1)
        self.assertEqual(result['selections'][0]['depth'],1002)
        self.assertEqual(result['selections'][0]['sequence'],self.boundary+2)

    def test_failed_or_unknown_prior_calls_fail_closed(self):
        for index in (0,self.clear_indices[1],self.copy,*self.bloom,self.boundary-1):
            for field,value in [('result',0x8876086c),('result_known',False)]:
                with self.subTest(index=index,field=field):
                    events=deepcopy(self.events);events[index][field]=value
                    self.assertEqual(self.run_events(events)['selections'],[])

    def test_failed_pending_clear_can_offer_copy_but_never_confirm(self):
        self.events[self.boundary]['result']=0x8876086c
        result=self.run_events();self.assertEqual(len(result['selections']),1)
        self.assertEqual(result['selections'][0]['candidate'],1)
        self.assertEqual(result['selections'][0]['confirmed'],0)
        self.assertEqual(result['state'],9)

    def test_sequence_gaps_duplicates_and_reordering(self):
        for index in (0,self.copy,self.bloom[1],self.boundary):
            for delta in (-1,1):
                events=deepcopy(self.events);events[index]['sequence']+=delta
                self.assertEqual(self.run_events(events)['selections'],[])
        self.events[self.bloom[0]],self.events[self.bloom[1]]=self.events[self.bloom[1]],self.events[self.bloom[0]]
        self.renumber(self.events);self.rejected()

    def test_background_requires_observed_family_and_separate_clear(self):
        self.events[1]['ps']=123;self.rejected()
        self.setUp();self.events.pop(self.clear_indices[1]);self.renumber(self.events);self.rejected()

    def test_scene_requires_depth_writing_geometry(self):
        for e in self.events[self.clear_indices[1]+1:self.copy]:
            if e['kind']==1:e['z_write']=0
        self.rejected()

    def test_copy_source_destination_parent_and_full_rect_gates(self):
        mutations=[('source_rect_null',False),('destination_rect_null',False)]
        for field,value in mutations:
            events=deepcopy(self.events);events[self.copy][field]=value
            self.assertEqual(self.run_events(events)['selections'],[])
        for role,field,value in [('source','identity',999),('destination','width',640),
                                 ('destination','container',0),('destination','msaa',2)]:
            events=deepcopy(self.events);events[self.copy][role][field]=value
            self.assertEqual(self.run_events(events)['selections'],[])

    def test_each_bloom_hash_input_quad_viewport_and_depth_gate(self):
        for index in self.bloom:
            for field,value in [('vs',1),('ps',2),('texture0',999),('topology',4),('primitives',1),
                                ('z_enable',1),('z_write',1),('draw_state_known',False),('only_rt0',False)]:
                events=deepcopy(self.events);events[index][field]=value
                self.assertEqual(self.run_events(events)['selections'],[],(index,field))
            events=deepcopy(self.events);events[index]['viewport'][3]-=1
            self.assertEqual(self.run_events(events)['selections'],[])
            events=deepcopy(self.events);events[index]['depth']['identity']=2
            self.assertEqual(self.run_events(events)['selections'],[])

    def test_bloom_target_order_sizes_and_parent_alias(self):
        for index in self.bloom:
            for field,value in [('identity',999),('container',999),('width',1),('format',113),('msaa',2)]:
                events=deepcopy(self.events);events[index]['rt'][field]=value
                self.assertEqual(self.run_events(events)['selections'],[])
        self.events[self.bloom[1]-1]['rt']['container']=self.events[self.bloom[0]]['rt']['container']
        self.rejected()

    def test_original_depth_rebind_and_clear_identity_required(self):
        for index in (self.boundary-1,self.boundary):
            events=deepcopy(self.events);events[index]['depth']['identity']=999
            self.assertEqual(self.run_events(events)['selections'],[])

    def test_clear_full_viewport_range_flags_and_query_gates(self):
        for index in self.clear_indices[:3]:
            for field,value in [('rect_count',1),('clear_z',.5),('only_rt0',False)]:
                events=deepcopy(self.events);events[index][field]=value
                self.assertEqual(self.run_events(events)['selections'],[])
            for vp_index,value in ((0,False),(1,1),(3,640),(6,.5)):
                events=deepcopy(self.events);events[index]['viewport'][vp_index]=value
                self.assertEqual(self.run_events(events)['selections'],[])
        self.events[self.boundary]['clear_flags']=3;self.rejected()

    def test_interposed_draw_copy_or_unsupported_event_rejects_final_clear(self):
        for kind in (1,4,5):
            events=deepcopy(self.events);e=deepcopy(events[self.boundary]);e['kind']=kind
            events.insert(self.boundary,e);self.renumber(events)
            self.assertEqual(self.run_events(events)['selections'],[])

    def test_device_loss_reset_generation_invalidates_pending_selection(self):
        self.assertEqual(self.run_events(extra={self.boundary:['I']})['selections'],[])
        # A new frame cannot inherit the old bloom chain, even reusing numeric frame index.
        self.assertEqual(self.run_events(extra={self.boundary:['B 2 2 2754']})['selections'],[])
        self.frame['generation']=0;self.rejected()

    def test_original_synthetic_signature_profile_survives_begin_frame(self):
        original=[(0x7b6393fe2d3e1d85,0x6109cf64c03529dd),
                  (0x37c34a7478544c14,0x5f82ecacd39529cd),
                  (0xbe199829a9bb78db,0xcd6d6eb4b3d99443)]
        original += [(self.events[i]['vs'],self.events[i]['ps']) for i in self.bloom]
        profile=[(100+2*i,101+2*i) for i in range(7)]
        remap=dict(zip(original,profile))
        for e in self.events:
            pair=remap.get((e.get('vs'),e.get('ps')))
            if pair:e['vs'],e['ps']=pair
        self.assertEqual(self.run_events()['selections'],[])
        result=replay(self.executable,self.frame,self.events,
                      extra_commands={0:['B 7 2 2754']},signature_profile=profile)
        self.assertEqual(len(result['selections']),1)
        self.assertEqual(result['selections'][0]['confirmed'],1)

    def test_truncation_cannot_confirm_future_clear(self):
        for end in (self.copy,self.bloom[-1]+1,self.boundary):
            self.assertEqual(self.run_events(self.events[:end])['selections'],[])


if __name__=='__main__':unittest.main()
