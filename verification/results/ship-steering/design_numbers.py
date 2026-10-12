#!/usr/bin/env python3
"""Derived steering/avoidance parameters for the five representative installed ships
(docs/architecture/ship-steering-and-avoidance.md section 5). Inputs are the per-ship
rows of verification/results/game-scale/recommend.txt (installed Mayhem 4 TShips, base
values): v m/s, a m/s^2, omega deg/s (slower of yaw/pitch), r m (collision radius).
k = TShips column 51 / 100 (angular acceleration = rate cap x k per second) is NOT in
recommend.txt; the values below are the sample rows of ship-movement.md section 4
(M4 0.5, TS 0.3, TL 0.1) applied by class and are marked assumed in the note.
Everything printed is derived (inferred), not measured.
"""
import math
ships = [  # class, name, v m/s, a m/s^2, omega deg/s, r m, k assumed, large?
    ("M5", "Discoverer", 532.0, 55.0, 147.66, 9.2, 0.5, False),
    ("M3", "Nova", 247.5, 40.0, 92.81, 11.9, 0.5, False),
    ("M6", "Centaur", 180.0, 20.0, 33.75, 64.4, 0.3, True),
    ("TS", "Mercury", 124.0, 30.0, 21.09, 38.5, 0.3, True),
    ("M2", "Boreas", 70.0, 15.0, 13.5, 705.4, 0.1, True),
]
TAU = 0.3      # s, linear tail time constant of the rate law
T_TICK = 0.05  # s, worst re-plan interval (reaction delay 1 x 50 ms)
T_R, K_RAMP, L_MAX = 0.5, 1.25, 30000.0
def lookahead(v, a, om_dps, r, r_obs):
    om = math.radians(om_dps)
    R = v / om
    C = r + r_obs + max(25.0, 0.5 * r)
    d_turn = math.sqrt(C * (C + 2 * R))
    d_stop = v * v / (2 * a)
    L = min(max(T_R * v + K_RAMP * min(d_turn, d_stop), C + r), L_MAX)
    return R, C, d_turn, d_stop, L
print("cls name | W deg/s | a_ang deg/s^2 (k) | rate error a_ang*50ms deg/s (%W) | theta_lin=a_ang*tau^2 deg | "
      "R m | d_sched=v^2/a m | v_corner(90deg,1km) m/s | L same m | L station1500 m | L asteroid60 m | arrival max(2r,R/4) m | roll")
for cls, name, v, a, om, r, k, large in ships:
    a_ang = om * k
    err = a_ang * T_TICK
    theta_lin = a_ang * TAU * TAU
    R, C, dt_, ds_, L_same = lookahead(v, a, om, r, r)
    _, _, _, _, L_st = lookahead(v, a, om, r, 1500.0)
    _, _, _, _, L_as = lookahead(v, a, om, r, 60.0)
    d_sched = v * v / a
    v_corner = math.radians(om) * 1000.0 / 2.0
    arrival = max(2 * r, R / 4)
    print(f"{cls} {name} | {om:.1f} | {a_ang:.1f} ({k}) | {err:.2f} ({100*err/om:.1f}%) | {theta_lin:.2f} | {R:.0f} | "
          f"{d_sched:.0f} | {v_corner:.0f} | {L_same:.0f} | {L_st:.0f} | {L_as:.0f} | {arrival:.0f} | {'level' if large else 'bank'}")
# Obstacle-scan cost model (inferred): snapshot scan per call, calls per second per ship, ships.
objs, ships_n, calls_s, ns_per = 420, 200, 40, 1.5
print(f"\nscan cost: {objs} objects x {ns_per} ns = {objs*ns_per/1000:.2f} us per call; "
      f"x {calls_s} calls/s x {ships_n} ships = {objs*ns_per*calls_s*ships_n/1e6:.1f} ms/s = "
      f"{objs*ns_per*calls_s*ships_n/1e6/60:.3f} ms per frame at 60 fps")
