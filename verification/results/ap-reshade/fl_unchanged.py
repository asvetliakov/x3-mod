#!/usr/bin/env python3
"""FL unchanged by the AP re-shade port (docs/verification/weapon-effects.md, "Re-shade on AP"): the committed native
weapon-effects records (git HEAD) against the working tree's, after
`run_weapon_effects.py --host native`.

- hook-fl-summary.json: the FL hook fixture's checks (name and verdict) and its muzzle row, compared whole.
- summary.json: every RESHADE and MUZZLE_RESHADE row of the D3D fixture keyed by case (and row / index / name when
  present), timing fields excluded (keys holding `us`, `ms` or `ticks`), the cases new with the AP port
  (`lone_classic_ap*`) listed apart; the D3D checks' verdicts.
- hook-summary.json: the AP hook fixture's checks before and after (the new ones listed).

  python3 -I verification/results/ap-reshade/fl_unchanged.py > verification/results/ap-reshade/fl_unchanged_out.txt
Prints names and counts only.
"""
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
REC = 'verification/results/native-windows/weapon-effects/'
TIMING = re.compile(r'(^|_)(us|ms|ticks)(_|$)|_us\b|us_')


def old(name):
    return json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', f'HEAD:{REC}{name}'], capture_output=True,
                                     text=True, check=True).stdout)


def new(name):
    return json.loads((ROOT / REC / name).read_text())


def checks(r):
    return {c['name'] if isinstance(c, dict) else c[0]: (c['passed'] if isinstance(c, dict) else c[1])
            for c in r['checks']}


def key(row):
    return (row.get('case'),) + tuple(str(row[k]) for k in ('row', 'index', 'name', 'depth', 'which', 'pose', 'subject')
                                      if k in row)


def rows(r, group):
    out = {}
    for row in r['metrics'].get(group) or []:
        out.setdefault(key(row), []).append({k: v for k, v in row.items() if not TIMING.search(k)})
    return out


def main():
    a, b = old('hook-fl-summary.json'), new('hook-fl-summary.json')
    ca, cb = checks(a), checks(b)
    print(f'FL hook: checks {len(ca)} -> {len(cb)}, same names and verdicts: {ca == cb}, all pass: {all(cb.values())}, '
          f'muzzle row equal: {a.get("muzzle") == b.get("muzzle")}, passed: {a.get("passed")} -> {b.get("passed")}')
    a, b = old('hook-summary.json'), new('hook-summary.json')
    ca, cb = checks(a), checks(b)
    print(f'AP hook: checks {len(ca)} -> {len(cb)}, kept checks unchanged: {all(cb.get(k) == v for k, v in ca.items())}, '
          f'all pass: {all(cb.values())}, new: {sorted(set(cb) - set(ca))}')
    a, b = old('summary.json'), new('summary.json')
    ca, cb = checks(a), checks(b)
    print(f'D3D: checks {len(ca)} -> {len(cb)}, kept checks unchanged: {all(cb.get(k) == v for k, v in ca.items())}, '
          f'all pass: {all(cb.values())}, new: {sorted(set(cb) - set(ca))}')
    for group in ('reshade', 'muzzle_reshade'):
        ra, rb = rows(a, group), rows(b, group)
        new_keys = sorted(k for k in rb if k not in ra)
        gone = sorted(k for k in ra if k not in rb)
        same = [k for k in ra if k in rb and ra[k] == rb[k]]
        differ = [k for k in ra if k in rb and ra[k] != rb[k]]
        print(f'{group}: {len(ra)} row keys before, {len(rb)} after; identical (timing excluded) {len(same)}, '
              f'differing {len(differ)}, gone {len(gone)}, new {[k[0] for k in new_keys]}')
        for k in differ:
            for x, y in zip(ra[k], rb[k]):
                diff = {f: (x.get(f), y.get(f)) for f in sorted(set(x) | set(y)) if x.get(f) != y.get(f)}
                if diff:
                    print(f'  {k}: {diff}')


if __name__ == '__main__':
    main()
