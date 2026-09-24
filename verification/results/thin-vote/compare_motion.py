#!/usr/bin/env python3
"""The option-off path against the committed motion-output summary: every case of the committed summary is still
present and every recorded leaf value is identical except run-bound ones: binary / trace hashes and directories, and
wall-clock measurements (leaf names ending in _us / _ms / _ns / us / ns or containing 'bench', 'ticks', 'elapsed',
'seconds'). Prints the leaf names that differ (all must be wall-clock) and a total line.
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
for name, case in old['cases'].items():
    if name not in new['cases']:
        print(f'MISSING {name}'); continue
    compared += 1
    mine = dict(leaves(new['cases'][name]))
    same = True
    for path, value in leaves(case):
        if path[0] in RUN_BOUND or mine.get(path, object()) == value:
            continue
        same = False
        timed = any(CLOCK.search(part) for part in path)
        (clock if timed else other)['/'.join(p for p in path if not p.isdigit())] += 1
    identical += same
added = sorted(set(new['cases']) - set(old['cases']))
print(f'non_clock_leaves_differing={dict(other)}')
print(f'clock_leaves_differing={len(clock)} names={sorted(clock)[:12]}')
print(f'committed_cases={len(old["cases"])} compared={compared} identical_including_clock={identical} '
      f'bench_committed={len(old.get("bench", {}))} bench_present={len(set(old.get("bench", {})) & set(new.get("bench", {})))} '
      f'new_cases={added} passed={new.get("passed")} status={new.get("status")}')
