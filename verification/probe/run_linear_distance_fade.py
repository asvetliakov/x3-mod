#!/usr/bin/env python3
"""Focused detached fade qualification (six Asteroid pairs plus the station
BUMPMAP pair); consumes frozen EXE/CSO.

Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. No build, game launch,
production registration or temporal-mask integration is performed here.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import bottle
from game_guard import game_running
import run_linear_material as material
import linear_material_reference as ref

ROOT=Path(__file__).resolve().parents[2]
SIZE=16
COMPOSITE_SHA256="7b5599fcce4796ab5c2095c7df587c5ce2544bde1db9f2885dab59bfaa30f43d"  # prototype 1: two-sample fade composite, 143 DWORDs
SCOPE=('src/renderer/linear_material.cpp','src/renderer/linear_distance_fade.h',
       'src/renderer/linear_emission_pass.cpp','src/renderer/linear_emission_pass.h',
       'verification/probe/linear_material_fixture.cpp',
       'verification/probe/linear_alpha_test_fixture_inc.h',
       'verification/probe/linear_distance_fade_fixture_inc.h',
       'verification/probe/linear_distance_fade_composite_inc.h',
       'src/proxy/fade_region_math.h','src/proxy/fade_region_core.h','src/proxy/locked_prefix_core.h',
       'verification/probe/run_linear_distance_fade.py',
       'verification/probe/linear_material_reference.py','verification/probe/run_linear_material.py')
BAD_BACKGROUND=0x100000
OCCLUDER=0x200000
# Seventh producer: the standard BUMPMAP hull pair 4944d81dfe531b37/64bac8bb307eb896
# (docs/architecture/linear-station-source-over.md), run_linear_material.PAIRS[51].
# Its material alpha stays the vertex AlphaValue (c39.x): .625 as in the ordinary
# fixture, or 0 / 1 by these flags; the Asteroid producers overwrite it with one.
STATION_PAIR=51
STATION_ALPHA_ZERO=0x400000
STATION_ALPHA_ONE=0x800000
# Region case group (linear_distance_fade_fixture_inc.h region_cases, same
# order): label, whether the production projection must yield a box-derived
# rectangle, whether the source must rasterise at least one M pixel, and the
# application viewport. Rectangles come from the fixture's call of the
# production header; the runner re-checks every M pixel against them.
REGION_CASES=(('interior',1,1),('edge_left',1,1),('edge_right',1,1),('edge_top',1,1),('edge_bottom',1,1),
              ('corner',1,1),('offscreen',1,0),('w_zero',0,0),('w_negative',0,0),('w_tiny',1,0),('near_plane',1,1),
              ('nan_rows',0,0),('inf_rows',0,0),('rows_unknown',0,1),('bound_unknown',0,1),('negative_extent',0,0),
              ('fill_wireframe',0,0),('tiny',1,0),('huge',1,1),('viewport_offset',1,1),('scissor',1,1))+tuple(
              (f'jitter_{k}',1,1) for k in range(1,9))
REGION_VIEWPORT={'viewport_offset':(4,4,8,8)}
# Step B locked-prefix group (linear_distance_fade_fixture_inc.h prefix_cases,
# same order): label, quads, tail kind (0 zeros, 1 NaN, 2 huge finite, 3
# beyond the world limit), expected bound flag, whether the source must
# rasterise at least one M pixel. The after-Reset case runs on the cleared table.
PREFIX_CASES=(('one_quad',1,0,1,1),('sixteen_quads_nan_tail',16,1,1,1),('seventeen_quads_nan_tail',17,1,0,1),
              ('seventeen_quads_zero_tail',17,0,1,1),('hundred_quads_huge_tail',100,2,1,1),('hundred_quads_absurd_tail',100,3,0,1),
              ('full_buffer',1024,1,1,1),('offset_box',40,0,1,1),('edge_box',25,2,1,1),
              ('jitter_1',20,0,1,1),('jitter_3',32,1,1,1),('jitter_6',33,0,1,1))
PREFIX_AFTER_RESET=('after_reset',17,0,1,1)
PREFIX_LOOKUPS={'seventeen_quads_nan_tail':'nonfinite','hundred_quads_absurd_tail':'nonfinite'}
REGION_REASONS={'w_zero':4,'w_negative':4,'nan_rows':5,'inf_rows':5,'rows_unknown':2,'bound_unknown':3,'negative_extent':3,'fill_wireframe':6}
# Step 2 in-place policy (linear_distance_fade_fixture_inc.h inplace_cases, same
# order): label and bracket count. Every non-fault case step, every region case
# and every rectangle case is also run in place on a copy of A by a second
# component instance; the fixture compares in-place A with the exchanged C and
# the two M targets bit-exactly, so the runner only checks the witness counts.
INPLACE_CASES=(('full_unknown',1),('exact',1),('partial',1),('zero_scissor',1),('tiny',1),('disjoint',2),('overlapping',2),('unknown_then_known',2))
# Failure ladder: stage, label, prepared, expected first HRESULT, recovery HRESULT, coverage, blocked.
INPLACE_LADDER=((1,'partial_vs',0,0x80004005,0x1,1,0),(2,'copy',0,0x80004005,0x1,1,0),(3,'region_scissor',0,0x80004005,0x1,1,0),
                (4,'source',1,0x8876086c,0x0,0,1),(5,'composite',1,0x80004005,0x0,0,1),(6,'composite_scissor',1,0x80004005,0x0,0,1),
                (7,'restore',1,0x80004005,0x1,0,1),(8,'recovery',1,0x8876086c,0x80004005,0,1),
                (9,'restore_recovery',1,0x8876086c,0x0,0,1))
TIMING_SIZES=((1280,768),(1920,1080));TIMING_FRACTIONS=3;TIMING_POLICIES=('exchange','inplace');TIMING_DIPS=(1,4,16);TIMING_ITERATIONS=8

def inplace_brackets(active):
    """In-place brackets of one batch: every non-fault case step, plus (first batch) region, rectangle and fallback twins."""
    reset=any(c['id']>=1000 for c in active)
    count=sum(steps(c) for c in active if not c['affine'])
    if not reset:count+=len(REGION_CASES)+sum(b for _,b in INPLACE_CASES)+1
    return count

def cases():
    base=dict(depth=1,lights=1,reverse=0,affine=0,valid=1,fp16=1,
              gains=[1.,1.,1.],diffuse=[.5,.25,.75,.5],lightmap=[.125,.25,.0625,.25],
              cube=[.25,.5,.125,1.],mask=.25,material=[.25,.125,.0625],point=[.5,.25,.125],
              dir0=[.375,.25,.5],dir1=[.125,.5,.25],normal=[0.,0.,1.],glow=.25,flags=0,
              normal_sample=[.25,.375,.75,.625],binormal=[0.,1.,0.],tangent=[1.,0.,0.],
              camera=[0.,0.,4.],fog_clip=[.75,.125],coefficients=[1.,.5,.0625,.1875])
    result=[]
    names=('fog_partial','zero','full','same_DIP_overlap','ordered_DIPs','zero_repeated',
           'HDR_cap','bad_background_full','captured_fog','bad_background_zero_repeated')
    for pair in range(110,116):
        for index,name in enumerate(names):
            c=copy.deepcopy(base);c.update(id=(pair-110)*10+index,pair=pair,label=name)
            if index==0:
                c['flags']=material.FOG
                if pair==111:c.update(gains=[0.,0.,0.],label='fog_partial_zero_gain')
            if index in (1,5,9):c['diffuse'][3]=0.
            if index in (2,6,7):c['diffuse'][3]=1.
            if index==3:
                c['reverse']=1
                if pair==113:c['flags']|=OCCLUDER
            if index==4:c['reverse']=2
            if index in (5,9):c['reverse']=3
            if index in (7,9):c['flags']|=BAD_BACKGROUND
            if index==6:
                c.update(gains=[16.]*3,dir0=[8.,4.,2.],lights=8);c['diffuse'][:3]=[2.,3.,4.]
            if index==8:
                c.update(flags=material.FOG,camera=[0.,0.,1000000.],fog_clip=[1.0526316165924072,2.1052636611784692e-7])
            result.append(c)
    # Station producer (ids 60-65): AlphaValue 0 / .625 / 1, EnableGlow 0 / 1 /
    # .375 with asymmetric diffuse/lightmap alpha, fog off / on, and two
    # overlapping primitives in one DIP over the depth occluder (Z-test on,
    # Z-write off). The port's captured state has AlphaValue 1 and, at
    # distance, the fog factor (station-material-distance.md).
    station=(('station_alpha_0',dict(glow=0.),STATION_ALPHA_ZERO,0),
             ('station_alpha_625_glow_0',dict(glow=0.),0,0),
             ('station_alpha_1_glow_1',dict(glow=1.,lightmap=[.125,.25,.0625,.75]),STATION_ALPHA_ONE,0),
             ('station_glow_375_asymmetric',dict(glow=.375,lightmap=[.125,.25,.0625,.875]),STATION_ALPHA_ONE,0),
             ('station_captured_fog',dict(),material.FOG,0),
             ('station_same_DIP_overlap_occluder',dict(),STATION_ALPHA_ONE|OCCLUDER,1))
    for index,(name,fields,flags,reverse) in enumerate(station):
        c=copy.deepcopy(base);c.update(fields);c.update(id=60+index,pair=STATION_PAIR,label=name,flags=flags,reverse=reverse)
        result.append(c)
    for fault in range(1,6):
        c=copy.deepcopy(base);c.update(id=100+fault,pair=113,label='fault_'+str(fault),affine=fault)
        result.append(c)
    return result

def station_alpha_value(c):
    return 0. if c['flags']&STATION_ALPHA_ZERO else 1. if c['flags']&STATION_ALPHA_ONE else .625

def oracle_case(c):
    """Case handed to the ordinary oracle: the fade Draw never flips the winding
    (reverse selects overlap modes here), so the station pair is lit front-facing."""
    if c['pair']!=STATION_PAIR:return c
    c=copy.deepcopy(c);c['reverse']=0
    return c

def expanded_cases(source):
    result=copy.deepcopy(source)
    for c in source:
        if c['id']%10==0:
            c=copy.deepcopy(c);c['id']+=1000;result.append(c)
    return result

def step_case(c,step):
    current=copy.deepcopy(c)
    if c['reverse']==2 and step:
        current['diffuse'][0],current['diffuse'][2]=current['diffuse'][2],current['diffuse'][0]
        current['diffuse'][3]=.25
    return current

def steps(c):return 2 if c['reverse']==2 else 16 if c['reverse']==3 else 1

def source_alpha(c):
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    a=f32(c['diffuse'][3])
    if c['pair']==STATION_PAIR:
        # Native: AlphaValue * fog * (EnableGlow * LightMap.a + (1 - EnableGlow) * Diffuse.a).
        glow=f32(c['glow'])
        a=f32(station_alpha_value(c))*(glow*f32(c['lightmap'][3])+(1-glow)*a)
    if c['flags']&material.FOG:
        a*=min(1.,max(0.,f32(c['fog_clip'][0])-f32(c['fog_clip'][1])*math.hypot(*(f32(x) for x in c['camera']))))
    return a

def covered(c,step,x,y):
    lo,hi=(6,14) if c['reverse']==2 and step else (2,10)
    return lo<=x<hi and lo<=y<hi and not (c['flags']&OCCLUDER and x<6)

def q_scalar(value):
    if math.isnan(value) or value<0:return 0.
    return min(value,1.)

def fp16_rt_store(value):
    """Observed X3 finite FP16 render-target narrowing: round toward zero.

    This is a fixed oracle model, not a per-pixel fit or a D3D9/native-Windows
    guarantee. Uploaded binary16 values still use the shared nearest-even half().
    """
    if not math.isfinite(value):
        raise ValueError('RT-store oracle expects finite post-policy values')
    rounded=ref.half(value)
    if abs(rounded)>abs(value):
        bits=struct.unpack('<H',struct.pack('<e',rounded))[0]
        rounded=struct.unpack('<e',struct.pack('<H',bits-1))[0]
    return rounded

def compose(a,q_rgb,q):
    q=q_scalar(q)
    if q==0:return tuple(a)
    rgb=[ref.encode(ref.sanitize(x)) for x in q_rgb] if q==1 else [
        ref.encode(ref.sanitize(q_rgb[i])+(1-q)*ref.sanitize(ref.decode(a[i]))) for i in range(3)]
    return tuple(fp16_rt_store(x) for x in rgb)+(a[3],)

def pixels(path):
    data=path.read_bytes()
    assert len(data)==SIZE*SIZE*16,(path,'readback length')
    return list(struct.iter_unpack('<4f',data))

def same_float(a,b):return struct.pack('<f',a)==struct.pack('<f',b)

def validate_report(text,source,raw):
    expected=expanded_cases(source)
    rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_CASE ')]
    assert [int(r['id']) for r in rows]==[c['id'] for c in expected],'exact ordered case set'
    failures=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_FAILURE ')]
    assert len(failures)==5,'all five failure boundaries'
    for row,fault in zip(failures,range(1,6)):
        assert int(row['id'])==100+fault and int(row['stage'])==fault
        assert int(row['native'])==1 and int(row['prepared'])==int(fault>=3)
        assert int(row['first'],16)==(0x8876086c if fault==3 else 0x80004005),'chronological failure HRESULT'
        assert int(row['coverage'])==int(fault not in (3,5))
    assert text.count('FADE_CAPS refused=4')==1 and text.count('FADE_STATE refused=3')==1,'capability and state refusal witnesses'
    assert re.search(r'^FADE_RESULT PASS reset=1 partial_vs_failures=2$',text,re.M),'actual partial setter failures (exchange and in-place ladders) and Reset'
    assert re.search(r'^RESULT PASS cases=71$',text,re.M),'fixture terminal success'
    batches=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_BATCH ')]
    assert len(batches)==2
    for reset,batch in enumerate(batches):
        active=[c for c in expected if (c['id']>=1000)==bool(reset)]
        calls=sum(steps(c) for c in active)
        assert int(batch['reset'])==reset and int(batch['cases'])==len(active)
        assert int(batch['native'])==calls and int(batch['brackets'])==calls-sum(c['affine'] in (1,2) for c in active)
        assert int(batch['restored'])==calls
        assert int(batch['refs_before'])==int(batch['refs_after']),'device owning resource retirement'
    channels=alpha_values=q_values=mask_values=exact_raw=exact_energy=0;max_fraction=0.
    for c,row in zip(expected,rows):
        count=steps(c)
        assert int(row['pair'])==c['pair'] and int(row['steps'])==count
        assert int(row['native'])==count and int(row['brackets'])==count-int(c['affine'] in (1,2))
        assert int(row['fault'])==c['affine'] and int(row['reset'])==int(c['id']>=1000)
        if c['affine']:continue # exact native/state/source-once fault gates run in C++
        current=pixels(raw/f"fade_{c['id']}_0_initial.rgba32f")
        union=[False]*(SIZE*SIZE)
        for step in range(count):
            cc=step_case(c,step)
            native_source=pixels(raw/f"fade_{c['id']}_{step}_source.rgba32f")
            energy=pixels(raw/f"fade_{c['id']}_{step}_E.rgba32f")
            mask=pixels(raw/f"fade_{c['id']}_{step}_M.rgba32f")
            output=pixels(raw/f"fade_{c['id']}_{step}_C.rgba32f")
            radiance=material.expected(oracle_case(cc)).linear_rgb
            alpha=source_alpha(cc)
            repeats=2 if c['reverse']==1 else 1
            for n,(a,e,m,out,src) in enumerate(zip(current,energy,mask,output,native_source)):
                x,y=n%SIZE,n//SIZE;inside=covered(c,step,x,y);union[n]|=inside
                q=0.;Q=[0.,0.,0.]
                if inside:
                    for _ in range(repeats):
                        Q=[fp16_rt_store(alpha*L+(1-alpha)*old) for L,old in zip(radiance,Q)]
                        q=fp16_rt_store(alpha+(1-alpha)*q)
                assert same_float(e[3],q),(c['id'],step,x,y,'q',e[3],q)
                q_values+=1
                want_alpha=ref.half(alpha) if inside else 0.
                # Exact original/dual/E RGBA32F alpha equality is checked in C++.
                # The independent scalar oracle retains its strict FP16-visible
                # precision contract; do not reconstruct blend alpha from E.
                assert math.isfinite(src[3]) and 0<=src[3]<=1
                assert same_float(ref.half(src[3]),want_alpha),(c['id'],step,'source alpha',src[3],want_alpha)
                assert same_float(out[3],a[3]),(c['id'],step,'native destination alpha')
                alpha_values+=2
                wanted_mask=(1.,1.,1.,0.) if union[n] else (0.,0.,0.,0.)
                assert m==wanted_mask,(c['id'],step,'union M',m)
                mask_values+=4
                wanted=compose(a,Q,q)
                for k in range(3):
                    exact_energy+=int(same_float(e[k],Q[k]))
                    for actual,target,label in ((e[k],Q[k],'Q'),(src[k],radiance[k] if inside else 0.,'L')):
                        tolerance=material.RGB_ABS_TOL+material.RGB_REL_TOL*abs(target)
                        fraction=abs(actual-target)/tolerance
                        if target==0:assert same_float(actual,0.),(c['id'],step,label,'exact positive zero')
                        assert math.isfinite(actual) and actual>=0 and fraction<=1,(c['id'],step,x,y,label,k,actual,target)
                        max_fraction=max(max_fraction,fraction);channels+=1
                    if q==0:
                        assert same_float(out[k],a[k]),(c['id'],step,x,y,'raw A q0',k)
                        exact_raw+=1
                    else:
                        tolerance=material.RGB_ABS_TOL+material.RGB_REL_TOL*abs(wanted[k])
                        fraction=abs(out[k]-wanted[k])/tolerance
                        assert math.isfinite(out[k]) and out[k]>=0 and fraction<=1,(c['id'],step,x,y,'C',k,out[k],wanted[k])
                        max_fraction=max(max_fraction,fraction)
                    channels+=1
            current=output
    regions=validate_regions(text,raw)
    regions.update(validate_prefix(text,raw))
    regions.update(validate_inplace(text,expected))
    regions.update(validate_timing(text))
    return dict(cases=len(expected),source_calls=sum(steps(c) for c in expected),**regions,
                numerical_channels=channels,alpha_values=alpha_values,q_values=q_values,
                mask_values=mask_values,exact_raw_channels=exact_raw,max_tolerance_fraction=max_fraction,
                energy_channels=q_values*3,exact_energy_channels=exact_energy,
                fp16_rt_store='Observed X3 round-toward-zero; fixed for every recurrence/output row; not a D3D9/native-Windows guarantee',
                source_alpha_identity='Exact original/dual native RGBA and native/E alpha in RGBA32F before FP16 storage',
                fault_cases=5,capability_refusals=4,state_refusals=3,reset=True,owned_references_retired=True,
                temporal_scope='Pass coverage only; no current/previous mask integration or transparent TAA claim')

def validate_regions(text,raw):
    rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_REGION ')]
    assert [r['label'] for r in rows]==[c[0] for c in REGION_CASES],'exact ordered region case set'
    result=re.search(r'^FADE_REGION_RESULT cases=(\d+) bound=(\d+) violations=(\d+)$',text,re.M)
    assert result and int(result.group(1))==len(REGION_CASES) and int(result.group(3))==0,'region group terminal line'
    assert int(result.group(2))==sum(c[1] for c in REGION_CASES),'bound case count'
    fractions={};violations=0;covered_total=0
    for (label,expect_bound,expect_covered),row in zip(REGION_CASES,rows):
        x,y,w,h=REGION_VIEWPORT.get(label,(0,0,SIZE,SIZE))
        assert tuple(int(v) for v in row['viewport'].split(','))==(x,y,w,h),(label,'viewport')
        l,t,r,b=(int(v) for v in row['rect'].split(','))
        assert int(row['bound'])==expect_bound,(label,'bound flag',row)
        assert int(row['reason'])==REGION_REASONS.get(label,0),(label,'reason',row)
        if not expect_bound:assert (l,t,r,b)==(x,y,x+w,y+h),(label,'full viewport on doubt',row)
        assert x<=l<r<=x+w and y<=t<b<=y+h,(label,'rectangle inside the viewport',row)
        assert int(row['area'])==(r-l)*(b-t)
        mask=pixels(raw/f"fade_{int(row['id'])}_0_M.rgba32f")
        covered=0
        for n,m in enumerate(mask):
            if m[0]==0:continue
            assert m==(1.,1.,1.,0.),(label,'M value',m)
            covered+=1;px,py=n%SIZE,n//SIZE
            if not (l<=px<r and t<=py<b):violations+=1
        assert covered==int(row['covered']),(label,'covered count')
        assert int(row['violations'])==0 and violations==0,(label,'M pixel outside the rectangle')
        if expect_covered:assert covered>0,(label,'source rasterised nothing')
        if label=='offscreen':assert covered==0,(label,'off-screen box rasterised')
        covered_total+=covered;fractions[label]=(r-l)*(b-t)/(w*h)
    return dict(region_cases=len(rows),region_bound_cases=sum(c[1] for c in REGION_CASES),region_violations=violations,
                region_covered_pixels=covered_total,region_area_fractions=fractions,
                region_scope='Step 1: rectangle derived and witnessed against M only; the prototype-1 bracket composes the full target')

def validate_prefix(text,raw):
    """Step B: every M pixel of the synthetic bullet producer inside the locked-prefix rectangle; refusals never get a rectangle."""
    rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_PREFIX ')]
    cases=PREFIX_CASES+(PREFIX_AFTER_RESET,)
    assert [r['label'] for r in rows]==[c[0] for c in cases],'exact ordered prefix case set (both Reset sides)'
    results=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_PREFIX_RESULT ')]
    assert [int(r['reset']) for r in results]==[0,1],'one prefix group per Reset side'
    assert int(results[0]['cases'])==len(PREFIX_CASES) and int(results[1]['cases'])==1
    assert all(int(r['violations'])==0 for r in results) and int(results[1]['table_cleared'])==1,'prefix terminal lines'
    assert int(results[0]['bound'])==sum(c[3] for c in PREFIX_CASES) and int(results[1]['bound'])==1
    assert all(int(r['vertices'])==6144 for r in results),'the whole 6144-vertex window is scanned per lock'
    violations=0;covered_total=0;fractions={};scan_us=[]
    for (label,quads,tail,expect_bound,expect_covered),row in zip(cases,rows):
        assert int(row['quads'])==quads and int(row['vertices'])==6*quads and int(row['primitives'])==2*quads and int(row['tail'])==tail,(label,row)
        assert int(row['bound'])==expect_bound and int(row['expect_bound'])==expect_bound,(label,'bound flag',row)
        assert row['lookup']==PREFIX_LOOKUPS.get(label,'bound'),(label,'lookup',row)
        assert int(row['checkpoint'])==(6*quads+95)//96-1,(label,'covering checkpoint',row)
        assert int(row['scanned'])==6144 and int(row['table_used'])==1,(label,'scan window and table occupancy',row)
        l,t,r,b=(int(v) for v in row['rect'].split(','))
        if expect_bound:assert 0<=l<r<=SIZE and 0<=t<b<=SIZE and int(row['reason'])==0,(label,'rectangle inside the viewport',row)
        else:assert (l,t,r,b)==(0,0,0,0) and int(row['reason'])==3,(label,'a refused draw has no rectangle',row)
        assert int(row['area'])==(r-l)*(b-t)
        mask=pixels(raw/f"fade_{int(row['id'])}_0_M.rgba32f")
        covered=0
        for n,m in enumerate(mask):
            if m[0]==0:continue
            assert m==(1.,1.,1.,0.),(label,'M value',m)
            covered+=1;px,py=n%SIZE,n//SIZE
            if expect_bound and not (l<=px<r and t<=py<b):violations+=1
        assert covered==int(row['covered']),(label,'covered count')
        assert int(row['violations'])==0 and violations==0,(label,'M pixel outside the prefix rectangle')
        if expect_covered:assert covered>0,(label,'source rasterised nothing')
        covered_total+=covered;scan_us.append(float(row['scan_us']))
        if expect_bound:fractions[label]=(r-l)*(b-t)/(SIZE*SIZE)
    return dict(prefix_cases=len(rows),prefix_bound_cases=sum(c[3] for c in cases),prefix_refused_cases=sum(1 for c in cases if not c[3]),
                prefix_violations=violations,prefix_covered_pixels=covered_total,prefix_area_fractions=fractions,
                prefix_scan_us=dict(max=max(scan_us),mean=sum(scan_us)/len(scan_us),vertices_per_scan=6144,locks=len(rows)),
                prefix_reset=dict(table_cleared=True,relearned=int(results[1]['bound'])==1),
                prefix_scope='Step B: locked-prefix checkpoints scanned at Unlock, reduced at the draw, witnessed against M only; no route consumes the rectangle')

def validate_inplace(text,expected):
    rows=lambda prefix:[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]
    assert not rows('FADE_INPLACE_DIFF') and not rows('FADE_INPLACE_REFUSAL') and not rows('FADE_INPLACE_INCOMPLETE'),'in-place mismatch witness'
    batches=rows('FADE_INPLACE_BATCH')
    assert [int(b['reset']) for b in batches]==[0,1],'one in-place batch per Reset side'
    brackets=0
    for reset,batch in enumerate(batches):
        active=[c for c in expected if (c['id']>=1000)==bool(reset)]
        want=inplace_brackets(active)
        cases=sum(1 for c in active if not c['affine'])+(0 if reset else len(REGION_CASES)+len(INPLACE_CASES)+1)
        assert int(batch['cases'])==cases,(reset,'in-place case count',batch)
        assert int(batch['brackets'])==want and int(batch['native'])==want,(reset,'in-place bracket/source-once count',batch)
        assert int(batch['exact_a'])==want and int(batch['exact_m'])==want,(reset,'bit-exact comparison count',batch)
        brackets+=want
    regions=rows('FADE_INPLACE_REGION')
    assert [r['label'] for r in regions]==[c[0] for c in REGION_CASES] and all(int(r['exact'])==1 for r in regions),'region twins'
    rect=rows('FADE_INPLACE')
    assert [(r['label'],int(r['brackets'])) for r in rect]==list(INPLACE_CASES) and all(int(r['exact'])==1 for r in rect),'rectangle case group'
    ladder=rows('FADE_INPLACE_FAILURE')
    assert len(ladder)==len(INPLACE_LADDER),'in-place failure ladder'
    for row,(stage,label,prepared,first,recovery,coverage,blocked) in zip(ladder,INPLACE_LADDER):
        assert int(row['stage'])==stage and row['label']==label and int(row['native'])==1 and int(row['exchange'])==0,(label,row)
        assert int(row['prepared'])==prepared and int(row['first'],16)==first,(label,'chronological first HRESULT',row)
        assert int(row['recovery'],16)==recovery,(label,'recovery HRESULT',row)
        assert int(row['coverage'])==coverage and int(row['blocked'])==blocked,(label,'suppression',row)
    assert text.count('FADE_INPLACE_CAPS refused=1 fallback=1 exact=1')==1,'no-scissor capability refusal and exchange fallback'
    assert text.count('FADE_INPLACE_RESET interrupted=1 detached=1')==1,'interrupted in-place bracket Reset'
    return dict(inplace_cases=sum(int(b['cases']) for b in batches),inplace_brackets=brackets,inplace_exact_comparisons=2*brackets,
                inplace_rectangle_cases=len(rect),inplace_ladder_stages=len(ladder),inplace_capability_refusals=1,inplace_reset=True,
                inplace_scope='Step 2: in-place A equals the exchanged C and M bit-exactly per bracket; no runtime route (step 3)')

def validate_timing(text):
    rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('FADE_TIMING ')]
    assert 'FADE_TIMING_RESULT sizes=2 fractions=3 policies=2 dips=3 iterations=8' in text,'timing group terminal line'
    groups={};iterations={}
    for r in rows:
        key=(int(r['width']),int(r['height']),r['policy'],float(r['f']),int(r['dips']))
        groups.setdefault(key,[]).append(float(r['completed_ms']));iterations.setdefault(key,[]).append(int(r['iteration']))
    expected=len(TIMING_SIZES)*TIMING_FRACTIONS*len(TIMING_POLICIES)*len(TIMING_DIPS)
    assert len(groups)==expected and all(v==list(range(TIMING_ITERATIONS)) for v in iterations.values()),'timing windows'
    timing=[dict(width=w,height=h,policy=policy,area_fraction=f,dips=dips,median_ms=sorted(v)[len(v)//2],min_ms=min(v),max_ms=max(v))
            for (w,h,policy,f,dips),v in sorted(groups.items())]
    return dict(timing=timing,timing_scope='EVENT-fenced windows of prepare/source/finish(/exchange) per frame on the detached device; M clear outside the window; not game FPS')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--composite',type=Path,required=True,help='Frozen CSO from host --composite exporter')
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--raw-dir',type=Path,required=True)
    parser.add_argument('--result-name',default='linear-distance-fade-gpu.json',help='Compact result file name under the bottle results directory')
    args=parser.parse_args()
    assert bottle.BOTTLE=='X3','Set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(),'Game running; no fixture launch'
    args.exe=args.exe.resolve();args.composite=args.composite.resolve();args.programs=args.programs.resolve();args.raw_dir=args.raw_dir.resolve()
    assert sha(args.composite)==COMPOSITE_SHA256,'Composite must match reviewed authored program'
    args.raw_dir.mkdir(parents=True,exist_ok=False)
    source=cases();case_file=args.raw_dir/'cases.bin';case_file.write_bytes(material.binary_cases(source))
    input_files=[args.exe,args.composite,case_file]+[ROOT/name for name in SCOPE]
    for vs,ps in material.ASTEROID_PAIRS+(material.PAIRS[STATION_PAIR],):
        input_files += [args.programs/f'vs_{vs}.bin',args.programs/f'ps_{ps}.bin']
    hashes={str(path):sha(path) for path in input_files}
    report=args.raw_dir/'report.txt'
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=b',str(args.exe),
             'Z:'+str(args.programs),'Z:'+str(case_file),'--distance-fade','Z:'+str(args.composite)]
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,command=command,input_sha256=hashes,
                raw_report=str(report),scope='Detached exact seven-pair source-over prototype (six Asteroid, one station BUMPMAP); no live route',
                tolerance=dict(relative=material.RGB_REL_TOL,absolute=material.RGB_ABS_TOL,
                               exact='native B, output alpha, native/E alpha, q, M, q0 raw A'),
                composite_bytes=args.composite.stat().st_size)
    start=time.monotonic()
    try:
        with report.open('w') as out,(args.raw_dir/'wine.log').open('w') as err:
            process=subprocess.run(command,cwd=args.raw_dir,stdout=out,stderr=err,
                env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=600)
        result.update(exit_code=process.returncode,wall_seconds=time.monotonic()-start)
        assert process.returncode==0,'Fixture failed; see raw report'
        result.update(validate_report(report.read_text(),source,args.raw_dir))
        assert hashes=={str(path):sha(path) for path in input_files},'Inputs changed during qualification'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error);raise
    finally:
        target=bottle.results_dir(ROOT)/args.result_name if result['passed'] else args.raw_dir/'failed-result.json'
        target.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({key:result[key] for key in ('passed','cases','source_calls','wall_seconds','error') if key in result}))

if __name__=='__main__':main()
