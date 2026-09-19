#!/usr/bin/env python3
"""Run only through wine_lock.py, X3M_FIXTURE_BOTTLE=X3. Never launch the game."""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import bottle
from build_collide_query_phases import ROOT,BUILD,RECORD,sha256,source_hashes


def parse(text):
    total=re.search(r'COLLIDE QUERY CPU checks=(\d+) failures=(\d+) queries=(\d+) differences=(\d+)',text)
    bench=re.search(r'COLLIDE QUERY BENCH off_ns=([\d.]+) on_ns=([\d.]+) overhead_ns=([-\d.]+) iterations=(\d+)',text)
    return {'checks':int(total[1]) if total else 0,'failures':int(total[2]) if total else -1,
            'queries':int(total[3]) if total else 0,'differences':int(total[4]) if total else -1,
            'bench':dict(zip(('off_ns','on_ns','overhead_ns','iterations'),map(float,bench.groups()))) if bench else None,
            'windows':[{key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',line)} for line in text.splitlines() if line.startswith('collide_query_phases ')],
            'failure_lines':[line for line in text.splitlines() if line.startswith(('FAIL','DETAIL','COMPONENT'))][:30]}


def accepted(result):
    return (provenance_valid(result) and result.get('exit_status')==0 and result['checks']==142 and result['failures']==0 and result['differences']==0
            and result['queries']==675 and len(result['windows'])==2 and bool(result['bench']) and not result['failure_lines']
            and all(w.get('frames')==300 and w.get('valid_frames')==300 and w.get('invalid_frames')==0
                    and w.get('invalid')==0 and w.get('queries')==300 and w.get('descents')==300
                    and w.get('misses')==300 and w.get('verifies')==0 and w.get('triangles')==0
                    and w.get('contacts')==0 and w.get('visits')==300 for w in result['windows'])
            and [(w.get('first_frame'),w.get('frame')) for w in result['windows']]==[(1,300),(301,600)])


def provenance_valid(result):
    build=result.get('build',{})
    digest=build.get('binary_sha256','')
    sources=build.get('source_sha256',{})
    return bool(re.fullmatch('[0-9a-f]{64}',digest) and sources and build.get('compiler') and build.get('commands')
                and result.get('exe_sha256_before')==digest==result.get('exe_sha256_after')
                and result.get('source_sha256_before_run')==sources==result.get('source_sha256_after_run')
                and re.fullmatch('[0-9a-f]{64}',result.get('stdout_sha256','')))


def main():
    if os.environ.get('X3M_FIXTURE_BOTTLE')!='X3':sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    if '--no-build' not in sys.argv[1:]:
        from build_collide_query_phases import build
        built=build()
    else:
        if not RECORD.exists():sys.exit('missing build record; run without --no-build')
        built=json.loads(RECORD.read_text())
    exe=BUILD/'collide_query_phases_fixture.exe'
    before=sha256(exe);sources_before=source_hashes(built['source_sha256'])
    if before!=built['binary_sha256'] or sources_before!=built['source_sha256']:
        sys.exit('fixture binary or scoped sources differ from build record; rebuild')
    start=time.monotonic();run=subprocess.run([bottle.WINE,*bottle.wine_args('X3'),str(exe)],capture_output=True,timeout=120)
    stdout=run.stdout.decode('utf-8',errors='replace')
    result={'exit_status':run.returncode,'elapsed_s':round(time.monotonic()-start,3),'bottle':bottle.describe('X3'),**parse(stdout),
            'build':built,'exe_sha256_before':before,'exe_sha256_after':sha256(exe),
            'source_sha256_before_run':sources_before,'source_sha256_after_run':source_hashes(built['source_sha256']),
            'stdout_sha256':hashlib.sha256(run.stdout).hexdigest(),
            'note':'CPU diagnostic overhead; not game FPS. Native Windows runtime remains unverified.'}
    out=bottle.results_dir(ROOT,'X3');(out/'collide-query-phases.txt').write_bytes(run.stdout);(out/'collide-query-phases-stderr.txt').write_bytes(run.stderr)
    (out/'collide-query-phases.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    return 0 if accepted(result) else 1

if __name__=='__main__':raise SystemExit(main())
