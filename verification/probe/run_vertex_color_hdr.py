#!/usr/bin/env python3
"""Run the original interpolation probe in Preview; record numeric evidence.

No game launch, installation, registry modification, or copied game shader occurs.
The probe has a hidden device window and a bounded process lifetime.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root/'verification/probe/build/vertex_color_hdr.exe'
    source = root/'verification/probe/vertex_color_hdr.cpp'
    results = root/'verification/results'
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle','Steam','--no-update','--workdir',str(executable.parent),
               str(executable),r'C:\X3\d3dx9_37.dll']
    environment = os.environ.copy()
    environment['WINEDLLOVERRIDES'] = 'd3d9=b'
    metadata = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    command=command, process_local_override='d3d9=b', timeout_seconds=60,
                    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    with (results/'vertex-color-hdr.txt').open('w') as out, (results/'vertex-color-hdr-wine.log').open('w') as err:
        try:
            p = subprocess.run(command,stdout=out,stderr=err,env=environment,timeout=60)
            metadata['exit_code'] = p.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None,timed_out=True)
    text = (results/'vertex-color-hdr.txt').read_text()
    samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
    cases = [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('CASE ')]
    metadata.update(sample_checks=len(samples),passed_sample_checks=sum(s.endswith(' PASS') for s in samples),cases=cases)
    models = {}
    for case in cases:
        key = 'sm'+case['profile']+'_'+case['semantic']
        models[key] = models.get(key,7) & int(case['consistent_models'])
    names = {1:'unclamped',2:'clamped_at_vertex_output',4:'clamped_after_interpolation'}
    metadata['interpolation_models_consistent_across_both_generations'] = {
        key:[name for bit,name in names.items() if mask & bit] for key,mask in models.items()}
    metadata['passed'] = (metadata['exit_code']==0 and len(samples)==96
                          and metadata['passed_sample_checks']==96 and len(cases)==32
                          and 'RESULT PASS:' in text
                          and 'API Reset after resource release result=00000000' in text
                          and all(models.values()))
    (results/'vertex-color-hdr-summary.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps({k:v for k,v in metadata.items() if k!='cases'},indent=2))
    return 0 if metadata['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
