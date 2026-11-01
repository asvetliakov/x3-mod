#!/usr/bin/env python3
"""Fade-owner shadow guard (fade-owner-shadow-receiver.md, review F1): routed fade-arm `motion_route` rows by VS,
the count at exactly 1000 per mille (a material draw at full alpha, not a distance fade: never a shadow receiver or
caster, fade_route::owner_shadow_permille_max 999) against the rows at 950-999 and below 950.
The motion_route row carries no overlay field, so overlay-arm rows (admitted at 1000) count in the 1000 column.
Usage: permille_1000_census.py /mnt/c/X3/x3m.log  (streams the log; prints one row per VS and a total)"""
import collections
import sys

rows = collections.defaultdict(lambda: [0, 0, 0, 1001, 0])  # at 1000, 950-999, below 950, min, max
with open(sys.argv[1], 'rb') as log:
    for line in log:
        if not line.startswith(b'motion_route ') or b' fade_arm=1 ' not in line or b' routed=1 ' not in line:
            continue
        f = dict(t.split(b'=', 1) for t in line.split()[1:] if b'=' in t)
        p, r = int(f[b'fade_permille']), rows[f[b'vs'].decode()]
        r[0 if p == 1000 else 1 if p >= 950 else 2] += 1
        r[3], r[4] = min(r[3], p), max(r[4], p)
print('vs at_1000 p950_999 below_950 min max')
for vs, r in sorted(rows.items()):
    print(vs, *r)
print('total', *(sum(r[i] for r in rows.values()) for i in range(3)))
