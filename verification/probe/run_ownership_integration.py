#!/usr/bin/env python3
"""Test the actual opt-in DLL in isolated Preview directories, never in X3."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
root=Path(__file__).resolve().parents[2];probe=root/'verification/probe/build';results=root/'verification/results'
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
dll=root/'build-ownership/d3d9.dll'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
sources=lambda:{str(path.relative_to(root)):sha(path) for directory in ('src/ownership','src/proxy') for path in sorted((root/directory).glob('*')) if path.suffix in ('.cpp','.h','.def')}
manifest={'dll_sha256':sha(dll),'runtime':wine,'bottle':'Steam','game_launched':False,'sources_at_start':sources(),'cases':{}}
fixtures=[('smoke','d3d9_smoke.exe','1'),('capture','capture_state_fixture.exe','5'),('lifetime','ownership_integration_lifetime.exe','0'),('contracts','ownership_baseline.exe','0'),('auto','ownership_integration_auto_depth.exe','0')]
for mode in ('off','on','depth_only','sample_depth'):
    for name,exe,frames in (fixtures[:1] if mode=='depth_only' else fixtures[-1:] if mode=='sample_depth' else fixtures):
        case=f'ownership-integration-{mode}-{name}'
        directory=probe/(case+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
        assert sha(dll)==manifest['dll_sha256'],'DLL changed during integration run'
        directory.mkdir(parents=True);shutil.copy(probe/exe,directory);shutil.copy(dll,directory/'d3d9.dll')
        env=dict(os.environ,X3M_TELEMETRY='1',X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES=frames,X3M_SAMPLEABLE_DEPTH='1' if mode in ('depth_only','sample_depth') else '0')
        if mode in ('on','sample_depth'):env['X3M_OWNERSHIP']='1'
        else:env.pop('X3M_OWNERSHIP',None)
        command=[wine,'--bottle','Steam','--no-update','--dll','d3d9=n,b','--workdir',str(directory),str(directory/exe)]
        stdout_path=results/(case+'.txt');stderr_path=results/(case+'-wine.log')
        with stdout_path.open('w') as stdout,stderr_path.open('w') as stderr:
            completed=subprocess.run(command,env=env,stdout=stdout,stderr=stderr,timeout=120)
        traces=list((directory/'x3-modern-captures').glob('session-*.log'))
        trace_path=results/(case+'-capture.log')
        if traces:shutil.copy(max(traces,key=lambda p:p.stat().st_mtime),trace_path)
        manifest['cases'][case]={'exit':completed.returncode,'exe_sha256':sha(probe/exe),'report_sha256':sha(stdout_path),'trace_sha256':sha(trace_path) if trace_path.exists() else None}
        (results/'ownership-integration-build.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(f'{case}: exit={completed.returncode}',flush=True)
        if completed.returncode:raise SystemExit(completed.returncode)
manifest['sources']=sources()
manifest['source_tree_unchanged_during_run']=manifest['sources_at_start']==manifest['sources']
(results/'ownership-integration-build.json').write_text(json.dumps(manifest,indent=2)+'\n')
