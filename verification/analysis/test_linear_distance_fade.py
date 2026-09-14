"""Host feasibility of the detached six-pair producer; no device/render claims."""
import os
import hashlib
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

PAIRS=(('b0602757fce6e870','517540ae6d5e5410'),
       ('0c223ad11bce02d5','7a0c3388065bb08d'),
       ('233d17d26ce0c1fc','7a0c3388065bb08d'),
       ('167eb2d5629ab9d3','d44db87778a43b61'),
       ('330ceb9dd874ede2','550c2a4d4d3ed70f'),
       ('12b8a13f13fe8cfe','550c2a4d4d3ed70f'))
IDS=sorted({f'{stage}_{pair[i]}' for pair in PAIRS for i,stage in enumerate(('vs','ps'))})

def parse(data):
    words,items,_=motion.instructions(data)
    return words,items

def token_span(words,item):
    return list(words[item['dword']:item['dword']+item['length']+1])

def body(data):
    words,items=parse(data)
    return [token_span(words,i) for i in items if i['opcode'] not in motion.HEADER_OPCODES]

def declarations(data,kind):
    _,items=parse(data)
    return {(i['words'][0]&31,(i['words'][0]>>16)&15):
            (motion.register_of(i['words'][1])[1],motion.mask_of(i['words'][1]))
            for i in items if i['opcode']==motion.DCL and motion.register_of(i['words'][1])[0]==kind}

def stats(data):
    _,items=parse(data)
    slots=0; temps=set(); samplers=set()
    costs=dict.fromkeys(('mov','add','mad','mul','rcp','rsq','dp3','dp4','min','max','slt','abs','cmp','mova','else','endif','exp','log'),1)
    costs.update(nrm=3,pow=3,rep=3,**{'if':3},endrep=2,lrp=2,dp2add=2,texld=1)
    for i in items:
        if i['opcode'] in motion.HEADER_OPCODES: continue
        slots+=costs[motion.OPCODES[i['opcode']]]
        destination,sources=motion.split_operands(i,3)
        for operand in ([destination] if destination else [])+sources:
            if operand['register_type']==0: temps.add(operand['register'])
            if operand['register_type']==10: samplers.add(operand['register'])
    return slots,max(temps,default=-1)+1,len(samplers)

class DistanceFadeProducer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.programs=Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not all((cls.programs/(name+'.bin')).is_file() for name in IDS):
            raise unittest.SkipTest('Local ten-program Asteroid corpus unavailable')
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler: raise RuntimeError('Host compiler required')
        cls.temp=tempfile.TemporaryDirectory(prefix='x3-distance-fade-host-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work=Path(cls.temp.name)
        cls.driver=cls.work/'driver'
        baseline=cls.work/'baseline.cpp'
        baseline.write_bytes(subprocess.check_output(['git','show','2cf65ae:src/renderer/linear_material.cpp'],cwd=ROOT))
        common=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/renderer'),
                str(ROOT/'verification/probe/linear_distance_fade_structure.cpp'),str(ROOT/'src/renderer/material_motion.cpp')]
        for extra,target in (([str(ROOT/'src/renderer/linear_material.cpp')],cls.driver),
                             ([str(baseline),'-DX3M_FADE_BASELINE'],cls.work/'baseline')):
            subprocess.run(common+extra+['-o',str(target)],check=True,capture_output=True,text=True)
        cls.generated={}
        for name in IDS:
            code,content=cls.export(name)
            if code: raise AssertionError((name,code))
            cls.generated[name]=content

    @classmethod
    def export(cls,name,mode=1,gain=1,depth=0,baseline=False,path=None):
        output=cls.work/'last.bin'
        result=subprocess.run([str(cls.work/'baseline' if baseline else cls.driver),
            str(path or cls.programs/(name+'.bin')),str(output),str(mode),str(gain),str(depth)],
            check=True,capture_output=True,text=True)
        return int(result.stdout.split()[0]),output.read_bytes()

    def test_exact_pair_sampler_contract(self):
        expected = {pair: 7 if index < 3 else 15 for index, pair in enumerate(PAIRS)}
        vertices = [pair[0] for pair in PAIRS] + ['0', '836022c003f4cf37']
        pixels = sorted({pair[1] for pair in PAIRS}) + ['0', 'a66fb1981ba755b2']
        for vs in vertices:
            for ps in pixels:
                actual = subprocess.check_output([str(self.driver), '--pair-mask', vs, ps], text=True)
                self.assertEqual(int(actual), expected.get((vs, ps), 0), (vs, ps))

    def test_native_body_and_direct_pp_alpha_are_exact(self):
        for name,generated in self.generated.items():
            original=body((self.programs/(name+'.bin')).read_bytes())
            emitted=body(generated)
            cursor=0; alpha=0
            for instruction in original:
                self.assertEqual(emitted[cursor],instruction,name);cursor+=1
                if name.startswith('ps') and motion.register_of(instruction[1])==(8,0) and motion.mask_of(instruction[1])=='w':
                    self.assertEqual(instruction[0]&0xffff,5)
                    self.assertTrue(instruction[1]&(1<<21))
                    duplicate=instruction.copy();duplicate[1]=(duplicate[1]&~2047)|1
                    self.assertEqual(emitted[cursor],duplicate,name);cursor+=1;alpha+=1
            self.assertEqual(alpha,0 if name.startswith('vs') else 1)
            flow=lambda rows:[i for i in rows if (i[0]&0xffff) in (38,39,40,41,42,43)]
            self.assertEqual(flow(emitted),flow(original)*2,'native and replay control-flow topology')
            for instruction in emitted[cursor:]:
                if len(instruction)<2: continue
                kind,index=motion.register_of(instruction[1])
                if name.startswith('ps') and kind==8:
                    self.assertIn(index,(1,2))
                    self.assertEqual(motion.mask_of(instruction[1]),'xyz' if index==1 else 'xyzw')
                if name.startswith('vs') and kind==6:
                    self.assertEqual(index,declarations(generated,6)[(10,1)][0])

    def test_budgets_linkage_and_no_temporal_outputs(self):
        maxima=[0,0]
        for vs,ps in PAIRS:
            v=self.generated['vs_'+vs];p=self.generated['ps_'+ps]
            vd,pd=declarations(v,6),declarations(p,1)
            for semantic,(_,mask) in pd.items():
                self.assertIn(semantic,vd)
                self.assertTrue(set(mask)<=set(vd[semantic][1]))
            for stage,data in enumerate((v,p)):
                slots,temps,samplers=stats(data)
                self.assertLessEqual(slots,512);self.assertLessEqual(temps,32)
                self.assertLessEqual(samplers,4)
                maxima[stage]=max(maxima[stage],slots)
            for kind,data in ((6,v),(1,p)):
                original=(self.programs/(('vs_'+vs if kind==6 else 'ps_'+ps)+'.bin')).read_bytes()
                self.assertEqual(set(declarations(data,kind))-set(declarations(original,kind)),{(10,1)})
        print('Distance-fade maximum weighted VS/PS slots:',maxima)

    def test_alias_config_and_failure_atomicity(self):
        for name in IDS:
            self.assertEqual(self.export(name,2)[1],self.generated[name])
            for gain in (0,16): self.assertEqual(self.export(name,gain=gain)[0],0)
            for gain in ('nan','inf',-1,17): self.assertEqual(self.export(name,gain=gain)[0],2)
        self.assertEqual(self.export('vs_53a0a641107ed76c')[0],3)
        self.assertEqual(self.export('ps_63f96eba9eea7880')[0],3)
        damaged=bytearray((self.programs/(IDS[0]+'.bin')).read_bytes());damaged[-8]^=1
        path=self.work/'damaged.bin';path.write_bytes(damaged)
        self.assertEqual(self.export(IDS[0],path=path)[0],3)

    def test_existing_opaque_programs_remain_byte_exact(self):
        # Comparison is scoped to all originals accepted by the existing ordinary
        # API. XT repaired APIs and emission sources are unchanged source files.
        accepted=0
        for path in sorted(self.programs.glob('*.bin')):
            code,expected=self.export(path.stem,0,baseline=True)
            if code: continue
            accepted+=1
            self.assertEqual(self.export(path.stem,0),(code,expected))
            for depth,gain in ((1,0),(1,16)):
                self.assertEqual(self.export(path.stem,0,gain,depth),self.export(path.stem,0,gain,depth,True))
        self.assertGreaterEqual(accepted,115)
        print('Existing opaque originals byte-exact:',accepted,'x3 configurations')

    def test_frozen_composite_budget_and_endpoint_branches(self):
        sys.path.insert(0,str(ROOT/'verification/probe'))
        import run_linear_distance_fade as runner
        output=self.work/'composite.bin'
        subprocess.run([str(self.driver),'--composite',str(output)],check=True)
        data=output.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),runner.COMPOSITE_SHA256)
        self.assertEqual(len(data),572,'prototype 1: 143 DWORDs, B sample removed')
        _,items=parse(data)
        self.assertEqual(motion.nesting(items),(True,2,0))
        # Prototype 1 samples only A (s0) and Q,q (s1); alpha is A.a, never B.
        self.assertEqual(sorted(motion.register_of(i['words'][1])[1] for i in items if i['opcode']==motion.DCL and motion.register_of(i['words'][1])[0]==10),[0,1])
        self.assertEqual([motion.register_of(i['words'][2])[1] for i in items if i['opcode']==66],[0,1])
        alpha=[i for i in items if i['opcode']==1 and motion.register_of(i['words'][0])==(0,0) and motion.mask_of(i['words'][0])=='w']
        self.assertEqual([(motion.register_of(a['words'][1]),a['words'][1]>>16&255) for a in alpha],[((0,4),0xff)],'composed alpha reads raw A.a')
        branches=[i for i in items if i['opcode']==41]
        self.assertEqual([(struct.unpack_from('<I',data,i['dword']*4)[0]>>16)&255 for i in branches],[1,4])
        first=next(i['dword'] for i in items if i['opcode']==41)
        self.assertTrue(all(i['dword']<first for i in items if i['opcode']==66),'no derivative sampling in divergent control flow')

    def test_promoted_composite_is_exact_qualified_bytecode(self):
        import re
        words=re.findall(r'0x([0-9a-f]{8})u',(ROOT/'src/renderer/linear_distance_fade_composite_inc.h').read_text())
        data=struct.pack('<'+str(len(words))+'I',*(int(x,16) for x in words))
        self.assertEqual(len(data),572)
        # Runtime copy is tied to the runner pin, which the GPU fixture enforces
        # on the exported program (linear-distance-fade-gpu-proto1.json).
        sys.path.insert(0,str(ROOT/'verification/probe'))
        import run_linear_distance_fade as runner
        self.assertEqual(hashlib.sha256(data).hexdigest(),runner.COMPOSITE_SHA256)
        self.assertEqual(runner.COMPOSITE_SHA256,'7b5599fcce4796ab5c2095c7df587c5ce2544bde1db9f2885dab59bfaa30f43d')
        # Old additive program bytes remain untouched; shared state/policy
        # changes are now covered by the whole-component host fixture.
        for name in ['linear_emission_composite_inc.h','linear_emission_copy_clear_inc.h']:
            self.assertEqual((ROOT/'src/renderer'/name).read_bytes(),subprocess.check_output(['git','show','bee7d71:src/renderer/'+name],cwd=ROOT))
        compiler=shutil.which('i686-w64-mingw32-g++')
        if not compiler:self.skipTest('x86 cross preprocessor unavailable')
        source=subprocess.check_output([compiler,'-std=c++17','-E','-P',str(ROOT/'src/renderer/linear_emission_pass.cpp')]).decode()
        self.assertIn('boundary.augmented_vertex',source,'real source-over VS setter cannot remain fixture-only')
        self.assertIn('source_over_composite',source)
        self.assertNotIn('fixture_source_over_',source)

if __name__=='__main__': unittest.main()
