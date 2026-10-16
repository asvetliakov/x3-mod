#!/usr/bin/env python3
"""High-frequency energy of one object in the presented image and the resolved image, per burst frame: mean |4-neighbour
Laplacian| over the object's pixels (eroded by 2 px, so the silhouette edge is excluded) divided by the mean luma there
(contrast-normalised detail), and the same on the raw jittered scene (hdr) as the reference for what detail the input had.
The ratio out/in is the share of input detail the resolve keeps. Usage: object_sharpness.py <capdir> <log> <first> <node | z:LO:HI> (z mode: routed pixels with view z in [LO, HI], for a
burst whose log is gone)"""
import sys, re, subprocess, numpy as np
W, H = 5120, 1440
cap, log, f0, node = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
g = lambda pat: subprocess.run(['grep', '-E', pat, log], capture_output=True, text=True).stdout.splitlines()
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
def lap(a):
    return np.abs(4 * a[1:-1, 1:-1] - a[:-2, 1:-1] - a[2:, 1:-1] - a[1:-1, :-2] - a[1:-1, 2:])
rows = []
for f in range(f0, f0 + 16):
    d4 = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4); dep = d4[..., 0]
    ys, xs = np.mgrid[0:H, 0:W]
    m = np.zeros((H, W), bool)
    if node.startswith('z:'):
        lo, hi = map(float, node.split(':')[1:])
        m = (dep >= 0) & (dep < 1) & (d4[..., 2] >= lo) & (d4[..., 2] <= hi)
    for l in [] if node.startswith('z:') else g(f'^object_bounds device=1 frame={f} .*node={node} '):
        k = kv(l)
        m |= (xs + .5 >= float(k['sx0'])) & (xs + .5 <= float(k['sx1'])) & (ys + .5 >= float(k['sy0'])) & (ys + .5 <= float(k['sy1'])) \
             & (dep >= float(k['zmin']) - 2e-6) & (dep <= float(k['zmax']) + 2e-6)
    e = m.copy()
    for _ in range(2):
        e[1:-1, 1:-1] &= e[:-2, 1:-1] & e[2:, 1:-1] & e[1:-1, :-2] & e[1:-1, 2:]
    e = e[1:-1, 1:-1]
    pr = np.fromfile(f'{cap}/present_1_{f}.bgra8', np.uint8).reshape(H, W, 4).astype(np.float32)
    P = 0.0722 * pr[..., 0] + 0.7152 * pr[..., 1] + 0.2126 * pr[..., 2]
    def lum16(kind):
        a = np.nan_to_num(np.fromfile(f'{cap}/{kind}_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32))
        return np.maximum(0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2], 0)
    T, R = lum16('taa'), lum16('hdr')
    hf = lambda a: lap(a)[e].mean() / max(a[1:-1, 1:-1][e].mean(), 1e-6)
    rows.append((f, int(e.sum()), hf(P), hf(T), hf(R)))
    print(f'{f} px {e.sum()} present {rows[-1][2]:.4f} taa {rows[-1][3]:.4f} hdr {rows[-1][4]:.4f} taa/hdr {rows[-1][3]/rows[-1][4]:.3f}')
a = np.array([r[2:] for r in rows[1:]])
print(f'capture frames {f0+1}-{f0+15} median: present {np.median(a[:,0]):.4f} taa {np.median(a[:,1]):.4f} hdr {np.median(a[:,2]):.4f} taa/hdr {np.median(a[:,1]/a[:,2]):.3f}')
