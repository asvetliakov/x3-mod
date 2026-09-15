#!/usr/bin/env python3
"""Run the built chase CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_chase_transition_cpu.py
Build first with build_chase_transition_cpu.py (which never runs Wine).
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
ROOT=Path(__file__).resolve().parents[2]
EXE=ROOT/'build/verification/chase-transition/chase_transition_cpu_fixture.exe'
OUT=ROOT/'verification/results/chase-restore-cpu.json'
def main():
    name=os.environ.get('X3M_FIXTURE_BOTTLE')
    if name!='X3':sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started=time.time()
    run=subprocess.run([bottle.WINE,*bottle.wine_args(name),str(EXE)],capture_output=True,text=True,timeout=900)
    total=re.search(r'CHASE TRANSITION LEAD CPU stubs=(\d+) restore_stubs=(\d+) checks=(\d+) failures=(\d+)',run.stdout)
    bench={}
    for m in re.finditer(r'CHASE STUB BENCH kind=(\d+) .*?baseline_mean_us=([\d.]+) full_stub_mean_us=([\d.]+) delta_mean_us=(-?[\d.]+) delta_best_trial_us=(-?[\d.]+) scope=(\S+)',run.stdout):
        bench[m.group(1)]={'baseline_mean_us':float(m.group(2)),'stub_mean_us':float(m.group(3)),'delta_mean_us':float(m.group(4)),'delta_best_trial_us':float(m.group(5)),'scope':m.group(6)}
    record={'fixture':str(EXE.relative_to(ROOT)),'exit_status':run.returncode,'elapsed_s':round(time.time()-started,1),
            'stubs':int(total.group(1)) if total else None,'restore_stubs':int(total.group(2)) if total else None,
            'checks':int(total.group(3)) if total else None,'failures':int(total.group(4)) if total else None,
            'failure_lines':[l for l in run.stdout.splitlines() if l.startswith('FAIL')],
            'seam_idle_prefilter':bench.get('18'),'full_stub_reference':bench.get('0'),'bottle':bottle.describe(name),
            'note':'paired harness estimates, not game FPS'}
    OUT.write_text(json.dumps(record,indent=1)+'\n')
    print(json.dumps({k:record[k] for k in ('checks','failures','stubs','restore_stubs','exit_status')}))
    sys.exit(0 if run.returncode==0 and total and record['failures']==0 else 1)
if __name__=='__main__':main()
