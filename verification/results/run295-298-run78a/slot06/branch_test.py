"""Slot 06 LOD switch: which LOD branch of 0x0047cfe0 explains each cull_census row.
For every census row carrying a ladder (lods/thr) and a body name, compute
  loop  = clamp(highest i in 1..n-1 with s < T_i (f = 1, no X3M_LOD_SCALE) minus 1, 0, n-1)   (Very High tail)
  dist  = distance branch 0047d36d..0047d427: k = 4/3/2/1/0 for D-R > 28.5M/21M/15.5M/7M, clamp n-1,
          k-1 if T_k < 2; the point-count guard (k-1 if points_k < points_{k-1}/3) is not observable here, so
          dist_hi = clamp(k-1) (no guard), dist_lo = clamp(k-2) (guard fired). R is the subtree radius 0x00488170,
          not logged; the census radius (node+0xa0) is used as a proxy (inferred).
and compare with the logged lod. Bodies are classed race18 when they are a part of a scene named by a TDocks/TFactories
row with race field [13] == 18 (row+0x5c, the 0x00441639 test), from race18_parts.txt (one body per line).
usage: branch_test.py RACE18_PARTS LOG [LOG ...]"""
import re, sys, collections
parts = {l.strip() for l in open(sys.argv[1]) if l.strip()}
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) view=(\w+) node=(\w+) model=(\w+) s=(-?\d+) measure=(-?\d+) d=(-?\d+) radius=(-?\d+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)? lods=(\d+) thr=(\S+) body=(\S+)')
def loop(s, T):
    n = len(T); sel = 0
    for i in range(n - 1, 0, -1):
        if s < T[i]: sel = i; break
    return max(0, min(sel - 1, n - 1))
def dist(D, R, T):
    n = len(T); x = D - R
    k = 4 if x > 28500000 else 3 if x > 21000000 else 2 if x > 15500000 else 1 if x > 7000000 else 0
    k = min(k, n - 1)
    if k > 0 and T[k] < 2: k -= 1; return max(0, k - 1), max(0, k - 1)
    return max(0, k - 2) if k > 0 else 0, max(0, k - 1)
tot = collections.Counter(); ex = collections.defaultdict(list)
for log in sys.argv[2:]:
    run = re.search(r'run(\d+)', log).group(1)
    for line in open(log, errors='replace'):
        if not line.startswith('cull_census device') or ' lods=' not in line: continue
        m = CEN.match(line)
        if not m or m.group(10) != 'kept': continue
        s, D, r, lod, n = int(m.group(5)), int(m.group(7)), int(m.group(8)), int(m.group(9)), int(m.group(11))
        T = [int(v) for v in m.group(12).split(',')]; body = m.group(13).lower().replace('\\', '/')
        if n < 3: continue  # two-LOD bodies always draw 0 in both branches
        lp = loop(s, T); dlo, dhi = dist(D, r, T)
        cls = 'race18' if body in parts else 'other'
        key = (cls, 'loop_ok' if lod == lp else 'loop_x', 'dist_ok' if dlo <= lod <= dhi else 'dist_x')
        tot[key] += 1
        if lod != lp or cls == 'race18':
            if len(ex[key]) < 6: ex[key].append(f'run{run} f{m.group(1)} {body} s={s} D={D} r={r} thr={m.group(12)} lod={lod} loop={lp} dist={dlo}..{dhi}')
print('class loop dist -> rows (kept rows, n>=3, main+other views)')
for k in sorted(tot): print(' ', k, tot[k])
print('examples (loop mismatches, and every race18 key)')
for k in sorted(ex):
    for e in ex[k]: print(' ', k, e)
# second pass: per race18 body, rows and (observed, loop, dist_hi) histograms; and every race18 row with dist_hi > 0
per = collections.defaultdict(collections.Counter); far = []
for log in sys.argv[2:]:
    run = re.search(r'run(\d+)', log).group(1)
    for line in open(log, errors='replace'):
        if not line.startswith('cull_census device') or ' lods=' not in line: continue
        m = CEN.match(line)
        if not m or m.group(10) != 'kept': continue
        body = m.group(13).lower().replace('\\', '/')
        if body not in parts and 'newdock' not in body: continue
        s, D, r, lod = int(m.group(5)), int(m.group(7)), int(m.group(8)), int(m.group(9)); T = [int(v) for v in m.group(12).split(',')]
        if len(T) < 3: continue
        lp = loop(s, T); dlo, dhi = dist(D, r, T)
        per[(body, m.group(12))][(lod, lp, dhi)] += 1
        if dhi > 0 and len(far) < 20: far.append(f'run{run} f{m.group(1)} {body} s={s} D={D} r={r} D-r={D-r} thr={m.group(12)} lod={lod} loop={lp} dist={dlo}..{dhi}')
print('race18 (+newdock) bodies: (observed, loop, dist_hi) -> rows')
for k in sorted(per): print(' ', k[0], 'thr=' + k[1], dict(per[k]))
print('race18 rows with D-r > 7M (dist_hi > 0):', len(far)); [print(' ', f) for f in far]
