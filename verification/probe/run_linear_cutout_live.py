#!/usr/bin/env python3
"""Consume retained fixture/DLL for two exact live cutouts, under wine_lock.py.

Never builds, installs or launches the game. Raw pixels stay in a local temp
folder; the compact report records scoped binary provenance and strict results.
"""
import argparse
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
import fixture_log  # X3M_LOG_FILE: the session log where this runner reads it (logging tiers)
from game_guard import game_running
import run_linear_material as material_reference

ROOT=Path(__file__).resolve().parents[2]
FRAMES=70
COUNTS=(1,4,16)
PROGRAMS=('vs_53a0a641107ed76c.bin','ps_8759c7838bbc86c2.bin',
          'ps_63f96eba9eea7880.bin','vs_4944d81dfe531b37.bin','ps_5e0a10fe752b6140.bin')
FADE_PROGRAMS=('vs_b0602757fce6e870.bin','ps_517540ae6d5e5410.bin')
LIMITATIONS=[
    'Two selected exact shader pairs and the initially admitted RGB-mask7 GE/ref1 state only.',
    'Fixture scene-owner/scope replaces game memory identity; actual native DIP, capability/state admission, HDR, same-draw motion/depth, and TAA remain live.',
    'Native Windows runtime, gameplay appearance and docking-port causality remain unverified.',
    'Native alpha/coverage and fallback RGB twins are exact; combined RGB reuses the detached material oracle in its '
    'gamma-2.2 encoded output space (binary32/binary16 source envelope) with its own tolerance constants '
    '(RGB_REL_TOL .006, RGB_ABS_TOL 2e-5), which already cover the FP16 store.',
    'The routed material arm compares combined RGB at one accepted pixel per frame: the route replaces the color by '
    'design so no exact native twin exists, and the detached oracle evaluates one texel sample, not a per-pixel image.',
    'TAA history publication is independent of cutout admission in every scripted configuration. Inactive-arm refusals '
    '(unsupported/retrying capabilities, nonzero bias) are asserted to retain history from the TAA rows; active-arm '
    'refusals dropping history are asserted only by the fixture history script; non-TAA configurations only prove no reference is generated.',
    'Nonzero mip bias is a refusal control, not admitted cutout support.',
    'EVENT/QPC completion includes CPU and GPU submission cost and excludes setup/readbacks; it is not game FPS.',
    'Near-coplanar changing-alpha edge errors are reported against current color without a selected artistic acceptance threshold; unavailable shader objects and state-lost rollback remain separate qualification gaps.',
]


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)',line))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plan(frame,material=True,bias=0,mixed=False):
    if mixed:frame+=70
    pair=0 if frame>=64 else frame//16 if frame<32 else frame%2
    step=frame%16 if frame<32 else (1,5,1,7,8,1)[frame-64] if 64<=frame<70 else 1
    wrong=frame-32 if 32<=frame<45 else 1 if frame in (60,71) else -1
    cap=frame-45 if 45<=frame<=54 else 10 if frame==56 else 0
    routed=bool(material and bias==0 and wrong<0 and cap==0) or step==10
    return dict(pair=pair,step=step,wrong=wrong,cap=cap,routed=int(routed))


def arm_active(frame,bias=0,mixed=False):
    # The cutout arm is inactive with unsupported/retrying capabilities or a
    # nonzero mip bias: a refused pair is then an ordinary native draw that
    # keeps TAA history and never raises a reactive Unavailable.
    return plan(frame,True,bias,mixed)['cap']==0 and bias==0


def missed(frame,material=True,bias=0,mixed=False):
    # Only the exact source-over triple (SRCALPHA/INVSRCALPHA) is exempt; the
    # wrong-3 refusal (ALPHABLENDENABLE on with ONE/ZERO) still misses.
    return int(material and not plan(frame,material,bias,mixed)['routed'] and arm_active(frame,bias,mixed))


def rows(output,prefix):
    return [fields(line) for line in output.splitlines() if line.startswith(prefix)]


def rgb_case(pair,step):
    return dict(pair=21 if pair else 0,fp16=0,reverse=int(step==2),lights=1,affine=0,
                gains=[1.,1.,1.],diffuse=[.5,.25,.75,.75],lightmap=[.125,.25,.0625,.25],
                cube=[.25,.5,.125,1.],mask=.25,material=[.25,.125,.0625],point=[.5,.25,.125],
                dir0=[.375,.25,.5],dir1=[.125,.5,.25],normal=[0.,0.,1.],
                normal_sample=[.25,.5,.75,.5],binormal=[0.,1.,0.],tangent=[1.,0.,0.],
                camera=[0.,0.,4.],fog_clip=[.75,.125],
                glow=1. if step==7 else .375 if step==8 else 0.,flags=material_reference.FOG if step==9 else 0)


def validate_report(output,material=True,depth=True,taa=True,bias=0,mixed=False):
    frames=3 if mixed else FRAMES
    terminal=rows(output,'RESULT PASS ')
    assert len(terminal)==1 and not any(l.startswith('RESULT FAIL') for l in output.splitlines()),'fixture did not complete'
    assert int(terminal[0]['frames'])==frames and int(terminal[0]['checks'])>0 and int(terminal[0]['restorations'])>0
    summary=rows(output,'CUTOUT_CHECKS ')
    assert len(summary)==1 and summary[0]==dict(frames=str(frames),benchmark='0',pairs='2')
    live=rows(output,'CUTOUT_LIVE ')
    assert [int(r['frame']) for r in live]==list(range(frames)),'ordered complete unique live rows'
    accepted=[];temporal=[];owned=0
    for frame,row in enumerate(live):
        expected=plan(frame,material,bias,mixed)
        for key,value in expected.items():assert int(row[key])==value,(frame,key,'plan mismatch')
        assert int(row['material'])==material and int(row['depth'])==depth
        n=int(row['accepted']);holes=int(row['holes']);assert 0<=n<4096 and n+holes==4096 and holes>0
        if expected['wrong']<0 and expected['step'] not in (5,12):assert n>0,'missing accepted witness'
        if expected['step'] in (5,12):assert n==0,'alpha/depth rejection witness'
        own=int(row['owned']);assert own==n*expected['routed'];owned+=own
        if expected['step']!=10:
            assert int(row['routed_delta'])==expected['routed']
            assert int(row['missed_delta'])==missed(frame,material,bias,mixed)
        assert int(row['unavailable'])==missed(frame,material,bias,mixed)
        if material:
            cap=expected['cap'];assert int(row['cap_status'])==(2 if 1<=cap<=8 else 3 if cap>=9 else 1),(frame,'capability verdict/recovery')
        accepted.append(n)
        if taa:temporal.append(frame)
    assert int(terminal[0]['taa_reference_frames'])==len(temporal)
    assert int(terminal[0]['taa_skipped_frames'])==0
    # Inactive-arm refusals (capability block, nonzero bias before the first
    # Reset) keep TAA history: only a scripted cut runs current-only.
    retained=[f for f in range(1,frames) if material and taa and not arm_active(f,bias,mixed) and (plan(f,material,bias,mixed)['cap']>0 or f<15)]
    if retained:
        taa_rows={int(r['frame']):r for r in rows(output,'TAA ')}
        for f in retained:
            r=taa_rows[f];assert int(r['history'])==int(r['cut']=='0') and r['skipped']=='0',(f,'inactive arm must retain TAA history',r['history'],r['cut'])
    samples=rows(output,'CUTOUT_RGB ')
    expected_samples=[f for f in range(frames) if material and plan(f,material,bias,mixed)['routed'] and plan(f,material,bias,mixed)['wrong']<0 and f not in (58,59,61) and accepted[f]]
    assert [int(r['frame']) for r in samples]==expected_samples,'complete unique independent RGB samples'
    maximum=0
    for sample in samples:
        frame=int(sample['frame']);p=plan(frame,material,bias,mixed);assert int(sample['pair'])==p['pair'] and int(sample['step'])==p['step'] and int(sample['reverse'])==(p['step']==2)
        c=rgb_case(p['pair'],p['step'])
        # The scene stores the route's gamma-2.2 encoded output (X3M_HDR_DECODE=gamma2.2),
        # so compare the encoded RGBA envelope exactly as run_linear_material does,
        # never the oracle's linear_rgb intermediate.
        ideal=material_reference.expected(c);quantized=material_reference.expected(c,half_source=True)
        actual=tuple(map(float,sample['rgb'].split(',')));assert len(actual)==3
        for k,a in enumerate(actual):
            lo=min(ideal.encoded_rgba[k],quantized.encoded_rgba[k]);hi=max(ideal.encoded_rgba[k],quantized.encoded_rgba[k])
            tolerance=material_reference.RGB_ABS_TOL+material_reference.RGB_REL_TOL*max(abs(lo),abs(hi))
            fraction=max(lo-a,a-hi,0.)/tolerance;assert math.isfinite(a) and fraction<=1,(frame,'combined RGB oracle',a,lo,hi,fraction);maximum=max(maximum,fraction)
    if mixed:
        fade=rows(output,'CUTOUT_FADE ');assert [(int(r['frame']),int(r['source'])) for r in fade]==[(f,i) for f in range(3) for i in (0,1)]
        assert all(r['prepared']==r['original_calls']=='1' for r in fade)
        union=rows(output,'CUTOUT_UNION ');assert [(int(r['frame']),int(r['covered']),int(r['unavailable'])) for r in union]==[(0,1536,0),(1,1536,1),(2,1536,0)]
        # The routed frames upload the fixture's valid fade mask (supplemental reactive policy); the
        # refused frame proves a valid mask cannot hide missing cutout motion. Counts are cumulative
        # before each frame's boundary, so the summary carries the last upload.
        assert [(int(r['mask_valid']),int(r['reactive_uploads'])) for r in union]==[(1,0),(0,1),(1,1)],'mixed reactive mask uploads'
        summary=rows(output,'CUTOUT_UNION_SUMMARY ');assert len(summary)==1 and int(summary[0]['reactive_uploads'])==2,'mixed reactive mask uploads'
    return dict(mixed=mixed,frames=frames,checks=int(terminal[0]['checks']),owned_pixels=owned,accepted_pixels=accepted,temporal_frames=temporal,rgb_samples=len(samples),max_rgb_tolerance_fraction=maximum,history_retained_frames=len(retained),reactive_uploads=int(summary[0]['reactive_uploads']) if mixed else 0)


def validate_pixels(work,result,depth=True):
    hashes={k:[] for k in ('color','motion','depth','coverage','temporal')}
    for frame in range(result['frames']):
        for kind,lanes in (('color',4),('motion',4),('depth',1 if depth else 0),('coverage',1)):
            blob=(work/f'cutout_{kind}_{frame}.f32').read_bytes();assert len(blob)==4096*lanes*4,(frame,kind,'pixel length')
            values=[v[0] for v in struct.iter_unpack('<f',blob)]
            assert all(math.isfinite(v) for v in values),(frame,kind,'nonfinite pixel')
            if kind=='coverage':assert set(values)<=set((0.,1.)) and sum(values)==result['accepted_pixels'][frame]
            hashes[kind].append(hashlib.sha256(blob).hexdigest())
        actual=work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f';reference=work/f'reference_taa_{frame}.rgba16f'
        if frame in result['temporal_frames']:
            a=actual.read_bytes();r=reference.read_bytes();assert len(a)==4096*8 and a==r,(frame,'actual TAA/reference mismatch')
            assert all(math.isfinite(p[0]) for p in struct.iter_unpack('<e',a)),(frame,'nonfinite resolved pixel')
            hashes['temporal'].append(hashlib.sha256(a).hexdigest())
        else:
            assert not reference.exists(),'refused or no-TAA frame generated reference history'
            hashes['temporal'].append(None)
    return hashes


def near_coplanar_metrics(work,result):
    if result['mixed'] or not result['temporal_frames']:return []
    metrics=[]
    for frame in range(65,70):
        current=[p[:3] for p in struct.iter_unpack('<4f',(work/f'cutout_color_{frame}.f32').read_bytes())]
        resolved=[p[:3] for p in struct.iter_unpack('<4e',(work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f').read_bytes())]
        previous=[p[0] for p in struct.iter_unpack('<f',(work/f'cutout_coverage_{frame-1}.f32').read_bytes())]
        coverage=[p[0] for p in struct.iter_unpack('<f',(work/f'cutout_coverage_{frame}.f32').read_bytes())]
        for kind in ('changed','stable'):
            indices=[i for i,(a,b) in enumerate(zip(previous,coverage)) if (a!=b)==(kind=='changed')]
            errors=[abs(resolved[i][c]-current[i][c]) for i in indices for c in range(3)]
            metrics.append(dict(frame=frame,coverage=kind,pixels=len(indices),max_linear_rgb_error=max(errors,default=0),rms_linear_rgb_error=math.sqrt(sum(e*e for e in errors)/len(errors)) if errors else 0))
    return metrics


def validate_trace(trace,result,material,depth,taa,shadow,lazy,bias):
    records=rows(trace,'motion_output_frame ')
    assert [int(r['frame']) for r in records]==list(range(result['frames'])),'complete native frame counters'
    native_gets=queries=hits=0
    for frame,row in enumerate(records):
        expected=plan(frame,material,bias,result['mixed'])
        assert int(row['draws'])==(5 if result['mixed'] else 3)
        assert int(row['routed'])==1+expected['routed']
        assert int(row['depth'])==depth and int(row['taa_resolved'])==taa
        assert int(row['apply_failures'])==int(row['restore_failures'])==0
        assert int(row['present'],16)==0
        assert int(row['state_shadow'])==shadow and row['rt_mode']==('lazy' if lazy else 'perdraw')
        q,h,g=(int(row[k]) for k in ('rs_queries','rs_hits','rs_gets'));assert q>=h>=0 and g>=0
        if not shadow:assert h==0
        queries+=q;hits+=h;native_gets+=g
    materials=rows(trace,'linear_material_frame ')
    if material:
        assert [int(r['frame']) for r in materials]==list(range(result['frames']))
        for frame,row in enumerate(materials):
            p=plan(frame,material,bias,result['mixed'])
            assert int(row['cutout_routed'])==(p['routed'] if p['step']!=10 else 0)
            assert int(row['cutout_missed'])==missed(frame,material,bias,result['mixed'])
            assert int(row['cutout_unavailable'])==missed(frame,material,bias,result['mixed'])
            # Only the selected live post-mutation bind faults cause failures.
            assert int(row['bind_failures'])==int(not result['mixed'] and frame in (58,59) and not bias)
    return dict(render_state_queries=queries,render_state_hits=hits,native_render_state_gets=native_gets)


def validate_timing(output,material,width,height,ordinary=False):
    terminal=rows(output,'RESULT PASS ');assert len(terminal)==1 and int(terminal[0]['frames'])==18 and not rows(output,'RESULT FAIL ')
    summary=rows(output,'CUTOUT_CHECKS ');assert summary==[dict(frames='18',benchmark='1',pairs='2')]
    timing=rows(output,'CUTOUT_TIMING ');assert [(int(r['count']),int(r['sample'])) for r in timing]==[(c,s) for c in COUNTS for s in range(4)]
    result={str(c):[] for c in COUNTS}
    for row in timing:
        assert int(row['material'])==material and int(row['ordinary'])==ordinary and int(row['width'])==width and int(row['height'])==height
        value=float(row['ms']);assert math.isfinite(value) and value>=0;result[row['count']].append(value)
    return dict(frames=18,warmups_per_count=2,counts=result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-cutout-live.json')
    parser.add_argument('--functional-only',action='store_true',help='bounded functional iteration; report explicitly excludes paired cost')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','new qualification requires X3'
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/name for name in PROGRAMS+FADE_PROGRAMS]];assert all(p.is_file() for p in inputs)
    raw=Path(tempfile.mkdtemp(prefix='x3-cutout-live-'));hashes={str(p):sha(p) for p in inputs}
    result=dict(passed=False,bottle=bottle.describe(),inputs=hashes,raw=str(raw),game_launched=False,limitations=LIMITATIONS,cases={})
    runs=[dict(name=f'lazy{lazy}-depth1-material1',lazy=lazy,depth=1,material=1,taa=1,bias=0,shadow=1) for lazy in (0,1)]
    runs += [dict(name='motion-only-depth0',lazy=1,depth=0,material=1,taa=0,bias=0,shadow=1),dict(name='feature-off',lazy=1,depth=1,material=0,taa=0,bias=0,shadow=1),dict(name='shadow-off',lazy=1,depth=1,material=1,taa=1,bias=0,shadow=0),dict(name='nonzero-bias-refused',lazy=1,depth=1,material=1,taa=1,bias=-.5,shadow=1)]
    runs.append(dict(name='mixed-fade',lazy=1,depth=1,material=1,taa=1,bias=0,shadow=1,mixed=True))
    if not args.functional_only:
        for pair,order in enumerate(((0,1,2),(2,1,0))):
            for route in order:runs.append(dict(name=f'timing-pair{pair}-route{route}',lazy=1,depth=1,material=int(route>0),ordinary=int(route==1),taa=0,bias=0,shadow=1,timing=True,width=1280,height=768))
    try:
        for run in runs:
            assert not game_running(),'game running; fixture refused'
            work=raw/run['name'];work.mkdir();shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_FIXTURE_CUTOUT_MIXED=str(int(run.get('mixed',False))),X3M_FIXTURE_CUTOUT_ORDINARY=str(run.get('ordinary',0)),X3M_MOTION_OUTPUT='1',X3M_HDR='1',X3M_HDR_TONEMAP='agx',X3M_HDR_DECODE='gamma2.2',X3M_HDR_EXPOSURE='manual',X3M_HDR_EV_MANUAL='0',X3M_HDR_CLAMP='0',X3M_HDR_BLOOM='0',X3M_LINEAR_MATERIALS=str(run['material']),X3M_MATERIAL_DIRECT_GAIN='1',X3M_MATERIAL_EMISSIVE_GAIN='1',X3M_LIGHTMAP_EMISSIVE_GAIN='1',X3M_LINEAR_DISTANCE_FADE=str(int(run.get('mixed',False))),X3M_LINEAR_EMISSIONS='0',X3M_OWNERSHIP='1',X3M_TAA=str(run['taa']),X3M_MOTION_JITTER='1',X3M_FIXTURE_TAA_SENTINEL='2',X3M_TAA_SHARPEN='0',X3M_TAA_MIP_BIAS=str(run['bias']),X3M_FIXTURE_MOTION_DEPTH=str(run['depth']),X3M_FIXTURE_CAMERA='rotate',X3M_SCENE_HOOK='0',X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',X3M_STATE_SHADOW=str(run['shadow']),X3M_MOTION_RT_MODE='lazy' if run['lazy'] else 'perdraw',X3M_TAA_DEBUG='0' if run.get('timing') else '1',X3M_CAPTURE_START='1000000',X3M_CAPTURE_FRAMES='0',WINEDLLOVERRIDES='d3d9=n,b')
            command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(inputs[2]),'Z:'+str(inputs[3]),'cutoutbench' if run.get('timing') else 'cutout']
            if run.get('timing'):command.append(f"{run['width']}x{run['height']}")
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as error:child=subprocess.run(command,env={**env, **fixture_log.session_log_env(work)},stdout=out,stderr=error,timeout=180)
            assert child.returncode==0,(run['name'],'fixture failed',str(work))
            output=(work/'stdout.txt').read_text()
            if run.get('timing'):case=validate_timing(output,run['material'],run['width'],run['height'],run.get('ordinary',False))
            else:
                case=validate_report(output,run['material'],run['depth'],run['taa'],run['bias'],run.get('mixed',False));logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1;case['state_cost']=validate_trace(logs[0].read_text(),case,run['material'],run['depth'],run['taa'],run['shadow'],run['lazy'],run['bias']);case['pixel_hashes']=validate_pixels(work,case,run['depth']);case['near_coplanar_edge_metrics']=near_coplanar_metrics(work,case)
            case['seconds']=round(time.monotonic()-start,3);result['cases'][run['name']]=case;print(f"{run['name']}: PASS {case['frames']} frames {case['seconds']} s",flush=True)
        base=result['cases']['lazy1-depth1-material1']
        for twin in ('lazy0-depth1-material1','shadow-off'):assert base['pixel_hashes']==result['cases'][twin]['pixel_hashes'],(twin,'state optimization changes image/motion/coverage/TAA')
        result['paired_cost']=[]
        if not args.functional_only:
            for count in COUNTS:
                for pair in (0,1):
                    modes=[result['cases'][f'timing-pair{pair}-route{route}']['counts'][str(count)] for route in range(3)]
                    result['paired_cost'].append(dict(width=1280,height=768,draws=count,order='native/ordinary/combined' if pair==0 else 'combined/ordinary/native',
                        native_median_ms=statistics.median(modes[0]),ordinary_median_ms=statistics.median(modes[1]),combined_median_ms=statistics.median(modes[2]),
                        combined_native_delta_ms=statistics.median(b-a for a,b in zip(modes[0],modes[2])),combined_ordinary_delta_ms=statistics.median(b-a for a,b in zip(modes[1],modes[2]))))
        assert hashes=={str(p):sha(p) for p in inputs},'retained inputs changed'
        result['paired_cost_qualified']=not args.functional_only;result['passed']=True
    finally:
        path=args.result if result['passed'] else raw/'failed-result.json';path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS result={args.result}',flush=True)


if __name__=='__main__':main()
