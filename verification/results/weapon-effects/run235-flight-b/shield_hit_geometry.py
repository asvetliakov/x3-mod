"""Run235 Flight B (weapon_effects=shields): per logged shield hit, where the stage puts the patch.

The stage (src/proxy/weapon_effects_stage.h) uses a sphere of radius obj+0xa4 centred on the root node origin
(shell_geometry: axes = unit rows x radius) and places each hit at radius x normalize(pos) (hit_direction), pos being
the engine's contact point in the owner frame. Prints per owner |pos|/R, the anchor, and the radial gap R - |pos|.
Usage: python3 shield_hit_geometry.py /mnt/c/X3/x3m.log
"""
import math, re, sys, collections
ANCH = ['+z', '-z', '+x', '-x', '+y', '-y']
def anchor(d):
    ax = [(d[2], 0), (-d[2], 1), (d[0], 2), (-d[0], 3), (d[1], 4), (-d[1], 5)]
    return ANCH[max(ax)[1]]
own = collections.defaultdict(list)
for line in open(sys.argv[1], errors='replace'):
    if not line.startswith('weapon_event'):
        continue
    d = dict(re.findall(r'(\w+)=(\S+)', line))
    if d.get('role') != 'shield_hit':
        continue
    p = [int(v) for v in d['pos'].split(',')]
    own[(d['owner'], int(d['radius']), d['patch_tier'], d['tier_source'])].append((int(d['frame']), p))
for (o, R, tier, src), hits in own.items():
    print(f'owner={o} radius={R} ({R/500:.0f} m) tier={tier}/{src} hits={len(hits)} frames={hits[0][0]}..{hits[-1][0]}')
    fr, gaps, anc = [], [], collections.Counter()
    for f, p in hits:
        n = math.sqrt(sum(c * c for c in p))
        u = [c / n for c in p]
        a = anchor(u)
        anc[a] += 1
        fr.append(n / R)
        gaps.append((R - n) / 500)
        dom = max(abs(u[0]), abs(u[1]), abs(u[2]))
        print(f'  f={f} pos={p} |pos|/R={n/R:.3f} gap_m={(R-n)/500:.0f} anchor={a} dom_cos={dom:.2f}'
              f' angle_to_axis_deg={math.degrees(math.acos(dom)):.0f}')
    print(f'  |pos|/R min={min(fr):.3f} median={sorted(fr)[len(fr)//2]:.3f} max={max(fr):.3f};'
          f' radial gap m min={min(gaps):.0f} max={max(gaps):.0f}; anchors={dict(anc)}')
