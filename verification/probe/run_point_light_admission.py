#!/usr/bin/env python3
"""Run the built point-light admission CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_point_light_admission.py
Build first with build_point_light_admission.py (which never runs Wine).
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
EXE = ROOT / 'build/verification/point-light-admission/point_light_admission_fixture.exe'
OUT = ROOT / 'verification/results/point-light-admission-cpu.json'


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=600)
    total = re.search(r'POINT LIGHT ADMISSION CPU checks=(\d+) failures=(\d+)', run.stdout)
    bench = re.search(r'POINT LIGHT ADMISSION BENCH native_admit_us=([\d.]+) native_reject_us=([\d.]+) patched_admit_us=([\d.]+) patched_root_walk_us=([\d.]+) patched_memo_hit_us=([\d.]+)', run.stdout)
    outcomes = re.search(r'POINT LIGHT ADMISSION OUTCOMES (.*)$', run.stdout, re.M)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1),
              'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
              'failure_lines': [l for l in run.stdout.splitlines() if l.startswith('FAIL')],
              'bench_us': {k: float(bench.group(i + 1)) for i, k in enumerate(('native_admit', 'native_reject', 'patched_admit', 'patched_root_walk', 'patched_memo_hit'))} if bench else None,
              'outcomes': dict(kv.split('=') for kv in outcomes.group(1).split()) if outcomes else None,
              'install_lines': [l for l in run.stdout.splitlines() if l.startswith('point_light_root_admission ')],
              'bottle': bottle.describe(name), 'note': 'harness-inclusive per-call estimates, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'bench_us')}))
    if run.returncode != 0 and not total:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if run.returncode == 0 and total and record['failures'] == 0 else 1)


if __name__ == '__main__':
    main()
