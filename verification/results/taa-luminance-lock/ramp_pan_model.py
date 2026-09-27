#!/usr/bin/env python3
"""Section 12: plate false locks under a pan with camera-gate creation. 1-D model of the fixture's luma ramp
(0.5 + 0.1 |t - 8|, a 16-px triangle wave) as the built resolve treats an unlocked plate: the history is the previous
history reprojected by the pan (bilinear at position - v, standing in for the 5-tap Catmull-Rom) blended at the base
weight 0.85 with the jittered sample; the residual's prediction is the bilinear previous history at
(position - jitter) - v, i.e. `previousUV - current jitter`; rule resid2 with floor TAU and RHO 0.25 on the 3x3 range;
creation open (camera gate); the previous residual is read at the nearest reprojected texel round(x - v) (the lane is
transported, so the chain is per world point; an earlier revision compared residuals per screen texel and locked the
kinks at any integer speed). Pan speeds 73.37 (the run351 pan at 1080p, fractional part 0.37), 0.37 and 0 px/frame,
along the ramp's axis; 48 frames from a cut, locks counted from frame 2. Prints the lock share of the 40 plate texels
(any frame) and the largest residual seen on the plate, per TAU 2 / 3 / 4.
Usage: python3 ramp_pan_model.py > ramp_pan_model_out.txt"""
import math


def halton(i, b):
    f, r = 1.0, 0.0
    while i:
        f /= b
        r += f * (i % b)
        i //= b
    return r


J = [halton(n % 8 + 1, 2) - .5 for n in range(64)]
q = lambda L: max(L, 0) / (1 + max(L, 0)) * 255
code = lambda L: math.floor(q(L) + .5)
ramp = lambda c: .5 + .1 * abs(c % 16 - 8)
N = 96                      # texels 0..95 (six ramp periods), periodic: the history wraps, the plate measured on 24..63


def bilinear(h, pos):
    b = math.floor(pos)
    f = pos - b
    return (1 - f) * h[b % N] + f * h[(b + 1) % N]


def run(v, tau_floor, frames=48, rho=.25):
    disp = 0.0
    sample = lambda x, n, d: ramp(x + .5 - J[n % 8] - d)
    hist = [sample(x, 0, 0.0) for x in range(N)]
    prev = [0] * N
    locked, emax = set(), 0
    for n in range(1, frames):
        disp += v
        new = [0.0] * N
        for x in range(N):
            reproj = bilinear(hist, x - v)                    # the history of this pixel's world point
            new[x] = reproj + .15 * (sample(x, n, disp) - reproj)
        newprev = [0] * N
        for x in range(N):
            pred = bilinear(hist, (x - J[n % 8]) - v)         # previousUV - current jitter
            e = code(sample(x, n, disp)) - code(pred)
            vals = [sample(x + d, n, disp) for d in (-1, 0, 1)]
            tau = max(tau_floor, rho * (q(max(vals)) - q(min(vals))))
            if 24 <= x < 64:
                emax = max(emax, abs(e))
            pe = prev[int(math.floor(x - v + .5)) % N]      # the previous residual of this world point (nearest texel)
            if n >= 2 and 24 <= x < 64 and e * pe < 0 and max(abs(e), abs(pe)) >= tau and min(abs(e), abs(pe)) >= tau_floor:
                locked.add(x)
            newprev[x] = e
        hist = new
        prev = newprev
    return len(locked) / 40, emax


for tau in (2, 3, 4):
    for v in (0.0, 0.37, 73.37):
        share, emax = run(v, tau)
        print(f'tau {tau} pan {v:6.2f} px/frame: plate lock share {share:.3f}, max |residual| {emax} codes')
