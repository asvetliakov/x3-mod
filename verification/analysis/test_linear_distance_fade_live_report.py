"""Focused positive/negative evidence checks; never execute Wine or rebuild DLLs."""
import copy
from pathlib import Path
import re
import struct
import tempfile
import unittest

import run_linear_distance_fade_live as live


def line(prefix, **values):
    return prefix+' '+' '.join(f'{k}={v}' for k,v in values.items())


def report(fade=1,emission=1,lazy=1):
    required=emission+2*fade
    out=['RESULT PASS frames=30 checks=123 restorations=35 taa_reference_frames=29 taa_skipped_frames=1',
         'RESET PASS',line('FADE_RESET',refs=3+bin(required).count('1') if required else 0,allocations=4 if required else 0),
         'FADE_OPAQUE_RETURN frame=13 matched=1',
         'FADE_REJECTED frame=22 source_failed=1 taa=0 history_seeded=0 copy_exact=1']
    native={i:(.95,.94,.96,1.) for i in range(6)}
    out += [line('FADE_NATIVE',pair=i,before='1,1,1,1',after=','.join(map(str,native[i]))) for i in range(6)]
    trace=['motion_output_release held=123 released=1']
    for f in range(live.FRAMES):
        sources,stopped=live.expected_sources(f,fade,emission)
        s=[0]*22
        s[0]=int(bool(required));s[1]=int(bool(required and not stopped))
        for index,key in ((4,'prepared'),(5,'linear'),(6,'native'),(7,'incomplete')):
            s[index]=sum(r[key] for r in sources)
        s[10]=s[4];s[11]=0x8876086c if f==22 and fade else 0;s[12]=len(sources)
        s[13]=sum(r['kind']=='fade' for r in sources)*bool(fade)
        s[14]=sum(r['prepared'] for r in sources if r['kind']=='fade')
        s[15]=sum(r['linear'] for r in sources if r['kind']=='fade')
        s[16]=s[18]=s[19]=required;s[17]=int(stopped)
        s[20]=7+bin(required).count('1') if required else 0;s[21]=4 if required else 0
        out.append(line('FADE_LIVE',frame=f,fade=fade,emission=emission,draws=len(sources),**{f's{i}':v for i,v in enumerate(s)},hash_alpha='a',hash_motion='m',hash_depth='d',hash_mask='mask'))
        prior=int(bool(required))
        for i,r in enumerate(sources):
            alpha=(.25 if i>=2 else .125) if r['kind']=='emission' else (0. if f==1 else .078125)
            out.append(line('FADE_SOURCE',frame=f,source=i,kind=r['kind'],pair=r['pair'],alpha=alpha,overlap=2 if f==15 else 1,fault=r['fault'],hr=f'{r["hr"]:08x}',original_calls=1,prepared=r['prepared'],linear=r['linear'],native=r['native'],mask_before=prior,mask_after=int(r['mask_valid']),hash_mask_before='mask',hash_mask_after='mask'))
            prior=int(r['mask_valid'])
        if f in (1,*live.PRESENT_FRAMES,15,16,17,18):
            before=(1.,1.,1.,.5)
            for source,(kind,pair,_,_) in enumerate(live.source_plan(f)):
                overlap=2 if f==15 else 1
                if f==1:after=before
                elif kind=='emission':after=live.expected_emission(before,emission)
                elif fade:after=live.expected_composite(before,pair,overlap=overlap)
                elif f in live.PRESENT_FRAMES:after=native[pair][:3]+(before[3],)
                else:
                    alpha=.078125;original=[(v-(1-alpha))/alpha for v in native[pair][:3]];after=before
                    for _ in range(overlap):after=tuple(live.component.fp16_rt_store(alpha*v+(1-alpha)*a) for a,v in zip(after,original))+(before[3],)
                for x in ((16,32) if f in (1,*live.PRESENT_FRAMES) else (32,)):
                    out.append(line('FADE_SAMPLE',frame=f,source=source,pair=pair,x=x,y=32,kind=kind,overlap=overlap,alpha=.125 if kind=='emission' else 0 if f==1 else .078125,before=','.join(map(str,before)),after=','.join(map(str,after))))
                before=after
        out.append(line('FADE_GEOMETRY',frame=f,ordinary_t=.03125*(f%3)))
        out.append(line('FADE_CAMERA',frame=f,view_translation=f'{.125*(f%5)},0,0'))
        trace.append(line('motion_output_frame',frame=f,rt_mode='lazy' if lazy else 'perdraw',apply_failures=0,restore_failures=0,taa_resolved=int(f!=22),taa_history=int(f not in (0,22,23,27))))
        if f!=22:trace.append(line('motion_output_taa_readback',frame=f,result='00000000'))
    out.append(line('FADE_CHECKS',frames=30,submissions=sum(len(live.source_plan(f)) for f in range(30)),qualified=1,benchmark=0))
    return '\n'.join(out),'\n'.join(trace)


class FadeLiveReportTests(unittest.TestCase):
    def test_scope_and_source_order(self):
        self.assertEqual(len(live.FADE_PAIRS),6)
        self.assertEqual(len(live.PROGRAMS),14)
        self.assertEqual(sum(len(live.source_plan(f)) for f in range(30)),35)
        for fade in (0,1):
            for emission in (0,1):
                for lazy in (0,1):
                    result=live.validate_functional(*report(fade,emission,lazy),fade,emission,lazy)
                    self.assertEqual((result['frames'],result['sources'],result['samples']),(30,35,21))

    def test_missing_duplicate_or_corrupt_rows_refused(self):
        output,trace=report()
        for prefix in ('FADE_LIVE frame=0 ','FADE_SOURCE frame=19 source=2 ', 'FADE_SAMPLE frame=12 source=0 pair=5 x=16 ', 'FADE_GEOMETRY frame=13 ', 'FADE_CAMERA frame=4 ', 'FADE_OPAQUE_RETURN ', 'FADE_REJECTED ', 'FADE_RESET ', 'FADE_CHECKS ', 'FADE_NATIVE pair=5 ', 'RESET PASS'):
            with self.subTest(prefix=prefix),self.assertRaises(AssertionError):
                live.validate_functional('\n'.join(r for r in output.splitlines() if not r.startswith(prefix)),trace,1,1,1)
        for prefix in ('FADE_LIVE frame=0 ','FADE_SOURCE frame=19 source=2 ','FADE_SAMPLE frame=12 source=0 pair=5 x=16 '):
            row=next(r for r in output.splitlines() if r.startswith(prefix))
            with self.subTest(duplicate=prefix),self.assertRaises(AssertionError):live.validate_functional(output+'\n'+row,trace,1,1,1)
        for old,new in (('s20=9','s20=10'),('s21=4','s21=8'),('original_calls=1','original_calls=2'),('s16=3','s16=1'),('s18=3','s18=1'),('ordinary_t=0.03125','ordinary_t=0'),('matched=1','matched=0'),('overlap=2','overlap=1'),('hr=8876086c','hr=00000000'),('mask_after=0','mask_after=1')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_functional(output.replace(old,new,1),trace,1,1,1)
        with self.assertRaises(AssertionError):live.validate_functional(output,trace.replace('taa_resolved=0','taa_resolved=1',1),1,1,1)

    def test_no_healing_and_failed_source_contract(self):
        for frame in (19,20,24):
            rows,stopped=live.expected_sources(frame,1,1)
            self.assertTrue(stopped)
            self.assertEqual(rows[-1]['prepared'],0)
            self.assertFalse(rows[-1]['mask_valid'])
        rows,_=live.expected_sources(21,1,1)
        self.assertEqual((rows[0]['prepared'],rows[0]['native'],rows[0]['linear']),(1,1,0))
        self.assertTrue(rows[0]['mask_valid'])
        for fade in (0,1):
            rows,_=live.expected_sources(22,fade,1)
            self.assertEqual(rows[1]['hr'],0x8876086c)
            self.assertEqual(rows[1]['incomplete'],fade)

    def test_independent_linear_witness_rejects_native_or_wrong_alpha(self):
        output,_=report()
        samples=[live.fields(r) for r in output.splitlines() if r.startswith('FADE_SAMPLE ')]
        native=live.native_baselines([live.fields(r) for r in output.splitlines() if r.startswith('FADE_NATIVE ')])
        for field,value in (('after','1,1,1,.5'),('pair','5'),('alpha','.125')):
            changed=copy.deepcopy(samples);changed[2][field]=value
            with self.subTest(field=field),self.assertRaises(AssertionError):live.validate_samples(changed,True,True,native)
        changed=copy.deepcopy(samples);changed[0]['after']='1,1,1,.5001'
        with self.assertRaises(AssertionError):live.validate_samples(changed,False,True,native)
        off,_=report(0,1)
        native_samples=[live.fields(r) for r in off.splitlines() if r.startswith('FADE_SAMPLE ')]
        native_samples[2]['after']='.949,.94,.96,.5'
        with self.assertRaises(AssertionError):live.validate_samples(native_samples,False,True,native)
        # Independently pinned qualified values ensure the reused case is not a
        # zero-gain variant or a value inferred from actual composed A.
        self.assertEqual(live.expected_composite((1.,1.,1.,.5),0)[:3],(.96728515625,.9638671875,.97021484375))
        self.assertEqual(live.expected_composite((1.,1.,1.,.5),5)[:3],(.96728515625,.9638671875,.96923828125))

    def test_raw_temporal_and_mask_corruption_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            work=Path(tmp);(work/'x3-modern-captures').mkdir()
            for frame in range(30):
                if frame!=22:
                    for path in (work/f'reference_taa_{frame}.rgba16f',work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f'):path.write_bytes(bytes(64*64*8))
                (work/f'distance_fade_color_{frame}.rgba32f').write_bytes(struct.pack('<4f',1.,1.,1.,.5)*(64*64))
                (work/f'distance_fade_mask_{frame}.rgba32f').write_bytes(bytes(64*64*16))
            result=live.validate_pixels(work,0,0)
            self.assertEqual(sum(result['covered_pixels']),0)
            self.assertEqual(len(result['temporal_sha256']),live.FRAMES)
            self.assertIsNone(result['temporal_sha256'][live.FAILED_SOURCE])
            path=work/'x3-modern-captures/taa_1_0.rgba16f';path.write_bytes(b'X'+path.read_bytes()[1:])
            with self.assertRaises(AssertionError):live.validate_pixels(work,0,0)
            path.write_bytes(bytes(64*64*8))
            path=work/'distance_fade_mask_0.rgba32f';path.write_bytes(struct.pack('<f',1.)+path.read_bytes()[4:])
            with self.assertRaises(AssertionError):live.validate_pixels(work,0,0)

    def test_timing_only_same_fenced_windows(self):
        out=['RESULT PASS frames=18 checks=1','FADE_CHECKS frames=18 submissions=126 benchmark=1']
        for count in live.COUNTS:
            for sample in range(4):out.append(line('FADE_TIMING',width=1280,height=768,count=count,sample=sample,fade=1,emission=0,source_ms=1.,terminal_ms=2.,total_ms=3.))
        text='\n'.join(out)
        self.assertEqual(set(live.validate_timing(text,1,1280,768)['counts']),{'1','4','16'})
        for old,new in (('total_ms=3.0','total_ms=4.0'),('source_ms=1.0','source_ms=nan'),('emission=0','emission=1'),('count=16','count=8'),('sample=0','sample=1')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_timing(text.replace(old,new,1),1,1280,768)

    def test_admission_refuses_false_availability(self):
        out=['RESULT PASS frames=4 checks=1']
        for frame in range(4):
            status={f's{i}':int(frame!=0) if i==12 else 0 for i in range(22)}
            out.append(line('FADE_LIVE',frame=frame,fade=1,emission=1,**status))
            if frame:out.append(line('FADE_SOURCE',frame=frame,source=0,hr='00000000',original_calls=1,prepared=0,linear=0))
        output='\n'.join(out);trace='motion_output_release held=10 released=1'
        for taa,hdr in ((0,1),(1,0)):
            self.assertEqual(live.validate_admission(output,trace,taa,hdr)['frames'],4)
            with self.assertRaises(AssertionError):live.validate_admission(output.replace('s16=0','s16=3',1),trace,taa,hdr)


if __name__=='__main__':unittest.main()
