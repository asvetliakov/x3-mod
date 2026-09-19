#!/usr/bin/env python3
"""Run the built collide-sat-sse2 CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_sat_sse2.py
Build first with build_collide_sat_sse2.py (which never runs Wine).
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
EXE = ROOT / 'build/verification/collide-sat-sse2/collide_sat_sse2_fixture.exe'
OUT = ROOT / 'verification/results/collide-sat-sse2-cpu.json'
CATEGORY_RE = re.compile(r'^CATEGORY (\w+) ' + ' '.join(rf'{k}=(?P<{k}>\d+)' for k in ('cases', 'both_keep', 'both_prune', 'sse_keeps', 'violations', 'axis_mismatch', 'axis_earlier', 'outside_band', 'axis_unexplained')) + '$')
BENCH_KEYS = ('null_ns', 'early_axis_mean', 'early_replica_ns', 'early_sse2_ns', 'early_bracketed_ns', 'full_replica_ns', 'full_sse2_ns', 'full_bracketed_ns')


def parse(stdout):
    lines = stdout.splitlines()
    total = re.search(r'COLLIDE SAT SSE2 CPU checks=(\d+) failures=(\d+)', stdout)
    categories = {m.group(1): {k: int(v) for k, v in m.groupdict().items()} for m in (CATEGORY_RE.match(l) for l in lines) if m}
    bench_line = next((l for l in lines if l.startswith('COLLIDE SAT SSE2 BENCH ')), '')
    bench = {k: float(v) for k, v in re.findall(r'(\w+)=([\d.]+)', bench_line) if k in BENCH_KEYS}
    summary = None
    if len(bench) == len(BENCH_KEYS):
        net = lambda key: max(bench[key] - bench['null_ns'], 0.01)
        summary = {'early_ratio': round(net('early_replica_ns') / net('early_sse2_ns'), 2), 'full_ratio': round(net('full_replica_ns') / net('full_sse2_ns'), 2),
                   'early_ns': [round(net('early_replica_ns'), 1), round(net('early_sse2_ns'), 1)], 'full_ns': [round(net('full_replica_ns'), 1), round(net('full_sse2_ns'), 1)],
                   'mxcsr_bracket_ns': round(bench['early_bracketed_ns'] - bench['early_sse2_ns'], 2), 'thunk_early_out_ns': round(net('early_sse2_ns'), 2)}
    cases = sum(c['cases'] for c in categories.values())
    keeps = sum(c['sse_keeps'] for c in categories.values())
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None, 'categories': categories, 'cases': cases,
            'violations': sum(c['violations'] for c in categories.values()), 'sse_keeps': keeps, 'sse_keep_rate': keeps / cases if cases else None,
            'precision': next((l for l in lines if l.startswith('PRECISION ')), None), 'notes': [l for l in lines if l.startswith(('MXCSR ', 'REPLAY ', 'KEEP ', 'ROUNDING '))], 'bench_raw_ns': bench or None, 'bench': summary,
            'failure_lines': [l for l in lines if l.startswith(('FAIL', 'DETAIL'))][:40]}


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=1800)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1), **parse(run.stdout), 'bottle': bottle.describe(name),
              'note': 'ns are per direct call through a two-push harness; bench ratios subtract the null-callee harness cost; diagnostic timings, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'cases', 'violations', 'sse_keeps', 'sse_keep_rate', 'precision', 'notes', 'bench_raw_ns', 'bench', 'failure_lines')}))
    if run.returncode != 0 and not record['checks']:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    ok = run.returncode == 0 and record['failures'] == 0 and record['violations'] == 0 and record['cases'] >= 1000000 and record['bench'] is not None
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
