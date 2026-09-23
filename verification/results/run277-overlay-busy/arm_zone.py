#!/usr/bin/env python3
"""Run 74 A (run277): per-material face count and area of LOD 0 faces whose centroid lies in the
radial arm zone (sqrt(y^2+z^2) > R, default 10000 body units), i.e. the solar-panel arms outside
the core. Usage: arm_zone.py BODY [R] [XBAND: |x| < XBAND only]"""
import sys, math, collections
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay, body_materials
name = sys.argv[1]; R = float(sys.argv[2]) if len(sys.argv) > 2 else 10000
XB = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
oa, _ = lod_overlay.original_assets(bob1.DEFAULT_GAME)
t = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name)))
mats = bob1.materials(t); alpha = lod_overlay.alpha_materials(mats)
L0 = bob1.lods(t)[0]; P = L0['points']
tot = collections.defaultdict(lambda: [0, 0.0, 0, 0.0, [1e9, -1e9]])
for part in L0['parts']:
    for g in part['groups']:
        for f in g['faces']:
            c = [sum(P[i][k] for i in f[:3]) / 3 for k in (1, 2, 3)]
            a = body_materials.face_area(P, f)
            r = tot[g['material']]; r[0] += 1; r[1] += a
            if math.hypot(c[1], c[2]) > R and abs(c[0]) < XB:
                r[2] += 1; r[3] += a; r[4][0] = min(r[4][0], c[0]); r[4][1] = max(r[4][1], c[0])
za = sum(v[3] for v in tot.values())
print(f'arm zone r_yz > {R:g}: faces {sum(v[2] for v in tot.values())}, area {za:.3e}')
for m, (n, a, zn, zarea, xr) in sorted(tot.items(), key=lambda kv: -kv[1][3]):
    if zn:
        print(f'mat{m:>2} {"A" if m in alpha else "-"} zone faces={zn:5d}/{n:5d} zone area share={zarea / za:.3f} x=[{xr[0]:.0f},{xr[1]:.0f}]')
