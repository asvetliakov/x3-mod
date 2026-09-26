"""Step A of the packed screen policy: policy table, plane layout, programs and
the step-A report parser (docs/architecture/screen-emission-region.md)."""
import re
import struct
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
sys.path.insert(0,str(ROOT/'tools/shaders'))
import run_screen_emission_step_a as r
import generate_screen_emission_programs as gen
from source_text import source_text


def witness(mutate=None):
    lines=['STEP_A_ATTACH supported=15 available=15 allocations=5 references=12']
    exact=0;pixels=0;per=[0,0,0]
    for p in range(9):
        for c in r.IN_DOMAIN:
            for s in range(3):
                rect='0,0,32,32' if s==1 else '1,2,31,30'
                l,t,rr,b=(int(v) for v in rect.split(','));pixels+=(rr-l)*(b-t)
                surviving=0 if c in (9,13) else 1400
                lines.append(f'STEP_A_ROW pair={p} case={c} name={r.packed.CASES[c]} schedule={s} known={int(s!=1)} rect={rect} inside_diff=0 '
                             f'outside_diff=0 mask_diff=0 red_outside=0 prototype_failed=0 surviving={surviving} overlap={500 if surviving else 0}')
                exact+=1;per[s]+=1
                if (p,c,s)==(0,0,0):lines.append('STEP_A_STRADDLE rect=8,8,24,24 inside_diff=0 outside_diff=0 red_outside=300 witness_fired=1')
    lines.append(f'STEP_A_CORPUS rows={r.ROWS} unsupported=0 exact={exact} exact_inside={exact} exact_outside={exact} exact_mask={exact} '
                 f'conservative={exact} prototype_failures=0 one_dip={per[0]} two_dips={per[1]} reverse={per[2]} region_pixels={pixels}')
    for order in ('fade_packed','packed_fade'):
        lines.append(f'STEP_A_SEQUENCE order={order} fade_rect=2,2,22,22 packed_rect=1,2,31,30 fade_diff=0 fade_max_fraction=0.31 packed_inside_diff=0 packed_outside_diff=0')
    lines.append('STEP_A_ALIAS exchange=1 c_diff=0 m_diff=0')
    for i,(label,prepared,first,recovery,coverage,blocked) in enumerate(r.LADDER):
        lines.append(f'STEP_A_FAILURE stage={i+1} label={label} native=1 prepared={prepared} first={first:08x} recovery={recovery:08x} coverage={coverage} blocked={blocked} exact_a=1 exchange=0')
    lines+=['STEP_A_CAPS refused=4','STEP_A_RESET interrupted=1 detached=1 post_reset_inside_diff=0 post_reset_outside_diff=0','PACKED_RESET passed=1',
            f'STEP_A_COMPLETE policy=8 pairs=9 cases=20 schedules=3 owned_targets=5 target_bytes={5*32*32*8} live_publication=0']
    text='\n'.join(lines)
    if mutate:
        old,new=mutate;assert old in text,old;text=text.replace(old,new,1)
    return text


def timing_witness():
    lines=[]
    for w,h in r.TIMING_SIZES:
        for a,(rw,rh) in zip(r.TIMING_AREAS,((56,42),(150,161))):
            l,t=w//2-rw//2,h//2-rh//2
            for policy in r.TIMING_POLICIES:
                for d in r.TIMING_DIPS:
                    for i in range(8):
                        ms=.1*d if policy=='native' else .3*d
                        lines.append(f'STEP_A_TIMING width={w} height={h} policy={policy} rect={l},{t},{l+rw},{t+rh} area={a} dips={d} iteration={i} completed_ms={ms:.6f}')
    lines.append('STEP_A_TIMING_RESULT sizes=2 rects=2 policies=2 dips=3 iterations=8')
    return '\n'.join(lines)


class StepAReportTests(unittest.TestCase):
    def test_complete_report_is_bit_exact_and_compact(self):
        report=r.validate(witness())
        self.assertTrue(report['bit_exact']);self.assertEqual((report['rows'],report['exact']),(540,540))
        self.assertEqual((report['one_dip'],report['two_dips'],report['reverse']),(180,180,180))
        self.assertEqual(len(report['ladder']),10);self.assertEqual(report['capability_refusals'],4)
        self.assertFalse(report['live_publication']);self.assertNotIn('rows_detail',report)
        self.assertEqual(report['straddle']['witness_fired'],'1')

    def test_mutations_are_rejected(self):
        for old,new in (('inside_diff=0 outside_diff=0 mask_diff=0 red_outside=0 prototype_failed=0 surviving=1400','inside_diff=1 outside_diff=0 mask_diff=0 red_outside=0 prototype_failed=0 surviving=1400'),
                        ('red_outside=300 witness_fired=1','red_outside=0 witness_fired=0'),
                        ('STEP_A_STRADDLE rect=8,8,24,24 inside_diff=0','STEP_A_STRADDLE rect=8,8,24,24 inside_diff=3'),
                        ('label=plane_init native=1 prepared=0 first=80004005','label=plane_init native=1 prepared=1 first=80004005'),
                        ('label=recovery native=1 prepared=1 first=8876086c recovery=80004005','label=recovery native=1 prepared=1 first=8876086c recovery=00000000'),
                        ('label=source native=1 prepared=1 first=8876086c recovery=00000000 coverage=0 blocked=1 exact_a=1 exchange=0','label=source native=1 prepared=1 first=8876086c recovery=00000000 coverage=0 blocked=1 exact_a=1 exchange=1'),
                        ('STEP_A_CAPS refused=4','STEP_A_CAPS refused=3'),
                        ('STEP_A_ALIAS exchange=1 c_diff=0','STEP_A_ALIAS exchange=1 c_diff=2'),
                        ('fade_diff=0 fade_max_fraction=0.31','fade_diff=4 fade_max_fraction=2.5'),
                        ('post_reset_inside_diff=0','post_reset_inside_diff=1'),
                        ('allocations=5 references=12','allocations=4 references=10'),
                        ('prototype_failures=0','prototype_failures=1'),
                        ('owned_targets=5','owned_targets=7')):
            with self.subTest(old=old):
                with self.assertRaises(AssertionError):r.validate(witness((old,new)))
        with self.assertRaises(AssertionError):r.validate(witness().replace('name=asymmetric schedule=2','name=asymmetric schedule=1',1))
        with self.assertRaises(AssertionError):r.validate(witness().replace('STEP_A_COMPLETE','STEP_A_COMPLETE_X'))

    def test_timing_pairs_native_and_packed_windows(self):
        report=r.validate_timing(timing_witness())
        self.assertEqual(len(report['timing']),24);self.assertEqual(len(report['per_bracket']),4)
        for cost in report['per_bracket']:
            self.assertEqual(cost['dips'],16);self.assertAlmostEqual(cost['per_bracket_ms'],.2,places=6)
        for old,new in (('policy=packed rect','policy=fused rect'),('completed_ms=0.100000','completed_ms=nan'),('area=2352','area=2000'),('iteration=7','iteration=9')):
            with self.assertRaises(AssertionError):r.validate_timing(timing_witness().replace(old,new,1))


class StepAPolicyAndLayoutTests(unittest.TestCase):
    def test_policy_table_caps_and_seams(self):
        header=source_text(ROOT/'src/renderer/linear_emission_pass.h')
        self.assertIn('PackedScreenInPlace = 8',header);self.assertIn('PlaneInit ',header)
        body=source_text(ROOT/'src/renderer/linear_emission_pass.cpp')
        # Capability gate: four targets, independent masks, scissor, ONE/INVSRCALPHA blend caps.
        self.assertIn('caps9.NumSimultaneousRTs < 4',body);self.assertIn('D3DPBLENDCAPS_INVSRCALPHA',body);self.assertIn('D3DPBLENDCAPS_ONE',body)
        self.assertIn('requested_policies & ~15u',body)
        # Bracket: native INVSRCCOLOR admitted, INVSRCALPHA substituted, M red|alpha (9) and RGB planes (7), init masks alpha-only (8).
        self.assertIn('saved.state(D3DRS_DESTBLEND) == D3DBLEND_INVSRCCOLOR',body)
        self.assertIn('call(SetRs, D3DRS_DESTBLEND, DWORD(D3DBLEND_INVSRCALPHA))',body)
        self.assertIn('call(SetRs, D3DRS_COLORWRITEENABLE, DWORD(9))',body);self.assertIn('call(SetRs, D3DRS_COLORWRITEENABLE, DWORD(8))',body)
        self.assertIn('LinearEmissionPassFault::PlaneInit',body)
        # Two owned programs for policy 8 beside copy (twin and production) and the shared policy loop.
        self.assertEqual(body.count('call(CreatePs,'),4)
        for forbidden in ('wined3d','__wine','GetProcAddress','LoadLibrary','GetModuleHandle'):
            self.assertNotIn(forbidden,body)

    def test_plane_layout_reuses_e_and_c_and_saves_five_stages(self):
        body=source_text(ROOT/'src/renderer/linear_emission_pass.cpp')
        self.assertIn('sources[0] = e; sources[1] = c; sources[2] = pb; sources[3] = m; sources[4] = a;',body)
        self.assertIn('constexpr unsigned base_stages = 3, max_stages = 5;',body);self.assertIn('IDirect3DBaseTexture9 *texture[max_stages]{};',body)
        # Bracket-local stage inventory: policies 1-4 keep three stages when policy 8 is merely available.
        self.assertIn('p.stages = p.packed ? max_stages : base_stages;',body);self.assertNotIn('p.stages = max_stages;',body)
        self.assertIn('supports(LinearCompositionPolicy::PackedScreenInPlace) ? 5u : 4u',body)
        self.assertIn('same_object(p, m) || same_object(p, pb)',body);self.assertIn('for (auto *target : {b, e, c, m, pb})',body)
        self.assertRegex(body,r'hr = call\(SetRt, DWORD\(1\), e\);\s*if \(SUCCEEDED\(hr\)\) hr = call\(SetRt, DWORD\(2\), c\);\s*if \(SUCCEEDED\(hr\)\) hr = call\(SetRt, DWORD\(3\), pb\);')
        # motion_output.cpp keeps requesting and publishing policies 1-4 only (step C is later).
        route=source_text(ROOT/'src/proxy/motion_output.cpp')
        self.assertIn('PackedScreenInPlace',route) # step C admits the policy from the route (screen-emission-region.md)


def prototype_words(kind):
    source=source_text(ROOT/'verification/probe/linear_emission_sm1_packed_fixture.cpp')
    def function(name):
        m=re.search(r'^(?:Words|DWORD|void) '+name+r'\(',source,re.M);assert m,name
        start=m.start();brace=source.index('{',start);depth=1;i=brace+1
        while depth:
            depth+=(source[i]=='{')-(source[i]=='}');i+=1
        return source[start:i]
    code='#include <cstdint>\n#include <cstring>\n#include <vector>\n#include <initializer_list>\n#include <iostream>\nusing DWORD=std::uint32_t;using Words=std::vector<DWORD>;\n'
    code+='\n'.join(function(n) for n in ('reg','dst','src','ins','literal','screen_ps'))
    code+='\nint main(){for(unsigned k:{0u,2u}){for(auto w:screen_ps(k))std::cout<<std::hex<<w<<" ";std::cout<<"\\n";}}'
    compiler=shutil.which('clang++') or shutil.which('c++')
    with tempfile.TemporaryDirectory(prefix='x3-screen-programs-') as d:
        cpp=Path(d)/'w.cpp';exe=Path(d)/'w';cpp.write_text(code)
        subprocess.run([compiler,'-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True,capture_output=True,text=True)
        rows=[[int(x,16) for x in line.split()] for line in subprocess.check_output([str(exe)],text=True).splitlines()]
    return rows[0 if kind==0 else 1]


class StepAProgramTests(unittest.TestCase):
    def test_generated_fragments_are_checked_in_and_ps3_ports_of_the_prototype(self):
        check=subprocess.run([sys.executable,str(ROOT/'tools/shaders/generate_screen_emission_programs.py'),'--check'],capture_output=True,text=True)
        self.assertEqual(check.returncode,0,check.stdout)
        for name,(kind,path) in gen.TARGETS.items():
            words=[int(x,16) for x in re.findall(r'0x([0-9a-f]{8})u',source_text(path))]
            self.assertEqual(words,gen.screen_ps(kind,True),name)
            self.assertEqual((words[0],words[-1]),(0xffff0300,0xffff))
            proto=prototype_words(kind)
            self.assertEqual(proto,gen.screen_ps(kind,False),'generator reproduces the prototype ps_2_0 words')
            # ps_3_0 port: version token, dcl usage and input register only; every arithmetic token equal.
            self.assertEqual(len(words),len(proto))
            differing=[i for i,(a,b) in enumerate(zip(words,proto)) if a!=b]
            self.assertEqual(differing[0],0)
            allowed={0,2,3}|{i for i in range(len(proto)) if proto[i]==gen.src(3,0)}
            self.assertTrue(set(differing)<=allowed,(name,differing))
            self.assertEqual(words[2],0x80000005);self.assertEqual(words[3],gen.dst(1,0,3))
            self.assertTrue(all(words[i]==gen.src(1,0) for i in differing if i>3))
        init=gen.screen_ps(0,True);composite=gen.screen_ps(2,True)
        # Step E: the gain literal sits where the pass host test expects it, authored at g = 1.
        index=gen.gain_literal_index(composite)
        self.assertEqual(index,gen.gain_literal_index(gen.screen_ps(2,False)))
        host=source_text(ROOT/'verification/probe/linear_emission_pass_host.cpp')
        self.assertEqual(int(re.search(r'constexpr unsigned gain_literal_index = (\d+);',host).group(1)),index)
        self.assertEqual(composite[index+2:index+6],list(struct.unpack('<4I',struct.pack('<4f',*gen.gain_literal(1.0)))))
        self.assertEqual(gen.gain_literal(1.0)[:2],(1.0,0.0))
        self.assertEqual(sum(1 for w in init if w==0x90000000),1);self.assertEqual(sum(1 for w in composite if w==0x90000000),5)


if __name__=='__main__':unittest.main()
