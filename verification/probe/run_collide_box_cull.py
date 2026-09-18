#!/usr/bin/env python3
"""Run the built collide-box-cull CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_box_cull.py
Build first with build_collide_box_cull.py (which never runs Wine).
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build/verification/collide-box-cull/collide_box_cull_fixture.exe'
OUT = ROOT / 'verification/results/collide-box-cull-cpu.json'
BENCH = ('native', 'disarmed', 'armed_counted', 'armed_plain')


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=900)
    total = re.search(r'COLLIDE BOX CULL CPU checks=(\d+) failures=(\d+)', run.stdout)
    number = r'=([\d.]+)'
    bench = re.search(r'COLLIDE BOX CULL BENCH ns_per_pair far: ' + ' '.join(k + number for k in BENCH) + ' near: ' + ' '.join(k + number for k in BENCH), run.stdout)
    lines = run.stdout.splitlines()
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1),
              'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
              'failure_lines': [l for l in lines if l.startswith(('FAIL', 'DETAIL'))][:40],
              'pair_lines': [l for l in lines if l.startswith(('pairs ', 'native exits', 'stub rejects'))],
              'bench_ns_per_pair': {group: {k: float(bench.group(i + 1 + offset)) for i, k in enumerate(BENCH)} for group, offset in (('far', 0), ('near', 4))} if bench else None,
              'bottle': bottle.describe(name), 'note': 'harness-inclusive per-pair estimates (frame copy, x87 load, exit record in every variant), not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'pair_lines', 'bench_ns_per_pair', 'failure_lines')}))
    if run.returncode != 0 and not total:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if run.returncode == 0 and total and record['failures'] == 0 else 1)


if __name__ == '__main__':
    main()
