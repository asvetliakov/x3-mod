#!/usr/bin/env python3
"""Proxy-loaded qualification of the step B locked-prefix bound
(docs/architecture/screen-emission-region.md): the built d3d9.dll beside
locked_prefix_live_fixture.exe, X3M_OWNERSHIP=1 X3M_MOTION_OUTPUT=1
X3M_SCREEN_EMISSION_BOUND=1, the game's bullet VS/PS from the shader sweep.
The proxy's capture log must carry one locked_prefix line per fixture frame
with the scripted lookup outcome and one locked_prefix_frame line with the
ownership scan counters. Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3.
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
CAPTURE_FRAMES=8 # the proxy's capture window cap; per-draw lines for frames 1-8
# Fixture frame script (locked_prefix_live_fixture.cpp): label, vertices, bound flag, lookup name.
FRAMES=(('first_draw_unknown',102,0,'unknown'),('bound',102,1,'bound'),('bound',102,1,'bound'),('bound',102,1,'bound'),
        ('bound_exact_checkpoint',96,1,'bound'),('nan_tail_refused',102,0,'nonfinite'),('nested_lock_invalid',102,0,'invalid'),
        ('instanced_refused',102,0,'bound'),('bound_after_instanced',102,1,'bound'),('full_buffer',6144,1,'bound'),
        ('no_relock_same_revision',6144,1,'bound'),('after_reset_unknown',102,0,'unknown'),('after_reset_bound',102,1,'bound'))

def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def validate(output,trace):
    frames=[fields(l) for l in output.splitlines() if l.startswith('FRAME ')]
    assert [f['label'] for f in frames]==[f[0] for f in FRAMES] and re.search(r'^RESULT PASS frames=13 device_refs=0$',output,re.M),'fixture frame script'
    assert all(int(f['draw'],16)==0 for f in frames if f['label']!='instanced_refused'),'every non-instanced draw succeeded'
    # Per-draw lines exist in capture frames (X3M_CAPTURE_START=1 and the
    # X3M_CAPTURE_FRAMES cap of 8: proxy frames 1-8, the fixture's frames
    # 2-9; proxy frame 0 is not capturable); the telemetry frame summary
    # covers all 13 frames.
    rows=[fields(l) for l in trace.splitlines() if 'locked_prefix device=' in l]
    summaries=[fields(l) for l in trace.splitlines() if 'locked_prefix_frame device=' in l]
    assert len(rows)==CAPTURE_FRAMES,('one locked_prefix line per captured draw',len(rows))
    assert len(summaries)==len(FRAMES),('one locked_prefix_frame line per frame',len(summaries))
    assert [int(s['frame']) for s in summaries]==list(range(len(FRAMES))) and [int(r['frame']) for r in rows]==list(range(1,CAPTURE_FRAMES+1)),'frame numbering'
    revisions=[];fractions=[]
    for index,((label,vertices,bound,lookup),summary) in enumerate(zip(FRAMES,summaries)):
        assert int(summary['draws'])==1 and int(summary['bound'])==bound and int(summary['refused'])==1-bound,(label,summary)
        assert int(summary['instanced'])==int(label=='instanced_refused'),(label,'instanced counter',summary)
        refusals=sum(int(summary['lookup_'+name]) for name in ('unknown','pending','invalid','empty','beyond','nonfinite'))
        assert refusals==(0 if lookup=='bound' else 1) and (lookup=='bound' or int(summary['lookup_'+lookup])==1),(label,'lookup counters',summary)
        assert int(summary['table_used'])==1,(label,'one marked buffer',summary)
        if index<1 or index>CAPTURE_FRAMES:continue
        row=rows[index-1]
        assert int(row['vertices'])==vertices and int(row['bound'])==bound and row['lookup']==lookup,(label,row)
        assert int(row['checkpoint'])==((vertices+95)//96-1 if lookup in ('bound','nonfinite') else 0),(label,'checkpoint',row)
        l,t,r,b=(int(v) for v in row['rect'].split(','))
        if bound:
            assert 0<=l<r<=96 and 0<=t<b<=96 and int(row['reason'])==0 and int(row['f_permille'])<1000,(label,'rectangle',row)
            fractions.append(int(row['f_permille'])/1000)
        else:assert (l,t,r,b)==(0,0,0,0),(label,'refused draws have no rectangle',row)
        revisions.append(int(row['rev']))
    # The first lock of the buffer precedes its mark (unrecorded, frame 0);
    # the nested frame locks twice (invalid); the instanced frame's lookup is
    # bound but the draw is refused.
    assert revisions==[1,2,3,4,5,7,8,9],('revisions',revisions)
    last=summaries[-1]
    # 11 recorded locks: 9 published, the nested pair (2, invalid); the two
    # pre-mark locks are not recorded. 9 scans of the whole 6144-vertex window.
    assert int(last['marks'])==2 and int(last['scans'])==9 and int(last['locks'])==11,('marks/scans/locks',last)
    assert int(last['scanned_vertices'])==9*6144,('whole window per scan',last)
    scan_us=float(last['scan_us'])
    return dict(frames=len(summaries),captured_draw_lines=len(rows),bound_frames=sum(f[2] for f in FRAMES),refused_frames=sum(1-f[2] for f in FRAMES),
                lookups={name:sum(1 for f in FRAMES if f[3]==name) for name in ('unknown','bound','nonfinite','invalid')},
                instanced_refusals=1,marks=int(last['marks']),scans=int(last['scans']),locks=int(last['locks']),
                scan_us_total=scan_us,scan_us_per_scan=scan_us/int(last['scans']),rect_fractions=fractions,
                scope='Proxy DLL path: MotionOutput::derive_prefix_region and the ownership Unlock scan executed under the game bullet VS/PS; no composition')

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
    env.update(X3M_OWNERSHIP='1',X3M_MOTION_OUTPUT='1',X3M_SCREEN_EMISSION_BOUND='1',X3M_STATE_SHADOW='1',X3M_SCENE_HOOK='0',
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
        print(json.dumps({k:result[k] for k in ('passed','frames','scans','scan_us_per_scan','wall_seconds','error') if k in result}))

if __name__=='__main__':main()
