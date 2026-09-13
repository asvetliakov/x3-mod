"""Focused authored contracts/report tests; no D3D or Wine execution."""
from pathlib import Path
import copy
import math
import shutil
import struct
import subprocess
import tempfile
import unittest

import run_varying_split as r


def instructions(data):
    words = struct.unpack('<%dI' % (len(data)//4), data)
    cursor = 1
    out = []
    while words[cursor] != 0xffff:
        count = (words[cursor] >> 24) & 15
        out.append(words[cursor:cursor+count+1])
        cursor += count+1
    if cursor != len(words)-1:
        raise ValueError('trailing tokens')
    return out


def register(token):
    return ((token>>28)&7)|((token>>8)&24), token&0x7ff


def declarations(ins, kind):
    result = {}
    for item in ins:
        if item[0]&0xffff != 31 or register(item[2])[0] != kind:
            continue
        key = (item[1]&15,(item[1]>>16)&15)
        if key in result:
            raise ValueError('duplicate semantic')
        result[key] = (register(item[2])[1], (item[2]>>16)&15,item[2]&0x00700000)
    return result


def witness(mode="qualification", native_flat=True):
    rows = []
    for d in range(2):
        for index,k in enumerate(r.KINDS):
            if mode=="separate" and index not in (0,3,6):
                continue
            rows.append(f'CREATE depth={d} kind={k} vs=1 ps=1 inputs=10 outputs=11')
    rows += ['CAPS mrt=3 vs=fffe0300 ps=ffff0300 msaa=0 format=116',
             'CONTEXT shading=gouraud+flat wrap9=0 no_msaa=1']
    if mode=='separate':
        for name,shade in zip(('first_flat','gouraud','repeat_flat'),(1,2,1)):
            rows.append(f'CLASSIFIER name={name} requested={shade} before={shade} after={shade} coverage=1400')
        rows.append(f'CLASSIFICATION native_flat_conformance={int(native_flat)} effective_flat={"flat" if native_flat else "gouraud"} stable=1')
    for c in r.cases():
        rows.append('CASE '+' '.join(f'{k}={v}' for k,v in c.items())+
                    f' effective_flat={int(c["flat"] and native_flat)} shade_before={1 if c["flat"] else 2} shade_after={1 if c["flat"] else 2} draws={len(r.selected(c["flat"],mode))} coverage=1400 analytic=1100 rgb_exact={0 if mode=="separate" else 1} alpha_exact=1 motion_exact=1 depth_exact=1 centroid_exact={0 if mode=="separate" else 1-c["flat"]} max_fraction=0.25')
    for d in range(2):
        for k,name in enumerate(r.KINDS):
            if mode=="separate" and k not in (0,3,6):
                continue
            n = sum(c['depth']==d and k in r.selected(c['flat'],mode) for c in r.cases())
            n += 3 if mode=='separate' and d==0 and k==0 else 0
            rows.append(f'PROGRAM depth={d} kind={name} draws={n} coverage={n*1400}')
    rows.append(f'RESULT PASS cases=64 creates={12 if mode=="separate" else 28} negatives=10 coverage=89600 max_fraction=0.25')
    return '\n'.join(rows)


class Report(unittest.TestCase):
    def test_complete_matrix(self):
        got = r.parse(witness())
        self.assertEqual((got['cases'],got['draws'],len(got['programs'])),(64,352,14))

    def test_separate_mode_keeps_strict_native_and_cpu_gates(self):
        got = r.parse(witness('separate'),'separate')
        self.assertEqual((got['draws'],got['shader_creates'],len(got['programs'])),(195,12,6))
        for old,new in [('alpha_exact=1','alpha_exact=0'),('motion_exact=1','motion_exact=0'),
                        ('depth_exact=1','depth_exact=0'),('max_fraction=0.25','max_fraction=1.01')]:
            with self.assertRaises(AssertionError):
                r.parse(witness('separate').replace(old,new,1),'separate')
        with self.assertRaises(AssertionError):
            r.parse(witness('separate'))

    def test_native_only_classifier_is_stable_unambiguous_and_exact(self):
        zero=[bytes(4)]*1100
        smooth=[struct.pack('<f',.01+(i%100)*.001) for i in range(1100)]
        covered=list(range(1100))
        self.assertTrue(r.classify_native(zero,smooth,zero,covered))
        self.assertFalse(r.classify_native(smooth,smooth,smooth,covered))
        for a,g,b in ((zero,smooth,smooth), (zero,zero,zero),
                      ([struct.pack('<I',0x80000000)]*1100,smooth,[struct.pack('<I',0x80000000)]*1100),
                      ([struct.pack('<I',0x7fc00000)]*1100,smooth,[struct.pack('<I',0x7fc00000)]*1100),
                      ([struct.pack('<f',.125)]*1100,smooth,[struct.pack('<f',.125)]*1100)):
            with self.assertRaises(AssertionError):
                r.classify_native(a,g,b,covered)

    def test_effective_mode_counts_and_state_gates(self):
        report=r.parse(witness('separate',False),'separate')
        self.assertFalse(report['native_flat_conformance'])
        self.assertEqual((report['requested_flat_cases'],report['effective_flat_cases'],report['effective_gouraud_cases']),(32,0,64))
        self.assertEqual((report['qualification_draws'],report['classifier_draws']),(192,3))
        for a,b in [('before=1','before=2'),('after=1','after=2'),
                    ('stable=1','stable=0'),('effective_flat=gouraud','effective_flat=flat'),
                    ('shade_before=2','shade_before=1'),('effective_flat=0','effective_flat=1')]:
            with self.assertRaises(AssertionError):
                r.parse(witness('separate',False).replace(a,b,1),'separate')

    def test_reject_bad_gates(self):
        text = witness()
        for old,new in [('creates=28','creates=27'),('negatives=10','negatives=9'),
                        ('alpha_exact=1','alpha_exact=0'),('motion_exact=1','motion_exact=0'),
                        ('depth_exact=1','depth_exact=0'),('rgb_exact=1','rgb_exact=0'),
                        ('max_fraction=0.25','max_fraction=1.01'),('wrap9=0','wrap9=1'),
                        ('no_msaa=1','no_msaa=0'),('mrt=3','mrt=2'),
                        ('draws=7','draws=6'),('inputs=10','inputs=9')]:
            with self.subTest(new=new), self.assertRaises(AssertionError):
                r.parse(text.replace(old,new,1))

    def test_missing_duplicate_and_flat_matrix(self):
        text = witness()
        for malformed in ('\n'.join(s for s in text.splitlines() if not s.startswith('CASE id=8 ')),
                          text.replace('CASE id=8 ','CASE id=7 '),
                          text.replace('centroid_exact=0','centroid_exact=1',1)):
            with self.assertRaises(AssertionError):
                r.parse(malformed)
        for c in r.cases():
            if c['flat']:
                self.assertNotIn(1,r.selected(1))
                self.assertNotIn(2,r.selected(1))

    def test_diagnostic_preserves_nan_failure_without_qualification_pass(self):
        with tempfile.TemporaryDirectory(prefix='x3-varying-report-') as directory:
            folder=Path(directory)
            coverage=1100
            color=struct.pack('<4f',.5,.5,.5,.25)*coverage+bytes((4096-coverage)*16)
            bad=struct.pack('<3fI',.5,.5,.5,0x7fc00000)*coverage+bytes((4096-coverage)*16)
            motion=struct.pack('<4f',.1,.2,.3,1)*coverage+bytes((4096-coverage)*16)
            rows=[]
            for dep in range(2):
                for name in r.DIAGNOSTIC_KINDS:
                    rows.append(f'CREATE depth={dep} kind={name} vs=1 ps=1 inputs=10 outputs=11')
            rows+=['CAPS mrt=3 vs=fffe0300 ps=ffff0300 msaa=0 format=116',
                   'CONTEXT shading=gouraud+flat wrap9=0 no_msaa=1']
            for case in range(4):
                for k,name in enumerate(r.DIAGNOSTIC_KINDS):
                    ref=7 if k==6 else 12 if k in (11,12) else 0
                    failure=k==2
                    for t,data in enumerate((bad if failure else color,motion,bytes(65536))):
                        (folder/f'{name}_rt{t}_{case}.rgba32f').write_bytes(data)
                    rows.append(f'OBSERVE id={case} depth={case//2} fog={case%2} perspective={case//2} kind={name} reference={r.DIAGNOSTIC_KINDS[ref]} coverage={coverage} alpha_diff={coverage if failure else 0} alpha_nonfinite={coverage if failure else 0} rgb_diff=0 temporal_diff=0 first_alpha_bits={"7fc00000" if failure else "3e800000"}')
            for dep in range(2):
                for name in r.DIAGNOSTIC_KINDS:
                    rows.append(f'PROGRAM depth={dep} kind={name} draws=2 coverage={coverage*2}')
            rows.append(f'RESULT DIAGNOSTIC cases=4 creates=52 negatives=10 coverage={coverage*4} max_fraction=0')
            text='\n'.join(rows)
            report=r.diagnostic_report(folder,text)
            self.assertTrue(report['diagnostic_completed'])
            self.assertFalse(report['qualification_pass'])
            self.assertEqual(sum(o['alpha_nonfinite'] for o in report['observations']),4400)
            with self.assertRaises(AssertionError):
                r.diagnostic_report(folder,text.replace('alpha_nonfinite=1100','alpha_nonfinite=0',1))

    def test_cpu_flat_first_vertex_and_positive_gradients(self):
        for c in r.cases():
            v = r.inputs(c)
            actual = r.expected_rgb(c,v,24,24)
            self.assertIsNotNone(actual)
            if c['flat']:
                self.assertEqual(actual,v[0][3])
            else:
                for channel in range(3):
                    self.assertLessEqual(min(x[3][channel] for x in v),actual[channel])
                    self.assertGreaterEqual(max(x[3][channel] for x in v),actual[channel])

    def test_precision_envelope_rejects_half_every_gradient_pattern(self):
        # Proves the independent CPU gate cannot bless two equally reduced
        # COLOR paths. No assertion that any actual PP path chooses half.
        for pattern in range(4):
            rejected = 0
            c = dict(pattern=pattern,perspective=1,flat=0)
            v = r.inputs(c)
            for y in range(12,40):
                for x in range(12,40):
                    expected = r.expected_rgb(c,v,x,y)
                    if expected is None:
                        continue
                    for e in expected:
                        half = struct.unpack('<e',struct.pack('<e',e))[0]
                        rejected += abs(half-e) > 1e-12+r.EPS*abs(e)
            self.assertGreater(rejected,500,pattern)


@unittest.skipUnless(shutil.which('clang++'),'host compiler required')
class Programs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='x3-varying-host-')
        cls.folder = Path(cls.temp.name)
        executable = cls.folder/'host'
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-DX3M_VARYING_HOST',str(r.ROOT/r.INPUTS[0]),'-o',str(executable)],check=True,capture_output=True)
        output = subprocess.run([str(executable),str(cls.folder)],check=True,capture_output=True,text=True)
        cls.output = output.stdout
        cls.diag = cls.folder/'diagnostic'
        cls.diag.mkdir()
        cls.diag_output = subprocess.run([str(executable),str(cls.diag),'diagnostic'],check=True,capture_output=True,text=True).stdout

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def code(self,name,depth,stage):
        return instructions((self.folder/f'{name}_{depth}.{stage}').read_bytes())

    def test_diagnostic_controls_are_scoped_mutations(self):
        self.assertEqual(self.diag_output,'CONTRACT programs=26 negatives=10\n')
        for dep in range(2):
            code = lambda k,stage: instructions((self.diag/f'{r.DIAGNOSTIC_KINDS[k]}_{dep}.{stage}').read_bytes())
            for k in (3,4,5):
                for stage in ('vs','ps'):
                    baseline = code(2,stage)
                    changed = code(k,stage)
                    reverse = (stage=='vs' and k in (3,5)) or (stage=='ps' and k in (4,5))
                    if reverse:
                        loc = [i for i,t in enumerate(baseline) if t[0]&65535==31 and t[1]&15==10]
                        baseline[loc[0]],baseline[loc[1]] = baseline[loc[1]],baseline[loc[0]]
                    self.assertEqual(changed,baseline)
            for k in (6,7):
                self.assertEqual(declarations(code(k,'ps'),1)[(10,0)][2],0)
            self.assertEqual(declarations(code(10,'vs'),6)[(10,0)],(1,8,0))
            self.assertEqual(declarations(code(10,'ps'),1)[(10,0)],(0,8,0x00200000))
            self.assertEqual(declarations(code(10,'ps'),1)[(10,1)],(1,7,0))
            for k in (11,12):
                tail = code(k,'ps')[-1]
                self.assertEqual(tail[0]&65535,1)
                self.assertEqual(register(tail[2]),(1,0))
                self.assertEqual((tail[2]>>16)&255,255)
            self.assertEqual(code(8,'vs')[-1][1]&0xf0000,0xf0000)
            self.assertEqual(register(code(8,'vs')[-1][1]),(6,1))
            self.assertEqual(register(code(9,'ps')[-1][1]),(8,0))
            self.assertEqual(code(9,'ps')[-1][1]&0xf0000,0xf0000)

    def test_positive_and_negative_contract_count(self):
        self.assertEqual(self.output,'CONTRACT programs=14 negatives=10\n')
        self.assertEqual(len(list(self.folder.glob('*.ps'))),14)

    def test_independent_semantic_mapping(self):
        for d in range(2):
            for k,name in enumerate(r.KINDS):
                v = declarations(self.code(name,d,'vs'),6)
                p = declarations(self.code(name,d,'ps'),1)
                self.assertEqual({x[0] for x in v.values()},set(range(11)))
                self.assertEqual({x[0] for x in p.values()},set(range(10)))
                packed = k in (1,2,5)
                self.assertEqual(p[(10,0)],(0,8 if packed else 15,0x00200000))
                if k:
                    semantic = (10,1) if k in (5,6) else (5,9)
                    self.assertEqual(p[semantic],(0 if packed else 1,7,0x00400000 if k in (1,3) else 0))
                for semantic,(reg,mask,flags) in p.items():
                    self.assertEqual(v[semantic][:2],(reg+1,mask))
                for registers in (v,p):
                    for a,(reg,mask,_) in registers.items():
                        for b,(other,other_mask,_) in registers.items():
                            if a!=b and reg==other:
                                self.assertEqual(mask&other_mask,0)

    def test_identical_native_alpha_fog_and_temporal_words(self):
        for d in range(2):
            native_vs = self.code('native',d,'vs')
            begin = next(i for i,t in enumerate(native_vs) if t[0]&0xffff==40)
            native_tail = native_vs[begin:]
            native_ps = self.code('native',d,'ps')
            # Native sampled-alpha load and final _pp multiply retain all words.
            native_tex = next(t for t in native_ps if t[0]&0xffff==66)
            for k,name in enumerate(r.KINDS):
                v,p = self.code(name,d,'vs'),self.code(name,d,'ps')
                start = next(i for i,t in enumerate(v) if t[0]&0xffff==40)
                self.assertEqual(v[start:],native_tail)
                self.assertEqual(next(t for t in p if t[0]&0xffff==66),native_tex)
                self.assertEqual(p[-1],native_ps[-1])
                self.assertEqual(p[-1][1]&0x007f0000,0x00280000)
                # Motion and depth semantic bindings and source words are stable;
                # only the dedicated reference's zero filler moves v1 -> v0.
                self.assertEqual(declarations(p,1)[(5,7)],(8,15,0))
                self.assertEqual(declarations(p,1)[(5,8)],(9,15,0))
                reads = set()
                for item in p:
                    if item[0]&0xffff==31:
                        continue
                    for token in item[2:]:
                        typ,index = register(token)
                        if typ==1:
                            reads.add(index)
                self.assertEqual(reads,set(range(10 if d else 9)))
                if d:
                    self.assertEqual(p[-5:],native_ps[-5:])


if __name__ == '__main__':
    unittest.main()
