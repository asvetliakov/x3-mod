"""run289 (scale 2) vs run290 (scale 4): frame-to-frame change of the fogged sky after rotational reprojection.
Inputs per burst frame: hdr_1_<f>.rgba16f (post-fog composite, pre-TAA), depth_1_<f>.rgba32f (sky: r outside [0,1]),
camera_state (p00, p11, r00..r22) and motion_output_frame (jitter_x/y, pixels) rows of the session log.
Sky is world-anchored and the camera only rotates (translation < 3 units/frame), so after reprojecting frame t-1 into t
the residual is background resampling error (phase-neutral) plus any screen-anchored change of the fog upsample. The
upsample interpolates march samples taken at full pixels S*q (S = 2 or 4), so its error is zero at x = 0 mod S and
largest mid-cell: a grid-anchored residual shows as residual energy rising with the pixel's phase distance from the
sample column/row. Reported: residual RMS / mean L, and RMS per phase x mod 4 and y mod 4 (sky pixels only).
usage: temporal_crawl.py"""
import numpy as np, re, glob, itertools
W, H, MARGIN = 5120, 1440, 16
RUNS = {'run289 s2': ('/tmp/x3-bottleX3-run289', 'session-20260924-031454-212.log'),
        'run290 s4': ('/tmp/x3-bottleX3-run290', 'session-20260924-031933-212.log')}
def dilate(m, r):
    c = np.cumsum(np.pad(m.astype(np.int32), ((0, 0), (r + 1, r))), 1); m = (c[:, 2*r+1:] - c[:, :-2*r-1]) > 0
    c = np.cumsum(np.pad(m.astype(np.int32), ((r + 1, r), (0, 0))), 0); return (c[2*r+1:] - c[:-2*r-1]) > 0
def rows(log, frames):
    cam, jit = {}, {}
    for line in open(log, errors='replace'):
        if line.startswith('camera_state device=1 '):
            f = int(re.search(r' frame=(\d+)', line).group(1))
            if f in frames:
                g = lambda k: float(re.search(r' %s=([-\d.e]+)' % k, line).group(1))
                cam[f] = (g('p00'), g('p11'), np.array([[g(f'r{i}{j}') for j in range(3)] for i in range(3)]))
        elif line.startswith('motion_output_frame device=1 '):
            f = int(re.search(r' frame=(\d+)', line).group(1))
            if f in frames:
                jit[f] = (float(re.search(r' jitter_x=([-\d.e]+)', line).group(1)), float(re.search(r' jitter_y=([-\d.e]+)', line).group(1)))
    return cam, jit
def lum(d, f):
    h = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
    return 0.2126*h[..., 0] + 0.7152*h[..., 1] + 0.0722*h[..., 2]
def sky(d, f):
    r = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
    return ~dilate((r >= 0) & (r <= 1), MARGIN)
def bilinear(img, x, y):
    x0 = np.floor(x).astype(np.int64); y0 = np.floor(y).astype(np.int64); fx = x - x0; fy = y - y0
    x0 = np.clip(x0, 0, W - 2); y0 = np.clip(y0, 0, H - 2)
    return ((img[y0, x0]*(1-fx) + img[y0, x0+1]*fx)*(1-fy) + (img[y0+1, x0]*(1-fx) + img[y0+1, x0+1]*fx)*fy)
def reproject(cam_t, cam_p, jt, jp, conv, jsign, ys, xs):
    p00, p11, Rt = cam_t; _, _, Rp = cam_p
    X = (xs + 0.5 - jsign*jt[0])/W*2 - 1; Y = 1 - (ys + 0.5 - jsign*jt[1])/H*2
    v = np.stack([X/p00, Y/p11, np.ones_like(X)], 1)
    M = {'RtT_Rp': Rt.T @ Rp, 'Rt_RpT': Rt @ Rp.T}[conv[0]]
    w = v @ M if conv[1] == 'row' else v @ M.T
    ok = w[:, 2] > 1e-3
    Xp = w[:, 0]/w[:, 2]*p00; Yp = w[:, 1]/w[:, 2]*p11
    return (Xp + 1)/2*W - 0.5 + jsign*jp[0], (1 - Yp)/2*H - 0.5 + jsign*jp[1], ok
print('run pair rotdeg conv | n_px rms/meanL | rms by x mod 4 (0..3) | rms by y mod 4 | x ratio (ph2/ph0) y ratio | odd/even x (scale-2 cell) y')
for name, (d, logname) in RUNS.items():
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    cam, jit = rows(f'{d}/{logname}', set(frames))
    conv = None
    for fp, ft in zip(frames, frames[1:]):
        Lt, Lp = lum(d, ft), lum(d, fp); St, Sp = sky(d, ft), sky(d, fp)
        ys, xs = np.nonzero(St)
        tr = np.trace(cam[ft][2] @ cam[fp][2].T); rot = np.degrees(np.arccos(np.clip((tr - 1)/2, -1, 1)))
        if conv is None:  # pick the convention with the smallest residual on a subsample, once per run
            sub = slice(None, None, 97); best = None
            for c in itertools.product(('RtT_Rp', 'Rt_RpT'), ('row', 'col')):
                for js in (1, -1, 0):
                    x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], c, js, ys[sub].astype(float), xs[sub].astype(float))
                    ok &= (x2 >= 0) & (x2 < W-1) & (y2 >= 0) & (y2 < H-1)
                    if ok.sum() < 1000: continue
                    e = np.sqrt(np.mean((Lt[ys[sub], xs[sub]][ok] - bilinear(Lp, x2[ok], y2[ok]))**2))
                    if best is None or e < best[0]: best = (e, c, js)
            conv = best[1:]
        x2, y2, ok = reproject(cam[ft], cam[fp], jit[ft], jit[fp], conv[0], conv[1], ys.astype(float), xs.astype(float))
        ok &= (x2 >= 0) & (x2 < W-1) & (y2 >= 0) & (y2 < H-1)
        ok[ok] &= Sp[np.round(y2[ok]).astype(int), np.round(x2[ok]).astype(int)]
        r = Lt[ys[ok], xs[ok]] - bilinear(Lp, x2[ok], y2[ok]); m = Lt[ys[ok], xs[ok]].mean()
        px = [np.sqrt(np.mean(r[(xs[ok] % 4) == k]**2)) for k in range(4)]; py = [np.sqrt(np.mean(r[(ys[ok] % 4) == k]**2)) for k in range(4)]
        oe = lambda a, k: np.sqrt(np.mean(r[(a[ok] % 2) == 1]**2))/np.sqrt(np.mean(r[(a[ok] % 2) == 0]**2))
        print(f"{name} {fp}->{ft} {rot:.2f} {conv[0][0]}/{conv[0][1]}/j{conv[1]:+d} | {ok.sum()} {np.sqrt(np.mean(r**2))/m:.4f} | "
              f"{' '.join(f'{v:.2e}' for v in px)} | {' '.join(f'{v:.2e}' for v in py)} | {px[2]/px[0]:.3f} {py[2]/py[0]:.3f} | {oe(xs,0):.3f} {oe(ys,0):.3f}")
