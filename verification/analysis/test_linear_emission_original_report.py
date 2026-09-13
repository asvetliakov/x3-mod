"""Bounded actual-original GPU oracle/report checks; no bundled shader bytes."""
import copy
import hashlib
import re
from pathlib import Path
import math
import struct
import unittest
import run_linear_emission as r


def report(cases):
    lines=['CAPS vs=fffe0300 ps=ffff0300 rt=4']
    lines += [f'FORMAT name={name} hr=00000000' for name in ('fp16_rt','fp16_blend','d24s8')]
    lines += ['DEPTH_MATCH format=113 hr=00000000','MRT_CAPS slots=4 postblend=1 independent_masks=0']
    data=bytearray()
    for c in cases:
        color,depth,count,native,energy=r.mrt_expected(c,True)
        lines.append('MRT_CASE id='+str(c['id'])+' '+' '.join(f'{k}={v}' for k,v in count.items()))
        data+=struct.pack('<I',c['id'])
        for values in ([v for p in color for v in p],depth,[v for p in native for v in p],[v for p in energy for v in p]):
            data+=struct.pack('<'+str(len(values))+'f',*values)
    lines.append(f'ORIGINAL_RESULT pass cases={len(cases)} shaders=77')
    return '\n'.join(lines)+'\n',bytes(data)


class OriginalReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.original_cases();cls.text,cls.data=report(cls.cases)

    def test_complete_pair_gain_and_program_record(self):
        result=r.validate_original_report(self.text,self.data,self.cases)
        self.assertEqual(result['cases'],311)
        self.assertEqual(result['original_vertex_programs'],8)
        self.assertEqual(result['original_pixel_programs'],10)
        self.assertEqual(result['source_variants'],50)
        self.assertEqual(result['shader_creations'],77)
        self.assertEqual(result['timings'],[])
        for p in range(20):
            rows=[c for c in self.cases if c['actual_profile']==p]
            self.assertEqual({o['gain'] for c in rows for o in c['ops']},set(r.ORIGINAL_GAINS))
            self.assertTrue(all(bool(o['affine'])==(r.ORIGINAL_PAIRS[p][1] in r.AFFINE_PS) for c in rows for o in c['ops']))
        self.assertGreater(result['invariants']['minuszero'],0)
        self.assertGreater(result['invariants']['capzero'],0)

    def test_retained_binary_prefix_and_complete_native_layouts(self):
        for maker,count,digest in ((r.original_cases,70,'19b18d25533a2429de7f1217d06fdd13ff28932d4bb278bf854b1998e7a957c5'),
                                  (r.coverage_cases,81,'4766f912430cd4ae2881cef7e710b0590e40c65827381f73907b5eb695b8fa7a'),
                                  (r.pass_cases,60,'ede7c8d05b58e02979f37210a27eab5f16c64ef32581c24f679778d8dca855ec')):
            self.assertEqual(hashlib.sha256(r.binary_cases(maker()[:count])).hexdigest(),digest)
        from verification.analysis.test_linear_emission_transformer import PREVIOUS_PAIRS,NEW_PAIR_OCCURRENCES,PROFILES,PS2X,VERTICES
        self.assertEqual({(r.ORIGINAL_VS[v],r.ORIGINAL_PS[p]) for v,p in r.ORIGINAL_PAIRS},PREVIOUS_PAIRS|set(NEW_PAIR_OCCURRENCES))
        self.assertEqual(set(r.ORIGINAL_VS),set(VERTICES))
        self.assertEqual(set(r.ORIGINAL_PS),set(PROFILES))
        self.assertEqual({r.ORIGINAL_PS[p] for p in r.PS21},PS2X)
        for pair,(v,p) in enumerate(r.ORIGINAL_PAIRS):
            self.assertEqual(PROFILES[r.ORIGINAL_PS[p]][-2:],(p in r.AFFINE_PS,v not in r.NO_FADE_VS))
        source=(Path(r.ROOT)/'verification/probe/linear_emission_fixture.cpp').read_text()
        table=source.split('constexpr unsigned actual_pairs[][2] = ',1)[1].split(';',1)[0]
        self.assertEqual(tuple(tuple(map(int,x)) for x in re.findall(r'\{(\d+),(\d+)\}',table)),r.ORIGINAL_PAIRS)

    def test_instance_direct_uv_and_native_fog_are_independent(self):
        default=next(c for c in self.cases if c['actual_profile']==2 and c['label']=='original_sampled_alpha')
        instance=next(c for c in self.cases if c['actual_profile']==6 and c['label']=='original_sampled_alpha')
        self.assertEqual(r.original_uv(instance,(.25,.25)),(.25,.25))
        self.assertEqual(r.original_uv(default,(.25,.25)),(.25,.75))
        self.assertNotEqual(r.mrt_sample(instance['ops'][0],instance,(.25,.25))[0],r.mrt_sample(default['ops'][0],default,(.25,.25))[0])
        for pair in range(5,20):
            if r.ORIGINAL_PAIRS[pair][0] in r.NO_FADE_VS:continue
            c=next(c for c in self.cases if c['actual_profile']==pair and c['label']=='original_vertex_fog' and not c['flags']&(2048|4096))
            o=c['ops'][0]
            self.assertGreater(r.original_fade(o,c,(.25,.75)),0)
            self.assertLess(r.original_fade(o,c,(.25,.75)),o['fade'])

    def test_every_new_pair_decoded_cap_precedes_fade_and_quarter_gain(self):
        rows=[c for c in self.cases if c['label']=='original_decoded_cap_before_scale']
        self.assertEqual({c['actual_profile'] for c in rows},set(range(5,20)))
        for c in rows:
            o=c['ops'][0];fade=1 if r.ORIGINAL_PAIRS[c['actual_profile']][0] in r.NO_FADE_VS else .125
            native,energy=r.mrt_sample(o,c,(.5,.5))
            self.assertEqual(energy[:3],[r.CAP*fade*.25]*3)
            self.assertEqual(native[3],.125)
            self.assertLess(energy[0],r.CAP)

    def test_actual_prefade_decode_and_raw_sampled_alpha(self):
        c=next(c for c in self.cases if c['actual_profile']==0 and c['ops'][0]['gain']==4)
        o=c['ops'][0]
        native,energy=r.mrt_sample(o,c,(.25,.25))
        artistic=.75*.5+.125*.25+.03125
        self.assertEqual(native[3],.125)
        self.assertEqual(energy[3],0.)
        self.assertAlmostEqual(energy[0],r.decode(artistic)*.75*4)
        self.assertGreater(abs(energy[0]-r.decode(native[0])*4),.01)
        changed=copy.deepcopy(o);changed['fade']=.125
        self.assertEqual(r.mrt_sample(changed,c,(.25,.25))[0][3],native[3])

    def test_fog_vertex_interpolation_not_per_pixel_distance(self):
        c=next(c for c in self.cases if c['label']=='original_vertex_fog' and not c['flags']&(2048|4096))
        o=c['ops'][0]
        corners=[r.original_fade(o,c,uv) for uv in ((0,0),(1,0),(0,1),(1,1))]
        self.assertGreater(max(corners)-min(corners),.01)
        self.assertAlmostEqual(r.original_fade(o,c,(.25,.5)),corners[0]*.25+corners[1]*.25+corners[2]*.5)
        self.assertAlmostEqual(r.original_fade(o,c,(.75,.5)),corners[1]*.5+corners[2]*.25+corners[3]*.25)
        # Center world distance is intentionally different from interpolated
        # corner distances, so a pixel-distance oracle cannot silently pass.
        distance=math.sqrt((.125-(.5*(-1/16)+.25))**2+(.25-(.5*(1/16)-.125))**2+4)
        self.assertGreater(abs(r.original_fade(o,c,(.5,.5))-o['fade']*(1.5-.5*distance)),.01)

    def test_fog_clamps_and_absent_fade(self):
        for c in self.cases:
            if c['label']=='original_vertex_fog' and c['flags']&(2048|4096):
                wanted=0. if c['flags']&2048 else c['ops'][0]['fade']
                self.assertEqual(r.original_fade(c['ops'][0],c,(.25,.75)),wanted)
        for p in (3,4,13,14):
            c=next(c for c in self.cases if c['actual_profile']==p)
            o=copy.deepcopy(c['ops'][0]);o['fade']=0
            self.assertEqual(r.original_fade(o,c,(.5,.5)),1.)

    def test_transformed_uv_samples_have_margin(self):
        minimum=1.
        for c in self.cases:
            if not c['flags']&4:continue
            for o in c['ops']:
                for i in r.pixels_in(o,c):
                    if o['z']>=.75:continue
                    offset=4 if c['flags']&16 else 0;size=8 if c['flags']&16 else 16
                    l,t,rr,b=o['rect'];x,y=i%16,i//16
                    uv=((x+.5-offset-l*size)/((rr-l)*size),(y+.5-offset-t*size)/((b-t)*size))
                    mapped=r.original_uv(c,uv)
                    minimum=min(minimum,*[abs(v-.5) for v in mapped])
                    self.assertTrue(all(0<v<1 for v in mapped))
        self.assertGreaterEqual(minimum,1/64)

    def test_rejects_energy_native_alpha_and_nonfinite_corruption(self):
        # Original mode appends native B and E after C/depth in each record.
        source=4+1280*4
        for offset,value in ((source+12,123.),(source+1024*4+12,-0.),
                             (source+1024*4,math.inf),(source+1024*4,123.)):
            raw=bytearray(self.data);struct.pack_into('<f',raw,offset,value)
            with self.assertRaises(AssertionError):r.validate_original_report(self.text,raw,self.cases)
        with self.assertRaises(AssertionError):r.validate_original_report(self.text,self.data[:-4],self.cases)

    def test_rejects_native_parity_count_and_shader_creation_mismatch(self):
        for text in (self.text.replace('native=1024','native=1023',1),
                     self.text.replace('shaders=77','shaders=41'),
                     self.text.replace('postblend=1','postblend=0')):
            with self.assertRaises(AssertionError):r.validate_original_report(text,self.data,self.cases)


if __name__=='__main__':unittest.main()
