"""Host checks of focused evidence gates; these do not stand in for GPU execution."""
import copy
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
import run_linear_distance_fade as run

def write(path,rows):path.write_bytes(b''.join(struct.pack('<4f',*p) for p in rows))

def witness(raw):
    source=run.cases();lines=['FADE_CAPS refused=4','FADE_STATE refused=3']
    for c in run.expanded_cases(source):
        count=run.steps(c)
        if c['affine']:
            f=c['affine'];first=0x8876086c if f==3 else 0x80004005
            lines.append(f"FADE_FAILURE id={c['id']} stage={f} native=1 prepared={int(f>=3)} first={first:08x} coverage={int(f not in (3,5))}")
        else:
            current=[(.333251953125,.50048828125,8.,.333251953125)]*256
            if c['flags']&run.BAD_BACKGROUND:
                current=[((float('inf'),-float('inf'),float('nan'),p[3]) if run.covered(c,0,n%16,n//16) else p) for n,p in enumerate(current)]
            write(raw/f"fade_{c['id']}_0_initial.rgba32f",current)
            union=[False]*256
            for step in range(count):
                cc=run.step_case(c,step);L=run.material.expected(cc).linear_rgb;a=run.source_alpha(cc)
                energy=[];mask=[];output=[];original=[]
                for n,old in enumerate(current):
                    inside=run.covered(c,step,n%16,n//16);union[n]|=inside
                    Q=[0.,0.,0.];q=0.
                    if inside:
                        for _ in range(2 if c['reverse']==1 else 1):
                            Q=[run.fp16_rt_store(a*x+(1-a)*y) for x,y in zip(L,Q)];q=run.fp16_rt_store(a+(1-a)*q)
                    energy.append(tuple(Q)+(q,));mask.append((1.,1.,1.,0.) if union[n] else (0.,0.,0.,0.))
                    output.append(run.compose(old,Q,q));original.append(tuple(L)+(a,) if inside else (0.,0.,0.,0.))
                for label,rows in (('source',original),('E',energy),('M',mask),('C',output)):
                    write(raw/f"fade_{c['id']}_{step}_{label}.rgba32f",rows)
                current=output
        lines.append(f"FADE_CASE id={c['id']} pair={c['pair']} steps={count} native={count} brackets={count-int(c['affine'] in (1,2))} fault={c['affine']} reset={int(c['id']>=1000)}")
    for reset in (0,1):
        active=[c for c in run.expanded_cases(source) if (c['id']>=1000)==bool(reset)];calls=sum(run.steps(c) for c in active)
        lines.append(f"FADE_BATCH reset={reset} cases={len(active)} brackets={calls-sum(c['affine'] in (1,2) for c in active)} native={calls} restored={calls} refs_before=10 refs_after=10")
    for index,(label,bound,covered) in enumerate(run.REGION_CASES):
        x,y,w,h=run.REGION_VIEWPORT.get(label,(0,0,16,16))
        rect=(x+1,y+1,x+w-1,y+h-1) if bound else (x,y,x+w,y+h)
        inside=lambda px,py:covered and rect[0]+1<=px<rect[2]-1 and rect[1]+1<=py<rect[3]-1
        mask=[(1.,1.,1.,0.) if inside(n%16,n//16) else (0.,0.,0.,0.) for n in range(256)]
        write(raw/f"fade_{5000+index}_0_M.rgba32f",mask)
        lines.append(f"FADE_REGION id={5000+index} label={label} bound={bound} reason={run.REGION_REASONS.get(label,0)} "
                     f"rect={rect[0]},{rect[1]},{rect[2]},{rect[3]} viewport={x},{y},{w},{h} covered={sum(m[0]==1 for m in mask)} "
                     f"violations=0 area={(rect[2]-rect[0])*(rect[3]-rect[1])}")
    lines.append(f"FADE_REGION_RESULT cases={len(run.REGION_CASES)} bound={sum(c[1] for c in run.REGION_CASES)} violations=0")
    # Step 2 in-place witness lines (fixture order: region twins, rectangle
    # cases, ladder, capability refusal, Reset, then one batch line per side).
    for index,(label,_,_) in enumerate(run.REGION_CASES):
        lines.append(f"FADE_INPLACE_REGION id={5000+index} label={label} rect=1,1,15,15 exact=1")
    for index,(label,brackets) in enumerate(run.INPLACE_CASES):
        lines.append(f"FADE_INPLACE id={6000+index} label={label} brackets={brackets} exact=1")
    for stage,label,prepared,first,recovery,coverage,blocked in run.INPLACE_LADDER:
        lines.append(f"FADE_INPLACE_FAILURE id={7000+stage} stage={stage} label={label} native=1 prepared={prepared} first={first:08x} "
                     f"recovery={recovery:08x} coverage={coverage} blocked={blocked} exchange=0")
    lines+=['FADE_INPLACE_CAPS refused=1 fallback=1 exact=1','FADE_INPLACE_RESET interrupted=1 detached=1']
    for reset in (0,1):
        active=[c for c in run.expanded_cases(source) if (c['id']>=1000)==bool(reset)]
        n=run.inplace_brackets(active);cases=sum(1 for c in active if not c['affine'])+(0 if reset else len(run.REGION_CASES)+len(run.INPLACE_CASES)+1)
        lines.append(f"FADE_INPLACE_BATCH reset={reset} cases={cases} brackets={n} native={n} exact_a={n} exact_m={n}")
    for w,h in run.TIMING_SIZES:
        for f in (1.,.06,.01):
            for policy in run.TIMING_POLICIES:
                for dips in run.TIMING_DIPS:
                    for i in range(run.TIMING_ITERATIONS):
                        lines.append(f"FADE_TIMING width={w} height={h} policy={policy} f={f:.4f} rect=0,0,{w},{h} dips={dips} iteration={i} completed_ms={.5*dips+i*.01:.6f}")
    lines.append('FADE_TIMING_RESULT sizes=2 fractions=3 policies=2 dips=3 iterations=8')
    lines+=['FADE_RESULT PASS reset=1 partial_vs_failures=2','RESULT PASS cases=65']
    return '\n'.join(lines),source

class DistanceFadeReport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='x3-fade-report-test-');cls.addClassCleanup(cls.temp.cleanup)
        cls.raw=Path(cls.temp.name);cls.text,cls.cases=witness(cls.raw)
    def validate(self,text=None):return run.validate_report(self.text if text is None else text,self.cases,self.raw)
    def test_component_attach_uses_current_display_format(self):
        source=(ROOT/'verification/probe/linear_distance_fade_fixture_inc.h').read_text()
        self.assertIn('api(d->GetDisplayMode(0, &display));',source)
        self.assertNotIn('D3DFMT_A8R8G8B8',source)
        import re
        calls=re.findall(r'(?:refused|pass)\.attach\((.*?)\)',source,re.S)
        self.assertEqual(len(calls),3,'refused, primary and timing attach')
        # slots.data() contains parentheses, so pin the complete argument spans.
        self.assertRegex(source,r'refused\.attach\(d, slots\.data\(\), caps, display\.Format,\s*D3DFMT_D24S8\)')
        self.assertRegex(source,r'pass\.attach\(d, slots\.data\(\), shaders\.caps,\s*display\.Format, D3DFMT_D24S8\)')
        self.assertIn('FADE_ATTACH reset=%u adapter=%u misc=%08lx hr=%08lx',source)

    def test_complete_witness_and_exact_counts(self):
        result=self.validate()
        self.assertEqual((result['cases'],result['source_calls'],result['fault_cases']),(71,257,5))
        self.assertGreater(result['exact_raw_channels'],150000)
        self.assertLess(result['max_tolerance_fraction'],.0001)
        self.assertEqual(result['energy_channels'],193536)
        self.assertEqual(result['exact_energy_channels'],193536)
        # Step 2: 252 non-fault steps (246 first batch, 6 after Reset) plus 29
        # region, 11 rectangle and 1 fallback twins, each compared twice (A, M).
        self.assertEqual((result['inplace_cases'],result['inplace_brackets'],result['inplace_exact_comparisons']),(104,293,586))
        self.assertEqual((result['inplace_rectangle_cases'],result['inplace_ladder_stages'],result['inplace_capability_refusals'],result['inplace_reset']),(8,9,1,True))
        self.assertEqual(len(result['timing']),36)
        self.assertEqual(result['timing'][0]['median_ms'],.5+4*.01)
    def test_inplace_policy_witness_mutations_are_rejected(self):
        # Selection (caps refusal, fallback), ladder (first HRESULT, source-once,
        # recovery, suppression, no exchange) and the bit-exact twin counts.
        for before,after in (
            ('FADE_INPLACE_CAPS refused=1 fallback=1 exact=1','FADE_INPLACE_CAPS refused=0 fallback=1 exact=1'),
            ('FADE_INPLACE_RESET interrupted=1 detached=1','FADE_INPLACE_RESET interrupted=1 detached=0'),
            ('label=source native=1 prepared=1 first=8876086c recovery=00000000','label=source native=1 prepared=1 first=80004005 recovery=00000000'),
            ('label=source native=1 prepared=1','label=source native=2 prepared=1'),
            ('label=composite native=1 prepared=1 first=80004005 recovery=00000000','label=composite native=1 prepared=1 first=80004005 recovery=80004005'),
            ('label=restore native=1 prepared=1 first=80004005 recovery=00000001 coverage=0 blocked=1','label=restore native=1 prepared=1 first=80004005 recovery=00000001 coverage=0 blocked=0'),
            ('label=copy native=1 prepared=0 first=80004005 recovery=00000001 coverage=1','label=copy native=1 prepared=0 first=80004005 recovery=00000001 coverage=0'),
            ('blocked=1 exchange=0','blocked=1 exchange=1'),
            ('label=restore_recovery native=1 prepared=1 first=8876086c recovery=00000000','label=restore_recovery native=1 prepared=1 first=8876086c recovery=00000001'),
            ('FADE_INPLACE id=6005 label=disjoint brackets=2 exact=1','FADE_INPLACE id=6005 label=disjoint brackets=1 exact=1'),
            ('label=overlapping brackets=2 exact=1','label=overlapping brackets=2 exact=0'),
            ('FADE_INPLACE_REGION id=5020 label=scissor rect=1,1,15,15 exact=1','FADE_INPLACE_REGION id=5020 label=scissor rect=1,1,15,15 exact=0'),
            ('FADE_INPLACE_BATCH reset=0 cases=98 brackets=287 native=287 exact_a=287 exact_m=287','FADE_INPLACE_BATCH reset=0 cases=98 brackets=287 native=288 exact_a=287 exact_m=287'),
            ('FADE_INPLACE_BATCH reset=1 cases=6 brackets=6 native=6 exact_a=6 exact_m=6','FADE_INPLACE_BATCH reset=1 cases=6 brackets=6 native=6 exact_a=5 exact_m=6'),
            ('FADE_TIMING_RESULT sizes=2 fractions=3 policies=2 dips=3 iterations=8','FADE_TIMING_RESULT sizes=2 fractions=3 policies=2 dips=3 iterations=7'),
            ('partial_vs_failures=2','partial_vs_failures=1')):
            with self.subTest(before=before):
                self.assertIn(before,self.text)
                with self.assertRaises(AssertionError):self.validate(self.text.replace(before,after,1))
        with self.assertRaises(AssertionError):self.validate(self.text+'\nFADE_INPLACE_DIFF label=case pixel=0 x=0 y=0 actual=0,0,0,0 expected=1,0,0,0')
        with self.assertRaises(AssertionError):self.validate(self.text.replace('FADE_TIMING width=1920 height=1080 policy=inplace f=0.0100 rect=0,0,1920,1080 dips=16 iteration=7','FADE_TIMING width=1920 height=1080 policy=inplace f=0.0100 rect=0,0,1920,1080 dips=16 iteration=8',1))
    def test_hostile_report_mutations_are_rejected(self):
        for before,after in (
            ('FADE_CAPS refused=4','FADE_CAPS refused=3'),('FADE_STATE refused=3','FADE_STATE refused=2'),
            ('restored=251','restored=250'),('refs_after=10','refs_after=11'),
            ('partial_vs_failures=2','partial_vs_failures=0'),
            ('first=8876086c','first=80004005'),
            ('id=101 stage=1 native=1','id=101 stage=1 native=2'),
            ('id=103 stage=3 native=1 prepared=1','id=103 stage=3 native=1 prepared=0'),
            ('first=8876086c coverage=0','first=8876086c coverage=1'),
            ('FADE_CASE id=1000 pair=110','FADE_CASE id=1000 pair=111')):
            with self.subTest(before=before):
                self.assertIn(before,self.text)
                with self.assertRaises(AssertionError):self.validate(self.text.replace(before,after,1))
    def test_numerical_and_exact_path_mutations_are_rejected(self):
        # All are interior pixels except raw-A corruption at untouched (0,0).
        for label,pixel,lane,value in (('E',3*16+3,3,.5),('M',3*16+3,0,0.),
                ('source',3*16+3,3,.25),('C',0,0,.4),('C',3*16+3,3,.5),('E',3*16+3,0,float('nan'))):
            path=self.raw/f'fade_0_0_{label}.rgba32f';saved=path.read_bytes();data=bytearray(saved)
            struct.pack_into('<f',data,(pixel*4+lane)*4,value);path.write_bytes(data)
            try:
                with self.subTest(label=label,lane=lane):
                    with self.assertRaises(AssertionError):self.validate()
            finally:path.write_bytes(saved)
    def test_fixed_rt_store_boundaries_and_upload_rounding(self):
        for value,wanted in ((0.,0.),(-0.,-0.),(1.,1.),(.42105263471603394,.4208984375),
                (1+3*2**-11,1+2**-10),(-(1+3*2**-11),-(1+2**-10)),
                (2**-25,0.),(3*2**-25,2**-24),(65504.,65504.),(65520.,65504.)):
            with self.subTest(value=value):
                self.assertEqual(struct.pack('<f',run.fp16_rt_store(value)),struct.pack('<f',wanted))
        self.assertEqual(run.ref.half(.42105263471603394),.421142578125,'shared upload RNE unchanged')
        for value in (float('nan'),float('inf'),-float('inf')):
            with self.assertRaises(ValueError):run.fp16_rt_store(value)

    def test_nearest_even_q_is_rejected_for_captured_fog(self):
        path=self.raw/'fade_8_0_E.rgba32f';saved=path.read_bytes();data=bytearray(saved)
        struct.pack_into('<f',data,(2*16+2)*16+12,.421142578125)
        path.write_bytes(data)
        try:
            with self.assertRaises(AssertionError):self.validate()
        finally:path.write_bytes(saved)

    def test_scalar_endpoints_do_not_evaluate_bad_background(self):
        a=(float('nan'),float('inf'),-float('inf'),.375);Q=(.25,.5,1.)
        for q in (0.,float('nan'),-float('inf')):
            actual=run.compose(a,Q,q)
            self.assertEqual(struct.pack('<4f',*actual),struct.pack('<4f',*a))
        for q in (1.,float('inf')):
            self.assertEqual(run.compose(a,Q,q),tuple(run.fp16_rt_store(run.ref.encode(x)) for x in Q)+(a[3],))
        ordinary=(.5,.25,.75,.375)
        expected=tuple(run.fp16_rt_store((Q[i]+.5*ordinary[i]**2.2)**(1/2.2)) for i in range(3))+(ordinary[3],)
        self.assertEqual(run.compose(ordinary,Q,.5),expected)

if __name__=='__main__':unittest.main()
