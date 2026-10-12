#!/usr/bin/env python3
"""Capital join model for docs/architecture/capital-gate-transit.md, amendment 15 (inferred unless a line says measured).

    python3 -I verification/results/ship-steering/capital-gate-transit/join_budget.py > \
        verification/results/ship-steering/capital-gate-transit/join_budget.txt

A 2-D kinematic model in the plane of the lateral offset (yaw only):
* plant: per-axis controller 0x0043ad20 (ship-movement.md section 4: brake when the written remainder e < w^2/alpha,
  else accelerate to W), alpha = W * f+0x2b0 (0.5) * rot_pct (10) / 100 = 0.05 W (Run156/157 rows rot=0x8000,
  rot_pct=10, W=1843/1638 binary/s: measured); the module's servo writes e = w_cmd^2/alpha + w_cmd*tick with
  w_cmd = min(sqrt(alpha*theta), theta/0.15, W) (rate_command); the velocity direction is the nose through a
  first-order lag tau = 2 s (slip-aware-steering.md section 1); speed slews at the hull's acceleration toward the
  written desired speed.
* laws:
  ALT  = the brief's carrot max(2 v / omega_eff, 1.5 e), omega_eff = 2.3 deg/s measured, window 6 r, 100 m/s.
  CORRIDOR = a bounded-intercept corridor from anywhere: carrot max(15 s v, 1.5 e), no window, 100 m/s.
  A13  = gate_lead_in_capital as installed: window 6 r before X blended from A between 6 r and 5 r, carrot
         max(0.75 s v, 2 v / W, 0.3 r) ahead of the projection (never beyond A), 100 m/s written both directions.
  A15  = amendment 15: the A13 pursuit unchanged; the written speed is the highest of {100, 60, 40, 25} m/s whose
         forward prediction (the same pursuit from the current state) reaches X with nose <= 4 deg, velocity <= 6 deg,
         |e| <= 50 m, excursion <= 50 m; re-evaluated every 1 s, lowered at once, raised only when the higher candidate
         passes; none passes, or s > 0 -> FB, latched.
  FB   = the fallback: aim at A, speed min(100, sqrt(25^2 + 2 a (d - 0.75 r)), omega_eff d / (2 sin theta)) floored at
         10 m/s (theta >= 90 deg: the floor), omega_eff = 2.3 deg/s, the engine's 0.75 r stop at the sphere (P2 vanilla), 0x1f rotate of the nose
         error under the same controller (engine-written remainder), order 5 from rest.
Totals add order 5 + launch from budget.txt (model: M2 o5 37.5/15.0 s continuous, 42.9/20.4 s from rest at SETA 1/10;
M1 36.0/15.4 and 47.1/26.6; launch 0.93 s).
Starts copied from run157-triage/capital_timeline.txt and signed_lateral.txt (measured states); 'nose at A' starts are
the state a vanilla approach would have there.
Gate-warn exposure: seconds spent moving with d(G) <= b + L + 2(rA + rB) and the gate inside the warn cone
(ship-navigation-ai.md section 3.1: L = clamp(rA + rB/10, 30 m, 1 km) + (rA + rB) + rA cos(theta), b = v^2/2a capped
600 m, cone cos^2 theta > 1 - ((rA + rB + 40 m)/(d - b))^2): what the order-4 approach is exposed to without the
amendment-15 site-E gate branch.
"""
import math

R_GATE = 1105.0
DT = 1 / 30.0
TAU_SLIP = 2.0
K_ROT = 0.05
TOL = 8.11
CAP = 100.0
V_FLOOR_JOIN = 20.0
V_FLOOR_FB = 10.0
SHIPS = {
    'M2': dict(vmax=110.0, acc=10.0, W=math.radians(1843 / 65536 * 360), r=4392 / 2 - R_GATE, o5=(37.5, 15.0), o5_rest=(42.9, 20.4)),
    'M1': dict(vmax=113.0, acc=5.0, W=math.radians(1638 / 65536 * 360), r=4253 / 2 - R_GATE, o5=(36.0, 15.4), o5_rest=(47.1, 26.6)),
}
CROSS = 0.75 * R_GATE      # X is this far before A along F
LAUNCH = 0.93


def depth_X(ship):
    """Distance from the gate origin G to X along the axis (slot 0)."""
    return 2 * (R_GATE + ship['r']) + CROSS


def t_turn(dpsi, W, alpha):
    if dpsi <= 0:
        return 0.0
    if dpsi <= W * W / alpha:
        return 2 * math.sqrt(dpsi / alpha)
    return W / alpha + dpsi / W


def rotate_time(theta_deg, W, alpha):
    """0x1f from rest, engine-written remainder, until within TOL (budget.py's engine column)."""
    e, w, t = math.radians(theta_deg), 0.0, 0.0
    while math.degrees(e) > TOL and t < 200:
        if e < w * w / alpha:
            w = max(0.0, w - alpha * DT)
        else:
            w = min(W, w + alpha * DT)
        e -= w * DT
        t += DT
    return t


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


class Sim:
    def __init__(self, ship, s0, e0, psi0_deg, v0=CAP):
        self.sh = ship
        self.W, self.alpha = ship['W'], ship['W'] * K_ROT
        self.x, self.y = s0, e0
        self.psi = math.radians(psi0_deg)
        self.upsi = self.psi
        self.w = 0.0
        self.v = v0
        self.t = 0.0
        self.ref = 1 if e0 >= 0 else -1
        self.cross, self.over, self.vmin, self.wpk, self.warn_s = 0, 0.0, v0, 0.0, 0.0

    def step(self, px, py, v_des):
        dx, dy = px - self.x, py - self.y
        theta = wrap(math.atan2(dy, dx) - self.psi)
        a = abs(theta)
        w_cmd = min(math.sqrt(self.alpha * a), a / 0.15, self.W)
        e_wr = min(w_cmd * w_cmd / self.alpha + w_cmd * 0.05, a)
        sgn = 1 if theta >= 0 else -1
        w = self.w * sgn                      # rate toward the target side
        if w < 0:
            w = min(0.0, w + self.alpha * DT)
        elif e_wr < w * w / self.alpha:
            w = max(0.0, w - self.alpha * DT)
        else:
            w = min(self.W, w + self.alpha * DT)
        turn = w * DT
        if turn > 0:
            turn = min(turn, a)
        self.psi += sgn * turn
        self.w = sgn * w
        self.wpk = max(self.wpk, abs(w))
        v_des = min(v_des, self.sh['vmax'])
        if self.v < v_des:
            self.v = min(v_des, self.v + self.sh['acc'] * DT)
        else:
            self.v = max(v_des, self.v - self.sh['acc'] * DT)
        self.vmin = min(self.vmin, self.v)
        self.upsi += wrap(self.psi - self.upsi) * (1 - math.exp(-DT / TAU_SLIP))
        self.x += self.v * math.cos(self.upsi) * DT
        self.y += self.v * math.sin(self.upsi) * DT
        side = 1 if self.y >= 0 else -1
        if side != self.ref:
            self.cross += 1
            self.ref = side
        if self.cross:
            self.over = max(self.over, abs(self.y))
        self.warn_s += DT * self.gate_warn()
        self.t += DT

    def gate_warn(self):
        """1 when the engine's warn test would fire against the gate this frame (slot 0 geometry)."""
        if self.v <= 0:
            return 0
        gx = depth_X(self.sh)                 # G sits at s = +depth_X on the axis
        dx, dy = gx - self.x, -self.y
        d = math.hypot(dx, dy)
        ra, rb = self.sh['r'], R_GATE
        cos_t = (math.cos(self.psi) * dx + math.sin(self.psi) * dy) / d
        b = min(600.0, self.v * self.v / (2 * self.sh['acc']))
        L = min(max(ra + rb / 10, 30.0), 1000.0) + (ra + rb) + (ra * cos_t if cos_t > 0 else 0)
        if b + L + (ra + rb) < d or cos_t <= 0:
            return 0
        if d <= b:
            return 1
        return 1 if cos_t * cos_t > 1 - ((ra + rb + 40.0) / (d - b)) ** 2 else 0

    def nose_err(self):
        return math.degrees(abs(wrap(self.psi)))

    def vel_err(self):
        return math.degrees(abs(wrap(self.upsi)))


def a13_step(S, v_des, window=True):
    """One frame of the installed lead-in (gate_lead_in_capital): inside the window the carrot, outside A."""
    r = S.sh['r']
    L = max(0.75 * S.v, 2 * S.v / S.W, 0.3 * r)
    if not window or -6 * R_GATE <= S.x <= CROSS:
        along = min(S.x + L, CROSS)
        tb = (-S.x - 5 * R_GATE) / R_GATE
        if window and tb > 0:
            along += min(tb, 1.0) * (CROSS - along)
        S.step(along, 0.0, v_des)
    else:
        S.step(CROSS, 0.0, v_des)


def a13(ship, s0, e0, psi0, t_max=900):
    S = Sim(ship, s0, e0, psi0)
    while S.x < 0 and S.t < t_max:
        a13_step(S, CAP)
    return S


CANDIDATES = (100.0, 60.0, 40.0, 25.0)
OMEGA_EFF = math.radians(2.3)   # measured effective join rate (Run157 frame 18240, rate=418 binary/s)
PRED_NOSE, PRED_VEL = 4.0, 6.0   # predictor acceptance (deg): half the 0x1f tolerance, so a re-acceleration cannot spend it
V_ENTRY = 25.0                  # fallback: sphere entry speed (the vanilla crossing speed is 21-28 m/s, budget.txt)


def predict(S, v):
    """The amendment-15 predictor: the A13 pursuit from S's state at the written speed v (slewing from S.v), until X;
    True when it converges. The module would run this at its decision cadence (here every 1 s)."""
    P = Sim(S.sh, S.x, S.y, math.degrees(S.psi), v0=S.v)
    P.w, P.upsi = S.w, S.upsi
    t_max = 3 * (-S.x + abs(S.y)) / v + 60
    while P.x < 0 and P.t < t_max:
        a13_step(P, v, window=False)
    # a margin over the acceptance (8.11 deg, 100 m): the predictor's own plant is the model's, not the engine's
    return P.x >= 0 and P.nose_err() <= PRED_NOSE and P.vel_err() <= PRED_VEL and abs(P.y) <= 50 and P.over <= 50


def fb_speed(S):
    dx, dy = CROSS - S.x, -S.y
    d = math.hypot(dx, dy)
    theta = abs(wrap(math.atan2(dy, dx) - S.psi))
    ramp = math.sqrt(V_ENTRY ** 2 + 2 * S.sh['acc'] * max(0.0, d - CROSS))
    if theta >= math.pi / 2:
        chord = V_FLOOR_FB
    else:
        chord = OMEGA_EFF * d / (2 * math.sin(theta)) if theta > 1e-3 else CAP
    return max(V_FLOOR_FB, min(CAP, ramp, chord)), d


def a15(ship, s0, e0, psi0, t_max=900):
    """Amendment 15: the A13 pursuit, the speed chosen by the predictor (highest converging candidate, re-evaluated every
    1 s; raise only when the higher candidate converges), fallback latched when none converges or s > 0.
    Returns (S, mode, rotate_s) with mode 'join' (X reached in join mode) or 'fallback' (stopped at the sphere)."""
    S = Sim(ship, s0, e0, psi0)
    mode, v_cmd, next_eval = 'join', CAP, 0.0
    while S.t < t_max:
        in_window = -6 * R_GATE <= S.x <= 0
        if mode == 'join' and S.x > 0:
            mode = 'fallback' if not converged(S) else mode
            if mode == 'join':
                break
        if mode == 'join' and in_window and S.t >= next_eval:
            next_eval = S.t + 1.0
            pick = None
            for v in CANDIDATES:
                if v > v_cmd + 1 and not predict(S, v):
                    continue
                if v <= v_cmd + 1 and v < v_cmd - 1 and pick is not None:
                    break
                if predict(S, v):
                    pick = v
                    break
            if pick is None:
                mode = 'fallback'
            else:
                v_cmd = pick
        if mode == 'join':
            if S.x >= 0:
                break
            a13_step(S, v_cmd if in_window else CAP)
        else:
            v, d = fb_speed(S)
            if d <= CROSS:
                break
            S.step(CROSS, 0.0, v)
    if mode == 'join' and converged(S):
        return S, mode, 0.0
    return S, 'fallback', rotate_time(S.nose_err(), S.W, S.alpha)


def bounded_intercept(ship, s0, e0, psi0, k=1.5, t_max=900):
    """The brief's alternative: carrot max(2 v / omega_eff, k e) with omega_eff the measured 2.3 deg/s, window 6 r."""
    S = Sim(ship, s0, e0, psi0)
    while S.x < 0 and S.t < t_max:
        if -6 * R_GATE <= S.x:
            L = max(2 * S.v / OMEGA_EFF, k * abs(S.y))
            S.step(min(S.x + L, CROSS), 0.0, CAP)
        else:
            S.step(CROSS, 0.0, CAP)
    return S


def corridor(ship, s0, e0, psi0, k=1.5, T_L=15.0, t_max=900):
    """Alternative: bounded-intercept corridor from anywhere (no window), carrot max(v T_L, k e), 100 m/s."""
    S = Sim(ship, s0, e0, psi0)
    while S.x < 0 and S.t < t_max:
        S.step(min(S.x + max(S.v * T_L, k * abs(S.y)), CROSS), 0.0, CAP)
    return S


def fallback(ship, s0, e0, psi0, t_max=900):
    S = Sim(ship, s0, e0, psi0)
    while S.t < t_max:
        v, d = fb_speed(S)
        if d <= CROSS:
            break
        S.step(CROSS, 0.0, v)
    return S, rotate_time(S.nose_err(), S.W, S.alpha)


def fmt(S):
    return (f"t_X={S.t:6.1f}s nose={S.nose_err():5.1f} vel={S.vel_err():5.1f} v_X={S.v:5.1f} e_X={abs(S.y):5.0f}m "
            f"cross={S.cross} over={S.over:4.0f}m v_min={S.vmin:5.1f} w_pk={math.degrees(S.wpk):4.1f}d/s gate_warn={S.warn_s:5.1f}s")


def converged(S):
    return S.x >= 0 and S.nose_err() <= TOL and abs(S.y) <= 100 and S.over <= 100


def total(ship, t_x, ok, rot):
    return [t_x + (ship['o5'][i] if ok else rot + ship['o5_rest'][i]) + LAUNCH for i in (0, 1)]


def main():
    print(__doc__.split('\n')[0])
    print("ok = X reached with the nose within 8.11 deg, |e| <= 100 m, no excursion beyond 100 m past the axis.")
    print("total = to X + order 5 + launch (continuous when ok, else stop + 0x1f + order 5 from rest), SETA 1 / SETA 10.\n")
    starts = [
        ('S1 brief: 6000 m lateral, 5 r before X, nose at A', -5 * R_GATE, 6000.0, None),
        ('S2 fixture: 6435 m lateral, 5.58 r (Run157 frame 17272 position), nose at A', -5.58 * R_GATE, 6435.0, None),
        ('S3 on axis, 5 r before X, aligned', -5 * R_GATE, 0.0, 0.0),
        ('S4 far: Run157 frame 761 (15932 m lateral, 35.8 r), nose at A', -35.8 * R_GATE, 15932.0, None),
        ('S5 6.4 km lateral, 5 r, nose 60 deg away (after an 0x12 leg)', -5 * R_GATE, 6435.0, 60.0),
        ('S6 Run157 frame 18472 after 0x12: 627 m, 2.97 r, nose 68 deg away', -2.97 * R_GATE, 627.0, 68.0),
        ('S7 Run157 escort M2 order-4 start: 1645 m, 0.70 r, nose 91 deg toward', -0.70 * R_GATE, 1645.0, -91.0),
        ('S8 Run157 M1 order-4 start: 3252 m, s +2.05 r (past X), nose 135 deg', 2.05 * R_GATE, 3252.0, 135.0),
    ]
    for name, ship in SHIPS.items():
        W, alpha = ship['W'], ship['W'] * K_ROT
        print(f"== {name}: W {math.degrees(W):.1f} deg/s, alpha {math.degrees(alpha):.2f} deg/s^2, a_lin {ship['acc']} m/s^2, "
              f"r {ship['r']:.0f} m, X-G {depth_X(ship):.0f} m; A13 carrot at 100 m/s {max(75, 200 / W, 0.3 * ship['r']):.0f} m")
        for sname, s0, e0, p0 in starts:
            psi0 = -math.degrees(math.atan2(e0, CROSS - s0)) if p0 is None else p0
            print(f"  {sname} (psi {psi0:+.0f})")
            if s0 < 0:
                S = a13(ship, s0, e0, psi0)
                ok = converged(S)
                t = total(ship, S.t, ok, rotate_time(S.nose_err(), W, alpha))
                print(f"    A13 installed   {fmt(S)} ok={ok} total {t[0]:5.1f}/{t[1]:5.1f}")
                S = bounded_intercept(ship, s0, e0, psi0)
                ok = converged(S)
                t = total(ship, S.t, ok, rotate_time(S.nose_err(), W, alpha))
                print(f"    ALT k=1.5       {fmt(S)} ok={ok} total {t[0]:5.1f}/{t[1]:5.1f}")
                S = corridor(ship, s0, e0, psi0)
                ok = converged(S)
                t = total(ship, S.t, ok, rotate_time(S.nose_err(), W, alpha))
                print(f"    CORRIDOR k=1.5  {fmt(S)} ok={ok} total {t[0]:5.1f}/{t[1]:5.1f}")
            S, mode, rot = a15(ship, s0, e0, psi0)
            ok = mode == 'join'
            t = total(ship, S.t, ok, rot)
            print(f"    A15 {mode:8s}    {fmt(S)} rotate {rot:4.1f}s total {t[0]:5.1f}/{t[1]:5.1f}")
        print()
    print("== gate warn reach (slot 0, nose at G): d at which b + L + 2(rA + rB) >= d, by speed")
    for name, ship in SHIPS.items():
        ra, rb = ship['r'], R_GATE
        for v in (10, 28, 50, 100):
            b = min(600.0, v * v / (2 * ship['acc']))
            reach = b + min(max(ra + rb / 10, 30), 1000) + (ra + rb) + ra + (ra + rb)
            print(f"  {name} v={v:3d} m/s: reach {reach:6.0f} m (X at {depth_X(ship):.0f} m from G)")
    print("\n== P2 keep-speed bound: coast under order 0 (v^2 / 2a) must end before the order-5 arrival sphere")
    for name, ship in SHIPS.items():
        for seta in (1, 10):
            R5 = (ship['r'] + 10.0) * (1 + (seta - 1) / 4)
            room = depth_X(ship) - R5
            print(f"  {name} SETA {seta:2d}: order-5 sphere {R5:5.0f} m, room {room:5.0f} m, v_bound {math.sqrt(2 * ship['acc'] * room):5.0f} m/s")


if __name__ == '__main__':
    main()
