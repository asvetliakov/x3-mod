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
          'verification/analysis/test_linear_emission_report.py',
          'verification/analysis/test_linear_emission_mrt_report.py',
          'verification/analysis/test_linear_emission_original_report.py',
          'verification/analysis/test_linear_emission_coverage_report.py',
          'verification/analysis/test_linear_emission_pass_report.py',
          'verification/analysis/test_linear_emission_fused_report.py',
          'verification/analysis/test_linear_emission_pass_programs.py',
          'verification/analysis/test_linear_emission_pass_host.py',
          'verification/probe/linear_emission_pass_cases_inc.h',
          'verification/probe/linear_emission_pass_host.cpp',
          'verification/probe/linear_emission_pass_stubs/d3d9.h',
          'src/renderer/linear_emission_pass.h','src/renderer/linear_emission_pass.cpp',
          'src/renderer/linear_emission_copy_inc.h','src/renderer/linear_emission_copy_clear_inc.h','src/renderer/linear_emission_composite_inc.h',
          'src/renderer/quad_vertex_program.h','src/renderer/quad_vertex_program_inc.h',
          'src/renderer/linear_emission.h', 'src/renderer/linear_emission.cpp')
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

# Same runner/session infrastructure, separate experiment and accepted record.
MRT_OPS = ('sources','bursts','copy','native','zero','alpha','minuszero','capzero','infinite','fallback','incomplete','refused')


def mrt_cases():
    cases=[]
    def add(label,operations,**kw):
        c=dict(id=len(cases),label=label,mode=1,mask=0,alpha=0,write=15,fault=0,pattern=0,flags=32,ops=copy.deepcopy(operations))
        c.update(kw);cases.append(c)
    a=op(rect=(0,0,.75,.75),color=(.5,.25,.125,.125),fade=.75,affine=1)
    b=op(rect=(.25,.25,1,1),color=(.125,.375,.5,.5),fade=.5,gain=4.)
    for pp in (0,32):
        for affine in (0,1):
            for nofade in (0,64):
                aa,bb=copy.deepcopy(a),copy.deepcopy(b)
                aa['affine']=bb['affine']=affine
                add('source_contract_variants',[aa,bb],flags=pp|nofade)
    for alpha in range(3):
        for alpha_test in range(3):
            add('sampled_alpha_test_raster',[a,b],flags=32|4|8|16|1,alpha=alpha,mask=alpha_test)
    for axis in range(3):
        color=[0.,0.,0.,.125];color[axis]=.5
        add('asymmetric_channel_identity', [op(rect=(0,0,1,1),color=color,fade=.75,gain=4.)],pattern=1)
    zero=op(rect=(0,0,1,1),gain=0.,color=(.5,.25,.125,.125),fade=1.)
    for rotations in (1,16,64):
        add('zero_energy_rotations_'+str(rotations),[x for _ in range(rotations) for x in (zero,zero,op(4))],pattern=1)
    add('zero_fade',[op(rect=(0,0,1,1),fade=0.)],pattern=1)
    add('fade_absent_zero_varying',[op(fade=0.)],flags=32|64)
    add('negative_affine_component',[op(rect=(0,0,1,1),color=(0,0,0,.125),fade=1.,affine=1)],pattern=1)
    add('finite_source_storage_cap',[op(rect=(0,0,1,1),color=(.5,256.,0.,.125),fade=1.,gain=16.)],pattern=1)
    capped=op(rect=(0,0,1,1),color=(0.,256.,0.,.125),fade=1.,gain=16.)
    add('positive_accumulation_overflow',[capped,capped],flags=32|128,pattern=1)
    add('positive_infinite_E_seed',[zero],flags=32|256,pattern=1)
    add('single_source_brackets',[a,b],flags=32|2)
    screen=op(2,rect=(.25,.125,.875,.875),color=(.25,.125,.5,.25),fade=1.)
    opaque=op(0,rect=(.25,.25,.75,.75),z=.2,color=(.0625,.5,.25,.5),fade=1.)
    add('intervening_screen',[a,screen,b])
    add('later_opaque',[a,b,opaque])
    add('depth_occluded',[op(z=.9),a,b])
    add('composition_refusal_native_adoption',[a,b],fault=3)
    add('repeated_native_adoption',[a,b,op(4),a,b],fault=3)
    add('rgb_write_mask_refusal',[a,b],write=7)
    add('preparation_refusal',[a,b],fault=1)
    add('native_encoded_control',[a,b],mode=0)
    return cases


# Derived identities/contracts only; originals stay in the local corpus.
ORIGINAL_PS = ('8360f422de08b5bd','9975b706e5a1c999','ff2473e73a6bdfa1','8559522220507d5e','875e780adb131b16',
    '39f3b4d5b6a5aaed','47e15e20d63b0e93','846c5c1a549f9491','c6dacb8f74b65c97','f0c91793a75e1203')
ORIGINAL_VS = ('d5e1c75351ed3f04','32e75459998d0388','089091aab2d5eb13',
    '5b7a3ccd9e7df00a','6435a84d8ac5908e','89193868c61c3846','a520be365951c9dc','cfb2c31707d545bc')
ORIGINAL_GAINS = (0.,.25,1.,4.,16.)
SOURCE_GAINS = (1.,2.,3.5,8.)  # source-only encoded gain variants (gain 1 = original bytes)


# Pair index is carried in the existing high flag bits. The first five values
# retain their original meaning and binary corpus exactly.
ORIGINAL_PAIRS = ((0,0),(1,1),(1,2),(2,3),(2,4),
    (3,1),(3,2),(4,5),(4,6),(4,7),(4,8),(5,0),(5,9),(6,3),(6,4),
    (7,5),(7,6),(7,7),(7,8),(0,9))
AFFINE_PS = frozenset((0,1,3,7,8,9))
PS21 = frozenset((6,8,9))
INSTANCE_VS = frozenset((3,4,5,6))
NO_FADE_VS = frozenset((2,6))


def original_cases(first=0,last=20):
    cases=[]
    def add(profile,label,operations,**kw):
        c=dict(id=len(cases),label=label,mode=1,mask=0,alpha=0,write=15,fault=0,pattern=1,
               flags=32|(profile<<16)|(64 if ORIGINAL_PAIRS[profile][0] in NO_FADE_VS else 0),actual_profile=profile,ops=copy.deepcopy(operations))
        extra=kw.pop('flags',0);c['flags']|=extra;c.update(kw)
        for o in c['ops']:o['affine']=int(ORIGINAL_PAIRS[profile][1] in AFFINE_PS)
        cases.append(c)
    for profile in range(first,last):
        for gain in ORIGINAL_GAINS:
            add(profile,'original_pair_gain',[
                op(rect=(0,0,.75,.75),color=(.5,.25,.125,.125),fade=.75,gain=gain),
                op(rect=(.25,.25,1,1),color=(.125,.375,.5,.5),fade=.5,gain=gain)])
        # Texture transform is independently witnessed along both axes. Full
        # rectangle/even viewport keeps nearest sampling away from texel ties.
        for alpha in range(3):
            add(profile,'original_sampled_alpha', [op(rect=(0,0,1,1),gain=4)],
                flags=4|16|1|1024,alpha=alpha)
        for alpha_test in (1,2):
            add(profile,'original_alpha_test', [op(rect=(0,0,1,1),gain=4),op(z=.9,gain=4)],
                flags=4|1024,mask=alpha_test)
        if ORIGINAL_PAIRS[profile][0] not in NO_FADE_VS:
            for fog_flags in (512,512|2048,512|4096):
                add(profile,'original_vertex_fog', [op(rect=(0,0,1,1),gain=4)],flags=fog_flags)
            add(profile,'original_zero_fade',[op(rect=(0,0,1,1),fade=0,gain=16)])
        if ORIGINAL_PAIRS[profile][1] in AFFINE_PS:
            add(profile,'original_negative_affine',[op(rect=(0,0,1,1),color=(0,0,0,.125),fade=.5,gain=4)])
        add(profile,'original_finite_source_cap',[op(rect=(0,0,1,1),color=(0,256,0,.125),fade=.5,gain=16)])
        if profile >= 5:
            add(profile,'original_decoded_cap_before_scale',[op(rect=(0,0,1,1),color=(256,256,256,.125),fade=.125,gain=.25)])
    return cases


def coverage_cases():
    cases=original_cases(0,5)
    def add(profile,label,operations,alpha=0,flags=0):
        cases.append(dict(id=len(cases),label=label,mode=1,mask=0,alpha=alpha,write=15,fault=0,pattern=1,
            flags=32|(profile<<16)|(64 if ORIGINAL_PAIRS[profile][0] in NO_FADE_VS else 0)|flags,actual_profile=profile,ops=copy.deepcopy(operations)))
        for o in cases[-1]['ops']:o['affine']=int(ORIGINAL_PAIRS[profile][1] in AFFINE_PS)
    for profile in range(5):
        add(profile,'coverage_zero_rgb_alpha',[op(rect=(0,0,1,1),color=(0,0,0,0),gain=1)])
    for alpha in range(3):
        add(2,'coverage_zero_fade_alpha',[op(rect=(0,0,1,1),color=(.5,.25,.125,0),fade=0,gain=16)],alpha=alpha)
    # M must survive every E clear/composition and must accumulate overlapping
    # submissions while uncovered pixels remain zero. Gain0 avoids color drift.
    a=op(rect=(0,0,.75,.75),gain=0);b=op(rect=(.25,.25,1,1),gain=0)
    for brackets in (2,16):
        add(2,'coverage_persistent_'+str(brackets),[copy.deepcopy((a,b)[i%2]) for i in range(brackets)],flags=2)
    add(2,'coverage_next_frame_clear',[op(rect=(0,0,1,1),z=.9,gain=1)])
    # Keep all81 accepted coverage payloads first, including their IDs.
    for c in original_cases(5,20):
        c['id']=len(cases);cases.append(c)
    for profile in range(5,20):
        add(profile,'coverage_zero_rgb_alpha',[op(rect=(0,0,1,1),color=(0,0,0,0),gain=1)])
        if ORIGINAL_PAIRS[profile][0] not in NO_FADE_VS:
            for alpha in range(3):
                add(profile,'coverage_zero_fade_alpha',[op(rect=(0,0,1,1),color=(.5,.25,.125,0),fade=0,gain=16)],alpha=alpha)
    return cases


def coverage_expected(c):
    result=[0.]*256
    for o in c['ops']:
        assert o['kind']==1,'coverage fixture source-only frame'
        if o['z']>=.75:continue
        offset=4 if c['flags']&16 else 0;size=8 if c['flags']&16 else 16
        l,t,rr,b=o['rect']
        for i in pixels_in(o,c):
            x,y=i%16,i//16
            uv=((x+.5-offset-l*size)/((rr-l)*size),(y+.5-offset-t*size)/((b-t)*size))
            alpha=mrt_sample(o,c,uv)[0][3]
            if c['mask']==1 and alpha<=64/255:continue
            if c['mask']==2 and alpha>=128/255:continue
            # Deliberately independent of emission RGB, fade, gain or source
            # alpha blend factors: source coverage is a separate RGB payload.
            result[i]=half(result[i]+1.)
    return result


def pass_cases():
    # Initial component admission excludes alpha test. Every source has its own
    # bracket; original70 alpha-test witnesses remain in their accepted mode.
    cases=[c for c in original_cases() if not c['mask']]
    for i,c in enumerate(cases):c['id']=i;c['flags']|=2
    return cases


def original_fade(o,c,uv):
    if ORIGINAL_PAIRS[c['actual_profile']][0] in NO_FADE_VS:return 1.
    if not c['flags']&512:return o['fade']
    vw=8 if c['flags']&16 else 16
    l,t,rr,b=o['rect']
    positions=((2*l-1-1/vw,1-2*t+1/vw),(2*rr-1-1/vw,1-2*t+1/vw),
               (2*l-1-1/vw,1-2*b+1/vw),(2*rr-1-1/vw,1-2*b+1/vw))
    clip=-.5 if c['flags']&2048 else 3. if c['flags']&4096 else 1.5
    fades=[]
    for x,y in positions:
        # Original VS computes distance at each vertex, clamps fog, then the
        # rasterizer interpolates COLOR0.x. This is not per-pixel distance.
        distance=math.sqrt((.125-(.5*x+.25))**2+(.25-(.5*y-.125))**2+4)
        fades.append(o['fade']*max(0.,min(1.,clip-.5*distance)))
    u,v=uv
    if u+v<=1:return fades[0]*(1-u-v)+fades[1]*u+fades[2]*v
    return fades[1]*(1-v)+fades[2]*(1-u)+fades[3]*(u+v-1)


def original_uv(c,uv):
    return (.5*uv[0]+.125,-.5*uv[1]+.875) if c['flags']&1024 and ORIGINAL_PAIRS[c['actual_profile']][0] not in INSTANCE_VS else uv


def mrt_sample(o,c,uv):
    geometry_uv=uv
    if 'actual_profile' in c:uv=original_uv(c,uv)
    rgba=[half(x) for x in o['color']]
    if c['flags']&4:
        sample=((1.,1.,1.,.125),(.5,1.,.75,.5),(1.,.5,.5,0.),(.75,.25,1.,1.))[min(1,int(uv[1]*2))*2+min(1,int(uv[0]*2))]
        rgba=[half(x*t) for x,t in zip(o['color'][:3],sample[:3])]+[sample[3]]
    rgb=rgba[:3]
    if o['affine']:
        rgb=[.75*rgb[0]+.125*rgb[1]+.03125,
             .5*rgb[1]+.25*rgb[2]+.0625,
             .125*rgb[0]+.875*rgb[2]-.03125]
    fade=1. if c['flags']&64 else o['fade']*(1-.5*uv[0] if c['flags']&8 else 1.)
    if 'actual_profile' in c:fade=original_fade(o,c,geometry_uv)
    native=[x*fade for x in rgb]+rgba[3:]
    energy=[sanitize(decode(x)*fade*o['gain']) for x in rgb]+[0.]
    return native,energy


def mrt_expected(c,include_sources=False):
    a=[initial_pixel(x,y,c['pattern']) for y in range(16) for x in range(16)]
    depth=[.75]*256;count={key:0 for key in MRT_OPS};start=0
    eligible=c['write']==15 and c['fault']!=1
    def draw(target,index,energy=None):
        o=c['ops'][index]
        for i in pixels_in(o,c):
            if o['z']>=depth[i]:continue
            if o['kind']!=1:
                target[i]=blend(target[i],o['color'],c,o['kind'],15 if o['kind']==0 else None)
                if o['kind']==0:depth[i]=o['z']
                continue
            ox=oy=4 if c['flags']&16 else 0;vw=vh=8 if c['flags']&16 else 16
            l,t,rr,bb=o['rect'];x,y=i%16,i//16
            uv=((x+.5-ox-l*vw)/((rr-l)*vw),(y+.5-oy-t*vh)/((bb-t)*vh))
            native,e=mrt_sample(o,c,uv)
            if c['mask']==1 and native[3]<=64/255:continue
            if c['mask']==2 and native[3]>=128/255:continue
            target[i]=blend(target[i],native,c,1)
            if energy is not None:
                energy[i]=[half(x+y) for x,y in zip(energy[i][:3],e[:3])]+[0.]
    while start<len(c['ops']):
        if c['ops'][start]['kind']!=1:
            if c['ops'][start]['kind']!=4:draw(a,start)
            start+=1;continue
        end=start+1
        if not c['flags']&2:
            while end<len(c['ops']) and c['ops'][end]['kind']==1:end+=1
        count['sources']+=end-start
        if not eligible or c['mode']==0:
            for index in range(start,end):draw(a,index)
            count['refused']+=not eligible;start=end;continue
        b=copy.deepcopy(a);e=[[0.]*4 for _ in range(256)]
        count['bursts']+=1;count['copy']+=1024;count['native']+=1024
        for index in range(start,end):draw(b,index,e)
        if c['flags']&256:e=[[0.,math.inf,0.,0.] for _ in range(256)]
        if c['fault']==3:
            a=b;count['fallback']+=1;start=end;continue
        result=[]
        for old,energy,native in zip(a,e,b):
            rgb=[]
            for x,y in zip(old[:3],energy[:3]):
                count['infinite']+=math.isinf(y) and y>0
                y=sanitize(y)
                if y==0:
                    rgb.append(x);count['zero']+=1
                    count['minuszero']+=x==0 and math.copysign(1.,x)<0
                    count['capzero']+=x>154.6012
                else:rgb.append(half(encode(sanitize(decode(x)+y))))
            result.append(rgb+native[3:]);count['alpha']+=1
        a=result;start=end
    result=(a,[1. if z>.5 else 2. for z in depth],count)
    return result+(b,e) if include_sources else result


def parse_mrt_pixels(data,cases,actual_original=False,coverage=False):
    stride=4+256*(17 if coverage else 13 if actual_original else 5)*4
    assert len(data)==stride*len(cases),'MRT readback bytes'
    result=[]
    for c,offset in zip(cases,range(0,len(data),stride)):
        assert struct.unpack_from('<I',data,offset)[0]==c['id'],'MRT readback order'
        values=struct.unpack_from('<1280f',data,offset+4)
        result.append(([list(values[i:i+4]) for i in range(0,1024,4)],list(values[1024:])))
    return result


def validate_branch_timings(text):
    rows=re.findall(r'^BRANCH_TIMING width=(\d+) height=(\d+) sparse=([01]) workload=([012]) branch=([01]) pair=(\d+) order=([01]) completed_ms=(\S+)$',text,re.M)
    assert len(rows)==192,'paired branch timing rows'
    result=[]
    for width,height in ((1280,768),(1920,1080)):
        for sparse in (0,1):
            for workload in range(3):
                block=[r for r in rows if tuple(map(int,r[:4]))==(width,height,sparse,workload)]
                assert [(int(r[5]),int(r[6])) for r in block]==[(pair,order) for pair in range(8) for order in range(2)],'paired timing order'
                assert [int(r[4]) for r in block]==[1-order if pair%2 else order for pair in range(8) for order in range(2)],'baseline/branch alternation'
                values={branch:[float(r[7]) for r in block if int(r[4])==branch] for branch in (0,1)}
                assert all(math.isfinite(v) and v>=0 for group in values.values() for v in group),'branch timing values'
                differences=[a-b for a,b in zip(values[0],values[1])]
                result.append(dict(width=width,height=height,coverage='sparse' if sparse else 'dense',
                    authored_union_fraction=1/32 if sparse else .5,
                    authored_per_source_fraction=1/64 if sparse else .3125,
                    composed_coverage_fractions=([1/64 if sparse else .3125]*2 if workload==2 else [1/32 if sparse else .5]),
                    workload=('composite only','best-case two-source burst','two single-source brackets')[workload],
                    samples_per_variant=8,warmups_per_variant=2,
                    baseline=dict(median_ms=statistics.median(values[0]),min_ms=min(values[0]),max_ms=max(values[0])),
                    branch=dict(median_ms=statistics.median(values[1]),min_ms=min(values[1]),max_ms=max(values[1])),
                    branch_wins=sum(d>0 for d in differences),ties=sum(d==0 for d in differences),
                    pairs=[dict(pair=i,baseline_first=i%2==0,baseline_ms=a,branch_ms=b,baseline_minus_branch_ms=a-b)
                           for i,(a,b) in enumerate(zip(values[0],values[1]))],
                    paired_baseline_minus_branch_ms=dict(median=statistics.median(differences),minimum=min(differences),maximum=max(differences))))
    return result


def emitting_feedback(c):
    """Split brackets can feed GPU-rounded composition back into the next A.

    A single step, or only zero-energy preceding steps, retains the known seed.
    The source oracle identifies whether feedback can change; it does not predict
    which side of an encoded-value category threshold the GPU result occupies.
    """
    if not c['flags']&2:return False
    for i in range(1,len(c['ops'])):
        prefix=copy.deepcopy(c);prefix['ops']=prefix['ops'][:i]
        if any(v>0 for pixel in mrt_expected(prefix,True)[4] for v in pixel[:3]):return True
    return False


def validate_mrt_report(text,data,cases,branch_experiment=False,actual_original=False,coverage=False,component=False,fused_comparison=False):
    lines=text.strip().splitlines()
    assert re.fullmatch(r'CAPS vs=fffe0300 ps=ffff0300 rt=[2-9]',lines[0]),'MRT shader caps'
    assert re.findall(r'^FORMAT name=(\w+) hr=00000000$',text,re.M)==['fp16_rt','fp16_blend','d24s8'],'MRT format caps'
    assert re.findall(r'^DEPTH_MATCH format=(\d+) hr=00000000$',text,re.M)==['113'],'MRT depth matches'
    caps=re.search(r'^MRT_CAPS slots=([2-9]) postblend=1 independent_masks=([01])$',text,re.M)
    assert caps,'MRT blend/mask caps'
    if coverage:assert int(caps[1])>=3,'three-output MRT slots'
    shaders=None if fused_comparison else 528 if component else 381 if coverage else 77 if actual_original else 81 if branch_experiment else 78
    end='PASS_RESULT' if component else 'COVERAGE_RESULT' if coverage else 'ORIGINAL_RESULT' if actual_original else 'BRANCH_RESULT' if branch_experiment else 'MRT_RESULT'
    completion=f'FUSED_RESULT pass cases={len(cases)}' if fused_comparison else f'{end} pass cases={len(cases)} shaders={shaders}'
    assert lines[-1]==completion,'MRT clean completion'
    pattern=r'^MRT_CASE id=(\d+) sources=(\d+) bursts=(\d+) copy=(\d+) native=(\d+) zero=(\d+) alpha=(\d+) minuszero=(\d+) capzero=(\d+) infinite=(\d+) fallback=(\d+) incomplete=(\d+) refused=(\d+)$'
    rows=re.findall(pattern,text,re.M);assert len(rows)==len(cases),'MRT case rows'
    actual=parse_mrt_pixels(data,cases,actual_original,coverage);maximum=0.;totals={k:0 for k in MRT_OPS};faults=[]
    feedback_categories=[];stable_capzero=0
    for c,row,(color,depth) in zip(cases,rows,actual):
        assert int(row[0])==c['id'],'MRT case order'
        ideal,idepth,count=mrt_expected(c)
        if c['label']=='positive_accumulation_overflow':
            observed=int(row[1+MRT_OPS.index('infinite')])
            assert observed in (0,256),'uniform overflow may store finite maximum or positive infinity'
            count['infinite']=observed
        if component and emitting_feedback(c):
            observed=int(row[1+MRT_OPS.index('capzero')])
            assert 0<=observed<=count['zero'],'feedback category exceeds exact zero-E retention count'
            # Exact A/C bit preservation was checked inside the EXE for every
            # zero-E lane. This category measures actual A, not ideal CPU A.
            count['capzero']=observed
            feedback_categories.append(dict(id=c['id'],capzero=observed))
        else:stable_capzero+=count['capzero']
        assert list(map(int,row[1:]))==list(count.values()),(c['label'],'MRT GPU invariant counts',row,count)
        for k,v in count.items():totals[k]+=v
        if c['fault'] or c['write']!=15:faults.append(dict(label=c['label'],sources=count['sources'],native_adoptions=count['fallback'],refused=count['refused'],incomplete=count['incomplete']))
        assert depth==idepth,(c['label'],'original depth/stencil')
        for i,(a,b) in enumerate(zip(color,ideal)):
            assert a[3]==b[3],(c['label'],i,'raw native alpha',a[3],b[3])
            for k,(v,w) in enumerate(zip(a[:3],b[:3])):
                assert math.isfinite(v),(c['label'],i,'nonfinite C')
                scale=abs(v-w)/(RGB_ABS+RGB_REL*abs(w));maximum=max(maximum,scale)
                assert scale<=1,(c['label'],i,k,'MRT C oracle',v,w,scale)
        if c['label'].startswith('zero_energy_rotations_'):
            for i,p in enumerate(color):
                wanted=initial_pixel(i%16,i//16,True)
                assert struct.pack('<3f',*p[:3])==struct.pack('<3f',*wanted[:3]),'repeated zero-E channel bits'
    assert totals['minuszero'] and totals['capzero'],'signed-zero/high encoded identity witnesses missing'
    if component:assert stable_capzero>0,'stable initial high-code identity witnesses missing'
    if not actual_original:
        control=next(c['id'] for c in cases if c['label']=='native_encoded_control')
        qualified=next(c['id'] for c in cases if c['label']=='composition_refusal_native_adoption')
        assert actual[control]==actual[qualified],'composition refusal did not adopt current native result'
    if branch_experiment:
        comparisons=re.findall(r'^BRANCH_COMPARE id=(\d+) channels=(\d+)$',text,re.M)
        assert [int(row[0]) for row in comparisons]==[c['id'] for c in cases],'branch comparison cases'
        for c,row in zip(cases,comparisons):
            assert int(row[1])==mrt_expected(c)[2]['alpha']*4,(c['label'],'exact compositor equality missing')
        summary=validate_branch_timings(text)
        assert len(lines)==7+len(rows)+len(comparisons)+192,'unexpected branch output'
    elif component:
        rows_pass=re.findall(r'^PASS_CASE id=(\d+) channels=4096 draws=(\d+) allocations=0$',text,re.M)
        assert [(int(i),int(n)) for i,n in rows_pass]==[(c['id'],len(c['ops'])) for c in cases],'component case proof'
        assert f"PASS_CHECKS channels={4096*len(cases)} draws={sum(len(c['ops']) for c in cases)}" in lines,'component DIP count'
        assert 'PASS_FAULTS checks=16 source_replays=0' in lines,'component failure controls'
        assert 'PASS_CAPS twins=2 forbidden_calls=0' in lines,'conditional capability controls'
        assert 'PASS_RESET passed=1 retained_programs=4 recreated_targets=4 frame_clear=1 transaction=1' in lines,'native Reset completion'
        summary=validate_pass_timings(text,fused_comparison)
        if fused_comparison:
            twins=re.findall(r'^FUSED_CASE id=(\d+) rgba_depth_mask_exact=1 sources=(\d+)$',text,re.M)
            assert len(cases)==271 and [(int(i),int(n)) for i,n in twins]==[(c['id'],len(c['ops'])) for c in cases],'fused image twins'
        assert len(lines)==7+(3 if fused_comparison else 2)*len(rows)+36,'unexpected component output'
    elif coverage:
        parity=re.findall(r'^COVERAGE_CASE id=(\d+) parity=(\d+)$',text,re.M)
        assert [int(row[0]) for row in parity]==[c['id'] for c in cases],'coverage parity cases'
        assert [int(row[1]) for row in parity]==[mrt_expected(c)[2]['bursts']*2048 for c in cases],'two/three-output B/E parity'
        summary=validate_coverage_timings(text)
        assert len(lines)==7+len(rows)+len(parity)+48,'unexpected coverage output'
    elif actual_original:
        summary=[]
        assert len(lines)==7+len(rows),'unexpected actual-original output'
    else:
        timings=re.findall(r'^MRT_TIMING width=(\d+) height=(\d+) variant=(\d+) sample=(\d+) completed_ms=(\S+)$',text,re.M)
        assert len(timings)==96,'MRT timing rows';summary=[]
        for width,height in ((1280,768),(1920,1080)):
            block=[x for x in timings if tuple(map(int,x[:2]))==(width,height)]
            assert [int(x[3]) for x in block]==list(range(48)),'MRT timing order'
            assert [int(x[2]) for x in block]==[5-i%6 if (i//6)%2 else i%6 for i in range(48)],'MRT variant order'
            for variant in range(6):
                values=[float(x[4]) for x in block if int(x[2])==variant]
                assert len(values)==8 and all(math.isfinite(v) and v>=0 for v in values)
                summary.append(dict(width=width,height=height,mode=('native','best-case two-source MRT burst','two single-source MRT brackets','copy only','clear only','composite only')[variant],samples=8,median_ms=statistics.median(values),min_ms=min(values),max_ms=max(values)))
        assert len(lines)==7+len(rows)+len(timings),'unexpected MRT output'
    result=dict(cases=len(cases),shader_creations=shaders,source_variants=50 if actual_original else 8,mrt_caps=dict(slots=int(caps[1]),postpixel_blending=True,independent_write_masks=bool(int(caps[2]))),source_shader_model='vs_2_0/ps_2_0; native full/partial precision variants',invariants=totals,
                branch_experiment=branch_experiment,exact_compositor_channels=totals['alpha']*4 if branch_experiment else None,max_rgb_tolerance_fraction=maximum,exact_alpha_pixels=256*len(cases),depth_stencil_pixels=256*len(cases),faults=faults,timings=summary)
    if component:result['high_code_identity']=dict(stable_seed_channels=stable_capzero,feedback_categories=feedback_categories,
        policy='GPU-composed feedback category is descriptive and bounded by exact zero-E retention count; static seed counts remain exact; every actual-A zero-E lane is compared bitwise inside the EXE')
    return result



def source_gain_cases():
    """Every reviewed pair, a flat and a 2x2-textured source, over a zero and a
    non-zero background: 80 cases, one native draw plus four variant draws each."""
    cases=[]
    sources=(op(rect=(0,0,.75,.75),color=(.5,.25,.125,.125),fade=.75,gain=1.),
             op(rect=(0,0,1,1),color=(.125,.375,.5,.5),fade=.5,gain=1.))
    for profile in range(20):
        for textured,source in enumerate(sources):
            for background in (0,1):
                c=dict(id=len(cases),label='source_gain',mode=1,mask=0,alpha=0,write=15,fault=0,pattern=0,
                       flags=32|(profile<<16)|(64 if ORIGINAL_PAIRS[profile][0] in NO_FADE_VS else 0)|(4 if textured else 0)|(32768 if background else 0),
                       actual_profile=profile,background=background,ops=[copy.deepcopy(source)])
                for o in c['ops']:o['affine']=int(ORIGINAL_PAIRS[profile][1] in AFFINE_PS)
                cases.append(c)
    return cases


def half_bits(value):
    return struct.unpack('<H',struct.pack('<e',value))[0]


def half_interval(value):
    """The real interval an FP16 readback value stands for (half an ULP each way)."""
    if value==0:return (0.,2.**-25)
    exponent=max(math.frexp(value)[1]-1,-14)
    spacing=2.**(exponent-10)
    return (value-spacing/2,value+spacing/2)


def validate_source_gain_report(text,data,cases):
    """Law: gained = bg + G (native - bg) per colour channel, alpha bit-exact,
    gain 1 bit-exact. Over the zero background the native readback is the
    source itself, so the gained channel must be G x native within one FP16
    code (unclipped); over a non-zero background the tolerance is the FP16
    half-ULP interval of the native readback propagated through the law,
    widened by one code."""
    lines=text.splitlines()
    rows=[dict(item.split('=',1) for item in line.split()[1:]) for line in lines if line.startswith('SOURCE_GAIN_CASE ')]
    assert [int(r['id']) for r in rows]==[c['id'] for c in cases],'complete case list'
    assert all(int(r['pair'])==c['actual_profile'] and int(r['background'])==c['background'] and r['draws']=='5' for r,c in zip(rows,cases))
    assert any(line.startswith('SOURCE_GAIN_RESULT pass') for line in lines),'fixture result line'
    n=WIDTH*HEIGHT*4;record=4+4*n*6
    assert len(data)==record*len(cases),'raw record size'
    per_gain={g:dict(channels=0,exact=0,within_one=0,max_codes=0,brighter=0,max_value=0.) for g in SOURCE_GAINS[1:]}
    identity_channels=alpha_channels=covered_total=0
    for i,c in enumerate(cases):
        base=i*record;(cid,)=struct.unpack_from('<I',data,base);assert cid==c['id']
        images=[struct.unpack_from(f'<{n}f',data,base+4+4*n*k) for k in range(6)]
        cleared,native,identity,*gained=images
        assert all(math.isfinite(v) for v in native),(c['id'],'finite native')
        assert struct.pack(f'<{n}f',*identity)==struct.pack(f'<{n}f',*native),(c['id'],'gain 1 variant is not the native image')
        identity_channels+=n
        covered=[p for p in range(WIDTH*HEIGHT) if any(native[4*p+k]!=cleared[4*p+k] for k in range(3))]
        assert covered,(c['id'],'native draw covered no pixel')
        covered_total+=len(covered)
        for g,image in zip(SOURCE_GAINS[1:],gained):
            stat=per_gain[g]
            for p in range(WIDTH*HEIGHT):
                assert image[4*p+3]==native[4*p+3],(c['id'],g,p,'alpha changed')
                alpha_channels+=1
                for k in range(3):
                    bg,s,v=cleared[4*p+k],native[4*p+k],image[4*p+k]
                    assert math.isfinite(v) and v<CAP,(c['id'],g,p,k,'clipped or nonfinite')
                    if c['background']==0:
                        expected=half(g*s);codes=abs(half_bits(v)-half_bits(expected))
                        assert codes<=1,(c['id'],g,p,k,v,expected,codes)
                    else:
                        lo,hi=half_interval(s)
                        low,high=half(bg+g*(lo-bg)),half(bg+g*(hi-bg))
                        codes=0 if low<=v<=high else min(abs(half_bits(v)-half_bits(low)),abs(half_bits(v)-half_bits(high)))
                        assert codes<=1,(c['id'],g,p,k,v,(low,high),codes)
                    stat['channels']+=1;stat['exact']+=codes==0;stat['within_one']+=codes<=1;stat['max_codes']=max(stat['max_codes'],codes)
                    stat['max_value']=max(stat['max_value'],v)
                    if s>bg:
                        assert v>s,(c['id'],g,p,k,'not brighter');stat['brighter']+=1
                    elif s==bg:
                        assert v==s,(c['id'],g,p,k,'uncovered channel changed')
    return dict(cases=len(cases),reviewed_pairs=20,source_gains=list(SOURCE_GAINS),identity_channels=identity_channels,alpha_channels_exact=alpha_channels,
                covered_pixels=covered_total,per_gain={str(g):v for g,v in per_gain.items()},
                law='gained = bg + G (native - bg) per colour channel; alpha and gain-1 bit-exact; blend state, draw order and alpha native')


def validate_original_report(text,data,cases,coverage=False,component=False,fused_comparison=False):
    result=validate_mrt_report(text,data,cases,actual_original=True,coverage=coverage,component=component,fused_comparison=fused_comparison)
    energy_maximum=native_maximum=0.;alpha_count=0
    stride=4+256*(17 if coverage else 13)*4
    for c,offset in zip(cases,range(0,len(data),stride)):
        _,_,_,wanted_native,wanted_energy=mrt_expected(c,True)
        values=struct.unpack_from('<2048f',data,offset+4+1280*4)
        for label,wanted,actual in (('native B',wanted_native,values[:1024]),('linear E',wanted_energy,values[1024:])):
            for i,p in enumerate(wanted):
                rgba=actual[i*4:i*4+4]
                assert struct.pack('<f',rgba[3])==struct.pack('<f',p[3]),(c['label'],label,i,'raw alpha bits')
                alpha_count+=1
                for k,(v,w) in enumerate(zip(rgba[:3],p[:3])):
                    assert math.isfinite(v),(c['label'],label,i,'nonfinite RGB')
                    if label=='linear E':assert 0<=v<=CAP,(c['label'],'finite source cap')
                    fraction=abs(v-w)/(RGB_ABS+RGB_REL*abs(w))
                    assert fraction<=1,(c['label'],label,i,k,v,w,fraction)
                    if label=='linear E':energy_maximum=max(energy_maximum,fraction)
                    else:native_maximum=max(native_maximum,fraction)
    result.update(original_vertex_programs=8,original_pixel_programs=10,reviewed_pairs=20,
        source_shader_model='Eight unchanged original VS2; seven exact PS2.0 and three exact PS2.1 native _pp paths; 50 full-precision emission tails',
        exact_pairs=[dict(vs=ORIGINAL_VS[v],ps=ORIGINAL_PS[p],pixel_model='2.1' if p in PS21 else '2.0',
                          vertex_layout='instance' if v in INSTANCE_VS else 'default',fade=v not in NO_FADE_VS)
                     for v,p in ORIGINAL_PAIRS],
        gains=list(ORIGINAL_GAINS),max_energy_tolerance_fraction=energy_maximum,
        max_native_oracle_tolerance_fraction=native_maximum,exact_source_alpha_pixels=alpha_count)
    return result


def validate_coverage_timings(text):
    rows=re.findall(r'^COVERAGE_TIMING width=(\d+) height=(\d+) variant=([012]) pair=(\d+) order=([01]) completed_ms=(\S+)$',text,re.M)
    assert len(rows)==48,'coverage timing rows'
    summary=[]
    for w,h in ((1280,768),(1920,1080)):
        block=[x for x in rows if tuple(map(int,x[:2]))==(w,h)]
        wanted=[(1-order if pair%2 else order,pair,order) for pair in range(8) for order in range(2)]
        paired=[x for x in block if x[2]!='2'];clears=[x for x in block if x[2]=='2']
        assert [tuple(map(int,x[2:5])) for x in paired]==wanted,'coverage paired order'
        assert [tuple(map(int,x[3:5])) for x in clears]==[(i,0) for i in range(8)],'frame clear order'
        assert all(math.isfinite(float(x[5])) and float(x[5])>=0 for x in block),'coverage timing value'
        two=[float(x[5]) for x in paired if x[2]=='0'];three=[float(x[5]) for x in paired if x[2]=='1'];clear=[float(x[5]) for x in clears]
        delta=[b-a for a,b in zip(two,three)]
        summary.append(dict(width=w,height=h,samples=8,warmups=2,source_draws=2,authored_union_fraction=.5,
            two_output_median_ms=statistics.median(two),three_output_median_ms=statistics.median(three),
            paired_extra_output_median_ms=statistics.median(delta),frame_clear_median_ms=statistics.median(clear),
            pairs=[dict(pair=i,two_first=i%2==0,two_ms=a,three_ms=b,extra_output_ms=b-a,frame_clear_ms=clear[i]) for i,(a,b) in enumerate(zip(two,three))]))
    return summary


def validate_pass_timings(text,fused_comparison=False):
    prefix="FUSED_TIMING" if fused_comparison else "PASS_TIMING"
    rows=re.findall(r"^"+prefix+r' width=(\d+) height=(\d+) variant=([01]) pair=(\d+) order=([01]) completed_ms=(\S+)$',text,re.M)
    assert len(rows)==32,'component timing rows'
    result=[]
    for w,h in ((1280,768),(1920,1080)):
        block=[x for x in rows if tuple(map(int,x[:2]))==(w,h)]
        assert [tuple(map(int,x[2:5])) for x in block]==[(1-order if pair%2 else order,pair,order) for pair in range(8) for order in range(2)],'component paired order'
        values={v:[float(x[5]) for x in block if int(x[2])==v] for v in (0,1)}
        assert all(math.isfinite(x) and x>=0 for group in values.values() for x in group),'component timing value'
        delta=[b-a for a,b in zip(values[0],values[1])]
        if fused_comparison:
            result.append(dict(width=w,height=h,samples=8,warmups=2,source_draws=1,authored_coverage_fraction=.3125,
                baseline_median_ms=statistics.median(values[0]),fused_median_ms=statistics.median(values[1]),
                baseline_range_ms=[min(values[0]),max(values[0])],fused_range_ms=[min(values[1]),max(values[1])],
                paired_delta_median_ms=statistics.median(delta),paired_delta_range_ms=[min(delta),max(delta)],
                delta_sign='fused minus separate-copy baseline; negative favors fused',
                pairs=[dict(pair=i,baseline_first=i%2==0,baseline_ms=a,fused_ms=b,delta_ms=b-a) for i,(a,b) in enumerate(zip(values[0],values[1]))]))
        else:
            result.append(dict(width=w,height=h,samples=8,warmups=2,source_draws=1,authored_coverage_fraction=.3125,
                native_median_ms=statistics.median(values[0]),component_median_ms=statistics.median(values[1]),
                paired_extra_median_ms=statistics.median(delta),
                pairs=[dict(pair=i,native_first=i%2==0,native_ms=a,component_ms=b,extra_ms=b-a) for i,(a,b) in enumerate(zip(values[0],values[1]))]))
    return result


def validate_coverage_report(text,data,cases,component=False,fused_comparison=False):
    result=validate_original_report(text,data,cases,True,component,fused_comparison)
    stride=4+256*17*4;covered=zero=0
    for c,offset in zip(cases,range(0,len(data),stride)):
        mask=struct.unpack_from('<1024f',data,offset+4+256*13*4)
        for i,wanted in enumerate(coverage_expected(c)):
            assert tuple(mask[i*4:i*4+3])==(wanted,)*3,(c['label'],i,'positive RGB coverage union')
            covered+=wanted>0;zero+=wanted==0
    result.update(coverage_variants=50,exact_two_three_output_channels=result['invariants']['bursts']*2048,
        coverage_pixels=256*len(cases),covered_pixels=covered,uncovered_pixels=zero,
        mask_contract='FP16 RGB positive iff at least one surviving source sample; additive count tested through16 draws; mask alpha ignored',
        source_shader_model='Eight unchanged original VS2; seven exact PS2.0 and three exact PS2.1;50 two-output and50 three-output transformer variants')
    if component:
        result.update(component=True,component_comparison_channels=4096*len(cases),indexed_source_draws=sum(len(c['ops']) for c in cases),
            steady_allocations=0,fault_controls=16,capability_twins=2,native_reset_passed=True,post_reset_same_instance_transaction=True,
            ownership='Owning-slot exchange acknowledgement preserves Incomplete source outcomes; blocked history/coverage stays unavailable after partial-B recovery')
    if fused_comparison:
        result.update(fused_comparison=True,fused_image_twins=len(cases),fused_exact_channels=256*17*len(cases),
            copy_program='qualified oC0 bytecode prefix plus MOV oC1,c20.x(+0); M detached',
            shader_creations=None)
    return result

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,default=EXE)
    p.add_argument('--raw-dir',type=Path)
    p.add_argument('--mode',choices=('ordered','mrt','mrt-branch','mrt-original','mrt-coverage','mrt-pass','mrt-fused','source-gain'),default='ordered')
    p.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    args=p.parse_args()
    if args.raw_dir is None:args.raw_dir=Path('/tmp/x3-linear-emission-gpu'+('-'+args.mode if args.mode!='ordered' else ''))
    assert bottle.BOTTLE=='X3','set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(),'game running; refused'
    assert args.exe.is_file(),'build the detached EXE explicitly first'
    args.raw_dir.mkdir(parents=True,exist_ok=True)
    cases=pass_cases() if args.mode in ('mrt-pass','mrt-fused') else coverage_cases() if args.mode=='mrt-coverage' else original_cases() if args.mode=='mrt-original' else source_gain_cases() if args.mode=='source-gain' else mrt_cases() if args.mode!='ordered' else fixture_cases();case_path=args.raw_dir/'cases.bin';case_path.write_bytes(binary_cases(cases))
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
                code_sha256={name:sha(ROOT/name) for name in INPUTS},executable_sha256=sha(args.exe),cases_sha256=sha(case_path),raw_dir=str(args.raw_dir))
    if args.mode!='ordered':
        result.update(scope='Detached authored PS2 same-draw native B/linear E and per-channel untouched A/composite C experiment; no live HdrPass, temporal producer or native Windows runtime proof',
            targets=dict(A='FP16 immutable encoded candidate',B='FP16 current native result',E='FP16 linear emission',C='FP16 publication candidate',depth='D24S8',msaa=False,srgb=False),
            tolerance=dict(rgb_relative=RGB_REL,rgb_absolute=RGB_ABS,native_RT0='exact original/augmented GPU bytes',zero_E='exact per-channel A bits',alpha='exact B alpha'),
            timing_scope='QPC through EVENT completion with reused resources/shaders/state blocks and actual capture/restore; setup/readback/reference draws excluded. Separate copy/clear/composite timings are not an additive GPU-time decomposition',
            limitations=['Authored PS2 PP/native-output parity is qualified only on the tested domain/backend; it is not a universal driver theorem',
                          'Finite A (including signed zero and values above the decode cap); finite authored sources. Positive E overflow and explicitly seeded positive infinity are separate sanitizer witnesses; NaN/negative E and nonfinite A remain unqualified',
                          'Only shared ADD/ONE/ONE with full RGBA mask15 admitted; RGB-only mode is a native refusal witness; fog/dither off',
                          'Alpha-test witnesses are boundary feasibility only; initial live admission remains alpha-test off',
                          'Composition refusal adopts B after successful source calls; failed/partial MRT source draws and failed bind/restore are not rollback-qualified',
                          'One synthetic two-source burst; batching across application setters and live ownership/consumer integration are unapproved'])
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=b',str(args.exe),'Z:'+str(case_path),'Z:'+str(pixels)]
    if args.mode!='ordered':command.append('--'+args.mode)
    if args.mode=='mrt-branch':
        result.update(scope='Detached zero-emission PS3 compositor branch experiment against unchanged baseline; same38 authored MRT cases, no live route or native Windows runtime proof',
            timing_scope='Paired baseline/branch QPC through EVENT completion with alternating order, matching inputs, cached source textures and pre-window fences; native-reference/paired-image readback excluded. Dense union50%, sparse disjoint union3.125%; single-source compositions cover31.25% or1.5625% each',
            comparison='All successful candidate compositions are compared RGBA bit-for-bit from identical A/E/B; native/refusal cases do not execute a compositor. Every case retains the original native-B/zero-lane/alpha/depth checks')
    if args.mode in ('mrt-original','mrt-coverage','mrt-pass','mrt-fused','source-gain'):
        originals={stage+'_'+name+'.bin':sha(args.programs/(stage+'_'+name+'.bin'))
                   for stage,names in (('vs',ORIGINAL_VS),('ps',ORIGINAL_PS)) for name in names}
        variants=args.raw_dir/'variants';variants.mkdir(exist_ok=True)
        command.extend(('Z:'+str(args.programs.resolve()),'Z:'+str(variants.resolve())))
        result.update(scope='Detached actual-original VS2/PS2 augmentation, native MRT parity and independent emission/composition oracle; no live route or native Windows runtime proof',
            original_sha256=originals,original_dir=str(args.programs),
            timing_scope='No new timing pass; accepted authored MRT/branch cost evidence is retained unchanged',
            source_shader_model='Eight unchanged original VS2; seven exact PS2.0 and three exact PS2.1 programs versus50 pure-transformer gain variants',
            limitations=['Actual original native _pp oC0 plus new plain full oC1 requires runtime acceptance; this run cannot qualify native Windows',
                'Finite original texture/affine/fade inputs only; source NaN/Inf and adverse emission intermediates remain GPU-unqualified',
                'Affine preshader comments stay opaque; fixture supplies known c0..2 directly, not CPU preshader execution',
                'Original VS identity WVP, DEFAULT transformed UV versus INSTANCE direct UV, layout-specific c12/c10 fade and b0 fog paths are exercised; no live material/global constant ownership proof',
                'Only shared ADD/ONE/ONE with full RGBA writes; inherited alpha tests are feasibility boundaries, not live admission',
                'No source submission failure recovery, device-loss or HdrPass integration qualification'])
    if args.mode=='source-gain':
        result.update(scope='Detached source-only encoded gain of the twenty reviewed pairs: native ADD/ONE/ONE draw versus the colour-MUL PS variant in the same state over zero and non-zero FP16 backgrounds; no live route, bloom or native Windows runtime proof',
            targets=dict(A='FP16 scene target (cleared background, native and gained images)',depth='D24S8',msaa=False,srgb=False),
            tolerance=dict(zero_background='G x native within one FP16 code',nonzero_background='native half-ULP interval propagated through bg + G (native - bg), widened by one code',alpha='exact binary16',gain_1='exact image'),
            timing_scope='No timing pass: the option adds one MUL per fragment and two native SetPixelShader calls per admitted draw',
            source_shader_model='Eight unchanged original VS2; ten exact PS2.0/PS2.x programs versus 40 source-gain variants (10 identical at gain 1)',
            limitations=['Fixture blend state is the admitted native ADD/ONE/ONE; the live admission predicate (blend shadow, HDR redirect active) is exercised by the DLL, not here',
                'Finite texture/affine/fade inputs only; sources near the FP16 cap are not exercised (clipping refuses the case)',
                'Native Windows untested; the variant is documented PS 2.0 bytecode'])
    if args.mode=='mrt-coverage':
        result.update(scope='Detached original three-output B/E/coverage producer, retained two-output parity, persistent FP16 RGB union and frame-clear experiment; no live route or native Windows runtime proof',
            timing_scope='QPC through EVENT completion; paired two/three-output two-source draws plus populated-mask clear only. Matching50% authored union, allocations/setup/readbacks outside; retained compositor evidence unchanged',
            coverage='M is same-format FP16 RT2, cleared once per authored frame and retained across brackets. Shared RGB ADD/ONE/ONE; alpha ignored; source alpha-test/depth/scissor/viewport retained')
        result['targets']['M']='persistent FP16 positive RGB source union; alpha ignored'
    if args.mode in ('mrt-pass','mrt-fused'):
        result.update(scope='Detached reusable single-DIP LinearEmissionPass qualification, exact independent original-reference B/E/C/M and explicit ownership/failure/Reset controls; no live wiring or native Windows runtime proof',
            timing_scope='Paired native DIP versus one component bracket through QPC/EVENT completion; source setup, allocations, readback and per-frame M clear outside; clear cost retained from coverage result',
            coverage='Persistent same-format FP16 M; clean native refusals preserve prior actual-enhanced coverage; failed augmented source/restoration invalidates coverage and blocks enhancement')
        result['targets']['M']='persistent FP16 RGB enhanced-source union; alpha ignored'
        result['limitations'][-2:]=[
            'Shared ADD/ONE/ONE, full effective RGBA writes, alpha-test off and Z-buffer test on; unsupported source states refuse cleanly before source submission',
            'Injected failures test explicit classification/restoration and partial-B ownership recovery; real failed draw atomicity and device-loss recovery are not guaranteed',
            'Same pass retains four programs across actual Reset, recreates four targets and completes one independently compared post-Reset source transaction; live HdrPass handoff/reference retirement remains unintegrated']
    if args.mode=='mrt-fused':
        result.update(scope='Isolated fused A-to-B/E-zero copy prototype versus checkpoint separate copy/Clear; all exact C/B/E/M/depth twins plus retained component fault/cap/Reset gates',
            timing_scope='Paired separate-copy and fused component brackets, same EXE/device;2 warmup pairs and8 alternating measured pairs per resolution. QPC through EVENT includes BeginScene, prepare, one original DIP, composition, local owning-slot acknowledgement and EndScene. Seeds, source setup, allocations, per-frame M clear and readback excluded; no live HdrPass exchange/TAA or game-FPS claim')
    result['command']=command
    try:
        with report.open('w') as out,(args.raw_dir/'wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=900)
        result['exit_code']=process.returncode
        assert process.returncode==0,'fixture failed; '+str(report)
        result.update(validate_coverage_report(report.read_text(),pixels.read_bytes(),cases,True,args.mode=='mrt-fused') if args.mode in ('mrt-pass','mrt-fused') else validate_coverage_report(report.read_text(),pixels.read_bytes(),cases) if args.mode=='mrt-coverage' else validate_original_report(report.read_text(),pixels.read_bytes(),cases) if args.mode=='mrt-original' else validate_source_gain_report(report.read_text(),pixels.read_bytes(),cases) if args.mode=='source-gain' else validate_report(report.read_text(),pixels.read_bytes(),cases) if args.mode=='ordered' else validate_mrt_report(report.read_text(),pixels.read_bytes(),cases,args.mode=='mrt-branch'))
        assert result['code_sha256']=={name:sha(ROOT/name) for name in INPUTS},'source changed during run'
        assert result['executable_sha256']==sha(args.exe),'EXE changed during run'
        assert result['cases_sha256']==sha(case_path),'case inputs changed during run'
        if args.mode in ('mrt-original','mrt-coverage','mrt-pass','mrt-fused','source-gain'):
            assert originals=={name:sha(args.programs/name) for name in originals},'original corpus changed during run'
            result['transformed_sha256']={f'ps_{name}-{g}.bin':sha(variants/f'ps_{name}-{g}.bin') for name in ORIGINAL_PS for g in range(5)}
            # Only --source-gain writes the four -source-<g> variants per PS
            # (linear_emission_fixture.cpp): every other mode leaves them absent.
            if args.mode=='source-gain':
                result['source_gain_sha256']={f'ps_{name}-source-{g}.bin':sha(variants/f'ps_{name}-source-{g}.bin') for name in ORIGINAL_PS for g in range(4)}
                assert all(sha(variants/f'ps_{name}-source-0.bin')==sha(args.programs/f'ps_{name}.bin') for name in ORIGINAL_PS),'gain 1 variant bytes differ from the original'
            if args.mode in ('mrt-coverage','mrt-pass','mrt-fused'):
                result['coverage_transformed_sha256']={f'ps_{name}-{g}-coverage.bin':sha(variants/f'ps_{name}-{g}-coverage.bin') for name in ORIGINAL_PS for g in range(5)}
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        path=bottle.results_dir(ROOT)/('linear-emission-'+args.mode+'-gpu.json' if args.mode!='ordered' else 'linear-emission-gpu.json') if result['passed'] else args.raw_dir/'failed-result.json'
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ('passed','cases','error','max_rgb_tolerance_fraction','per_gain') if k in result}))

if __name__=='__main__':main()
