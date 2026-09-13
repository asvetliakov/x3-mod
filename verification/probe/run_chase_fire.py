#!/usr/bin/env python3
"""Focused chase-fire branch fixture. --build-only never invokes Wine.

Runtime: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
         --holder chase-fire --timeout 1 python3 verification/probe/run_chase_fire.py
Builds only this fixture/audit objects; never the production DLL or game.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
from run_chase_aim_trace import FLAGS, audit_object, audit_fixture_image
from verify_chase_fire_site import verify as verify_site
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/chase-fire'
MODES=('complete','off','bad','late')
# Actual fixture translation units and their project-local includes. Candidate
# DLL/CMake/camera implementation provenance belongs to the candidate build.
SOURCES=('src/proxy/chase_fire.cpp','src/proxy/chase_fire.h',
         'src/proxy/chase_aim_trace.cpp','src/proxy/chase_aim_trace.h',
         'src/proxy/chase_camera.h','src/proxy/engine_patch.cpp','src/proxy/engine_patch.h',
         'src/proxy/engine_memory.cpp','src/proxy/engine_memory.h','src/proxy/cpu_state.h',
         'src/proxy/capture.h','src/proxy/object_trace.h','src/proxy/telemetry.h',
         'verification/probe/chase_fire_fixture.cpp')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def build():
    BUILD.mkdir(parents=True,exist_ok=True)
    compiler='i686-w64-mingw32-g++';objects=[]
    for source,stem in (('src/proxy/chase_fire.cpp','audit'),('verification/probe/chase_fire_fixture.cpp','fixture'),
                        ('src/proxy/engine_patch.cpp','patch'),('src/proxy/engine_memory.cpp','memory')):
        obj=BUILD/(stem+'.o');flags=list(FLAGS)
        if stem=='memory':flags+=['-mno-sse','-mno-mmx','-mfpmath=387']
        subprocess.run([compiler,*flags,'-c',str(ROOT/source),'-o',str(obj)],check=True,cwd=ROOT)
        if stem!='audit':objects.append(obj)
    audit=audit_object(BUILD/'audit.o','x3m_chase_fire_enter')
    binary=BUILD/'chase_fire_fixture.exe'
    subprocess.run([compiler,*map(str,objects),'-static','-static-libgcc','-static-libstdc++',
                    '-Wl,--image-base,0x00400000','-Wl,--disable-dynamicbase',
                    '-Wl,--section-start,.x3map=0x00401000','-Wl,--section-start,.text=0x00630000',
                    '-o',str(binary)],check=True,cwd=ROOT)
    image=audit_fixture_image(binary)
    site=verify_site()
    if not site['passed']:raise RuntimeError('installed executable cursor branch refused')
    return binary,dict(cpu=audit,image=image,site=site)
def parse(output,mode):
    match=re.search(r'^CHASE FIRE RESULT mode=(\w+) checks=(\d+) failures=(\d+)\s*$',output,re.M)
    rows=re.findall(r'^CHECK (.+) (PASS|FAIL)\r?$',output,re.M)
    if not match or match[1]!=mode or int(match[3]) or len(rows)!=int(match[2]) or any(v!='PASS' for _,v in rows):
        raise RuntimeError('incomplete/failing fixture checks: '+mode)
    return dict(mode=mode,checks=len(rows),failures=0)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-only',action='store_true');args=ap.parse_args()
    binary,audit=build()
    sources={name:sha(ROOT/name) for name in SOURCES};binary_sha=sha(binary)
    if args.build_only:
        print(json.dumps(dict(built=True,runtime_verified=False,site=audit['site'],cpu=audit['cpu'],binary_sha256=binary_sha),sort_keys=True));return
    import bottle
    from game_guard import game_running
    if bottle.BOTTLE!='X3' or game_running():raise RuntimeError('X3-only fixture requires stopped game')
    results=bottle.results_dir(ROOT);modes=[]
    for mode in MODES:
        if game_running():raise RuntimeError('game started before fixture '+mode)
        run=subprocess.run([bottle.WINE,*bottle.wine_args(),'--workdir',str(BUILD),str(binary),mode],
                           capture_output=True,text=True,timeout=60,env=dict(os.environ,X3M_CAMERA='vanilla',X3M_TELEMETRY='0'))
        (results/f'chase-fire-{mode}.txt').write_text(run.stdout)
        (results/f'chase-fire-{mode}-wine.log').write_text(run.stderr)
        if run.returncode:raise RuntimeError(f'{mode} exited {run.returncode}; retained fixture output')
        modes.append(parse(run.stdout,mode))
    if sources!={name:sha(ROOT/name) for name in SOURCES} or binary_sha!=sha(binary):raise RuntimeError('source/binary changed during fixture run')
    summary=dict(passed=True,runtime_verified=True,game_launched=False,bottle=bottle.describe(),modes=modes,
                 source_and_binary_stable=True,sources=sources,binary_sha256=binary_sha,audit=audit)
    path=results/'chase-fire-summary.json';path.write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
    print(json.dumps(dict(passed=True,cases=len(modes),checks=sum(m['checks'] for m in modes),summary=str(path))))
if __name__=='__main__':main()
