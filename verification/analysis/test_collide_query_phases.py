"""Host qualification only. Runtime ABI/clock benchmarks are separate Windows fixtures."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_collide_query_phases as site
import run_collide_query_phases as runner
from verification.analysis import test_collide_memo as launch_tests
ROOT = Path(__file__).resolve().parents[2]

class QueryPhases(unittest.TestCase):
    def test_accumulator_faults_and_same_sample_quantiles(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'host'
            subprocess.run([shutil.which('clang++') or 'c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/collide_query_phases_host.cpp'),'-o',str(binary)],check=True,capture_output=True,text=True)
            run=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn('failures=0',run.stdout)
            self.assertIn('iterations=1000000',run.stdout)

    def test_site_and_corrupted_byte_refusal(self):
        exe=site.memo.sites.DEFAULT_EXE
        if not exe.exists():self.skipTest('installed EXE unavailable')
        data=exe.read_bytes();decoded=site.memo.decode(exe);source=site.SOURCE.read_text()
        report=site.inspect(data,decoded,source)
        self.assertEqual(report['result'],'PASS',report)
        self.assertEqual(site.inspect(data,decoded,source.replace('0x33,0xc0','0x33,0xc1'))['result'],'FAIL')
        self.assertEqual(site.inspect(data,decoded,source.replace('site_va=0x004e2956','site_va=0x004e2957'))['result'],'FAIL')

    def test_runtime_report_rejects_partial_or_invalid_windows(self):
        windows='\n'.join(f'collide_query_phases frames=300 valid_frames=300 invalid_frames=0 invalid=0 queries=300 descents=300 misses=300 verifies=0 triangles=0 contacts=0 visits=300 first_frame={first} frame={last}' for first,last in [(1,300),(301,600)])
        text=windows+'\nCOLLIDE QUERY BENCH off_ns=200.0 on_ns=800.0 overhead_ns=600.0 iterations=70000\nCOLLIDE QUERY CPU checks=142 failures=0 queries=675 differences=0\n'
        digest='a'*64;sources={'fixture.cpp':'b'*64}
        provenance={'build':{'binary_sha256':digest,'source_sha256':sources,'compiler':'test compiler','commands':[['test compiler','fixture.cpp']]},
                    'exe_sha256_before':digest,'exe_sha256_after':digest,'source_sha256_before_run':sources,
                    'source_sha256_after_run':sources,'stdout_sha256':'c'*64}
        result={**provenance,**runner.parse(text),'exit_status':0}
        self.assertTrue(runner.accepted(result))
        for before,after in [('valid_frames=300','valid_frames=299'),('checks=142','checks=141'),('differences=0','differences=1'),('first_frame=301','first_frame=302'),('triangles=0','triangles=1')]:
            self.assertFalse(runner.accepted({**provenance,**runner.parse(text.replace(before,after)),'exit_status':0}))
        self.assertFalse(runner.accepted({**provenance,**runner.parse(''),'exit_status':0}))
        for key,value in [('exe_sha256_after','d'*64),('source_sha256_after_run',{'fixture.cpp':'d'*64}),('stdout_sha256','')]:
            self.assertFalse(runner.accepted({**result,key:value}))
        self.assertFalse(runner.accepted({**runner.parse(text),'exit_status':0}))

    def test_cli_implies_memo_and_clears_inherited(self):
        # Part of --debug since the logging tiers (2026-09-26): the DLL reads X3M_COLLIDE_QUERY_PHASES or X3M_DEBUG and runs
        # only with the memo (collide_memo.cpp initialize -> collide_query_phases::initialize(true, ...)); the option is gone.
        helper=launch_tests.MemoLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            rc,_,err=helper.launch(directory,'--collide-query-phases');self.assertEqual(rc,2);self.assertIn('unrecognized arguments',err)
            rc,out,err=helper.launch(directory,'--debug','--collide-memo');self.assertEqual(rc,0,err)
            env=json.loads(out)['env'];self.assertEqual((env['X3M_DEBUG'],env['X3M_COLLIDE_MEMO']),('1','1'));self.assertNotIn('X3M_COLLIDE_QUERY_PHASES',env)
            rc,out,err=helper.launch(directory,inherited={'X3M_COLLIDE_QUERY_PHASES':'1'});self.assertEqual(rc,0,err);self.assertNotIn('X3M_COLLIDE_QUERY_PHASES',json.loads(out)['env'])

if __name__=='__main__':unittest.main()
