"""Bounded actual-original GPU oracle/report checks; no bundled shader bytes."""
import copy
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
    lines.append(f'ORIGINAL_RESULT pass cases={len(cases)} shaders=42')
    return '\n'.join(lines)+'\n',bytes(data)


class OriginalReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.original_cases();cls.text,cls.data=report(cls.cases)

    def test_complete_pair_gain_and_program_record(self):
        result=r.validate_original_report(self.text,self.data,self.cases)
        self.assertEqual(result['cases'],70)
        self.assertEqual(result['original_vertex_programs'],3)
        self.assertEqual(result['original_pixel_programs'],5)
        self.assertEqual(result['source_variants'],25)
        self.assertEqual(result['shader_creations'],42)
        self.assertEqual(result['timings'],[])
        for p in range(5):
            rows=[c for c in self.cases if c['actual_profile']==p]
            self.assertEqual({o['gain'] for c in rows for o in c['ops']},set(r.ORIGINAL_GAINS))
            self.assertTrue(all(bool(o['affine'])==(p in (0,1,3)) for c in rows for o in c['ops']))
        self.assertGreater(result['invariants']['minuszero'],0)
        self.assertGreater(result['invariants']['capzero'],0)

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
        for p in (3,4):
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
                     self.text.replace('shaders=42','shaders=41'),
                     self.text.replace('postblend=1','postblend=0')):
            with self.assertRaises(AssertionError):r.validate_original_report(text,self.data,self.cases)


if __name__=='__main__':unittest.main()
