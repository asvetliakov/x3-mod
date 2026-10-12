"""Amendment 16b (capital-gate-transit.md section 16.10): how far the trailing ship's spacing can fall below the sequencing
target D_x when it obeys the follow cap of ship_capital_lane_core.h (follow_cap: the lower of the sum form and the stopping
form, floor 1 unit/s) re-written once per decision interval tau from the leader's state as stored at its last refresh (age tau),
while the leader brakes at its full deceleration (or to half its speed) at any time. Pure kinematics along F, speeds slew at
the hull's acceleration (both directions), the 100 m/s order-4 cap. Prints the worst undershoot D_x - min(ds) per (a_T, a_L,
tau) and the worst case's start, for two caps: "a16" = follow_cap as built for (b); "16b" = the sequencing cap of section 16.10
(the stored leader speed aged by a_L x age, the room less the trailer's travel over the latency tau_r = tau + age, and the
relative-braking form when a_T > a_L). Usage: python3 -I seq_cap_undershoot.py > seq_cap_undershoot.txt"""
import itertools
import math

FLOOR = 1.0 / 500.0
VMAX = 100.0


def follow_cap(vL, aT, ds, D, aL):
    room, vL = ds - D, max(0.0, vL)
    cap = vL + (math.sqrt(2 * aT * room) if room > 0 else 0.0)
    k = vL * vL * aT / aL + 2 * aT * room
    return max(FLOOR, min(cap, math.sqrt(k) if k > 0 else 0.0))


def seq_cap(vL_stored, age, vT, aT, ds, D, aL, tau):
    vL = max(0.0, vL_stored - aL * age)
    room = ds - D - vT * (tau + age)
    if room <= 0:
        return FLOOR
    cap = min(vL + math.sqrt(2 * aT * room), math.sqrt(vL * vL * aT / aL + 2 * aT * room))
    if aT > aL:
        vrel = math.sqrt(2 * (aT - aL) * room)
        if vrel / (aT - aL) <= vL / aL:
            cap = min(cap, vL + vrel)
    return max(FLOOR, cap)


def run(aT, aL, tau, vL0, vT0, gap0, tb, v_end, dt=0.02, T=150.0, kind="a16"):
    D = 0.0  # spacing measured relative to D_x
    s_L, s_T, vL, vT = gap0, 0.0, vL0, vT0
    stored = (s_L, vL)  # the leader's board state (refreshed every tau)
    capf = (lambda st, sT, v: follow_cap(st[1], aT, st[0] - sT, D, aL)) if kind == "a16" else \
        (lambda st, sT, v: seq_cap(st[1], tau, v, aT, st[0] - sT, D, aL, tau))
    w = min(VMAX, capf(stored, s_T, vT))
    vT = min(vT, w)  # admissible start: the trailer already obeys the cap
    t, next_dec, worst = 0.0, 0.0, s_L - s_T
    while t < T:
        if t >= next_dec - 1e-9:
            w = min(VMAX, capf(stored, s_T, vT))
            stored = (s_L, vL)
            next_dec += tau
        vT = min(w, vT + aT * dt) if vT < w else max(w, vT - aT * dt)
        if t >= tb and vL > v_end:
            vL = max(v_end, vL - aL * dt)
        s_L += vL * dt
        s_T += vT * dt
        worst = min(worst, s_L - s_T)
        t += dt
    return worst


def main():
    for kind in ("a16", "16b"):
        sweep(kind)


def sweep(kind):
    accs = [2.0, 5.0, 10.0]
    print(f"## cap {kind}")
    print("# undershoot = max(0, D_x - min ds) over the sweep, m (measured by this script; kinematic model)")
    print("# a_T a_L tau_s | worst_undershoot_m | at vL0 vT0 gap0 tb v_end")
    overall = 0.0
    for aT, aL, tau in itertools.product(accs, accs, [0.25, 0.5, 1.0]):
        worst, arg = 0.0, None
        for vL0, vT0, gap0, tb in itertools.product([0, 25, 50, 75, 100], [0, 50, 100], [0, 25, 100, 300, 1000, 3000], [0, 0.5, 2, 10, 30]):
            for v_end in (0.0, vL0 / 2):
                u = -run(aT, aL, tau, float(vL0), float(vT0), float(gap0), tb, v_end, kind=kind)
                if u > worst:
                    worst, arg = u, (vL0, vT0, gap0, tb, v_end)
        overall = max(overall, worst)
        print(f"{aT:4.0f} {aL:4.0f} {tau:5.2f} | {worst:8.1f} | {arg}")
    print(f"cap {kind}: overall worst undershoot {overall:.1f} m")


if __name__ == "__main__":
    main()
