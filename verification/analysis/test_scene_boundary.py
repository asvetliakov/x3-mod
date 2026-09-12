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

    def selected(self,events=None):
        result=self.run_events(events);self.assertEqual(result['state'],8)
        self.assertEqual([s['confirmed'] for s in result['selections']],[1]);return result

    def test_background_accepts_any_successful_draw_on_the_pair_before_the_depth_clear(self):
        # Structural rule: shader identity is not a background criterion.
        self.events[1]['ps']=123;self.selected()
        for e in self.events[1:self.clear_indices[1]]:
            if e['kind']==1:e['vs'],e['ps']=0xf80f7af59b667bb7,0xd6f6ba4fee1cd53e
        self.selected()

    def test_background_requires_a_draw_and_the_separate_depth_clear(self):
        self.events.pop(self.clear_indices[1]);self.renumber(self.events);self.rejected()
        self.setUp();del self.events[1:self.clear_indices[1]];self.renumber(self.events);self.rejected()

    def test_background_draw_must_target_the_pair_with_a_full_viewport(self):
        self.events[1]['viewport'][3]-=1;self.rejected()
        self.setUp();self.events[1]['rt']=deepcopy(self.events[self.copy]['destination']);self.rejected()
        self.setUp();self.events[1]['depth']={'known':True};self.rejected()
        self.setUp();self.events[1]['only_rt0']=False;self.rejected()
        self.setUp();self.events[1]['z_write']=2;self.rejected()

    def test_null_pixel_shader_draw_is_tolerated_but_never_a_depth_writer(self):
        scene=[i for i,e in enumerate(self.events) if e['kind']==1 and self.clear_indices[1]<i<self.copy]
        self.events[scene[0]].update(ps=0,draw_state_known=False);self.selected()
        self.setUp();self.events[1].update(ps=0,draw_state_known=False);self.selected()
        # Unknown state with a pixel shader bound is still a failed query.
        self.setUp();self.events[scene[0]]['draw_state_known']=False;self.rejected()
        # An adapter that does know the z state of a null-PS draw may count it.
        self.setUp()
        for i in scene:self.events[i].update(ps=0,draw_state_known=False)
        self.rejected()
        self.events[scene[0]].update(draw_state_known=True,z_enable=1,z_write=1);self.selected()

    def test_second_depth_only_clear_inside_the_scene_rejects(self):
        extra=deepcopy(self.events[self.clear_indices[1]])
        self.events.insert(self.copy-1,extra);self.renumber(self.events);self.rejected()

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

    def fill(self, target=None, result=0):
        return dict(kind=6,result_known=True,result=result,
                    destination=deepcopy(target or self.events[self.copy]['destination']),
                    destination_rect_null=True)

    def test_verified_background_does_not_require_haze(self):
        for e in self.events[1:self.clear_indices[1]]:
            if e['kind']==1:e['vs'],e['ps']=0x7b6393fe2d3e1d85,0x6109cf64c03529dd
        result=self.run_events()
        self.assertEqual((result['state'],len(result['selections'])),(8,1))
        self.assertEqual(result['selections'][0]['confirmed'],1)

    def test_await_copy_accepts_known_distinct_scratch_fills_full_and_partial(self):
        fills=[self.fill() for _ in range(3)];fills[1]['destination_rect_null']=False
        self.events[self.copy:self.copy]=fills;self.renumber(self.events)
        result=self.run_events();self.assertEqual(result['state'],8)
        self.assertEqual(result['selections'][0]['confirmed'],1)

    def test_color_fill_rejects_main_depth_unknown_standalone_and_invalid_descriptor(self):
        for field,value in [('known',False),('identity',0),('identity',self.events[0]['rt']['identity']),
                            ('identity',self.events[0]['depth']['identity']),('container',0),
                            ('width',0),('height',0),('format',77),('format',113),('msaa',2)]:
            events=deepcopy(self.events);fill=self.fill();fill['destination'][field]=value
            events.insert(self.copy,fill);self.renumber(events)
            result=self.run_events(events)
            self.assertEqual((result['state'],result['selections'],result['rejection_sequence']),(9,[],self.copy+1),(field,value))

    def test_color_fill_shared_container_alias_is_rejected(self):
        for role in ('rt','depth'):
            events=deepcopy(self.events);protected=events[0][role]['identity'];container=self.fill()['destination']['container']
            for e in events:
                for field in ('rt','depth','source','destination'):
                    if e.get(field,{}).get('identity')==protected:e[field]['container']=container
            events.insert(self.copy,self.fill());self.renumber(events)
            result=self.run_events(events)
            self.assertEqual((result['state'],result['selections'],result['rejection_sequence']),(9,[],self.copy+1))

    def test_color_fill_failure_and_unknown_result_reject(self):
        for result_known,result_code in ((True,0x8876086c),(False,0)):
            events=deepcopy(self.events);fill=self.fill(result=result_code);fill['result_known']=result_known
            events.insert(self.copy,fill);self.renumber(events);result=self.run_events(events)
            self.assertEqual((result['rejection'],result['rejection_sequence'],result['selections']),(4,self.copy+1,[]))

    def test_color_fill_rejected_in_every_other_phase_including_selected(self):
        for index in (0,1,self.clear_indices[1]+1,self.copy+1,self.bloom[0],
                      self.bloom[-1]+1,self.boundary,len(self.events)):
            events=deepcopy(self.events);events.insert(index,self.fill());self.renumber(events)
            result=self.run_events(events);self.assertEqual(result['state'],9,index)
            self.assertEqual(result['rejection_sequence'],index+1,index)

    def test_first_pattern_rejection_survives_later_unsupported_and_invalidation(self):
        self.events[0]['clear_flags']=1
        self.events.insert(self.copy,self.fill());unsupported=self.fill();unsupported['kind']=5
        self.events.insert(self.copy+1,unsupported);self.renumber(self.events)
        result=self.run_events(extra={len(self.events)-1:['I']})
        self.assertEqual((result['rejection'],result['rejection_sequence'],result['selections']),(5,1,[]))

    def test_gameplay_fixtures_replay_to_the_live_adapter_boundaries(self):
        # Iteration 05: the live capture adapter confirmed these boundary events
        # (scene_depth_boundary records); four menu frames had a color-only Clear.
        live={1794:714,1795:714,1796:714,1797:714,1975:288,1976:289,1977:290,1978:290,
              2435:392,2436:392,2437:392,2438:392,2806:273,2807:273,2808:273,2809:273,
              3047:264,3048:262,3049:227,3050:227,4096:469,4097:469,4098:469,4099:469}
        fixture=json.loads((ROOT/'verification/fixtures/iteration05-scene-boundaries.json').read_text())
        outcomes={f['frame']:replay(self.executable,f,expand(fixture,f,True)) for f in fixture['frames']}
        self.assertEqual(sorted(outcomes),[120,121,122,123,*sorted(live)])
        for frame,sequence in live.items():
            self.assertEqual((outcomes[frame]['state'],[s['sequence'] for s in outcomes[frame]['selections'] if s['confirmed']]),(8,[sequence]),frame)
        for frame in (120,121,122,123):
            self.assertEqual((outcomes[frame]['state'],outcomes[frame]['rejection_sequence'],outcomes[frame]['selections']),(9,1,[]),frame)
        # Iteration 06: every captured frame selects at the depth-only Clear that
        # follows background + scene + the four bloom draws (analyzer phase counts).
        fixture=json.loads((ROOT/'verification/fixtures/iteration06-scene-boundaries.json').read_text())
        summary=json.loads((ROOT/'verification/results/iteration-06-motion-summary.json').read_text())
        phases={f['frame']:f['phase_draws'] for b in summary['bursts'] for f in b['per_frame']}
        previously_rejected={f['frame'] for f in summary['scene_selection']['rejected_frames']}
        self.assertEqual(len(previously_rejected),32)
        selected=set()
        for frame in fixture['frames']:
            result=replay(self.executable,frame,expand(fixture,frame,True))
            confirmed=[s for s in result['selections'] if s['confirmed']]
            self.assertEqual((result['state'],len(confirmed)),(8,1),frame['frame'])
            p=phases[frame['frame']]
            self.assertEqual((frame['after_draw'][confirmed[0]['sequence']-1],p['bloom']),(p['background']+p['scene']+p['bloom'],4),frame['frame'])
            selected.add(frame['frame'])
        self.assertEqual(len(selected),68);self.assertTrue(previously_rejected<=selected)

    def test_truncation_cannot_confirm_future_clear(self):
        for end in (self.copy,self.bloom[-1]+1,self.boundary):
            self.assertEqual(self.run_events(self.events[:end])['selections'],[])


if __name__=='__main__':unittest.main()
