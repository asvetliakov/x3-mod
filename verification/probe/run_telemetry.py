#!/usr/bin/env python3
"""Run disposable graphics fixture with backend, proxy off and proxy on."""
from pathlib import Path
import datetime
import os
import shutil
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
root=Path(__file__).resolve().parents[2]
probe=root/'verification/probe/build'
results=bottle.results_dir(root)
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
for mode in ('baseline','off','on'):
    directory=probe/('telemetry-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+mode)
    directory.mkdir(parents=True)
    shutil.copy(probe/'telemetry_fixture.exe',directory)
    env=dict(os.environ,X3M_TELEMETRY='1' if mode=='on' else '0',X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES='1')
    command=[wine,'--bottle',bottle.BOTTLE,'--no-update','--workdir',str(directory)]
    if mode!='baseline':
        shutil.copy(root/'build/d3d9.dll',directory)
        command+=['--dll','d3d9=n,b']
    command+=[str(directory/'telemetry_fixture.exe')]
    with (results/f'telemetry-{mode}.txt').open('w') as stdout,(results/f'telemetry-{mode}-wine.log').open('w') as stderr:
        result=subprocess.run(command,env=env,stdout=stdout,stderr=stderr,timeout=90)
    traces=list((directory/'x3-modern-captures').glob('session-*.log'))
    if traces:shutil.copy(max(traces,key=lambda p:p.stat().st_mtime),results/f'telemetry-{mode}-capture.log')
    print(f'{mode}: exit={result.returncode}',flush=True)
    if result.returncode:raise SystemExit(result.returncode)
