#!/usr/bin/env python3
"""Proxy-loaded qualification of the locked-prefix bullet bound (step B of
docs/architecture/screen-emission-region.md, step D of
docs/architecture/screen-emission-bullet-bound.md): the built d3d9.dll beside
locked_prefix_live_fixture.exe, X3M_OWNERSHIP=1 X3M_MOTION_OUTPUT=1
X3M_SCREEN_EMISSION_BOUND=1 X3M_LOCKED_PREFIX_LOG=1, the game's bullet VS/PS
from the shader sweep. The proxy's capture log must carry one locked_prefix
line per fixture frame with the scripted lookup outcome, the hull rectangle
covering the footprint the fixture read back from the GPU, and one
locked_prefix_frame line with the sentinel/scan counters and the per-draw
derivation cost. Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import bottle
from game_guard import game_running

ROOT=Path(__file__).resolve().parents[2]
PROGRAMS=('vs_5e484a06672e28fb.bin','ps_0a523f33ac47ae05.bin')
CAPTURE_FRAMES=8 # the proxy's capture window cap; X3M_LOCKED_PREFIX_LOG=1 prints the per-draw line on every frame regardless
WINDOW=6144      # vertices of the whole 147456-byte buffer
# Fixture frame script (locked_prefix_live_fixture.cpp): label, vertices,
# bound flag, lookup name, vertices the Unlock scan published (0: none),
# locks the frame performs on the marked buffer (recorded), viewport.
FRAMES=(('first_draw_unknown',102,0,'unknown',0,0,(96,96)),('bound',102,1,'bound',WINDOW,1,(96,96)),
        ('near_straddle',96,1,'bound',WINDOW,1,(96,96)),('near_exact',96,1,'bound',WINDOW,1,(96,96)),('near_behind',96,0,'bound',WINDOW,1,(96,96)),('near_beam',96,1,'bound',WINDOW,1,(96,96)),
        ('nan_tail_96',96,1,'bound',WINDOW,1,(96,96)),('nan_tail_102',102,1,'bound',WINDOW,1,(96,96)),('prefix_nan_refused',102,0,'nonfinite',WINDOW,1,(96,96)),
        ('nested_lock_invalid',102,0,'invalid',0,2,(96,96)),('instanced_refused',102,0,'bound',WINDOW,1,(96,96)),('bound_after_instanced',102,1,'bound',WINDOW,1,(96,96)),
        ('full_buffer',6144,1,'bound',WINDOW,1,(96,96)),('no_relock_same_revision',6144,1,'bound',WINDOW,0,(96,96)),
        ('fan_176',1056,1,'bound',1056,1,(320,192)),('straddle_72',432,1,'bound',432,1,(320,192)),('big_605_origin_tail',3630,1,'bound',WINDOW,1,(320,192)),('small_27',162,1,'bound',162,1,(320,192)),
        ('after_reset_unknown',102,0,'unknown',0,0,(96,96)),('after_reset_bound',102,1,'bound',WINDOW,1,(96,96)))
# Near-plane cases (screen-emission-region.md, step B; rows x' = x, y' = y,
# z' = .1 (z - 1), w = z on the 96x96 viewport): reason, vertices cut by the
# near plane (16 copies of the six vertices), a pixel footprint the
# rasteriser touches (the rectangle must contain it; the fixture's readback
# is checked on top) and the largest admissible area fraction in per mille.
NEAR={'near_straddle':dict(reason=0,clipped=16,footprint=(48,30,84,36),max_permille=999),
      'near_exact':dict(reason=0,clipped=0,footprint=(48,36,72,60),max_permille=999),
      'near_behind':dict(reason=7,clipped=96,footprint=None,max_permille=0),
      'near_beam':dict(reason=0,clipped=0,footprint=(48,36,72,60),max_permille=500)}
# Step D geometry cases (screen-emission-bullet-bound.md section 1 through
# the fixture's diagonal world frame): the hull rectangle's largest
# admissible fraction of the 320x192 viewport, the AABB rectangle's smallest
# (the step-B route on the same vertices), whether vertices are cut by the
# near plane, and the bolts (for the per-draw cost report).
HULL={'fan_176':dict(max_permille=260,aabb_min_permille=580,clipped=False,bolts=176),
      'straddle_72':dict(max_permille=260,aabb_min_permille=580,clipped=True,bolts=72),
      'big_605_origin_tail':dict(max_permille=260,aabb_min_permille=580,clipped=False,bolts=605),
      'small_27':dict(max_permille=260,aabb_min_permille=580,clipped=False,bolts=27)}

def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def rect_of(text):return tuple(int(v) for v in text.split(','))
def covers(rect,inner):return rect[0]<=inner[0] and rect[1]<=inner[1] and rect[2]>=inner[2] and rect[3]>=inner[3]

def expected_counters():
    """Recorded locks, scans, scanned vertices, sentinel bytes and window-end
    scans over the script: the sentinel covers the previous published prefix
    (the whole window after a mark), a nested lock pair records two locks and
    publishes nothing, Reset clears the table (the new buffer is marked at
    its first draw, after its first lock)."""
    locks=scans=scanned=sentinel=window_end=0;prev=WINDOW
    for label,_,_,lookup,published,frame_locks,_ in FRAMES:
        if label.startswith('after_reset_unknown'):prev=WINDOW
        if not frame_locks:continue
        locks+=frame_locks
        sentinel+=min(WINDOW,prev)*24 # the outer lock of a nested pair is sentinelled; the inner one is not
        if published:
            scans+=1;scanned+=published;prev=published
            if published==WINDOW:window_end+=1
    return dict(locks=locks,scans=scans,scanned_vertices=scanned,sentinel_bytes=sentinel,window_end_scans=window_end)

def validate(output,trace):
    frames=[fields(l) for l in output.splitlines() if l.startswith('FRAME ')]
    assert [f['label'] for f in frames]==[f[0] for f in FRAMES] and re.search(r'^RESULT PASS frames=%d device_refs=0$'%len(FRAMES),output,re.M),'fixture frame script'
    assert all(int(f['draw'],16)==0 for f in frames if f['label']!='instanced_refused'),'every non-instanced draw succeeded'
    rows=[fields(l) for l in trace.splitlines() if 'locked_prefix device=' in l]
    summaries=[fields(l) for l in trace.splitlines() if 'locked_prefix_frame device=' in l]
    assert len(rows)==len(FRAMES),('one locked_prefix line per draw (X3M_LOCKED_PREFIX_LOG=1)',len(rows))
    assert len(summaries)==len(FRAMES),('one locked_prefix_frame line per frame',len(summaries))
    assert [int(s['frame']) for s in summaries]==list(range(len(FRAMES))) and [int(r['frame']) for r in rows]==list(range(len(FRAMES))),'frame numbering'
    revisions=[];fractions=[];near_cases={};hull_cases={};derive_us={};rechecks=0
    for index,((label,vertices,bound,lookup,published,_,viewport),summary,row,frame) in enumerate(zip(FRAMES,summaries,rows,frames)):
        assert int(summary['draws'])==1 and int(summary['bound'])==bound and int(summary['refused'])==1-bound,(label,summary)
        assert int(summary['instanced'])==int(label=='instanced_refused'),(label,'instanced counter',summary)
        rechecks+=int(summary['rechecks'])
        near=NEAR.get(label);hull=HULL.get(label)
        expect_clipped=bool(bound and ((near and near['clipped']) or (hull and hull['clipped'])))
        assert int(summary['clipped'])==int(expect_clipped),(label,'clipped counter',summary)
        assert int(summary['reason_near'])==int(bool(near and near['reason']==7)),(label,'reason_near counter',summary)
        refusals=sum(int(summary['lookup_'+name]) for name in ('unknown','pending','invalid','empty','beyond','nonfinite'))
        assert refusals==(0 if lookup=='bound' else 1) and (lookup=='bound' or int(summary['lookup_'+lookup])==1),(label,'lookup counters',summary)
        assert int(summary['table_used'])==1,(label,'one marked buffer',summary)
        assert (int(frame['viewport'].split(',')[0]),int(frame['viewport'].split(',')[1]))==viewport,(label,'viewport',frame)
        assert int(row['vertices'])==vertices and int(row['bound'])==bound and row['lookup']==lookup,(label,row)
        assert int(row['scanned'])==published,(label,'published scan count (sentinel-exact or the whole window)',row)
        l,t,r,b=rect_of(row['rect']);vw,vh=viewport
        hull_px,aabb_px=int(row['hull_px']),int(row['aabb_px'])
        footprint=rect_of(frame['footprint']);covered=int(frame['covered'])
        if bound:
            assert 0<=l<r<=vw and 0<=t<b<=vh and int(row['reason'])==0 and int(row['f_permille'])<1000,(label,'rectangle',row)
            assert hull_px==(r-l)*(b-t) and int(summary['hull_px'])==hull_px and int(summary['aabb_px'])==aabb_px and 0<hull_px<=aabb_px,(label,'hull/aabb pixels',row,summary)
            assert covered>0 and covers((l,t,r,b),footprint),(label,'the hull rectangle covers the GPU footprint',(l,t,r,b),footprint,covered)
            fractions.append(int(row['f_permille'])/1000)
        else:
            assert (l,t,r,b)==(0,0,0,0) and hull_px==0 and aabb_px==0,(label,'refused draws have no rectangle',row)
            if int(row['reason'])==7:assert covered==0,(label,'a batch behind the near plane rasterises nothing',frame)
        if near:
            assert int(row['reason'])==near['reason'] and int(row['clipped'])==near['clipped'],(label,'near-plane reason/clipped',row)
            fp=near['footprint']
            if fp:assert covers((l,t,r,b),fp),(label,'the clipped rectangle covers the visible footprint',row,fp)
            assert int(row['f_permille'])<=near['max_permille'],(label,'area fraction',row)
            near_cases[label]=dict(reason=int(row['reason']),clipped=int(row['clipped']),rect=(l,t,r,b),f_permille=int(row['f_permille']),footprint=fp,gpu_footprint=footprint,covered=covered)
        elif hull:
            permille=int(row['f_permille']);aabb_permille=aabb_px*1000//(vw*vh)
            assert permille<=hull['max_permille'],(label,'hull rectangle fraction',permille,hull)
            assert aabb_permille>=hull['aabb_min_permille'],(label,'the AABB rectangle of the same vertices stays large',aabb_permille,hull)
            assert (int(row['clipped'])>0)==hull['clipped'],(label,'near-plane cut',row)
            us=float(summary['derive_us']);derive_us[hull['bolts']]=us
            assert us>0 and int(row['ticks'])>0,(label,'per-draw derivation cost recorded (X3M_TELEMETRY_DRAW=1)',summary)
            hull_cases[label]=dict(bolts=hull['bolts'],vertices=vertices,rect=(l,t,r,b),hull_px=hull_px,aabb_px=aabb_px,hull_permille=permille,aabb_permille=aabb_permille,
                                   clipped=int(row['clipped']),pad=int(row['pad']),scanned=published,gpu_footprint=footprint,covered=covered,derive_us=us,ticks=int(row['ticks']))
        elif int(row['clipped'])!=0:raise AssertionError((label,'only near-plane cases are clipped',row))
        revisions.append(int(row['rev']))
    # The first lock of the buffer precedes its mark (unrecorded, frame 0);
    # the nested frame locks twice (invalid); the instanced frame's lookup is
    # bound but the draw is refused; no_relock keeps the revision.
    assert revisions==[0,1,2,3,4,5,6,7,8,10,11,12,13,13,14,15,16,17,0,1],('revisions',revisions)
    assert set(near_cases)==set(NEAR) and set(hull_cases)==set(HULL),('every near-plane and hull case captured',sorted(near_cases),sorted(hull_cases))
    assert rechecks==0,('no record changed under a projection',rechecks)
    last=summaries[-1];expect=expected_counters()
    assert int(last['marks'])==2,('marks',last)
    for key,value in expect.items():assert int(last[key])==value,(key,'expected',value,'logged',last[key])
    scan_us=float(last['scan_us'])
    return dict(frames=len(summaries),captured_draw_lines=len(rows),bound_frames=sum(f[2] for f in FRAMES),refused_frames=sum(1-f[2] for f in FRAMES),
                lookups={name:sum(1 for f in FRAMES if f[3]==name) for name in ('unknown','bound','nonfinite','invalid')},
                instanced_refusals=1,marks=int(last['marks']),scans=int(last['scans']),locks=int(last['locks']),scanned_vertices=int(last['scanned_vertices']),
                sentinel_bytes=int(last['sentinel_bytes']),window_end_scans=int(last['window_end_scans']),
                scan_us_total=scan_us,scan_us_per_scan=scan_us/int(last['scans']),rect_fractions=fractions,near_plane=near_cases,hull=hull_cases,
                derive_us_per_draw={str(k):v for k,v in sorted(derive_us.items())},
                scope='Proxy DLL path: MotionOutput::derive_prefix_region (step D vertex hull) and the ownership sentinel/Unlock scan executed under the game bullet VS/PS; GPU footprint read back by the fixture; no composition')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'screen-emission-bound-live1.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','Set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(),'Game running; no fixture launch'
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/name for name in PROGRAMS]]
    assert all(p.is_file() for p in inputs),'prebuilt fixture, DLL and the bullet programs required'
    hashes={str(p):sha(p) for p in inputs}
    work=Path(tempfile.mkdtemp(prefix='x3-locked-prefix-live-'))
    shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
    env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
    env.update(X3M_OWNERSHIP='1',X3M_MOTION_OUTPUT='1',X3M_SCREEN_EMISSION_BOUND='1',X3M_LOCKED_PREFIX_LOG='1',X3M_TELEMETRY_DRAW='1',X3M_STATE_SHADOW='1',X3M_SCENE_HOOK='0',
               X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES=str(CAPTURE_FRAMES),X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',WINEDLLOVERRIDES='d3d9=n,b')
    command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(inputs[2]),'Z:'+str(inputs[3])]
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,raw=str(work),command=command,input_sha256=hashes,
                environment={k:v for k,v in env.items() if k.startswith('X3M_') or k=='WINEDLLOVERRIDES'})
    start=time.monotonic()
    try:
        with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as err:
            child=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=180)
        result.update(exit_code=child.returncode,wall_seconds=round(time.monotonic()-start,3))
        assert child.returncode==0,f'fixture failed; see {work}'
        logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1,'one proxy session log'
        result.update(validate((work/'stdout.txt').read_text(),logs[0].read_text()))
        assert hashes=={str(p):sha(p) for p in inputs},'inputs changed during the run'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error);raise
    finally:
        target=args.result if result['passed'] else work/'failed-result.json';target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ('passed','frames','scans','scan_us_per_scan','derive_us_per_draw','wall_seconds','error') if k in result}))

if __name__=='__main__':main()
