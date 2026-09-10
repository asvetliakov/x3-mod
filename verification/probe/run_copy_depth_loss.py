#!/usr/bin/env python3
"""Fresh-build regression with pre/post source hashes; original hidden device only."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    root=Path(__file__).resolve().parents[2]
    source_paths=[root/'verification/probe/copy_depth_loss.cpp',root/'verification/probe/build_copy_depth_loss.sh',Path(__file__).resolve(),*sorted((root/'src/ownership').glob('*.cpp')),*sorted((root/'src/ownership').glob('*.h'))]
    def hashes():return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
    before=hashes();results=root/'verification/results';exe=root/'verification/probe/build/copy_depth_loss.exe'
    metadata=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),sources_before=before,game_launched=False,fresh_build=True)
    build=subprocess.run(['sh',str(root/'verification/probe/build_copy_depth_loss.sh')],capture_output=True,text=True)
    metadata['build_exit']=build.returncode;(results/'copy-depth-loss-build.txt').write_text(build.stdout+build.stderr)
    metadata['sources_after_build']=hashes()
    if build.returncode or before!=metadata['sources_after_build']:
        metadata.update(passed=False,reason='Build failed or source changed during compilation')
    else:
        metadata['executable_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--workdir',str(exe.parent),str(exe)]
        env=dict(os.environ);env['WINEDLLOVERRIDES']='d3d9=b'
        metadata.update(command=command,process_local_override='d3d9=b',timeout_seconds=90)
        with (results/'copy-depth-loss.txt').open('w') as out,(results/'copy-depth-loss-wine.log').open('w') as err:
            try:metadata['exit_code']=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=90).returncode
            except subprocess.TimeoutExpired:metadata.update(exit_code=None,timed_out=True)
        text=(results/'copy-depth-loss.txt').read_text();metadata['cases']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('CASE ')]
        metadata['sources_after_run']=hashes()
        metadata['executable_sha256_after_run']=hashlib.sha256(exe.read_bytes()).hexdigest()
        metadata['passed']=(metadata['exit_code']==0 and 'RESULT PASS' in text and len(metadata['cases'])==33 and before==metadata['sources_after_run'] and metadata['executable_sha256']==metadata['executable_sha256_after_run'])
        metadata['report_sha256']=hashlib.sha256((results/'copy-depth-loss.txt').read_bytes()).hexdigest()
    (results/'copy-depth-loss-summary.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps({k:v for k,v in metadata.items() if k not in ('cases','sources_before','sources_after_build','sources_after_run')},indent=2));return 0 if metadata['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
