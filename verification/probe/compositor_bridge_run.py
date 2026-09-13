#!/usr/bin/env python3
"""Run the synthetic EXE under wine_lock.py. Never launches the game."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import bottle
from game_guard import game_running
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'verification/probe/build/compositor_bridge'
EXPECTED_CHECKS=393
RESULT_PREFIX='COMPOSITOR BRIDGE RESULT '
RESULT_PATTERN=re.compile(r'COMPOSITOR BRIDGE RESULT checks=(\d+) failures=(\d+) normal=8 exceptional=16 continued=4 declined=4 stack_alignments=4')

def parse_report(stdout, returncode):
    """Reject missing, duplicated, failed or changed fixture inventories."""
    records=[line for line in stdout.splitlines() if line.startswith(RESULT_PREFIX)]
    match=RESULT_PATTERN.fullmatch(records[0]) if len(records)==1 else None
    checks=int(match[1]) if match else None
    passed=(returncode==0 and match is not None and checks==EXPECTED_CHECKS
            and int(match[2])==0 and not re.search(r'^FAIL(?:\s|$)',stdout,re.M))
    return {'passed':passed,'checks':checks}

def source_hashes(exe):
    paths=sorted((ROOT/'verification/probe').glob('compositor_bridge*'))
    paths += [ROOT/'verification/probe/bloom_return_bridge.S',ROOT/'verification/probe/bloom_return_bridge.h',
              ROOT/'src/proxy/compositor_bridge.h',ROOT/'src/proxy/compositor_bridge.S',ROOT/'src/proxy/compositor_bridge_seh.c',
              ROOT/'tools/build/build_compositor_bridge.py',ROOT/'verification/analysis/test_compositor_bridge.py',exe]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


def main():
    games=game_running()
    print('game_guard='+str(games),flush=True)
    if games:raise SystemExit('Game running; refusing fixture')
    inventory=subprocess.check_output(['ps','-axww','-o','pid=,args='],text=True)
    # This runner and its lock parent are expected. All unrelated fixture
    # executables/runners are refused even if not cooperating with the lock.
    conflicts=[line for line in inventory.splitlines() if 'compositor_bridge' not in line and ('verification/probe/run_' in line or re.search(r'(fixture|probe)[^ /]*\.exe(?:\s|$)',line,re.I))]
    if conflicts:raise SystemExit('Conflicting fixture processes: '+str(conflicts))
    exe=BUILD/'compositor_bridge.exe'
    env=dict(os.environ,WINEDEBUG='-all')
    command=[bottle.WINE,*bottle.wine_args(),str(exe)]
    inputs=source_hashes(exe)
    result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=60)
    log=BUILD/('compositor_bridge_'+bottle.BOTTLE+'.txt')
    log.write_text(result.stdout+'\nSTDERR\n'+result.stderr)
    parsed=parse_report(result.stdout,result.returncode)
    after_inputs=source_hashes(exe)
    sources_stable=after_inputs==inputs
    passed=parsed['passed'] and sources_stable
    report={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'bottle':bottle.describe(),'command':command,'source_hashes':inputs,'source_hashes_after':after_inputs,'sources_stable':sources_stable,'returncode':result.returncode,'passed':passed,'checks':parsed['checks'],'expected_checks':EXPECTED_CHECKS,'log':str(log),'native_windows_verified':False}
    (BUILD/('compositor_bridge_'+bottle.BOTTLE+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(result.stdout,end='');print('report_passed='+str(passed),flush=True)
    return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
