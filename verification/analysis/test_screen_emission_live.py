"""Step C of docs/architecture/screen-emission-region.md, host side: the
launcher option, the shared admission table (nine SM1 pairs, the bullet
scan allowlist), the live runner's counter/sample parser and the witness
union with packed rectangles. Never executes Wine or rebuilds DLLs."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
import run_linear_distance_fade_live as live

PREREQUISITES=['--ownership','--object-trace','--object-lifetime','--motion-output','--taa','--hdr','--hdr-tonemap','--linear-materials']
SM1_PAIRS={('5e484a06672e28fb','ec1f5c4a2f4e1445'),('1b6863a088a177af','84d3de8887c963c5'),('21a2c13be7f989c3','d4a26efb7c603931'),
           ('0d44b36d48d24f7a','078494828322bcca'),('637dadcb5efa3288','078494828322bcca'),('6da1b1b6ed63ec82','2ea025492d370c8e'),
           ('ed42e0742e47dca4','2ea025492d370c8e'),('88620f88d6e0a00e','a5c3495e27270b4a'),('f9755e1154244f58','a5c3495e27270b4a')}
BULLET_VS={'5e484a06672e28fb','1b6863a088a177af','21a2c13be7f989c3'}


def line(prefix,**values):
    return prefix+' '+' '.join(f'{k}={v}' for k,v in values.items())


def launch(directory,*args):
    spec=importlib.util.spec_from_file_location('screen_emission_manage',ROOT/'tools/manage.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    game=Path(directory)/'game';game.mkdir(exist_ok=True);(game/'X3AP.exe').touch()
    wine=Path(directory)/'wine';wine.touch()
    argv=['manage.py','launch','--dry-run','--vanilla','--game-dir',str(game),*args]
    output,error=io.StringIO(),io.StringIO()
    with mock.patch.object(sys,'argv',argv),mock.patch.object(module,'WINE',wine),\
            mock.patch.object(module.subprocess,'call',side_effect=AssertionError('must never launch')),\
            contextlib.redirect_stdout(output),contextlib.redirect_stderr(error):
        try:module.main()
        except SystemExit as exit_error:return exit_error.code,output.getvalue(),error.getvalue()
    return 0,output.getvalue(),error.getvalue()


def rgba(v):return ','.join(repr(float(x)) for x in v)


def screen_report(screen=1,fade=1,emission=1,caps=1,injected=None,region=441):
    """Synthetic fixture output and session log of one functional process,
    consistent with the runner's expectation model and laws."""
    out=[];trace=['motion_output_release device=1 held=54 count=56 released=1']
    if screen:trace.append(line('screen_emission_variant',device=1,original=live.SCREEN_PAIR[1],transform=0,create='00000000',words=191,gain=1,outputs='packed'))
    policies=(3|live.IN_PLACE_POLICY)|(8 if screen and caps else 0);submissions=0;total_pixels=0
    for f in range(live.SCREEN_FRAMES):
        sources,stopped=live.screen_expected_sources(f,screen,fade,emission,caps)
        s=[0]*live.SCREEN_STATUS_KEYS
        s[0]=1;s[1]=int(not stopped);s[17]=int(stopped);s[16]=3;s[18]=s[19]=policies;s[21]=5 if screen and caps else 4;s[20]=12
        for index,key in ((4,'prepared'),(5,'linear'),(6,'native'),(7,'incomplete'),(40,'packed_eligible'),(41,'packed_admitted'),(42,'packed_linear'),(43,'packed_incomplete'),(44,'packed_unbounded'),(45,'packed_caps'),(47,'prefix_bound'),(48,'prefix_refused')):
            s[index]=sum(r[key] for r in sources)
        s[14]=sum(r['prepared'] for r in sources if r['kind']=='f');s[15]=sum(r['linear'] for r in sources if r['kind']=='f')
        s[27]=s[14]+s[41];s[28]=s[15]+s[42];s[10]=s[4]-s[27]
        packed_pixels=(live.rect_area(injected) if injected else region)*s[41];s[46]=packed_pixels;s[29]=packed_pixels+s[14]*live.rect_area(live.source_scissor(0))
        out.append(line('SCREEN_LIVE',frame=f,screen=screen,fade=fade,emission=emission,draws=len(sources),**{f's{i}':v for i,v in enumerate(s)},hash_alpha='a',hash_motion='m',hash_depth='d',hash_mask='k'))
        before=(1.,1.,1.,.39013671875)
        for i,r in enumerate(sources):
            kind=r['kind'];rect=live.SCREEN_QUAD if kind=='s' else live.source_scissor(0)
            for x in live.SCREEN_SAMPLE_X:
                covered=int(rect[0]<=x<rect[2]);packed=r['packed_admitted']
                row=dict(frame=f,source=i,kind=kind,overlap=r['overlap'],x=x,y=32,covered=covered,bracket=int(bool(packed)),packed=int(bool(packed) and (not injected or injected[0]<=x<injected[2])),q='0.5,0.25,0.125',a=0.5,before=rgba(before),after='')
                wanted=live.screen_sample_expectation({k:str(v) for k,v in row.items()},sources,fade,emission) or before
                row['after']=rgba(wanted);out.append(line('SCREEN_SAMPLE',**row))
                if x==36:after36=wanted
            before=after36
            pixels=(live.rect_area(injected) if injected else region) if r['packed_admitted'] else 0
            out.append(line('SCREEN_SOURCE',frame=f,source=i,kind=kind,overlap=r['overlap'],fault=r['fault'],hr='00000000',original_calls=1,prepared=r['prepared'],linear=r['linear'],native=0,incomplete=r['incomplete'],refused=r['refused'],
                            packed_eligible=r['packed_eligible'],packed_admitted=r['packed_admitted'],packed_linear=r['packed_linear'],packed_incomplete=r['packed_incomplete'],packed_unbounded=r['packed_unbounded'],packed_caps=r['packed_caps'],
                            packed_region_pixels=pixels,prefix_bound=r['prefix_bound'],prefix_refused=r['prefix_refused'],rect=','.join(map(str,rect)),mask_before=1,mask_after=int(r['mask_valid']),hash_mask_before='m0',hash_mask_after='m1' if r['prepared'] or r['fault']==5 else 'm0',hash_red_before='r0',hash_red_after='r1' if r['prepared'] else 'r0'))
            if kind=='s' and r['packed_eligible'] and not r['packed_unbounded'] and not r['packed_caps']:
                trace.append(line('packed_region',device=1,frame=f,index=i+2,vs=live.SCREEN_PAIR[0],ps=live.SCREEN_PAIR[1],vb=7,rect=','.join(map(str,injected or (31,23,52,44))),f_permille=107))
            submissions+=1
        total_pixels+=s[29]
        trace.append(line('linear_composition_frame',device=1,frame=f,prepared=s[4],linear=s[5],native=0,incomplete=s[7],refused=1,region_pixels=s[29],packed_eligible=s[40],packed_admitted=s[41],packed_linear=s[42],packed_incomplete=s[43],packed_unbounded_refused=s[44],packed_caps_refused=s[45],packed_region_pixels=s[46]))
        trace.append(line('linear_composition_refusals',device=1,frame=f,pair=1,permission_scene=0,readiness=0,readers=0,frame_stop=0,preparation=sum(r['refused'] for r in sources if r['fault']==5)))
        w=live.screen_expected_witness(f,screen,fade,emission,caps);sampled=w['reason']=='sampled'
        union=(live.rect_area(injected) if injected else region)*w['packed_prepared']+4096*w['fade_prepared']
        outside=(live.rect_area(live.SCREEN_QUAD)-live.rect_area(injected)) if injected and sampled and w['packed_prepared'] and not w['fade_prepared'] else 0
        trace.append(line('fade_witness',device=1,frame=f,k=1,sampled=int(sampled),reason=w['reason'],result='00000000' if sampled else '00000001',width=64 if sampled else 0,height=64 if sampled else 0,
                          rects=w['rects'],rects_prepared=w['fade_prepared']+w['packed_prepared'],rects_unprepared=w['rects']-w['fade_prepared']-w['packed_prepared'],overflow=0,lines_truncated=0,
                          covered=256*w['packed_prepared']+1024*w['fade_prepared'] if sampled else 0,outside=outside,union=union if sampled else 0,fade_prepared=w['fade_prepared'],emission_prepared=w['emission_prepared'],packed_prepared=w['packed_prepared'],
                          f_hist=','.join(map(str,[0,0,0,0,w['rects']-w['fade_prepared'],0,0,w['fade_prepared']]))))
        if f==live.SCREEN_RESET_FRAME:
            out.append('RESET PASS');out.append(line('SCREEN_RESET',frame=f,refs_before=12,refs_after=12-s[21],allocations=s[21],quarantine=0,state_lost=0))
    out.append(line('SCREEN_CHECKS',frames=live.SCREEN_FRAMES,submissions=submissions,qualified=1,benchmark=0,quad=','.join(map(str,live.SCREEN_QUAD)),injected=int(injected is not None)))
    out.append('RESULT PASS checks=1 restorations=1 frames=14')
    return '\n'.join(out),'\n'.join(trace)


def timing_report(screen,width,height):
    out=[];trace=[]
    quad=(width//2-width//40,height//2-height//40,width//2+width//40,height//2+height//40);per=live.rect_area(quad)+80
    for c in live.COUNTS:
        for s in range(live.SCREEN_TIMING_SAMPLES):
            out.append(line('SCREEN_TIMING',width=width,height=height,count=c,sample=s,screen=screen,admitted=c*screen,unbounded=0,region_pixels=per*c*screen,quad=','.join(map(str,quad)),source_ms=f'{.03*c+.15*c*screen:.6f}',terminal_ms='1.9',total_ms=f'{1.93+.03*c+.15*c*screen:.6f}'))
    out.append(line('SCREEN_CHECKS',frames=18,submissions=6*sum(live.COUNTS),qualified=1,benchmark=1,quad=','.join(map(str,quad)),injected=0))
    for f in range(18):trace.append(line('linear_composition_frame',device=1,frame=f,packed_incomplete=0,packed_caps_refused=0))
    return '\n'.join(out),'\n'.join(trace)


class LauncherOption(unittest.TestCase):
    def test_option_sets_both_variables_only_when_requested(self):
        with tempfile.TemporaryDirectory() as directory:
            code,output,error=launch(directory,*PREREQUISITES);self.assertEqual(code,0,error)
            baseline=json.loads(output)['env']
            self.assertEqual((baseline['X3M_SCREEN_EMISSION'],baseline['X3M_SCREEN_EMISSION_BOUND']),('0','0'))
            code,output,error=launch(directory,*PREREQUISITES,'--screen-emission');self.assertEqual(code,0,error)
            env=json.loads(output)['env']
            self.assertEqual((env['X3M_SCREEN_EMISSION'],env['X3M_SCREEN_EMISSION_BOUND']),('1','1'))
            self.assertEqual({k:v for k,v in env.items() if k not in ('X3M_SCREEN_EMISSION','X3M_SCREEN_EMISSION_BOUND')},
                             {k:v for k,v in baseline.items() if k not in ('X3M_SCREEN_EMISSION','X3M_SCREEN_EMISSION_BOUND')})

    def test_prerequisites_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            for missing in ('--ownership','--linear-materials','--taa'):
                args=[a for a in PREREQUISITES if a!=missing]
                if missing=='--taa':args=[a for a in args if a not in ('--object-trace','--object-lifetime')]
                code,_,error=launch(directory,*args,'--screen-emission')
                self.assertEqual(code,2,missing);self.assertIn('--screen-emission',error)


class AdmissionTable(unittest.TestCase):
    def test_nine_pairs_shared_with_the_sm1_emitter_and_the_bullet_allowlist(self):
        header=(ROOT/'src/proxy/screen_emission_admission.h').read_text()
        rows=re.findall(r'\{0x([0-9a-f]{16})ull, 0x([0-9a-f]{16})ull, (true|false)\}',header)
        self.assertEqual(len(rows),9);self.assertEqual({(v,p) for v,p,_ in rows},SM1_PAIRS)
        self.assertEqual(rows[0][:2],live.SCREEN_PAIR,'row 19 first')
        self.assertEqual({v for v,_,b in rows if b=='true'},BULLET_VS)
        emitter=(ROOT/'src/renderer/linear_emission_sm1.cpp').read_text()
        self.assertEqual({(v,p) for v,p in re.findall(r'\{0x([0-9a-f]{16})ull,0x([0-9a-f]{16})ull\}',emitter)},SM1_PAIRS,'the emitter registers the same nine pairs')
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:raise unittest.SkipTest('host compiler required')
        program='''#include "screen_emission_admission.h"
#include <cstdio>
int main(){using namespace x3m::screen_emission;unsigned pairs=0,vs=0,ps=0;
for(const auto&p:pairs_ref()){}return 0;}'''
        program='''#include "screen_emission_admission.h"
#include <cstdio>
int main(){using namespace x3m::screen_emission;unsigned ok=0;
for(const auto& p:pairs){ok+=admitted_pair(p.vertex,p.pixel);ok+=admitted_pixel_shader(p.pixel);ok+=admitted_vertex_shader(p.vertex)==p.bullet;}
ok+=!admitted_pair(pairs[0].vertex,pairs[3].pixel);ok+=!admitted_pair(0,pairs[0].pixel);ok+=!admitted_vertex_shader(0);ok+=!admitted_vertex_shader(pairs[3].vertex);
std::printf("ok=%u bullets=%u\\n",ok,unsigned(admitted_vertex_shader(pairs[0].vertex))+admitted_vertex_shader(pairs[1].vertex)+admitted_vertex_shader(pairs[2].vertex));return 0;}'''
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'admission.cpp';source.write_text(program)
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/proxy'),str(source),'-o',str(Path(directory)/'admission')],check=True,capture_output=True,text=True)
            self.assertEqual(subprocess.check_output([str(Path(directory)/'admission')],text=True).strip(),'ok=31 bullets=3')


class RunnerParser(unittest.TestCase):
    def test_functional_report_and_laws(self):
        output,trace=screen_report()
        case=live.validate_screen_functional(output,trace)
        self.assertEqual((case['frames'],case['sources'],case['region_pixels_per_bracket']),(14,19,441))
        self.assertEqual(case['totals'],dict(packed_eligible=13,packed_admitted=10,packed_linear=9,packed_incomplete=1,packed_unbounded=2,packed_caps=0,packed_region_pixels=4410))
        self.assertLessEqual(case['max_tolerance_fraction'],1e-9);self.assertGreaterEqual(case['alpha_pairs'],1)
        self.assertEqual(live.validate_screen_functional(*screen_report(screen=0),screen=0)['totals']['packed_eligible'],0)
        caps=live.validate_screen_functional(*screen_report(caps=0),caps=0)
        self.assertEqual((caps['totals']['packed_caps'],caps['totals']['packed_admitted']),(11,0))
        straddle=live.validate_screen_functional(*screen_report(injected=live.SCREEN_STRADDLE_RECT),injected=live.SCREEN_STRADDLE_RECT)
        self.assertEqual(straddle['region_pixels_per_bracket'],128)
        self.assertAlmostEqual(live.screen_law((1.,1.,1.,1.),live.SCREEN_TEXEL,1.,1)[0],(.5**2.2+.5)**(1/2.2))
        self.assertEqual(live.screen_native((1.,1.,1.,.39),live.SCREEN_TEXEL,1.,1),(1.,1.,1.,.5+.5*.39))

    def test_mutations_are_refused(self):
        output,trace=screen_report()
        for old,new in (('packed_unbounded=1','packed_unbounded=0'),('original_calls=1','original_calls=2'),('after=1.0,1.0,1.0,0.69','after=0.9,1.0,1.0,0.69'),
                        ('s18=15 s19=15','s18=7 s19=7'),('released=1','released=0')):
            self.assertIn(old,output+trace,old)
            with self.assertRaises(AssertionError,msg=old):live.validate_screen_functional(output.replace(old,new,1),trace.replace(old,new,1))
        trace_mutated=trace.replace('preparation=1','preparation=0',1)
        with self.assertRaises(AssertionError):live.validate_screen_functional(output,trace_mutated)

    def test_witness_union_counts_packed_rectangles_and_the_straddle_fires(self):
        _,trace=screen_report()
        witness=live.validate_screen_witness(trace)
        self.assertEqual(witness['sampled_frames'],[2,4,5,7,8,10,12]);self.assertEqual(witness['outside_pixels'],0)
        self.assertEqual(witness['skipped'],{'no_fade':3,'emission':3,'mask_invalid':1})
        self.assertEqual(witness['f_histogram']['f<=0.25'],7);self.assertEqual(witness['f_histogram']['f=1'],2)
        _,straddle=screen_report(injected=live.SCREEN_STRADDLE_RECT)
        with self.assertRaises(live.WitnessViolation) as raised:live.validate_screen_witness(straddle,injected=live.SCREEN_STRADDLE_RECT)
        self.assertEqual(raised.exception.violations,live.screen_straddle_violations());self.assertEqual(live.screen_straddle_violations(),{2:128,7:128,10:128,12:128})
        with self.assertRaises(AssertionError):live.validate_screen_witness(trace.replace('packed_prepared=1','packed_prepared=0',1))

    def test_timing_parser_and_paired_cost(self):
        cases={}
        for width,height in live.RESOLUTIONS:
            for pair in (0,1):
                for screen in (0,1):
                    cases[live.screen_timing_name(width,height,pair,screen)]=live.validate_screen_timing(*timing_report(screen,width,height),screen,width,height)
        cost=live.screen_paired_cost(cases)
        self.assertEqual(len(cost),len(live.RESOLUTIONS)*len(live.COUNTS))
        for row in cost:
            self.assertAlmostEqual(row['pairs'][0]['paired_window_median_delta_ms']['source'],.15*row['ordered_dips'],places=5)
            self.assertAlmostEqual(row['per_bracket_source_delta_ms'][1],.15,places=3)
        with self.assertRaises(AssertionError):live.validate_screen_timing(*timing_report(1,1280,768),0,1280,768)


if __name__=='__main__':
    unittest.main()
