#!/usr/bin/env python3
"""Run original GPU depth reconstruction precision and cost checks in Preview."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    root=Path(__file__).resolve().parents[2]
    executable=root/'verification/probe/build/depth_decode.exe'
    shader=root/'src/temporal/depth_decode.hlsl'
    source=root/'verification/probe/depth_decode.cpp'
    results=root/'verification/results'
    paths = [root / name for name in ['verification/probe/depth_decode.cpp', 'verification/probe/build_depth_decode.sh', 'verification/probe/run_depth_decode.py', 'src/temporal/depth_decode.hlsl']]
    def source_hashes():
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    results.mkdir(parents=True, exist_ok=True)
    before = source_hashes()
    # Always compile the recorded inputs; a stale executable is never evidence.
    try:
        build = subprocess.run(['sh', str(root / 'verification/probe/build_depth_decode.sh')],
                               cwd=root, capture_output=True, text=True, timeout=60)
        build_code, build_output = build.returncode, build.stdout + build.stderr
    except subprocess.TimeoutExpired:
        build_code, build_output = None, 'Build timed out after 60 seconds'
    after_build = source_hashes()
    if build_code != 0 or after_build != before or not executable.is_file():
        failure = dict(passed=False, freshly_built=False, build_exit_code=build_code,
                       reason='Build failed, executable absent, or source changed during compilation',
                       build_output=build_output, sources_sha256_before=before,
                       sources_sha256_after=after_build)
        (results / 'depth-decode-summary.json').write_text(json.dumps(failure, indent=2)+'\n')
        print(json.dumps(failure, indent=2))
        return 1
    command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
             '--bottle','Steam','--no-update','--workdir',str(executable.parent),str(executable),
             r'C:\X3\d3dx9_37.dll','Z:'+str(shader)]
    metadata=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),command=command,
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  shader_sha256=hashlib.sha256(shader.read_bytes()).hexdigest(),
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  process_local_override='d3d9=b',timeout_seconds=60)
    metadata.update(freshly_built=True, build_exit_code=build_code, build_output=build_output,
                    sources_sha256=before, sources_unchanged_after_build=after_build == before)
    with (results/'depth-decode.txt').open('w') as out,(results/'depth-decode-wine.log').open('w') as err:
        try:
            p=subprocess.run(command,stdout=out,stderr=err,timeout=60,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'))
            metadata['exit_code']=p.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None,timed_out=True)
    text=(results/'depth-decode.txt').read_text()
    def lines(prefix):return [line for line in text.splitlines() if line.startswith(prefix+' ')]
    def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))
    samples=lines('SAMPLE');copies=lines('COPY_STATE');restores=lines('RESTORE');timings=lines('TIMING')
    metadata.update(sample_checks=len(samples),passed_sample_checks=sum(s.endswith(' PASS') for s in samples),
                    copy_checks=len(copies),passed_copy_checks=sum(s.endswith(' PASS') for s in copies),
                    state_checks=len(restores),passed_state_checks=sum(s.endswith(' PASS') for s in restores),
                    maximum_error_d24_lsb=max((float(fields(s)['error_d24_lsb']) for s in samples),default=None),
                    timings=[fields(line) for line in timings],reset_passed='RESET PASS' in text)
    metadata['sources_unchanged_after_run'] = source_hashes() == before
    metadata['executable_unchanged_after_run'] = (
        hashlib.sha256(executable.read_bytes()).hexdigest() == metadata['executable_sha256'])
    metadata['passed']=(metadata['exit_code']==0 and metadata['sources_unchanged_after_run']
                        and metadata['executable_unchanged_after_run'] and metadata['passed_sample_checks']==len(samples)==256
                        and metadata['passed_copy_checks']==len(copies)==4
                        and metadata['passed_state_checks']==len(restores)==8
                        and len(timings)==4 and metadata['reset_passed'] and 'RESULT PASS:' in text)
    (results/'depth-decode-summary.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata,indent=2))
    return 0 if metadata['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
