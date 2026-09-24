"""Where does the fogged sky quantise? run289 (scale 2) / run290 (scale 4) hdr_1 bursts (FP16 scene after the fog composite,
before TAA and before exposure/AgX; run291-293 took no burst). Sky = depth sentinel (as temporal_crawl.py), minus a
16-px margin; stars/motes removed by dropping pixels whose G differs from the 5-px horizontal median by > 3 %.
Per frame, on the green channel (engine-encoded, gamma 2.2):
  HDR stage: distinct FP16 G values; share of right-neighbour pairs with identical FP16 value (plateau share); median
  |dG/dx| in FP16 ulps (ulp = 2^(floor(log2 G) - 10)); median plateau run length along x (px).
  Presented model: G8 = round(255 * AgX(decode(rgb) * 2^ev)) with the frame's logged ev (hdr_frame ev) and the
  agx_reference constants (look none) - the shader's own output quantisation, no dither (agx.hlsl:112 saturate only).
  Same statistics in 8-bit codes, plus: share of sky pixels whose code changes when ev moves by the logged ev step to
  the next burst frame (the 'rings move with exposure' test) and the median sky luminance/code.
usage: stage_quantisation.py"""
import numpy as np, glob, re, sys, os
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import agx_reference as A
W, H = 5120, 1440
RUNS = {'run289 s2': ('/tmp/x3-bottleX3-run289', 'session-20260924-031454-212.log'),
        'run290 s4': ('/tmp/x3-bottleX3-run290', 'session-20260924-031933-212.log')}
MIN, MAX = A.MIN_EV, A.MAX_EV
MI, MO = np.array(A.M_IN), np.array(A.M_OUT); C = A.CONTRAST_COEFFICIENTS
def agx8(rgb, ev):  # rgb engine-encoded (N,3) -> 8-bit codes (N,3)
    v = np.maximum(rgb, 0) ** A.DECODE_GAMMA * 2.0 ** ev
    v = v @ MI.T
    x = (np.clip(np.log2(np.maximum(v, A.LOG_FLOOR)), MIN, MAX) - MIN) / (MAX - MIN)
    y = np.polyval(C, x)
    o = np.clip(y @ MO.T, 0, 1)
    return np.round(o * 255).astype(np.int32)
def dilate(m, r):
    c = np.cumsum(np.pad(m.astype(np.int32), ((0, 0), (r + 1, r))), 1); m = (c[:, 2*r+1:] - c[:, :-2*r-1]) > 0
    c = np.cumsum(np.pad(m.astype(np.int32), ((r + 1, r), (0, 0))), 0); return (c[2*r+1:] - c[:-2*r-1]) > 0
def evs(log):
    out = {}
    for line in open(log, errors='replace'):
        if line.startswith('hdr_frame device=1 '):
            m = re.search(r' frame=(\d+) .* ev=([-\d.e]+) ', line)
            if m: out[int(m.group(1))] = float(m.group(2))
    return out
def runs_x(eq):  # eq: (H, W-1) bool, True where pixel == right neighbour; return run lengths of equal values
    L = []
    for row in eq[::8]:  # every 8th row
        d = np.diff(np.concatenate(([0], (~row).astype(np.int8), [1])))
        b = np.flatnonzero(np.concatenate(([True], ~row, [True])))
        L.extend(np.diff(b).tolist())
    return np.array(L)
print('run frame ev | HDR: distinct_G plateau% med|dG|/ulp run50/run90 px | 8-bit model: distinct_G8 plateau% run50/run90 px med_code | code change% for d_ev to next frame (d_ev)')
for name, (d, log) in RUNS.items():
    ev = evs(f'{d}/{log}')
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    for i, f in enumerate(frames):
        h = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)
        r = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
        sky = ~dilate((r >= 0) & (r <= 1), 16)
        G = h[..., 1]; Gf = G.astype(np.float32)
        med = np.median(np.stack([np.roll(Gf, k, 1) for k in range(-2, 3)]), 0)
        ok = sky & (np.abs(Gf - med) <= 0.03 * np.maximum(med, 1e-4)) & (Gf > 0)
        pair = ok[:, :-1] & ok[:, 1:]
        eqh = (G[:, :-1] == G[:, 1:]) & pair
        ulp = 2.0 ** (np.floor(np.log2(np.maximum(Gf, 1e-8))) - 10)
        grad = np.abs(Gf[:, 1:] - Gf[:, :-1]) / ulp[:, :-1]
        rh = runs_x(np.where(pair, eqh, False)); rh = rh[rh > 0]
        ys, xs = np.nonzero(ok); sub = slice(None, None, 1)
        rgb = h[ys, xs, :3].astype(np.float64)
        c8 = agx8(rgb, ev[f]); code = np.full((H, W), -1, np.int32); code[ys, xs] = c8[:, 1]
        eq8 = (code[:, :-1] == code[:, 1:]) & pair
        r8 = runs_x(np.where(pair, eq8, False)); r8 = r8[r8 > 0]
        nxt = frames[i + 1] if i + 1 < len(frames) else None
        ch = ''
        if nxt:
            c8b = agx8(rgb, ev[nxt]); ch = f"{100*np.mean(c8b[:, 1] != c8[:, 1]):.1f}% (d_ev {ev[nxt]-ev[f]:+.3f})"
        print(f"{name} {f} {ev[f]:+.3f} | {len(np.unique(G[ok]))} {100*eqh.sum()/pair.sum():.1f}% {np.median(grad[pair]):.1f} "
              f"{np.median(rh):.0f}/{np.percentile(rh,90):.0f} | {len(np.unique(c8[:,1]))} {100*eq8.sum()/pair.sum():.1f}% "
              f"{np.median(r8):.0f}/{np.percentile(r8,90):.0f} {int(np.median(c8[:,1]))} | {ch}  n_sky={ok.sum()}")
