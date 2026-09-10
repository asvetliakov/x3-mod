#!/usr/bin/env python3
"""Run baseline and experimental wrapper fixture in isolated Preview directories."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
root=Path(__file__).resolve().parents[2]
probe=root/'verification/probe/build';results=root/'verification/results'
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
manifest={'runtime':wine,'bottle':'Steam','game_launched':False,'fixtures':{}}
for mode in ('baseline','wrapped'):
    exe='ownership_'+mode+'.exe'
    directory=probe/('ownership-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+mode)
    directory.mkdir(parents=True);shutil.copy(probe/exe,directory)
    env=dict(os.environ);env.pop('X3M_TELEMETRY',None)
    command=[wine,'--bottle','Steam','--no-update','--workdir',str(directory),str(directory/exe)]
    stdout_path=results/f'ownership-{mode}.txt';stderr_path=results/f'ownership-{mode}-wine.log'
    with stdout_path.open('w') as stdout,stderr_path.open('w') as stderr:
        completed=subprocess.run(command,env=env,stdout=stdout,stderr=stderr,timeout=90)
    manifest['fixtures'][mode]={'exe_sha256':hashlib.sha256((probe/exe).read_bytes()).hexdigest(),'report_sha256':hashlib.sha256(stdout_path.read_bytes()).hexdigest(),'exit':completed.returncode}
    print(f'{mode}: exit={completed.returncode}',flush=True)
    if completed.returncode:
        (results/'ownership-build-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
        raise SystemExit(completed.returncode)
manifest['sources']={str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted((root/'src/ownership').glob('*')) if path.suffix in ('.cpp','.h')}
manifest['source_sha256']=manifest['sources']['src/ownership/d3d9_ownership.cpp']
(results/'ownership-build-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
