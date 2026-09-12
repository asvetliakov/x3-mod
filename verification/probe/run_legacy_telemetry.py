#!/usr/bin/env python3
"""Re-run existing D3D9 semantics fixtures against the integrated telemetry DLL."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
root=Path(__file__).resolve().parents[2]
results=bottle.results_dir(root);probe=root/'verification/probe/build'
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
manifest={'bottle':bottle.describe(),'dll_sha256':hashlib.sha256((root/'build/d3d9.dll').read_bytes()).hexdigest(),'fixtures':{}}
for name,exe,frames in [('d3d9-smoke-v3','d3d9_smoke.exe','1'),('capture-state-v3','capture_state_fixture.exe','5')]:
    directory=probe/(name+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    directory.mkdir(parents=True)
    shutil.copy(probe/exe,directory);shutil.copy(root/'build/d3d9.dll',directory)
    env=dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0',X3M_TELEMETRY='1',X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES=frames)
    command=[wine,'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=n,b','--workdir',str(directory),str(directory/exe)]
    with (results/(name+'.txt')).open('w') as stdout,(results/(name+'-wine.log')).open('w') as stderr:
        result=subprocess.run(command,env=env,stdout=stdout,stderr=stderr,timeout=90)
    print(f'{name}: exit={result.returncode}',flush=True)
    if result.returncode:raise SystemExit(result.returncode)
    traces=list((directory/'x3-modern-captures').glob('session-*.log'))
    trace=max(traces,key=lambda p:p.stat().st_mtime)
    shutil.copy(trace,results/(name+'-capture.log'))
    manifest['fixtures'][name]={'exe_sha256':hashlib.sha256((probe/exe).read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(trace.read_bytes()).hexdigest(),'exit':result.returncode}
(results/'telemetry-build-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
