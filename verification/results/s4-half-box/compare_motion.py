#!/usr/bin/env python3
"""(From verification/results/aprime-only.) S4 half-resolution box (X3M_TAA_BOX_RESOLUTION, opt-in; 2026-09-24): the full
motion-output run with the option at its default (full, pinned by the runner) against the committed summary. Every committed
case is still present and every recorded leaf value is identical except run-bound ones: binary / trace hashes and
directories, and wall-clock measurements (leaf names ending in _us / _ms / _ns / us / ns or containing 'bench', 'ticks',
'elapsed', 'seconds'). Prints missing and added cases, the cases whose non-clock leaves differ (with the leaf names) and a
total line.
usage: compare_motion.py [baseline-rev]   (default HEAD; reads verification/results/bottle-X3/motion-output-summary.json)"""
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATH = 'verification/results/bottle-X3/motion-output-summary.json'
RUN_BOUND = {'directory', 'dll_sha256', 'exe_sha256', 'trace_sha256'}
CLOCK = re.compile(r'(^|_)(us|ms|ns)$|_us_|_ms_|bench|ticks|elapsed|seconds|^us$|costs_us')
REMOVED = set()  # no case was removed by this change


def leaves(value, path=()):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from leaves(item, path + (str(key),))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from leaves(item, path + (str(index),))
    else:
        yield path, value


rev = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
old = json.loads(subprocess.run(['git', 'show', f'{rev}:{PATH}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout)
new = json.loads((ROOT / PATH).read_text())
compared = identical = 0
clock, other = collections.Counter(), collections.Counter()
differing_cases = {}
for name, case in old['cases'].items():
    if name not in new['cases']:
        print(('REMOVED ' if name in REMOVED else 'MISSING ') + name); continue
    compared += 1
    mine = dict(leaves(new['cases'][name]))
    same = True
    for path, value in leaves(case):
        if path[0] in RUN_BOUND or mine.get(path, object()) == value:
            continue
        same = False
        timed = any(CLOCK.search(part) for part in path)
        leaf = '/'.join(p for p in path if not p.isdigit())
        (clock if timed else other)[leaf] += 1
        if not timed:
            differing_cases.setdefault(name, set()).add(leaf)
    identical += same
added = sorted(set(new['cases']) - set(old['cases']))
for name, names in sorted(differing_cases.items()):
    print(f'NON_CLOCK_DIFF {name}: {sorted(names)[:8]}')
print(f'non_clock_leaves_differing={dict(other)}')
print(f'clock_leaves_differing={len(clock)} names={sorted(clock)[:12]}')
print(f'committed_cases={len(old["cases"])} compared={compared} identical_including_clock={identical} '
      f'non_clock_identical={compared - len(differing_cases)} '
      f'bench_committed={len(old.get("bench", {}))} bench_present={len(set(old.get("bench", {})) & set(new.get("bench", {})))} '
      f'new_cases={added} cases_now={len(new["cases"])} passed={new.get("passed")} status={new.get("status")}')
