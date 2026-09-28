#!/usr/bin/env python3
"""Union screen box per frame of argon_spacedock and of the far military_outpost (zmin > 100k) from bounds_names_out.txt."""
import re, collections
B = collections.defaultdict(lambda: [9999, 9999, 0, 0, 1e9, 0])
for l in open('bounds_names_out.txt'):
    m = re.match(r'frame (\d+) node \S+ box (\d+),(\d+)-(\d+),(\d+) z (\d+)-(\d+) .* (\S+)$', l)
    if not m: continue
    f, x0, y0, x1, y1, z0, z1 = map(int, m.groups()[:7]); n = m.group(8)
    k = 'spacedock' if 'spacedock' in n else 'far_outpost' if 'outpost' in n and z0 > 100000 else None
    if not k or x1 - x0 >= 1900: continue
    b = B[(f, k)]; b[:] = [min(b[0], x0), min(b[1], y0), max(b[2], x1), max(b[3], y1), min(b[4], z0), max(b[5], z1)]
for (f, k), b in sorted(B.items()): print(f, k, 'box %d,%d-%d,%d z %d-%d' % tuple(b))
