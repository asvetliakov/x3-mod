#!/usr/bin/env python3
"""Focused scene-hook integration, consuming an existing SEH build package.

Runtime: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
         python3 verification/probe/run_scene_compositor.py --bridge-dir <dir>
--build-only never executes Wine. No production DLL rebuild or game launch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/verification/scene-compositor'
FLAGS=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse',
       '-mstackrealign','-mincoming-stack-boundary=2','-fno-exceptions',
       '-DX3M_MOTION_OUTPUT_FIXTURE']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge-dir',type=Path,required=True)
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args()
    BUILD.mkdir(parents=True,exist_ok=True)
    objects=[]
    sources=['src/proxy/scene_hook.cpp','src/proxy/engine_patch.cpp',
             'verification/probe/scene_compositor_fixture.cpp']
    for source in sources:
        obj=BUILD/(Path(source).stem+'.o'); objects.append(obj)
        subprocess.run(['i686-w64-mingw32-g++',*FLAGS,'-c',str(ROOT/source),'-o',str(obj)],check=True)
    for name in ('compositor_bridge.o','compositor_bridge_seh_gnu.obj','libx3m_compositor_seh_runtime.a'):
        obj=args.bridge_dir.resolve()/name
        if not obj.is_file(): raise RuntimeError('Missing existing bridge artifact: '+str(obj))
        objects.append(obj)
    undefined=subprocess.check_output(['i686-w64-mingw32-nm','-u',str(objects[0])],text=True)
    if 'SjLj' in undefined: raise RuntimeError('Unexpected SJLJ CPU-boundary bookends')
    binary=BUILD/'scene_compositor_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++',*map(str,objects),'-static','-static-libgcc',
                    '-static-libstdc++','-o',str(binary)],check=True)
    report={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'scene_signal_no_sjlj':True,'native_windows_verified':False}
    if args.build_only:
        print(json.dumps(report)); return
    import bottle
    from game_guard import game_running
    if game_running(): raise RuntimeError('Game running; refusing fixture')
    result=subprocess.run([bottle.WINE,*bottle.wine_args(),str(binary)],
                          capture_output=True,text=True,env=dict(os.environ,WINEDEBUG='-all'),timeout=60)
    (BUILD/'run.log').write_text(result.stdout+'\nSTDERR\n'+result.stderr)
    records=re.findall(r'^scene_compositor checks=(\d+) failures=(\d+)\r?$',result.stdout,re.M)
    passed=result.returncode==0 and records==[('43','0')] and not re.search(r'^FAIL ',result.stdout,re.M)
    report.update(bottle=bottle.describe(),passed=passed,records=records,returncode=result.returncode)
    (bottle.results_dir(ROOT)/'scene-compositor-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(result.stdout,end=''); print('passed='+str(passed))
    if not passed: raise SystemExit(1)

if __name__=='__main__': main()
