#!/usr/bin/env python3
"""Per-model draws/LOD of one frame in two logs (object_context rows): run248 stand 8055 vs run250 stand 7357 by default."""
import re, sys
from collections import Counter
def census(log, frame):
    c = Counter(); pat = re.compile(rf'^object_context device=1 frame={frame} .*?\bmodel=(\w+) lod=(\w+)')
    with open(log, errors='replace') as fh:
        for line in fh:
            if line.startswith('object_context') and f' frame={frame} ' in line:
                m = pat.match(line)
                if m: c[(m.group(1), int(m.group(2), 16))] += 1
    return c
a_log, a_f, b_log, b_f = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
A, B = census(a_log, a_f), census(b_log, b_f)
print(f'A frame {a_f}: draws={sum(A.values())}  B frame {b_f}: draws={sum(B.values())}')
am = Counter(); bm = Counter(); al = {}; bl = {}
for (m, l), n in A.items(): am[m] += n; al.setdefault(m, set()).add(l)
for (m, l), n in B.items(): bm[m] += n; bl.setdefault(m, set()).add(l)
rows = sorted(set(am) | set(bm), key=lambda m: -(abs(bm[m] - am[m])))
print('model     A  lodA   B  lodB  delta')
for m in rows:
    d = bm[m] - am[m]
    if d: print(f'{m} {am[m]:4d} {sorted(al.get(m, []))!s:8} {bm[m]:4d} {sorted(bl.get(m, []))!s:8} {d:+d}')
