"""Prototype of slip-aware steering variants on the fixture's calibrated integrator (slip_sweep.py port), for
docs/architecture/slip-aware-steering.md (questions 2 and 5). Not production code: a 2-D (yaw only) replica of the
host fixture's escort scenario (formation_scenario: the leader at 0.9 v on a 5 km circle, the slot 4 r behind, the
engine's catch-up / close-in branches, site B lowering the speed) and of a carrot line-follow (the gate lead-in's
0.75 s carrot), with the law's rate servo through the controller 0x0043ad20 and the think tick.

Variants:
  L0  the current law: the nose aims at P; the schedule uses the nose speed and the distance to P.
  L1  aim-point feed-forward: the nose aims at P - slip tau (1 - exp(-t_go / tau)) (where a fixed nose and the decaying
      slip land on P); the schedule is written for the closing speed along the line of sight: the nose speed command
      is (v_sched - slip . t) / max(f . t, 0.5); v_ref is the leader's velocity along the line of sight.
  L2:G  lead compensation: the nose aims along u + G (v t - u) (G = 1 is L0; G = tau / tau_c), the aim angle to t
      clamped to 60 deg; the schedule as L1.
  L4:Td lateral-velocity lead: the nose aims at P - Td u_perp, u_perp = the velocity's component perpendicular to the
      line of sight (Td = tau = 2 s is the slip's own memory); the schedule as L1.
  L5:G  velocity matching (formation only): u_des = slot velocity + (slot - x) / T_f, nose velocity N = u + G (u_des - u),
      the nose along N and the nose speed |N| written at site B (lower only).
Usage: PYTHONPATH=. python3 slip_law_proto.py > slip_law_proto.txt
"""
import math
import slip_sweep as S

TAU = 2.0
KTAU = 0.15


class Lcg:
    def __init__(self, seed):
        self.s = seed & 0xffffffff

    def next(self):
        self.s = (self.s * 214013 + 2531011) & 0xffffffff
        return (self.s >> 16) & 0x7fff


def yaw_of(t, r0, r2):
    """The engine's yaw remainder for a unit target direction t in the ship's rows: atan2(-t0, t2)."""
    t0 = t[0] * r0[0] + t[1] * r0[1]
    t2 = t[0] * r2[0] + t[1] * r2[1]
    return math.atan2(-t0, t2)


class Flyer:
    def __init__(self, ship, seed, variant):
        self.cls, self.name, self.v, self.a, self.yaw_dps, self.r, self.k = ship
        self.W = math.radians(self.yaw_dps)
        self.wmax = round(self.yaw_dps / 360.0 * 65536)
        self.acc = S.fx(self.wmax, (0x10000 * self.k) // 100)
        self.a_ang = self.W * self.k / 100.0
        self.s = S.Ship(self.v)
        self.w = 0
        self.e = 0
        self.desired = self.s.speed
        self.rng = Lcg(seed)
        self.next_think = 0
        self.variant = variant
        self.sched = None  # (P, v_ref, d_arr, ramp)
        self.v_nose_cmd = None
        self.Tf = 4.0
        self.last_aim = None

    # ---- the law (one call) ----
    def law(self, P, v_ref_ms, d_arr, ramp, tick_s, lf=None, v_slot=None):
        s = self.s
        self.v_nose_cmd = None
        x = [s.pos[0] / 500.0, s.pos[1] / 500.0]
        slip = [s.slip[0] / 500.0, s.slip[1] / 500.0]
        f = s.r2
        vn = s.speed / 500.0
        d_vec = [P[0] - x[0], P[1] - x[1]]
        d = math.hypot(*d_vec)
        aim = list(P)
        kind, _, par = self.variant.partition(":")
        par = float(par) if par else None
        if kind != "L0" and d > 0:
            t_go = min(d / max(vn, 1.0), 10.0)
            t_hat = [d_vec[0] / d, d_vec[1] / d]
            if kind == "L1":
                k_s = (par if par is not None else 1.0) * TAU * (1 - math.exp(-t_go / TAU))
                aim = [P[0] - slip[0] * k_s, P[1] - slip[1] * k_s]
            elif kind == "L4":
                u = [vn * f[0] + slip[0], vn * f[1] + slip[1]]
                ut = u[0] * t_hat[0] + u[1] * t_hat[1]
                Td = par if par is not None else 1.0
                aim = [P[0] - (u[0] - ut * t_hat[0]) * Td, P[1] - (u[1] - ut * t_hat[1]) * Td]
            elif kind == "L5" and v_slot is not None:
                u = [vn * f[0] + slip[0], vn * f[1] + slip[1]]
                G, Tf = (par if par is not None else 2.0), self.Tf
                u_des = [v_slot[i] + d_vec[i] / Tf for i in range(2)]
                m = math.hypot(*u_des)
                if m > self.v:
                    u_des = [c * self.v / m for c in u_des]
                N = [u[i] + G * (u_des[i] - u[i]) for i in range(2)]
                n = math.hypot(*N)
                if n > 1.0:
                    aim = [x[0] + N[0] / n * max(d, 50.0), x[1] + N[1] / n * max(d, 50.0)]
                    self.v_nose_cmd = min(max(n, 10.0), self.v)
            elif kind == "L2":
                u = [vn * f[0] + slip[0], vn * f[1] + slip[1]]
                g = par if par is not None else 3.0
                dirv = [u[i] + g * (vn * t_hat[i] - u[i]) for i in range(2)]
                n = math.hypot(*dirv)
                if n > 0:
                    dirv = [c / n for c in dirv]
                    ang = math.atan2(dirv[0] * t_hat[1] - dirv[1] * t_hat[0], dirv[0] * t_hat[0] + dirv[1] * t_hat[1])
                    lim = math.radians(60)
                    if abs(ang) > lim:
                        ang = lim if ang > 0 else -lim
                        c, sn = math.cos(-ang), math.sin(-ang)
                        dirv = [c * t_hat[0] - sn * t_hat[1], sn * t_hat[0] + c * t_hat[1]]
                    aim = [x[0] + dirv[0] * d, x[1] + dirv[1] * d]
        self.last_aim = aim
        a_vec = [aim[0] - x[0], aim[1] - x[1]]
        da = math.hypot(*a_vec)
        hold = d <= max(d_arr if ramp else 0.0, 1.0) and self.v_nose_cmd is None
        if hold or da < 1.0:
            self.e = 0
            v_corner = 0.0
            theta = 0.0
        else:
            t_aim = [a_vec[0] / da, a_vec[1] / da]
            yaw = yaw_of(t_aim, s.r0, s.r2)
            theta = abs(yaw)
            omega = min(self.W, math.sqrt(self.a_ang * theta), theta / KTAU)
            e = min(theta, omega * omega / self.a_ang + omega * tick_s) if omega > 0 else 0.0
            self.e = round((e if yaw >= 0 else -e) * 65536 / (2 * math.pi))
            sin_t = abs(math.sin(yaw))
            v_corner = self.W * da / (2 * sin_t) if sin_t > 0.05 else 0.0
        self.sched = (list(P), v_ref_ms, d_arr, ramp, v_corner)

    # ---- site B: the clamp from the stored decision at the current position ----
    def clamp(self):
        if not self.sched:
            return
        P, v_ref, d_arr, ramp, v_corner = self.sched
        s = self.s
        if self.v_nose_cmd is not None:
            units = round(self.v_nose_cmd * 500)
            if units < self.desired:
                self.desired = units
            return
        x = [s.pos[0] / 500.0, s.pos[1] / 500.0]
        d_vec = [P[0] - x[0], P[1] - x[1]]
        d = math.hypot(*d_vec)
        v = None
        if ramp:
            v = v_ref + (math.sqrt(self.a * (d - d_arr)) if d > d_arr else 0.0)
        if v_corner > 0 and (v is None or v_corner < v):
            v = v_corner
        if v is None:
            return
        if self.variant != "L0" and d > 0:
            t_hat = [d_vec[0] / d, d_vec[1] / d]
            slip = [s.slip[0] / 500.0, s.slip[1] / 500.0]
            ft = max(s.r2[0] * t_hat[0] + s.r2[1] * t_hat[1], 0.5)
            v = (v - (slip[0] * t_hat[0] + slip[1] * t_hat[1])) / ft
            v = min(v, self.v)
        v = max(v, 10.0)
        units = round(v * 500)
        if units < self.desired:
            self.desired = units

    def advance(self, dt):
        s = self.s
        vmax = round(self.v * 500)
        if self.desired > vmax:
            self.desired = vmax
        if s.speed != self.desired:
            step = max(round(self.a * 500) * dt // 1000, 1)
            if s.speed > self.desired:
                s.speed = self.desired if s.speed - step < self.desired else s.speed - step
            else:
                s.speed = self.desired if s.speed + step > self.desired else s.speed + step
        turned = 0.0
        if self.e == 0:
            self.w = 0
        else:
            neg = self.e < 0
            nw, d = S.controller(-self.e if neg else self.e, -self.w if neg else self.w, self.wmax, self.acc, dt)
            mag = -self.e if neg else self.e
            if d >= mag:
                d, nw = mag, 0
            self.e += d if neg else -d
            self.w = -nw if neg else nw
            turned = (-d if neg else d) * 2 * math.pi / 65536
        s.advance(dt, turned)


def formation(ship, variant, seed=1, fps=60, total_s=180.0, trace=False, Tf=4.0):
    """The fixture's formation_scenario in 2-D."""
    cls, name, v, a, yaw_dps, r, k = ship
    fl = Flyer(ship, seed, variant)
    fl.Tf = Tf
    Rc = max(5000.0, 40 * r)
    v_lead = 0.9 * v
    om = v_lead / Rc
    slot_back = 4 * r
    prec = 0.25 * r

    def leader_at(ts):
        ph = om * ts
        return [Rc * math.cos(ph), Rc * math.sin(ph)], [-math.sin(ph), math.cos(ph)]

    lp, lfw = leader_at(0)
    start = [lp[i] - lfw[i] * (slot_back + max(500.0, 3 * r)) for i in range(2)]
    fl.s.pos = [round(start[0] * 500), round(start[1] * 500)]
    fl.s.r2 = list(lfw)
    fl.s.r0 = [lfw[1], -lfw[0]]  # perpendicular; the sign only fixes which way is "yaw +"
    fl.s.vel = [S.fx(round(fl.s.r2[k2] * 65536), fl.s.speed) for k2 in range(2)]
    clock = S.Clock(fps)
    sum_sq, n_last = 0.0, 0
    entered, win_start, win_max, prev_max, increases, max_after = False, 0, 0.0, 1e18, 0, 0.0
    last_call = None
    branch = "-"
    frames = 0
    while fl.s.t_ms < total_s * 1000:
        frames += 1
        dt = clock.step()
        if not dt:
            continue
        ts = fl.s.t_ms / 1000.0
        lp, lfw = leader_at(ts)
        slot = [lp[i] - lfw[i] * slot_back for i in range(2)]
        x = [fl.s.pos[0] / 500.0, fl.s.pos[1] / 500.0]
        err = math.hypot(slot[0] - x[0], slot[1] - x[1])
        if fl.s.t_ms >= fl.next_think:
            fl.next_think = fl.s.t_ms + fl.rng.next() * 50 // 32768
            along = lfw[0] * fl.s.r2[0] + lfw[1] * fl.s.r2[1]
            d_vec = [slot[0] - x[0], slot[1] - x[1]]
            if variant == "L0" or err < 1e-6:
                v_ref = v_lead * along if along > 0 else 0.0
            else:
                t_hat = [d_vec[0] / err, d_vec[1] / err]
                v_ref = max(v_lead * (lfw[0] * t_hat[0] + lfw[1] * t_hat[1]), 0.0)
            s_behind = d_vec[0] * lfw[0] + d_vec[1] * lfw[1]
            if err > 10 * prec:
                P = slot
                fl.desired = round(v * 500)
                branch = "catch"
            elif along > 0.9:
                branch = "close"
                P = [slot[i] + lfw[i] * 20 * r for i in range(2)]
                fr = max(-1.0, min(1.0, s_behind / (10 * prec)))
                vl = v_lead * 500
                des = vl + fr * (v * 500 - vl) if fr >= 0 else vl + fr * (vl - 5000.0)
                fl.desired = max(round(des), 5000)
            else:
                P = slot
                fl.desired = round(v_lead * 500)
                branch = "slot"
            gap = (fl.s.t_ms - last_call) if last_call is not None else 50
            last_call = fl.s.t_ms
            v_slot = [v_lead * lfw[0], v_lead * lfw[1]]
            if variant.startswith("L5"):
                P = slot  # the velocity law closes on the slot itself whatever the engine's branch aims at
            fl.law(P, v_ref, r, True, min(max(gap, dt), 50) / 1000.0, lfw, v_slot)
        fl.clamp()
        fl.advance(dt)
        if trace and fl.s.t_ms % 1000 < dt:
            u = fl.s.u()
            print("ftrace %s %s t=%5.1f err=%7.1f d_P=%7.1f branch=%s speed=%5.1f des=%5.1f slip=%5.1f lag=%5.1f e=%6d" % (
                ship[0], variant, ts, err, math.hypot(fl.sched[0][0] - x[0], fl.sched[0][1] - x[1]), branch, fl.s.speed / 500.0,
                fl.desired / 500.0, math.hypot(*fl.s.slip) / 500.0, fl.s.lag_deg(), fl.e))
        if not entered and err <= 4 * r:
            entered, win_start = True, fl.s.t_ms
        if entered:
            max_after = max(max_after, err)
            win_max = max(win_max, err)
            if fl.s.t_ms - win_start >= 2000:
                if win_max > prev_max * 1.10 + 0.5 * r:
                    increases += 1
                prev_max, win_max, win_start = win_max, 0.0, fl.s.t_ms
        if ts >= total_s - 20.0:
            sum_sq += err * err
            n_last += 1
    rms = math.sqrt(sum_sq / n_last) if n_last else 1e9
    return dict(rms=rms, entered=entered, increases=increases, max_after=max_after, frames=frames)


def carrot_line(ship, variant, seed=1, fps=60, offset_m=1000.0, look_s=0.75, min_r=331.5, run_m=3315.0):
    """A carrot line-follow (the gate lead-in's shape): the line is z (x = 0); the ship starts offset_m off it, heading
    along it, at full speed; the carrot sits look_s of flight (at least min_r) ahead of the ship's projection on the line.
    Measured when the ship has flown run_m along the line (3 r_gate): lateral offset, the nose's and the velocity's angle
    to the line; the peak overshoot across the line."""
    cls, name, v, a, yaw_dps, r, k = ship
    fl = Flyer(ship, seed, variant)
    fl.s.pos = [round(offset_m * 500), 0]
    clock = S.Clock(fps)
    peak_cross = 0.0
    last_call = None
    while fl.s.t_ms < 400000:
        dt = clock.step()
        if not dt:
            continue
        x = [fl.s.pos[0] / 500.0, fl.s.pos[1] / 500.0]
        if x[1] >= run_m:
            u = fl.s.u()
            nose = math.degrees(math.atan2(fl.s.r2[0], fl.s.r2[1]))
            vel = math.degrees(math.atan2(u[0], u[1]))
            return dict(lateral=x[0], nose_deg=nose, vel_deg=vel, peak_cross=peak_cross, t_s=fl.s.t_ms / 1000.0)
        if x[0] < 0:
            peak_cross = max(peak_cross, -x[0])
        ahead = max(look_s * v, min_r)
        P = [0.0, x[1] + ahead]
        gap = (fl.s.t_ms - last_call) if last_call is not None else dt
        last_call = fl.s.t_ms
        fl.law(P, 0.0, 0.0, False, min(max(gap, dt), 50) / 1000.0)
        fl.desired = round(v * 500)
        fl.clamp()
        fl.advance(dt)
    return dict(lateral=float("nan"), nose_deg=0, vel_deg=0, peak_cross=peak_cross, t_s=0)


VARIANTS = ("L0", "L1:1", "L1:0.5", "L4:0.5", "L4:1", "L4:2", "L2:0.5", "L2:1", "L2:2")


def main():
    print("slip_law_proto.py: 2-D replica on the calibrated integrator (slip_sweep.py); measured on this port, not the fixture")
    print()
    print("A. Formation (the fixture's scenario 3 shape): slot RMS over the last 20 s (2 r), 2 s window increases, max error after entry")
    print("   (rms / incr / max per variant)")
    print("cls   2r m | " + " | ".join("%-16s" % v for v in VARIANTS))
    for ship in S.SHIPS:
        row = "%-3s %6.1f |" % (ship[0], 2 * ship[5])
        for var in VARIANTS:
            f = formation(ship, var)
            row += " %6.1f/%2d/%5.0f |" % (f["rms"], f["increases"], f["max_after"])
        print(row)
    print()
    print("A2. Formation with the velocity-matching law L5:G (u_des = slot velocity + error / T_f, nose velocity N = u + G (u_des - u),")
    print("    the nose along N at speed |N|): rms / incr / max")
    combos = [("L5:1", 2.0), ("L5:1", 4.0), ("L5:2", 2.0), ("L5:2", 4.0), ("L5:2", 8.0), ("L5:3", 4.0)]
    print("cls   2r m | " + " | ".join("%-16s" % ("%s Tf=%.0f" % c) for c in combos))
    for ship in S.SHIPS:
        row = "%-3s %6.1f |" % (ship[0], 2 * ship[5])
        for var, Tf in combos:
            f = formation(ship, var, Tf=Tf)
            row += " %6.1f/%2d/%5.0f |" % (f["rms"], f["increases"], f["max_after"])
        print(row)
    print()
    print("B2. Carrot line-follow with a longer carrot: look_s 2 s and 4 s (floor 0.3 r_gate), variants L0, L4:2, L2:2")
    for look in (2.0, 4.0):
        print("look_s = %.0f s: cls | L0 lat nose vel cross | L4:2 ... | L2:2 ..." % look)
        for ship in S.SHIPS:
            row = "%-3s  |" % ship[0]
            for var in ("L0", "L4:2", "L2:2"):
                c = carrot_line(ship, var, look_s=look)
                row += " %5.0f %5.1f %5.1f %5.0f |" % (c["lateral"], c["nose_deg"], c["vel_deg"], c["peak_cross"])
            print(row)
    print()
    print("B. Carrot line-follow (0.75 s carrot, at least 0.3 r_gate = 331 m; start 1 km off the line, parallel): at 3 r_gate")
    print("   along the line: lateral m, nose angle to the line deg, velocity angle deg, peak overshoot across the line m")
    print("   (lateral / nose / vel / cross per variant)")
    print("cls  | " + " | ".join("%-22s" % v for v in VARIANTS))
    for ship in S.SHIPS:
        row = "%-3s  |" % ship[0]
        for var in VARIANTS:
            c = carrot_line(ship, var)
            row += " %5.0f %5.1f %5.1f %5.0f |" % (c["lateral"], c["nose_deg"], c["vel_deg"], c["peak_cross"])
        print(row)


if __name__ == "__main__":
    main()
