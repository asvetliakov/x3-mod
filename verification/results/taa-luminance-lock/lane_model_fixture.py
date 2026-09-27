#!/usr/bin/env python3
"""CPU model of the luminance-lock lane (resolve.hlsl X3M_LUMA_LOCK as implemented 2026-09-28) on the LUMA_LOCK fixture
scene (temporal_luma_lock_inc.h): point samples at the jittered position under the 8-phase Halton jitter, the plate ramp,
vertical 0.4-px struts at [s + 0.115, s + 0.515), a 0.37 px/frame pan (frames 16-39, the screen gate closed), a cut at 40.
Models the lane only (P1, P2, lifetime, reference; the q-domain flip test, the own-screen-gate freeze, the 3x3-mean
release), not the colour. The nearest reprojected texel is the pixel's own at |v| < 0.5 px/frame, so the lane does not
move with the content during the pan. Prints the rows the fixture measures on the lane: form, plate share, carry.
Usage: python3 lane_model_fixture.py > lane_model_fixture_out.txt"""
import math


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


def plate(cx, cy, axis='x'):
    t = (cx if axis == 'x' else cy) % 16
    return .5 + .1 * abs(t - 8)


def strut(cx, cy, slope=0.0):
    m = (cx - slope * cy) % 16 - 8.115
    return 1.5 if 0 <= m < .4 else .5


def run(content, xs, rho=.25, tau=2, release=.75, T=16, gate_always=False, frames=56):
    """Lane per pixel (x, y in rows 1..14): returns per frame the lifetime dict and the displacement."""
    lane = {}
    disp, out = 0.0, []
    for n in range(frames):
        jx, jy = J[n % 8]
        v = .37 if 16 <= n < 40 else 0.0
        if n == 40:
            disp = 0.0
        disp += v
        fresh = n in (0, 40)
        open_gate = gate_always or v <= .14
        new = {}
        for y in range(1, 15):
            for x in xs:
                sample = lambda dx, dy: content(x + dx + .5 - jx - disp, y + dy + .5 - jy)
                vals = [sample(dx, dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
                lc = code(sample(0, 0))
                if fresh:
                    new[x, y] = (lc, lc, 0, 0)
                    continue
                P1, P2, t, R = lane.get((x, y), (lc, lc, 0, 0))  # own texel at |v| < 0.5
                tt = max(tau, rho * (q(max(vals)) - q(min(vals))))
                d0, d1 = lc - P1, P1 - P2
                flip = d0 * d1 < 0 and abs(d0) >= tt and abs(d1) >= tt
                mean = math.floor(q(sum(vals) / 9) + .5)
                t = max(t - 1, 0) if open_gate else t
                if min(R, mean) < release * max(R, mean):
                    t = 0
                create = flip and open_gate
                if create:
                    t = T
                new[x, y] = (lc, P1, t, mean if create else (R if t > 0 else 0))
        lane = new
        out.append((disp, {k: v[2] for k, v in lane.items()}))
    return out


def share(life, pixels):
    return sum(life[p] > 0 for p in pixels) / len(pixels)


def main():
    rows = range(4, 12)
    struts = [(x, y) for x in (264, 280, 296) for y in rows]
    xs = list(range(258, 302))
    for name, kw in (('lock', {}), ('lock_rho0.5', {'rho': .5}), ('lock_always', {'gate_always': True})):
        lane = run(strut, xs, **kw)
        form = next((n for n in range(16) if share(lane[n][1], struts) >= .9), -1)
        form_cut = next((n - 40 for n in range(40, 56) if share(lane[n][1], struts) >= .9), -1)
        carry = []
        for n in range(16, 40):
            d = lane[n][0]
            px = [(x, y) for x in xs for y in rows if -1 < (x - (8.115 + d)) % 16 - (16 if (x - (8.115 + d)) % 16 > 15 else 0) < .4]
            carry.append(share(lane[n][1], px) if px else 0.0)
        print(f'{name}: form={form} form_after_cut={form_cut} carry_min={min(carry):.3f} carry_first8={[round(c, 2) for c in carry[:8]]}')
    for axis in ('x', 'y'):
        for rho in (.25, .5):
            lane = run(lambda cx, cy: plate(cx, cy, axis), list(range(24, 64)), rho=rho)
            pixels = [(x, y) for x in range(24, 64) for y in rows]
            rest = max(share(lane[n][1], pixels) for n in list(range(16)) + list(range(41, 56)))
            print(f'plate ramp 0.1/px along {axis}, rho {rho}: lock share max at rest {rest:.3f}')


if __name__ == '__main__':
    main()
