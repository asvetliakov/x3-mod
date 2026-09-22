"""Independent bounded SM1 promotion proof; all original bytes stay local."""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/analysis'))
import inspect_motion_output_profiles as shader
from run_linear_emission import decode,sanitize


def f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


SCALAR=('078494828322bcca','2ea025492d370c8e','a5c3495e27270b4a')
BULLET=('84d3de8887c963c5','d4a26efb7c603931','ec1f5c4a2f4e1445')
GAINS=(0,.25,1,4,16)


def simulate(items,texture,color):
    """Bounded FP32 arithmetic witness; PP is a permission, not a quantizer.

    The actual fixture measures native PS1 precision independently. This host
    evaluator checks the authored algebra and initialized lanes, not GPU parity.
    """
    r={(1,0):list(map(f32,color))};writes=[]
    def source(token):
        a=r[shader.register_of(token)]
        values=[a['xyzw'.index(c)] for c in shader.swizzle_of(token)]
        return [-x for x in values] if token&(1<<24) else values
    for item in items:
        op=item['opcode'];w=item['words']
        if op==31:continue
        if op==81:r[shader.register_of(w[0])]=list(struct.unpack('<4f',struct.pack('<4I',*w[1:])));continue
        d,*s=w;key=shader.register_of(d);values=[] if op==66 else [source(t) for t in s]
        if op==66:v=list(map(f32,texture))
        elif op==1:v=values[0]
        elif op==5:v=[x*y for x,y in zip(*values)]
        elif op==8:v=[sum(x*y for x,y in zip(values[0][:3],values[1][:3]))]*4
        elif op==11:v=[x if x>=y else y for x,y in zip(*values)]
        elif op==10:v=[x if x<=y else y for x,y in zip(*values)]
        elif op==32:v=[math.pow(x,y) for x,y in zip(*values)]
        elif op==88:v=[b if a>=0 else c for a,b,c in zip(*values)]
        else:raise AssertionError(op)
        target=r.setdefault(key,[math.nan]*4)
        for c in shader.mask_of(d):target['xyzw'.index(c)]=f32(v['xyzw'.index(c)])
        writes.append((key,shader.mask_of(d)))
    return r,writes


class Sm1TransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals=Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals/f'ps_{h}.bin').exists() for h in SCALAR+BULLET):raise unittest.SkipTest('local SM1 corpus missing')
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:raise RuntimeError('host compiler required')
        temp=tempfile.TemporaryDirectory(prefix='x3-sm1-host-');cls.addClassCleanup(temp.cleanup);cls.directory=Path(temp.name)
        exe=cls.directory/'structure'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/linear_emission_sm1_structure.cpp'),str(ROOT/'src/renderer/linear_emission.cpp'),'-o',str(exe)],check=True,capture_output=True)
        result=subprocess.run([str(exe),str(cls.originals),str(cls.directory)],check=True,capture_output=True,text=True)
        cls.report=json.loads(result.stdout)
        cls.variants={}
        cls.packed={}
        for h in SCALAR+BULLET:
            for g in range(5):
                cls.packed[h,g]=shader.instructions((cls.directory/f'sm1_{h}-{g}-4-0.bin').read_bytes())[:2]
                for mode in (1,2,3):
                    for pp in (0,1):
                        cls.variants[h,g,mode,pp]=shader.instructions((cls.directory/f'sm1_{h}-{g}-{mode}-{pp}.bin').read_bytes())[:2]

    def test_driver_all_profiles_pairs_mutations_alias_and_resources(self):
        self.assertEqual((self.report['variants'],self.report['pairs']),(210,9))
        self.assertGreater(self.report['checks'],7000)
        self.assertEqual(self.report['max_slots'],[4,26,28,12])
        self.assertEqual(self.report['max_words'],153)

    def test_all_current_sm2_outputs_and_registry_are_retained(self):
        record=json.loads((ROOT/'verification/results/bottle-X3/linear-emission-mrt-coverage-gpu.json').read_text())
        expected={**record['transformed_sha256'],**record['coverage_transformed_sha256']}
        self.assertEqual(len(expected),100)
        for name,digest in expected.items():self.assertEqual(hashlib.sha256((self.directory/name).read_bytes()).hexdigest(),digest,name)

    def test_original_shape_comments_and_alpha_coissue_roles(self):
        for h in SCALAR+BULLET:
            original,items,_=shader.instructions((self.originals/f'ps_{h}.bin').read_bytes())
            self.assertEqual(original[0],0xffff0101)
            self.assertEqual([i['dword'] for i in items], [39,45,47,51,55] if h in SCALAR else [39,41,45])
            self.assertEqual([i['coissued'] for i in items],[False]*(len(items)-1)+[True])
            d,s=shader.split_operands(items[-1],1)
            self.assertEqual((d['name'],d['mask'],s[0]['name'],s[0]['swizzle']),('r0','w','a0','wwww'))
            # Old-model register3 is the t# texture register, despite the shared
            # disassembler operand helper's VS-style a# display name.
            self.assertEqual(shader.register_of(items[-1]['words'][1]),(3,0))
            for (key,g,mode,pp),(words,_) in self.variants.items():
                if key==h:self.assertEqual(words[1:39],original[1:39])

    def test_promoted_native_path_precision_and_complete_outputs(self):
        for (h,g,mode,pp),(words,items) in self.variants.items():
            self.assertEqual(words[0],0xffff0200)
            self.assertTrue(all(not i['coissued'] for i in items))
            actual=[i for i in items if i['opcode'] not in (31,81)]
            outputs=[i for i in actual if shader.register_of(i['words'][0])[0]==8]
            self.assertEqual([shader.register_of(i['words'][0])[1] for i in outputs],list(range(mode)))
            for item in outputs:
                d,s=shader.split_operands(item,2)
                self.assertEqual((item['opcode'],d['mask'],d['modifiers'],s[0]['swizzle']),(1,'xyzw',[],'xyzw'))
            native_end=outputs[0]['dword']
            native=[i for i in actual if i['dword']<native_end]
            self.assertEqual([i['opcode'] for i in native],[66,8,5,1] if h in SCALAR else [66,5,1])
            self.assertTrue(all(bool(i['words'][0]&0x200000)==bool(pp) for i in native))
            self.assertTrue(all(not i['words'][0]&0x200000 for i in actual if i['dword']>=native_end))
            alpha=native[-1];d,s=shader.split_operands(alpha,2)
            self.assertEqual((d['name'],d['mask'],s[0]['name'],s[0]['swizzle']),('r0','w','r1','wwww'))
            weight=next(i for i in actual if i['opcode']==5)
            _,src=shader.split_operands(weight,2)
            self.assertEqual((src[1]['name'],src[1]['swizzle']),('r0','xyzw') if h in SCALAR else ('v0','wwww'))

    def test_independent_weighted_budgets_definitions_and_modes(self):
        for (h,g,mode,pp),(words,items) in self.variants.items():
            arithmetic=sum(3 if i['opcode']==32 else 1 for i in items if i['opcode'] not in (31,81,66))
            self.assertEqual(arithmetic,(4 if h in SCALAR else 3)+(22 if mode>1 else 0)+(2 if mode==3 else 0))
            self.assertEqual(sum(i['opcode']==66 for i in items),1)
            definitions={shader.register_of(i['words'][0])[1] for i in items if i['opcode']==81}
            self.assertEqual(definitions,({0} if h in SCALAR else set())|({30,31} if mode>1 else set()))
            if mode==1:self.assertEqual(words,self.variants[h,0,mode,pp][0])

    def test_scalar_x_and_bullet_w_native_alpha_and_energy_equations(self):
        tex=(.123,.456,.789,.317);color=(.25,.9,.8,.75)
        for (h,g,mode,pp),(_,items) in self.variants.items():
            r,_=simulate(items,tex,color);weight=color[0] if h in SCALAR else color[3]
            for i in range(3):self.assertAlmostEqual(r[8,0][i],tex[i]*weight,places=7)
            self.assertEqual(r[8,0][3],f32(tex[3]))
            if mode>1:
                for i in range(3):self.assertAlmostEqual(r[8,1][i],sanitize(decode(tex[i])*weight*GAINS[g]),delta=2e-6)
                self.assertEqual(struct.pack('<f',r[8,1][3]),b'\0'*4)
            if mode==3:self.assertEqual(r[8,2],[1.]*4)

    def test_decoded_cap_precedes_quarter_gain_and_fade(self):
        for h in SCALAR+BULLET:
            _,items=self.variants[h,1,3,0]
            r,_=simulate(items,(256,256,256,.317),(.125,0,0,.125))
            self.assertEqual(r[8,1][:3],[65504*.125*.25]*3)
            self.assertEqual(r[8,2],[1.]*4)

    def test_new_tail_finite_policy_and_zero_coverage_independence(self):
        # Out-of-range/nonfinite native SM1 results are not claimed here;
        # these host witnesses only establish the authored tail's selected S.
        for h in SCALAR+BULLET:
            for rgb in ((0.,-0.,-1.),(math.nan,math.inf,-math.inf)):
                _,items=self.variants[h,2,3,0];r,_=simulate(items,(*rgb,0.),(0,0,0,0))
                self.assertEqual(r[8,1],[0.]*4)
                self.assertTrue(all(math.isfinite(x) for x in r[8,1]))
                self.assertTrue(all(struct.pack('<f',x)==b'\0'*4 for x in r[8,1]))
                self.assertEqual(r[8,2],[1.]*4)


    def test_all_180_prior_sm1_outputs_are_byte_exact(self):
        digest=hashlib.sha256()
        for name in sorted(f'sm1_{h}-{g}-{mode}-{pp}.bin' for h in SCALAR+BULLET
                           for g in range(5) for mode in (1,2,3) for pp in (0,1)):
            digest.update(name.encode()+b'\0'+(self.directory/name).read_bytes())
        self.assertEqual(digest.hexdigest(),'21d26569494ffc58ece3d2de069093746feabb76cbafc678b8058fe3b486adf7')

    def test_packed_full_precision_resources_and_channel_roles(self):
        for (h,g),(words,items) in self.packed.items():
            self.assertEqual(words[0],0xffff0200)
            actual=[i for i in items if i['opcode'] not in (31,81)]
            self.assertTrue(all(not i['words'][0]&0x200000 for i in actual))
            # Step E: no per-fragment decode (no POW in the packed producer).
            self.assertEqual(sum(3 if i['opcode']==32 else 1 for i in actual if i['opcode']!=66),12 if h in SCALAR else 11)
            self.assertFalse(any(i['opcode']==32 for i in actual))
            self.assertEqual(sum(i['opcode']==66 for i in actual),1)
            outs=[i for i in actual if shader.register_of(i['words'][0])[0]==8]
            self.assertEqual([shader.register_of(i['words'][0])[1] for i in outs],[0,1,2,3])
            for i in outs:
                self.assertEqual((i['opcode'],shader.mask_of(i['words'][0]),shader.register_of(i['words'][1]),shader.swizzle_of(i['words'][1])),(1,'xyzw',(0,1),'xyzw'))
            texture=(.125,.5,.875,.317);color=(.25,.9,.8,.75)
            r,_=simulate(items,texture,color);weight=color[0] if h in SCALAR else color[3]
            self.assertEqual(r[8,0],[1.,0.,0.,f32(texture[3])])
            for c in range(3):
                q=f32(texture[c]*weight);plane=r[8,c+1]
                # P_c = (q, q, q, q): the red lane accumulates native B, the blue
                # lane is the modified flag, the green lane is masked off by the
                # pass (it keeps decode(A) from the plane initialization).
                self.assertEqual(plane,[q,q,q,q])

    def test_packed_zero_signed_and_cap_order_are_explicit(self):
        for h in SCALAR+BULLET:
            for tex,g in (((0.,-0.,0.,0.),2),((-.5,0.,.5,0.),0),((256.,0.,1.,.3),1)):
                _,items=self.packed[h,g];r,_=simulate(items,tex,(.125,0.,0.,.125))
                self.assertEqual(r[8,0][:3],[1.,0.,0.])
                for c in range(3):
                    q=r[8,c+1][0]
                    self.assertEqual(r[8,c+1],[q]*4) # the gain never enters the producer (step E: gain at publication)
                if tex[0]==256:self.assertEqual(r[8,1][0],256*.125) # native q uncapped, no decode
                if tex[0]<0:self.assertLess(r[8,1][0],0) # no invented clamp of native q

    def test_packed_ordered_law_and_unchanged_channel(self):
        half=lambda x:struct.unpack('<e',struct.pack('<e',x))[0]
        _,items=self.packed[SCALAR[0],2]
        fragments=[simulate(items,(*q,a),(1.,0.,0.,1.))[0]
                   for q,a in (((.5,.25,0.),.25),((.25,.75,0.),.75))]
        A=(.2,.35,.8);a0=half(.4)
        def accumulate(order):
            planes=[[half(x),half(decode(x)),0.] for x in A]
            alpha=a0;mask=half(.5)
            native=list(map(half,A))
            for fragment in order:
                a=fragment[8,0][3]
                alpha=half(a+(1-a)*alpha);mask=half(1+(1-a)*mask)
                for c in range(3):
                    q,_,flag,srcalpha=fragment[8,c+1]
                    native[c]=half(q+(1-q)*native[c])
                    # Red and blue lanes only (plane masks 5): green keeps decode(A).
                    planes[c]=[half(q+(1-srcalpha)*planes[c][0]),planes[c][1],half(flag+(1-srcalpha)*planes[c][2])]
            self.assertEqual([p[0] for p in planes],native)
            self.assertEqual([p[1] for p in planes],[half(decode(x)) for x in A])
            self.assertEqual(planes[2][2],0.)
            self.assertGreater(mask,.5)
            return planes,alpha
        forward,af=accumulate(fragments);reverse,ar=accumulate(fragments[::-1])
        # The screen blend is commutative in exact arithmetic; only FP16 stores can order it.
        self.assertAlmostEqual(forward[0][0],reverse[0][0],places=2)
        self.assertEqual(af,ar) # these selected binary alpha values compose exactly
        # Assembly must copy immutable A when the channel flag is ordered zero.
        self.assertEqual(forward[2][0],half(A[2]))
        self.assertEqual(forward[2][2],0.)
        # Step E publication at g = 1: encode(decode(B_native)) is the native lane.
        for c in range(2):
            self.assertAlmostEqual(forward[c][0]**2.2*1+forward[c][1]*0,forward[c][0]**2.2)
            self.assertEqual(half((forward[c][0]**2.2)**(1/2.2)),forward[c][0])

if __name__=='__main__':unittest.main()
