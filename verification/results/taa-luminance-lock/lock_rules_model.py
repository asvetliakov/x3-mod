#!/usr/bin/env python3
"""CPU models for section 10 of docs/architecture/taa-luminance-lock.md, on the LUMA_LOCK fixture scene of
lane_model_fixture.py (8-phase Halton jitter, plate ramp 0.1 per px as a 16-px triangle wave, vertical 0.4-px struts of
luma 1.5 over 0.5 at [s + 0.115, s + 0.515) i.e. 0.185 px left of the texel centre, a slanted copy at 0.2 px per row,
a 0.37 px/frame pan over frames 16-39).

(b) Create rule at rest, plate vs strut: 'raw' = the built rule (sign-alternating change of the quantised current sample,
    |d| >= max(TAU, RHO * range3)); 'hist' = the same test on the residual e = q(Lc) - q(H at the jittered position),
    H the converged rest history (the per-texel mean over the 8 phases) sampled with Catmull-Rom at the jittered position;
    the residual test is the residual's own sign alternation, e_n * e_{n-1} < 0 with both above tau (one lane channel);
    'resid2' = the asymmetric form (max above tau, min above TAU only) with the Catmull-Rom history
    (the resolve's `old`, whose taps already carry the current jitter); 'histlin' = the same with the history sampled
    bilinearly (s11, the hardware-filtered previous colour, no overshoot at a kink). Prints lock share on the plate (x and y ramps),
    the vertical struts and the slanted line, and the strut formation frame.
(a) Transport under the pan (1D, per row): the lane stores the lock's sub-texel offset a (8-bit, 1/128 px, range [-1, 1))
    in the P1/P2 channels while the screen gate is closed; each frame the pixel reads the two texels around its
    reprojected position p = x - v and claims the lock at m whose carried world point m + a_m + v lies within
    0.5 + MARGIN of the pixel centre (max lifetime when both do); a' = a_m - (p - m). Prints the carry (share of the
    strut's true pixels locked on every pan frame), the dilation (locked pixels per strut) and the loss, for margin 0 and
    0.25, against the built nearest read (carry 0.00).
Usage: python3 lock_rules_model.py > lock_rules_model_out.txt"""
import math

import numpy as np


def halton(i, b):
    f, r = 1.0, 0.0
    while i:
        f /= b
        r += f * (i % b)
        i //= b
    return r


J = [(halton(n % 8 + 1, 2) - .5, halton(n % 8 + 1, 3) - .5) for n in range(8)]
q = lambda L: max(L, 0) / (1 + max(L, 0)) * 255
code = lambda L: math.floor(q(L) + .5)
ROWS = range(4, 12)


def plate(cx, cy, axis='x'):
    t = (cx if axis == 'x' else cy) % 16
    return .5 + .1 * abs(t - 8)


def strut(cx, cy, slope=0.0):
    m = (cx - slope * cy) % 16 - 8.115
    return 1.5 if 0 <= m < .4 else .5


def catmull_rom(grid, x, y):
    """Catmull-Rom sample of grid[y][x] (dict keyed (x, y)) at a continuous position, texel centres at integers."""
    def w(t):
        t = abs(t)
        return (1.5 * t - 2.5) * t * t + 1 if t < 1 else ((-0.5 * t + 2.5) * t - 4) * t + 2 if t < 2 else 0.0
    bx, by = math.floor(x), math.floor(y)
    s = 0.0
    for dy in range(-1, 3):
        for dx in range(-1, 3):
            s += w(x - (bx + dx)) * w(y - (by + dy)) * grid[(bx + dx, by + dy)]
    return s


def bilinear(grid, x, y):
    bx, by = math.floor(x), math.floor(y)
    fx, fy = x - bx, y - by
    return ((1 - fx) * (1 - fy) * grid[(bx, by)] + fx * (1 - fy) * grid[(bx + 1, by)]
            + (1 - fx) * fy * grid[(bx, by + 1)] + fx * fy * grid[(bx + 1, by + 1)])


def rest_run(content, xs, rule, rho=.25, tau=2, T=16, frames=16, probe=None):
    """Creation at rest (frames 0..15, screen gate open); returns per-frame lifetime dicts."""
    ys = range(0, 16)
    sample = lambda x, y, n: content(x + .5 - J[n % 8][0], y + .5 - J[n % 8][1])
    H = {(x, y): sum(sample(x, y, n) for n in range(8)) / 8 for x in range(min(xs) - 3, max(xs) + 4) for y in ys}
    lane, out = {}, []
    for n in range(frames):
        jx, jy = J[n % 8]
        new = {}
        for y in ROWS:
            for x in xs:
                vals = [sample(x + dx, y + dy, n) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
                lc = code(sample(x, y, n))
                if rule in ('hist', 'resid2'):
                    lc -= code(catmull_rom(H, x - jx, y - jy))
                elif rule == 'histlin':
                    lc -= code(bilinear(H, x - jx, y - jy))
                if probe is not None and (x, y) == probe:
                    print(f'    probe {probe} frame {n}: residual/sample codes {lc}')
                if n == 0:
                    new[x, y] = (lc, lc, 0)
                    continue
                P1, P2, t = lane[x, y]
                tt = max(tau, rho * (q(max(vals)) - q(min(vals))))
                if rule == 'raw':
                    d0, d1 = lc - P1, P1 - P2
                    flip = d0 * d1 < 0 and abs(d0) >= tt and abs(d1) >= tt
                elif rule == 'resid2':
                    flip = lc * P1 < 0 and max(abs(lc), abs(P1)) >= tt and min(abs(lc), abs(P1)) >= tau
                else:
                    flip = lc * P1 < 0 and abs(lc) >= tt and abs(P1) >= tt
                t = T if flip else max(t - 1, 0)
                new[x, y] = (lc, P1, t)
        lane = new
        out.append({k: v[2] for k, v in lane.items()})
    return out


def share(life, pixels):
    return sum(life[p] > 0 for p in pixels) / len(pixels)


def creation_rows():
    xs = list(range(258, 302))
    plate_px = [(x, y) for x in range(24, 64) for y in ROWS]
    vert = [(x, y) for x in (264, 280, 296) for y in ROWS]
    for rule in ('raw', 'hist', 'histlin', 'resid2'):
        for axis in ('x', 'y'):
            lane = rest_run(lambda cx, cy: plate(cx, cy, axis), list(range(24, 64)), rule)
            print(f'{rule:4s} plate ramp along {axis}: lock share max {max(share(l, plate_px) for l in lane):.3f} '
                  f'mean {sum(share(l, plate_px) for l in lane) / len(lane):.3f}')
        if rule != 'raw':
            print(f'{rule:4s} kink column (x 40, the ramp maximum), residual per frame:')
            rest_run(lambda cx, cy: plate(cx, cy, 'x'), [40], rule, frames=8, probe=(40, 4))
        lane = rest_run(strut, xs, rule)
        form = next((n for n in range(16) if share(lane[n], vert) >= .9), -1)
        print(f'{rule:4s} vertical struts: form frame {form}, share at frame 8..15 {min(share(l, vert) for l in lane[8:]):.2f}')
        lane = rest_run(lambda cx, cy: strut(cx, cy, 0.2), xs, rule)
        covered = [(x, y) for x in xs for y in ROWS if any(strut(x + .5 - J[n][0], y + .5 - J[n][1], 0.2) > 1 for n in range(8))]
        shares = [share(l, covered) for l in lane[8:]]
        sd = {p: np.std([strut(p[0] + .5 - J[n][0], p[1] + .5 - J[n][1], 0.2) for n in range(8)]) for p in covered}
        locked = [p for p in covered if lane[15][p] > 0]
        energy = sum(sd[p] for p in locked) / max(sum(sd.values()), 1e-9)
        print(f'{rule:4s} slanted line (0.2 px/row): covered px {len(covered)}, lock share mean {sum(shares) / len(shares):.2f} '
              f'min {min(shares):.2f}; ripple energy on locked px (frame 15) {energy:.3f}')


def transport(margin, frames=24, v=.37, T=16, quant=128):
    """1D lane over one row: locks created at rest on the three strut texels (a = 0), then the pan."""
    xs = range(250, 400)
    struts = [264, 280, 296]
    lane = {x: (T if x in struts else 0, 0.0) for x in xs}
    disp, carry, dil, loss = 0.0, [], [], 0
    for n in range(frames):
        disp += v
        new = {}
        for x in xs:
            p = x - v
            base = math.floor(p)
            best_t, best_a = 0, 0.0
            for m in (base, base + 1):
                t_m, a_m = lane.get(m, (0, 0.0))
                if t_m <= 0:
                    continue
                a_new = a_m - (p - m)                   # the carried world point relative to this pixel's centre
                if abs(a_new) < .5 + margin:
                    if t_m > best_t:
                        best_t, best_a = t_m, a_new
            best_a = max(-1, min(1 - 1 / quant, math.floor(best_a * quant + .5) / quant))
            new[x] = (best_t, best_a)
        lane = new
        true_px = [math.floor(s + .315 + disp) for s in struts]  # the strut centre is at cx = s + 0.315 + disp; pixel x's cell is [x, x + 1)
        carry.append(sum(lane[t][0] > 0 for t in true_px) / len(struts))
        locked = sum(lane[x][0] > 0 for x in xs)
        dil.append(locked / len(struts))
    return min(carry), sum(carry) / len(carry), max(dil), sum(dil) / len(dil)


def transport_rows():
    for margin in (0.0, 0.25):
        cmin, cmean, dmax, dmean = transport(margin)
        print(f'transport margin {margin}: carry min {cmin:.2f} mean {cmean:.2f}; locked px per strut max {dmax:.2f} mean {dmean:.2f}')
    # long pan: quantisation drift of the accumulator over 180 frames at 73.37 px/frame (fractional part 0.37 per frame)
    cmin, cmean, dmax, dmean = transport(0.25, frames=180, v=.37)
    print(f'transport margin 0.25, 180 frames: carry min {cmin:.2f} mean {cmean:.2f}; locked px per strut max {dmax:.2f}')


if __name__ == '__main__':
    creation_rows()
    transport_rows()
