#!/usr/bin/env python3
"""Cut-out coverage and bright-source extent of one object per burst frame (same method for every flight).
Coverage: object pixels = routed valid depth in the mask; the filled silhouette = a 3-px morphological closing of them;
hole share = (closed - covered) / closed (cut-out openings that show the background), edge density = covered pixels with
an uncovered 4-neighbour / covered. Bright sources: pre-bloom HDR scene (hdr readback) luma > 1 inside the closed
silhouette: pixel share, 4-connected blob count and mean blob size (px), peak luma p90.
Usage: cutout_bloom.py <capdir> <log> <first> <node | z:LO:HI[:X0:Y0:X1:Y1]>"""
import sys, re, subprocess, numpy as np
W, H = 5120, 1440
cap, log, f0, spec = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
g = lambda pat: subprocess.run(['grep', '-E', pat, log], capture_output=True, text=True).stdout.splitlines()
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
def shift_or(a, r):
    o = a.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            o |= np.roll(np.roll(a, dy, 0), dx, 1)
    return o
def blobs(m):
    ys, xs = np.nonzero(m); seen = set(); sizes = []
    pts = set(zip(ys.tolist(), xs.tolist()))
    for p in pts:
        if p in seen: continue
        st = [p]; seen.add(p); c = 0
        while st:
            y, x = st.pop(); c += 1
            for q in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if q in pts and q not in seen: seen.add(q); st.append(q)
        sizes.append(c)
    return sizes
rows = []
for f in range(f0, f0 + 16):
    d4 = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4); dep = d4[..., 0]
    ys, xs = np.mgrid[0:H, 0:W]
    valid = (dep >= 0) & (dep < 1)
    if spec.startswith('z:'):
        v = list(map(float, spec.split(':')[1:]))
        m = valid & (d4[..., 2] >= v[0]) & (d4[..., 2] <= v[1])
        if len(v) == 6: m &= (xs >= v[2]) & (ys >= v[3]) & (xs < v[4]) & (ys < v[5])
    else:
        m = np.zeros((H, W), bool)
        for l in g(f'^object_bounds device=1 frame={f} .*node={spec} '):
            k = kv(l)
            m |= (xs + .5 >= float(k['sx0'])) & (xs + .5 <= float(k['sx1'])) & (ys + .5 >= float(k['sy0'])) & (ys + .5 <= float(k['sy1'])) \
                 & (dep >= float(k['zmin']) - 2e-6) & (dep <= float(k['zmax']) + 2e-6)
        m &= valid
    if not m.any(): continue
    y0, y1, x0, x1 = max(ys[m].min() - 8, 0), min(ys[m].max() + 9, H), max(xs[m].min() - 8, 0), min(xs[m].max() + 9, W)
    c = m[y0:y1, x0:x1]
    closed = ~shift_or(~shift_or(c, 3), 3)
    holes = (closed & ~c).sum() / max(closed.sum(), 1)
    nb = np.zeros_like(c)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)): nb |= ~np.roll(np.roll(c, dy, 0), dx, 1)
    edge = (c & nb).sum() / max(c.sum(), 1)
    a = np.nan_to_num(np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)[y0:y1, x0:x1].astype(np.float32))
    Lh = np.maximum(0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2], 0)
    br = (Lh > 1) & closed
    sz = blobs(br)
    rows.append((c.sum(), holes, edge, br.sum() / max(closed.sum(), 1), len(sz), np.mean(sz) if sz else 0, np.percentile(Lh[br], 90) if br.any() else 0))
    print(f'{f} covered {c.sum()} holes {holes:.4f} edge {edge:.4f} | bright share {rows[-1][3]:.5f} blobs {len(sz)} mean size {rows[-1][5]:.2f} L p90 {rows[-1][6]:.2f}')
a = np.array(rows[1:])
print(f'median frames 2-16: covered {np.median(a[:,0]):.0f} holes {np.median(a[:,1]):.4f} edge {np.median(a[:,2]):.4f} | bright share {np.median(a[:,3]):.5f} '
      f'blobs {np.median(a[:,4]):.0f} mean blob {np.median(a[:,5]):.2f} px, L p90 {np.median(a[:,6]):.2f}')
