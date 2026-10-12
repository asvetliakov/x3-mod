"""Does the amendment-15 predictor give the same verdict at a coarse step (0.25 s) as at the model's 1/30 s?
For each start of join_budget.py and each candidate speed: converged (strict predictor test) at both steps, and the time
per evaluation in this Python port (inferred cost ratio only). Usage: python3 -I predictor_dt.py"""
import importlib.util, math, os, time
sp = importlib.util.spec_from_file_location('jb', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'join_budget.py'))
jb = importlib.util.module_from_spec(sp); sp.loader.exec_module(jb)
R = jb.R_GATE
starts = [('S1', -5 * R, 6000.0, None), ('S2', -5.58 * R, 6435.0, None), ('S5', -5 * R, 6435.0, 60.0), ('S6', -2.97 * R, 627.0, 68.0),
          ('S7', -0.70 * R, 1645.0, -91.0), ('edge 4r/4km/+60', -4 * R, 4000.0, 60.0), ('edge 3r/2km/+30', -3 * R, 2000.0, 30.0)]
mism = 0
for name in ('M2', 'M1'):
    sh = jb.SHIPS[name]
    for sname, s0, e0, p0 in starts:
        psi0 = -math.degrees(math.atan2(e0, jb.CROSS - s0)) if p0 is None else p0
        out = []
        for v in jb.CANDIDATES:
            res = []
            for dt in (1 / 30.0, 0.25):
                jb.DT = dt
                S = jb.Sim(sh, s0, e0, psi0)
                t0 = time.perf_counter(); ok = jb.predict(S, v); el = time.perf_counter() - t0
                res.append((ok, el))
            jb.DT = 1 / 30.0
            mism += res[0][0] != res[1][0]
            out.append('%3.0f:%s/%s' % (v, 'Y' if res[0][0] else 'n', 'Y' if res[1][0] else 'n'))
        print('%s %-16s %s' % (name, sname, '  '.join(out)))
print('verdict mismatches (1/30 s vs 0.25 s): %d' % mism)
