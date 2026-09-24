"""Run 78 A dither A/B (run295 dither on, run296 --hdr-dither off, same fog spot). The hdr_1 bursts are the FP16 scene
before TAA/exposure/AgX, so the 8-bit output is MODELLED as in run291-293-rings/stage_quantisation.py:
c = 255*AgX(decode(rgb)*2^ev) on G with the frame's logged ev; off: round(c); on: round(c + (IGN(x,y)-0.5)) (display_dither.hlsl,
agx_reference.dither_noise). Sky = depth sentinel minus 16 px, stars dropped (3 % from the 5-px median), every 4th row.
Per frame and mode: plateau% (pixel == right neighbour), run50/run90 of equal codes along x (px), and the low-frequency
banding error: |box33x33(code) - box33x33(c)| p50/p99 in codes (a contour the eye sees survives the box; the dither averages out).
Also the frame's hdr_frame ev and the session's hdr_tonemap dither/dither_reason fields. usage: dither_contours.py"""
import numpy as np, glob, re, sys
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis'); import agx_reference as A
W, H = 5120, 1440
MI, MO, C = np.array(A.M_IN), np.array(A.M_OUT), A.CONTRAST_COEFFICIENTS
def agxc(rgb, ev):
    v = (np.maximum(rgb, 0) ** A.DECODE_GAMMA * 2.0 ** ev) @ MI.T
    x = (np.clip(np.log2(np.maximum(v, A.LOG_FLOOR)), A.MIN_EV, A.MAX_EV) - A.MIN_EV) / (A.MAX_EV - A.MIN_EV)
    return 255 * np.clip(np.polyval(C, x) @ MO.T, 0, 1)
def dilate(m, r):
    c = np.cumsum(np.pad(m.astype(np.int32), ((0, 0), (r + 1, r))), 1); m = (c[:, 2*r+1:] - c[:, :-2*r-1]) > 0
    c = np.cumsum(np.pad(m.astype(np.int32), ((r + 1, r), (0, 0))), 0); return (c[2*r+1:] - c[:-2*r-1]) > 0
def box(a, k):
    p = np.pad(a, k, mode='edge'); cs = p.cumsum(0).cumsum(1); cs = np.pad(cs, ((1, 0), (1, 0)))
    n = 2*k+1; return (cs[n:, n:] - cs[:-n, n:] - cs[n:, :-n] + cs[:-n, :-n]) / n**2
yy, xx = np.mgrid[0:H, 0:W]
inner = A.DITHER_IGN_WEIGHTS[0] * (xx + 0.5) + A.DITHER_IGN_WEIGHTS[1] * (yy + 0.5)
noise = (A.DITHER_IGN_SCALE * (inner - np.floor(inner))) % 1.0
def runs(eq):
    L = []
    for row in eq:
        d = np.flatnonzero(~row); L.extend(np.diff(np.concatenate(([-1], d, [len(row)]))))
    return np.percentile(L, 50), np.percentile(L, 90)
for run in ('295', '296'):
    d = f'/tmp/x3-bottleX3-run{run}'; log = glob.glob(f'{d}/session-*.log')[0]
    ev = {}; tm = ''
    for l in open(log, errors='replace'):
        if l.startswith('hdr_frame device=1 '):
            m = re.search(r' frame=(\d+) .* ev=([-\d.e]+) ', l); ev[int(m.group(1))] = float(m.group(2))
        elif l.startswith('hdr_tonemap '): tm = ' '.join(re.findall(r'dither\w*=\S+', l))
    print(f'run{run} hdr_tonemap {tm}')
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    for f in (frames[0], frames[-1]):
        h = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)
        z = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
        sky = ~dilate((z >= 0) & (z <= 1), 16)
        G = h[..., 1].astype(np.float32); med = np.median(np.stack([np.roll(G, k, 1) for k in range(-2, 3)]), 0)
        ok = sky & (np.abs(G - med) <= 0.03 * np.maximum(med, 1e-4)) & (G > 0)
        rows = slice(0, H, 4)
        c = agxc(h[rows, :, :3].astype(np.float64).reshape(-1, 3), ev[f])[:, 1].reshape(-1, W)
        okr = ok[rows]; bad = ~np.isfinite(c); c[bad] = 0; okr &= ~bad
        out = {'off': np.floor(c + 0.5), 'on': np.floor(np.clip(c + (noise[rows] - 0.5), 0, 255) + 0.5)}
        bc = box(c, 16); okb = ~dilate(~okr, 16)
        for k, q in out.items():
            eq = (q[:, 1:] == q[:, :-1]) & okr[:, 1:] & okr[:, :-1]
            pl = eq.sum() / (okr[:, 1:] & okr[:, :-1]).sum() * 100
            r50, r90 = runs(np.where(okr[:, 1:] & okr[:, :-1], eq, False))
            e = np.abs(box(q, 16) - bc)[okb]
            print(f'run{run} frame {f} ev {ev[f]:+.3f} n_sky={okr.sum()} {k:3s}: plateau {pl:.1f}% run50/run90 {r50:.0f}/{r90:.0f} px | '
                  f'banding |box33(code)-box33(c)| p50/p99 {np.percentile(e,50):.3f}/{np.percentile(e,99):.3f} codes | code p50 {np.percentile(c[okr],50):.1f}')
