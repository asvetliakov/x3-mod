"""Two rounding details of the engine against a ship_trace log: (1) the ship roll gamma = obj+0x20 read off the row's
basis, rounded, floored or truncated, on the port-roll match steps of legs with a known port roll (the replay's
port_rolls()), judged by the roll rate the controller then predicts; (2) the operation order of the angular acceleration
acc = W x k with k = f+0x2b0 x record+0xd0 / 100 in 16.16, on leaf steps of orders 4/5 outside the match (yaw and pitch
rates both exact). Usage: python3 -I replay_rounding.py <trace or x3m.log>"""
import collections
import math
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'probe'))
import ship_trace_replay as R  # noqa: E402

groups = {}
for r in R.iter_rows(sys.argv[1]):
    groups.setdefault(r['obj'], []).append(r)

rots = collections.Counter()
accv = collections.Counter()
gam = collections.Counter()


def accs(a):
    W, rot, pct = a['W'], a['rot'], a['rot_pct']
    k1 = R.fx(rot, R.tdiv(0x10000 * pct, 100))
    v1 = [R.fx(W[i], k1) for i in range(3)]
    v2 = [R.fx(R.fx(W[i], rot), R.tdiv(0x10000 * pct, 100)) for i in range(3)]
    v3 = [R.tdiv(R.fx(W[i], rot) * pct, 100) for i in range(3)]
    k4 = R.tdiv(rot * pct, 100)
    v4 = [R.fx(W[i], k4) for i in range(3)]
    return {'fx(W,fx(rot,pct))': v1, 'fx(fx(W,rot),pct)': v2, 'fx(W,rot)*pct/100': v3, 'fx(W,rot*pct/100)': v4}


for obj, rows in groups.items():
    gammas = R.port_rolls(rows)
    carried = None
    for n in range(len(rows) - 1):
        a, b = rows[n], rows[n + 1]
        if b['frame'] != a['frame'] + 1 or a['order'] != b['order']:
            carried = None
            continue
        rots[(a['class'], hex(a['rot']), a['rot_pct'])] += 1
        e = a['e'] if a['e'] is not None else carried
        if e is None:
            continue
        e = list(e)
        basis = R.basis_from_row(a['fwd'], a['up'])
        if R.match_condition(a) and a['tgt'] in gammas:
            g = math.atan2(basis[0][1], basis[1][1]) * 65536.0 / (2 * math.pi)
            for name, gb in (('round', R.llround(g)), ('floor', math.floor(g)), ('trunc', int(g))):
                ee = list(e)
                ee[2] = R.wrap16(gammas[a['tgt']] - gb)
                acc = R.axis_acc(a['W'], a['rot'], a['rot_pct'])
                _, w2, _ = R.integrate_axis(ee[2], a['rate'][2], a['W'][2], acc[2], a['dt_ms'])
                d = abs(w2 - b['rate'][2])
                gam[(name, '0' if d == 0 else '<=1' if d <= 1 else '<=3' if d <= 3 else '>3')] += 1
        # acceleration variants on leaf steps outside the match, yaw/pitch
        if a['e'] is not None and not R.match_condition(a) and a['order'] in (4, 5):
            for name, acc in accs(a).items():
                ok = True
                for i in (0, 1):
                    _, w2, _ = R.integrate_axis(e[i], a['rate'][i], a['W'][i], acc[i], a['dt_ms'])
                    if w2 != b['rate'][i]:
                        ok = False
                accv[(name, a['rot'] == 0x10000, ok)] += 1
        acc = R.axis_acc(a['W'], a['rot'], a['rot_pct'])
        carried = tuple(R.integrate_axis(e[i], a['rate'][i], a['W'][i], acc[i], a['dt_ms'])[0] for i in range(3))
print('rot values (class, rot, pct): steps', rots.most_common(12))
print('gamma rounding on match steps with a known port roll (variant, |roll rate residual|):')
for k, c in sorted(gam.items()):
    print('  ', k, c)
print('acceleration operation order on leaf steps (variant, rot == 1.0, yaw+pitch exact):')
for k, c in sorted(accv.items()):
    print('  ', k, c)
