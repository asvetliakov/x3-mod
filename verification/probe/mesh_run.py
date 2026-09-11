#!/usr/bin/env python3
"""Run the original x86 mesh fixture only; preserve exact DLL identity and evidence."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    processes=subprocess.run(['ps','-axo','pid=,comm='],capture_output=True,text=True,check=True).stdout
    if any(re.search(r'(^|[\\/])X3AP\.exe(?:\s|$)',line,re.I) for line in processes.splitlines()):
        raise SystemExit('Refusing mesh fixture while X3AP is running')
    root=Path(__file__).resolve().parents[2]
    exe=root/'verification/probe/build/mesh_preparation.exe'
    source=root/'verification/probe/mesh_preparation.cpp'
    dll=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
    expected=digest(dll) # Recorded fixture identity, never a version allowlist.
    results=root/'verification/results'
    command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
             '--bottle','Steam','--no-update','--workdir',str(exe.parent),str(exe),r'C:\X3\d3dx9_37.dll',expected]
    env=os.environ.copy();env['WINEDLLOVERRIDES']='d3d9=b'
    metadata=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),command=command,
                  source_sha256=digest(source),executable_sha256=digest(exe),d3dx_sha256=expected,
                  process_local_override='d3d9=b',timeout_seconds=90)
    with (results/'mesh-preparation.txt').open('w') as out,(results/'mesh-preparation-wine.log').open('w') as err:
        try:
            run=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=90)
            metadata['exit_code']=run.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None,timed_out=True)
    text=(results/'mesh-preparation.txt').read_text()
    groups={}
    for line in text.splitlines():
        kind=line.split(' ',1)[0]
        groups.setdefault(kind,[]).append(dict(re.findall(r'(\w+)=([^\s]+)',line)))
    metadata['results']=groups
    metadata['passed']=(metadata['exit_code']==0 and 'RESULT PASS' in text
                        and len(groups.get('CASE',[]))==5 and len(groups.get('INVALIDATION',[]))==7
                        and all(c.get('exact_parity')=='1' for c in groups.get('CASE',[]))
                        and all(c.get('miss')=='1' and c.get('exact_parity')=='1' for c in groups.get('INVALIDATION',[]))
                        and len(groups.get('TIMING',[]))==1 and len(groups.get('SEQUENCE_TIMING',[]))==1
                        and len(groups.get('BOUND',[]))==1)
    metadata['limits']=['Standalone original meshes; no game loading improvement demonstrated.',
                        'Cache stores adjacency only; cleaning/optimization still execute on each current mesh.',
                        'Cold timing is repeated uncached computation with warmed DLL/mesh; not cold OS/disk startup.',
                        'Reuse timing includes serialization, full-key comparison and adjacency output allocation/copy.',
                        'Retained 2 MiB budget accounts vector capacities and entry structs; allocator bookkeeping is excluded.',
                        'Caller mesh and bounded transient key/output/candidate allocations are outside the retained budget.',
                        'Full-sequence timing includes mesh creation and verification snapshots in both paths.']
    (results/'mesh-preparation-summary.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata,indent=2));return 0 if metadata['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
