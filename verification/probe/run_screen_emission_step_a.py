#!/usr/bin/env python3
"""Step A of docs/architecture/screen-emission-region.md: consume the prebuilt
screen_emission_step_a_fixture.exe (production LinearEmissionPass policy 8
inside the frozen packed prototype) and write the compact result.

Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Detached device only: no
build, no game launch, no runtime admission or route (steps B/C).
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time
import bottle
from game_guard import game_running
import run_linear_emission_sm1_packed as packed

ROOT=Path(__file__).resolve().parents[2]
IN_DOMAIN=tuple(c for c in range(24) if c not in packed.BOUNDARIES)
ROWS=9*len(IN_DOMAIN)*3
LADDER=(('copy',0,0x80004005,0x1,1,0),('region_scissor',0,0x80004005,0x1,1,0),('plane_init',0,0x80004005,0x1,1,0),
        ('source_bind',0,0x80004005,0x1,1,0),('source',1,0x8876086c,0x0,0,1),('composite',1,0x80004005,0x0,0,1),
        ('composite_scissor',1,0x80004005,0x0,0,1),('restore',1,0x80004005,0x1,0,1),('recovery',1,0x8876086c,0x80004005,0,1),
        ('restore_recovery',1,0x8876086c,0x0,0,1))
TIMING_SIZES=((1280,768),(1920,1080));TIMING_AREAS=(2352,24150);TIMING_POLICIES=('native','packed');TIMING_DIPS=(1,4,16);TIMING_ITERATIONS=8
SOURCES=('src/renderer/linear_emission_pass.cpp','src/renderer/linear_emission_pass.h',
         'src/renderer/linear_screen_plane_init_inc.h','src/renderer/linear_screen_composite_inc.h',
         'src/renderer/linear_emission_sm1.cpp','src/renderer/linear_emission_sm1.h',
         'verification/probe/screen_emission_step_a_fixture.cpp','verification/probe/linear_emission_sm1_packed_fixture.cpp',
         'verification/probe/build_screen_emission_step_a.sh','verification/probe/run_screen_emission_step_a.py')

fields=packed.fields
sha=packed.sha

def finite(value):
    value=float(value);assert math.isfinite(value) and value>=0
    return value

def rect(text):
    l,t,r,b=(int(v) for v in text.split(','));assert 0<=l<r and 0<=t<b
    return l,t,r,b

def validate(output):
    lines=output.splitlines();assert not any(x.startswith(('STEP_A_ABORT','PACKED_ABORT','SM1_API')) for x in lines),'fixture aborted'
    attach=[fields(x) for x in lines if x.startswith('STEP_A_ATTACH ')]
    assert len(attach)==1 and {k:int(v) for k,v in attach[0].items()}==dict(supported=15,available=15,allocations=5,references=12),'policy 8 attach, five-target pool, seven programs'
    rows=[fields(x) for x in lines if x.startswith('STEP_A_ROW ')]
    assert [(int(x['pair']),int(x['case']),int(x['schedule'])) for x in rows]==[(p,c,s) for p in range(9) for c in IN_DOMAIN for s in range(3)],'every in-domain packed row through policy 8'
    exact=inside=outside=mask=conservative=proto=0;per_schedule=[0,0,0];pixels=0;surviving=overlap=0
    for row in rows:
        c,s=int(row['case']),int(row['schedule']);assert row['name']==packed.CASES[c] and int(row['known'])==int(s!=1)
        l,t,r,b=rect(row['rect']);assert r<=32 and b<=32
        if s==1:assert (l,t,r,b)==(0,0,32,32),'unknown selects the whole target'
        counts=[int(row[k]) for k in ('inside_diff','outside_diff','mask_diff','red_outside')]
        assert int(row['prototype_failed'])==0,'prototype gate failed in-domain';proto+=int(row['prototype_failed'])==0
        assert all(0<=v<=8192 for v in counts)
        inside+=counts[0]==0;outside+=counts[1]==0;mask+=counts[2]==0;conservative+=counts[3]==0
        ok=not any(counts);exact+=ok;per_schedule[s]+=ok;pixels+=(r-l)*(b-t)
        if c not in (9,13):assert int(row['surviving'])>0
        surviving+=int(row['surviving']);overlap+=int(row['overlap'])
    corpus=[fields(x) for x in lines if x.startswith('STEP_A_CORPUS ')]
    assert len(corpus)==1
    assert {k:int(v) for k,v in corpus[0].items()}==dict(rows=ROWS,unsupported=0,exact=exact,exact_inside=inside,exact_outside=outside,
        exact_mask=mask,conservative=conservative,prototype_failures=0,one_dip=per_schedule[0],two_dips=per_schedule[1],
        reverse=per_schedule[2],region_pixels=pixels),'corpus summary consistent with the rows'
    straddle=[fields(x) for x in lines if x.startswith('STEP_A_STRADDLE ')]
    assert len(straddle)==1 and int(straddle[0]['witness_fired'])==1 and int(straddle[0]['red_outside'])>0,'M-outside-union witness must fire'
    assert int(straddle[0]['inside_diff'])==0 and int(straddle[0]['outside_diff'])==0,'packed law inside the injected rectangle, A outside'
    sequences=[fields(x) for x in lines if x.startswith('STEP_A_SEQUENCE ')]
    assert [x['order'] for x in sequences]==['fade_packed','packed_fade']
    for x in sequences:
        fl,ft,fr,fb=rect(x['fade_rect']);pl,pt,pr,pb=rect(x['packed_rect']);assert pl<fr and pt<fb and fl<pr and ft<pb,'overlapping rectangles'
        assert int(x['fade_diff'])==0 and finite(x['fade_max_fraction'])<=1,'fade stage against the CPU oracle'
        assert int(x['packed_inside_diff'])==0 and int(x['packed_outside_diff'])==0,'packed stage bit-exact against the prototype on the intermediate'
    alias=[fields(x) for x in lines if x.startswith('STEP_A_ALIAS ')]
    assert len(alias)==1 and {k:int(v) for k,v in alias[0].items()}==dict(exchange=1,c_diff=0,m_diff=0),'E/C plane alias leaks into the emission exchange'
    ladder=[fields(x) for x in lines if x.startswith('STEP_A_FAILURE ')]
    assert len(ladder)==len(LADDER)
    for row,(label,prepared,first,recovery,coverage,blocked) in zip(ladder,LADDER):
        assert row['label']==label and int(row['native'])==1 and int(row['exchange'])==0 and int(row['exact_a'])==1,(label,row)
        assert int(row['prepared'])==prepared and int(row['first'],16)==first,(label,'chronological first HRESULT',row)
        assert int(row['recovery'],16)==recovery and int(row['coverage'])==coverage and int(row['blocked'])==blocked,(label,row)
    assert lines.count('STEP_A_CAPS refused=4')==1,'three-target, INVSRCALPHA and mask capability refusals'
    reset=[fields(x) for x in lines if x.startswith('STEP_A_RESET ')]
    assert len(reset)==1 and {k:int(v) for k,v in reset[0].items()}==dict(interrupted=1,detached=1,post_reset_inside_diff=0,post_reset_outside_diff=0)
    assert lines.count('PACKED_RESET passed=1')==1
    complete=[fields(x) for x in lines if x.startswith('STEP_A_COMPLETE ')]
    assert len(complete)==1 and {k:int(v) for k,v in complete[0].items()}==dict(policy=8,pairs=9,cases=20,schedules=3,owned_targets=5,target_bytes=5*32*32*8,live_publication=0)
    witnesses=[fields(x) for x in lines if x.startswith('STEP_A_WITNESS ')]
    assert len(witnesses)==int(exact<ROWS)
    return dict(completed=True,bit_exact=exact==ROWS,rows=ROWS,exact=exact,exact_inside=inside,exact_outside=outside,exact_mask=mask,
                conservative_rectangles=conservative,prototype_rows_passed=proto,one_dip=per_schedule[0],two_dips=per_schedule[1],reverse=per_schedule[2],
                region_pixels=pixels,surviving=surviving,overlap=overlap,straddle=straddle[0],sequences=sequences,alias=alias[0],
                ladder=[{k:(v if k in ('label','first','recovery') else int(v)) for k,v in row.items()} for row in ladder],
                capability_refusals=4,reset=reset[0],witnesses=witnesses,owned_targets=5,target_bytes=5*32*32*8,live_publication=False)

def validate_timing(output):
    lines=output.splitlines();assert not any(x.startswith(('STEP_A_ABORT','PACKED_ABORT','SM1_API')) for x in lines),'timing aborted'
    rows=[fields(x) for x in lines if x.startswith('STEP_A_TIMING ')]
    assert 'STEP_A_TIMING_RESULT sizes=2 rects=2 policies=2 dips=3 iterations=8' in lines
    groups={};iterations={}
    for r in rows:
        key=(int(r['width']),int(r['height']),int(r['area']),r['policy'],int(r['dips']))
        l,t,rr,b=rect(r['rect']);assert (rr-l)*(b-t)==key[2]
        groups.setdefault(key,[]).append(finite(r['completed_ms']));iterations.setdefault(key,[]).append(int(r['iteration']))
    expected=[(w,h,a,p,d) for w,h in TIMING_SIZES for a in TIMING_AREAS for p in TIMING_POLICIES for d in TIMING_DIPS]
    assert sorted(groups)==sorted(expected) and all(v==list(range(TIMING_ITERATIONS)) for v in iterations.values()),'timing windows'
    timing=[dict(width=w,height=h,area=a,policy=p,dips=d,median_ms=statistics.median(v),min_ms=min(v),max_ms=max(v)) for (w,h,a,p,d),v in sorted(groups.items())]
    cost=[]
    for w,h in TIMING_SIZES:
        for a in TIMING_AREAS:
            native=statistics.median(groups[w,h,a,'native',16]);bracket=statistics.median(groups[w,h,a,'packed',16])
            cost.append(dict(width=w,height=h,area=a,dips=16,native_ms=native,packed_ms=bracket,per_bracket_ms=(bracket-native)/16))
    return dict(timing=timing,per_bracket=cost,
                scope='Paired EVENT-fenced windows of the native source alone and prepare/source/finish per frame on the detached device; M clear outside the window; not game FPS')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--rect',default='8,8,24,24',help='Injected straddling rectangle l,t,r,b for the witness case')
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'screen-emission-gpu-step-a.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','X3 bottle required';assert not game_running(),'game running'
    inputs=[args.fixture.resolve()]+[args.programs.resolve()/name for name in packed.PROGRAMS]
    assert all(p.is_file() for p in inputs),'prebuilt fixture and local originals required'
    hashes={str(p):sha(p) for p in inputs};source_hashes={name:sha(ROOT/name) for name in SOURCES}
    raw=Path(tempfile.mkdtemp(prefix='x3-screen-emission-step-a-'))
    report=dict(completed=False,raw=str(raw),bottle=bottle.describe(),inputs=hashes,source_sha256=source_hashes,game_launched=False,
                scope='Step A: PackedScreenInPlace (policy 8) of the production pass inside the in-place region bracket, detached; no runtime admission or route',
                limitations=['Native Windows untested; documented D3D9 only.','No bound (step B) and no admission/route change (step C).',
                             'Bit-exactness is against the qualified packed prototype on this X3 backend; timings are detached diagnostics.'])
    try:
        results={}
        for name in ('functional','timing'):
            assert not game_running(),'game running';work=raw/name;work.mkdir();shutil.copy2(inputs[0],work/'fixture.exe')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')};env['WINEDLLOVERRIDES']='d3d9=b'
            command=[bottle.WINE,*bottle.wine_args(),'--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(args.programs.resolve())]
            command.append('timing' if name=='timing' else '--rect='+args.rect)
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as err:child=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=1800)
            assert child.returncode==0,f'{name}: exit {child.returncode}; {work}'
            measured=validate((work/'stdout.txt').read_text()) if name=='functional' else validate_timing((work/'stdout.txt').read_text())
            measured['seconds']=time.monotonic()-start;results[name]=measured
        assert hashes=={str(p):sha(p) for p in inputs},'prebuilt inputs changed'
        assert source_hashes=={name:sha(ROOT/name) for name in SOURCES},'frozen qualification source changed'
        report.update(results);report['completed']=True
    finally:
        target=args.result if report['completed'] else raw/'failed-result.json';target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(completed=True,bit_exact=results['functional']['bit_exact'],exact=results['functional']['exact'],rows=ROWS,
                          per_bracket=results['timing']['per_bracket'])))

if __name__=='__main__':main()
