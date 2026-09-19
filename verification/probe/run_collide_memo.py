#!/usr/bin/env python3
"""Build (host only) and run the collide-memo CPU fixture in the X3 bottle; write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_memo.py
The build step (build_collide_memo.py) cross-compiles and audits on the host and never runs Wine; pass --no-build to
run an existing binary.
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
EXE = ROOT / 'build/verification/collide-memo/collide_memo_fixture.exe'
OUT = ROOT / 'verification/results/collide-memo-cpu.json'
SUMMARY_KEYS = ('queries', 'hits', 'contacts', 'differences', 'stale_hits', 'hits_on_contact', 'register_differences', 'stored', 'evictions', 'ineligible', 'skipped_visits', 'verified',
                'verify_mismatches')
MUST_BE_ZERO = ('differences', 'stale_hits', 'hits_on_contact', 'register_differences')
EXPECTED_CHECKS = 76   # the fixture's check() count: a run that skips a section is not a pass
COST_KEYS = ('visits', 'run_ns', 'hit_ns', 'tiny_run_ns', 'tiny_miss_store_ns', 'tiny_hit_ns', 'miss_store_overhead_ns')
SCENARIOS = ('static', 'one_step', 'approach', 'modes', 'running_minimum', 'addresses', 'expiry', 'overflow', 'random', 'guards', 'verify', 'advance')


def _fields(line):
    return {k: (float(v) if '.' in v else int(v)) for k, v in re.findall(r'(\w+)=([\d.]+)(?=\s|$)', line)}


def parse(stdout):
    lines = stdout.splitlines()
    total = re.search(r'COLLIDE MEMO CPU checks=(\d+) failures=(\d+)', stdout)
    first = lambda prefix: next((l for l in lines if l.startswith(prefix)), '')
    summary = _fields(first('SUMMARY '))
    scenarios = {m.group(1): _fields(m.group(2)) for m in (re.match(r'SCENARIO (\w+) (.*)$', l) for l in lines) if m}
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
            'summary': summary if all(k in summary for k in SUMMARY_KEYS) else None, 'scenarios': scenarios, 'bench': _fields(first('COLLIDE MEMO BENCH ')) or None,
            'approach': _fields(first('APPROACH ')), 'minimum': _fields(first('MINIMUM ')), 'misses': _fields(first('MISSES ')), 'advance': _fields(first('ADVANCE ')), 'advance_cost': _fields(first('ADVANCE_COST ')), 'advance_deep': _fields(first('ADVANCE_DEEP ')),
            'advance_runs': {m.group(1): _fields(m.group(2)) for m in (re.match(r'ADVANCE_RUN name=(\w+) (.*)$', l) for l in lines) if m}, 'verify': _fields(first('VERIFY ')), 'window_line': first('WINDOW ')[7:] or None,
            'install_lines': [l for l in lines if l.startswith(('collide_memo requested', 'collide_sat_sse2 '))][:8], 'failure_lines': [l for l in lines if l.startswith(('FAIL', 'DETAIL'))][:40]}


def accepted(record):
    summary = record.get('summary')
    advance = record.get('advance') or {}
    if advance.get('mismatches') != 0 or not advance.get('total_answers') or not record.get('advance_cost'):
        return False
    return bool(record.get('exit_status') == 0 and record.get('failures') == 0 and record.get('checks') == EXPECTED_CHECKS and summary and all(summary[k] == 0 for k in MUST_BE_ZERO) and summary['hits'] > 0
                and summary['contacts'] > 0 and set(record.get('scenarios', {})) == set(SCENARIOS) and record.get('bench') and all(k in record['bench'] for k in COST_KEYS))


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    build = None
    if '--no-build' not in sys.argv[1:]:
        import build_collide_memo
        build = build_collide_memo.build()
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=3000)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1), **parse(run.stdout), 'bottle': bottle.describe(name),
              'build': {k: build[k] for k in ('engine', 'audit')} if build else None,
              'note': 'run_ns is one un-memoed query of the cost pair, hit_ns one answered query, harness included; diagnostic timings, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'elapsed_s', 'summary', 'scenarios', 'bench', 'approach', 'minimum', 'misses', 'advance', 'advance_cost', 'advance_deep', 'advance_runs', 'verify', 'failure_lines')}))
    if run.returncode != 0 and not record['checks']:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if accepted(record) else 1)


if __name__ == '__main__':
    main()
