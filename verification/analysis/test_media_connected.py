"""Check the connected evidence rejects missing pixels, stale identities and fake completion."""
import copy
from fractions import Fraction
from pathlib import Path
import tempfile
import unittest
import media_connected_evidence as e

class ConnectedEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='x3-connected-evidence-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.pixels = [bytes([i,2*i,3*i,255])*512*512 for i in range(1,6)]
        hashes = [e.rgb_digest(x) for x in self.pixels]
        self.oracles = {'A1':{hashes[0]:(0,400000)},
                        'B1':{hashes[1]:(100000000,100400000),hashes[2]:(100400000,100800000)},
                        'A2':{hashes[3]:(19393200000,19393600000),hashes[4]:(19395200000,19395600000)}}
        self.rows = {
            'CXR_HEADER':[dict(kind=e.KIND,frequency='1000',main_thread='1',source='2',workers='2',copy='native',clock='production',engine='authored_handler_frames')],
            'CXR_RESULT':[dict(checks='100',failures='0',captures='9',assigned='0',draining='0',leases='0',B_after_cancel='1',A2_callbacks='1',B_callbacks='1',max_pass_qpc='2')],
            'CXR_SURFACE':[dict(index=str(i),key=str(100+i),device='10',width='512',height='512',format='21',pool='2') for i in range(2)],
            'CXR_RECORD':[dict(name='1',lifetime='1',record='20',shell='40',session_slot='0',session_generation='1',key_generation='1'),
                          dict(name='2',lifetime='1',record='30',shell='50',session_slot='1',session_generation='1',key_generation='2'),
                          dict(name='1',lifetime='2',record='20',shell='40',session_slot='0',session_generation='2',key_generation='3')],
            'CXR_PLAY':[dict(name='1',lifetime='1',operation='1',epoch='1',start='0',end='-1',qpc='900'),
                        dict(name='2',lifetime='1',operation='2',epoch='1',start='10000',end='10359',qpc='1900'),
                        dict(name='1',lifetime='2',operation='3',epoch='1',start='1939320',end='-1',qpc='3500')],
            'CXR_EVENT':[dict(name='overlap_cancel',record='2',qpc='2100',a='1'),dict(name='reused',qpc='3000'),dict(name='draining_refusal',qpc='2200'),dict(name='rate',record='2',a='10000'),dict(name='vacant',qpc='2900',a='1',b='0')],
            'CXR_CALLBACK':[dict(name='1',lifetime='1',operation='1',epoch='1',status='1',count='1',reason='retired',qpc='2100'),
                            dict(name='2',lifetime='1',operation='2',epoch='1',status='1',count='1',reason='endpoint',qpc='5800'),
                            dict(name='1',lifetime='2',operation='3',epoch='1',status='1',count='1',reason='endpoint',qpc='4300')],
        }
        cases = [('1','1','1','1','selected',1000,0),('2','1','1','2','selected',2101,1),
                 ('2','1','2','2','selected',2502,2),('1','2','1','3','selected',4001,3),
                 ('1','2','2','3','selected',4201,4),('2','1','2','2','terminal',6000,2),
                 ('1','2','2','3','terminal',6000,4),('2','1','2','2','hold',6400,2),
                 ('1','2','2','3','hold',6400,4)]
        captures=[]
        for i,(name,lifetime,sequence,operation,reason,qpc,pixels) in enumerate(cases):
            captures.append(dict(index=str(i),name=name,lifetime=lifetime,sequence=sequence,operation=operation,epoch='1',reason=reason,qpc=str(qpc),pitch='2048',lock_hr='0',unlock_hr='0',schedule=str([1000,2100,2501,4000,4200,5701,4240,5701,4240][i]),position=str([0,10000,10040,1939320,1939520,10360,1939560,10360,1939560][i])))
            (self.directory/f'capture-{i:02d}.bgra').write_bytes(self.pixels[pixels])
        self.rows['CXR_CAPTURE']=captures
        self.rows['CXR_STARTUP']=[dict(requested='100',begin='110',end='120',ready='130')]
        self.rows['CXR_CONTINUITY']=[dict(phase=phase,qpc='2100',operation='2',epoch='1',position='10000',rate=str(10000*2748779),generation='1',revision='1',assigned='2',draining=draining)
                                    for phase,draining in [('before_cancel','0'),('after_cancel','1'),('after_refusal','1')]]
        clocks=[]
        for name,lifetime,operation,reason,schedule,position in [('1','1','1','selected',1000,0),('2','1','2','selected',2100,10000),
                    ('2','1','2','selected',2501,10040),('1','2','3','selected',4000,1939320),('1','2','3','selected',4200,1939520),
                    ('1','2','3','terminal',4240,1939560),('2','1','2','terminal',5701,10360)]:
            terminal=reason=='terminal'
            clocks.append(dict(name=name,lifetime=lifetime,operation=operation,epoch='1',reason=reason,
                               schedule=str(schedule),before=str(schedule),after=str(schedule),position=str(position),
                               c0=str(schedule),c1=str(schedule),c2=str(schedule),c3=str(schedule if terminal else 0),
                               reads='4' if terminal else '3',rate=str(10000*2748779 if name=='2' else 1<<38),generation='2' if terminal else '1',revision='2' if terminal else '1'))
        self.rows['CXR_CLOCK']=clocks
    def valid(self,rows=None):
        return e.validate(self.rows if rows is None else rows,self.directory,self.oracles)
    def test_connected_content_and_order_acceptance(self):
        proof=self.valid()
        self.assertEqual((proof['captures'],proof['callbacks']),(9,3))
        self.assertTrue(proof['actual_native_RGB'])
    def test_rejects_scalar_success_without_real_pixels(self):
        (self.directory/'capture-01.bgra').unlink()
        with self.assertRaisesRegex(ValueError,'capture missing'):self.valid()
    def test_rejects_wrong_real_destination_bytes(self):
        (self.directory/'capture-01.bgra').write_bytes(self.pixels[0])
        with self.assertRaisesRegex(ValueError,'native RGB'):self.valid()
    def test_rejects_duplicate_and_missing_completion(self):
        for callbacks in (self.rows['CXR_CALLBACK'][:1],self.rows['CXR_CALLBACK']*2):
            with self.subTest(count=len(callbacks)):
                rows=copy.deepcopy(self.rows);rows['CXR_CALLBACK']=callbacks
                with self.assertRaisesRegex(ValueError,'callback duplicated'):self.valid(rows)
    def test_rejects_identity_and_overlap_failures(self):
        mutations=[('CXR_RECORD',2,'session_generation','1'),('CXR_RECORD',2,'key_generation','1'),
                   ('CXR_CAPTURE',4,'epoch','2'),('CXR_CALLBACK',0,'operation','99'),
                   ('CXR_EVENT',0,'a','2'),('CXR_EVENT',1,'qpc','1000'),
                   ('CXR_EVENT',3,'a','100000'),('CXR_RESULT',0,'draining','1'),
                   ('CXR_HEADER',0,'copy','synthetic')]
        for group,index,key,value in mutations:
            with self.subTest(group=group,key=key):
                rows=copy.deepcopy(self.rows);rows[group][index][key]=value
                with self.assertRaises(ValueError):self.valid(rows)
    def test_rejects_last_frame_loss_and_early_callback(self):
        (self.directory/'capture-04.bgra').write_bytes(self.pixels[3])
        with self.assertRaises(ValueError):self.valid()
        (self.directory/'capture-04.bgra').write_bytes(self.pixels[4])
        rows=copy.deepcopy(self.rows);rows['CXR_CALLBACK'][2]['qpc']='100'
        with self.assertRaisesRegex(ValueError,'callback precedes'):self.valid(rows)
    def test_rejects_hold_change_and_short_hold(self):
        (self.directory/'capture-08.bgra').write_bytes(self.pixels[3])
        with self.assertRaisesRegex(ValueError,'hold differs'):self.valid()
        (self.directory/'capture-08.bgra').write_bytes(self.pixels[4])
        rows=copy.deepcopy(self.rows);rows['CXR_CAPTURE'][8]['qpc']='6001'
        with self.assertRaisesRegex(ValueError,'hold interval'):self.valid(rows)
    def test_rejects_early_deadlines_and_changed_B_clock(self):
        mutations=[('CXR_PLAY',1,'qpc','999999'),('CXR_CALLBACK',1,'qpc','2502'),
                   ('CXR_CALLBACK',2,'qpc','4201'),('CXR_CLOCK',5,'position','1939521'),
                   ('CXR_CLOCK',6,'position','10359'),('CXR_CONTINUITY',1,'revision','2'),
                   ('CXR_CONTINUITY',2,'generation','2'),('CXR_CLOCK',1,'reads','4')]
        for group,index,key,value in mutations:
            with self.subTest(group=group,key=key):
                rows=copy.deepcopy(self.rows);rows[group][index][key]=value
                with self.assertRaises(ValueError):self.valid(rows)

    def test_rejects_preparation_time_in_target_aligned_first_picture(self):
        rows=copy.deepcopy(self.rows)
        for row in rows['CXR_CLOCK']+rows['CXR_CAPTURE']:
            if row['name']=='2':row['position']=str(int(row['position'])+1)
        for row in rows['CXR_CONTINUITY']:row['position']=str(int(row['position'])+1)
        with self.assertRaisesRegex(ValueError,'anchor at requested start'):self.valid(rows)

    def test_rejects_unpaired_unknown_and_duplicate_clocks(self):
        for change in ('missing','unknown','duplicate'):
            with self.subTest(change=change):
                rows=copy.deepcopy(self.rows)
                if change=='missing':rows['CXR_CLOCK'].pop(4)
                elif change=='unknown':rows['CXR_CLOCK'][4]['name']='99'
                else:rows['CXR_CLOCK'].append(copy.deepcopy(rows['CXR_CLOCK'][4]))
                with self.assertRaises(ValueError):self.valid(rows)

    def test_rejects_uniformly_forged_continuity_and_later_generation(self):
        for key in ('operation','epoch','rate','generation','revision','position'):
            with self.subTest(key=key):
                rows=copy.deepcopy(self.rows)
                for row in rows['CXR_CONTINUITY']:row[key]=str(int(row[key])+1)
                with self.assertRaises(ValueError):self.valid(rows)
        for key in ('generation','revision'):
            rows=copy.deepcopy(self.rows);rows['CXR_CLOCK'][2][key]='99'
            with self.assertRaises(ValueError):self.valid(rows)

    def test_selected_intervals_share_the_same_anchor(self):
        # Both intervals separately overlap the public integer positions, but
        # their schedule times require incompatible anchors within that 1 ms.
        first,last=tuple(self.oracles['A2'])
        self.oracles['A2'][first]=(19393200000,19393200001)
        self.oracles['A2'][last]=(19395209000,19395600000)
        with self.assertRaisesRegex(ValueError,'inconsistent real QPC'):self.valid()

    def test_two_round_position_preimage_and_endpoint_openness(self):
        exact=Fraction(1939560)-Fraction(1,10**10)
        public=int(float(exact/1000)*1000)
        self.assertEqual(public,1939560)
        self.assertLess(exact,public)
        lower,upper=e.legacy_position_preimage(public)
        self.assertLessEqual(lower,exact);self.assertLess(exact,upper)
        interval=e.AnchorInterval(10)
        interval.intersect(Fraction(10),Fraction(10),True,True)
        with self.assertRaises(ValueError):interval.intersect(Fraction(10),Fraction(11),False,True)

    def test_parser_refuses_failure_and_duplicate_fields(self):
        for line in ('CXR_FAIL label=actual_copy','CXR_TIMEOUT reason=provider','CXR_RESULT failures=0 failures=1'):
            with self.assertRaises(ValueError):e.parse(line)
        self.assertEqual(e.parse('msync: enabled\nCXR_EVENT name=ready qpc=2'),{'CXR_EVENT':[{'name':'ready','qpc':'2'}]})
if __name__=='__main__':unittest.main()
