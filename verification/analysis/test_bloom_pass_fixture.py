"""Host controls for acceptance parser and independent GPU-image oracle.

These tests do not qualify GPU/state behavior. They prevent stale/incomplete
logs, missing image bytes, alpha corruption and gross no-op RGB from passing.
"""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
import run_bloom_pass as fixture

class FixtureAcceptance(unittest.TestCase):
    def setUp(self):
        self.cases=fixture.make_cases()
        self.lines=['FILL label=neutral requested=6b193957 mismatches=0 first=4294967295 observed=6b193957']
        self.lines += [f'CONTROL test={i} pass=1' for i in range(16)]
        self.lines += [f"CASE index={i} width={c['width']} height={c['height']} checks={16 if i==0 else 1} pass=1"
                       for i,c in enumerate(self.cases)]
        self.lines += ['NPATCH accepted=1 hr=00000000','ADAPTIVE accepted=1 hr=00000000','NPATCH_DRAWS checked=10 pass=1',
                       'RESET_CASE index=23 width=9 height=7 checks=1 pass=1',
                       'RESULT PASS cases=24 controls=16 checks=40 reset=1 reset_cases=1']

    def test_terminal_and_complete_records(self):
        fixture.validate_log('\n'.join(self.lines),self.cases,0)
        for lines,code in [(self.lines,1),(self.lines[:-1],0),(self.lines+self.lines[-1:],0),
                           (self.lines+['unexpected after terminal'],0),(self.lines[1:],0),
                           ([self.lines[0]]+self.lines,0),
                           ([x for x in self.lines if not x.startswith('CASE index=23 ')],0),
                           ([x for x in self.lines if not x.startswith('RESET_CASE ')],0),
                           ([x for x in self.lines if not x.startswith('NPATCH_DRAWS ')],0),
                           (['API_FAIL getter']+self.lines,0),
                           (['CONTROL nonsense']+self.lines,0)]:
            with self.subTest(lines=len(lines),code=code),self.assertRaises(ValueError):
                fixture.validate_log('\n'.join(lines),self.cases,code)

    def test_corpus_has_six_extract_variants_and_sharpen_strength_controls(self):
        self.assertEqual(len(self.cases),24)
        self.assertEqual({(c['mode'],c['width']%2) for c in self.cases},
                         {(m,p) for m in fixture.ref.DECODE_MODES for p in (0,1)})
        for c in self.cases:
            self.assertEqual(len(fixture.expected(c)),c['height'])
        for i in range(0,24,4):
            a=fixture.expected(self.cases[i]);b=fixture.expected(self.cases[i+2])
            self.assertGreater(max(abs(x-y) for ar,br in zip(a,b) for ap,bp in zip(ar,br) for x,y in zip(ap,bp)),.005)

    def test_structured_cases_detect_skipped_sharpen(self):
        for i in range(0,len(self.cases),2):
            if self.cases[i]['kind']!='structured': continue
            a=fixture.expected(self.cases[i]);b=fixture.expected(self.cases[i+1])
            difference=max(abs(fixture.oracle.code8(x)-fixture.oracle.code8(y))
                           for ar,br in zip(a,b) for ap,bp in zip(ar,br) for x,y in zip(ap,bp))
            self.assertGreater(difference,fixture.MAX_CODE_ERROR)

    def test_readback_strictness_and_independent_oracle(self):
        c=self.cases[0]
        ideal=bytes(v for row in fixture.expected(c) for p in row
                    for v in (*reversed([fixture.oracle.code8(x) for x in p]),0x6b))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'image.bgra8';path.write_bytes(ideal)
            baseline=Path(directory)/'baseline.bgra8';baseline.write_bytes(ideal)
            self.assertTrue(fixture.compare(c,path,baseline)['passed'])
            path.write_bytes(struct.pack('<I',0x6b193957)*(c['width']*c['height']))
            self.assertFalse(fixture.compare(c,path,baseline)['passed'])
            bad=bytearray(ideal);bad[3]=0;path.write_bytes(bad)
            baseline.write_bytes(bad)
            self.assertTrue(fixture.compare(c,path,baseline)['passed'])
            baseline.write_bytes(ideal)
            self.assertFalse(fixture.compare(c,path,baseline)['passed'])
            path.write_bytes(ideal[:-1])
            with self.assertRaises(ValueError):fixture.compare(c,path,baseline)

    def test_input_bundle_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            a,b=Path(directory)/'a',Path(directory)/'b'
            fixture.write_cases(self.cases,a);fixture.write_cases(fixture.make_cases(),b)
            self.assertEqual(a.read_bytes(),b.read_bytes())
            self.assertEqual(a.read_bytes()[:12],b'X3BP0001'+struct.pack('<I',24))

if __name__=='__main__':unittest.main()
