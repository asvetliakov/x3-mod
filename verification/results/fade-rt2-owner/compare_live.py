#!/usr/bin/env python3
"""The live distance-fade result (verification/results/bottle-X3/linear-distance-fade-live.json, run_linear_distance_fade_live.py
on the seam DLL with X3M_FADE_RT2_OWNER unset) against the committed one: every leaf identical except run-bound ones (hashes of
binaries, traces and directories, and wall-clock measurements: leaf names ending in _us / _ms / _ns / _s or containing
'seconds', 'elapsed', 'bench', 'ticks', 'timing' values). Prints the differing leaf names by class and a total line.
usage: compare_live.py [baseline-rev | baseline.json]   (default HEAD; a file: the same runner's result on another build,
e.g. main's seam DLL and fixture built from `git archive 4de081ae` and run with --result <file>)"""
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATH = 'verification/results/bottle-X3/linear-distance-fade-live.json'
RUN_BOUND = re.compile(r'sha256|directory|path|dll|exe|trace|log|^raw$')
CLOCK = re.compile(r'(^|_)(us|ms|ns|s)$|seconds|elapsed|bench|ticks|wall|^counts$')  # counts: the timing cases' per-sample milliseconds


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
baseline = Path(rev).read_text() if Path(rev).is_file() else subprocess.run(['git', 'show', f'{rev}:{PATH}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout
old = dict(leaves(json.loads(baseline)))
new = dict(leaves(json.loads((ROOT / PATH).read_text())))
bound, clock, other = collections.Counter(), collections.Counter(), collections.Counter()
for path in sorted(set(old) | set(new)):
    if old.get(path, object()) == new.get(path, object()):
        continue
    name = '/'.join(p for p in path if not p.isdigit())
    if any(RUN_BOUND.search(p) for p in path):
        bound[name] += 1
    elif any(CLOCK.search(p) for p in path):
        clock[name] += 1
    else:
        other[name] += 1
print(f'non_clock_leaves_differing={dict(other)}')
print(f'run_bound_leaves_differing={sum(bound.values())} clock_leaves_differing={sum(clock.values())} names={sorted(clock)[:12]}')
print(f'leaves_committed={len(old)} leaves_now={len(new)} verdict={"identical_except_clock_and_run_bound" if not other else "differs"}')
