"""Focused selected-mode report/contract witnesses; no Wine or source builds."""
import copy
import math
import struct
import unittest
from pathlib import Path
import shutil
import subprocess
import tempfile
from verification.analysis.test_capture_bloom_lifetime import extract_function
from unittest.mock import patch
import run_linear_material as runner


def report():
    cases=runner.alpha_cutout_cases()
    rows=['CAPS mrt=4 vs_slots=32768 ps_slots=32768']
    for pair in (0,21):
        for stage,shader in zip(('vs','ps'),runner.PAIRS[pair]):
            for mode in range(3):
                for depth in ((0,) if mode==0 else (0,1)):
                    gains='1_1_1' if mode==2 else '0_0_0'
                    rows.append(f'CREATE stage={stage} key={shader}_{mode}_{depth}_{gains} instructions=100 words=500 completed_ms=0.25')
    checks=0
    for c in cases:
        pattern=[0.,1/255,1/255,1/255,1/512,1/128,.5,1.]*2
        if c['coefficients'][0]==0: pattern=[0.]*16
        row=''.join('1' if x>=1/255 else '0' for x in pattern)
        passed=row.count('1')*16
        edges=sum(a!=b for a,b in zip(row,row[1:]))*16
        bits=','.join(struct.pack('>f',x).hex() for x in pattern)
        numerical=copy.deepcopy(c);numerical['fp16']=0
        rgb=runner.expected(numerical).encoded_rgba[:3]
        rows.extend((f"CUTOUT_ALPHA id={c['id']} bits={bits}",
                     f"CUTOUT_RGB id={c['id']} rgb="+','.join(map(str,rgb)),
                     f"CUTOUT_COVERAGE id={c['id']} passed={passed} rejected={256-passed} edges={edges} row={row}"))
        rows.extend(f"CUTOUT_TWIN id={c['id']} scene={scene} mode={mode} pixels=256 bad=0" for scene in range(6) for mode in range(3))
        checks+=51714+1280*c['depth']+18*passed
    rows.extend((f'CUTOUT_RESULT cases=48 scenes=288 twins=864 checks={checks}', 'RESULT PASS cases=48'))
    return '\n'.join(rows)


class CutoutFixture(unittest.TestCase):
    def test_case_inventory_and_abi(self):
        cases=runner.alpha_cutout_cases()
        self.assertEqual(len(cases),48)
        self.assertEqual({runner.PAIRS[c['pair']] for c in cases},
                         {('53a0a641107ed76c','63f96eba9eea7880'),('4944d81dfe531b37','5e0a10fe752b6140')})
        self.assertEqual({c['glow'] for c in cases},{0.,1.,.375})
        self.assertEqual({c['coefficients'][0] for c in cases},{0.,.625,1.})
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases},
                         {(p,d,r) for p in (0,21) for d in (0,1) for r in (0,1)})
        binary=runner.binary_cases(cases)
        self.assertEqual(len(binary),4+48*240)
        self.assertEqual(struct.unpack_from('<I',binary)[0],48)
        for i,c in enumerate(cases):
            self.assertEqual(struct.unpack_from('<4f',binary,4+i*240+36+47*4),tuple(c['coefficients']))

    def test_report_uses_existing_rgb_oracle(self):
        result=runner.validate_cutout_report(report())
        self.assertEqual((result['cases'],result['scenes'],result['twins']),(48,288,864))
        self.assertEqual(len(result['native_threshold_rows']),8)
        self.assertEqual(result['shader_creations'],20)
        self.assertEqual(result['alpha_samples'],12288)
        self.assertEqual(result['max_tolerance_fraction'],0)

    def test_report_rejects_missing_or_corrupt_evidence(self):
        text=report()
        mutations=(text.replace('RESULT PASS cases=48',''),
                   text.replace('bad=0','bad=1',1),
                   text.replace('CUTOUT_TWIN id=0 scene=0 mode=0 pixels=256 bad=0\n','',1),
                   text.replace('CUTOUT_ALPHA id=0','CUTOUT_ALPHA id=1',1),
                   text.replace('bits=00000000','bits=7fc00000',1),
                   text.replace('bits=00000000','bits=3f800000',1),
                   text.replace('rejected=64','rejected=65',1),
                   text.replace('pixels=256','pixels=255',1),
                   text.replace('rgb=','rgb=nan,',1),
                   text.replace('checks=','checks=9',1),
                   text+'\nRESULT PASS cases=48',
                   text.replace('CREATE stage=vs','CREATE stage=ps',1),
                   text.replace('vs_slots=32768','vs_slots=1'))
        for bad in mutations:
            self.assertNotEqual(bad,text)
            with self.subTest(bad=bad[:100]),self.assertRaises((AssertionError,ValueError)):
                runner.validate_cutout_report(bad)

    def test_actual_pixel_reducer_rejects_mask_and_mrt_corruption(self):
        source=(runner.ROOT/'verification/probe/linear_alpha_test_fixture_inc.h').read_text()
        methods='\n'.join(extract_function(source,token) for token in
                          ('  static bool same(', '  void pixel_twin('))
        code=r'''
#include <cstring>
#include <stdexcept>
#include <initializer_list>
struct Pixel {float f[4];};
struct Probe {
 unsigned checks=0;
 void check(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
'''+methods+r'''
};
int main() {
 const Pixel poison{{.9375f,.0625f,.8125f,.6875f}},color{{.25f,.5f,.125f,.6875f}},
   motion{{.125f,.25f,.5f,1}},depth{{.5f,0,0,0}};
 for (unsigned mode=0;mode<3;++mode) for (bool writes:{false,true}) {
   const Pixel expected=writes ? color : poison;
   const Pixel wanted_motion=mode && writes ? motion : poison;
   const Pixel wanted_depth=mode && writes ? depth : poison;
   for (unsigned fault=0;fault<8;++fault) {
     Pixel after=expected,native=expected,actual_motion=wanted_motion,actual_depth=wanted_depth;
     if(fault==1)after.f[3]=0; // RT0 mask violated
     if(fault==2)after.f[0]+=1; // coverage/RGB violated
     if(fault==3)actual_motion.f[0]+=1;
     if(fault==4)actual_motion.f[3]=-1;
     if(fault==5)actual_depth.f[0]+=.125f;
     if(fault==6)native.f[1]+=1; // compared only for original/native twins
     if(fault==7)actual_depth.f[3]=123; // R32F has only one lane
     bool rejected=false;
     try {Probe p;p.pixel_twin(mode,writes,after,native,poison,color,actual_motion,
                              wanted_motion,actual_depth,wanted_depth);}
     catch(const std::runtime_error&){rejected=true;}
     const bool must_reject=(fault>=1 && fault<=5) || (fault==6 && mode<2);
     if(rejected!=must_reject)return 1;
   }
 }
}
'''
        compiler=shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-cutout-reducer-') as tmp:
            root=Path(tmp);(root/'test.cpp').write_text(code)
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(root/'test.cpp'),'-o',str(root/'test')],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(root/'test')],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_refuses_unowned_case_and_conflicting_mode(self):
        cases=runner.alpha_cutout_cases();cases[0]['pair']=1
        with self.assertRaises(AssertionError): runner.validate_cutout_report(report(),cases)
        with self.assertRaises(SystemExit),patch('sys.stderr'):
            runner.parse_arguments(['--exe','fixture.exe','--glass-only','--alpha-test-cutout'])
        args=runner.parse_arguments(['--exe','fixture.exe','--alpha-test-cutout'])
        self.assertTrue(args.alpha_test_cutout)
        self.assertFalse(args.glass_only)


if __name__=='__main__': unittest.main()
