#!/usr/bin/env python3
"""Screen boxes of station objects: object_bounds rows of a frame joined to cull_census body names by node.
View depth from zmin/zmax via z = p32 / (d - p22). Usage: bounds_names.py <dir> <frame>..."""
import glob, re, sys
P22, P32 = 1.00000298, -6.00001812
d = sys.argv[1]; frames = set(sys.argv[2:]); L = glob.glob(f'{d}/session-*.log')[0]
body, rows = {}, []
for line in open(L, errors='replace'):
    if line.startswith('cull_census '):
        m = re.search(r' node=(\S+).* body=(\S+)', line)
        if m: body[m.group(1)] = m.group(2)
    elif line.startswith('object_bounds device=1 frame='):
        f = re.search(r'frame=(\d+)', line).group(1)
        if f in frames: rows.append((f, dict(re.findall(r' (\w+)=(\S+)', line))))
for f, r in rows:
    b = body.get(r['node'], '?')
    if 'station' not in b.lower(): continue
    z0, z1 = (P32 / (float(r[k]) - P22) for k in ('zmin', 'zmax'))
    print(f"frame {f} node {r['node']} box {float(r['sx0']):.0f},{float(r['sy0']):.0f}-{float(r['sx1']):.0f},{float(r['sy1']):.0f} z {z0:.0f}-{z1:.0f} fp {z0/960:.0f}-{z1/960:.0f} u/px {b}")
