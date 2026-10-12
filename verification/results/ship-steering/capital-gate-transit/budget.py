#!/usr/bin/env python3
"""Phase budget model for docs/architecture/capital-gate-transit.md (inferred: a kinematic model of the engine laws
quoted in ship-navigation-ai.md section 10 and ship-movement.md section 4, with the Run156 record values of the
three traced capitals). Nothing here is flight data except the 'measured' columns copied from
run156-triage/capital_timeline.txt.

    python3 -I verification/results/ship-steering/capital-gate-transit/budget.py > budget.txt
"""
import math

R_GATE = 1105.0           # m, the installed ordinary gate (ship-navigation-ai.md 10.9)
DT = 1 / 60.0
TOL_0x1F = 8.11           # deg, 0xfd70 = cos 8.1 deg
# Run156 trace rows (ship_trace W/rot/rot_pct/vmax/acc; r_ship from the grid depth 2 (r_gate + r_ship)): measured
SHIPS = {
    'M2 (0x8c71c728)': dict(vmax=110.0, acc=10.0, W=1843 / 65536 * 360, rot=0.5, rot_pct=10, r=4392 / 2 - R_GATE),
    'M1 (0x8c7228a8)': dict(vmax=113.0, acc=5.0,  W=1638 / 65536 * 360, rot=0.5, rot_pct=10, r=4253 / 2 - R_GATE),
}
LARGE_CAP = 100.0         # m/s, order-4 cap for large hulls (0x004550ed)


def controller(e, w, W, a, dt):
    """0x0043ad20 in floats: brake when e < w*w/a (twice the stopping distance), else accelerate to W; returns (w', delta)."""
    if e < w * w / a:
        w2 = max(0.0, w - a * dt)
    else:
        w2 = min(W, w + a * dt)
    delta = min(e, 0.5 * (w + w2) * dt)
    return w2, delta


def rotate_time(theta, W, a, shaped):
    """Time for order 0x1f from theta deg off to within TOL (engine-written remainder, or the module's shaped
    remainder that keeps the controller accelerating until the true stopping distance w^2/(2a))."""
    e, w, t = theta, 0.0, 0.0
    while e > TOL_0x1F:
        e_written = e
        if shaped and e > w * w / (2 * a) + 0.5:
            e_written = max(e, w * w / a + 1.0)   # never let the early-brake test fire before the true stopping point
        w, delta = controller(e_written, w, W, a, DT)
        e -= min(delta, e)
        t += DT
        if t > 600: break
    return t


def ramp(x, x0, y0, x1, y1):
    if x <= x0: return y0
    if x >= x1: return y1
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def approach_time(s, vmax, acc, hold):
    """Along the axis from 3 r_gate before the crossing X (= 0.75 r_gate before the grid point A) to X.
    hold: the module writes the large cap in both directions (no engine ramp); else the engine's order-4 gate law:
    vmax beyond d_slow = 0.75 r + vmax^2/a, ramp(dist A; 0 -> 10 m/s, d_slow -> vmax/2) inside, large cap 100 m/s."""
    d_arr = 0.75 * R_GATE
    d_slow = d_arr + vmax * vmax / acc
    d = d_arr + 3 * R_GATE
    v, t = min(s['vmax'], LARGE_CAP), 0.0
    while d > d_arr:
        want = LARGE_CAP if hold else min(LARGE_CAP, (vmax if d > d_slow else ramp(d, 0, 10.0, d_slow, vmax / 2)))
        v = min(want, v + acc * DT) if want > v else max(want, v - acc * DT)
        d -= v * DT
        t += DT
    return t, v


def order5_time(depth_x, v0, vmax, acc, r_ship, seta):
    """Order 5 from the crossing X (depth_x in front of the gate origin) to the arrival sphere
    (|v|/10 + r_ship) (1 + (seta - 1)/4), aligned: desired vmax (dist >= r_gate), slewed at acc."""
    d, v, t = depth_x, v0, 0.0
    while True:
        r_arr = (v / 10 + r_ship) * (1 + (seta - 1) / 4)
        if d <= r_arr: return t, v, r_arr
        want = vmax if d >= R_GATE else ramp(d, R_GATE / 5, vmax / 2, R_GATE, vmax)
        v = min(want, v + acc * DT) if want > v else max(want, v - acc * DT)
        d -= v * DT
        t += DT


print('== order 0x1f rotate time, s (model; measured Run156: M2 90.6 deg 29.2 s, M2 152.1 deg 34.4 s, M1 109.3 deg 44.9 s)')
print('ship              a_rot deg/s2  angle   engine   shaped   engine@rot=1.0  shaped@rot=1.0')
for name, s in SHIPS.items():
    a = s['W'] * s['rot'] * s['rot_pct'] / 100
    for th in (20.0, 45.0, 90.6, 109.3, 152.1):
        print('%-17s %6.2f       %6.1f  %6.1f   %6.1f   %6.1f          %6.1f' % (name, a, th,
              rotate_time(th, s['W'], a, False), rotate_time(th, s['W'], a, True),
              rotate_time(th, s['W'], 2 * a, False), rotate_time(th, s['W'], 2 * a, True)))

print()
print('== phase budget, s, from 3 r_gate (3315 m) before the crossing X to the end of the launch (model)')
print('   A = grid point (depth 2 (r_gate + r_ship)), X = A - 0.75 r_gate F (the order-4 arrival sphere, measured 823-828 m);')
print('   vanilla-aligned = engine ramp to X, stop at X (P2), 0x1f one frame, order 5 from rest; continuous = module holds')
print('   the large cap to X, P2 keeps the speed, 0x1f one frame, order 5 from the crossing speed; launch 0.93 s')
print('ship              seta  approach  cross_v  o5_vanilla  o5_cont  r_arr_m  total_vanilla  total_cont')
for name, s in SHIPS.items():
    depth_x = 2 * (R_GATE + s['r']) + 0.75 * R_GATE
    for seta in (1, 10):
        ta_v, vx_v = approach_time(s, s['vmax'], s['acc'], False)
        ta_c, vx_c = approach_time(s, s['vmax'], s['acc'], True)
        t5_v, _, r_arr = order5_time(depth_x, 0.0, s['vmax'], s['acc'], s['r'], seta)
        t5_c, _, _ = order5_time(depth_x, vx_c, s['vmax'], s['acc'], s['r'], seta)
        print('%-17s %4d   %5.1f/%5.1f %4.0f/%4.0f   %6.1f    %6.1f   %6.0f     %6.1f       %6.1f' % (
            name, seta, ta_v, ta_c, vx_v, vx_c, t5_v, t5_c, r_arr, ta_v + 1 / 60 + t5_v + 0.93, ta_c + 1 / 60 + t5_c + 0.93))
print()
print('== order-5 arrival sphere check against Run156 (measured dG at the order-5 end: M2 3584/3577, M2 3569, M1 3346 m)')
for name, s, v in (('M2 leader', SHIPS['M2 (0x8c71c728)'], 108.0), ('M2 second', SHIPS['M2 (0x8c71c728)'], 69.0), ('M1', SHIPS['M1 (0x8c7228a8)'], 80.0)):
    print('   %-10s (v/10 + r_ship) x (1 + (10 - 1)/4) = %.0f m' % (name, (v / 10 + s['r']) * 3.25))
print()
print('== turn geometry at the large cap (model): R = v / W, 90 deg turn time 2 sqrt(90 / a_rot) (controller from rest)')
for name, s in SHIPS.items():
    a = s['W'] * s['rot'] * s['rot_pct'] / 100
    print('   %-17s R = %.0f m at 100 m/s; 90 deg: %.0f s = %.0f m of path' % (name, 100.0 / math.radians(s['W']), 2 * math.sqrt(90 / a), 100 * 2 * math.sqrt(90 / a)))
