#!/usr/bin/env python3
"""Run the built collide-narrow-census CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_narrow_census.py
Build first with build_collide_narrow_census.py (which never runs Wine).
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
import verify_collide_sites as sites
ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build/verification/collide-narrow-census/collide_narrow_census_fixture.exe'
OUT = ROOT / 'verification/results/collide-narrow-census-cpu.json'


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=900)
    total = re.search(r'COLLIDE NARROW CENSUS CPU checks=(\d+) failures=(\d+)', run.stdout)
    bench = re.search(r'COLLIDE NARROW CENSUS BENCH ns_per_pair native=([\d.]+) patched=([\d.]+) ns_per_node_visit native=([\d.]+) patched=([\d.]+)', run.stdout)
    lines = run.stdout.splitlines()
    window = next((sites.parse_narrow_window_line(l[7:]) for l in lines if l.startswith('WINDOW ')), None)
    row = next((sites.parse_narrow_pair_line(l[4:]) for l in lines if l.startswith('ROW ')), None)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1),
              'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
              'failure_lines': [l for l in lines if l.startswith(('FAIL', 'DETAIL'))][:40],
              'cross_thread': next((l for l in lines if l.startswith('cross-thread ')), None),
              'window_line_parsed': bool(window and window['bounded']), 'pair_row_parsed': bool(row and row['bounded']),
              'bench_ns': {'pair_native': float(bench.group(1)), 'pair_patched': float(bench.group(2)), 'node_visit_native': float(bench.group(3)),
                           'node_visit_patched': float(bench.group(4))} if bench else None,
              'bottle': bottle.describe(name), 'note': 'harness-inclusive estimates (frame copy, x87 load, exit record in every variant), not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'cross_thread', 'window_line_parsed', 'pair_row_parsed', 'bench_ns', 'failure_lines')}))
    if run.returncode != 0 and not total:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    ok = run.returncode == 0 and total and record['failures'] == 0 and record['window_line_parsed'] and record['pair_row_parsed']
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
