"""Authored PS2 MRT composition oracle and strict compact-report parser."""
import copy
import math
import struct
import unittest
import run_linear_emission as r


def report(cases):
    lines=['CAPS vs=fffe0300 ps=ffff0300 rt=4']
    lines += [f'FORMAT name={name} hr=00000000' for name in ('fp16_rt','fp16_blend','d24s8')]
    lines += [f'DEPTH_MATCH format={f} hr=00000000' for f in (113,)]
    lines += ['MRT_CAPS slots=4 postblend=1 independent_masks=1']
    data=bytearray()
    for c in cases:
        color,depth,count=r.mrt_expected(c)
        lines.append('MRT_CASE id='+str(c['id'])+' '+' '.join(f'{k}={v}' for k,v in count.items()))
        data+=struct.pack('<I1280f',c['id'],*(v for p in color for v in p),*depth)
    for w,h in ((1280,768),(1920,1080)):
        for i in range(48):
            variant=5-i%6 if (i//6)%2 else i%6
            lines.append(f'MRT_TIMING width={w} height={h} variant={variant} sample={i} completed_ms=1.25')
    lines.append(f'MRT_RESULT pass cases={len(cases)} shaders=78')
    return '\n'.join(lines)+'\n',bytes(data)


class MrtReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.mrt_cases();cls.text,cls.data=report(cls.cases)

    def test_complete_record_and_source_variant_coverage(self):
        result=r.validate_mrt_report(self.text,self.data,self.cases)
        self.assertEqual(result['cases'],38)
        self.assertEqual(result['max_rgb_tolerance_fraction'],0)
        self.assertEqual(len(result['timings']),12)
        variants={c['ops'][0]['affine']+(0 if c['flags']&64 else 2)+(4 if c['flags']&32 else 0) for c in self.cases if c['label']=='source_contract_variants'}
        self.assertEqual(variants,set(range(8)))
        self.assertGreater(result['invariants']['minuszero'],0)
        self.assertGreater(result['invariants']['capzero'],0)
        self.assertGreaterEqual(result['invariants']['infinite'],256)

    def test_native_rgb_fade_affine_and_raw_alpha_are_separate(self):
        c=self.cases[0];o=r.op(color=(.5,.25,.125,.125),affine=1,fade=.5,gain=4)
        native,energy=r.mrt_sample(o,c,(.25,.25))
        self.assertEqual(native[3],.125);self.assertEqual(energy[3],0)
        artistic=.5*.75+.25*.125+.03125
        self.assertAlmostEqual(native[0],artistic*.5)
        self.assertAlmostEqual(energy[0],artistic**2.2*.5*4)
        absent=copy.deepcopy(c);absent['flags']|=64
        nofade,_=r.mrt_sample(o,absent,(.25,.25));self.assertAlmostEqual(nofade[0],artistic)

    def test_zero_E_copies_each_A_channel_and_native_B_alpha(self):
        c=next(c for c in self.cases if c['label']=='zero_energy_rotations_64')
        color,_,count=r.mrt_expected(c)
        for i,p in enumerate(color):
            original=r.initial_pixel(i%16,i//16,True)
            self.assertEqual(struct.pack('<3f',*p[:3]),struct.pack('<3f',*original[:3]))
            self.assertEqual(p[3],16.25)
        self.assertEqual(count['bursts'],64)
        red=next(c for c in self.cases if c['label']=='asymmetric_channel_identity')
        result,_,_=r.mrt_expected(red)
        self.assertEqual(result[0][1:3],[154.625,200.])
        self.assertGreater(result[0][0],0)

    def test_alpha_test_is_shared_by_native_and_energy(self):
        c=copy.deepcopy(self.cases[0]);c.update(mask=1,ops=[r.op(color=(.5,.25,.125,.125))])
        color,_,count=r.mrt_expected(c)
        self.assertEqual(color,[r.initial_pixel(x,y,False) for y in range(16) for x in range(16)])
        self.assertEqual(count['zero'],768)
        c['ops'][0]['color'][3]=.5
        self.assertNotEqual(r.mrt_expected(c)[0],color)

    def test_separate_alpha_modes_are_independent_of_E(self):
        c=copy.deepcopy(self.cases[0]);c['ops']=[r.op(rect=(0,0,1,1),color=(.5,.25,.125,.125),gain=0)]
        for mode,alpha in ((0,.375),(1,.25),(2,.234375)):
            c['alpha']=mode
            self.assertEqual(r.mrt_expected(c)[0][0],[.125,.25,.375,alpha])

    def test_composition_refusal_is_current_native_adoption(self):
        fallback=next(c for c in self.cases if c['label']=='composition_refusal_native_adoption')
        native=next(c for c in self.cases if c['label']=='native_encoded_control')
        self.assertEqual(r.mrt_expected(fallback)[0],r.mrt_expected(native)[0])
        count=r.mrt_expected(fallback)[2]
        self.assertEqual(count['fallback'],1);self.assertEqual(count['sources'],2)
        self.assertGreater(count['native'],0)

    def test_rgb_only_mask_refuses_mrt_and_retains_native_alpha(self):
        c=next(c for c in self.cases if c['label']=='rgb_write_mask_refusal')
        color,_,count=r.mrt_expected(c)
        self.assertEqual(count['bursts'],0);self.assertEqual(count['refused'],1)
        self.assertEqual({p[3] for p in color},{.25})

    def test_positive_infinity_is_sanitized_before_composition(self):
        c=next(c for c in self.cases if c['label']=='positive_infinite_E_seed')
        color,_,count=r.mrt_expected(c)
        self.assertEqual(count['infinite'],256)
        self.assertTrue(all(p[1]==r.half(r.encode(r.CAP)) for p in color))
        self.assertEqual(color[0][2],200.)
        self.assertEqual(math.copysign(1.,color[2][0]),-1.)

    def test_point_sampling_witnesses_avoid_texel_boundaries(self):
        for c in self.cases:
            if not c['flags']&4:continue
            for o in c['ops']:
                for i in r.pixels_in(o,c):
                    x,y=i%16,i//16;l,t,rr,b=o['rect']
                    uv=((x+.5-4-l*8)/((rr-l)*8),(y+.5-4-t*8)/((b-t)*8))
                    self.assertGreaterEqual(min(abs(v-.5) for v in uv),.08)

    def test_rejects_bad_native_copy_zero_and_alpha_invariants(self):
        for field in ('copy=1024','native=1024','zero=480','alpha=256'):
            # Select actual first occurrence; values differ with geometry.
            name=field.split('=')[0]
            import re
            m=re.search(r'\b'+name+r'=(\d+)',self.text)
            damaged=self.text[:m.start()]+name+'='+str(int(m.group(1))+1)+self.text[m.end():]
            with self.assertRaises(AssertionError):r.validate_mrt_report(damaged,self.data,self.cases)

    def test_full_writes_do_not_require_independent_masks_or_R32F(self):
        text=self.text.replace('independent_masks=1','independent_masks=0',1)
        result=r.validate_mrt_report(text,self.data,self.cases)
        self.assertEqual(result['cases'],38)
        self.assertFalse(result['mrt_caps']['independent_write_masks'])
        self.assertNotIn('r32f',text.lower())
        self.assertNotIn('format=114',text)

    def test_rejects_bad_caps_and_missing_completion(self):
        for text in (self.text.replace('postblend=1','postblend=0',1),self.text.replace('slots=4','slots=1',1),self.text.replace('FORMAT name=fp16_blend hr=00000000','FORMAT name=fp16_blend hr=8876086a',1),self.text.rsplit('MRT_RESULT',1)[0]):
            with self.assertRaises(AssertionError):r.validate_mrt_report(text,self.data,self.cases)

    def test_rejects_alpha_depth_and_readback_order_corruption(self):
        for offset in (0,4+12,4+1024*4):
            raw=bytearray(self.data);struct.pack_into('<f',raw,offset,123.)
            with self.assertRaises(AssertionError):r.validate_mrt_report(self.text,raw,self.cases)
        with self.assertRaises(AssertionError):r.parse_mrt_pixels(self.data[:-1],self.cases)

    def test_rejects_nonfinite_or_reordered_timings(self):
        for text in (self.text.replace('completed_ms=1.25','completed_ms=nan',1),self.text.replace('variant=0 sample=0','variant=1 sample=0',1)):
            with self.assertRaises(AssertionError):r.validate_mrt_report(text,self.data,self.cases)

if __name__=='__main__':unittest.main()
