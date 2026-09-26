#!/usr/bin/env python3
"""Steady-state pan replay on a real station texture (model; the capture's burst frames run ~10x slower than play, so their
history never reaches the steady state of a normal-rate pan). Scene: the at-rest pre-resolve current hdr_(frame) crop,
magnified by 1.37 with nearest texels at a 0.31-texel offset (hard, off-grid edges, so the jitter aliases as a raster does).
Frame t: every pixel point-samples the scene at x + v t + j_t (Halton 2,3 over 8 phases, the log's jitter sequence);
the resolve is replay_common.resolve (validated to p99 0.09 codes on the rest frame, p50 0.20 on the pan frame) with the
history at x + v, keep w, the crop's k. Reference: the 4x4-supersampled pixel box of the scene at x + v t (the ideal AA
image). 64 warm-up frames, 16 measured. Metrics on the interior: E(AgX(out)) / E(AgX(ref)) (sharpness vs ideal, before
and after RCAS 0.75 / 1.0 applied to both, reference unsharpened) and the shimmer proxy = rms over pixels of the temporal
std of AgX luma (out - ref) in 8-bit codes over the 16 frames (the ledger's motion-compensated hot-pixel std, here against
the moving reference instead of a motion-compensated tile).
Usage: replay_steady.py <dir> <frame> x0 y0 x1 y1 k ev"""
import sys, numpy as np
from replay_common import *
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); K, EV = float(sys.argv[7]), float(sys.argv[8])
S = np.array(np.memmap(f'{d}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(H, W, 4))[y0:y1, x0:x1, :3]).astype(np.float64)
S = np.nan_to_num(S)
sh, sw = S.shape[:2]
MAG, OFF = 1.37, 0.31
def scene(px, py):  # periodic, nearest texel of the magnified scene
    ix = np.floor(px / MAG + OFF).astype(int) % sw; iy = np.floor(py / MAG + OFF).astype(int) % sh
    return S[iy, ix]
CH, CW, MARGIN = 220, 320, 28
ys, xs = np.mgrid[0:CH + 2, 0:CW + 2].astype(np.float64)  # the crop with its 1-px ring
def halton(i, b):
    f, r = 1., 0.
    while i > 0: f /= b; r += f * (i % b); i //= b
    return r
jit = [(halton(i + 1, 2) - 0.5, halton(i + 1, 3) - 0.5) for i in range(8)]
ss = [((a + 0.5) / 4 - 0.5, (b + 0.5) / 4 - 0.5) for a in range(4) for b in range(4)]
inner = (slice(MARGIN, CH - MARGIN), slice(MARGIN, CW - MARGIN))
mask = np.zeros((CH, CW), bool); mask[inner] = True
print(f'scene {fr} box {x0},{y0},{x1},{y1} k {K} ev {EV}; crop {CW}x{CH}, measured {mask.sum()} px')
for vx in (0.0, 1.3, 5.9, 19.7):
    vy = 0.07 * (vx > 0)
    refs = {}
    for t in range(64, 80):
        acc = 0
        for sx, sy in ss: acc = acc + scene(xs[1:-1, 1:-1] + vx * t + sx, ys[1:-1, 1:-1] + vy * t + sy)
        refs[t] = agx(acc / len(ss), EV)
    for w in (0.9, 0.85, 0.8):
        hist = None; outs = []
        for t in range(80):
            jx, jy = jit[t % 8]
            cur = scene(xs + vx * t + jx, ys + vy * t + jy)
            if hist is None: out = cur[1:-1, 1:-1]
            else: out = resolve(cur, hist, xs[1:-1, 1:-1] - 1 + vx, ys[1:-1, 1:-1] - 1 + vy, w, K)
            hist = out
            if t >= 64: outs.append(agx(out, EV))
        E = {}
        for name, fn in (('taa', lambda a: a), ('rcas0.75', lambda a: rcas(a, 0.75)), ('rcas1.0', lambda a: rcas(a, 1.0))):
            E[name] = np.mean([energy(fn(o), mask) / energy(refs[64 + i], mask) for i, o in enumerate(outs)])
        res = np.stack([(o - refs[64 + i]) @ LUMA * 255 for i, o in enumerate(outs)])
        shimmer = float(np.sqrt((res.std(0)[mask] ** 2).mean())); bias = float(np.abs(res.mean(0))[mask].mean())
        print(f'v {vx:5.1f} px/frame w {w:.2f}: E/E_ideal taa {E["taa"]:.3f} rcas0.75 {E["rcas0.75"]:.3f} rcas1.0 {E["rcas1.0"]:.3f} | shimmer {shimmer:.2f} codes, |mean residual| {bias:.2f} codes')
