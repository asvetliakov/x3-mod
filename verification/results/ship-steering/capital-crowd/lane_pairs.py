"""Run158 gate 2: capital-vs-capital detour rows classified by the amendment-16 lane footprint.
Usage: LC_ALL=C grep -aE '^(ship_trace frame=|ship_avoid_detour)' /mnt/c/X3/x3m.log > rows.txt; python3 -I lane_pairs.py rows.txt
Gate 0x8f875810: G (m) and F from capital_timeline.txt (F = +z, the grid's a axis = +x, b = 0 for all five slots).
Per detour row whose own ship AND obstacle are traced capitals in the same frame (+-3 frames): each ship's lateral
position in the gate's x-y plane, its nose angle off F, the lateral distance between the centres, and the lane
footprint gap = lateral distance - ext_A - ext_B with ext = w + r sin(theta) for w = k r, k in {0.15, 0.25, 0.35}
(the hull half-width is not known from any note: inferred), and ext = r (the sphere) for a ship off its lane
(|lateral - lane| > 100 m or theta > 8.11 deg). Rows with an untraced side are counted separately."""
import re, sys, math, collections as C
G = (4259.0, -18037.0, 31134.0)
LANE = {'0x8f43c4a8': 0, '0x8f88cbf0': 666, '0xa4c06c10': 0, '0x3a2c2c30': 1332, '0x8f893d48': 666}
R = {'0x8f43c4a8': 1022, '0x8f88cbf0': 1092, '0xa4c06c10': 1092, '0x3a2c2c30': 755, '0x8f893d48': 755, '0x3a2b1b30': 1022}
W = (60000, 77000)
tr = {}  # (obj, frame) -> (lat_x, lat_y, theta_deg)
det = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.search(r'frame=(\d+) t_ms=\d+ obj=(\S+) class=(\S+) .*? pos=(\S+) fwd=(\S+)', l)
        f = int(m.group(1)); o = m.group(2)
        if o not in LANE or not W[0] <= f <= W[1] or m.group(3) not in ('M1', 'M2'): continue
        p = [float(v) / 500 for v in m.group(4).split(',')]; fw = [float(v) for v in m.group(5).split(',')]
        th = math.degrees(math.acos(max(-1.0, min(1.0, fw[2]))))
        tr[(o, f)] = (p[0] - G[0], p[1] - G[1], th)
    elif l.startswith('ship_avoid_detour'):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if W[0] <= f <= W[1] and d['obj'] in R and d['obstacle'] in R: det.append((f, d['obj'], d['obstacle'], float(d['miss_m']), float(d['C_m'])))
def near(o, f):
    for k in (0, -1, 1, -2, 2, -3, 3):
        if (o, f + k) in tr: return tr[(o, f + k)]
    return None
PAIR = C.Counter(); gaps1 = {k: [] for k in (0.15, 0.25, 0.35)}; cls = C.Counter(); gaps = {k: [] for k in (0.15, 0.25, 0.35)}; lat_d = []; inlane = C.Counter()
for f, a, b, miss, Cm in det:
    sa, sb = near(a, f), near(b, f)
    if not sa and not sb: cls['neither traced'] += 1; continue
    if not sa or not sb:
        s, o = (sa, a) if sa else (sb, b)
        on = abs(s[0] - LANE[o]) <= 100 and abs(s[1]) <= 100 and s[2] <= 8.11
        cls['one traced: traced side %s' % ('in lane' if on else 'off lane')] += 1
        other = b if sa else a
        if other not in LANE: cls['  of which the other is the untraced M1 (lane unknown)'] += 1; continue
        # footprint of the traced side: the lateral segment from its position to its lane, inflated by r off the lane (by
        # k r + r sin(theta) in it); the untraced side assumed in its lane (an upper bound on what the rule could exempt)
        lo, hi = min(s[0], LANE[o]), max(s[0], LANE[o])
        for k in gaps1:
            e_s = (k * R[o] + R[o] * math.sin(math.radians(s[2]))) if on else R[o]
            gx = max(LANE[other] - hi, lo - LANE[other], 0.0)
            g = math.hypot(gx, s[1]) - e_s - k * R[other]
            gaps1[k].append(g)
            if k == 0.25: PAIR[(o, other, g >= 50)] += 1
        continue
    cls['both traced'] += 1
    dl = math.hypot(sa[0] - sb[0], sa[1] - sb[1]); lat_d.append(dl)
    on = [abs(s[0] - LANE[o]) <= 100 and abs(s[1]) <= 100 and s[2] <= 8.11 for s, o in ((sa, a), (sb, b))]
    inlane[tuple(on)] += 1
    for k in gaps:
        ext = [(k * R[o] + R[o] * math.sin(math.radians(s[2]))) if ok else R[o] for s, o, ok in ((sa, a, on[0]), (sb, b, on[1]))]
        gaps[k].append(dl - ext[0] - ext[1])
print('cap-vs-cap detour rows in window', len(det), dict(cls))
print('both traced: (own in lane, obstacle in lane) ->', dict(inlane))
q = lambda v: 'n=%d min=%.0f med=%.0f max=%.0f' % (len(v), min(v), sorted(v)[len(v) // 2], max(v)) if v else 'n=0'
print('lateral centre distance m', q(lat_d))
for k, v in gaps1.items(): print('one traced, other assumed in lane: footprint gap m, w = %.2f r:' % k, q(v), ' exempt (gap >= 50 m):', sum(g >= 50 for g in v))
for k, v in gaps.items(): print('footprint gap m, w = %.2f r:' % k, q(v), ' exempt (gap >= 50 m):', sum(g >= 50 for g in v))
print('one traced (traced, other, exempt at w = 0.25 r):')
for k, n in sorted(PAIR.items()): print('  ', *k, n)
