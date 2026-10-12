"""Minimum along distance before X (m) from which the installed A13 pursuit (carrot max(0.75 v, 2 v/W, 0.3 r), no window,
constant speed v) converges (join_budget.converged: nose <= 8.11 deg, |e| <= 100 m, excursion <= 100 m), by speed, start
nose (at A, or the given angle off F turned away from the axis) and lateral offset e. Model (inferred) on the plant of
join_budget.py, which a13_replay.py checks against the Run157 leg 18000-18240 (measured). Usage: python3 -I a13_feasibility.py"""
import importlib.util, math, os
sp = importlib.util.spec_from_file_location('jb', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'join_budget.py'))
jb = importlib.util.module_from_spec(sp); sp.loader.exec_module(jb)
ES = (0, 500, 1000, 2000, 4000, 6000, 8000)
def run(sh, s0, e0, psi0, v):
    S = jb.Sim(sh, s0, e0, psi0, v0=v)
    t_max = 3 * (-s0 + e0) / v + 200
    while S.x < 0 and S.t < t_max:
        L = max(0.75 * S.v, 2 * S.v / S.W, 0.3 * sh['r'])
        S.step(min(S.x + L, jb.CROSS), 0.0, v)
    return jb.converged(S)
print(__doc__.split('\n')[0])
print('ship  v   nose     ' + ''.join('%7d' % e for e in ES))
for name in ('M2', 'M1'):
    sh = jb.SHIPS[name]
    for v in (100.0, 60.0, 40.0, 25.0):
        for away in (None, 30.0, 60.0, 90.0):
            row = []
            for e in ES:
                lo, hi = 0.0, 20 * jb.R_GATE
                p = lambda d: -math.degrees(math.atan2(e, jb.CROSS + d)) if away is None else (away if e else away)
                if not run(sh, -hi, e, p(hi), v):
                    row.append('  >22km'); continue
                for _ in range(12):
                    mid = 0.5 * (lo + hi)
                    if run(sh, -mid, e, p(mid), v): hi = mid
                    else: lo = mid
                row.append('%7.0f' % hi)
            print('%s %5.0f %-7s ' % (name, v, 'at A' if away is None else '+%d' % away) + ''.join(row))
