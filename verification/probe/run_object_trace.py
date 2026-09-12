#!/usr/bin/env python3
"""Fresh-build original object-context ABI/exception/rollback regression."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory


def main():
    root=Path(__file__).resolve().parents[2]
    inputs=['src/proxy/object_trace.cpp','src/proxy/object_trace.h','src/proxy/engine_memory.cpp','src/proxy/engine_memory.h','verification/probe/object_trace.cpp','verification/probe/build_object_trace.sh','verification/probe/run_object_trace.py']
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    def sources():return {name:digest(root/name) for name in inputs}
    results=bottle.results_dir(root);results.mkdir(exist_ok=True)
    exe=root/'verification/probe/build/object_trace.exe'
    report=results/'object-trace.txt';wine_log=results/'object-trace-wine.log'
    data=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),game_launched=False, bottle=bottle.describe(),fresh_build=True,sources_before=sources())
    build=subprocess.run(['sh',str(root/'verification/probe/build_object_trace.sh')],capture_output=True)
    (results/'object-trace-build.txt').write_bytes(build.stdout+build.stderr)
    data.update(build_exit=build.returncode,sources_after_build=sources(),passed=False)
    if build.returncode==0 and data['sources_before']==data['sources_after_build']:
        data['executable_sha256']=digest(exe)
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle',bottle.BOTTLE,'--no-update','--workdir',str(exe.parent),str(exe)]
        data['command']=command
        with report.open('wb') as out,wine_log.open('wb') as err:
            try:data['exit_code']=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ),timeout=45).returncode
            except subprocess.TimeoutExpired:data.update(exit_code=None,timed_out=True)
        data['executable_sha256_after_run']=digest(exe)
        data['sources_after_run']=sources()
        data['report_sha256']=digest(report)
        text=report.read_text()
        match=re.search(r'RESULT (PASS|FAIL) checks=(\d+) failures=(\d+) backend_calls=(\d+)',text)
        if match:data.update(checks=int(match[2]),failures=int(match[3]),backend_calls=int(match[4]))
        # Read-path evidence: per-mode cost (TIMING) and record identity (IDENTITY).
        parse=lambda line:{k:v for k,v in (kv.split('=',1) for kv in line.split()[1:])}
        data['read_path']={'timing':[parse(l) for l in text.splitlines() if l.startswith('TIMING ')],'identity':[parse(l) for l in text.splitlines() if l.startswith('IDENTITY ')]}
        identical=len(data['read_path']['identity'])==1 and data['read_path']['identity'][0].get('equal')=='1' and len(data['read_path']['timing'])==2
        data['passed']=bool(data['exit_code']==0 and match and match[1]=='PASS' and data['failures']==0 and identical and data['sources_before']==data['sources_after_run'] and data['executable_sha256']==data['executable_sha256_after_run'])
    (results/'object-trace-summary.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({k:v for k,v in data.items() if not k.startswith('sources_')},indent=2))
    return 0 if data['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
