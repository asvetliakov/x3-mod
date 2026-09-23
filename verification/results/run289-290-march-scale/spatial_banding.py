"""run289 (march scale 2) vs run290 (scale 4): 4-px/2-px lattice signature in the post-fog, pre-TAA HDR readback
(hdr_1_<f>.rgba16f, 5120x1440 RGBA16F) over sky pixels at least MARGIN px from any geometry (depth r in [0,1]).
Bilinear upsampling of a march sampled at full pixels S*q is piecewise linear between the samples, so the second
difference of the fog term is zero except at x = 0 mod S (kinks). Metric: mean |d2| of luminance per phase (x mod 4 /
y mod 4), ratio phase0 / mean(phases 1..3); and the normalised autocorrelation of d2 at lags 1..8.
usage: spatial_banding.py  (reads /tmp/x3-bottleX3-run289, run290)"""
import numpy as np, glob, os, re
W, H, MARGIN = 5120, 1440, 16
RUNS = {'run289 s2': '/tmp/x3-bottleX3-run289', 'run290 s4': '/tmp/x3-bottleX3-run290'}
def load(d, kind, f):
    if kind == 'hdr': return np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
    return np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
def dilate(m, r):
    # separable box max via cumulative sums
    c = np.cumsum(np.pad(m.astype(np.int32), ((0, 0), (r + 1, r))), 1); m = (c[:, 2*r+1:] - c[:, :-2*r-1]) > 0
    c = np.cumsum(np.pad(m.astype(np.int32), ((r + 1, r), (0, 0))), 0); return (c[2*r+1:] - c[:-2*r-1]) > 0
def phase_stats(L, sky, axis):
    d2 = np.abs(np.diff(L, 2, axis=axis))
    ok = sky[:, 1:-1] & sky[:, :-2] & sky[:, 2:] if axis == 1 else sky[1:-1] & sky[:-2] & sky[2:]
    idx = (np.arange(1, (W if axis == 1 else H) - 1) % 4)
    ph = [d2[:, idx == k][ok[:, idx == k]].mean() if axis == 1 else d2[idx == k][ok[idx == k]].mean() for k in range(4)]
    # autocorrelation of d2 (mean removed) inside sky rows/cols, lags 1..8
    x = np.where(ok, d2 - d2[ok].mean(), 0.0)
    ac = []
    for lag in range(1, 9):
        a = (x[:, :-lag] * x[:, lag:]).sum() if axis == 1 else (x[:-lag] * x[lag:]).sum()
        ac.append(a / (x * x).sum())
    return ph, ac
print('run frame sky_px lumMean | x: d2 phase0..3 ratio0 ratio02(ph0+ph2 vs ph1+ph3) | y: same | acx lag1..8 | acy lag1..8')
for name, d in RUNS.items():
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    for f in frames:
        h = load(d, 'hdr', f); dep = load(d, 'depth', f)
        geo = (dep[..., 0] >= 0) & (dep[..., 0] <= 1)
        sky = ~dilate(geo, MARGIN)
        L = 0.2126*h[..., 0] + 0.7152*h[..., 1] + 0.0722*h[..., 2]
        px, acx = phase_stats(L, sky, 1); py, acy = phase_stats(L, sky, 0)
        r = lambda p: p[0] / np.mean(p[1:]); r2 = lambda p: (p[0] + p[2]) / (p[1] + p[3])
        print(f"{name} {f} {sky.sum()} {L[sky].mean():.4f} | {' '.join(f'{v:.2e}' for v in px)} {r(px):.2f} {r2(px):.2f} | "
              f"{' '.join(f'{v:.2e}' for v in py)} {r(py):.2f} {r2(py):.2f} | {' '.join(f'{v:+.2f}' for v in acx)} | {' '.join(f'{v:+.2f}' for v in acy)}")
