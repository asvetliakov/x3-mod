#!/usr/bin/env python3
"""Predicts the temporal_pass_fixture.cpp MOTION_WEIGHT pan12.5 row (motion_weight_cases, line ~3712: grey stripe
v = .625 + .375 sin(2 pi (x - jx - disp) / 4), 16-phase Halton jitter, 48 frames, displacement from frame 16 at 12.5 px/frame,
last 16 frames measured on x 386..508, y 2..13) with replay_common.resolve at k = 0 for keep 0.9 / 0.85 / 0.8.
e_ratio = sum of the 5-point Laplacian^2 of the output over that of the current sample; ripple_rms = rms of
out[n](x) - out[n-2](x-25). Calibration target: the committed fixture's age-program pan12.5 row at 0.9, e_ratio 0.1968-0.1975,
ripple_rms 0.00943-0.00946 (verification/results/far-weight-camera-gate/). A Python replica, not the GPU."""
import numpy as np
import sys; sys.path.insert(0, "/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay")
from replay_common import resolve
W, H, PER, V = 512, 16, 4.0, 12.5
def halton(i, b):
    f, r = 1., 0.
    while i > 0: f /= b; r += f * (i % b); i //= b
    return r
ys, xs = np.mgrid[-1:H + 1, -1:W + 1].astype(np.float64)  # the crop with its 1-px ring; interior = pixel indices 0..W-1
X0, X1, Y0, Y1 = 386, W - 3, 2, H - 2
for w in (0.9, 0.85, 0.7):
    hist = None; outs = []; curs = []; prev = 0.
    for n in range(48):
        idx = n % 16 + 1; jx = halton(idx, 2) - .5
        disp = V * (n - 16 + 1) if n >= 16 else 0.; vx = disp - prev; prev = disp
        c = (.625 + .375 * np.sin(2 * np.pi * (xs - (jx + disp)) / PER))[..., None].repeat(3, -1)
        o = c[1:-1, 1:-1] if hist is None else resolve(c, np.pad(hist, ((0, 0), (0, 0), (0, 0))), xs[1:-1, 1:-1] - vx, ys[1:-1, 1:-1], w, 0.0)
        hist = o; outs.append(o[..., 0]); curs.append(c[1:-1, 1:-1, 0])
    def lap(a): return (4 * a[Y0:Y1, X0:X1] - a[Y0:Y1, X0 - 1:X1 - 1] - a[Y0:Y1, X0 + 1:X1 + 1] - a[Y0 - 1:Y1 - 1, X0:X1] - a[Y0 + 1:Y1 + 1, X0:X1]) ** 2
    eo = sum(lap(outs[n]).sum() for n in range(32, 48)); ec = sum(lap(curs[n]).sum() for n in range(32, 48))
    rip = np.sqrt(np.mean([((outs[n][Y0:Y1, X0:X1] - outs[n - 2][Y0:Y1, X0 - 25:X1 - 25]) ** 2).mean() for n in range(32, 48)]))
    print(f'keep {w:.2f}: e_ratio {eo / ec:.4f} ripple_rms {rip:.5f}')
