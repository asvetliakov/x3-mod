#!/usr/bin/env python3
# FL run 67: temporal persistence of the flagged bolt coverage (RT2.g > 1) at a fixed screen pixel. A pixel the bolt
# covers in frame N and also covered in N-1 (same pixel, 3x3 dilation) has bolt in its history; a pixel covered in N
# only enters the resolve as a one-frame sample (base keep 0.85 -> ~15 % plus the 3x3 clamp). Per frame: flagged px,
# share covered in N-1, per-blob share distribution (blob = 8-connected flag component), and peak radiance by bucket.
import numpy as np, sys
D = sys.argv[1]; frames = [int(x) for x in sys.argv[2:]]
def flag(fr):
    d = np.memmap(f'{D}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(1440, 5120, 4)); return np.asarray(d[..., 1]) > 1
def dil(m):
    o = m.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): o |= np.roll(np.roll(m, dy, 0), dx, 1)
    return o
def blobs(m):
    lab = np.zeros(m.shape, np.int32); n = 0
    for y, x in zip(*np.nonzero(m)):
        if lab[y, x]: continue
        n += 1; st = [(y, x)]; lab[y, x] = n
        while st:
            a, b = st.pop()
            for u in (a - 1, a, a + 1):
                for v in (b - 1, b, b + 1):
                    if 0 <= u < 1440 and 0 <= v < 5120 and m[u, v] and not lab[u, v]: lab[u, v] = n; st.append((u, v))
    return lab, n
prev = None; last = None
for fr in frames:
    m = flag(fr)
    if last is not None and fr != last + 1: prev = None  # a new capture group: no previous frame
    last = fr
    if prev is not None and m.any():
        hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(1440, 5120, 4))
        pd = dil(prev); lab, n = blobs(m); ys, xs = np.nonzero(m); ids = lab[ys, xs]; hit = pd[ys, xs]
        L = np.asarray(hdr[ys, xs, :3], np.float32).max(-1)
        share = np.bincount(ids, hit, n + 1)[1:] / np.maximum(np.bincount(ids, None, n + 1)[1:], 1)
        peak = np.zeros(n + 1); np.maximum.at(peak, ids, L); peak = peak[1:]
        lo, hi = share < 0.25, share >= 0.75
        print(f'{fr} flagged_px={m.sum()} covered_prev={hit.mean():.3f} blobs={n} share<0.25: {lo.sum()} (peak med {np.median(peak[lo]) if lo.any() else 0:.2f}) '
              f'0.25-0.75: {(~lo & ~hi).sum()} share>=0.75: {hi.sum()} (peak med {np.median(peak[hi]) if hi.any() else 0:.2f})')
    prev = m
