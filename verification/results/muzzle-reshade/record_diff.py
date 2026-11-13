"""The muzzle effects-pair step's D3D record against the base's (docs/verification/weapon-effects.md "Muzzle re-shade:
the effects-pair body 12009"): the existing metric rows (bullet re-shade, classic muzzle, lathe, hull hit) without their
timing fields, which are measured per run, row by row; the checks the two records share with their verdicts; the new
checks and the new MUZZLE_EFFECTS rows.

    python3 -I verification/results/muzzle-reshade/record_diff.py BEFORE.json \
        verification/results/native-windows/weapon-effects/summary.json
BEFORE: the base's committed summary.json (`reshade` 5613593e2), e.g. from `git show 5613593e2:<path>`.
"""
import json
import re
import sys

TIMING = re.compile(r'(^|_)(us|ms|ticks|gpu)(_|$)|_us_|_us$|us_per|us_timed|us_frame')


def strip(row):
    return {k: v for k, v in row.items() if not TIMING.search(k)}


def main(before_path, after_path):
    before, after = json.load(open(before_path)), json.load(open(after_path))
    for key in ('reshade', 'muzzle_reshade', 'lathe', 'hull_reshade'):
        a = [strip(r) for r in before['metrics'].get(key) or []]
        b = [strip(r) for r in after['metrics'].get(key) or []]
        same = sum(1 for x, y in zip(a, b) if x == y)
        differing = [(x.get('case'), sorted(k for k in set(x) | set(y) if x.get(k) != y.get(k)))
                     for x, y in zip(a, b) if x != y]
        print(f'{key}: rows before {len(a)} after {len(b)} identical {same} differing {differing}')
    checks_a, checks_b = dict(before['checks']), dict(after['checks'])
    shared = set(checks_a) & set(checks_b)
    changed = sorted(k for k in shared if checks_a[k] != checks_b[k])
    new = sorted(set(checks_b) - set(checks_a))
    print(f'checks before {len(checks_a)} after {len(checks_b)} shared {len(shared)} verdict_changed {changed}')
    print(f'new checks {[(k, checks_b[k]) for k in new]}')
    print(f'gone checks {sorted(set(checks_a) - set(checks_b))}')
    print(f'muzzle_effects rows {len(after["metrics"].get("muzzle_effects") or [])}; '
          f'passed before {before["passed"]} after {after["passed"]}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
