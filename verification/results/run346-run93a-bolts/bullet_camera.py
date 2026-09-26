#!/usr/bin/env python3
"""For every bullet draw (VS 5e484a06, whose c0-c3 is the view-projection: position-shaders.md) in the chosen frames:
camera position (solve clip x=y=w=0), the bolt_copy bbox relative to the camera, and the bbox corners projected to
screen (x, y px at 5120x1440) with their clip w and z/w.  usage: bullet_camera.py LOG FRAME [FRAME...]"""
import sys, re, struct, itertools
import numpy as np
log, frames = sys.argv[1], set(sys.argv[2:])
kv = re.compile(r'(\w+)=(\S+)')
def fl(h): return struct.unpack('>f', bytes.fromhex(h))[0]
cur = None; rec = None; out = []; bbox = {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_begin'):
            fr = line.split('frame=')[1].split()[0]; cur = fr if fr in frames else None; rec = None; continue
        if cur is None: continue
        k = line.split(' ', 1)[0]
        if k == 'draw':
            d = dict(kv.findall(line)); rec = None
            if d['vs'].startswith('5e484a06'): rec = {'f': cur, 'i': d['index'], 'ps': d['ps'][:8], 'c': {}}; out.append(rec)
        elif k == 'bolt_copy':
            d = dict(kv.findall(line)); bbox[(cur, d['draw'])] = ([float(v) for v in d['bbox'].split(',')], d['revision'])
        elif rec is not None and line.startswith('constant kind=vs type=f reg='):
            r = int(line.split('reg=')[1].split()[0])
            if r < 4: rec['c'][r] = [fl(x) for x in line.split('bits=')[1].split(',')]
for r in out:
    M = np.array([r['c'][i] for i in range(4)])
    cam = np.linalg.solve(M[[0, 1, 3], :3], -M[[0, 1, 3], 3])
    b = bbox.get((r['f'], r['i']))
    s = f"{r['f']} d{r['i']} ps={r['ps']} cam=({cam[0]:.0f},{cam[1]:.0f},{cam[2]:.0f})"
    if b:
        bb, rev = b; lo = np.array(bb[:3]) - cam; hi = np.array(bb[3:]) - cam
        s += f" bbox-cam lo=({lo[0]:.0f},{lo[1]:.0f},{lo[2]:.0f}) hi=({hi[0]:.0f},{hi[1]:.0f},{hi[2]:.0f}) rev={rev}"
        pts = []
        for c in itertools.product((0, 3), (1, 4), (2, 5)):
            cl = M @ np.array([bb[c[0]], bb[c[1]], bb[c[2]], 1.0])
            pts.append((cl[3], cl[2] / cl[3], (cl[0] / cl[3] * .5 + .5) * 5120, (.5 - cl[1] / cl[3] * .5) * 1440))
        w = [p[0] for p in pts]; zw = [p[1] for p in pts if p[0] > 0]
        s += f" w=[{min(w):.0f},{max(w):.0f}] z/w=[{min(zw):.6f},{max(zw):.6f}] sx=[{min(p[2] for p in pts):.0f},{max(p[2] for p in pts):.0f}] sy=[{min(p[3] for p in pts):.0f},{max(p[3] for p in pts):.0f}]"
    print(s)
