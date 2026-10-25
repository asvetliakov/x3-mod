#!/usr/bin/env python3
"""Summary of strength_census.py output (docs/architecture/overlay-material-strength.md, Q2 range and Q7 draws).
Usage: python3 strength_census_summary.py OUT.tsv"""
import sys
from collections import Counter

bodies, classes, errors = [], [], []
for line in open(sys.argv[1]):
    f = line.rstrip('\n').split('\t')
    if f[0] == 'BODY':
        bodies.append((f[1],) + tuple(int(x) for x in f[2:]))
    elif f[0] == 'CLASS':
        bodies_name, eff, cull, bump, n, ns, faces, mean, smax, rmin, rmax, minmax = f[1:]
        classes.append((bodies_name, eff, cull, bump, int(n), int(ns), int(faces), float(mean), float(smax),
                        float(rmin), float(rmax), float(minmax)))
    elif f[0] == 'ERROR':
        errors.append(f[1:])
print('bodies', len(bodies), 'errors', len(errors), errors[:5])
r0 = sum(b[1] for b in bodies)
d2, d4, d3 = (sum(b[i] for b in bodies) for i in (2, 3, 4))
c2, c4, c3 = (sum(b[i] for b in bodies) for i in (5, 6, 7))
a2, a4 = (sum(b[i] for b in bodies) for i in (8, 9))
print(f'C draws: K2 (pre-fix) {d2}, K4 (bef0d269) {d4}, K3 (this design) {d3}; record 0 drawn {r0}')
print(f'opaque classes K2/K4/K3 {c2}/{c4}/{c3}; alpha groups K2 {a2} -> per material {a4}')
ng4 = [b for b in bodies if b[3] >= b[1]]
ng3 = [b for b in bodies if b[4] >= b[1]]
print(f'no_draw_gain bodies (draws >= record 0): K4 {len(ng4)}, K3 {len(ng3)}: {[b[0] for b in ng3]}')
print('bodies whose K3 draws exceed K2:', sum(1 for b in bodies if b[4] > b[2]),
      'max increase', max((b[4] - b[2], b[0]) for b in bodies))
print(f'multi-strength K3 classes {len(classes)} in {len({c[0] for c in classes})} bodies, '
      f'faces {sum(c[6] for c in classes)}')
rmax = sorted(c[10] for c in classes)
rmin = sorted(c[9] for c in classes)
print('rmax (s_i / s_mean) percentiles 50/90/99/max:', [round(rmax[int(len(rmax) * q)] if q < 1 else rmax[-1], 3)
                                                      for q in (0.5, 0.9, 0.99, 1)])
print('rmin (s_i / s_mean) percentiles 50/10/1/min:', [round(rmin[int(len(rmin) * q)], 3) for q in (0.5, 0.1, 0.01, 0)])
for k in (1.5, 2.0, 3.0, 4.0):
    over = [c for c in classes if c[10] > k]
    print(f'classes with rmax > {k}: {len(over)} ({sum(c[6] for c in over)} faces)',
          sorted({c[0] for c in over})[:6] if k >= 2 else '')
print('classes with max ratio under s_merged = max(mean, s_max/2) exceeding 2:', sum(1 for c in classes if c[8] / max(c[7], c[8] / 2) > 2.0000001))
minmax = sorted(c[11] for c in classes)
print('min s_i / s_max percentiles 50/10/1/min:', [round(minmax[int(len(minmax) * q)], 3) for q in (0.5, 0.1, 0.01, 0)])
print('n strengths per class:', sorted(Counter(c[5] for c in classes).items()))
print('effects of multi-strength classes:', Counter((c[1], c[3]) for c in classes).most_common())
print('cull of multi-strength classes:', Counter(c[2] for c in classes).most_common())
