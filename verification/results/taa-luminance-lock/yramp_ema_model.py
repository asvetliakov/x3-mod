#!/usr/bin/env python3
"""Why the LUMA_LOCK y-ramp plate row fails (2026-09-28 fixture: the whole row y = 7 locks, 12.5 % of the plate).
1-D column model of the fixture's y ramp (0.5 + 0.1 |t - 8|, t = content y mod 16, the V's vertex between texel rows 7
and 8) under the 8-phase Halton y jitter: the residual e = code(sample) - code(bilinear history at the jittered position)
with the history either the exact phase mean (section 10's model: `mean`) or the resolve's exponential history at the base
weight 0.85 (`ema`, from a cut); the section-10 rule (e_n e_(n-1) < 0, max |e| >= max(2, 0.25 range3), min |e| >= 2).
Prints per row whether it ever locks in frames 2..40 and the row's residual codes over one cycle.
Usage: python3 yramp_ema_model.py > yramp_ema_model_out.txt"""
import math


def halton(i, b):
    f, r = 1.0, 0.0
    while i:
        f /= b
        r += f * (i % b)
        i //= b
    return r


JY = [halton(n % 8 + 1, 3) - .5 for n in range(64)]
q = lambda L: max(L, 0) / (1 + max(L, 0)) * 255
code = lambda L: math.floor(q(L) + .5)
plate = lambda cy: .5 + .1 * abs(cy % 16 - 8)
ROWS = range(2, 14)


def run(history_kind, frames=41):
    sample = lambda y, n: plate(y + .5 - JY[n % 8])
    mean = {y: sum(sample(y, n) for n in range(8)) / 8 for y in range(0, 16)}
    hist = {y: sample(y, 0) for y in range(0, 16)}
    prev = {y: 0 for y in ROWS}
    locked, trace = set(), {y: [] for y in ROWS}
    for n in range(1, frames):
        H = mean if history_kind == 'mean' else hist
        new_hist = {}
        for y in range(0, 16):
            new_hist[y] = hist[y] + .15 * (sample(y, n) - hist[y])  # base weight 0.85 (unlocked plate), no clip needed on a ramp
        for y in ROWS:
            pos = y - JY[n % 8]                                   # the jittered sample's position in texel coordinates
            base = math.floor(pos)
            f = pos - base
            predicted = (1 - f) * H[base] + f * H[base + 1]
            e = code(sample(y, n)) - code(predicted)
            vals = [sample(y + d, n) for d in (-1, 0, 1)]
            tau = max(2, .25 * (q(max(vals)) - q(min(vals))))
            if n >= 2 and e * prev[y] < 0 and max(abs(e), abs(prev[y])) >= tau and min(abs(e), abs(prev[y])) >= 2:
                locked.add(y)
            prev[y] = e
            if 32 <= n < 40:
                trace[y].append(e)
        hist = new_hist
    return locked, trace


for kind in ('mean', 'ema'):
    locked, trace = run(kind)
    print(f'{kind}: rows locking {sorted(locked)}')
    for y in (6, 7, 8, 9):
        print(f'  row {y} residual codes frames 32-39: {trace[y]}')
