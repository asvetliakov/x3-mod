"""Create-time selective bytecode and independently separated source witnesses.

Uses local game programs only. Float64 execution is an algebra/source oracle,
not partial-precision, FP16 blending, native D3D or GPU qualification.
"""
import copy
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.analysis import inspect_motion_output_profiles as motion
from verification.analysis import xt_pixel_execution as vm
from verification.analysis.test_linear_material_constant_port import weighted_slots, constant_sources

ROOT=Path(__file__).resolve().parents[2]
BASELINE='070df80'

def unpack(data):
    words,items,_=motion.instructions(data)
    return list(words),items

def pack(words):return struct.pack('<%dI'%len(words),*words)
def span(words,i):return words[i['dword']:i['dword']+i['length']+1]
def reg(token):return motion.register_of(token)
def token(kind,index,mask=15):return 0x80000000|((kind&7)<<28)|((kind&24)<<8)|index|(mask<<16)
def source(kind,index,swizzle=228):return token(kind,index,swizzle)

def original_roles(data,directions):
    """Independent lane ancestry: actual saturated N.L versus reflected-view DP3.

    Native angular clamp/multiply chains propagate the two roles separately;
    this never uses the production seed table, directional fraction, or RGB
    subtraction. The first diffuse/specular addition closes their ancestry.
    """
    _,items=unpack(data);state={};joins={}
    for item in items:
        if item['opcode'] in motion.HEADER_OPCODES:continue
        dst,src=motion.split_operands(item,3)
        if not dst:continue
        updates={}
        for lane in dst['mask']:
            terms=[state.get((s['name'],s['swizzle']['xyzw'.index(lane)]),0) for s in src]
            if item['opcode']==8:
                role=1 if 'saturate' in dst['modifiers'] and any(s['name'] in directions for s in src) else 2
            elif item['opcode']==66:role=0
            else:
                addends=terms if item['opcode']==2 else [terms[0]|terms[1],terms[2]] if item['opcode']==4 else []
                if len(addends)==2 and ((addends[0]==1 and addends[1]&2) or (addends[1]==1 and addends[0]&2)):
                    key=(item['dword'],addends[0]==1)
                    joins[key]=joins.get(key,'')+lane;role=4
                else:
                    role=0
                    for term in terms:role|=term
            updates[dst['name'],lane]=role
        state.update(updates)
    return joins

class MaterialExposureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.corpus=Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not cls.corpus.is_dir():raise unittest.SkipTest('local shader corpus unavailable')
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:raise RuntimeError('host C++ compiler required')
        temp=tempfile.TemporaryDirectory(prefix='x3-material-exposure-');cls.addClassCleanup(temp.cleanup)
        cls.work=Path(temp.name);old=cls.work/'old';old.mkdir()
        for name in ('linear_material.cpp','linear_xt_material_inc.h'):
            (old/name).write_bytes(subprocess.check_output(['git','show',f'{BASELINE}:src/renderer/{name}'],cwd=ROOT))
        cls.reports={}
        for label,cpp,selective in [('ordinary',ROOT/'src/renderer/linear_material.cpp',0),('selective',ROOT/'src/renderer/linear_material.cpp',1),('baseline',old/'linear_material.cpp',0)]:
            exe=cls.work/label
            if label!='selective':
                subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/renderer'),str(ROOT/'verification/probe/material_exposure_structure.cpp'),str(cpp),str(ROOT/'src/renderer/material_motion.cpp'),'-o',str(exe)],check=True,capture_output=True,text=True)
            else:exe=cls.work/'ordinary'
            output=cls.work/(label+'-output')
            run=subprocess.run([str(exe),str(cls.corpus),str(output),str(selective)],check=True,capture_output=True,text=True)
            cls.reports[label]=json.loads(run.stdout)
        cls.seeds={h:(int(at),int(mask),product=='true') for h,at,mask,product in re.findall(r'\{0x([a-f0-9]+)ull,(\d+),(\d+),(true|false)\}',(ROOT/'src/renderer/material_exposure_profiles_inc.h').read_text())}
        cls.profile={p['id'][3:]:p for p in json.loads((ROOT/'docs/reverse-engineering/linear-material-profiles.json').read_text())['programs']}

    def path(self,label,name,depth=1,gain=1,fill=1):return self.work/(label+'-output')/f'{name}-{depth}-{gain}-{fill}.bin'

    def test_all_137_programs_atomic_alias_and_exact_ordinary_regression(self):
        for report in self.reports.values():self.assertEqual(report,{'programs':137,'variants':2192,'guards':411})
        for path in (self.work/'ordinary-output').glob('*.bin'):
            self.assertEqual(path.read_bytes(),(self.work/'baseline-output'/path.name).read_bytes(),path.name)

    def test_diffuse_seed_ancestry_independent_of_production_plan(self):
        self.assertEqual(len(self.seeds),108)
        for h,(at,mask,product) in self.seeds.items():
            if h in self.profile:
                lights={p['name'] for p in self.profile[h]['directional_rgb_sources']}
            else:lights={'c6','c8'} if h not in {'a66fb1981ba755b2','ebc9b2b3f1564e9a','f31c9e2701c8eee4','9d49f288800f898d'} else {'c1','c3'}
            directions={'c'+str(int(c[1:])-1) for c in lights}
            self.assertEqual(original_roles((self.corpus/f'ps_{h}.bin').read_bytes(),directions),{(at,product):''.join(c for n,c in enumerate('xyzw') if mask&(1<<n))},h)

    def test_constants_slots_alpha_and_temporal_writes(self):
        slots=[];extra=[];vs_count=0;ps_count=0
        for path in (self.work/'selective-output').glob('*.bin'):
            before=self.work/'ordinary-output'/path.name;bw,bi=unpack(before.read_bytes());sw,si=unpack(path.read_bytes())
            vertex=path.name.startswith('vs_');reserved=250 if vertex else 222
            for item in bi:
                if item['opcode']==31:continue
                operands=item['words'][:1] if item['opcode']==81 else item['words']
                self.assertNotIn((2,reserved),[reg(w) for w in operands],path.name)
                self.assertNotIn((0,16),[reg(w) for w in operands],path.name)
            for item in si:
                if not vertex:self.assertLessEqual(len(constant_sources(item)),1,(path.name,item['dword']))
            def outputs(words,items):
                return [span(words,i) for i in items if i['opcode'] not in motion.HEADER_OPCODES and i['words'] and reg(i['words'][0])[0]==8 and not(reg(i['words'][0])==(8,0) and motion.mask_of(i['words'][0])=='xyz') and not(reg(i['words'][0])==(8,2) and motion.mask_of(i['words'][0])=='y')]
            if not vertex:
                self.assertEqual(outputs(bw,bi),outputs(sw,si),path.name)
                self.assertEqual((si[-1]['opcode'],reg(si[-1]['words'][0]),motion.mask_of(si[-1]['words'][0])),(1,(8,2),'y'))
                ps_count+=1
            else:
                point=[i for i in si if i['opcode']==5 and any(reg(w)==(2,250) for w in i['words'][1:])]
                self.assertEqual(len(point),1,path.name);self.assertEqual(reg(point[0]['words'][0]),(0,16));vs_count+=1
            cost=weighted_slots(path);delta=cost-weighted_slots(before)
            self.assertLessEqual(cost,512,path.name);slots.append(cost);extra.append(delta)
        print('Selective material evidence: '+json.dumps({'programs':137,'vs_variants':vs_count,'ps_variants':ps_count,'weighted_slots':[min(slots),max(slots)],'added_slots':[min(extra),max(extra)]}))

    def test_emitted_all_family_B_over_e_plus_H_without_subtraction(self):
        xt={p['ps']:p for p in json.loads((ROOT/'docs/reverse-engineering/xt-material-profiles.json').read_text())['programs']}
        arithmetic=vm._result
        def result(op,values):
            if op=='pow':return (abs(values[0][0])**values[1][0],)*4
            if op=='exp':return (2.**values[0][0],)*4
            if op=='log':return (math.log2(abs(values[0][0])),)*4
            return arithmetic(op,values)
        def header(words,items):
            return [words[0]]+[w for i in items if i['opcode']==81 for w in span(words,i)]
        comparisons=0;max_error=0.
        with patch.object(vm,'SUPPORTED',vm.SUPPORTED|{'exp','log'}),patch.object(vm,'_result',result):
            for ordinal,(h,(at,lanes,product)) in enumerate(self.seeds.items()):
                ow,oi=unpack((self.corpus/f'ps_{h}.bin').read_bytes());original=next(i for i in oi if i['dword']==at)
                for gain in (0,1,4,16):
                    for fill in (0,1):
                        bw,bi=unpack(self.path('ordinary','ps_'+h,gain=gain,fill=fill).read_bytes())
                        sw,si=unpack(self.path('selective','ps_'+h,gain=gain,fill=fill).read_bytes())
                        joins=[i for i in bi if i['opcode']==4 and i['words'][1:]==original['words'][1:] and (i['words'][0]&~0x200000)==(original['words'][0]&~0x200000)]
                        self.assertEqual(len(joins),1,h);join=joins[0]
                        pre=header(bw,bi);tail=bw[join['dword']:]
                        destination,a,b,c=join['words']
                        diffuse=[(3<<24)|5,destination,a,b] if product else [(2<<24)|1,destination,c]
                        high=[(2<<24)|1,destination,c] if product else [(3<<24)|5,destination,a,b]
                        base_code=pack(pre+diffuse+tail[5:]);high_words=pre+high+tail[5:]
                        # H has no fill; retain all its unrelated original DEFs.
                        _,hi=unpack(pack(high_words))
                        for item in hi:
                            if item['opcode']==81 and reg(item['words'][0])==(2,215):high_words[item['dword']+2]=0
                        high_code=pack(high_words)
                        start=next(i['dword'] for i in si if i['opcode']==5 and reg(i['words'][0])==(0,16))
                        selected_code=pack(header(sw,si)+sw[start:])
                        rgb_input=next(reg(i['words'][1])[1] for i in bi if i['opcode']==31 and i['words'][0]==0x8001000a)
                        if h in self.profile:
                            emission=[p['sampler'] for p in self.profile[h]['texture_sources'] if p['role'] in ('reflection_cube_rgb','lightmap_emissive_rgb')]
                        elif h in xt:
                            p=xt[h];emission=[4,3] if p['bump'] else [3,2]
                            if p['terra']:emission+=[5 if p['bump'] else 4]
                        else:emission=[2] # opaque glass Fresnel cube
                        for branch in (False,True):
                            rng=random.Random(ordinal)
                            registers={f'{k}{n}':[rng.uniform(.1,.3) for _ in range(4)] for k,count in [('r',32),('v',10),('c',224)] for n in range(count)}
                            registers['c216']=[.2,.3,.4,1.]
                            samplers={n:(.43,.37,.29,.8) for n in range(16)}
                            point=[gain*.04,gain*.07,gain*.02];material=[gain*.03,gain*.01,gain*.08]
                            def state(rgb,e=1.,base=False):
                                st=vm.State(copy.deepcopy(registers),{n:branch for n in range(16)},dict(samplers))
                                st.registers[f'v{rgb_input}']=[*rgb,.6]
                                st.registers['c222']=[1/e,65504/min(e,1.),min(e,1.),0.]
                                if base:
                                    for sampler in emission:st.samplers[sampler]=(0.,0.,0.,.8)
                                return st
                            def execute(code,st):
                                return vm.execute(code,st).registers['oC0']
                            B=execute(base_code,state(point,base=True));H=execute(high_code,state(material))
                            inverse=1/struct.unpack('<f',struct.pack('<I',next(i['words'][2] for i in bi if i['opcode']==81 and reg(i['words'][0])==(2,213))))[0]
                            Blinear=[v**inverse for v in B[:3]];Hlinear=[v**inverse for v in H[:3]]
                            for e in (.125,.5,1.,2.):
                                actual=execute(selected_code,state([p/e+m for p,m in zip(point,material)],e))
                                for q,base,high in zip(actual[:3],Blinear,Hlinear):
                                    error=abs(q**inverse-(base/e+high));max_error=max(max_error,error)
                                    self.assertLessEqual(error,2e-9*max(1.,base/e+high),(h,gain,fill,branch,e))
                                    comparisons+=1
                                self.assertEqual(actual[3],H[3],(h,e))
        print('Independent emitted source components: '+json.dumps({'rgb_comparisons':comparisons,'max_float64_error':max_error,'domain':'post-angular join through palette/albedo/occlusion/emission/encode; no GPU rounding'}))

    def test_allocation_failure_publication_is_atomic(self):
        reports={}
        for name in ('vs_53a0a641107ed76c','vs_badefd5143b3024f','ps_8759c7838bbc86c2','ps_3602b05ce11ca6ff','ps_d51cf763125cb85a'):
            run=subprocess.run([str(self.work/'ordinary'),'--faults',str(self.corpus/(name+'.bin'))],check=True,capture_output=True,text=True)
            report=json.loads(run.stdout);self.assertGreater(report['allocation_failures'],0);reports[name]=report['allocation_failures']
        print('Selective allocation rollback:',json.dumps(reports))

    def test_emitted_finite_ceiling_near_cap_and_original_out_of_domain(self):
        from material_exposure_reference import evaluate
        arithmetic=vm._result
        def ordered(op,values):
            if op=='max':return tuple(a if a>=b else b for a,b in zip(*values))
            if op=='min':return tuple(a if a<=b else b for a,b in zip(*values))
            return arithmetic(op,values)
        checked=0
        with patch.object(vm,'_vec4',lambda v,n:tuple(v)),patch.object(vm,'_result',ordered):
            for h in self.seeds:
                words,items=unpack(self.path('selective','ps_'+h).read_bytes())
                cap=next(n for n,i in enumerate(items) if i['opcode']==10 and any(reg(w)==(2,222) for w in i['words'][1:]))
                self.assertEqual(items[cap-1]['opcode'],11)
                end=next(i for i in items[cap:] if i['opcode']==1 and reg(i['words'][0])==(8,0))
                defs=[w for i in items if i['opcode']==81 for w in span(words,i)]
                code=pack([words[0]]+defs+words[items[cap-1]['dword']:end['dword']+end['length']+1]+[0xffff])
                exponent=struct.unpack('<f',struct.pack('<I',next(i['words'][2] for i in items if i['opcode']==81 and reg(i['words'][0])==(2,213))))[0]
                for e in (.125,.5,1.,2.):
                    oracle=evaluate((65500.,60000.,1e-25),(4.,5000.,1e-26),e)
                    st=vm.State({'r11':[*oracle.q,0.],'c222':[1/e,65504/min(e,1.),min(e,1.),0.]})
                    rgba=vm.execute(code,st,stop_after_oc0=False).registers['oC0']
                    self.assertTrue(oracle.exact_domain)
                    for actual,q in zip(rgba[:3],oracle.q):self.assertAlmostEqual(actual,max(q,1e-22)**exponent,delta=1e-10)
                    checked+=3
                for value in (0.,-0.,-1.,math.nan,-math.inf,math.inf,1e30):
                    st=vm.State({'r11':[value]*4,'c222':[8.,524032.,.125,0.]})
                    rgb=vm.execute(code,st,stop_after_oc0=False).registers['oC0'][:3]
                    self.assertTrue(all(math.isfinite(v) and v>=0 for v in rgb))
                    if value<=0 or math.isnan(value):self.assertEqual(rgb,[0.,0.,0.])
        print('Emitted near-cap/tiny RGB comparisons:',checked)

    def test_fade_scratch_normalization_and_exact_native_outputs(self):
        for h in ('517540ae6d5e5410','7a0c3388065bb08d','d44db87778a43b61','550c2a4d4d3ed70f','64bac8bb307eb896'):
            output=self.work/(h+'-fade.bin')
            subprocess.run([str(self.work/'ordinary'),'--fade',str(self.corpus/('ps_'+h+'.bin')),str(output)],check=True,capture_output=True,text=True)
            original,oi=unpack((self.corpus/('ps_'+h+'.bin')).read_bytes());words,items=unpack(output.read_bytes())
            native=[span(original,i) for i in oi if i['opcode'] not in motion.HEADER_OPCODES and i['words'] and reg(i['words'][0])==(8,0)]
            actual=[span(words,i) for i in items if i['opcode'] not in motion.HEADER_OPCODES and i['words'] and reg(i['words'][0])==(8,0)]
            self.assertEqual(native,actual)
            scale=[i for i in items if i['opcode']==5 and any(reg(w)==(2,222) and motion.swizzle_of(w)=='zzzz' for w in i['words'][1:])]
            self.assertEqual(len(scale),1);self.assertLessEqual(weighted_slots(output),512)
            for i in items:self.assertLessEqual(len(constant_sources(i)),1)
            e=.125;st=vm.State({'r11':[524032.,480000.,1e-25,0.],'c222':[8.,524032.,e,0.]})
            fragment=pack([0xffff0300]+span(words,scale[0])+[0xffff])
            result=vm.execute(fragment,st,stop_after_oc0=False).registers['r11']
            self.assertEqual(result[:3],[65504.,60000.,1e-25*e])

    def test_mutated_seed_clamp_alias_and_constant_reservations(self):
        compiler=shutil.which('clang++') or shutil.which('c++');exe=self.work/'internal'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/material_exposure_probe.cpp'),str(ROOT/'src/renderer/material_motion.cpp'),'-o',str(exe)],check=True,capture_output=True,text=True)
        run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
        report=json.loads(run.stdout);self.assertEqual(report['guard_checks'],54)

    def test_emitted_vertex_point_factor_and_unscaled_emission(self):
        checked=0
        for path in sorted((self.work/'selective-output').glob('vs_*-1-*-0.bin')):
            words,items=unpack(path.read_bytes())
            point=next(n for n,i in enumerate(items) if i['opcode']==5 and any(reg(w)==(2,250) for w in i['words'][1:]))
            final=next(i for i in items[point:] if i['opcode'] in (2,4) and reg(i['words'][0])[0]==6 and any(reg(w)==(0,16) for w in i['words'][1:]))
            emitted=items[point];scalar=final['opcode']==4
            # This fragment uses only stage-common MUL/MAD/MAX/MIN/ABS, with
            # exact VS tokens and DEFs. PS header permits reuse of the host VM;
            # stage legality is checked on the complete real VS separately.
            defs=[w for i in items if i['opcode']==81 for w in span(words,i)]
            code=pack([0xffff0300]+defs+words[emitted['dword']:final['dword']+final['length']+1]+[0xffff])
            emissive=next(i for i in items[point:] if i['opcode']==11 and reg(i['words'][1])[0]==2)
            gain=float(path.stem.split('-')[2])
            for lights in (0,1,8):
                factor=[gain*lights*.03,gain*lights*.07,gain*lights*.05,.4]
                for e in (.125,.5,1.,2.):
                    registers={f'r{n}':[0.,0.,0.,.4] for n in range(32)}
                    pname=motion.name_of(*reg(emitted['words'][1]));registers[pname]=factor[:]
                    mname=motion.name_of(*reg(emissive['words'][1]));registers[mname]=[.2,.1,.3,0.]
                    registers['c250']=[1/e,0.,0.,0.]
                    attenuation=vm._source(vm.State(registers),final['words'][2])[0] if scalar else 1.
                    output=vm.execute(code,vm.State(registers),stop_after_oc0=False).registers[motion.name_of(*reg(final['words'][0]))]
                    expected=[p*attenuation/e+gain*m for p,m in zip(factor,[.2,.1,.3])]
                    for value,want in zip(output,expected):self.assertAlmostEqual(value,want,delta=1e-12);checked+=1
        print('Emitted point/emissive RGB comparisons:',checked)
