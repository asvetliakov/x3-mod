#!/usr/bin/env python3
"""Run 264: on (hdr_1_<on>) minus off (hdr_1_<off>) capture; count mote blobs, extent, brightness vs surrounding fog."""
import sys, numpy as np
D = '/tmp/x3-bottleX3-run264'; W, H = 1280, 768
on_f, off_f = (sys.argv[1], sys.argv[2]) if len(sys.argv) > 2 else ('5048', '6411')
ld = lambda f: np.fromfile(f'{D}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
a, b = ld(on_f), ld(off_f)
la = a[..., :3].mean(-1); lb = b[..., :3].mean(-1); d = la - lb
print(f'on {on_f} off {off_f}: lum p50 on {np.median(la):.4f} off {np.median(lb):.4f}; |d| p50 {np.median(abs(d)):.5f} p99 {np.percentile(abs(d),99):.4f}; d<-0.02 frac {(d<-0.02).mean():.4f}')
# local background: median of 16x16 tiles of d, upsampled
t = np.median(d.reshape(H//16,16,W//16,16).transpose(0,2,1,3).reshape(H//16,W//16,256),-1)
r = d - np.repeat(np.repeat(t,16,0),16,1)
noise = 1.4826 * np.median(abs(r - np.median(r)))
for k in (6, 12):
    th = max(k * noise, 0.01)
    m = r > th; seen = np.zeros_like(m); blobs = []
    for y0, x0 in zip(*np.nonzero(m)):
        if seen[y0, x0]: continue
        st = [(y0, x0)]; seen[y0, x0] = True; px = []
        while st:
            y, x = st.pop(); px.append((y, x))
            for yy in (y-1, y, y+1):
                for xx in (x-1, x, x+1):
                    if 0 <= yy < H and 0 <= xx < W and m[yy, xx] and not seen[yy, xx]:
                        seen[yy, xx] = True; st.append((yy, xx))
        blobs.append(np.array(px))
    n = len(blobs)
    if n == 0: print('no blobs'); continue
    hw = np.array([(np.ptp(p[:,0])+1, np.ptp(p[:,1])+1) for p in blobs])
    ext = hw.max(1); wid = hw.min(1)
    area = np.array([len(p) for p in blobs])
    peak = np.array([r[p[:,0], p[:,1]].max() for p in blobs])
    fog = np.array([np.median(lb[p[:,0], p[:,1]]) for p in blobs])
    rel = peak / np.maximum(fog, 1e-3)
    q = lambda v: '/'.join(f'{np.percentile(v,p):.3g}' for p in (10, 50, 90, 100))
    print(f'thr {k}sigma={th:.4f} blobs {n} (area>=4: {(area>=4).sum()}); extent px p10/50/90/max {q(ext)}; width {q(wid)}; area {q(area)}')
    print(f'  peak add p10/50/90/max {q(peak)}; off lum under blob {q(fog)}; peak/fog {q(rel)}')
    print('  extent hist', np.bincount(np.minimum(ext, 20)).tolist())
