"""Synthetic light-count/layout/failure tests; no extracted shader assets."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from analyze_lights import decode, analyze

class LightTests(unittest.TestCase):
    def setUp(self):
        self.metadata={'vs_x':[
            dict(name='g_LightPoint',register_set=2,register=4,count=24,parameter_class=5,elements=8,
                 members=[dict(name=n,parameter_class=1,parameter_type=3,rows=1,columns=w,elements=1,struct_members=0)
                          for n,w in [('pos',3),('color',3),('atten',4)]]),
            dict(name='g_nNumLightPoint',register_set=1,register=0,count=1,parameter_class=0,
                 parameter_type=2,rows=1,columns=1,elements=1)]}
        self.draw=dict(vs='x',draw_result={'result':'00000000'},constant_status={'vs':{
            'i':dict(result='00000000',count='16',encoding='full'),
            'f':dict(result='00000000',count='256',encoding='sparse_zero')}},
            constants={'vs':{'i':{'0':[1,0,0,0]},'f':{'4':'3f800000,40000000,40400000,00000000'}}})
    def test_active_layout_and_sparse_zero(self):
        r=decode(self.draw,self.metadata,'vs')
        self.assertEqual(r['lights'],[dict(pos=[1,2,3],color=[0,0,0],atten=[0,0,0,0])])
    def test_zero_count_ignores_stale_array(self):
        self.draw['constants']['vs']['i']['0'][0]=0
        self.draw['constant_status']['vs']['f']['result']='8876086c'
        self.assertEqual(decode(self.draw,self.metadata,'vs')['lights'],[])
    def test_missing_count_is_not_zero(self):
        del self.draw['constants']['vs']['i']['0']
        with self.assertRaisesRegex(ValueError,'count missing'): decode(self.draw,self.metadata,'vs')
    def test_invalid_count_and_layout(self):
        for count in (-1,9):
            self.draw['constants']['vs']['i']['0'][0]=count
            with self.assertRaisesRegex(ValueError,'capacity'): decode(self.draw,self.metadata,'vs')
        self.metadata['vs_x'][0]['members'][0]['columns']=4
        with self.assertRaisesRegex(ValueError,'layout'): decode(self.draw,self.metadata,'vs')
    def test_query_failure_and_coverage(self):
        for status in (dict(result='8876086c',count='256',encoding='sparse_zero'),
                       dict(result='00000000',count='6',encoding='sparse_zero')):
            self.draw['constant_status']['vs']['f']=status
            with self.assertRaisesRegex(ValueError,'query unavailable'): decode(self.draw,self.metadata,'vs')
    def test_failed_draw_and_nonfinite(self):
        self.draw['draw_result']['result']='8876086c'
        with self.assertRaisesRegex(ValueError,'draw did not succeed'): decode(self.draw,self.metadata,'vs')
        self.draw['draw_result']['result']='00000000'
        self.draw['constants']['vs']['f']['4']='7f800000,0,0,0'
        with self.assertRaisesRegex(ValueError,'nonfinite'): decode(self.draw,self.metadata,'vs')
    def test_bad_frame_excludes_evidence(self):
        for trailer in ('','frame_end device=1 frame=1 draws=2 present=00000000'):
            r=analyze('frame_begin device=1 frame=1\ndraw device=1 frame=1 index=1 kind=up topology=4 primitives=1 vs=x ps=0\n'+trailer,self.metadata)
            self.assertFalse(r['frames']['1:1']['complete'])
            self.assertEqual(r['frames']['1:1']['observations'],[])
    def test_impossible_integer_namespace(self):
        self.metadata['vs_x'][1]['register']=16
        self.draw['constant_status']['vs']['i']['count']='17'
        self.draw['constants']['vs']['i']['16']=[0,0,0,0]
        with self.assertRaisesRegex(ValueError,'query unavailable'): decode(self.draw,self.metadata,'vs')
    def test_impossible_float_namespace_and_word(self):
        self.draw['constant_status']['vs']['f']['count']='257'
        with self.assertRaisesRegex(ValueError,'query unavailable'): decode(self.draw,self.metadata,'vs')
        self.draw['constant_status']['vs']['f']['count']='256'
        self.draw['constants']['vs']['f']['4']='100000000,0,0,0'
        with self.assertRaisesRegex(ValueError,'uint32'): decode(self.draw,self.metadata,'vs')

if __name__=='__main__': unittest.main()
