"""Qualify exact XT source transforms on a local corpus; no GPU behavior claim."""
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
import inspect_motion_output_profiles as motion
import inspect_xt_materials as proof
from verification.analysis.xt_material_reference import default_authored_varyings
PP=0x200000;SAT=0x100000

def reg(kind,n):return 0x80000000|((kind&7)<<28)|((kind&24)<<8)|n
def src(kind,n,sw=0xe4):return reg(kind,n)|(sw<<16)
def dst(kind,n,mask=7):return reg(kind,n)|(mask<<16)
def load(path):return motion.instructions(path.read_bytes())[:2]
def span(w,i):return tuple(w[i['dword']:i['dword']+i['length']+1])
def declarations(items,kind):
    return {(i['words'][0]&31,(i['words'][0]>>16)&15):(motion.register_of(i['words'][1])[1],(i['words'][1]>>16)&15,(i['words'][1]>>20)&15)
            for i in items if i['opcode']==31 and motion.register_of(i['words'][1])[0]==kind}

class XtMaterialTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals=Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals/f'ps_{p[0]}.bin').is_file() for p in proof.PAIRS):raise unittest.SkipTest('local XT originals absent')
        cls.report=json.loads((ROOT/'docs/reverse-engineering/xt-material-profiles.json').read_text())['programs']
        cls.by_hash={p['ps']:p for p in cls.report}
        tmp=tempfile.TemporaryDirectory(prefix='x3-xt-structure-');cls.addClassCleanup(tmp.cleanup);cls.output=Path(tmp.name)
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:raise RuntimeError('host C++ compiler required')
        cls.executable=cls.output/'structure'
        command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/xt_material_structure.cpp'),str(ROOT/'src/renderer/linear_material.cpp'),str(ROOT/'src/renderer/material_motion.cpp'),'-o',str(cls.executable)]
        result=subprocess.run(command,text=True,capture_output=True);assert result.returncode==0,result.stdout+result.stderr
        result=subprocess.run([str(cls.executable),str(cls.originals),str(cls.output)],text=True,capture_output=True);assert result.returncode==0,result.stdout+result.stderr
        cls.driver=json.loads(result.stdout)

    def each(self):
        for file in sorted(self.output.glob('*.bin')):
            program,depth,linear,gain=file.stem.rsplit('-',3)
            w,items=load(file)
            yield program,int(depth),bool(int(linear)),int(gain),w,items

    def test_full_fourteen_pair_scope_alias_and_failure_guards(self):
        self.assertEqual([self.driver[k] for k in ['programs','pairs','variants']],[16,14,168])
        self.assertGreaterEqual(self.driver['checks'],823)
        self.assertEqual(proof.derive(self.originals),self.report)

    def test_native_instructions_survive_only_enumerated_edits(self):
        for program,depth,linear,gain,w,items in self.each():
            original,old=load(self.originals/(program+'.bin'))
            vertex=program.startswith('vs_');bump=program.endswith('37c34a7478544c14') if vertex else self.by_hash[program[3:]]['bump']
            p=None if vertex else self.by_hash[program[3:]]
            transformed=[span(w,i) for i in items];cursor=0
            for i in old:
                at=i['dword'];tokens=list(span(original,i));op=i['opcode']
                if vertex:
                    if op==31:
                        kind,n=motion.register_of(tokens[2])
                        if kind==6 and bump and n==8:continue
                        if kind==6 and bump and n in (3,4):tokens[2]|=8<<16
                        if kind==6 and not bump and n==2:tokens[2]|=12<<16
                    if bump and at in (733,743):tokens[1]=dst(6,4 if at==733 else 3,8)
                    if linear and at==(593 if bump else 428):tokens[0]=(3<<24)|5;tokens[3]=src(0,7);del tokens[4]
                    if linear and at==(608 if bump else 443):tokens[1]=dst(6,8 if bump else 10);tokens[3]=src(0,7)
                else:
                    if op==31:
                        kind,n=motion.register_of(tokens[2])
                        if bump and kind==1 and n==7:continue
                        if bump and kind==1 and n in (2,3):tokens[2]|=8<<16
                    if linear and at in p['rgb']:tokens[1]&=~PP
                    if linear and at in p['clamps']:tokens[1]&=~SAT;tokens[2]=src(1,7 if bump else 8)
                    for operand,lane in p['scalar']:
                        if at<operand<=at+i['length']:tokens[operand-at]=src(1,2+lane,255) if bump else src(0,15,0) if lane==0 else tokens[operand-at]
                    if linear:
                        for operand,c in p['lights']:
                            if at<operand<=at+i['length']:tokens[operand-at]=src(0,12 if c==6 else 13)
                        for operand,c in p['palette']:
                            if at<operand<=at+i['length']:tokens[operand-at]=src(0,14)
                        if at==p['final_rgb']:
                            tokens[1]=dst(0,11)
                            if p['terra']:
                                occ=next(t['register'] for t in p['textures'] if t['sampler']==(5 if bump else 4))
                                tokens[2:]=[src(0,14) if t==src(0,occ) else t for t in tokens[2:]]
                try:cursor=transformed.index(tuple(tokens),cursor)+1
                except ValueError:self.fail((program,depth,linear,gain,'native instruction missing',at))

    def test_original_control_flow_samples_and_alpha_are_exact(self):
        for program,depth,linear,gain,w,items in self.each():
            ow,original=load(self.originals/(program+'.bin'))
            for ops in ((38,39,40,41,42,43),(66,)):
                self.assertEqual([span(w,i) for i in items if i['opcode'] in ops],[span(ow,i) for i in original if i['opcode'] in ops])
            if program.startswith('ps_'):
                p=self.by_hash[program[3:]];alpha=next(i for i in original if i['dword']==p['alpha'])
                self.assertEqual(sum(span(w,i)==span(ow,alpha) for i in items),1)
                self.assertEqual(sum(i['opcode']==31 and motion.register_of(i['words'][1])==(1,0) for i in items),1)
                # All retained v0 reads use alpha only on the linear path.
                if linear:
                    reads=[s for i in items if i['opcode'] not in motion.HEADER_OPCODES for s in motion.split_operands(i,3)[1] if s['name']=='v0']
                    self.assertEqual([(s['swizzle']) for s in reads],['wwww'])

    def test_complete_repaired_and_linear_semantic_linkages(self):
        for p in self.report:
            for depth in (0,1):
                for linear in ((True,) if p['bump'] else (False,True)):
                    vs='37c34a7478544c14' if p['bump'] else '494fe349b8bc12ec'
                    _,vi=load(self.output/f'vs_{vs}-{depth}-{int(linear)}-1.bin')
                    _,pi=load(self.output/f"ps_{p['ps']}-{depth}-{int(linear)}-1.bin")
                    vd,pd=declarations(vi,6),declarations(pi,1)
                    owners={}
                    for semantic,(register,mask,mods) in pd.items():
                        self.assertIn(semantic,vd,(p['ps'],semantic));vreg,vmask,_=vd[semantic]
                        self.assertEqual(mask&~vmask,0,(p['ps'],semantic))
                        self.assertEqual(owners.setdefault(register,vreg),vreg)
                    if linear:self.assertEqual(pd[(10,1)],(7 if p['bump'] else 8,7,0))
                    if p['bump']:
                        self.assertNotIn((5,6),pd)
                        self.assertEqual(pd[(5,1)][1:],(15,2));self.assertEqual(pd[(5,2)][1:],(15,2))
                    else:self.assertEqual(vd[(5,0)][1],15);self.assertIn((5,5),vd);self.assertIn((5,6),vd)

    def test_runtime_palette_conversions_are_inside_original_palette_branch(self):
        for p in self.report:
            w,items=load(self.output/f"ps_{p['ps']}-1-1-1.bin")
            depth=0;seen=[]
            for i in items:
                if i['opcode'] in (40,41):depth+=1
                if i['opcode']==43:depth-=1
                dest,sources=motion.split_operands(i,3)
                if i['opcode']==11 and dest['name']=='r14' and sources[0]['name'].startswith('c'):
                    self.assertEqual(depth,1)
                    seen.append(sources[0]['register'])
            self.assertEqual(seen,[v for _,v in p['palette']])

    def test_caps_and_resource_limits(self):
        maxima={}
        costs=dict.fromkeys(('mov','add','mad','mul','rcp','rsq','dp3','dp4','min','max','slt','abs','cmp','mova','else','endif','exp','log'),1)
        costs.update(nrm=3,pow=3,rep=3,endrep=2,lrp=2,dp2add=2,ifc=3);costs['if']=3
        for program,depth,linear,gain,w,items in self.each():
            samplers={motion.register_of(i['words'][1])[1]:(i['words'][0]>>27)&15 for i in items if i['opcode']==31 and motion.register_of(i['words'][1])[0]==10}
            count=0
            for i in items:
                if i['opcode'] in motion.HEADER_OPCODES:continue
                name=motion.OPCODES[i['opcode']]
                if name=='texld':count+=4 if samplers[motion.register_of(i['words'][-1])[1]]==3 else 1
                else:count+=costs[name]
                d,ss=motion.split_operands(i,3)
                for op in ([d] if d else [])+ss:
                    self.assertLess(op['register'],{0:32,1:16 if program.startswith('vs_') else 10,2:256 if program.startswith('vs_') else 224,6:12,8:4}.get(op['register_type'],2048))
            self.assertLessEqual(count,512)
            key=(program[:2],linear,depth);maxima[key]=max(maxima.get(key,0),count)
        self.maxDiff=None
        self.assertEqual(maxima,{('vs',False,0):79,('vs',False,1):81,('vs',True,0):104,('vs',True,1):106,('ps',False,0):136,('ps',False,1):138,('ps',True,0):296,('ps',True,1):298})

    def test_authored_default_geometry_matches_independent_reference(self):
        w,items=load(self.output/'vs_494fe349b8bc12ec-1-0-1.bin')
        fragment=items[-14:]
        self.assertEqual([i['opcode'] for i in fragment],[1,36,36,8,2,4,35,8,5,5,5,5,2,5])
        for view,normal in [((0,0,4),(0,0,2)),((2,0,0),(0,0,3)),((2,1,4),(1,3,2)),((-4,2,-1),(1,1,3))]:
            registers={'r0':list(view)+( [0] ),'r2':list(normal)+[0],'v1':[0.2,0.4,0.7,0.9],'c244':[1,0,0,0]}
            for i in fragment:
                dest,sources=motion.split_operands(i,3);values=[]
                for k,source in enumerate(sources):
                    value=[registers[source['name']]['xyzw'.index(c)] for c in source['swizzle']]
                    if (i['words'][k+1]>>24)&15==1:value=[-x for x in value]
                    values.append(value)
                op=i['opcode']
                if op==36:
                    norm=math.sqrt(sum(x*x for x in values[0][:3]));result=[x/norm for x in values[0]]
                elif op==8:result=[sum(a*b for a,b in zip(values[0][:3],values[1][:3]))]*4
                elif op==1:result=values[0]
                elif op==35:result=[abs(x) for x in values[0]]
                elif op==2:result=[a+b for a,b in zip(*values)]
                elif op==5:result=[a*b for a,b in zip(*values)]
                elif op==4:result=[a*b+c for a,b,c in zip(*values)]
                else:self.fail(op)
                if i['words'][0]&SAT:result=[min(1,max(0,x)) for x in result]
                target=registers.setdefault(dest['name'],[0]*4)
                for c in dest['mask']:target['xyzw'.index(c)]=result['xyzw'.index(c)]
            reference=default_authored_varyings(view,normal,1)
            self.assertEqual(registers['o2'][2:],[0.7,0.9])
            for got,want in zip(registers['o8'][:3],reference.palette_weights):self.assertAlmostEqual(got,want)
            self.assertAlmostEqual(registers['o9'][1],reference.highlight_weight)
            self.assertAlmostEqual(registers['o9'][0],reference.fresnel_shape)

if __name__=='__main__':unittest.main()
