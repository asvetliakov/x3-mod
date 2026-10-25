#!/usr/bin/env python3
"""Summarise class_census_k3.py TSV: bodies, error rows, total C draws under K2 (installed: effect + occlusion) and under
the option-B key (effect + occlusion + cull; one alpha group per part both ways), opaque classes before/after, alpha
groups, bodies whose classes merge different diffuse strengths (the ratio alpha serves them), and bodies whose C would
no longer draw fewer groups than record 0 (lod_batch_census no_draw_gain). Usage: class_census_k3_summary.py OUT.tsv"""
import sys
from collections import Counter
rows, errors = [], []
for line in open(sys.argv[1]):
    f = line.rstrip('\n').split('\t')
    if f[1] == 'error':
        errors.append(f[0]); continue
    rows.append((f[0],) + tuple(int(x) for x in f[1:]))
print('bodies', len(rows), 'errors', len(errors), errors[:5])
tot = lambda k: sum(r[k] for r in rows)
print(f'C draws (visible groups, all parts) K2 {tot(2)} -> option B {tot(3)} (+{tot(3) - tot(2)}, x{tot(3) / tot(2):.3f});'
      f' record 0 drawn {tot(1)}')
print(f'opaque classes per body before {tot(4)} after {tot(5)}; alpha groups (one per part, both ways) {tot(6)}')
print('bodies with a class merging several diffuse strengths (ratio alpha in use):', sum(1 for r in rows if r[7]),
      'classes', tot(7))
inc = Counter(min(r[3] - r[2], 10) for r in rows)
print('per-body draw increase (10 = 10 or more):', sorted(inc.items()))
print('bodies unchanged:', sum(r[3] == r[2] for r in rows))
lost = [r for r in rows if r[3] >= r[1] > r[2]]
print('bodies that keep a draw gain before but not after (after >= record 0 drawn):', len(lost),
      [(r[0], r[1], r[2], r[3]) for r in lost[:12]])
print('largest increases:', [(r[0], r[2], r[3]) for r in sorted(rows, key=lambda r: r[2] - r[3])[:8]])
