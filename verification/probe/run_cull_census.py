#!/usr/bin/env python3
"""Run the built cull-census CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_census.py
Build first with build_cull_census.py (which never runs Wine).
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
EXE = ROOT / 'build/verification/cull-census/cull_census_fixture.exe'
OUT = ROOT / 'verification/results/cull-census-cpu.json'


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=600)
    total = re.search(r'CULL CENSUS CPU checks=(\d+) failures=(\d+)', run.stdout)
    bench = re.search(r'CULL CENSUS BENCH native_pass_us=([\d.]+) patched_disarmed_us=([\d.]+) patched_armed_us=([\d.]+)', run.stdout)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1),
              'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
              'failure_lines': [l for l in run.stdout.splitlines() if l.startswith('FAIL') or l.startswith('DETAIL')],
              'bench_us': {k: float(bench.group(i + 1)) for i, k in enumerate(('native_pass', 'patched_disarmed', 'patched_armed'))} if bench else None,
              'install_lines': [l for l in run.stdout.splitlines() if l.startswith('cull_census ')],
              'bottle': bottle.describe(name), 'note': 'harness-inclusive per-pass estimates over a 12-node tree (10 measured), not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'bench_us', 'failure_lines')}))
    if run.returncode != 0 and not total:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if run.returncode == 0 and total and record['failures'] == 0 else 1)


if __name__ == '__main__':
    main()
