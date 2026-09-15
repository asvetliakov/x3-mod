#!/usr/bin/env python3
"""Owner-only CPU fixture execution; build first, wrap this runner in wine_lock.py.

No candidate DLL build, game launch, graphics fixture or installation. Existing
forwarder fixture runs with interval recording enabled. Benchmark pairs compare
the same changed wrapper with recorder admission off/on, including thread startup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time
import sys
import bottle
from game_guard import game_running

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/analysis'))
from analyze_loading_intervals import analyze
WINE='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-benchmark',action='store_true')
    args=parser.parse_args()
    if bottle.BOTTLE!='X3':raise RuntimeError('acceptance requires X3M_FIXTURE_BOTTLE=X3')
    result_dir=bottle.results_dir(ROOT);result_dir.mkdir(parents=True,exist_ok=True)
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,cases={},benchmarks=[])
    path=result_dir/'loading-intervals-summary.json'
    path.write_text(json.dumps(result,indent=2)+'\n')
    interval=ROOT/'verification/probe/build/loading_intervals/loading_intervals_fixture.exe'
    forwarder=ROOT/'verification/probe/build/loading_trace/loading_trace_fixture.exe'
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    binaries={str(p):digest(p) for p in (interval,forwarder)}
    result['binaries']=binaries
    def run(label,exe,arguments=(),override=''):
        if game_running():raise RuntimeError('X3 is running')
        if digest(exe)!=binaries[str(exe)]:raise RuntimeError('fixture changed while queued')
        command=[WINE,'--bottle','X3','--no-update','--workdir',str(exe.parent)]
        if override:command+=['--dll',override]
        command+=[str(exe),*map(str,arguments)]
        env=dict(os.environ,X3M_LOADING_INTERVALS='1',X3M_MESH_CACHE='0',X3M_ADMISSION='0',WINEDLLOVERRIDES=override)
        start=time.monotonic()
        completed=subprocess.run(command,cwd=exe.parent,env=env,text=True,capture_output=True,timeout=60)
        elapsed=time.monotonic()-start
        (result_dir/f'loading-intervals-{label}.txt').write_text(completed.stdout)
        (result_dir/f'loading-intervals-{label}-wine.log').write_text(completed.stderr)
        if completed.returncode:raise RuntimeError(f'{label}: exit {completed.returncode}')
        result['cases'][label]=dict(exit_code=0,elapsed_seconds=elapsed)
        print(f'loading intervals {label}: exit 0, {elapsed:.3f} s',flush=True)
        return completed.stdout
    try:
        for mode in ('normal','abandon','threads','allocation','tls_alloc','tls_set','reuse'):
            text=run(mode,interval,(mode,))
            match=re.search(rf'loading_intervals_fixture mode={mode} checks=(\d+) failures=0\s*\Z',text)
            if not match:raise RuntimeError(f'{mode}: incomplete checks')
            result['cases'][mode]['checks']=int(match[1])
        text=run('forwarder',forwarder,override='d3dx9_37,zlib1,libxml2=n,b')
        if not text.rstrip().endswith('loading_fixture checks=112 failures=0'):
            raise RuntimeError('forwarder: incomplete inventory')
        if not re.search(r'loading_intervals_start requested=1 telemetry=1 .*flags=0 ',text):
            raise RuntimeError('forwarder: recorder did not initialize')
        result['cases']['forwarder']['checks']=112
        files=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('loading_intervals_file ')]
        if len(files)!=1 or files[0]['written']!='1':raise RuntimeError('export was not successful and exactly once')
        artifact=forwarder.parent/files[0]['file']
        reduced=analyze(artifact)
        if (artifact.stat().st_size!=int(files[0]['bytes']) or
                reduced['retained_records']!=int(files[0]['rows']) or not reduced['complete'] or
                (reduced['device'],reduced['reset'],reduced['frame'])!=(3,4,5)):
            raise RuntimeError('export identity/count/coverage mismatch')
        result['artifact']=dict(path=str(artifact),sha256=digest(artifact),summary=reduced,
                                export_ticks=int(files[0]['export_ticks']),frequency=int(files[0]['frequency']))
        if not args.no_benchmark:
            for threads in (1,4):
                for nesting in (1,2):
                    pairs=[]
                    for repeat in range(3):
                        ns={}
                        for mode in ('bench_disabled','bench_enabled'):
                            text=run(f'{mode}-{threads}-{nesting}-{repeat}',interval,(mode,threads,nesting))
                            match=re.search(r'ns_per_span=([0-9.]+)',text)
                            if not match:raise RuntimeError('missing benchmark result')
                            ns[mode]=float(match[1])
                        pairs.append(dict(disabled_ns=ns['bench_disabled'],enabled_ns=ns['bench_enabled'],added_ns=ns['bench_enabled']-ns['bench_disabled']))
                    added=statistics.median(row['added_ns'] for row in pairs)
                    item=dict(threads=threads,nesting=nesting,pairs=pairs,median_added_ns=added,coarse_ceiling_ns=13244.424,within_ceiling=added<13244.424)
                    result['benchmarks'].append(item)
                    if not item['within_ceiling']:raise RuntimeError('recorder overhead exceeds coarse budget')
        result['limits']='CrossOver fixture cost, not game FPS/native Windows. Off/on delta excludes the disabled admission check; no old-build baseline. Exporter duration measured separately in the CPU forwarder fixture; game exporter cost remains unmeasured.'
        result['passed']=True
    finally:
        path.write_text(json.dumps(result,indent=2)+'\n')
    print(path)


if __name__=='__main__':main()
