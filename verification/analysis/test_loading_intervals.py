"""Frozen recorder core and binary clipping/loss witnesses; no game launch."""
from pathlib import Path
import subprocess
import json
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/analysis'))
from analyze_loading_intervals import HEADER, THREAD, RECORD, analyze, reduce
from snapshot_x3_run import references

class LoadingIntervals(unittest.TestCase):
    def test_concurrent_value_core(self):
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'core'
            build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread',str(ROOT/'verification/probe/loading_intervals_host.cpp'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('failures=0 payload_bytes=25165824',run.stdout)

    def test_launch_option_requires_telemetry_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper=LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code,_,error=helper.launch(directory,'--loading-intervals')
            self.assertEqual(code,2);self.assertIn('requires --telemetry',error)
            code,output,error=helper.launch(directory,'--loading-intervals','--telemetry')
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_LOADING_INTERVALS'],'1')
            code,output,error=helper.launch(directory,inherited={'X3M_LOADING_INTERVALS':'1'})
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_LOADING_INTERVALS'],'0')

    def artifact(self, rings=(), flags=0):
        data=HEADER.pack(b'X3MINT01',1,96,24,16,65536,flags,len(rings),42,1000,1,10,100,3,4,5)
        for index in range(16):
            if index<len(rings):
                rows,completed,lost,fault=rings[index]
                data+=THREAD.pack(42,index+1,len(rows),completed,*lost,fault,0,max((r[1] for r in rows),default=0))
                data+=b''.join(RECORD.pack(*row,0,0) for row in rows)
            else:
                data+=bytes(THREAD.size)
        return data

    def parse(self,data):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'record.bin';p.write_bytes(data);return analyze(p)

    def test_nested_cross_thread_clip_and_tid_reuse(self):
        r=self.parse(self.artifact([([(1,30),(12,20),(40,110)],3,(0,0),0), ([(20,50)],1,(0,0),0)]))
        self.assertEqual([t['union_ticks'] for t in r['threads']],[80,30])
        self.assertEqual(r['any_thread']['union_ticks'],90)
        self.assertEqual([t['generation'] for t in r['threads']],[1,2])
        self.assertTrue(r['complete'])
        self.assertNotIn('main_thread',r)

    def test_gaps_no_calls_and_bounded_witnesses(self):
        self.assertEqual(reduce([],10,100)[0],dict(union_ticks=0,largest_gaps=[[10,100]]))
        result,_=reduce([(0,20),(40,50),(45,60),(80,120)],10,100)
        self.assertEqual(result,dict(union_ticks=50,largest_gaps=[[20,40],[60,80]]))
        self.assertEqual(len(reduce([(n,n+1) for n in range(1,40,2)],0,40)[0]['largest_gaps']),8)
        self.assertTrue(self.parse(self.artifact())['complete'])

    def test_loss_before_target_and_intersecting_loss(self):
        for lost,expected in [((1,9),True),((1,11),False),((90,120),False)]:
            result=self.parse(self.artifact([([(120,130)]*65536,65537,lost,0)]))
            self.assertEqual(result['complete'],expected)
        for flags in (1,2,4,8,16):
            self.assertFalse(self.parse(self.artifact(flags=flags))['complete'])
            self.assertFalse(self.parse(self.artifact([([],0,(0,0),flags)]))['complete'])

    def test_corrupt_records_and_short_file_refused(self):
        good=self.artifact([([(20,30)],1,(0,0),0)])
        for data in (good[:-1],good+b'x',self.artifact([([(30,20)],1,(0,0),0)]),self.artifact([([],65537,(0,0),0)])):
            with self.assertRaises(ValueError):self.parse(data)

    def test_snapshot_reference_is_bounded_and_safe(self):
        wanted,issues=references(['loading_intervals_file file=loading-intervals-4-8.bin written=1 bytes=864\n'])
        self.assertEqual(wanted,{'loading-intervals-4-8.bin':dict(size=864,timed=True)});self.assertFalse(issues)
        for name,size,ok in [('../bad.bin',864,1),('loading-intervals-4-8.bin',25166689,1),('loading-intervals-4-8.bin',864,0)]:
            wanted,issues=references([f'loading_intervals_file file={name} written={ok} bytes={size}\n'])
            self.assertFalse(wanted);self.assertTrue(issues)

if __name__=='__main__':unittest.main()
