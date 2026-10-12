#!/usr/bin/env python3
"""Model timeline of an AI gate transit from the engine laws quoted in ship-navigation-ai.md section 10.

Inputs are measured constants (order 4/5/8/0x1f handlers, KC GetWarpInPos) and the Mayhem 4 class medians and gate
radius of game-scale-and-ship-classes.md; the outputs are a kinematic model, i.e. inferred, not observed flight data.
Rotation is modelled at the full yaw rate (no angular acceleration), so turn times are lower bounds.

    python3 -I verification/results/ship-navigation-ai/gates/gate_timeline.py > gate_timeline.txt
"""
import math

R_GATE = 1105.0                      # m, Mayhem gate types 0-3/5-14 (game-scale note)
CLASSES = {  # name: (vmax m/s, accel m/s^2, yaw deg/s, radius m, large, capital)  Mayhem medians
    'M5': (541, 55.4, 147.7, 10, False, False), 'M3': (267, 40.0, 92.8, 15, False, False),
    'TS': (124, 20.0, 18.0, 55, False, False), 'M6': (186, 20.0, 33.1, 90, True, False),
    'M2': (104, 10.0, 11.2, 1337, True, True), 'TL': (104, 10.0, 13.5, 1135, True, True)}


def ramp(x, x0, y0, x1, y1):         # 0x0042fb20, clamped linear
    if x <= x0: return y0
    if x >= x1: return y1
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def order4_boundary(v, a, large):
    """Vanilla order 4 toward a gate point: speed when crossing d_arr = 0.75 r (then 0x00454b94 zeroes it)."""
    d_arr = 0.75 * R_GATE
    d_slow = d_arr + v * v / a        # radius + 0x00469a30(vmax, vmax, a)
    d, s, dt = d_slow * 3 + 2000, v, 0.001
    while d > d_arr:
        want = v if d > d_slow else ramp(d, 0, 10.0, d_slow, v / 2)
        if large and want > 100.0 and d <= d_slow + 3 * R_GATE: want = 100.0   # 50000 cap (0x004550f9)
        s = min(want, s + a * dt) if want > s else max(want, s - a * dt)
        d -= s * dt
    return s


def order5(v, a, r_ship, start, target, heading_err_deg, w):
    """From rest at `start` to `target`: turn at 1 m/s while misaligned beyond 18.2 deg, then the gate ramp."""
    t_turn = max(0.0, heading_err_deg - 18.2) / w
    L = math.dist(start, target); d, s, t, dt = L, 0.0, 0.0, 0.001
    while d > 0.1 * s + r_ship:       # arrival (|v|/10 + r) at time factor 1 (0x004546e3)
        want = ramp(d, R_GATE / 5, v / 2, R_GATE, v)
        s = min(want, s + a * dt) if want > s else max(want, s - a * dt)
        d -= s * dt; t += dt
    return t_turn, t, s, L


A = (0.0, -0.8 * R_GATE, -1.0 * R_GATE)          # order-4 point, gate-local (x, y, z), z = gate axis
B0 = (0.0, -0.55 * R_GATE, 0.0)                  # order-5 point at theta = 0
print('gate radius %.0f m; A = %s m; B(theta) = (0, -0.55 cos, 0.55 sin) r; |A-B0| = %.0f m' % (R_GATE, A, math.dist(A, B0)))
for th in (-45, 0, 45):
    B = (0.0, -0.55 * R_GATE * math.cos(math.radians(th)), 0.55 * R_GATE * math.sin(math.radians(th)))
    dv = [B[i] - A[i] for i in range(3)]
    print('  theta %+3d deg: B = (%.0f, %.0f, %.0f) m, |AB| = %.0f m, bearing A->B off the gate axis %.1f deg'
          % (th, *B, math.dist(A, B), math.degrees(math.atan2(math.hypot(dv[0], dv[1]), dv[2]))))
bear = []
for k in range(-450, 451):
    th = math.radians(k / 10)
    y, z = -0.55 * math.cos(th) - A[1] / R_GATE, 0.55 * math.sin(th) - A[2] / R_GATE
    bear.append(math.degrees(math.atan2(y, z)))
lo, hi = min(bear), max(bear); mid = (lo + hi) / 2
share = sum(b > 18.2 for b in bear) / len(bear)
print('  bearing A->B over theta in [-45, 45]: %.1f..%.1f deg above the axis; %.0f%% of theta exceed 18.2 deg for an '
      'axial arrival; aiming the arrival heading %.1f deg above the axis leaves <= %.1f deg' % (lo, hi, 100 * share, mid, (hi - lo) / 2))
print('order 8: s(t) = 1.665 t^2 units (t ms): %.0f m in 0.9 s, end speed %.0f m/s' % (1.665 * 900 ** 2 / 500, 3.33 * 900 * 1000 / 500))
print()
print('%-3s %8s %10s %10s | %-28s | %-28s' % ('cls', 'vmax', 'o4 vanilla', 'o4 mod', 'o5 axial arrival: turn+fly s, v@B', 'o5 side arrival (90 deg)'))
for name, (v, a, w, rs, large, cap) in CLASSES.items():
    if cap: continue
    v4 = order4_boundary(v, a, large)
    t0, t1, vb, L = order5(v, a, rs, A, B0, 14.0, w)
    u0, u1, _, _ = order5(v, a, rs, A, B0, 90.0, w)
    print('%-3s %6.0f m/s %7.1f m/s %7.1f m/s | %4.1f + %4.1f s, %5.1f m/s stop | %4.1f + %4.1f s, then 0.9 s warp'
          % (name, v, v4, 10.0, t0, t1, vb, u0, u1))
print()
print('capital slots: P_s = G - 2 (r_gate + r_ship) z + 666 m (a x + b y), a = {0,1,-1,2,-2}[s%5], b = -(s/5)')
for name, (v, a, w, rs, large, cap) in CLASSES.items():
    if not cap: continue
    D = 2 * (R_GATE + rs); rows = []
    for s_ in range(20):
        ax, by = (0, 1, -1, 2, -2)[s_ % 5], -(s_ // 5)
        lat = 666.0 * math.hypot(ax, by); phi = math.degrees(math.atan2(lat, D))
        t0, t1, vb, L = order5(v, a, rs, (lat, 0.0, -D), (0.0, 0.0, 0.0), phi, w)
        rows.append((s_, phi, t0, t1, vb))
    turned = [r_[0] for r_ in rows if r_[1] > 18.2]
    print('%s: D = %.0f m; order-5 bearing off axis %.1f..%.1f deg; slots needing a second in-place turn: %s; '
          'order 5 slot 0: %.1f s to the origin, %.1f m/s stop; RotateToAngles from 90 deg >= %.1f s'
          % (name, D, min(r[1] for r in rows), max(r[1] for r in rows), turned or 'none', rows[0][3], rows[0][4], (90 - 8.1) / w))
