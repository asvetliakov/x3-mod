#!/usr/bin/env python3
"""Build (host only) and run the collide-descent-sse2 CPU fixture in the X3 bottle; write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_descent_sse2.py
The build step (build_collide_descent_sse2.py) cross-compiles and audits on the host and never runs Wine; pass
--no-build to run an existing binary.
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
EXE = ROOT / 'build/verification/collide-descent-sse2/collide_descent_sse2_fixture.exe'
OUT = ROOT / 'verification/results/collide-descent-sse2-cpu.json'
SUMMARY_KEYS = ('pairs', 'visits', 'leaf_calls', 'contacts', 'core_transform_differs', 'core_nodes_differs', 'core_leafs_differs', 'core_outputs_differ', 'core_entries_differ',
                'thunk_leafs_differs', 'thunk_outputs_differ', 'thunk_entries_differ', 'vanilla_sat_nodes_differs', 'vanilla_sat_leafs_differs', 'float_nodes_differs',
                'float_leafs_differs', 'float_outputs_differ')
MUST_BE_ZERO = SUMMARY_KEYS[4:12]
BENCH_KEYS = ('visits_per_query', 'leaf_calls_per_query', 'engine_vanilla_ns', 'engine_sse2_sat_ns', 'descent_sse2_ns')


def _fields(line):
    return {k: (float(v) if '.' in v else int(v)) for k, v in re.findall(r'(\w+)=([\d.]+)(?=\s|$)', line)}


def parse(stdout):
    lines = stdout.splitlines()
    total = re.search(r'COLLIDE DESCENT SSE2 CPU checks=(\d+) failures=(\d+)', stdout)
    summary = _fields(next((l for l in lines if l.startswith('SUMMARY ')), ''))
    bench = _fields(next((l for l in lines if l.startswith('COLLIDE DESCENT SSE2 BENCH ')), ''))
    compare = {}
    for line in lines:
        match = re.match(r'COMPARE (\w+) category=(\w+) (.*)$', line)
        if match:
            compare.setdefault(match.group(1), {})[match.group(2)] = _fields(match.group(3))
    complete = all(k in summary for k in SUMMARY_KEYS) and all(k in bench for k in BENCH_KEYS)
    ratios = None
    if complete and bench['descent_sse2_ns'] > 0:
        ratios = {'vs_vanilla': round(bench['engine_vanilla_ns'] / bench['descent_sse2_ns'], 2), 'vs_sse2_sat': round(bench['engine_sse2_sat_ns'] / bench['descent_sse2_ns'], 2)}
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None, 'summary': summary if complete else None,
            'bench': bench if complete else None, 'bench_ratios': ratios, 'compare': compare, 'passes': [l for l in lines if l.startswith('PASS ')], 'visit_mix': _fields(next((l for l in lines if l.startswith('MIX ')), '')), 'micro_ns': {m.group(1): float(m.group(2)) for m in (re.match(r'MICRO (\w+) ns=([\d.]+)', l) for l in lines) if m},
            'install_lines': [l for l in lines if l.startswith(('collide_descent_sse2 ', 'collide_sat_sse2 '))][:12], 'failure_lines': [l for l in lines if l.startswith(('FAIL', 'DETAIL'))][:40]}


def accepted(record):
    summary = record.get('summary')
    return bool(record.get('exit_status') == 0 and record.get('failures') == 0 and summary and summary['pairs'] >= 100000 and all(summary[k] == 0 for k in MUST_BE_ZERO)
                and record.get('bench'))


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    build = None
    if '--no-build' not in sys.argv[1:]:
        import build_collide_descent_sse2
        build = build_collide_descent_sse2.build()
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=3000)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1), **parse(run.stdout), 'bottle': bottle.describe(name),
              'build': {k: build[k] for k in ('engine', 'audit')} if build else None,
              'note': 'ns are per node-pair visit of one overlapping, contact-free tree pair through the real query 0x004e2780; diagnostic timings, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'elapsed_s', 'summary', 'bench', 'bench_ratios', 'micro_ns', 'visit_mix', 'passes', 'failure_lines')}))
    if run.returncode != 0 and not record['checks']:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if accepted(record) else 1)


if __name__ == '__main__':
    main()
