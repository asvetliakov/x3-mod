"""Contour geometry of the modelled 8-bit output on the run289/290 bursts (first and last frame each).
Continuous code c = 255 * AgX(decode(rgb) * 2^ev) on G (no rounding), sky pixels as stage_quantisation.py.
  codes_per_ev: median |dc/d ev| (finite difference 0.01 EV)
  px_per_code:  median 1 / |grad c| (central differences, x and y), i.e. the spacing of adjacent 8-bit contours
  shift_px per 0.01 EV = 0.01 * codes_per_ev * px_per_code (per-pixel median): how far every contour moves per 0.01 EV
  fp16_ulp_in_codes: median |dc| caused by one FP16 ulp of the stored G (the HDR target's own step)
Then, with the logged per-frame |d ev| quantiles of run291-293 (exposure_series_out.txt), contour motion px/frame.
usage: contour_motion.py"""
import numpy as np, re, sys
exec(open(__file__.replace('contour_motion.py', 'stage_quantisation.py')).read().split("print('run frame ev")[0])
def agxc(rgb, ev):
    v = np.maximum(rgb, 0) ** A.DECODE_GAMMA * 2.0 ** ev
    v = v @ MI.T
    x = (np.clip(np.log2(np.maximum(v, A.LOG_FLOOR)), MIN, MAX) - MIN) / (MAX - MIN)
    return 255 * np.clip(np.polyval(C, x) @ MO.T, 0, 1)
for name, (d, log) in RUNS.items():
    ev = evs(f'{d}/{log}')
    frames = sorted(int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f'))
    for f in (frames[0], frames[-1]):
        h = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)
        r = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
        sky = ~dilate((r >= 0) & (r <= 1), 16)
        Gf = h[..., 1].astype(np.float32)
        med = np.median(np.stack([np.roll(Gf, k, 1) for k in range(-2, 3)]), 0)
        ok = sky & (np.abs(Gf - med) <= 0.03 * np.maximum(med, 1e-4)) & (Gf > 0)
        ok[:, :2] = ok[:, -2:] = False; ok[:2] = ok[-2:] = False
        # smooth the HDR field 5x5 box to measure the underlying gradient, not the FP16 ulp noise
        rgb = h[..., :3].astype(np.float64)
        k = 5; pad = np.pad(rgb, ((k, k), (k, k), (0, 0)), mode='edge'); cs = pad.cumsum(0).cumsum(1)
        sm = (cs[2*k:, 2*k:] - cs[:-2*k, 2*k:] - cs[2*k:, :-2*k] + cs[:-2*k, :-2*k])[:H, :W] / (2*k)**2
        ys, xs = np.nonzero(ok); s = slice(None, None, 7); ys, xs = ys[s], xs[s]
        c0 = agxc(sm[ys, xs], ev[f])[:, 1]; c1 = agxc(sm[ys, xs], ev[f] + 0.01)[:, 1]
        cpe = np.abs(c1 - c0) / 0.01
        cx = (agxc(sm[ys, xs + 2], ev[f])[:, 1] - agxc(sm[ys, xs - 2], ev[f])[:, 1]) / 4
        cy = (agxc(sm[ys + 2, xs], ev[f])[:, 1] - agxc(sm[ys - 2, xs], ev[f])[:, 1]) / 4
        g = np.hypot(cx, cy); ppc = 1 / np.maximum(g, 1e-6)
        shift = 0.01 * cpe * ppc
        raw = h[ys, xs, :3].astype(np.float64); G16 = h[ys, xs, 1].astype(np.float32)
        ulp = 2.0 ** (np.floor(np.log2(np.maximum(G16, 1e-8))) - 10); bump = raw.copy(); bump[:, 1] += ulp
        du = np.abs(agxc(bump, ev[f])[:, 1] - agxc(raw, ev[f])[:, 1])
        q = lambda a, p: np.percentile(a[np.isfinite(a)], p)
        print(f"{name} {f} ev {ev[f]:+.3f} | codes_per_ev p50 {q(cpe,50):.1f} | px_per_code p25/p50/p75 {q(ppc,25):.1f}/{q(ppc,50):.1f}/{q(ppc,75):.1f} "
              f"| shift_px per 0.01 EV p50/p75 {q(shift,50):.2f}/{q(shift,75):.2f} | fp16_ulp_in_codes p50/p90 {q(du,50):.3f}/{q(du,90):.3f} | code p50 {q(c0,50):.1f}")
