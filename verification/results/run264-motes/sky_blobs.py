#!/usr/bin/env python3
"""Run 264: per-frame bright-blob census on sky pixels (RT2 depth.b == -1 sentinel), each frame against its own
16x16-tile median, so camera misalignment between the on and off frames does not create blobs.
Mote census = on-frame blobs minus off-frame blobs (same threshold); sizes from on-frame blobs absent in off."""
import sys, numpy as np
D = '/tmp/x3-bottleX3-run264'; W, H = 1280, 768
def load(f):
    c = np.fromfile(f'{D}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3].mean(-1)
    z = np.fromfile(f'{D}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 2]
    return c, z < 0
def blobs(c, sky, th):
    t = np.median(c.reshape(H//16, 16, W//16, 16).transpose(0, 2, 1, 3).reshape(H//16, W//16, 256), -1)
    bg = np.repeat(np.repeat(t, 16, 0), 16, 1); r = c - bg
    m = (r > th * np.maximum(bg, 1e-3)) & sky
    seen = np.zeros_like(m); out = []
    for y0, x0 in zip(*np.nonzero(m)):
        if seen[y0, x0]: continue
        st = [(y0, x0)]; seen[y0, x0] = True; px = []
        while st:
            y, x = st.pop(); px.append((y, x))
            for yy in (y-1, y, y+1):
                for xx in (x-1, x, x+1):
                    if 0 <= yy < H and 0 <= xx < W and m[yy, xx] and not seen[yy, xx]:
                        seen[yy, xx] = True; st.append((yy, xx))
        p = np.array(px)
        hh, ww = np.ptp(p[:, 0]) + 1, np.ptp(p[:, 1]) + 1
        out.append((max(hh, ww), min(hh, ww), len(p), (r[p[:, 0], p[:, 1]] / np.maximum(bg[p[:, 0], p[:, 1]], 1e-3)).max(), bg[p[:,0],p[:,1]].mean()))
    return np.array(out) if out else np.zeros((0, 5))
on, off = (sys.argv[1:3] if len(sys.argv) > 2 else ('5048', '6411'))
co, so = load(on); cf, sf = load(off); sky = so & sf
print(f'on {on} off {off}: sky fraction {sky.mean():.3f}; sky lum p50 on {np.median(co[sky]):.4f} off {np.median(cf[sky]):.4f}')
q = lambda v: '/'.join(f'{np.percentile(v, p):.3g}' for p in (10, 50, 90, 100)) if len(v) else '-'
for th in (0.15, 0.3, 0.6):
    a, b = blobs(co, sky, th), blobs(cf, sky, th)
    print(f'rel thr +{th:.0%}: blobs on {len(a)} off {len(b)} diff {len(a)-len(b)}')
    for name, x in (('on', a), ('off', b)):
        if len(x): print(f'  {name}: extent {q(x[:,0])} width {q(x[:,1])} area {q(x[:,2])} peak/bg {q(x[:,3])} bg {q(x[:,4])}; extent hist(<=16+) {np.bincount(np.minimum(x[:,0].astype(int),16), minlength=17)[1:].tolist()}')
# mote-sized blobs (extent 5..20 px) only: count and brightness, on vs off (the off count is the scene's own)
for th in (0.15, 0.3, 0.6):
    a, b = blobs(co, sky, th), blobs(cf, sky, th)
    sa, sb = a[(a[:,0] >= 5) & (a[:,0] <= 20)], b[(b[:,0] >= 5) & (b[:,0] <= 20)]
    print(f'mote-sized rel thr +{th:.0%}: on {len(sa)} off {len(sb)} excess {len(sa)-len(sb)}; on extent {q(sa[:,0])} width {q(sa[:,1])} peak/bg {q(sa[:,3])} | off peak/bg {q(sb[:,3])}')
