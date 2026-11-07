#!/usr/bin/env python3
"""The rows this change did not touch (the explosion CPU rows and the 512-bolt GPU row) in two run_weapon_effects.py
--host native records, with after / before (docs/verification/weapon-effects.md "Ion lightning: more and longer").
usage: python3 verification/results/weapon-effects-ion-lightning/untouched_rows.py BEFORE.json AFTER.json"""
import json
import sys


def rows(path):
    out = {}

    def walk(o, path):
        if isinstance(o, dict):
            if 'median_us' in o or 'gpu_total_us' in o:
                key = path + ':' + str(o.get('case', '')) + (':big' if o.get('big') else '')
                out[key] = {k: o[k] for k in ('median_us', 'us_per_quad', 'gpu_total_us') if k in o}
            for k, v in o.items():
                walk(v, path + '/' + k)
        elif isinstance(o, list):
            for v in o:
                walk(v, path)

    walk(json.load(open(path))['metrics'], '')
    return out


before, after = rows(sys.argv[1]), rows(sys.argv[2])
for k in sorted(set(before) | set(after)):
    b, a = before.get(k, {}), after.get(k, {})
    for f in sorted(set(b) | set(a)):
        x, y = b.get(f), a.get(f)
        ratio = f' x{y / x:.3f}' if x and y else ''
        print(f'{k} {f} {x} -> {y}{ratio}')
