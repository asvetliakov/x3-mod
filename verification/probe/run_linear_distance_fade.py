#!/usr/bin/env python3
"""Focused detached six-Asteroid fade qualification; consumes frozen EXE/CSO.

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
COMPOSITE_SHA256="0acae2e3dcfed4f04534cf09f3b74c897fd9c6e66e706c6a21c45deaf6da0b53"
SCOPE=('src/renderer/linear_material.cpp','src/renderer/linear_distance_fade.h',
       'src/renderer/linear_emission_pass.cpp','src/renderer/linear_emission_pass.h',
       'verification/probe/linear_material_fixture.cpp',
       'verification/probe/linear_alpha_test_fixture_inc.h',
       'verification/probe/linear_distance_fade_fixture_inc.h',
       'verification/probe/linear_distance_fade_composite_inc.h',
       'verification/probe/run_linear_distance_fade.py',
       'verification/probe/linear_material_reference.py','verification/probe/run_linear_material.py')
BAD_BACKGROUND=0x100000
OCCLUDER=0x200000

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
    for fault in range(1,6):
        c=copy.deepcopy(base);c.update(id=100+fault,pair=113,label='fault_'+str(fault),affine=fault)
        result.append(c)
    return result

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
    assert re.search(r'^FADE_RESULT PASS reset=1 partial_vs_failures=1$',text,re.M),'actual partial setter failure and Reset'
    assert re.search(r'^RESULT PASS cases=65$',text,re.M),'fixture terminal success'
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
            radiance=material.expected(cc).linear_rgb
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
    return dict(cases=len(expected),source_calls=sum(steps(c) for c in expected),
                numerical_channels=channels,alpha_values=alpha_values,q_values=q_values,
                mask_values=mask_values,exact_raw_channels=exact_raw,max_tolerance_fraction=max_fraction,
                energy_channels=q_values*3,exact_energy_channels=exact_energy,
                fp16_rt_store='Observed X3 round-toward-zero; fixed for every recurrence/output row; not a D3D9/native-Windows guarantee',
                source_alpha_identity='Exact original/dual native RGBA and native/E alpha in RGBA32F before FP16 storage',
                fault_cases=5,capability_refusals=4,state_refusals=3,reset=True,owned_references_retired=True,
                temporal_scope='Pass coverage only; no current/previous mask integration or transparent TAA claim')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--composite',type=Path,required=True,help='Frozen CSO from host --composite exporter')
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--raw-dir',type=Path,required=True)
    args=parser.parse_args()
    assert bottle.BOTTLE=='X3','Set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(),'Game running; no fixture launch'
    args.exe=args.exe.resolve();args.composite=args.composite.resolve();args.programs=args.programs.resolve();args.raw_dir=args.raw_dir.resolve()
    assert sha(args.composite)==COMPOSITE_SHA256,'Composite must match reviewed authored program'
    args.raw_dir.mkdir(parents=True,exist_ok=False)
    source=cases();case_file=args.raw_dir/'cases.bin';case_file.write_bytes(material.binary_cases(source))
    input_files=[args.exe,args.composite,case_file]+[ROOT/name for name in SCOPE]
    for vs,ps in material.ASTEROID_PAIRS:
        input_files += [args.programs/f'vs_{vs}.bin',args.programs/f'ps_{ps}.bin']
    hashes={str(path):sha(path) for path in input_files}
    report=args.raw_dir/'report.txt'
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=b',str(args.exe),
             'Z:'+str(args.programs),'Z:'+str(case_file),'--distance-fade','Z:'+str(args.composite)]
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,command=command,input_sha256=hashes,
                raw_report=str(report),scope='Detached exact six-pair source-over prototype; no live route',
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
        target=bottle.results_dir(ROOT)/'linear-distance-fade-gpu.json' if result['passed'] else args.raw_dir/'failed-result.json'
        target.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({key:result[key] for key in ('passed','cases','source_calls','wall_seconds','error') if key in result}))

if __name__=='__main__':main()
