#!/usr/bin/env python3
"""A' only, review item F1: the selected-case rerun on the final DLL (after the box-target edit;
verification/results/bottle-X3/motion-output-partial.json) against the full run of the same change (a saved copy of that
run's motion-output-summary.json): every rerun case identical in every non-clock leaf, and the twins' colour hashes equal
(seam-taa-thin-taps16-refused = seam-taa-taps16, seam-taa-region-hold-ignored = seam-taa-on).
usage: compare_partial.py FULL_SUMMARY.json"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RUN_BOUND = {'directory', 'dll_sha256', 'exe_sha256', 'trace_sha256'}
# Written by the full run's cross-case comparisons only (seam-taa-on against its off twin); a partial run has none.
CROSS_CASE = {'frames_changed_by_history'}
CLOCK = re.compile(r'(^|_)(us|ms|ns)$|_us_|_ms_|bench|ticks|elapsed|seconds|^us$|costs_us')
TWINS = {'seam-taa-thin-taps16-refused': 'seam-taa-taps16', 'seam-taa-region-hold-ignored': 'seam-taa-on'}


def leaves(value, path=()):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from leaves(item, path + (str(key),))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from leaves(item, path + (str(index),))
    else:
        yield path, value


full = json.loads(Path(sys.argv[1]).read_text())['cases']
partial = json.loads((ROOT / 'verification/results/bottle-X3/motion-output-partial.json').read_text())
bad = 0
for name, case in partial['cases'].items():
    mine, theirs = dict(leaves(case)), dict(leaves(full[name]))
    differing = sorted('/'.join(p for p in path if not p.isdigit()) for path, value in theirs.items()
                       if path[0] not in RUN_BOUND | CROSS_CASE and not any(CLOCK.search(part) for part in path) and mine.get(path, object()) != value)
    bad += bool(differing)
    print(f'{name}: exit={case.get("exit")} non_clock_identical_to_full_run={not differing} {differing[:6]} dll_sha256={case.get("dll_sha256", "")[:16]}')
for a, b in TWINS.items():
    same = partial['cases'][a]['color_hashes'] == partial['cases'][b]['color_hashes'] and \
        partial['cases'][a]['color_hashes_before_boundary'] == partial['cases'][b]['color_hashes_before_boundary']
    bad += not same
    print(f'twin {a} = {b}: colour hashes identical={same}')
print(f'RESULT {"PASS" if not bad else "FAIL"} cases={len(partial["cases"])} status={partial.get("status")}')
