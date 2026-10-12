"""Model budget (join_budget.py's plant and its A15 law, inferred) for the amendment-15 host-fixture scenarios E-G
(verification/probe/ship_steering_host.cpp, capital_join_scenario): time to X, nose/velocity at X, the excursion past the axis,
the lowest written speed and the mode, per start and ship. Usage: python3 -I a15_fixture_budget.py > a15_fixture_budget.txt"""
import importlib.util, math, os
sp = importlib.util.spec_from_file_location('jb', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'join_budget.py'))
jb = importlib.util.module_from_spec(sp); sp.loader.exec_module(jb)
R = jb.R_GATE
STARTS = [('E1 6435 m lateral, 5.58 r, nose at A', -5.58 * R, 6435.0, None),
          ('E2 6435 m lateral, 5 r, nose at A', -5.0 * R, 6435.0, None),
          ('E3 on axis, 5 r, aligned', -5.0 * R, 0.0, 0.0),
          ('F1 627 m, 2.97 r, nose 68 deg away', -2.97 * R, 627.0, 68.0),
          ('F2 6435 m, 5 r, nose 60 deg away', -5.0 * R, 6435.0, 60.0),
          ('G1 1645 m, 0.70 r, nose 91 deg toward', -0.70 * R, 1645.0, -91.0),
          ('G2 3252 m, s +2.05 r, nose 135 deg', 2.05 * R, 3252.0, 135.0)]
print(__doc__.split('\n')[0])
for name in ('M2', 'M1'):
    sh = jb.SHIPS[name]
    for sname, s0, e0, p0 in STARTS:
        psi0 = -math.degrees(math.atan2(e0, jb.CROSS - s0)) if p0 is None else p0
        S, mode, rot = jb.a15(sh, s0, e0, psi0)
        print(f'{name} {sname:40s} psi0 {psi0:+6.1f}  mode={mode:8s} t_X={S.t:6.1f}s nose={S.nose_err():5.1f} vel={S.vel_err():5.1f} '
              f'v_X={S.v:5.1f} e_X={abs(S.y):5.0f}m cross={S.cross} over={S.over:4.0f}m v_min={S.vmin:5.1f} rotate={rot:4.1f}s')
