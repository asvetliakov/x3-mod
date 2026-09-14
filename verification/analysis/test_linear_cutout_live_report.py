"""Small strict-report tests. These never execute Wine or build a DLL."""
from pathlib import Path
import tempfile
import unittest
import subprocess
import shutil
import run_linear_cutout_live as live


def report(material=True,depth=True,taa=True,bias=0,mixed=False):
    frames=3 if mixed else live.FRAMES
    lines=[];temporal=0
    for frame in range(frames):
        p=live.plan(frame,material,bias,mixed);accepted=0 if p['step'] in (5,12) else 512
        temporal+=bool(taa)
        row=dict(frame=frame,**p,material=int(material),depth=int(depth),matched=0,accepted=accepted,
                 holes=4096-accepted,owned=accepted*p['routed'],cap_status=2 if 1<=p['cap']<=8 else 3 if p['cap']>=9 else 1,
                 cap_queries=frame+1,routed_delta=p['routed'] if p['step']!=10 else 0,
                 missed_delta=live.missed(frame,material,bias,mixed),unavailable=live.missed(frame,material,bias,mixed))
        lines.append('CUTOUT_LIVE '+' '.join(f'{k}={v}' for k,v in row.items()))
        if taa:lines.append(f'TAA frame={frame} history={int(frame>0)} cut=0 changed=0 policy=1 skipped=0')
        if material and p['routed'] and p['wrong']<0 and frame not in (58,59,61) and accepted:
            rgb=live.material_reference.expected(live.rgb_case(p['pair'],p['step'])).encoded_rgba[:3]
            lines.append(f"CUTOUT_RGB frame={frame} pair={p['pair']} step={p['step']} reverse={int(p['step']==2)} rgb="+','.join(map(str,rgb)))
    if mixed:
        lines += [f'CUTOUT_FADE frame={f} source={i} prepared=1 original_calls=1' for f in range(3) for i in (0,1)]
        lines += [f'CUTOUT_UNION frame={f} covered=1536 unavailable={int(f==1)} mask_valid={int(f!=1)} reactive_uploads={int(f>0)}' for f in range(3)]
        lines.append('CUTOUT_UNION_SUMMARY reactive_uploads=2')
    lines += [f'CUTOUT_CHECKS frames={frames} benchmark=0 pairs=2',
              f'RESULT PASS frames={frames} checks=100 restorations=64 taa_reference_frames={temporal} taa_skipped_frames={frames-temporal if taa else 0}']
    return '\n'.join(lines)


class CutoutReport(unittest.TestCase):
    def test_complete_modes(self):
        for material,depth,taa,bias in ((True,True,True,0),(True,False,False,0),(False,True,False,0),(True,True,True,-.5)):
            r=live.validate_report(report(material,depth,taa,bias),material,depth,taa,bias)
            self.assertEqual(r['frames'],70)
            if material and not bias:self.assertGreater(r['owned_pixels'],0)

    def test_reject_incomplete_duplicate_and_scope(self):
        text=report()
        for changed in (text.replace('CUTOUT_LIVE frame=2 ','OTHER frame=2 '),text+'\n'+text.splitlines()[0],
                        text.replace('CUTOUT_CHECKS frames=70','CUTOUT_CHECKS frames=71'),
                        text.replace('RESULT PASS','RESULT FAIL'),text.replace('pair=0 step=0','pair=1 step=0',1)):
            with self.subTest(changed=changed[-80:]),self.assertRaises(AssertionError):live.validate_report(changed)

    def test_reject_counter_caps_pixel_and_rgb_corruption(self):
        text=report()
        changes=(('routed_delta=1','routed_delta=0'),('owned=512','owned=511'),('holes=3584','holes=3585'),
                 ('cap=9 cap_status=3','cap=9 cap_status=1'),('cap=0 cap_status=1','cap=0 cap_status=3'),
                 ('TAA frame=46 history=1 cut=0','TAA frame=46 history=0 cut=0'),('TAA frame=56 history=1 cut=0 changed=0 policy=1 skipped=0','TAA frame=56 history=1 cut=0 changed=0 policy=1 skipped=1'),
                 ('unavailable=1','unavailable=0'))
        for before,after in changes:
            # cap fields are separated by the fixture's other ordered fields.
            if before not in text:
                before='cap_status=3';after='cap_status=1'
            with self.subTest(before=before),self.assertRaises(AssertionError):live.validate_report(text.replace(before,after,1))
        line=next(l for l in text.splitlines() if l.startswith('CUTOUT_RGB '))
        for rgb in ('nan,0,0','1e9,1e9,1e9','0,0'):
            changed=text.replace(line,line.split('rgb=')[0]+'rgb='+rgb)
            with self.subTest(rgb=rgb),self.assertRaises(AssertionError):live.validate_report(changed)

    def test_inactive_arm_never_reports_missed(self):
        for bias in (0,-.5):
            text=report(bias=bias)
            for frame in (46,50,54,56)+((3,12) if bias else ()):
                line=next(l for l in text.splitlines() if l.startswith(f'CUTOUT_LIVE frame={frame} '))
                self.assertTrue(line.endswith('missed_delta=0 unavailable=0'),line)
                with self.subTest(bias=bias,frame=frame),self.assertRaises(AssertionError):
                    live.validate_report(text.replace(line,line.replace('missed_delta=0 unavailable=0','missed_delta=1 unavailable=1')),bias=bias)
            line=next(l for l in text.splitlines() if l.startswith('CUTOUT_LIVE frame=33 '))
            self.assertTrue(line.endswith(f'missed_delta={int(not bias)} unavailable={int(not bias)}'),line)
        self.assertEqual(live.validate_report(report(bias=-.5),bias=-.5)['history_retained_frames'],24)
        self.assertEqual(live.validate_report(report())['history_retained_frames'],10)

    def test_native_trace_counts_and_state_cost(self):
        result=live.validate_report(report())
        lines=[]
        for frame in range(live.FRAMES):
            p=live.plan(frame)
            values=dict(frame=frame,draws=3,routed=1+p['routed'],depth=1,taa_resolved=1,apply_failures=0,restore_failures=0,present='00000000',state_shadow=1,rt_mode='lazy',rs_queries=20,rs_hits=18,rs_gets=2)
            lines.append('motion_output_frame '+' '.join(f'{k}={v}' for k,v in values.items()))
            values=dict(frame=frame,cutout_routed=p['routed'] if p['step']!=10 else 0,cutout_missed=live.missed(frame),cutout_unavailable=live.missed(frame),bind_failures=int(frame in (58,59)))
            lines.append('linear_material_frame '+' '.join(f'{k}={v}' for k,v in values.items()))
        text='\n'.join(lines)
        self.assertEqual(live.validate_trace(text,result,True,True,True,True,True,0)['native_render_state_gets'],140)
        for changed in (text.replace('draws=3','draws=4',1),text.replace('restore_failures=0','restore_failures=1',1),text.replace('cutout_missed=1','cutout_missed=0',1)):
            with self.assertRaises(AssertionError):live.validate_trace(changed,result,True,True,True,True,True,0)

    def test_one_shot_retry_and_recovery_are_distinct(self):
        self.assertEqual([(f,live.plan(f)['cap'],live.plan(f)['routed']) for f in (54,55,56,57)],
                         [(54,9,0),(55,0,1),(56,10,0),(57,0,1)])
        self.assertEqual([live.plan(f)['routed'] for f in (58,59,60,61)], [1,1,0,1])

    def test_mixed_fade_union_and_missing_history(self):
        text=report(mixed=True);r=live.validate_report(text,mixed=True)
        self.assertTrue(r['mixed']);self.assertEqual(r['frames'],3)
        self.assertEqual(r['reactive_uploads'],2)
        for changed in (text.replace('covered=1536','covered=1535',1),text.replace('original_calls=1','original_calls=2',1),text.replace('CUTOUT_FADE frame=1 source=0','CUTOUT_FADE frame=1 source=1',1),
                        # A lost emission_mask_valid (no reactive uploads) must be caught, not silently pass.
                        text.replace('mask_valid=1 reactive_uploads=0','mask_valid=0 reactive_uploads=0',1),text.replace('reactive_uploads=1','reactive_uploads=0'),
                        text.replace('CUTOUT_UNION_SUMMARY reactive_uploads=2','CUTOUT_UNION_SUMMARY reactive_uploads=0'),text.replace('CUTOUT_UNION_SUMMARY reactive_uploads=2\n','')):
            with self.assertRaises(AssertionError):live.validate_report(changed,mixed=True)

    def test_actual_pixel_reducer_corruption(self):
        compiler=shutil.which('c++')
        self.assertIsNotNone(compiler,'host C++ compiler required for actual reducer witness')
        header=(live.ROOT/'verification/probe/motion_output_cutout_inc.h').read_text()
        reducer=header[header.index('unsigned cutout_pixel_errors('):header.index('// Selected live cutouts.')]
        body=r"""
#include <cmath>
#include <cstring>
#include <limits>
#include <cassert>
REPLACE
int main(){
    float color[]={.5f,.25f,.75f,1},before[]={.5f,.25f,.75f,1};
    float motion[]={.5f,.5f,.3f,1},old[]={.2f,.4f,.5f,1};
    auto run=[&](bool depth,bool pass,bool routed,bool matched){return cutout_pixel_errors(color,before,motion,old,.3f,.5f,depth,pass,routed,matched,true,.5,.5,64,64);};
    assert(run(true,true,true,true)==0);
    color[0]=std::numeric_limits<float>::quiet_NaN();assert(run(true,true,true,true)&1);color[0]=.5f;
    color[3]=.5f;assert(run(true,true,true,true)&2);color[3]=1;
    color[0]=.125f;assert(run(false,false,false,false)&4);color[0]=.5f;
    assert(run(false,false,false,false)&8);assert(run(true,false,false,false)&16);
    motion[0]+=.25f;assert(run(true,true,true,true)&64);motion[0]=.5f;
    assert(run(true,true,true,false)&128);assert(run(true,true,false,false)&256);
    assert(cutout_pixel_errors(color,before,motion,old,.7f,.5f,true,true,true,true,true,.5,.5,64,64)&32);
    assert(!(cutout_pixel_errors(color,before,motion,old,.7f,.5f,false,true,true,true,true,.5,.5,64,64)&32));
    motion[0]=motion[1]=motion[2]=0;motion[3]=-1;assert(run(true,true,true,false)==0);
    std::memcpy(motion,old,16);assert(run(false,false,false,false)==0);
}
""".replace('REPLACE',reducer)
        with tempfile.TemporaryDirectory(prefix='x3-cutout-reducer-') as tmp:
            path=Path(tmp);source=path/'witness.cpp';source.write_text(body)
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(path/'witness')],check=True,capture_output=True,text=True)
            subprocess.run([str(path/'witness')],check=True,capture_output=True,text=True)

    def test_timing_needs_all_paired_samples(self):
        lines=['RESULT PASS frames=18','CUTOUT_CHECKS frames=18 benchmark=1 pairs=2']
        lines += [f'CUTOUT_TIMING width=1280 height=768 count={c} sample={s} material=1 ordinary=0 ms=.125' for c in live.COUNTS for s in range(4)]
        text='\n'.join(lines);self.assertEqual(len(live.validate_timing(text,True,1280,768)['counts']),3)
        for changed in (text.replace('sample=3','sample=2',1),text.replace('ms=.125','ms=nan',1),text.replace('width=1280','width=64',1)):
            with self.assertRaises(AssertionError):live.validate_timing(changed,True,1280,768)

    def test_consume_only_source_contract(self):
        source=Path(live.__file__).read_text()
        self.assertNotIn('cmake',source);self.assertNotIn('build_motion_output.sh',source)
        self.assertIn("assert bottle.BOTTLE=='X3'",source)
        self.assertIn("assert not game_running()",source)
        self.assertEqual(len(live.PROGRAMS),5)


if __name__=='__main__':unittest.main()
