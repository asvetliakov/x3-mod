#!/usr/bin/env python3
"""measure.py <dump_dir>: for each 1920x1080 -glow / -burst PFM: peak luma (max RGB), lit pixels above 10 % of peak,
equivalent radius sqrt(A/pi) of the union, connected blobs (4-neighbour) count and median blob equivalent radius, and
for single-hit cases the max lit distance from the frame centre. Step 3 (docs/architecture/explosion-look.md 9): the death
explosion's dump cases (weapon_effects_boom_cases.h, boom_*) write the parts -flash, -body, -shock, -ember, -fragment,
-glow and -smoke the same way; each is measured likewise, with the max lit distance from the centre (one death on the
axis; the nested case's three about it). Section 13: the big tiers' cases write -body (the composed cluster) and -wave
(the fire wave) beside each frame; the wave's max lit distance is its front's reach from the centre."""
import sys, numpy as np
from pathlib import Path

def blobs(m):
    lit = set(zip(*np.nonzero(m)))
    sizes = []
    while lit:
        stack = [lit.pop()]; n = 0
        while stack:
            y, x = stack.pop(); n += 1
            for q in ((y+1, x), (y-1, x), (y, x+1), (y, x-1)):
                if q in lit:
                    lit.remove(q); stack.append(q)
        sizes.append(n)
    return sizes

def read_pfm(p):
    with open(p, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        a = np.fromfile(f, dtype='<f4' if scale < 0 else '>f4').reshape(h, w, 3)
    return np.flipud(a)

rows = []
for p in sorted(Path(sys.argv[1]).glob('*_1920x1080-*-*.pfm')):
    stem = p.stem
    case, rest = stem.split('_1920x1080-')
    ms, part = rest.split('-')
    l = read_pfm(p).max(axis=2)
    peak = float(l.max())
    if peak <= 0:
        rows.append((case, int(ms), part, 0.0, 0, 0.0, 0, 0.0, 0.0)); continue
    m = l > 0.1 * peak
    A = int(m.sum())
    sizes = blobs(m); n = len(sizes)
    med = float(np.sqrt(np.median(sizes) / np.pi)) if n else 0.0
    ys, xs = np.nonzero(m)
    rmax = float(np.hypot(xs - 960, ys - 540).max()) if 'single' in case or case.startswith('boom_') else float('nan')
    rows.append((case, int(ms), part, peak, A, float(np.sqrt(A / np.pi)), n, med, rmax))
rows.sort()
print('case ms part peak lit_px eq_radius_px blobs median_blob_radius_px max_r_from_centre_px')
for r in rows:
    print(f'{r[0]} {r[1]} {r[2]} {r[3]:.4f} {r[4]} {r[5]:.1f} {r[6]} {r[7]:.1f} {r[8]:.1f}')
