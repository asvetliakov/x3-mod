#!/usr/bin/env python3
"""The existing fade-route cases and seam-taa-cutout-opaque after the alpha-tested fade cutout change: every one of
them in the partial run (verification/results/bottle-X3/motion-output-partial.json) against its record in the
committed summary (motion-output-summary.json at [rev], default HEAD). Every recorded leaf must be identical except
run-bound ones (binary / trace hashes, directories) and wall-clock measurements (leaf names ending in _us / _ms / _ns /
us / ns or containing 'bench', 'ticks', 'elapsed', 'seconds'). Also prints the new cases' headline numbers.
usage: compare_fade_cases.py [rev]"""
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SUMMARY = 'verification/results/bottle-X3/motion-output-summary.json'
PARTIAL = ROOT / 'verification/results/bottle-X3/motion-output-partial.json'
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
old = json.loads(subprocess.run(['git', 'show', f'{rev}:{SUMMARY}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout)
new = json.loads(PARTIAL.read_text())
NEW = [n for n in new['cases'] if n.startswith('seam-taa-fade-route-cutout-')]
selected = [n for n in new['cases'] if n not in NEW]
missing = [n for n in selected if n not in old['cases']]
clock, other = collections.Counter(), collections.Counter()
identical = 0
for name in selected:
    if name in missing:
        continue
    mine = dict(leaves(new['cases'][name]))
    theirs = dict(leaves(old['cases'][name]))
    same = True
    for path in set(mine) | set(theirs):
        if path[0] in RUN_BOUND or mine.get(path, object()) == theirs.get(path, object()):
            continue
        same = False
        timed = any(CLOCK.search(part) for part in path)
        (clock if timed else other)[name + ':' + '/'.join(p for p in path if not p.isdigit())] += 1
    identical += same
checks = {n: new['cases'][n].get('checks') for n in selected}
print(f'compared={len(selected) - len(missing)} missing_from_committed={missing} identical_including_clock={identical}')
print(f'non_clock_leaves_differing={dict(other)}')
print(f'clock_leaves_differing={len(clock)}')
print(f'checks_total_existing={sum(v for v in checks.values() if v)}')
for name in NEW:
    case = new['cases'].get(name)
    if case:
        print(f'{name}: checks={case["checks"]} variant={case["variant"]} mip_bias={case["mip_bias"]:g} fade_routed/frame={case["fade_routed_per_frame"]} '
              f'fade_tested/frame={case["fade_tested_per_frame"]} model={case["model"]} owned_px/frame={case["panel_owned_per_frame"]}+{case["hull_owned_per_frame"]} '
              f'max_depth_error={case["max_depth_error"]:.3g} jittered={sorted(set(case["jittered"].values()))} color_identical_to_twin={case.get("color_identical_to_twin")}')
print(f'passed={new.get("passed")} status={new.get("status")}  (PARTIAL is the runner\'s status for a selected-case subset, not a failure)')
sys.exit(0 if not other and not missing else 1)
