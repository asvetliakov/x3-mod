#!/usr/bin/env python3
"""The shock front's ring cost (docs/verification/weapon-effects.md "Shock front"): the ring-only and the whole-class
CLASS_TIMING rows of the heavy, plasma and missile classes and the ring that fills the frame, from one or two
run_weapon_effects.py --host native records (before, after), with the after/before ratio.
The before record is the committed summary at 55dc676bd (not tracked here):
  git show 55dc676bd:verification/results/native-windows/weapon-effects/summary.json > before-summary.json
usage: python3 verification/results/weapon-effects-shock-front/ring_cost.py BEFORE.json [AFTER.json]"""
import json
import sys


def rows(path):
    s = json.load(open(path))
    out = {}
    for r in s['metrics']['class_timing']:
        if 'gpu_us' not in r:
            continue
        if r.get('class') in ('heavy', 'plasma', 'missile') or r['case'].startswith('ring_alone'):
            out[(r['width'], r.get('class', '-'), r['case'])] = (r.get('rings'), r['gpu_us'])
    return s['source']['commit'], s['passed'], out


before = rows(sys.argv[1])
after = rows(sys.argv[2]) if len(sys.argv) > 2 else None
print('before', before[0], 'passed', before[1], *(('after', after[0], 'passed', after[1]) if after else ()))
for key in sorted(before[2]):
    b = before[2][key]
    a = after[2].get(key) if after else None
    tail = f'  after rings={a[0]} {a[1]:7.1f} us  x{a[1] / b[1]:.2f}' if a and b[1] else ''
    print(f'{key[0]:>5} {key[1]:<8} {key[2]:<36} rings={b[0]} {b[1]:7.1f} us{tail}')
