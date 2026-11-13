"""The hull hit re-shade's D3D record against the one before it (docs/verification/weapon-effects.md "Hull hit re-shade,
step 1"): the existing bullet re-shade, muzzle and lathe metric rows unchanged apart from timings, and the checks the
two records share with the same verdict. Timing fields (names containing us, ms, ticks or gpu) are excluded: they are
measured per run.

    python3 -I verification/results/hull-reshade/record_diff.py BEFORE.json \
        verification/results/native-windows/weapon-effects/summary.json
BEFORE: the committed summary.json of the base (`reshade` f18b3d3b1), e.g. from `git show <base>:<path>`.
"""
import json
import re
import sys

TIMING = re.compile(r'(^|_)(us|ms|ticks|gpu)(_|$)|_us_|_us$|us_per|us_timed|us_frame')


def strip(row):
    return {k: v for k, v in row.items() if not TIMING.search(k)}


def main(before_path, after_path):
    before, after = json.load(open(before_path)), json.load(open(after_path))
    for key in ('reshade', 'muzzle_reshade', 'lathe'):
        a = [strip(r) for r in before['metrics'][key]]
        b = [strip(r) for r in after['metrics'][key]]
        same = sum(1 for x, y in zip(a, b) if x == y)
        diffs = sorted({k for x, y in zip(a, b) for k in set(x) | set(y) if x.get(k) != y.get(k)})
        print(f'{key}: rows before {len(a)} after {len(b)} identical {same} differing_keys {diffs}')
    checks_a, checks_b = dict(before['checks']), dict(after['checks'])
    shared = set(checks_a) & set(checks_b)
    changed = sorted(k for k in shared if checks_a[k] != checks_b[k])
    new = sorted(set(checks_b) - set(checks_a))
    print(f'checks before {len(checks_a)} after {len(checks_b)} shared {len(shared)} verdict_changed {changed}')
    print(f'new checks {new}')
    print(f'passed before {before["passed"]} after {after["passed"]}; hull rows {len(after["metrics"].get("hull_reshade") or [])}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
