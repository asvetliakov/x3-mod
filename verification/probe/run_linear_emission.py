#!/usr/bin/env python3
"""Consume the detached EXE under wine_lock.py; no implicit build or game launch.

The oracle is an authored pixel-by-pixel blend/depth simulation with independent
binary16 stores. Fullscreen round-trip drift is measured against original bits,
separately from the shader-math comparison tolerance. No no-op drift threshold
or live recovery/completeness guarantee is implied by a successful experiment.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import os
import re
import statistics
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT/'verification/probe/build/linear-emission/linear_emission_fixture.exe'
INPUTS = ('verification/probe/linear_emission_fixture.cpp',
          'verification/probe/build_linear_emission.sh',
          'verification/probe/run_linear_emission.py',
          'verification/analysis/test_linear_emission_report.py')
CAP = 65504.
WIDTH = HEIGHT = 16
RGB_REL = .003  # full-precision POW plus binary16 store, no retained _pp lobe
RGB_ABS = 3e-5


def half(x):
    try:
        return struct.unpack('<e', struct.pack('<e', x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def sanitize(x):
    return 0. if math.isnan(x) or x <= 0 else min(x, CAP)


def decode(x):
    x = sanitize(x)
    return sanitize(max(x, 1e-10)**2.2) if x else 0.


def encode(x):
    x = sanitize(x)
    return max(x, 1e-22)**(1/2.2) if x else 0.


def op(kind=1, rect=(.125, .125, .625, .75), z=.4,
       color=(.5, .25, .125, .125), fade=.75, gain=1., affine=0):
    return dict(kind=kind, rect=list(rect), z=z, color=list(color),
                fade=fade, gain=gain, affine=affine)


def fixture_cases():
    cases = []

    def add(label, operations, **kw):
        c = dict(id=len(cases), label=label, mode=1, mask=0, alpha=0,
                 write=15, fault=0, pattern=0, flags=0, ops=copy.deepcopy(operations))
        c.update(kw)
        cases.append(c)

    a = op(affine=1)
    b = op(rect=(.375, .25, .875, .875), color=(.125, .375, .5, 0.), fade=.5, gain=1.)
    screen = op(2, rect=(.25, .125, .875, .875), color=(.25, .125, .5, .25), fade=1.)
    opaque = op(0, rect=(.25, .25, .75, .75), z=.2, color=(.0625, .5, .25, .5), fade=1.)
    late = op(3, rect=(.25, .25, .75, .75), color=(.25, .125, .0625, .125), fade=.5)
    hidden = op(z=.9, color=(.75, .25, .5, .125))
    scenarios = {'overlap': [a, b], 'later_opaque': [a, b, opaque],
                 'intervening_screen': [a, screen, b], 'depth_occluded': [a, hidden, b],
                 'late_boundary': [a, late, b]}
    for label, ops in scenarios.items():
        for alpha in range(3):
            for write in (15, 7):
                for mask in (0, 1):
                    add(label, ops, alpha=alpha, write=write, mask=mask)
        add(label+'_gamma_control', ops, mode=0)
        add(label+'_scene_end_control', ops, mode=2)
    add('isolated_single_draw_brackets', [a, b], flags=2, mask=1)
    add('inherited_scissor_rgb_mask', [a, b, screen], flags=1, mask=1, write=7, alpha=2)
    # Known unsupported synthetic world writers are conservatively covered with
    # depth rejection relaxed. This deliberately includes a hidden screen draw.
    gain_source=copy.deepcopy(b);gain_source['gain']=4.
    add('relaxed_unknown_coverage', [a, op(2,z=.9), gain_source], mask=1)
    for fault, label in ((1,'preflight_refusal'), (2,'decode_refusal'),
                         (3,'post_source_encode_failure'), (4,'post_encode_restore_failure'),
                         (5,'post_source_mask_failure'), (6,'capture_refusal'),
                         (7,'first_source_refusal'), (8,'second_source_refusal')):
        add(label, [a,b], fault=fault, mask=1)
    add('drift_original', [], mode=0, pattern=1)
    zero = op(color=(0,0,0,0), fade=1.)
    for n in (1,16,64):
        add('drift_'+str(n), [x for _ in range(n) for x in (zero,zero,op(4))], pattern=1,alpha=1)
    # Six-by-six source extents in the eight-by-eight viewport keep every
    # POINT sample away from the equidistant u/v=.5 texel decision boundary.
    sampled_a=copy.deepcopy(a);sampled_a['rect']=[0.,0.,.75,.75]
    sampled_b=copy.deepcopy(b);sampled_b['rect']=[.25,.25,1.,1.]
    for alpha in range(3):
        for mask in (0,1):
            add('sampled_rgba_vertex_fade_restricted_viewport', [sampled_a,sampled_b,screen],
                flags=1|4|8|16,alpha=alpha,mask=mask)
    return cases


def binary_cases(cases):
    data = bytearray(struct.pack('<I', len(cases)))
    for c in cases:
        data += struct.pack('<9I', *(c[k] for k in ('id','mode','mask','alpha','write','fault','pattern','flags')),len(c['ops']))
        for o in c['ops']:
            data += struct.pack('<I11fI',o['kind'],*o['rect'],o['z'],*o['color'],o['fade'],o['gain'],o['affine'])
    return bytes(data)


def initial_pixel(x,y,pattern):
    if not pattern:
        return [.125,.25,.375,.25]
    rgb = [(x+16*y+1)/256, (17*x+5*y+1)/64, (7*x+11*y+1)/128]
    if (x,y)==(0,0): rgb = [0.,154.625,200.]
    if (x,y)==(1,0): rgb = [2**-24,1/1024,1/16]
    if (x,y)==(2,0): rgb[0]=-0.
    return [half(v) for v in rgb]+[.25]


def pixels_in(o,c):
    l,t,r,b = o['rect']
    ox=oy=4 if c['flags']&16 else 0
    vw=vh=8 if c['flags']&16 else 16
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if l <= (x-ox)/vw < r and t <= (y-oy)/vh < b:
                if not c['flags']&1 or (4 <= x < 12 and 4 <= y < 12):
                    yield y*WIDTH+x


def source(o,linear,uv=(0.,0.),flags=0):
    sample=(1.,1.,1.,.125)
    if flags&4:
        texels=((1.,1.,1.,.125),(.5,1.,.75,.5),(1.,.5,.5,0.),(.75,.25,1.,1.))
        sample=texels[min(1,int(uv[1]*2))*2+min(1,int(uv[0]*2))]
    # The source contract preserves raw sampled alpha independently of every
    # authored color/affine/fade parameter, including c0.a.
    rgba=[x*t for x,t in zip(o['color'][:3],sample[:3])]+[sample[3]]
    rgb=rgba[:3]
    if o['affine']:
        rgb = [x*m+a for x,m,a in zip(rgb,(.75,.5,1.25),(.03125,.0625,-.03125))]
    if linear:
        rgb = [decode(x)*o['gain'] for x in rgb]
    fade=o['fade']*(1-.5*uv[0] if flags&8 else 1.)
    return [x*fade for x in rgb]+rgba[3:]


def blend(dst,src,c,kind,write=None):
    out = dst[:]
    write = c['write'] if write is None else write
    for k in range(4):
        if not write & (1<<k): continue
        if kind==0: value=src[k]
        elif kind==2: value=src[k]+dst[k]*(1-src[k])
        elif k==3 and c['alpha']==1: value=dst[k]
        elif k==3 and c['alpha']==2: value=src[k]*src[k]+dst[k]*(1-src[k])
        else: value=src[k]+dst[k]
        out[k]=half(value)
    return out


def expected(c):
    scene=[initial_pixel(x,y,c['pattern']) for y in range(HEIGHT) for x in range(WIDTH)]
    depth=[.75]*256;mask=[0.]*256;layer=[[0.]*4 for _ in range(256)]
    scratch=None
    fault=c['fault'];mode=0 if fault in (1,2,6,7) else c['mode']
    out=dict(accepted=0,fallback=int(fault in (1,2,6,7)),incomplete=0,restored=1,brackets=0,replays=0)

    def close():
        nonlocal scene,scratch
        if scratch is None: return True
        if fault==3:
            out['incomplete']=1;scratch=None;return False
        scene=[[half(encode(v)) for v in p[:3]]+p[3:] for p in scratch]
        scratch=None
        if fault==4:
            out.update(incomplete=1,restored=0);return False
        return True

    for o in c['ops']:
        if o['kind']!=1 and scratch is not None and not close(): break
        if o['kind']==4: continue
        if o['kind']==1 and mode==1:
            if fault==8 and out['accepted']:
                close();out['incomplete']=1;break
            if scratch is None:
                scratch=[[half(decode(v)) for v in p[:3]]+p[3:] for p in scene]
                out['brackets']+=1
            target=scratch;out['accepted']+=1
        elif o['kind']==1 and mode==2:
            target=layer;out['accepted']+=1
        else: target=scene
        for i in pixels_in(o,c):
            if o['z'] < depth[i]:
                ox=oy=4 if c['flags']&16 else 0
                vw=vh=8 if c['flags']&16 else 16
                l,t,rr,b=o['rect'];x,y=i%16,i//16
                uv=((x+.5-ox-l*vw)/((rr-l)*vw),(y+.5-oy-t*vh)/((b-t)*vh))
                values=source(o,o['kind']==1 and mode!=0,uv,c['flags']) if o['kind'] in (1,3) else o['color']
                target[i]=blend(target[i],values,c,o['kind'],7 if mode==2 and o['kind']==1 else 15 if o['kind']==0 else None)
                if mode==2 and o['kind']==1:
                    scene[i]=blend(scene[i],source(o,False,uv,c['flags']),c,1,c['write']&8)
                if o['kind']==0: depth[i]=o['z']
        if fault==5 and out['accepted']:
            close();out['incomplete']=1;break
        if c['mask'] and o['kind']!=0:
            out['replays']+=1
            for i in pixels_in(o,c):
                if o['kind']!=1 or o['z'] < depth[i]:mask[i]=1.
        if scratch is not None and c['flags']&2 and not close():break
    if scratch is not None:close()
    if mode==2:
        scene=[[half(encode(half(decode(p[k]))+layer[i][k])) for k in range(3)]+p[3:] for i,p in enumerate(scene)]
    return scene,mask,[1. if d>.5 else 2. for d in depth],out


def parse_pixels(data,cases):
    size=4+256*6*4
    assert len(data)==size*len(cases),'readback byte count'
    result=[]
    for c,offset in zip(cases,range(0,len(data),size)):
        assert struct.unpack_from('<I',data,offset)[0]==c['id'],'readback ID'
        values=struct.unpack_from('<1536f',data,offset+4)
        result.append(([list(values[i:i+4]) for i in range(0,1024,4)],list(values[1024:1280]),list(values[1280:])))
    return result


def drift_metrics(original,actual):
    # Membership in the initial cap domain does not attribute all subsequent
    # error to capping. Compare separately with the ideal cap-only encoded value
    # to expose residual shader-arithmetic/FP16-storage drift.
    ordinary=[];cap_domain=[];cap_only=[];residual=[]
    changed_pixels=zero_to_nonzero=negative_zero=0
    for a,b in zip(original,actual):
        changed=False
        for old,new in zip(a[:3],b[:3]):
            assert math.isfinite(new) and 0<=new<=half(encode(CAP)), 'drift finite cap'
            assert new!=0 or math.copysign(1.,new)>0, 'drift output must be positive zero'
            error=abs(new-old)
            capped=old**2.2>CAP
            cap_reference=half(encode(CAP)) if capped else old
            cap_error=abs(cap_reference-old)
            if cap_error:cap_only.append((cap_error,cap_error/abs(old) if old else 0.))
            residual_error=abs(new-cap_reference)
            if residual_error:residual.append((residual_error,residual_error/abs(cap_reference) if cap_reference else 0.))
            if error:
                changed=True
                target=cap_domain if capped else ordinary
                target.append((error,error/abs(old) if old else 0.))
                if not old and new:zero_to_nonzero+=1
            if old==0 and math.copysign(1.,old)<0:negative_zero+=1
        changed_pixels+=changed
    assert zero_to_nonzero==0, 'zero RGB gained energy during round trip'
    def summarize(rows):
        return dict(channels=len(rows),max_absolute=max((x[0] for x in rows),default=0),
                    max_relative_nonzero_base=max((x[1] for x in rows),default=0))
    return dict(changed_pixels=changed_pixels,non_cap_domain=summarize(ordinary),
                initial_cap_domain=summarize(cap_domain),cap_only_reference=summarize(cap_only),
                residual_against_cap_only_reference=summarize(residual),
                zero_to_nonzero_channels=zero_to_nonzero,negative_zero_input_channels=negative_zero)


def rounding_model_comparison(original,actual,brackets):
    """Diagnostic hypotheses, never a replacement acceptance oracle.

    Keeping POW at float64 while changing only the two half-store rounding
    rules can show whether quantization alone closely predicts the drift. It
    cannot identify the backend's actual arithmetic or native rounding mode.
    """
    def toward_zero(v):
        h=half(v)
        if h>v:
            bits=struct.unpack('<H',struct.pack('<e',h))[0]
            h=struct.unpack('<e',struct.pack('<H',bits-1))[0]
        return h
    result={}
    for name,quantize in (('nearest_even',half),('toward_zero',toward_zero)):
        model=[p[:3] for p in original]
        for _ in range(brackets):
            model=[[quantize(encode(quantize(decode(v)))) for v in p] for p in model]
        differences=[abs(a-b) for p,q in zip(actual,model) for a,b in zip(p[:3],q)]
        result[name]=dict(exact_rgb_channels=sum(v==0 for v in differences),
                          max_absolute_error=max(differences))
    return result


def validate_report(text,data,cases):
    lines=text.strip().splitlines()
    assert re.fullmatch(r'CAPS vs=fffe0300 ps=ffff0300 rt=[1-9]',lines[0]),'caps'
    assert lines[-1]==f'RESULT pass cases={len(cases)} shaders=24','clean completion'
    formats=re.findall(r'^FORMAT name=(\w+) hr=00000000$',text,re.M)
    assert formats==['fp16_rt','fp16_blend','r32f_rt','d24s8'],'format support'
    assert re.findall(r'^DEPTH_MATCH format=(\d+) hr=00000000$',text,re.M)==['113','114'],'depth match'
    rows=re.findall(r'^CASE id=(\d+) accepted=(\d+) fallback=(\d+) incomplete=(\d+) restored=(\d+) brackets=(\d+) replays=(\d+)$',text,re.M)
    assert len(rows)==len(cases),'case rows'
    actual=parse_pixels(data,cases)
    maximum=0.;alpha_pixels=mask_pixels=depth_pixels=0;faults=[]
    for c,row,(rgb,mask,depth) in zip(cases,rows,actual):
        assert int(row[0])==c['id'],'case order'
        ideal,imask,idepth,outcome=expected(c)
        assert list(map(int,row[1:]))==list(outcome.values()),(c['label'],'fault/state/counter result',row,outcome)
        if c['fault']:faults.append(dict(label=c['label'],**outcome))
        for i,(p,q) in enumerate(zip(rgb,ideal)):
            assert p[3]==q[3],(c['label'],i,'exact destination alpha',p[3],q[3])
            alpha_pixels+=1
            for k,(v,w) in enumerate(zip(p[:3],q[:3])):
                assert math.isfinite(v),(c['label'],i,'nonfinite RGB')
                if c['label']=='drift_original' and i==2 and k==0:
                    assert v==0 and math.copysign(1.,v)<0,'negative-zero ingress did not survive baseline storage'
                if c['label'].startswith('drift_') and c['label']!='drift_original':
                    # Descriptive experiment, not a cumulative acceptance bound
                    # disguised as comparison to a repeated float64/half model.
                    assert 0<=v<=half(encode(CAP)),(c['label'],i,'finite RGB cap')
                    continue
                scale=abs(v-w)/(RGB_ABS+RGB_REL*abs(w))
                maximum=max(maximum,scale)
                assert scale<=1,(c['label'],i,k,'RGB oracle',v,w,scale)
        assert mask==imask,(c['label'],'mask coverage')
        assert depth==idepth,(c['label'],'original D24 depth/stencil content')
        mask_pixels+=len(mask);depth_pixels+=len(depth)
    witnesses=[]
    for label in ('overlap','later_opaque','intervening_screen','late_boundary'):
        ordered=next(c['id'] for c in cases if c['label']==label and c['alpha']==0 and c['write']==15 and c['mask']==0)
        for suffix in ('_gamma_control','_scene_end_control'):
            # Scene-end has the right result when no later relevant writer.
            if suffix=='_scene_end_control' and label=='overlap':continue
            wrong=next(c['id'] for c in cases if c['label']==label+suffix)
            differences=[abs(a[k]-b[k]) for a,b in zip(actual[ordered][0],actual[wrong][0]) for k in range(3)]
            delta=max(differences)
            assert delta>.025,(label,suffix,'negative control not discriminating')
            witnesses.append(dict(scenario=label,wrong=suffix[1:],max_rgb_difference=delta))
    base=next(c['id'] for c in cases if c['label']=='drift_original')
    drift=[dict(brackets=int(c['label'].split('_')[1]),**drift_metrics(actual[base][0],actual[c['id']][0]))
           for c in cases if c['label'].startswith('drift_') and c['id']!=base]
    assert all(d['negative_zero_input_channels']>=1 for d in drift),'negative-zero transfer witness missing'
    for d in drift:
        cid=next(c['id'] for c in cases if c['label']=='drift_'+str(d['brackets']))
        d['float64_power_with_alternative_store_rounding']=rounding_model_comparison(actual[base][0],actual[cid][0],d['brackets'])
    timings=re.findall(r'^TIMING width=(\d+) height=(\d+) bursts=(\d+) variant=(\d+) sample=(\d+) completed_ms=(\S+)$',text,re.M)
    assert len(timings)==192,'timing rows'
    timing_summary=[]
    for width,height in ((1280,768),(1920,1080)):
        for bursts in (1,16):
            block=[r for r in timings if tuple(map(int,r[:3]))==(width,height,bursts)]
            assert [int(r[4]) for r in block]==list(range(48)),'timing order'
            assert [int(r[3]) for r in block]==[5-i%6 if (i//6)%2 else i%6 for i in range(48)],'variant order'
            for variant in range(6):
                values=[float(r[5]) for r in block if int(r[3])==variant]
                assert len(values)==8 and all(math.isfinite(v) and v>=0 for v in values),'timing values'
                timing_summary.append(dict(width=width,height=height,bursts=bursts,
                    workload='observed burst count; synthetic geometry/tail' if bursts==1 else 'stress only',
                    color=('native encoded','ordered two-source best-case batch','ordered single-source brackets')[variant//2],
                    mask=bool(variant%2),sources=bursts*2,transfers=0 if variant<2 else bursts*(4 if variant>=4 else 2),
                    samples=8,median_ms=statistics.median(values),min_ms=min(values),max_ms=max(values)))
    assert len(lines)==8+len(rows)+len(timings),'unrecognized output'
    return dict(cases=len(cases),analytic_rgb_cases=sum(not (c["label"].startswith("drift_") and c["label"]!="drift_original") for c in cases),descriptive_drift_cases=3,shader_creations=24,exact_alpha_pixels=alpha_pixels,
                exact_mask_pixels=mask_pixels,depth_stencil_probe_pixels=depth_pixels,
                max_rgb_tolerance_fraction=maximum,negative_controls=witnesses,
                fault_results=faults,round_trip_drift=drift,timings=timing_summary)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,default=EXE)
    p.add_argument('--raw-dir',type=Path,default=Path('/tmp/x3-linear-emission-gpu'))
    args=p.parse_args()
    assert bottle.BOTTLE=='X3','set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(),'game running; refused'
    assert args.exe.is_file(),'build the detached EXE explicitly first'
    args.raw_dir.mkdir(parents=True,exist_ok=True)
    cases=fixture_cases();case_path=args.raw_dir/'cases.bin';case_path.write_bytes(binary_cases(cases))
    report=args.raw_dir/'report.txt';pixels=args.raw_dir/'pixels.bin'
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,
                scope='Detached authored D3D9 ordered RGB/actual alpha, closed-world R32F mask producer, drift/cost/fault experiment; no live renderer or native Windows runtime qualification',
                targets=dict(scene='A16B16G16R16F',scratch='A16B16G16R16F',mask='R32F',depth='D24S8',msaa=False,srgb=False),
                tolerance=dict(rgb_relative=RGB_REL,rgb_absolute=RGB_ABS,alpha='exact binary16',drift='measured against original; no acceptance threshold'),
                timing_scope='QPC through EVENT completion, reusable targets/shaders/state blocks; Capture/Apply and actual per-source/per-replay RT/depth COM queries included; construction/setup/readback outside; mask clear inside; not GPU-only or game FPS',
                limitations=['Synthetic closed-world coverage only; live classifier and sentinel/TemporalPass integration unqualified',
                             'Two-source batching is a best-case prototype; application-setter batching unapproved',
                             'One burst is observed per sampled frame; 16 bursts are stress-only; synthetic tail is one screen draw per burst',
                             'Injected failures refuse selected calls; no device-loss/partial-API-execution or successful post-submission native-recovery guarantee',
                             'Fullscene decode/reencode can drift untouched pixels; cap effects and rounding are reported separately'],
                code_sha256={name:sha(ROOT/name) for name in INPUTS},executable_sha256=sha(args.exe),raw_dir=str(args.raw_dir))
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=b',str(args.exe),'Z:'+str(case_path),'Z:'+str(pixels)]
    try:
        with report.open('w') as out,(args.raw_dir/'wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=900)
        result['exit_code']=process.returncode
        assert process.returncode==0,'fixture failed; '+str(report)
        result.update(validate_report(report.read_text(),pixels.read_bytes(),cases))
        assert result['code_sha256']=={name:sha(ROOT/name) for name in INPUTS},'source changed during run'
        assert result['executable_sha256']==sha(args.exe),'EXE changed during run'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        path=bottle.results_dir(ROOT)/'linear-emission-gpu.json' if result['passed'] else args.raw_dir/'failed-result.json'
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ('passed','cases','error','max_rgb_tolerance_fraction') if k in result}))

if __name__=='__main__':main()
