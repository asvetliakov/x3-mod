#!/usr/bin/env python3
"""Before/after table of atlas_mip_bleed.py outputs (same body, same source record).

  python3 verification/results/lod-overlay-pilot/atlas_mip_bleed_compare.py <before_out.txt> <after_out.txt>
"""
import re
import sys

NUM = r'(-?[\d.]+)'
ROW = re.compile(r'  mip (\d) .*exhaust content err mean/p95 ' + NUM + '/' + NUM + r'.*edge\+gutter err mean/p95/max '
                 + NUM + '/' + NUM + '/' + NUM + r'.*outside band max RGB ' + NUM + r' luma excess max/mean ' + NUM
                 + '/' + NUM + r'.*other tiles err mean/p95 ' + NUM + '/' + NUM + r' luma excess max/mean ' + NUM
                 + '/' + NUM + r'.*ambiguous texels (\d+)')


def load(path):
    out, key = {}, None
    for line in open(path):
        m = re.match(r'(\S+): atlas (\d+)x\d+ gutter (\d+) source_record (\S+)', line)
        if m:
            key = (m.group(1), m.group(4))
            out.setdefault(key, {})['meta'] = f'{m.group(2)} g{m.group(3)}'
            continue
        m = ROW.match(line)
        if m and key:
            out[key][int(m.group(1))] = [float(x) for x in m.groups()[1:]]
    return out


before, after = load(sys.argv[1]), load(sys.argv[2])
print('body source mip | exhaust content mean err | exhaust edge+gutter mean/max err | outside band luma excess'
      ' max/mean | other tiles luma excess max/mean | ambiguous texels   (before -> after)')
for key in sorted(before.keys() & after.keys()):
    b, a = before[key], after[key]
    print(f'{key[0]} source {key[1]} atlas {b["meta"]} -> {a["meta"]}')
    for lv in range(5):
        if lv in b and lv in a:
            x, y = b[lv], a[lv]
            print(f'  mip {lv} | {x[0]:.2f} -> {y[0]:.2f} | {x[2]:.1f}/{x[4]:.0f} -> {y[2]:.1f}/{y[4]:.0f} |'
                  f' {x[6]:.1f}/{x[7]:.2f} -> {y[6]:.1f}/{y[7]:.2f} | {x[10]:.1f}/{x[11]:.3f} -> {y[10]:.1f}/{y[11]:.3f} |'
                  f' {x[12]:.0f} -> {y[12]:.0f}')
