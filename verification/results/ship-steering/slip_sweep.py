"""Slip physics sweep for docs/architecture/slip-aware-steering.md (question 1).

A Python port of the host fixture's calibrated integrator (verification/probe/ship_steering_host.cpp, Model::advance;
ship-movement.md section 13.2, the fixed point of 0x0045a549..0x0045a778): p+0x100 = fx(row2, speed); for a ship not
on order 5 with speed != 0 the part of last frame's p+0x100 perpendicular to the new row 2 is added to the slip;
position += (slip + p+0x100) dt / 1000 (truncating); slip *= (2000 - dt) / 2000 (toward zero). The nose is rotated
either at a constant rate (the steady-state sweep) or by the engine's per-axis controller 0x0043ad20 (the turn-in
sweep), both ported from the fixture. The five fixture ships (kShips). 60 fps clock with the fractional carry.

Usage: python3 -I slip_sweep.py > slip_sweep.txt
"""
import math

TAU = 2.0  # s, the slip decay 2000 ms

SHIPS = [  # cls, name, v m/s, a m/s^2, yaw deg/s, r m, k %
    ("M5", "Discoverer", 532.0, 55.0, 147.66, 9.2, 50),
    ("M3", "Nova", 247.5, 40.0, 92.81, 11.9, 32),
    ("M6", "Centaur", 180.0, 20.0, 33.75, 64.4, 10),
    ("TS", "Mercury", 124.0, 30.0, 21.09, 38.5, 20),
    ("M2", "Boreas", 70.0, 15.0, 13.5, 705.4, 5),
]


def fx(a, b):
    return (a * b + 0x8000) >> 16


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def div16(a, b):
    if b == 0:
        return 0
    q = (abs(a) << 16) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def controller(e, w, wmax, acc, dt_ms):
    """0x0043ad20 as in the fixture: (new rate, angle turned this frame), e >= 0."""
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
    rest = max(e - d0 - dstop, 0)
    tr = div16(rest, nw) if nw else 0
    if t <= tr + ta:
        return nw, fx(nw, t - ta) + d0
    tb = t - tr - ta
    return nw - fx(tb, acc), fx(nw, tb) + rest + d0


class Clock:
    def __init__(self, fps):
        self.fps, self.acc, self.last = fps, 0.0, 0

    def step(self):
        self.acc += 1000.0 / self.fps
        now = int(self.acc)
        dt, self.last = now - self.last, now
        return dt


class Ship:
    """2-D (x, z): row0 = x (the ship's row 0), row2 = z (forward); yaw turns row2 toward -row0."""

    def __init__(self, v_ms, slip_on=True):
        self.speed = round(v_ms * 500)
        self.r0 = [1.0, 0.0]
        self.r2 = [0.0, 1.0]
        self.pos = [0, 0]
        self.vel = [fx(0, self.speed), fx(65536, self.speed)]
        self.slip = [0, 0]
        self.slip_on = slip_on
        self.t_ms = 0

    def rotate(self, a):
        c, s = math.cos(a), math.sin(a)
        r0, r2 = self.r0, self.r2
        n2 = [c * r2[i] - s * r0[i] for i in range(2)]
        n0 = [c * r0[i] + s * r2[i] for i in range(2)]
        n = math.hypot(*n2)
        self.r2 = [v / n for v in n2]
        d = n0[0] * self.r2[0] + n0[1] * self.r2[1]
        n0 = [n0[i] - d * self.r2[i] for i in range(2)]
        n = math.hypot(*n0)
        self.r0 = [v / n for v in n0]

    def advance(self, dt, turned_rad):
        self.rotate(turned_rad)
        r16 = [[round(self.r0[k] * 65536) for k in range(2)], [round(self.r2[k] * 65536) for k in range(2)]]
        v_old = list(self.vel)
        self.vel = [fx(r16[1][k], self.speed) for k in range(2)]
        if self.slip_on and self.speed != 0:
            l0 = sum(fx(r16[0][j], v_old[j]) for j in range(2))
            for k in range(2):
                self.slip[k] += fx(r16[0][k], l0)
        for k in range(2):
            self.pos[k] += tdiv((self.slip[k] + self.vel[k]) * dt, 1000)
            self.slip[k] = tdiv(self.slip[k] * (2000 - dt), 2000)
        self.t_ms += dt

    def u(self):
        return [self.slip[k] + self.vel[k] for k in range(2)]

    def lag_deg(self):
        u = self.u()
        n = math.hypot(*u)
        if n == 0:
            return 0.0
        c = max(-1.0, min(1.0, (u[0] * self.r2[0] + u[1] * self.r2[1]) / n))
        return math.degrees(math.acos(c))


def clearance(r_own, r_obs):
    return r_own + r_obs + max(25.0, 0.5 * r_own)


def steady_turn(ship, fps=60, seconds=12.0):
    """Constant nose rate at the yaw cap; the last second's mean of the steady-state figures."""
    cls, name, v, a, yaw_dps, r, k = ship
    omega = math.radians(yaw_dps)
    s = Ship(v)
    clock = Clock(fps)
    samples = []
    while s.t_ms < seconds * 1000:
        dt = clock.step()
        if not dt:
            continue
        s.advance(dt, omega * dt / 1000.0)
        if s.t_ms > (seconds - 1.0) * 1000:
            u = s.u()
            samples.append((math.hypot(*u) / s.speed, math.hypot(*s.slip) / s.speed, s.lag_deg()))
    g = sum(x[0] for x in samples) / len(samples)
    sl = sum(x[1] for x in samples) / len(samples)
    lag = sum(x[2] for x in samples) / len(samples)
    wt = omega * TAU
    return dict(g=g, slip=sl, lag=lag, R=v / omega, R_eff=g * v / omega, wt=wt,
                g_pred=1 / math.sqrt(1 + wt * wt), slip_pred=wt / math.sqrt(1 + wt * wt), lag_pred=math.degrees(math.atan(wt)))


def turn_in(ship, angle_deg, C_list, slip_on, fps=60):
    """One command of angle_deg on the yaw axis through the controller (W, acc = W k), from straight flight at full speed.
    Returns the forward distance (m, along the original heading) at which the lateral offset first reaches each C, the
    peak velocity-nose lag, the time the nose finishes, and the drift along the original heading after the nose finished
    (the ship keeps moving along the old heading while the slip decays)."""
    cls, name, v, a, yaw_dps, r, k = ship
    wmax = round(yaw_dps / 360.0 * 65536)
    acc = fx(wmax, (0x10000 * k) // 100)
    e = round(angle_deg / 360.0 * 65536)
    w = 0
    s = Ship(v, slip_on)
    clock = Clock(fps)
    d_turn = {C: None for C in C_list}
    peak_lag = 0.0
    t_nose = None
    z_at_nose = None
    z_max = 0.0
    while s.t_ms < 60000:
        dt = clock.step()
        if not dt:
            continue
        if e > 0:
            nw, d = controller(e, w, wmax, acc, dt)
            if d >= e:
                d, nw = e, 0
            e -= d
            w = nw
            turned = d * 2 * math.pi / 65536
        else:
            turned = 0.0
        s.advance(dt, turned)
        x, z = -s.pos[0] / 500.0, s.pos[1] / 500.0  # lateral toward the turn (-row0), forward along the original heading
        lat = abs(x)
        for C in C_list:
            if d_turn[C] is None and lat >= C:
                d_turn[C] = z
        peak_lag = max(peak_lag, s.lag_deg())
        if e <= 91 and t_nose is None:  # the fixture's "axis done": under 0.5 deg (the controller can idle at e = 1 binary)
            t_nose, z_at_nose = s.t_ms, z
        z_max = max(z_max, z)
        if t_nose is not None and s.t_ms > t_nose + 8000:
            break
    return dict(d_turn=d_turn, peak_lag=peak_lag, t_nose=t_nose, drift_after=z_max - z_at_nose if z_at_nose is not None else None,
                pos=[s.pos[0] / 500.0, s.pos[1] / 500.0], fwd=list(s.r2), r0=list(s.r0))


def offset(nose_run, slip_run):
    """The flown path's offset from the nose path at the end of both runs (same clock, same nose history), in the final
    heading's frame: perpendicular (positive = outward, toward -(-row0) = row0 side... sign: positive = away from the turn
    centre) and along the heading (negative = behind)."""
    d = [slip_run["pos"][i] - nose_run["pos"][i] for i in range(2)]
    f, r0 = nose_run["fwd"], nose_run["r0"]
    along = d[0] * f[0] + d[1] * f[1]
    perp = d[0] * r0[0] + d[1] * r0[1]  # the turn is toward -row0, so +row0 is outward
    return perp, along


def main():
    print("slip_sweep.py: the fixture's calibrated integrator (fixed point), 60 fps, yaw only; measured on this port")
    print()
    print("1. Steady constant-rate turn at the yaw cap (after 11 s): ground speed |u|/v, |slip|/v, velocity-nose lag,")
    print("   R = v/omega (the nose circle) and R_eff = |u|/omega (the circle the ship flies); predicted = the first-order")
    print("   lag u' = (v f - u)/tau with tau = 2 s: |u|/v = 1/sqrt(1+(w tau)^2), lag = atan(w tau)")
    print("cls   v m/s omega deg/s  w*tau  |u|/v  (pred)  |slip|/v (pred)  lag deg (pred)   R m   R_eff m")
    for ship in SHIPS:
        r = steady_turn(ship)
        print("%-3s %7.1f %10.2f %6.2f %6.3f (%.3f) %7.3f (%.3f) %8.1f (%5.1f) %7.0f %8.0f" % (
            ship[0], ship[2], ship[4], r["wt"], r["g"], r["g_pred"], r["slip"], r["slip_pred"], r["lag"], r["lag_pred"], r["R"], r["R_eff"]))
    print()
    print("2. Turn-in: one 90 deg yaw command through the controller 0x0043ad20 (cap W, accel W k) from straight full-speed")
    print("   flight. d_turn(C) = forward distance (m, along the original heading) at which the lateral offset reaches C,")
    print("   without the slip (the nose path) and with it; C_ship = clearance(r, r) (the crossing), C_sta = clearance(r, 200)")
    print("   (the flyby part); the design's sqrt(C(C+2R)) for comparison; peak lag; forward drift after the nose finished.")
    print("cls  C_ship  d_turn nose / slip   sqrtC(C+2R)  | C_sta  d_turn nose / slip  sqrtC(C+2R) | peak lag  t_nose s  drift_after m")
    for ship in SHIPS:
        cls, name, v, a, yaw_dps, r, k = ship
        R = v / math.radians(yaw_dps)
        Cs, Ct = clearance(r, r), clearance(r, 200.0)
        n = turn_in(ship, 90, [Cs, Ct], False)
        w = turn_in(ship, 90, [Cs, Ct], True)
        f = lambda x: "%7.0f" % x if x is not None else "   none"
        print("%-3s %6.1f %s / %s %11.0f | %6.1f %s / %s %10.0f | %6.1f  %7.2f  %8.0f" % (
            cls, Cs, f(n["d_turn"][Cs]), f(w["d_turn"][Cs]), math.sqrt(Cs * (Cs + 2 * R)), Ct, f(n["d_turn"][Ct]), f(w["d_turn"][Ct]),
            math.sqrt(Ct * (Ct + 2 * R)), w["peak_lag"], w["t_nose"] / 1000.0, w["drift_after"]))
    print()
    print("3. Small turns (10 / 30 / 90 deg commands): the asymptotic offset of the flown path from the nose path, 8 s after")
    print("   the nose finished, perpendicular to the final heading (outward, +) and along it (behind, -); peak lag; the")
    print("   first-order prediction for a turn much faster than tau is tau v sin(beta) outward and tau v (cos(beta) - 1) behind.")
    print("cls   10: peak lag  out m  behind m (pred out) | 30: peak lag  out m  behind m (pred) | 90: peak lag  out m  behind m (pred)")
    for ship in SHIPS:
        row = "%-3s" % ship[0]
        for beta in (10, 30, 90):
            n = turn_in(ship, beta, [], False)
            w = turn_in(ship, beta, [], True)
            out, behind = offset(n, w)
            row += " %12.1f %6.0f %8.0f (%5.0f) |" % (w["peak_lag"], out, behind, TAU * ship[2] * math.sin(math.radians(beta)))
        print(row)
    print()
    print("3b. Turn-in distance for the detour offsets the avoidance uses (1.75 C_ship for ships, 1.3 C_sta for a part):")
    print("    the forward distance at which the flown path reaches that lateral offset, nose path / with slip, and the")
    print("    implemented push-ahead 1 s v and lookahead L = 1 s v + 2 v / k + 1.25 min(sqrt(C(C+2R)), v^2/2a).")
    print("cls  1.75 C_ship  d nose / slip | 1.3 C_sta  d nose / slip | 1 s v   L_ship  L_sta")
    for ship in SHIPS:
        cls, name, v, a, yaw_dps, r, k = ship
        R = v / math.radians(yaw_dps)
        Cs, Ct = 1.75 * clearance(r, r), 1.3 * clearance(r, 200.0)
        n = turn_in(ship, 90, [Cs, Ct], False)
        w = turn_in(ship, 90, [Cs, Ct], True)
        f = lambda x: "%6.0f" % x if x is not None else "  none"
        L = lambda C: v + 2 * v / (k / 100.0) + 1.25 * min(math.sqrt(C * (C + 2 * R)), v * v / (2 * a))
        print("%-3s %11.1f %s / %s | %9.1f %s / %s | %5.0f %7.0f %6.0f" % (
            cls, Cs, f(n["d_turn"][Cs]), f(w["d_turn"][Cs]), Ct, f(n["d_turn"][Ct]), f(w["d_turn"][Ct]), v, L(clearance(r, r)), L(clearance(r, 200.0))))
    print()
    print("4. The formation circle of the fixture (leader at 0.9 v on R_c = max(5 km, 40 r)): the steady lag of a follower")
    print("   holding the circle, and the nose speed it needs for a ground speed of 0.9 v (v / cos lag).")
    print("cls   R_c m  omega_c deg/s  lag deg  nose speed m/s (cap)")
    for ship in SHIPS:
        cls, name, v, a, yaw_dps, r, k = ship
        Rc = max(5000.0, 40 * r)
        om = 0.9 * v / Rc
        lag = math.atan(om * TAU)
        print("%-3s %7.0f %12.2f %8.1f %10.1f (%.0f)" % (cls, Rc, math.degrees(om), math.degrees(lag), 0.9 * v / math.cos(lag), v))


if __name__ == "__main__":
    main()
