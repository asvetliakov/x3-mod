#!/usr/bin/env python3
"""Host model of the FAR_JITTER_LINE fixture scene (verification/probe/temporal_far_jitter_line_inc.h) through the camera-gate
resolve's path for a far pixel outside the thin region (resolve.hlsl X3M_REGION_HOLD): farw = 1, far weight 0.985 on an open
camera gate (world-static content), no far filter, k = 0, uniform depth (no dilation, no disocclusion), history clipped by the
3x3 variance box (mean +/- 1.25 sigma intersected with the 3x3 min / max) or by the far clip's 7x7 min / max, the 5-tap
Catmull-Rom history (exact 1-D Catmull-Rom here: the pans are along x, so fy = 0), FP16 history. Prints the fixture's row
fields (spike_codes, sparkles, delta_codes, dim_codes) so the fixture's GPU numbers can be compared with it. An inference
aid, not the fixture: the GPU's filter-unit rounding, FP16 current colour and float32 arithmetic are not modelled.
No game, no Wine. Usage: python3 far_jitter_line_model.py"""
import numpy as np

W, H, P = 512, 16, 16
FRAMES, MOVE_FROM, WINDOW = 96, 64, 16
X0, X1, Y0, Y1 = 352, W - 16, 4, H - 4
BACKGROUND, BRIGHT, SLOPE = .05, 2., .2
PHASES = 8


def halton(i, b):
    f, r = 1., 0.
    while i > 0:
        f /= b
        r += f * (i % b)
        i //= b
    return r


def lround(v):
    return int(np.floor(abs(v) + .5)) * (1 if v >= 0 else -1)  # std::lround: halves away from zero


def code(y):
    y = np.maximum(y, 0)
    return 255 * (y / (1 + y)) ** (1 / 2.2)


def scene(width, jx, jy, disp):
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    p = np.floor(xs) + .5 - (jx + disp)
    q = p - SLOPE * (ys + .5 - jy)
    m = q - P * np.floor(q / P) - 7.31
    return np.where((m >= 0) & (m < width), BRIGHT, BACKGROUND)


def box(img, r):
    pad = np.pad(img, r, mode='edge')
    stack = [pad[r + dy:r + dy + H, r + dx:r + dx + W] for dy in range(-r, r + 1) for dx in range(-r, r + 1)]
    return np.stack(stack)


def catmull_rom_x(img, shift):
    # history at x - shift (shift >= 0): base texel and fraction, 4-tap Catmull-Rom along x, clamped addressing
    pos = np.arange(W) - shift
    base = np.floor(pos)
    f = pos - base
    if abs(f[0]) < 1e-4 or abs(f[0] - 1) < 1e-4:
        idx = np.clip(np.round(pos).astype(int), 0, W - 1)
        return img[:, idx]
    w = [-.5 * f + f * f - .5 * f ** 3, 1 - 2.5 * f * f + 1.5 * f ** 3, .5 * f + 2 * f * f - 1.5 * f ** 3, -.5 * f * f + .5 * f ** 3]
    out = 0
    for k, wk in enumerate(w):
        idx = np.clip(base.astype(int) + k - 1, 0, W - 1)
        out = out + wk * img[:, idx]
    return out


def run(width, yaw, clip7, screen=False):
    # screen: the far weight on the screen speed gate (X3M_TAA_FAR_GATE=screen, the default since the far clip): a moving frame's
    # speed is above HI (0.25 px/frame), so keep is the base weight 0.9 there; the far clip still gates on openC (1: world-static).
    history = age = None
    disp, outs, ins, shifts = 0., [], [], []
    for n in range(FRAMES):
        i = n % PHASES + 1
        jx, jy = halton(i, 2) - .5, halton(i, 3) - .5
        v = yaw if n >= MOVE_FROM else 0.
        disp += v
        shifts.append(v)
        cur = scene(width, jx, jy, disp)
        ins.append(cur)
        if history is None:
            out, age = cur.copy(), np.ones_like(cur)
        else:
            old = catmull_rom_x(history, v)
            a = catmull_rom_x(age, v) if v == 0 else age[:, np.clip(np.floor(np.arange(W) - v + .5).astype(int), 0, W - 1)]
            if clip7:
                b = box(cur, 3)
                lo, hi = b.min(0), b.max(0)
            else:
                b = box(cur, 1)
                mean, sigma = b.mean(0), np.sqrt(np.maximum((b * b).mean(0) - b.mean(0) ** 2, 0))
                lo, hi = np.maximum(b.min(0), mean - 1.25 * sigma), np.minimum(b.max(0), mean + 1.25 * sigma)
            old = np.clip(old, lo, hi)
            keep = np.full_like(a, .9) if screen and v > 0 else np.minimum(a / (a + 1), .985)
            out = cur + keep * (old - cur)
            age = np.minimum(a + 1, 64)
        history = out.astype(np.float16).astype(np.float64)
        outs.append(history)
    at = np.concatenate([[0], np.cumsum(shifts)])
    spike, sparkles, delta = 0., 0, 0.
    xs = np.arange(X0, X1)
    for n in range(FRAMES - WINDOW - 1, FRAMES - 1):
        back, ahead = lround(at[n + 1] - at[n]), lround(at[n + 2] - at[n + 1])
        c = code(outs[n][Y0:Y1, X0:X1])
        before = np.max([code(outs[n - 1][Y0:Y1, xs - back + dx]) for dx in (-1, 0, 1)], axis=0)
        after = np.max([code(outs[n + 1][Y0:Y1, xs + ahead + dx]) for dx in (-1, 0, 1)], axis=0)
        margin = np.minimum(c - before, c - after)
        spike, sparkles = max(spike, margin.max()), sparkles + int((margin > 6).sum())
        if yaw == 0:
            delta = max(delta, np.abs(c - code(outs[n - 1][Y0:Y1, X0:X1])).max())
    dim = None
    if yaw == 0:
        mo = np.mean(outs[FRAMES - WINDOW:], axis=0)[Y0:Y1, X0:X1]
        mi = np.mean(ins[FRAMES - WINDOW:], axis=0)[Y0:Y1, X0:X1]
        line = mi > BACKGROUND * 1.5
        d = code(mo[line]) - code(mi[line])
        dim = (d.min(), d.max(), np.percentile(d, 10))
    return spike, sparkles, delta, dim


if __name__ == '__main__':
    for width in (1., .4):
        for yaw in (0., 10., 10.5, 8.25):
            for clip7, screen in ((True, False), (False, False)) + (((True, True),) if yaw else ()):
                spike, sparkles, delta, dim = run(width, yaw, clip7, screen)
                print('model gate=%s width=%.1f yaw=%.2f clip=%s spike_codes=%.3f sparkles=%d rest_delta_codes=%.3f dim_codes=%s' % (
                    'screen' if screen else 'camera', width, yaw, '7x7' if clip7 else '3x3', spike, sparkles, delta,
                    'n/a' if dim is None else 'min %.3f max %.3f p10 %.3f' % dim))
