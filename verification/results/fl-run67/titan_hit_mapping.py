"""FL run 67 triage: map the Titan's logged shield hits (owner a89e2110, radius 545751) onto the bubble the walk
should build from the scene (titan_scene.py: hull part argon_M2 at z +17329, identity; half-extents 151433 / 166374 /
528422 from LOD max |coord| (the engine's stored extent is >= this); 24 rotated turret parts widen y to -171,594 (turret at y -149,558 + its half-diagonal 22,036)),
x 1.15, with bubble_point's radial map (weapon_effects_bubble.h:695), against the radius sphere (the fallback).
Usage: grep -a 'role=shield_hit' /mnt/c/X3FL/x3m.log | grep -a owner=a89e2110 | python3 -I titan_hit_mapping.py"""
import math, re, sys
S = 1.15
lo = [-151433.0, -171594.0, 17329 - 528422.0]
hi = [151433.0, 166374.0, 17329 + 528422.0]
c = [(a + b) / 2 for a, b in zip(lo, hi)]
e = [(b - a) / 2 * S for a, b in zip(lo, hi)]
print('BUBBLE_INFERRED c', [round(x) for x in c], 'e', [round(x) for x in e], 'clamp r*1.15', round(545751 * S))
def bp(p, c, e):
    q = [(p[i] - c[i]) / e[i] for i in range(3)]
    n = math.sqrt(sum(v * v for v in q))
    return [c[i] + e[i] * q[i] / n for i in range(3)], n
rows = []
for l in sys.stdin:
    p = [int(v) for v in re.search(r' pos=(\S+)', l)[1].split(',')]
    rows.append(p)
def stats(name, c, e):
    d, qs, dz, dlat = [], [], [], []
    side = 0
    for p in rows:
        b, n = bp(p, c, e)
        d.append(math.dist(p, b)); qs.append(n); dz.append(abs(b[2] - p[2]))
        dlat.append(math.hypot(b[0], b[1]) - math.hypot(p[0], p[1]))
    d.sort(); qs.sort(); dz.sort(); dlat.sort()
    m = lambda v: v[len(v) // 2]
    print(name, 'hits', len(rows), '|q| median %.2f min %.2f max %.2f' % (m(qs), qs[0], qs[-1]),
          'move_m median %.0f p90 %.0f max %.0f' % (m(d) / 500, d[int(.9 * len(d))] / 500, d[-1] / 500),
          'along_z_m median %.0f p90 %.0f' % (m(dz) / 500, dz[int(.9 * len(dz))] / 500),
          'lateral_out_m median %.0f p90 %.0f' % (m(dlat) / 500, dlat[int(.9 * len(dlat))] / 500))
stats('WALK_BUBBLE', c, e)
stats('RADIUS_SPHERE', [0, 0, 0], [545751.0] * 3)
side = [p for p in rows if abs(p[2]) < 300000]
print('HITS |z|<300k:', len(side), 'max|x|', max(abs(p[0]) for p in side), 'max|y|', max(abs(p[1]) for p in side))
