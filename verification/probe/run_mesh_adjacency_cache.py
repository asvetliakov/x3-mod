#!/usr/bin/env python3
"""Fresh-build and run detached production cache against the exact native DLL."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
DLL=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
EXPECTED='c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8'
FILES=['src/proxy/mesh_adjacency_cache.h','src/proxy/mesh_adjacency_cache.cpp',
       'verification/probe/mesh_adjacency_cache_fixture.cpp','verification/probe/mesh_preparation.cpp',
       'verification/probe/build_mesh_adjacency_cache.sh','verification/probe/run_mesh_adjacency_cache.py',
       'verification/probe/build/mesh_adjacency_cache_fixture.exe']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes(include_executable=True):
    names=FILES if include_executable else FILES[:-1]
    return {p:digest(ROOT/p) for p in names}|{'native_d3dx9_37.dll':digest(DLL)}
def main():
    results=ROOT/'verification/results';summary=results/'mesh-adjacency-cache-summary.json'
    meta=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=False,phase='building',fresh_build=False,timeout_seconds=90)
    def save():summary.write_text(json.dumps(meta,indent=2)+'\n')
    save() # Invalidate any previous PASS before reading inputs or invoking the compiler.
    try:
        meta['source_hashes_before_build']=hashes(False);save()
        if meta['source_hashes_before_build']['native_d3dx9_37.dll']!=EXPECTED:
            raise RuntimeError('Native runtime differs from verified binary')
        subprocess.run(['sh','verification/probe/build_mesh_adjacency_cache.sh'],cwd=ROOT,check=True,timeout=60)
        meta['source_hashes_after_build']=hashes(False)
        if meta['source_hashes_before_build']!=meta['source_hashes_after_build']:
            raise RuntimeError('Sources or native DLL changed during build; refusing stale evidence')
        before=hashes();exe=ROOT/FILES[-1]
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--workdir',str(exe.parent),str(exe),r'C:\X3\d3dx9_37.dll',EXPECTED]
        meta.update(fresh_build=True,phase='running',command=command,hashes_before=before);save()
        env=os.environ.copy();env['WINEDLLOVERRIDES']='d3d9=b'
        report=results/'mesh-adjacency-cache.txt';wine=results/'mesh-adjacency-cache-wine.log'
        with report.open('wb') as out,wine.open('wb') as err:
            run=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=90)
        meta['exit_code']=run.returncode;meta['hashes_after']=hashes();meta['report_sha256']=digest(report)
        groups={}
        for line in report.read_text().splitlines():
            kind=line.split(' ',1)[0];groups.setdefault(kind,[]).append(dict(re.findall(r'(\w+)=([^\s]+)',line)))
        meta['results']=groups
        terminal=report.read_text().rstrip().splitlines()[-1]
        meta['passed']=run.returncode==0 and bool(re.fullmatch(r'RESULT PASS checks=\d+',terminal)) and len(groups.get('RESULT',[]))==1 and before==meta['hashes_after'] and len(groups.get('CASE',[]))==5 and len(groups.get('FP',[]))==1 and len(groups.get('TIMING',[]))==1
        meta['phase']='complete'
        meta['limits']=[
            'Detached core; no hooks, installation or game loading improvement.',
            'Caller must verify/pin runtime and serialize mesh/output mutation.',
            'SYSTEMMEM non-writeonly/non-shared meshes only; dynamic additionally needs positive exact readonly runtime contract.',
            'Default masked FP computational controls only; unsupported controls forward unchanged.',
            'FP instruction/data pointers and allocator side effects are not emulated.',
            'Persistent acquisition unlock failure has distinct origin, permanently disables cache and does not call native.',
            'Retained budget includes fixed metadata and HeapSize payload, excludes allocator bookkeeping and caller memory.',
            'Synthetic alternating warm-runtime timing; output vector creation is common fixture overhead; no game hit-rate estimate.'
        ]
    except (Exception,KeyboardInterrupt) as error:
        meta.update(passed=False,phase='failed',error=repr(error))
        if isinstance(error,subprocess.TimeoutExpired):meta['timed_out']=True
    save();print(json.dumps(meta,indent=2));return 0 if meta['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
