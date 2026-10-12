#!/usr/bin/env python3
"""Offline model of the per-axis rotation controller 0x0043ad20 as the integrator
0x004596e0 drives it (docs/reverse-engineering/ship-movement.md section 3). Integer
16.16 arithmetic re-derived from the disassembly; it is a model, not a capture.

  python3 verification/results/ship-movement/axis_controller_sim.py

For a ship that is commanded once with a remaining angle e (the steering leaves
write obj+0x30/0x34/0x38 once per think tick), it prints the time until the
remaining angle reaches 0, the peak rate, and the time spent accelerating,
cruising and braking. Inputs: max rate w (record +8/+0xc/+0x10, binary angle per
second, 65536 = one turn), angular acceleration a = w * record+0xd0 / 100.
"""


def fx(a, b):  # (a*b + 0x8000) >> 16, as the engine's imul/add/adc/shrd sequence
    return (a * b + 0x8000) >> 16


def div16(a, b):  # (a << 16) / b, truncating toward zero (idiv)
    if b == 0:
        return 0
    q = abs(a << 16) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def controller(e, w, wmax, acc, dt_ms):
    """0x0043ad20(e, w, wmax, acc, &new_w, &delta, dt) for e >= 0 (caller mirrors sign)."""
    t = div16(dt_ms, 1000)
    if wmax == 0:
        return 0, 0
    acc = max(acc, 1)
    if w < 0:
        ts = div16(w, acc)
        if -ts < t:
            return 0, fx(w, -ts)
        return fx(t, acc) + w, fx(w, t)
    ts = div16(w, acc)
    dstop = fx(w, ts)
    if e < dstop:
        if ts < t:
            return 0, dstop
        return w - fx(t, acc), fx(w, t)
    tc = div16(e - dstop, wmax)
    if t <= tc:
        ta = div16(wmax - w, acc)
        if t < ta:
            return w + fx(t, acc), fx(w, t)
        return wmax, fx(w, ta) + fx(wmax, t - ta)
    nw = fx(tc, acc) + w
    if nw < wmax:
        ta = tc
    else:
        ta = div16(wmax - w, acc)
        nw = wmax
    d0 = fx(w, ta)
    rest = max(e - d0 - dstop, 0)
    tr = div16(rest, nw) if nw else 0
    if t <= tr + ta:
        return nw, fx(nw, t - ta) + d0
    tb = t - tr - ta
    return nw - fx(tb, acc), fx(nw, tb) + rest + d0


def run(name, w_turn_per_s, accel_pct, angle_deg, dt_ms=16):
    wmax = round(w_turn_per_s * 65536)
    acc = fx(wmax, (0x10000 * accel_pct) // 100)
    e = round(angle_deg / 360 * 65536)
    w, t_ms, peak, phases = 0, 0, 0, []
    # Settled = remaining angle under 0.5 deg (91 units). The 16.16 rounding of
    # delta = w*t leaves a residue of a few units (< 0.05 deg) at 60 fps that the
    # model never removes; in the game the next think tick re-commands the axis.
    while e > 91 and t_ms < 600000:
        nw, d = controller(e, w, wmax, acc, dt_ms)
        if e == d or e - d < 0:  # integrator clamp after the call
            nw, d = 0, e
        e -= d
        phases.append('a' if nw > w else ('b' if nw < w else 'c'))
        w = nw
        peak = max(peak, w)
        t_ms += dt_ms
    a, c, b = phases.count('a'), phases.count('c'), phases.count('b')
    print(f'{name:10s} w={wmax:6d} acc={acc:6d} angle={angle_deg:5.1f}  settle(<0.5deg)={t_ms / 1000:6.2f} s '
          f'peak={peak / 65536 * 360:6.1f} deg/s  accel/cruise/brake frames={a}/{c}/{b}')


if __name__ == '__main__':
    # TShips rows 1, 2 and 20 of /mnt/c/X3/ZMap/TShips.txt: rotation X, column 51 (record +0xd0)
    for angle in (10, 45, 90, 180):
        run('TL 0.0375', 0.0375, 10, angle)
        run('TS 0.0586', 0.058594, 30, angle)
        run('M4 0.3594', 0.359375, 50, angle)
