"""Host controls for acceptance parser and independent GPU-image oracle.

These tests do not qualify GPU/state behavior. They prevent stale/incomplete
logs, missing image bytes, alpha corruption and gross no-op RGB from passing.
"""
from pathlib import Path
import hashlib
import math
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
        last=self.cases[fixture.RESET_CASE_INDEX]
        self.lines += ['NPATCH accepted=1 hr=00000000','ADAPTIVE accepted=1 hr=00000000','NPATCH_DRAWS checked=10 pass=1',
                       f"RESET_CASE index={fixture.RESET_CASE_INDEX} width={last['width']} height={last['height']} checks=1 pass=1",
                       f'RESULT PASS cases={len(self.cases)} controls=16 checks={len(self.cases)+16} reset=1 reset_cases=1']

    def test_terminal_and_complete_records(self):
        fixture.validate_log('\n'.join(self.lines),self.cases,0)
        for lines,code in [(self.lines,1),(self.lines[:-1],0),(self.lines+self.lines[-1:],0),
                           (self.lines+['unexpected after terminal'],0),(self.lines[1:],0),
                           ([self.lines[0]]+self.lines,0),
                           ([x for x in self.lines if not x.startswith(f'CASE index={len(self.cases)-1} ')],0),
                           ([x.replace(f'RESET_CASE index={fixture.RESET_CASE_INDEX} ',
                                       f'RESET_CASE index={len(self.cases)-1} ') for x in self.lines],0),
                           ([x for x in self.lines if not x.startswith('RESET_CASE ')],0),
                           ([x for x in self.lines if not x.startswith('NPATCH_DRAWS ')],0),
                           (['API_FAIL getter']+self.lines,0),
                           (['CONTROL nonsense']+self.lines,0)]:
            with self.subTest(lines=len(lines),code=code),self.assertRaises(ValueError):
                fixture.validate_log('\n'.join(lines),self.cases,code)

    def test_corpus_has_six_extract_variants_and_sharpen_strength_controls(self):
        legacy=self.cases[:24]
        self.assertEqual(len(legacy),24)
        self.assertEqual({(c['mode'],c['width']%2) for c in legacy},
                         {(m,p) for m in fixture.ref.DECODE_MODES for p in (0,1)})
        for c in legacy:
            self.assertEqual((c['threshold'],c['exposure'],c['authored_glow_gain'],c['highlight_gain']),
                             (0.,1.,0.,.05))
            self.assertEqual(len(fixture.expected(c)),c['height'])
        for i in range(0,24,4):
            a=fixture.expected(self.cases[i]);b=fixture.expected(self.cases[i+2])
            self.assertGreater(max(abs(x-y) for ar,br in zip(a,b) for ap,bp in zip(ar,br) for x,y in zip(ap,bp)),.005)

    def test_legacy_case_inputs_and_expectations_retain_canonical_bytes(self):
        inputs=hashlib.sha256();expected=hashlib.sha256()
        for c in self.cases[:24]:
            inputs.update(struct.pack('<3I2f',c['width'],c['height'],
                                      fixture.ref.DECODE_MODES.index(c['mode']),
                                      c['strength'],c['sharp']))
            for row in c['image']:
                for pixel in row:inputs.update(struct.pack('<4e',*pixel))
            for row in fixture.expected(c):
                for pixel in row:expected.update(struct.pack('<3d',*pixel))
        self.assertEqual(inputs.hexdigest(),
                         '596005b9481f7771c0564f19dcc61b4673640cda317c05aa1e0089e4e2a8f7fe')
        self.assertEqual(expected.hexdigest(),
                         '46c4b26ac1b977efd3c89df5542ae8684ce2e7e9452662c10ce4b37d6835f698')

    def test_authored_cases_cover_masks_evs_decoders_geometry_and_f10_gate(self):
        authored=self.cases[24:36]
        self.assertEqual(len(authored),12)
        self.assertEqual({(c['mode'],c['width']%2) for c in authored},
                         {(m,p) for m in fixture.ref.DECODE_MODES for p in (0,1)})
        self.assertEqual({round(math.log2(c['exposure']),1) for c in authored},{0.,1.5,2.})
        self.assertEqual({c['authored_glow_gain'] for c in authored},{.1,.2})
        self.assertEqual((authored[-1]['kind'],authored[-1]['strength']),('authored',1.))
        for index in range(0,len(authored),2):
            off,on=authored[index:index+2]
            self.assertEqual({off['strength'],on['strength']},{0.,1.})
            alphas=[p[3] for row in off['image'] for p in row]
            self.assertEqual({a for a in alphas if math.isfinite(a)},{0.,.5,1.})
            self.assertEqual(sum(math.isnan(a) for a in alphas),1)
            self.assertEqual(sum(math.isinf(a) and a < 0 for a in alphas),1)
            self.assertEqual(sum(math.isinf(a) and a > 0 for a in alphas),1)
            self.assertTrue(all(math.isfinite(v) for row in off['image'] for p in row for v in p[:3]))
            off_expected=fixture.expected(off);on_expected=fixture.expected(on)
            self.assertTrue(all(math.isfinite(v) for expected in (off_expected,on_expected)
                                for row in expected for p in row for v in p))
            for actual,source in zip((p for row in off_expected for p in row),
                                     (p for row in off['image'] for p in row)):
                base=fixture.oracle.composition(source,(0.,0.,0.),0.,
                                                exposure=off['exposure'],mode=off['mode'])
                self.assertEqual(actual,base[:3])
            difference=max(abs(fixture.oracle.code8(a)-fixture.oracle.code8(b))
                           for ar,br in zip(off_expected,on_expected)
                           for ap,bp in zip(ar,br) for a,b in zip(ap,bp))
            self.assertGreater(difference,fixture.MAX_CODE_ERROR)
            emitter=on['image'][on['height']//2][on['width']//2]
            legacy=fixture.ref.prefilter(emitter,fixture.ref.Params(threshold=1),
                                         exposure=on['exposure'],mode=on['mode'])
            colored=fixture.ref.prefilter(emitter,fixture.ref.Params(
                threshold=1,authored_glow_gain=on['authored_glow_gain'],highlight_gain=.05),
                exposure=on['exposure'],mode=on['mode'])
            self.assertEqual(legacy,(0.,0.,0.))
            self.assertGreater(max(colored),0.)
            params=fixture.ref.Params(threshold=1,authored_glow_gain=on['authored_glow_gain'],
                                      highlight_gain=.05)
            for x,sanitized in ((1,0.),(2,0.),(3,1.)):
                pixel=on['image'][0][x]
                actual=fixture.ref.prefilter(pixel,params,exposure=on['exposure'],mode=on['mode'])
                wanted=fixture.ref.prefilter(pixel[:3]+(sanitized,),params,
                                             exposure=on['exposure'],mode=on['mode'])
                self.assertEqual(actual,wanted)

    def test_post_reset_case_is_pinned_to_the_odd_generic_authored_case(self):
        pinned=self.cases[fixture.RESET_CASE_INDEX]
        self.assertEqual((fixture.RESET_CASE_INDEX,pinned['kind'],pinned['mode'],
                          pinned['width'],pinned['height'],pinned['strength']),
                         (35,'authored','none',9,7,1.))
        self.assertNotEqual(fixture.RESET_CASE_INDEX,len(self.cases)-1)

    def test_source_clamp_cases_pin_live_constants_and_the_clamp_identities(self):
        clamp=self.cases[36:45]
        self.assertEqual(len(clamp),9)
        self.assertEqual(len(self.cases),47)
        for c in clamp:
            self.assertEqual((c['kind'],c['mode'],c['levels'],c['scatter']),('clamp','gamma2.2',5,.65))
            self.assertEqual((c['authored_glow_gain'],c['highlight_gain'],c['threshold'],c['strength']),
                             (.375,.05,1.,1.))
            self.assertEqual(round(math.log2(c['exposure']),1),1.3)
            self.assertEqual(len(fixture.ref.layout(c['width'],c['height'],c['levels'])),5)
            self.assertEqual({p[3] for row in c['image'] for p in row},{c['alpha']})
        # Both extraction lanes: the thresholded highlight term and the
        # alpha-authored glow term.
        self.assertEqual({c['alpha'] for c in clamp},{0.,1.})
        by_label={c['label']:c for c in clamp}
        self.assertEqual(sorted(by_label),
                         ['clamp_authored_hot_none','clamp_authored_hot_one',
                          'clamp_authored_ref_none','clamp_authored_ref_one',
                          'clamp_hot_none','clamp_hot_one','clamp_hot_two',
                          'clamp_ref_none','clamp_ref_one'])
        self.assertEqual([by_label[n]['source_clamp'] for n in
                          ('clamp_ref_none','clamp_ref_one','clamp_hot_none','clamp_hot_one','clamp_hot_two')],
                         [0.,1.,0.,1.,2.])
        params=fixture.ref.Params(levels=5,threshold=1.,strength=1.,scatter=.65,
                                  authored_glow_gain=.375,highlight_gain=.05)
        exposure=by_label['clamp_ref_none']['exposure']
        feeds={name:fixture.ref.bloom(c['image'],params,exposure=exposure,
                                      clamp_max=c['source_clamp'],mode='gamma2.2')
               for name,c in by_label.items()}
        flat=lambda name:[v for row in feeds[name] for p in row for v in p]
        column=(fixture.CLAMP_BAR[0]+fixture.CLAMP_BAR[1])//2
        for prefix in ('clamp_','clamp_authored_'):
            # A code-1 source is at the clamp: the whole oracle image is identical.
            self.assertEqual(fixture.expected(by_label[prefix+'ref_none']),
                             fixture.expected(by_label[prefix+'ref_one']))
            # A code-5 source at clamp 1 feeds exactly the code-1 pyramid.
            self.assertEqual(flat(prefix+'hot_one'),flat(prefix+'ref_none'))
            self.assertGreater(min(a-b for a,b in zip(flat(prefix+'hot_none'),
                                                      flat(prefix+'hot_one')) if a>0),0.)
            # The displayed bar keeps its unbounded HDR code under every clamp.
            native=fixture.expected(by_label[prefix+'hot_none'])
            clamped=fixture.expected(by_label[prefix+'hot_one'])
            reference=fixture.expected(by_label[prefix+'ref_none'])
            self.assertGreater(max(abs(fixture.oracle.code8(a)-fixture.oracle.code8(b))
                                   for ar,br in zip(native,clamped)
                                   for ap,bp in zip(ar,br) for a,b in zip(ap,bp)),fixture.MAX_CODE_ERROR)
            self.assertEqual([fixture.oracle.code8(v) for v in clamped[0][column]],
                             [fixture.oracle.code8(v) for v in native[0][column]])
            self.assertNotEqual([fixture.oracle.code8(v) for v in clamped[0][column]],
                                [fixture.oracle.code8(v) for v in reference[0][column]])
        # Alpha 1 routes through the authored lane, alpha 0 through the
        # thresholded highlight lane: the same source must feed more with alpha 1.
        self.assertGreater(min(a-b for a,b in zip(flat('clamp_authored_ref_none'),
                                                  flat('clamp_ref_none')) if a>0),0.)
        self.assertGreater(min(a-b for a,b in zip(flat('clamp_hot_none'),
                                                  flat('clamp_hot_two')) if a>0),0.)
        self.assertGreater(min(a-b for a,b in zip(flat('clamp_hot_two'),
                                                  flat('clamp_hot_one')) if a>0),0.)

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
            payload=a.read_bytes()
            self.assertEqual(payload[:12],b'X3BP0004'+struct.pack('<I',47))
            offset=12
            for c in self.cases:
                header=struct.pack('<4I9f',c['width'],c['height'],
                                   fixture.ref.DECODE_MODES.index(c['mode']),c['levels'],
                                   c['strength'],c['sharp'],c['threshold'],c['exposure'],
                                   c['authored_glow_gain'],c['highlight_gain'],c['scatter'],
                                   c['source_clamp'],c.get('dither',0.))
                self.assertEqual(payload[offset:offset+len(header)],header)
                offset+=len(header)+c['width']*c['height']*8
            self.assertEqual(offset,len(payload))

if __name__=='__main__':unittest.main()
