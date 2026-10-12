"""Replay conformance of the ship steering fixture's engine model against ship_trace rows.

For every followed ship and every pair of consecutive frames (n, n+1) of a trace (a file of `ship_trace` rows or a
whole x3m.log, streamed), frame n's inputs (pos, fwd, up, rate, v, des, e, W, rot, rot_pct, vmax, acc, dt_ms) go
through a Python port of the engine model of verification/probe/ship_steering_host.cpp and the prediction of frame
n+1's rate, v, fwd and pos is compared with the actual row n+1. The model (ship-movement.md sections 3, 4 and 13):
the per-axis controller 0x0043ad20 in 16.16 integers; the integrator's axis step (a zero remainder sets the rate to 0
at once, W*k == 0 skips the axis); the slew of the speed toward the desired speed at acc*dt/1000 with the 1-unit
minimum; the rotation of the flight basis by the turned angles; the fixed-point velocity, slip and position update
(p+0x100 = fx(row2, speed); off order 5 the part of last frame's p+0x100 perpendicular to the new row2 is added to the
slip; position += (slip + p+0x100) dt / 1000; slip *= (2000 - dt) / 2000); on order 5 to a port the port-roll match
after the leaf (all three written remainders below 0x51e: roll = wrap(port roll - ship roll), the port roll per leg
from port_rolls()); the final docking zone (no leaf call, v = des = 0: the rate fields hold the per-frame step of a
kinematic turn at 0x38e or 0x71c binary/s; the slip is zeroed); order 8 (pos = f+0x240 + axis * s(u), u from the
run's first row, s = trunc(u^2 * 1.665) + v0 u / 1000).

Residuals per step: rate (per axis, binary angle/s), speed (m/s), heading (deg between predicted and actual fwd),
position (m), and the conditional position residual pos_c (m): the translation law alone with row n+1's heading and
speed (and the carried slip). Steps whose inputs are not in the row are named in `unmodelled` and judged only on what
is known (final zone: the step magnitude and v = 0; port roll unknown: not the roll; launch axis: the magnitude).

A divergent step (any judged residual over its tolerance) is classified, first match wins:
  order boundary   the order differs between n-1, n or n+1
  final zone       a final-zone step whose rate is neither 0 nor the per-frame step cap x dt(n+1) / 1000 (+-2.5) nor the
                   turn's last (smaller) step
  position         pos_c over tolerance (the translation law: units, dt, the slip term or an outside writer)
  rate controller  a rate residual
  orientation      a heading residual while the rate matches (a sign or axis convention)
  speed law        a speed residual while the rate matches
  position         the remaining position residual
Consecutive divergent steps form a window; a window is "order boundary" when all its steps are, otherwise the most
frequent class of its other steps.

The velocity's lag behind the nose (amendment 14, slip-aware-steering.md section 5) per step, in degrees: lag_obs the
angle between row n -> n+1's displacement and fwd(n+1) (the flown velocity, judged when the step moved at least
LAG_MIN_UNITS and outside the final zone and order 8), lag_model the same for the model's conditional displacement, and
lag_row the angle between v fwd + slip of row n and fwd(n) when the row carries `slip=` (the value the law read). A row
with `slip=` seeds the carried slip (the integrator's obj+0x60 between two frames); slip_res is the carried value's
distance to it (m/s, after the first step of a run). The summary gives lag p50/p95/max overall and per order.

e: the row's written remainders when the module called the leaf; on an `e=-` row the remainder the model carried
from the previous step (the integrator consumes obj+0x30 across frames); a step with no known remainder is skipped.
The angular acceleration is acc_i = fx(W_i, k) with k = trunc(rot x rot_pct / 100) in 16.16 (ship-movement.md section 3:
k = f+0x2b0 x record+0xd0 / 100; the Run156 trace pins this operation order: exact on 76,188 of 76,190 leaf steps; the
fixture's rot is 1.0, where it equals its fx(W, k_pct x 0x10000 / 100)).

  python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --json out.json
  python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --gate
"""
import argparse
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ship_trace_rows as st  # noqa: E402

# core::kRadPerBinary = float(6.28318530717959f) / 65536.0f (src/proxy/ship_steering_core.h), as the fixture uses it
RAD_PER_BINARY = struct.unpack('f', struct.pack('f', 6.28318530717959))[0] / 65536.0
UNITS_PER_M = 500.0
CLASSES = ('order boundary', 'final zone', 'position', 'rate controller', 'orientation', 'speed law')
TOL = {'rate': 0.5, 'speed': 0.002, 'heading': 0.01, 'pos': 0.01, 'pos_c': 0.01}  # binary/s, m/s, deg, m, m
LAG_MIN_UNITS = 200  # a step shorter than 0.4 m carries no usable velocity direction (whole-unit positions)


# ---- the port of ship_steering_host.cpp (same names, same integer arithmetic) ----
def fx(a, b):
    """(a*b + 0x8000) >> 16, arithmetic shift."""
    return (a * b + 0x8000) >> 16


def div16(a, b):
    """(a << 16) / b truncating toward zero."""
    if b == 0:
        return 0
    q = abs(a << 16) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def tdiv(a, b):
    """C integer division (truncating toward zero)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def controller(e, w, wmax, acc, dt_ms):
    """0x0043ad20: (new rate, angle turned) for remaining e >= 0, rate w, cap wmax, acceleration acc."""
    t = div16(dt_ms, 1000)
    if wmax == 0:
        return 0, 0
    if acc < 1:
        acc = 1
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
    rest = e - d0 - dstop
    if rest < 0:
        rest = 0
    tr = div16(rest, nw) if nw else 0
    if t <= tr + ta:
        return nw, fx(nw, t - ta) + d0
    tb = t - tr - ta
    return nw - fx(tb, acc), fx(nw, tb) + rest + d0


def integrate_axis(e, w, wmax, acc, dt_ms):
    """The integrator's use of the controller: (e', w', turned binary). A zero remainder sets the rate to 0 at once
    (0x00459a99), W*k == 0 skips the axis (0x004599aa); otherwise sign-mirrored, delta clamped to |e| (rate 0 then)."""
    if e == 0:
        return 0, 0, 0
    if acc == 0:
        return e, w, 0
    neg = e < 0
    sw, sd = controller(-e if neg else e, -w if neg else w, wmax, acc, dt_ms)
    mag = -e if neg else e
    if sd >= mag:
        sd = mag
        sw = 0
    e = e + sd if neg else e - sd
    w = -sw if neg else sw
    return e, w, (-sd if neg else sd)


def llround(x):
    """C llround: half away from zero."""
    return int(math.copysign(math.floor(abs(x) + 0.5), x))


def wrap16(a):
    """A binary angle wrapped to [-0x8000, 0x7fff] (0x004547bc)."""
    return ((a + 0x8000) & 0xffff) - 0x8000


def rows16(rows):
    return [[llround(rows[r][k] * 65536.0) for k in range(3)] for r in range(3)]


def translate(slip, v_old, b16, speed, order, dt_ms):
    """The integrator's velocity, slip and position update (ship-movement.md section 13.2, Model::advance): p+0x100 =
    fx(row2, speed); not on order 5 and speed != 0: slip += the part of last frame's p+0x100 perpendicular to the new
    row2; displacement = (slip + p+0x100) dt / 1000 truncating; slip *= (2000 - dt) / 2000 toward zero.
    Returns (displacement, the decayed slip, p+0x100)."""
    vel = [fx(b16[2][k], speed) for k in range(3)]
    slip = list(slip)
    if order != 5 and speed != 0:
        l0 = sum(fx(b16[0][j], v_old[j]) for j in range(3))
        l1 = sum(fx(b16[1][j], v_old[j]) for j in range(3))
        slip = [slip[k] + fx(b16[0][k], l0) + fx(b16[1][k], l1) for k in range(3)]
    disp = [tdiv((slip[k] + vel[k]) * dt_ms, 1000) for k in range(3)]
    return disp, [tdiv(slip[k] * (2000 - dt_ms), 2000) for k in range(3)], vel


def gamma_binary(rows):
    """obj+0x20, the third Euler angle of the basis (the builder 0x004f0270: row0.y = sin g cos b, row1.y = cos g cos b)."""
    return math.floor(math.atan2(rows[0][1], rows[1][1]) * 65536.0 / (2 * math.pi))  # floored (Run156: 10,137 exact match steps vs 9,923 rounded)


def launch_units(v0, u):
    """Order 8 (0x004551fe): trunc(u^2 * 1665000 / 1e6) units after u ms, + v0 u / 1000 with the module's P5 stub."""
    u = max(0, u)
    return max(0, v0) * u // 1000 + u * u * 1665000 // 1000000


# The final docking zone's turn cap (0x004542c4 jump table, 0x004542b7 / 0x004542e4): 0x38e binary/s (5 deg/s) for the
# TShips class codes 0, 1, 3, 4, 8, 9, 0xb-0xf, 0x71c (10 deg/s) for the others (M0 2, M3 5, M4 6, M5 7, GO 10).
CLASS_CODES = {n: i for i, n in enumerate(('TL', 'TS', 'M0', 'M1', 'M2', 'M3', 'M4', 'M5', 'TL_P', 'TS_P', 'GO', 'M6', 'TP', 'M7', 'TM', 'M8'))}
SLOW_TURN_CODES = {0, 1, 3, 4, 8, 9, 0xb, 0xc, 0xd, 0xe, 0xf}
MATCH_LIMIT = 0x51e


def final_zone_cap(cls):
    return 0x38e if CLASS_CODES.get(cls, 0) in SLOW_TURN_CODES else 0x71c


def is_final_zone(r):
    """An order-5 row of the final docking zone (0x004543da..0x00454647): no leaf call, speed and desired speed 0, not a gate."""
    return r['order'] == 5 and r['e'] is None and r['tgt_class'] != 0x12 and r['v'] == 0 and r['des'] == 0


def match_condition(r):
    """The port-roll match runs after this row's leaf call (0x0045474a..0x00454795): a port target (not a gate) and all
    three written remainders below 0x51e (strict, signed). The row does not carry the port data pointer."""
    return r['order'] == 5 and r['e'] is not None and r['tgt_class'] != 0x12 and all(-MATCH_LIMIT < x < MATCH_LIMIT for x in r['e'])


def port_rolls(rows):
    """Gamma_port per (target) leg from the aligned match steps: the match ran (match_condition) and the roll rate stayed 0
    (rate(n) = rate(n+1) = 0): a remainder of more than a few binary units would have started the roll, so the ship's own
    gamma is the port's there. The median over the leg's aligned steps, or None."""
    samples = {}
    for n in range(len(rows) - 1):
        a, b = rows[n], rows[n + 1]
        if b['frame'] != a['frame'] + 1 or not match_condition(a) or b['order'] != 5:
            continue
        if a['rate'][2] == 0 and b['rate'][2] == 0:
            samples.setdefault(a['tgt'], []).append(gamma_binary(basis_from_row(a['fwd'], a['up'])))
    out = {}
    for tgt, g in samples.items():
        ref = g[0]
        rel = sorted(wrap16(x - ref) for x in g)
        out[tgt] = wrap16(ref + rel[len(rel) // 2])
    return out


def slew(speed, desired, vmax, accel, dt_ms):
    """Model::advance's speed slew (the max speed clamp; the script speed limit f+0x278 is not in the row)."""
    if desired > vmax:
        desired = vmax
    if speed != desired:
        step = tdiv(accel * dt_ms, 1000)
        if step < 1:
            step = 1
        if speed > desired:
            speed = desired if speed - step < desired else speed - step
        else:
            speed = desired if speed + step > desired else speed + step
    return speed


def _norm(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return [v[0] / n, v[1] / n, v[2] / n]


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def basis_from_row(fwd, up):
    """Rows 0, 1, 2 of the flight basis from the row's (rounded) fwd = row 2 and up = row 1; row 0 = row1 x row2
    (the fixture's right-handed basis)."""
    r2 = _norm(fwd)
    d = _dot(up, r2)
    r1 = _norm([up[i] - d * r2[i] for i in range(3)])
    return [_cross(r1, r2), r1, r2]


def rotate(rows, a):
    """Model::rotate: yaw row2 toward -row0, pitch row2 toward +row1, roll row0 toward row1, re-orthonormalised."""
    r0, r1, r2 = list(rows[0]), list(rows[1]), list(rows[2])
    c, s = math.cos(a[0]), math.sin(a[0])
    for i in range(3):
        n2, n0 = c * r2[i] - s * r0[i], c * r0[i] + s * r2[i]
        r2[i], r0[i] = n2, n0
    c, s = math.cos(a[1]), math.sin(a[1])
    for i in range(3):
        n2, n1 = c * r2[i] + s * r1[i], c * r1[i] - s * r2[i]
        r2[i], r1[i] = n2, n1
    c, s = math.cos(a[2]), math.sin(a[2])
    for i in range(3):
        n0, n1 = c * r0[i] + s * r1[i], c * r1[i] - s * r0[i]
        r0[i], r1[i] = n0, n1
    r2 = _norm(r2)
    d = _dot(r0, r2)
    r0 = _norm([r0[i] - d * r2[i] for i in range(3)])
    d, d1 = _dot(r1, r2), _dot(r1, r0)
    r1 = _norm([r1[i] - d * r2[i] - d1 * r0[i] for i in range(3)])
    return [r0, r1, r2]


def axis_acc(W, rot, rot_pct):
    k = tdiv(rot * rot_pct, 100)  # k = f+0x2b0 x record+0xd0 / 100 in 16.16 (Run156: exact on 76,188 of 76,190 leaf steps)
    return [fx(W[i], k) for i in range(3)]


def step(row, e, dt_ms):
    """One frame of the model from row n's state with remainders e: the predicted (rate, e', v, rows, pos)."""
    acc = axis_acc(row['W'], row['rot'], row['rot_pct'])
    v = slew(row['v'], row['des'], row['vmax'], row['acc'], dt_ms)
    w_new, e_new, turned = [], [], []
    for i in range(3):
        ei, wi, di = integrate_axis(e[i], row['rate'][i], row['W'][i], acc[i], dt_ms)
        e_new.append(ei)
        w_new.append(wi)
        turned.append(float(di) * RAD_PER_BINARY)
    rows = rotate(basis_from_row(row['fwd'], row['up']), turned)
    pos = [row['pos'][i] + rows[2][i] * float(v) * float(dt_ms) / 1000.0 for i in range(3)]
    return w_new, e_new, v, rows, pos


# ---- the replay ----
def iter_rows(path):
    """Stream the ship_trace rows of a file (a trace or a whole x3m.log)."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'ship_trace frame=' not in line:
                continue
            r = st.parse_row(line[line.index('ship_trace frame='):])
            if r is not None:
                yield r


def _angle_deg(a, b):
    a, b = _norm(a), _norm(b)
    c = max(-1.0, min(1.0, _dot(a, b)))
    return math.degrees(math.acos(c))


def classify(res, boundary, tol, final_zone=False):
    if boundary:
        return 'order boundary'
    if final_zone:
        return 'final zone'
    if res['pos_c'] is not None and res['pos_c'] > tol['pos_c']:
        return 'position'
    if max(res['rate']) > tol['rate']:
        return 'rate controller'
    if res['heading'] is not None and res['heading'] > tol['heading']:
        return 'orientation'
    if res['speed'] > tol['speed']:
        return 'speed law'
    return 'position'


def divergent(res, tol):
    return (max(res['rate']) > tol['rate'] or res['speed'] > tol['speed'] or
            any(res[k] is not None and res[k] > tol[k] for k in ('heading', 'pos', 'pos_c')))


def replay_ship(rows, tol=TOL, dt_source='row'):
    """The per-step residuals of one ship's rows (in order). dt_source 'row' = row n's dt_ms, 'next' = row n+1's.

    Carried across the steps of a contiguous run: the remainder (an `e=-` row), the slip vector (from 0 at the run's
    start; the final zone zeroes it) and the order-8 launch (its first row's t_ms, f+0x240 = the rows' `given`, v0 = the
    speed of the row before). Per (target) leg the port's roll from port_rolls(). Unmodelled parts are named per step
    (`unmodelled`): `port roll unknown` (a match step on a leg without an aligned step: the roll residual is not judged),
    `final zone` (the step axis toward the port's orientation and the glide path are not in the row: heading and position
    are not judged; the rates are judged as the per-frame step magnitude cap x dt(n+1) / 1000, or 0), `launch axis` (the
    first order-8 displacement: only its magnitude is judged)."""
    steps, skipped = [], {'gap': 0, 'no_e': 0}
    dt_match = {'row': 0, 'next': 0, 'neither': 0}
    carried = None
    slip = [0, 0, 0]
    gammas = port_rolls(rows)
    launch = None
    run_t0 = rows[0]['t_ms'] if rows else 0
    for n in range(len(rows) - 1):
        a, b = rows[n], rows[n + 1]
        if b['frame'] != a['frame'] + 1:
            skipped['gap'] += 1
            carried, slip, launch = None, [0, 0, 0], None
            run_t0 = b['t_ms']  # the slip restarts from 0 with the run
            continue
        dt_t = b['t_ms'] - a['t_ms']
        dt_match['row' if dt_t == a['dt_ms'] else 'next' if dt_t == b['dt_ms'] else 'neither'] += 1
        dt = a['dt_ms'] if dt_source == 'row' else b['dt_ms']
        slip_res = None
        if a.get('slip') is not None:  # amendment 14: the row's obj+0x60 seeds the carried slip
            if a['t_ms'] != run_t0:
                slip_res = max(abs(slip[k] - a['slip'][k]) for k in range(3)) / UNITS_PER_M
            slip = list(a['slip'])
        basis_a = basis_from_row(a['fwd'], a['up'])
        basis_b = basis_from_row(b['fwd'], b['up'])
        v_old = [fx(x, a['v']) for x in rows16(basis_a)[2]]
        # the conditional translation (row n+1's heading and speed) carries the slip whatever else is known
        disp_c, slip_next, _ = translate(slip, v_old, rows16(basis_b), b['v'], a['order'], dt)
        slip_in = slip
        slip = slip_next
        fz = is_final_zone(b)
        if fz:
            slip = [0, 0, 0]  # 0x004543f3
        if b['order'] == 8 and a['order'] != 8:
            launch = {'t0': b['t_ms'], 'given': b['given'], 'v0': a['v']}
        elif b['order'] != 8:
            launch = None
        e = a['e'] if a['e'] is not None else carried
        if e is None:
            skipped['no_e'] += 1
            continue
        e = list(e)
        unmodelled = None
        if match_condition(a):  # the port-roll match after the leaf (0x004547d6)
            g = gammas.get(a['tgt'])
            if g is None:
                unmodelled = 'port roll unknown'
            else:
                e[2] = wrap16(g - gamma_binary(basis_a))
        acc = axis_acc(a['W'], a['rot'], a['rot_pct'])
        v = slew(a['v'], a['des'], a['vmax'], a['acc'], dt)
        w, e_new, turned = [], [], []
        for i in range(3):
            ei, wi, di = integrate_axis(e[i], a['rate'][i], a['W'][i], acc[i], dt)
            e_new.append(ei)
            w.append(wi)
            turned.append(float(di) * RAD_PER_BINARY)
        carried = tuple(e_new)
        brows = rotate(basis_a, turned)
        disp, _, _ = translate(slip_in, v_old, rows16(brows), v, a['order'], dt)
        pos = [a['pos'][k] + disp[k] for k in range(3)]
        cond = [a['pos'][k] + disp_c[k] for k in range(3)]
        rate_res = [abs(w[i] - b['rate'][i]) for i in range(3)]
        heading = _angle_deg(brows[2], b['fwd'])
        if a['order'] == 8 and b['order'] == 8 and launch is not None:
            # frame n+1's pass A writes pos = f+0x240 + fx(axis, s(u)) (0x0045528e..): the axis (the gate's row 2) is
            # known from row n's displacement once that is not 0; the first one is judged on its magnitude
            v = 0
            g0 = launch['given']
            s_b = launch_units(launch['v0'], b['t_ms'] - launch['t0'])
            s_a = launch_units(launch['v0'], a['t_ms'] - launch['t0'])
            d_a = [a['pos'][k] - g0[k] for k in range(3)]
            if s_a > 0 and any(d_a):
                pos = [g0[k] + d_a[k] * s_b / s_a for k in range(3)]
            else:
                d_b = [b['pos'][k] - g0[k] for k in range(3)]
                nb = math.sqrt(_dot(d_b, d_b)) or 1.0
                pos = [g0[k] + d_b[k] / nb * s_b for k in range(3)]
                unmodelled = 'launch axis'
            cond = pos
        if fz:
            # frame n+1's pass A: the kinematic turn stores the step's Euler angles (0..0xffff) in the rate fields
            mag = math.sqrt(sum(wrap16(x) ** 2 for x in b['rate']))
            m_exp = final_zone_cap(b['class']) * b['dt_ms'] / 1000.0
            # the last step of the turn is the remaining angle (step = min(cap dt / 1000, remaining), 0x00454587): below the cap,
            # with zero rates on the next row of the zone
            c = rows[n + 2] if n + 2 < len(rows) and rows[n + 2]['frame'] == b['frame'] + 1 else None
            last = mag < m_exp and c is not None and is_final_zone(c) and not any(c['rate'])
            r = 0.0 if mag == 0 or abs(mag - m_exp) <= 2.5 or last else abs(mag - m_exp)
            rate_res = [r, 0, 0]
            v = 0
            unmodelled = 'final zone'
        elif unmodelled == 'port roll unknown':
            rate_res[2] = 0
        res = {
            'rate': rate_res,
            'speed': abs(v - b['v']) / UNITS_PER_M,
            'heading': None if fz else heading,
            'pos': None if fz else math.dist(pos, b['pos']) / UNITS_PER_M,
            'pos_c': None if fz else math.dist(cond, b['pos']) / UNITS_PER_M,
        }
        prev_order = rows[n - 1]['order'] if n > 0 and rows[n - 1]['frame'] == a['frame'] - 1 else a['order']
        boundary = a['order'] != b['order'] or prev_order != a['order']
        rec = {'frame': a['frame'], 't_ms': a['t_ms'], 'order': a['order'], 'dt_ms': dt, 'leaf_call': a['e'] is not None,
               'turning': any(a['rate']) or any(b['rate']), 'unmodelled': unmodelled, 'run_ms': a['t_ms'] - run_t0,
               'v_obs': [a['v'], a['des'], b['v']],
               'pred': {'rate': w, 'v': v, 'fwd': [round(x, 6) for x in brows[2]], 'pos': [round(x, 1) for x in pos]}}
        rec.update(res)
        if not fz:  # pos_c along / lateral to fwd(n+1)
            f = _norm(b['fwd'])
            dvec = [b['pos'][k] - cond[k] for k in range(3)]
            along = _dot(dvec, f)
            rec['pos_c_along'] = abs(along) / UNITS_PER_M
            rec['pos_c_lat'] = math.sqrt(max(0.0, _dot(dvec, dvec) - along * along)) / UNITS_PER_M
        else:  # row n+1's Euler step applied to row n's basis (a convention check, not a prediction), and the glide speed
            rec['fz_euler_heading'] = _angle_deg(rotate(basis_a, [wrap16(x) * RAD_PER_BINARY for x in b['rate']])[2], b['fwd'])
            rec['fz_glide_ms'] = math.dist(a['pos'], b['pos']) / UNITS_PER_M / (b['dt_ms'] / 1000.0) if b['dt_ms'] else None
        # the velocity's lag behind the nose (amendment 14): flown (the observed displacement), modelled, and as the law read it
        d_obs = [b['pos'][k] - a['pos'][k] for k in range(3)]
        if not fz and a['order'] != 8 and b['order'] != 8 and _dot(d_obs, d_obs) >= LAG_MIN_UNITS ** 2:
            rec['lag_obs'] = _angle_deg(d_obs, b['fwd'])
            rec['lag_model'] = _angle_deg(disp_c, b['fwd']) if any(disp_c) else None
        if a.get('slip') is not None:
            u_row = [v_old[k] + a['slip'][k] for k in range(3)]
            rec['lag_row'] = _angle_deg(u_row, a['fwd']) if _dot(u_row, u_row) >= 1 else None
        rec['slip_res'] = slip_res
        rec['class'] = classify(res, boundary, tol, fz) if divergent(res, tol) else None
        rec['boundary'] = boundary
        steps.append(rec)
    return steps, skipped, dt_match


def _pct(vals, p):
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo = int(math.floor(k))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def windows(steps):
    out, cur = [], None
    for s in steps:
        if s['class'] is None:
            cur = None
            continue
        if cur is not None and s['frame'] == cur['last'] + 1:
            cur['last'] = s['frame']
            cur['classes'].append(s['class'])
            continue
        cur = {'first': s['frame'], 'last': s['frame'], 'classes': [s['class']]}
        out.append(cur)
    result = []
    for wdw in out:
        other = [c for c in wdw['classes'] if c != 'order boundary']
        if not other:
            cls = 'order boundary'
        else:
            cls = max(CLASSES, key=lambda c: (other.count(c), -CLASSES.index(c)))
        result.append({'first': wdw['first'], 'last': wdw['last'], 'steps': len(wdw['classes']), 'class': cls})
    return result


def summarise(obj, rows, steps, skipped, dt_match, worst_n=5):
    q = {  # (frame, value); heading and position are not judged on final-zone steps (None)
        'rate_yaw': [(s['frame'], s['rate'][0]) for s in steps], 'rate_pitch': [(s['frame'], s['rate'][1]) for s in steps],
        'rate_roll': [(s['frame'], s['rate'][2]) for s in steps], 'speed': [(s['frame'], s['speed']) for s in steps],
        'heading': [(s['frame'], s['heading']) for s in steps if s['heading'] is not None],
        'pos': [(s['frame'], s['pos']) for s in steps if s['pos'] is not None],
        'pos_c': [(s['frame'], s['pos_c']) for s in steps if s['pos_c'] is not None],
    }
    stats = {k: {'p50': _pct([x for _, x in v], 50), 'p95': _pct([x for _, x in v], 95), 'max': max(x for _, x in v) if v else None}
             for k, v in q.items()}
    worst = {}
    for k, v in q.items():
        worst[k] = [[f, x] for f, x in sorted(v, key=lambda fv: -fv[1])[:worst_n] if x > 0]
    wins = windows(steps)
    counts = {c: 0 for c in CLASSES}
    for s in steps:
        if s['class']:
            counts[s['class']] += 1
    wcounts = {c: sum(1 for w in wins if w['class'] == c) for c in CLASSES}
    unmodelled = {}
    for s in steps:
        if s['unmodelled']:
            unmodelled[s['unmodelled']] = unmodelled.get(s['unmodelled'], 0) + 1
    return {'obj': '0x%08x' % obj, 'class': rows[0]['class'], 'rows': len(rows), 'steps': len(steps), 'skipped': skipped,
            'unmodelled': unmodelled, 'lag': lag_summary(steps),
            'dt_vs_t_ms': dt_match, 'divergent_steps': sum(counts.values()), 'step_classes': counts,
            'windows': len(wins), 'window_classes': wcounts, 'window_list': wins[:50], 'residuals': stats, 'worst_frames': worst,
            'orders': sorted({r['order'] for r in rows}), 'tgt_classes': sorted({r['tgt_class'] for r in rows})}


def _stats(vals):
    return {'n': len(vals), 'p50': _pct(vals, 50), 'p95': _pct(vals, 95), 'max': max(vals) if vals else None}


def lag_summary(steps):
    """The velocity-vs-nose lag (deg) of a ship's steps: flown, modelled, as read from the row's slip, the flown lag per order
    (hex), the flown-minus-modelled difference and the carried slip's distance to the row's (m/s)."""
    obs = [s['lag_obs'] for s in steps if s.get('lag_obs') is not None]
    by_order = {}
    for s in steps:
        if s.get('lag_obs') is not None:
            by_order.setdefault('0x%x' % s['order'], []).append(s['lag_obs'])
    diff = [abs(s['lag_obs'] - s['lag_model']) for s in steps if s.get('lag_obs') is not None and s.get('lag_model') is not None]
    return {'obs': _stats(obs), 'model': _stats([s['lag_model'] for s in steps if s.get('lag_model') is not None]),
            'row': _stats([s['lag_row'] for s in steps if s.get('lag_row') is not None]),
            'by_order': {o: _stats(v) for o, v in sorted(by_order.items())}, 'obs_minus_model': _stats(diff),
            'slip_res': _stats([s['slip_res'] for s in steps if s.get('slip_res') is not None])}


# ---- --gate ----
def gate_facts(rows):
    """Transition facts of a ship's gate legs (rows with tgt_class 0x12), per run of equal order."""
    g = [r for r in rows if r['tgt_class'] == 0x12]
    if not g:
        return None
    segs = []
    for r in g:
        if segs and segs[-1]['order'] == r['order'] and r['frame'] == segs[-1]['rows'][-1]['frame'] + 1:
            segs[-1]['rows'].append(r)
        else:
            segs.append({'order': r['order'], 'rows': [r]})

    def heading_err(r):
        d = [r['given'][i] - r['pos'][i] for i in range(3)]
        if math.sqrt(_dot(d, d)) < 1:
            return None
        return _angle_deg(r['fwd'], d)

    out, prev_given = [], None
    for s in segs:
        rs = s['rows']
        g0 = rs[0]['given']
        drift = max(math.dist(r['given'], g0) for r in rs) / UNITS_PER_M
        seg = {
            'order': s['order'], 'frame_first': rs[0]['frame'], 'frame_last': rs[-1]['frame'], 'frames': len(rs),
            'duration_ms': rs[-1]['t_ms'] - rs[0]['t_ms'] + rs[-1]['dt_ms'],
            'given_first': list(g0),
            'given_first_rel_prev_m': ([round((g0[i] - prev_given[i]) / UNITS_PER_M, 2) for i in range(3)] if prev_given else None),
            'given_drift_max_m': round(drift, 3),
            'v_first_ms': rs[0]['v'] / UNITS_PER_M, 'v_last_ms': rs[-1]['v'] / UNITS_PER_M,
            'v_min_ms': min(r['v'] for r in rs) / UNITS_PER_M,
            'dist_first_m': round(math.dist(rs[0]['given'], rs[0]['pos']) / UNITS_PER_M, 1),
            'heading_err_first_deg': heading_err(rs[0]), 'heading_err_last_deg': heading_err(rs[-1]),
        }
        out.append(seg)
        prev_given = g0
    o5 = [r['v'] for r in g if r['order'] == 5]
    return {'segments': out, 'order_sequence': [s['order'] for s in out],
            'min_v_order5_ms': (min(o5) / UNITS_PER_M) if o5 else None}


def run(path, obj=None, tol=TOL, dt_source='row', gate=False):
    groups = {}
    for r in iter_rows(path):
        if obj is not None and r['obj'] != obj:
            continue
        groups.setdefault(r['obj'], []).append(r)
    ships, per_step = [], {}
    for o, rows in groups.items():
        steps, skipped, dt_match = replay_ship(rows, tol, dt_source)
        summ = summarise(o, rows, steps, skipped, dt_match)
        if gate:
            summ['gate'] = gate_facts(rows)
        ships.append(summ)
        per_step['0x%08x' % o] = steps
    return {'source': path, 'dt_source': dt_source, 'tolerance': tol, 'ships': ships}, per_step


def _fmt(x, nd=3):
    return '-' if x is None else ('%.*f' % (nd, x))


def print_report(report, gate=False, out=sys.stdout):
    for s in report['ships']:
        print('ship %s %s rows=%d steps=%d skipped gap=%d no_e=%d dt(t_ms delta == row n %d, row n+1 %d, neither %d) '
              'divergent=%d windows=%d' % (s['obj'], s['class'], s['rows'], s['steps'], s['skipped']['gap'], s['skipped']['no_e'],
                                           s['dt_vs_t_ms']['row'], s['dt_vs_t_ms']['next'], s['dt_vs_t_ms']['neither'],
                                           s['divergent_steps'], s['windows']), file=out)
        for k, v in s['residuals'].items():
            print('  %-10s p50=%s p95=%s max=%s worst=%s' % (k, _fmt(v['p50'], 4), _fmt(v['p95'], 4), _fmt(v['max'], 4),
                                                           ','.join(str(f) for f, _ in s['worst_frames'][k])), file=out)
        print('  steps by class: ' + ' '.join('%s=%d' % (c.replace(' ', '_'), n) for c, n in s['step_classes'].items()), file=out)
        print('  windows by class: ' + ' '.join('%s=%d' % (c.replace(' ', '_'), n) for c, n in s['window_classes'].items()), file=out)
        lag = s.get('lag')
        if lag and lag['obs']['n']:
            print('  lag_deg (velocity vs nose) flown n=%d p50=%s p95=%s max=%s | model p50=%s p95=%s | |flown-model| p95=%s | row n=%d p95=%s '
                  '| slip_res m/s p95=%s max=%s' % (lag['obs']['n'], _fmt(lag['obs']['p50'], 2), _fmt(lag['obs']['p95'], 2), _fmt(lag['obs']['max'], 2),
                                                    _fmt(lag['model']['p50'], 2), _fmt(lag['model']['p95'], 2),
                                                    _fmt(lag['obs_minus_model']['p95'], 3), lag['row']['n'], _fmt(lag['row']['p95'], 2),
                                                    _fmt(lag['slip_res']['p95'], 3), _fmt(lag['slip_res']['max'], 3)), file=out)
            print('  lag_deg by order: ' + ' '.join('%s n=%d p50=%s p95=%s' % (o, v['n'], _fmt(v['p50'], 1), _fmt(v['p95'], 1))
                                                    for o, v in lag['by_order'].items()), file=out)
        if s['unmodelled']:
            print('  unmodelled steps: ' + ' '.join('%s=%d' % (c.replace(' ', '_'), n) for c, n in sorted(s['unmodelled'].items())), file=out)
        if gate and s.get('gate'):
            gf = s['gate']
            print('  gate orders=%s min_v_order5=%s m/s' % ('/'.join(str(o) for o in gf['order_sequence']), _fmt(gf['min_v_order5_ms'], 1)),
                  file=out)
            for seg in gf['segments']:
                print('   order=%d frames=%d dur=%dms given_rel_prev=%s drift=%sm v first/last/min=%s/%s/%s dist_first=%sm '
                      'head_err first/last=%s/%s deg' % (seg['order'], seg['frames'], seg['duration_ms'], seg['given_first_rel_prev_m'],
                                                         _fmt(seg['given_drift_max_m'], 1), _fmt(seg['v_first_ms'], 1),
                                                         _fmt(seg['v_last_ms'], 1), _fmt(seg['v_min_ms'], 1), _fmt(seg['dist_first_m'], 1),
                                                         _fmt(seg['heading_err_first_deg'], 1), _fmt(seg['heading_err_last_deg'], 1)),
                      file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n\n', 1)[1])
    ap.add_argument('trace', help='a file of ship_trace rows or a whole x3m.log (streamed)')
    ap.add_argument('--json', help='write the summaries and the per-step residuals here')
    ap.add_argument('--obj', help='only this ship (0x...)')
    ap.add_argument('--gate', action='store_true', help='also print the gate transition facts of ships with a class-0x12 target')
    ap.add_argument('--dt-source', choices=('row', 'next'), default='row', help="row n's dt_ms (default) or row n+1's")
    for k, v in TOL.items():
        ap.add_argument('--tol-' + k.replace('_', '-'), type=float, default=v, help='tolerance (default %g)' % v)
    a = ap.parse_args(argv)
    tol = {k: getattr(a, 'tol_' + k) for k in TOL}
    report, per_step = run(a.trace, int(a.obj, 16) if a.obj else None, tol, a.dt_source, a.gate)
    print_report(report, a.gate)
    if a.json:
        with open(a.json, 'w') as f:
            json.dump({'summary': report, 'steps': per_step}, f)
    return 0


if __name__ == '__main__':
    sys.exit(main())
