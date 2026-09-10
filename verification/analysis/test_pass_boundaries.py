"""Boundary evidence must use successful ordered calls and resource flow."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from analyze_pass_boundaries import inspect_frame, BLOOM_PAIRS

class BoundaryTests(unittest.TestCase):
    def frame(self,first=1):
        ds=[]
        for n,((vs,ps),target) in enumerate(zip(BLOOM_PAIRS,('2','3','2','1'))):
            ds.append(dict(index=first+n,vs=vs,ps=ps,draw_result={'result':'00000000'},
                           targets=[dict(role='rt0',identity=target,width='64',height='64')],states={},
                           texture=[dict(identity='10')]))
        copy=dict(op='stretch_rect',seq='1',after_draw=str(first-1),result='00000000',details=[
            dict(event='stretch_rect',source_rect_null='1',dest_rect_null='1',filter='2'),
            dict(event='surface',role='stretch_source',identity='1'),
            dict(event='surface',role='stretch_dest',identity='9',container='10')])
        clear=dict(op='clear',seq='6',after_draw=str(first+3),result='00000000',details=[
            dict(event='clear',flags='2',z='1',rect_count='0'),
            dict(event='surface',role='clear_depth',identity='7')])
        return dict(complete=True,draw_count_matches=True,event_sequence_contiguous=True,
                    present_result='00000000',draws=ds,events=[copy,clear])
    def test_shifted_indices_use_resource_flow(self):
        f=inspect_frame(self.frame(first=57));c=f['bloom_chains'][0]
        self.assertEqual(c['draw_indices'],[57,58,59,60])
        self.assertEqual(c['next_depth_clear']['depth']['identity'],'7')
    def test_incomplete_and_failed_evidence(self):
        for key in ('complete','draw_count_matches','event_sequence_contiguous'):
            f=self.frame();f[key]=False;self.assertFalse(inspect_frame(f)['valid'])
        f=self.frame();f['draws'][2]['draw_result']['result']='8876086c'
        self.assertFalse(inspect_frame(f)['valid'])
    def test_copy_parent_must_feed_first_bloom_draw(self):
        f=self.frame();f['draws'][0]['texture']=[dict(identity='11')]
        self.assertEqual(inspect_frame(f)['bloom_chains'],[])
    def test_target_order_and_shader_pair_required(self):
        f=self.frame();f['draws'][2]['targets'][0]['identity']='4'
        self.assertEqual(inspect_frame(f)['bloom_chains'],[])
        f=self.frame();f['draws'][1]['vs']='unknown'
        self.assertEqual(inspect_frame(f)['bloom_chains'],[])
    def test_copy_ambiguity_and_failed_copy_rejected(self):
        f=self.frame();f['events'].append(deepcopy(f['events'][0]))
        self.assertEqual(inspect_frame(f)['bloom_chains'],[])
        f=self.frame();f['events'][0]['result']='8876086c'
        self.assertEqual(inspect_frame(f)['bloom_chains'],[])

if __name__=='__main__':unittest.main()
