"""Bounded host witnesses; synthetic test timestamps never drive native playback."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import media_worker_clock_evidence as e

ROOT=Path(__file__).resolve().parents[2]


def event(name,label='',at=1,owner='main',instance=0,session=1,**values):
    r=dict(name=name,label=label,begin=at,end=at,owner=owner,instance=instance,session=session,thread=1,index=0,hr='00000000',**dict.fromkeys('abcdef',0));r.update(values)
    return {k:str(v) for k,v in r.items()}


class ClockReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();path=Path(cls.temp.name)
        source=(ROOT/'verification/probe/media_owned_clock/host.cpp').read_bytes()
        (path/'host.cpp').write_bytes(source);cls.exe=path/'host'
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/media/owned_clock'),str(path/'host.cpp'),'-o',str(cls.exe)],check=True,capture_output=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def run_clock(self,commands,ident=0):
        lines=[]
        for name,qpc,args in commands:
            command={'submit':'frame','restart':'loop'}.get(name,name)
            fields=list(args)+([] if name=='submit' else [qpc]);lines.append(command+' '+' '.join(map(str,fields)))
        output=subprocess.check_output([str(self.exe)],input=('10000000\n'+'\n'.join(lines)+'\n').encode())
        rows=[]
        for i,((name,qpc,args),raw) in enumerate(zip(commands,output.splitlines())):
            row=json.loads(raw);row['seconds']=row.pop('seconds_bits');row['scaled']=row.pop('scaled_bits');row['frame_generation']=row['generation'] if row['selected'] else 0
            row.update(name=name,qpc=qpc,instance=ident,index=i,tick=i,**dict(zip('abcd',list(args)+[0]*4)))
            rows.append({k:str(v) for k,v in row.items()})
        return rows

    def ordinary(self):
        return self.run_clock([('begin',10,(101,0,280,0)),('update',100,()),('submit',110,(1,1,0,400000)),('update',120,()),('pause',150,()),('submit',160,(1,2,400000,800000)),('submit',170,(1,3,800000,1200000)),('submit',180,(1,4,1200000,1600000)),('update',400,()),('seek',500,(22000000,2480)),('submit',600,(2,5,22000000,22400000)),('update',650,()),('rate',700,(50000,)),('resume',800,()),('update',900,()),('rate',801000,(200000,)),('update',1801000,()),('stop',1801100,()),('seek',1801200,(0,280)),('update',1900000,())])

    def test_fraction_replay_real_cpp_pause_seek_rate_stop(self):
        rows=self.ordinary();selected=e.replay_clock({'MC_CLOCK':rows},10000000)
        self.assertEqual([r['token'] for r in selected],['1','5'])
        self.assertEqual(rows[-1]['armed'],'0');self.assertEqual(rows[-1]['intent'],'0')

    def test_two_independent_clock_replays(self):
        a=self.ordinary();b=self.run_clock([('begin',15,(202,100000000,10360,1)),('rate',20,(50000,)),('submit',115,(1,1,100000000,100400000)),('update',125,()),('update',8000125,()),('restart',8000130,()),('submit',8000140,(2,2,100000000,100400000)),('update',8000150,()),('update',16000150,()),('restart',16000160,()),('submit',16000170,(3,3,100000000,100400000)),('update',16000180,()),('stop',16000190,())],1)
        rows=sorted(a+b,key=lambda r:int(r['qpc']))
        for i,r in enumerate(rows):r['index']=str(i)
        self.assertEqual(len(e.replay_clock({'MC_CLOCK':rows},10000000)),5)

    def test_late_and_superseded_match_cpp(self):
        rows=self.run_clock([('begin',1,(1,0,0,0)),('submit',2,(1,1,0,400000)),('update',3,()),('submit',4,(1,2,400000,800000)),('submit',5,(1,3,700000,1200000)),('submit',6,(1,4,750000,1300000)),('update',900003,())])
        e.replay_clock({'MC_CLOCK':rows},10000000);self.assertEqual((rows[-1]['late'],rows[-1]['superseded'],rows[-1]['token']),('1','1','4'))

    def test_replay_rejects_mutations(self):
        for field,value in [('numerator','ff'),('rate','1'),('armed','0'),('token','999'),('consumed','0'),('qpc','1')]:
            with self.subTest(field=field):
                rows=self.ordinary();rows[3][field]=value
                with self.assertRaises(ValueError):e.replay_clock({'MC_CLOCK':rows},10000000)
