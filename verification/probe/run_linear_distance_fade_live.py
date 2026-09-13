#!/usr/bin/env python3
"""Consume-only distance-fade runtime/coverage qualification and paired cost.

Root owns the matching candidate seam DLL and the Wine lease. No builds or game
launches are performed. The detached fixture owns the broad material equations;
this script reuses its oracle for six bounded actual-route color witnesses.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import tempfile
import time

import bottle
from game_guard import game_running
import run_linear_distance_fade as component
import run_linear_material as material

ROOT=Path(__file__).resolve().parents[2]
BOOTSTRAP=('vs_53a0a641107ed76c.bin','ps_8759c7838bbc86c2.bin')
FADE_PAIRS=tuple(material.PAIRS[110:116])
EMISSION_PAIR=('d5e1c75351ed3f04','8360f422de08b5bd')
PROGRAMS=tuple(dict.fromkeys(BOOTSTRAP+tuple(f'vs_{v}.bin' for v,_ in FADE_PAIRS)+tuple(f'ps_{p}.bin' for _,p in FADE_PAIRS)+(f'vs_{EMISSION_PAIR[0]}.bin',f'ps_{EMISSION_PAIR[1]}.bin')))
FRAMES=30
FAILED_SOURCE=22
PRESENT_FRAMES=tuple(range(2,14,2))
RESOLUTIONS=((1280,768),(1920,1080))
COUNTS=(1,4,16)
SCOPE=('Actual capture DIP / MotionOutput / shared additive-and-fade composition pool / '
       'Hdr owning exchange / supplemental TemporalPass. Six exact Asteroid pairs, one '
       'existing additive pair, and two bootstrap programs; no broad detached equation rerun.')
LIMITATIONS=[
    'A fixture-only scene-owner admission seam replaces game owner-memory binding; actual capture, render-state admission, original DIP, HDR exchange, supplemental TAA and terminal publication remain exercised.',
    'The six bounded linear-color witnesses reuse the detached Asteroid oracle and fixed observed X3 FP16 render-target round-toward-zero model. This store rule is not a native-Windows guarantee.',
    'Current/previous reactive coverage rejects invalid blended history; it does not implement layered transparent temporal accumulation or establish a shimmer fix.',
    'A real invalid-index-buffer source establishes one failed native call and no completed history. Partial driver submission and unrelated capability matrices retain existing host/component evidence.',
    'Timing toggles only fade, with the same material/motion/TAA/emission settings. EVENT-fenced source and terminal windows exclude setup, the frame-level M clear, and readbacks; paired processes identify order effects but are not GPU timestamps or game FPS.',
    'Native Windows, installation, gameplay appearance and docking-port distance-transition causality remain unverified.',
]


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)',line))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgba(value):
    result=tuple(map(float,value.split(',')))
    assert len(result)==4 and all(math.isfinite(x) for x in result)
    return result


def sample_case(pair,zero=False):
    case=copy.deepcopy(component.cases()[10*pair])
    case.update(pair=110+pair,fp16=0,gains=[1.,1.,1.],flags=material.FOG)
    if zero:case['diffuse'][3]=0.
    return case


def expected_composite(before,pair,zero=False,overlap=1):
    c=sample_case(pair,zero)
    linear=material.expected(c).linear_rgb
    # The live original retains material alpha .625; the detached source fixture
    # deliberately overwrote it with one. All factors here are binary-exact.
    alpha=.625*component.source_alpha(c)
    q=0.;energy=[0.]*3
    for _ in range(overlap):
        q=component.fp16_rt_store(alpha+(1-alpha)*q)
        energy=[component.fp16_rt_store(alpha*x+(1-alpha)*old) for x,old in zip(linear,energy)]
    return component.compose(before,energy,q)


def native_baselines(rows):
    assert [int(row['pair']) for row in rows]==list(range(6))
    result={}
    for row in rows:
        before,after=rgba(row['before']),rgba(row['after'])
        assert before==(1.,1.,1.,1.) and after[3]==1.
        result[int(row['pair'])]=after
    return result


def expected_emission(before,enabled):
    if enabled:
        energy=[component.fp16_rt_store(x**2.2*.5) for x in (.5,.25,.125)]
        rgb=[component.fp16_rt_store((max(before[i],0)**2.2+energy[i])**(1/2.2)) for i in range(3)]
    else:rgb=[component.fp16_rt_store(a+.5*b) for a,b in zip(before,(.5,.25,.125))]
    return tuple(rgb)+(before[3]+.125,)


def validate_samples(rows,fade,emission,native):
    seen=set();maximum=0.;previous={}
    singles=(1,*PRESENT_FRAMES)
    expected_keys={(f,0,x,32) for f in singles for x in (16,32)}
    expected_keys|={(f,i,32,32) for f in range(15,19) for i in range(len(source_plan(f)))}
    for row in rows:
        frame,source,x,y=(int(row[k]) for k in ('frame','source','x','y'))
        key=(frame,source,x,y)
        assert key not in seen and key in expected_keys
        seen.add(key)
        kind,pair,_,_=source_plan(frame)[source]
        overlap=2 if frame==15 else 1
        assert int(row['pair'])==pair and row['kind']==kind and int(row['overlap'])==overlap
        assert float(row['alpha'])==(.125 if kind=='emission' else 0. if frame==1 else .078125)
        before,after=rgba(row['before']),rgba(row['after'])
        if source==0:assert before[:3]==(1.,1.,1.),(key,'known ordinary background')
        else:assert before==previous[frame],(key,'ordered original source chain')
        previous[frame]=after
        if kind=='fade':assert before[3]==after[3],(key,'native target alpha')
        if frame==1:
            assert all(component.same_float(a,b) for a,b in zip(before,after)),(key,'zero-alpha raw A')
            continue
        if kind=='emission':wanted=expected_emission(before,emission)
        elif fade:wanted=expected_composite(before,pair,overlap=overlap)
        elif frame in PRESENT_FRAMES:
            assert all(component.same_float(a,b) for a,b in zip(after[:3],native[pair][:3])),(key,'native original-program FP16 twin')
            continue
        else:
            # Only the native mixed/overlap recurrence uses this calibrated
            # original shader result. Linear L is always the independent CPU
            # oracle. One FP16 calibration store is covered by RGB tolerance.
            alpha=.078125
            original=[(v-(1-alpha))/alpha for v in native[pair][:3]]
            wanted=before
            for _ in range(overlap):wanted=tuple(component.fp16_rt_store(alpha*v+(1-alpha)*a) for a,v in zip(wanted,original))+(before[3],)
        for a,b in zip(after[:3],wanted[:3]):
            fraction=abs(a-b)/(.006*abs(b)+.00002)
            maximum=max(maximum,fraction)
            assert fraction<=1,(key,'actual source policy/order',a,b,fraction)
    assert seen==expected_keys,'zero, six independent pair witnesses, and overlap/mixed order'
    return dict(samples=len(seen),max_tolerance_fraction=maximum)


def source_plan(frame):
    """Producer submissions; ordinary background/opaque draws are separate."""
    if frame==1:return [('fade',0,0,False)]
    if frame in PRESENT_FRAMES:return [('fade',(frame-2)//2,0,False)]
    plans={
        15:[('fade',0,0,False)],16:[('fade',0,0,False),('fade',0,0,False)],
        17:[('fade',0,0,False),('emission',0,0,False)],
        18:[('emission',0,0,False),('fade',0,0,False)],
        19:[('emission',0,0,False),('fade',0,3,False),('emission',0,0,False)],
        20:[('fade',0,0,False),('emission',0,3,False),('fade',0,0,False)],
        21:[('fade',0,6,False)],
        22:[('emission',0,0,False),('fade',0,0,True)],
        23:[('fade',0,0,False),('emission',0,0,False)],
        24:[('fade',0,0,False),('emission',0,7,False),('fade',0,0,False)],
        25:[('fade',0,0,False),('emission',0,0,False)],
        26:[('fade',0,0,False)],
        27:[('fade',0,0,False),('emission',0,0,False)],
        28:[('emission',0,0,False),('fade',0,0,False)],
    }
    return plans.get(frame,[])


def expected_sources(frame,fade,emission):
    stopped=False;result=[]
    for kind,pair,fault,failed in source_plan(frame):
        active=bool(fade if kind=='fade' else emission)
        applied_fault=fault if active else 0
        prepared=active and not stopped and applied_fault!=3
        linear=prepared and not (failed or applied_fault in (6,7))
        native=prepared and applied_fault==6 and not failed
        incomplete=prepared and (failed or applied_fault==7)
        if active and (applied_fault==3 or incomplete):stopped=True
        result.append(dict(kind=kind,pair=pair,fault=applied_fault,hr=0x8876086c if failed else 0,
                           original_calls=1,prepared=int(prepared),linear=int(linear),native=int(native),
                           incomplete=int(incomplete),mask_valid=bool((fade or emission) and not stopped)))
    return result,stopped


def indexed(lines,prefix,key):
    rows=[fields(line) for line in lines if line.startswith(prefix)]
    assert len(rows)==len({int(r[key]) for r in rows}),(prefix,'duplicate evidence')
    return {int(r[key]):r for r in rows}


def validate_functional(output,trace,fade,emission,lazy):
    lines=output.splitlines();traces=trace.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    summary=terminal[0]
    assert int(summary['frames'])==FRAMES and int(summary['checks'])>0 and int(summary['restorations'])>0
    assert int(summary['taa_reference_frames'])==FRAMES-1 and int(summary['taa_skipped_frames'])==1
    live=indexed(lines,'FADE_LIVE ','frame')
    assert set(live)==set(range(FRAMES))
    source_rows=[fields(line) for line in lines if line.startswith('FADE_SOURCE ')]
    assert [(int(r['frame']),int(r['source'])) for r in source_rows]==[(f,i) for f in range(FRAMES) for i in range(len(source_plan(f)))]
    by_frame={f:[] for f in range(FRAMES)}
    for row in source_rows:by_frame[int(row['frame'])].append(row)
    required=int(emission)+2*int(fade);total_sources=0
    details=[]
    for frame,row in live.items():
        assert int(row['fade'])==fade and int(row['emission'])==emission
        status=[int(row[f's{i}']) for i in range(22)]
        expected,stopped=expected_sources(frame,fade,emission)
        assert status[0]==bool(required) and status[1]==bool(required and not stopped)
        assert status[16]==status[18]==status[19]==required
        assert status[20]==(7+bin(required).count('1') if required else 0)
        assert status[21]==(4 if required else 0), 'one shared four-target pool per attach'
        assert status[2]==status[3]==status[9]==0,'recoverable controls must not retain lost/quarantined/suppressed state'
        assert status[4]==sum(s['prepared'] for s in expected)
        assert status[5]==sum(s['linear'] for s in expected)
        assert status[6]==sum(s['native'] for s in expected)
        assert status[7]==sum(s['incomplete'] for s in expected)
        assert status[10]==status[4],'all prepared controls publish linear or recovered B'
        assert status[12]==int(row['draws'])==len(expected),'each original source DIP is submitted once'
        assert status[13]==sum(s['kind']=='fade' for s in expected)*bool(fade)
        assert status[11]==(0x8876086c if frame==FAILED_SOURCE and fade else 0)
        assert status[14]==sum(s['prepared'] for s in expected if s['kind']=='fade')
        assert status[15]==sum(s['linear'] for s in expected if s['kind']=='fade')
        if required:assert status[17]==stopped
        prior_mask=bool(required)
        for source,(actual,wanted) in enumerate(zip(by_frame[frame],expected)):
            assert actual['kind']==wanted['kind'] and int(actual['pair'])==wanted['pair']
            assert int(actual['fault'])==wanted['fault'] and int(actual['hr'],16)==wanted['hr']
            assert int(actual['mask_before'])==prior_mask and int(actual['mask_after'])==wanted['mask_valid']
            prior_mask=wanted['mask_valid']
            assert int(actual['overlap'])==(2 if frame==15 else 1)
            alpha=(.25 if source>=2 else .125) if wanted['kind']=='emission' else (0. if frame==1 else .078125)
            assert float(actual['alpha'])==alpha
            for key in ('original_calls','prepared','linear','native'):
                assert int(actual[key])==wanted[key],(frame,source,key,actual[key],wanted[key])
            if not wanted['prepared']:assert actual['hash_mask_before']==actual['hash_mask_after'],(frame,source,'unprepared source changed shared M bytes')
        total_sources+=len(expected)
        details.append(dict(mask_valid=bool(status[1]),prepared=status[4],linear=status[5],native=status[6],incomplete=status[7],
                            alpha=row['hash_alpha'],motion=row['hash_motion'],depth=row['hash_depth'],mask=row['hash_mask']))
    geometry=indexed(lines,'FADE_GEOMETRY ','frame')
    assert set(geometry)==set(range(FRAMES))
    assert all(float(row['ordinary_t'])==.03125*(f%3) for f,row in geometry.items())
    cameras=[fields(line) for line in lines if line.startswith('FADE_CAMERA ')]
    assert {int(row['frame']) for row in cameras}==set(range(FRAMES))
    translations=[tuple(map(float,row['view_translation'].split(','))) for row in cameras]
    assert all(len(v)==3 and all(math.isfinite(x) for x in v) for v in translations) and len(set(translations))>2
    assert [fields(line) for line in lines if line.startswith('FADE_OPAQUE_RETURN ')]==[dict(frame='13',matched='1')]
    assert [fields(line) for line in lines if line.startswith('FADE_REJECTED ')]==[dict(frame='22',source_failed='1',taa='0',history_seeded='0',copy_exact='1')]
    checks=[fields(line) for line in lines if line.startswith('FADE_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==FRAMES and int(checks[0]['submissions'])==total_sources
    assert checks[0]['qualified']=='1' and checks[0]['benchmark']=='0'
    assert sum(line=='RESET PASS' for line in lines)==1
    resets=[fields(line) for line in lines if line.startswith('FADE_RESET ')]
    assert len(resets)==1 and int(resets[0]['refs'])==(3+bin(required).count('1') if required else 0)
    assert int(resets[0]['allocations'])==(4 if required else 0)
    releases=[fields(line) for line in traces if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1
    motion=indexed(traces,'motion_output_frame ','frame')
    assert set(motion)==set(range(FRAMES))
    for frame,row in motion.items():
        assert row['rt_mode']==('lazy' if lazy else 'perdraw')
        assert int(row['apply_failures'])==int(row['restore_failures'])==0
        assert int(row['taa_resolved'])==int(frame!=FAILED_SOURCE)
        if frame==FAILED_SOURCE:assert int(row['taa_history'])==0
    temporal=indexed(traces,'motion_output_taa_readback ','frame')
    assert set(temporal)==set(range(FRAMES))-{FAILED_SOURCE}
    assert all(row['result']=='00000000' for row in temporal.values())
    native=native_baselines([fields(line) for line in lines if line.startswith('FADE_NATIVE ')])
    sample_result=validate_samples([fields(line) for line in lines if line.startswith('FADE_SAMPLE ')],fade,emission,native)
    return dict(frames=FRAMES,checks=int(summary['checks']),restorations=int(summary['restorations']),
                sources=total_sources,held_references=int(releases[0]['held']),frames_detail=details,**sample_result)


def validate_pixels(work,fade,emission):
    temporal=[];alpha_hashes=[];masks=[]
    for frame in range(FRAMES):
        actual_path=work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f'
        reference_path=work/f'reference_taa_{frame}.rgba16f'
        if frame==FAILED_SOURCE:
            assert not actual_path.exists() and not reference_path.exists(),'failed native source must not publish or seed TAA'
            temporal.append(None)
        else:
            actual=actual_path.read_bytes();reference=reference_path.read_bytes()
            assert len(actual)==64*64*8 and actual==reference,(frame,'actual supplemental TAA differs from independent reference')
            temporal.append(hashlib.sha256(actual).hexdigest())
        color=(work/f'distance_fade_color_{frame}.rgba32f').read_bytes()
        mask=(work/f'distance_fade_mask_{frame}.rgba32f').read_bytes()
        assert len(color)==len(mask)==64*64*16
        colors=list(struct.iter_unpack('<4f',color));values=list(struct.iter_unpack('<4f',mask))
        assert all(math.isfinite(v) for p in colors for v in p)
        assert all(math.isfinite(v) and v>=0 for p in values for v in p[:3])
        expected=[False]*(64*64)
        sources,_=expected_sources(frame,fade,emission)
        for source,s in enumerate(sources):
            if not s['prepared'] or s['hr']:continue
            left,right=(24,56) if source else (8,40)
            for y in range(16,48):
                for x in range(left,right):expected[y*64+x]=True
        for index,(pixel,wanted) in enumerate(zip(values,expected)):
            assert all((v>0)==wanted for v in pixel[:3]),(frame,index,'actual shared M footprint, including invalid-but-retained bytes')
        masks.append(sum(expected))
        alpha_hashes.append(hashlib.sha256(b''.join(struct.pack('<f',p[3]) for p in colors)).hexdigest())
    return dict(temporal_sha256=temporal,alpha_sha256=alpha_hashes,covered_pixels=masks)


def validate_admission(output,trace,taa,hdr):
    lines=output.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    assert int(terminal[0]['frames'])==4 and int(terminal[0]['checks'])>0
    live=indexed(lines,'FADE_LIVE ','frame');assert set(live)==set(range(4))
    for frame,row in live.items():
        status=[int(row[f's{i}']) for i in range(22)]
        assert int(row['fade'])==1 and int(row['emission'])==1
        assert all(status[k]==0 for k in (0,1,4,5,6,7,9,10,14,15,16,18,19,20,21)),(frame,'missing prerequisite must not enhance or claim supplemental availability')
        assert status[12]==int(frame!=0)
    source=[fields(line) for line in lines if line.startswith('FADE_SOURCE ')]
    assert [(int(r['frame']),int(r['source'])) for r in source]==[(f,0) for f in (1,2,3)]
    assert all(int(r['hr'],16)==0 and int(r['original_calls'])==1 and int(r['prepared'])==int(r['linear'])==0 for r in source)
    releases=[fields(line) for line in trace.splitlines() if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1
    return dict(frames=4,checks=int(terminal[0]['checks']),taa=taa,hdr=hdr,sources=3,held_references=int(releases[0]['held']))


def validate_timing(output,fade,width,height):
    lines=output.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    assert int(terminal[0]['frames'])==18
    rows=[fields(line) for line in lines if line.startswith('FADE_TIMING ')]
    assert [(int(r['count']),int(r['sample'])) for r in rows]==[(c,s) for c in COUNTS for s in range(4)]
    result={}
    for row in rows:
        count=int(row['count'])
        assert int(row['width'])==width and int(row['height'])==height and int(row['fade'])==fade and int(row['emission'])==0
        values={k:float(row[k+'_ms']) for k in ('source','terminal','total')}
        assert all(math.isfinite(v) and v>=0 for v in values.values())
        assert abs(values['total']-values['source']-values['terminal'])<=3e-9,'full cost must share the two fenced timestamps'
        result.setdefault(str(count),[]).append(values)
    checks=[fields(line) for line in lines if line.startswith('FADE_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==18 and int(checks[0]['benchmark'])==1
    assert int(checks[0]['submissions'])==6*sum(COUNTS)
    return dict(frames=18,warmups_per_count=2,samples_per_count=4,counts=result)


def compare_functional(cases):
    both=cases['lazy1-fade1-emission1'];perdraw=cases['lazy0-fade1-emission1']
    for key in ('temporal_sha256','alpha_sha256','covered_pixels','frames_detail'):
        assert both[key]==perdraw[key],('lazy attachment mode changed result',key)
    for emission in (0,1):
        off,on=(cases[f'lazy1-fade{fade}-emission{emission}'] for fade in (0,1))
        assert off['alpha_sha256']==on['alpha_sha256'],'fade changed native target alpha'
        for a,b in zip(off['frames_detail'],on['frames_detail']):
            assert a['motion']==b['motion'] and a['depth']==b['depth'],'fade changed ordinary temporal attachments'


def paired_cost(cases):
    result=[]
    for width,height in RESOLUTIONS:
        for count in COUNTS:
            pairs=[]
            for pair in (0,1):
                off,on=(cases[f'timing-{width}x{height}-pair{pair}-fade{fade}']['counts'][str(count)] for fade in (0,1))
                deltas={k:[b[k]-a[k] for a,b in zip(off,on)] for k in ('source','terminal','total')}
                pairs.append(dict(order='off/on' if pair==0 else 'on/off',
                                  off_median_ms={k:statistics.median(r[k] for r in off) for k in deltas},
                                  on_median_ms={k:statistics.median(r[k] for r in on) for k in deltas},
                                  paired_window_median_delta_ms={k:statistics.median(v) for k,v in deltas.items()}))
            result.append(dict(width=width,height=height,ordered_dips=count,pairs=pairs))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-distance-fade-live.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','new qualification requires X3'
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/name for name in PROGRAMS]]
    assert all(path.is_file() for path in inputs),'explicit prebuilt inputs and scoped original programs required'
    hashes={str(p):sha(p) for p in inputs}
    raw=Path(tempfile.mkdtemp(prefix='x3-distance-fade-live-'))
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,raw=str(raw),scope=SCOPE,inputs=hashes,cases={},limitations=LIMITATIONS)
    # Batch all three draw counts into each timing process; alternating process
    # order controls drift without multiplying startups for individual counts.
    runs=[dict(name=f'lazy1-fade{fade}-emission{emission}',fade=fade,emission=emission,lazy=1,taa=1,hdr=1) for fade in (0,1) for emission in (0,1)]
    runs.append(dict(name='lazy0-fade1-emission1',fade=1,emission=1,lazy=0,taa=1,hdr=1))
    runs += [dict(name=f'admission-taa{taa}-hdr{hdr}',fade=1,emission=1,lazy=1,taa=taa,hdr=hdr,admission=True) for taa,hdr in ((0,1),(1,0))]
    for width,height in RESOLUTIONS:
        for pair,order in enumerate(((0,1),(1,0))):
            for fade in order:runs.append(dict(name=f'timing-{width}x{height}-pair{pair}-fade{fade}',fade=fade,emission=0,lazy=1,taa=1,hdr=1,timing=True,width=width,height=height))
    try:
        for run in runs:
            assert not game_running(),'game running; no fixture launch'
            work=raw/run['name'];work.mkdir()
            shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1',X3M_HDR=str(run['hdr']),X3M_HDR_TONEMAP='agx',X3M_HDR_DECODE='gamma2.2',
                       X3M_HDR_EXPOSURE='manual',X3M_HDR_EV_MANUAL='0',X3M_HDR_CLAMP='0',X3M_HDR_BLOOM='0',
                       X3M_LINEAR_MATERIALS='1',X3M_MATERIAL_DIRECT_GAIN='1',X3M_MATERIAL_EMISSIVE_GAIN='1',X3M_LIGHTMAP_EMISSIVE_GAIN='1',
                       X3M_LINEAR_DISTANCE_FADE=str(run['fade']),X3M_LINEAR_EMISSIONS=str(run['emission']),X3M_EMISSION_GAIN='1',
                       X3M_OWNERSHIP='1',X3M_TAA=str(run['taa']),X3M_TAA_SENTINEL='2',X3M_TAA_SHARPEN='0',X3M_TAA_MIP_BIAS='-.5',
                       X3M_FIXTURE_CAMERA='rotate',X3M_SCENE_HOOK='0',X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',X3M_STATE_SHADOW='1',
                       X3M_MOTION_RT_MODE='lazy' if run['lazy'] else 'perdraw',X3M_TAA_DEBUG='0' if run.get('timing') else '1',
                       X3M_CAPTURE_START='1000000' if run.get('timing') else '1',X3M_CAPTURE_FRAMES='0',WINEDLLOVERRIDES='d3d9=n,b')
            mode='distancefadebench' if run.get('timing') else 'distancefade'
            command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(inputs[2]),'Z:'+str(inputs[3]),mode]
            if run.get('timing'):command.append(f"{run['width']}x{run['height']}")
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as error:
                child=subprocess.run(command,env=env,stdout=out,stderr=error,timeout=180)
            assert child.returncode==0,f"{run['name']}: fixture failed; {work}"
            logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1
            output=(work/'stdout.txt').read_text();trace=logs[0].read_text()
            if run.get('timing'):case=validate_timing(output,run['fade'],run['width'],run['height'])
            elif run.get('admission'):case=validate_admission(output,trace,run['taa'],run['hdr'])
            else:
                case=validate_functional(output,trace,run['fade'],run['emission'],run['lazy'])
                case.update(validate_pixels(work,run['fade'],run['emission']))
            case['seconds']=round(time.monotonic()-start,3);result['cases'][run['name']]=case
            print(f"{run['name']}: passed, {case['frames']} frames, {case['seconds']} s",flush=True)
        compare_functional(result['cases'])
        result['paired_completion_cost']=paired_cost(result['cases'])
        assert hashes=={str(p):sha(p) for p in inputs},'prebuilt qualification inputs changed'
        result['functional_frames']=150;result['admission_frames']=8;result['timing_frames']=144
        result['passed']=True
    finally:
        path=args.result if result['passed'] else raw/'failed-result.json';path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS result={args.result}',flush=True)


if __name__=='__main__':main()
