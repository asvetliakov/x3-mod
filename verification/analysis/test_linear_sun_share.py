"""Real generated PS sun-share checks; copyrighted originals remain local.

Numerics execute converted shader tails with independently varied live inputs.
The numerator oracle is the difference of TWO ordinary material executions:
normal minus sun MAD multiplicands zeroed. Fill still reads the unchanged r12.
This is deliberately independent of the production contribution propagation.
It does not model GPU rounding, PP, filtering or native runtime behavior.
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
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.analysis import inspect_motion_output_profiles as motion
from verification.analysis import xt_pixel_execution as vm


def program_ids():
    source = (ROOT/'src/renderer/linear_material.cpp').read_text().split('constexpr Pixel pixels[] = {')[1].split('\n};')[0]
    source += (ROOT/'src/renderer/linear_xt_profiles_inc.h').read_text().split('constexpr XtPixel xt_pixels[] = {')[1]
    return sorted(set(re.findall(r'\{0x([0-9a-f]+)ull,', source)))


def unpack(code):
    words, items, _ = motion.instructions(code)
    return list(words), items


def pack(words):
    return struct.pack('<%dI' % len(words), *words)


def seeds(items, shadow=False):
    return [i for i in items if i['opcode'] == (5 if shadow else 4)
            and (motion.register_of(i['words'][0])[1] >= 16 if shadow else True)
            and any(motion.register_of(w) == (0, 12) for w in i['words'][1:])]


def tail(code, shadow=False, zero_sun=False):
    words, items = unpack(code)
    seed = seeds(items, shadow)
    if not shadow:
        # Injected fill is MAD(sum, r12, c215.x, sum), excluded explicitly.
        seed = [i for i in seed if not any(motion.register_of(w) == (2,215) for w in i['words'][1:])]
    if zero_sun:
        for i in seed:
            for q,w in enumerate(i['words'][1:], 2):
                if motion.register_of(w) == (0,12): words[i['dword']+q] = 0xa05500d4  # c212.yyyy = 0
    header = [words[0]]
    for i in items:
        if i['opcode'] == motion.DEF: header += words[i['dword']:i['dword']+i['length']+1]
    return pack(header + words[seed[0]['dword']:]), len(seed)


def retain_sun_lobe(code, keep):
    """Zero unselected original MAD terms AND their emitted parallel seed.

    The fill MAD is identified by c215 and remains untouched. ``None`` selects
    genuine zero sun; integer 0/1 selects an authored diffuse/gloss seed alone.
    """
    words,items=unpack(code); ordinary=parallel=0
    for i in items:
        if i['opcode'] not in (4,5) or not i['words']: continue
        destination=motion.register_of(i['words'][0])
        if any(motion.register_of(w)==(2,215) for w in i['words'][1:]): continue
        operands=[q for q,w in enumerate(i['words'][1:],2) if motion.register_of(w)==(0,12)]
        if not operands: continue
        shadow=destination[0]==0 and 16<=destination[1]<=23
        if not shadow and i['opcode']!=4: continue
        ordinal=parallel if shadow else ordinary
        if shadow: parallel+=1
        else: ordinary+=1
        if keep is None or ordinal!=keep:
            for q in operands: words[i['dword']+q]=0xa05500d4
    return pack(words)


def initial(seed, branch):
    rng=random.Random(seed)
    registers={f'{kind}{r}': [rng.uniform(.1,.3) for _ in range(4)]
               for kind,count in [('r',32),('v',10),('c',224)] for r in range(count)}
    # Motion epilogue constants are DEFs or these well-conditioned inputs.
    for r in range(16,24): registers[f'r{r}']=[0.]*4
    registers['c216']=[.2,.3,.4,1.]
    samplers={r:(.43,.37,.29,.8) for r in range(16)}
    return vm.State(registers, {r:branch for r in range(16)}, samplers)


def luma(rgb):
    return sum(x*w for x,w in zip(rgb, (.2126,.7152,.0722)))


class SunShareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals=Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not cls.originals.is_dir(): raise unittest.SkipTest('local original corpus unavailable')
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler: raise RuntimeError('host compiler required')
        temporary=tempfile.TemporaryDirectory(prefix='x3-sun-share-'); cls.addClassCleanup(temporary.cleanup)
        cls.directory=Path(temporary.name); exe=cls.directory/'probe'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                        str(ROOT/'verification/probe/linear_sun_share_structure.cpp'),
                        str(ROOT/'src/renderer/linear_material.cpp'),str(ROOT/'src/renderer/material_motion.cpp'),
                        '-o',str(exe)],check=True,capture_output=True,text=True)
        internal=cls.directory/'internal'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                        str(ROOT/'verification/probe/linear_sun_share_probe.cpp'),
                        str(ROOT/'src/renderer/material_motion.cpp'),'-o',str(internal)],
                       check=True,capture_output=True,text=True)
        run=subprocess.run([str(internal),str(cls.directory)],check=True,capture_output=True,text=True)
        cls.internal_report=json.loads(run.stdout)
        cls.fault_reports={}
        for name in ('593e5dea9b3457d5','fffdabd910793aba'):
            run=subprocess.run([str(exe),'--faults',str(cls.originals/f'ps_{name}.bin')],
                               check=True,capture_output=True,text=True)
            cls.fault_reports[name]=json.loads(run.stdout)
        cls.rows={}
        for name in program_ids():
            for depth in (0,1):
                for fill in (0.,.06):
                    prefix=cls.directory/f'{name}-{depth}-{fill}'
                    run=subprocess.run([str(exe),str(cls.originals/f'ps_{name}.bin'),str(prefix),str(depth),str(fill)],
                                       capture_output=True,text=True)
                    if run.returncode: raise AssertionError(name+': '+run.stderr)
                    cls.rows[name,depth,fill]=(Path(str(prefix)+'-base.bin').read_bytes(),
                                               Path(str(prefix)+'-share.bin').read_bytes(),json.loads(run.stdout))

    def test_all_108_exact_extractions_both_depth_modes_and_fill(self):
        self.assertEqual(len(program_ids()),108)
        self.assertEqual(len(self.rows),432)
        for key,(_,_,report) in self.rows.items(): self.assertTrue(report['extracted'],key)

    def test_original_instructions_preserved_and_share_written_last(self):
        for key,(base,share,_) in self.rows.items():
            bw,bi=unpack(base); sw,si=unpack(share)
            # Each baseline instruction remains verbatim and in order. Only
            # private r16..23/c221 instructions and final oC2.g may be inserted.
            retained=[]
            for i in si:
                k,r=motion.register_of(i['words'][0]) if i['words'] else (-1,-1)
                added=(k==0 and 16<=r<=23) or (i['opcode']==motion.DEF and r==221) or (k==8 and r==2 and motion.mask_of(i['words'][0])=='y')
                if not added: retained.append(tuple(sw[i['dword']:i['dword']+i['length']+1]))
            self.assertEqual(retained,[tuple(bw[i['dword']:i['dword']+i['length']+1]) for i in bi],key)
            self.assertEqual((si[-1]['opcode'],motion.register_of(si[-1]['words'][0]),motion.mask_of(si[-1]['words'][0])),(1,(8,2),'y'),key)

    def test_allocation_failures_keep_output_and_extraction_flag_atomic(self):
        for report in self.fault_reports.values(): self.assertGreater(report['allocation_failures'],20)

    def test_combined_weighted_slot_and_register_budget(self):
        def slots(code):
            _,items=unpack(code)
            samplers={motion.register_of(i['words'][1])[1]:(i['words'][0]>>27)&15
                      for i in items if i['opcode']==31 and motion.register_of(i['words'][1])[0]==10}
            cost=0
            for i in items:
                op=i['opcode']
                if op in (31,81): continue
                if op==66: cost+=4 if samplers[motion.register_of(i['words'][2])[1]]==3 else 1
                else: cost+={32:3,36:3,38:3,40:3,41:3,39:2,18:2,90:2}.get(op,1)
            return cost
        totals=[]; additions=[]; baseline_port_programs=set()
        for key,(base,share,_) in self.rows.items():
            before,after=slots(base),slots(share)
            self.assertLessEqual(after,512,key)
            totals.append(after); additions.append(after-before)
            _,instructions=unpack(share)
            for instruction in instructions:
                if instruction['opcode'] in (31,81): continue
                destination,sources=vm._raw_operands(instruction)
                kind,number=motion.register_of(destination) if destination is not None else (-1,-1)
                added=(kind==0 and 16<=number<=23) or (kind==8 and number==2 and motion.mask_of(destination)=='y')
                if not added:
                    if len({motion.register_of(w)[1] for w in sources if motion.register_of(w)[0]==2})>1:
                        baseline_port_programs.add(key[0])
                    continue
                for kind,limit in ((0,3),(1,1),(2,1)):
                    reads={motion.register_of(w)[1] for w in sources if motion.register_of(w)[0]==kind}
                    self.assertLessEqual(len(reads),limit,(key,instruction['dword'],kind,reads))
        self.assertFalse(baseline_port_programs, 'sanitizer repair must keep combined sun shaders within constant read ports')
        self.evidence={'variants':len(self.rows),'sun_mads':152,'weighted_slots':[min(totals),max(totals)],
                       'added_slots':[min(additions),max(additions)],'allocation_faults':self.fault_reports,
                       'baseline_constant_port_programs':len(baseline_port_programs)}
        print('Sun-share host evidence: '+json.dumps(self.evidence,sort_keys=True))

    def test_malformed_plans_collisions_and_branch_initialization(self):
        self.assertEqual(self.internal_report['checks'],39)
        for branch in (False,True):
            state=initial(1,branch)
            result=vm.execute((self.directory/'branch.bin').read_bytes(),state,stop_after_oc0=False).registers
            self.assertGreater(result['oC2'][1],0.) if branch else self.assertEqual(result['oC2'][1],0.)
        for mutation in (1,2):
            result=vm.execute((self.directory/f'refused-{mutation}.bin').read_bytes(),initial(1,False),stop_after_oc0=False).registers
            self.assertEqual(result['oC2'][1],-1.)

    def test_reduction_black_epsilon_clip_negative_nan_and_infinity(self):
        code=(self.directory/'reduction.bin').read_bytes()
        epsilon=2.**-20
        cases=[([0.,0.,0.],[0.,0.,0.],0.),
               ([epsilon]*3,[epsilon]*3,1.),
               ([epsilon/2]*3,[epsilon/4]*3,-1.),
               ([1e-35]*3,[0.]*3,-1.),
               ([65504.]*3,[65504.]*3,1.),
               ([65505.,1.,1.],[0.,0.,0.],-1.),
               ([1.,2.,3.],[2.,0.,0.],-1.),
               ([1.,2.,3.],[-.01,0.,0.],-1.),
               ([-1.,2.,3.],[0.,0.,0.],-1.),
               ([1.,2.,3.],[0.,0.,0.],0.)]
        for special in (math.nan,math.inf,-math.inf):
            for channel in range(3):
                for source in ('L','S'):
                    total=[1.,2.,3.]; sun=[.1,.2,.3]
                    (total if source=='L' else sun)[channel]=special
                    cases.append((total,sun,-1.))
        # The existing finite XT interpreter's input adapter alone is relaxed
        # for this authored reduction. CMP uses its independently implemented
        # ordered >= predicate; no GPU exceptional-value behavior is claimed.
        with patch.object(vm,'_vec4',lambda values,name:tuple(values)):
            for total,sun,expected in cases:
                state=vm.State({'r11':[*total,0.],'r23':[*sun,0.],'oC2':[.625,0.,0.,0.]})
                result=vm.execute(code,state,stop_after_oc0=False).registers
                self.assertEqual(result['oC2'][0],.625)
                self.assertAlmostEqual(result['oC2'][1],expected,delta=2e-7,msg=str((total,sun)))
                self.assertEqual(result['r11'][:3],total)
        # Luminance equation is the oracle, independent of the emitted
        # reduction's temporary registers; hue equality is not asserted.
        for total,sun in [([1.,.4,3.],[.1,.3,.7]),([.01,20.,4.],[.001,1.,3.])]:
            state=vm.State({'r11':[*total,0.],'r23':[*sun,0.]})
            fraction=vm.execute(code,state,stop_after_oc0=False).registers['oC2'][1]
            for visibility in (0.,.2,.8,1.):
                scaled=[x*(1.-fraction*(1.-visibility)) for x in total]
                removed=[x-(1.-visibility)*y for x,y in zip(total,sun)]
                self.assertAlmostEqual(luma(scaled),luma(removed),delta=2e-7)

    def test_actual_generated_zero_sun_and_separate_authored_lobes(self):
        checked=0
        for index,name in enumerate(program_ids()):
            base,share,_=self.rows[name,1,.06]
            ordinary,count=tail(base); extracted,_=tail(share,shadow=True)
            state=initial(index,True)
            zero=vm.execute(retain_sun_lobe(extracted,None),copy.deepcopy(state),stop_after_oc0=False).registers
            self.assertEqual(zero['oC2'][1],0.,name)
            self.assertGreater(luma(zero['oC0'][:3]),0.,name)
            dark=vm.execute(retain_sun_lobe(ordinary,None),copy.deepcopy(state),stop_after_oc0=False).registers
            self.assertEqual(zero['oC0'],dark['oC0'],name)
            for keep in range(count):
                a=vm.execute(retain_sun_lobe(ordinary,keep),copy.deepcopy(state),stop_after_oc0=False).registers
                c=vm.execute(retain_sun_lobe(extracted,keep),copy.deepcopy(state),stop_after_oc0=False).registers
                self.assertEqual(a['oC0'],c['oC0'],(name,keep))
                total=[x**2.2 for x in a['oC0'][:3]]
                sun=[x-y**2.2 for x,y in zip(total,dark['oC0'][:3])]
                self.assertGreater(c['oC2'][1],0.,(name,keep))
                self.assertAlmostEqual(c['oC2'][1],luma(sun)/luma(total),delta=2e-7,msg=str((name,keep)))
                checked+=1
        self.assertEqual(checked,152)

    def test_fill_and_non_sun_tails_do_not_enter_numerator(self):
        for index,name in enumerate(program_ids()):
            for branch in (False,True):
                numerators=[]; totals=[]
                for fill in (0.,.06):
                    base,_,_=self.rows[name,1,fill]
                    ordinary,_=tail(base); dark,_=tail(base,zero_sun=True)
                    state=initial(index,branch)
                    a=vm.execute(ordinary,copy.deepcopy(state),stop_after_oc0=False).registers
                    b=vm.execute(dark,copy.deepcopy(state),stop_after_oc0=False).registers
                    total=[x**2.2 for x in a['oC0'][:3]]; totals.append(total)
                    numerators.append([x-y**2.2 for x,y in zip(total,b['oC0'][:3])])
                for x,y in zip(*numerators): self.assertAlmostEqual(x,y,delta=2e-7,msg=name)
                self.assertGreater(luma(totals[1]),luma(totals[0]),(name,branch))

    def test_numerical_sun_isolation_and_color_alpha_depth_identity(self):
        count=0; mad_count=0
        for index,(key,(base,share,_)) in enumerate(self.rows.items()):
            if key[1]!=1: continue
            ordinary,n=tail(base); extinguished,_=tail(base,zero_sun=True); extracted,_=tail(share,shadow=True)
            if key[2]==0.: mad_count+=n
            for branch in (False,True):
                state=initial(index,branch)
                # Independent execution needs no knowledge of the generated
                # parallel registers: removing only sun leaves D1/fill/tails.
                try: a=vm.execute(ordinary,copy.deepcopy(state),stop_after_oc0=False).registers
                except Exception as error: raise AssertionError((key,branch)) from error
                b=vm.execute(extinguished,copy.deepcopy(state),stop_after_oc0=False).registers
                c=vm.execute(extracted,copy.deepcopy(state),stop_after_oc0=False).registers
                self.assertEqual(a['oC0'],c['oC0'],(key,branch))
                self.assertEqual(a['oC2'][0],c['oC2'][0],(key,branch))
                # r11 has been encoded; inverse transfer gives the finite total.
                total=[max(x,0.)**2.2 for x in a['oC0'][:3]]
                dark=[max(x,0.)**2.2 for x in b['oC0'][:3]]
                sun=[x-y for x,y in zip(total,dark)]
                expected=luma(sun)/luma(total)
                self.assertAlmostEqual(c['oC2'][1],expected,delta=2e-7,msg=str((key,branch,sun,total)))
                count+=1
        self.assertEqual(mad_count,152)
        self.assertEqual(count,432)


if __name__=='__main__': unittest.main()
