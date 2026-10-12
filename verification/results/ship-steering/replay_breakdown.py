"""Break down the replay residuals of a real trace (ship_trace_replay.py, dt = row n's dt_ms):
1. the conditional position residual pos_c split into its component along fwd(n+1) and lateral to it, for straight
   steps (rate 0 at n and n+1) and turning steps, per order (the slip term predicts lateral residuals on turning
   steps and decaying lateral residuals after them; a dt or unit error predicts longitudinal ones);
2. the rate-controller steps: the e and rate signs of row n, the leaf_call flag, |rate(n+1) - pred| buckets, and
   whether the actual rate is consistent with a rate computed from a different e (e = 0, or the written e sign).
Usage: python3 -I replay_breakdown.py x3m.log"""
import collections
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'probe'))
import ship_trace_replay as R  # noqa: E402


def pct(v, p):
    v = sorted(v)
    return v[int((len(v) - 1) * p / 100)] if v else float('nan')


def main(path):
    groups = {}
    for r in R.iter_rows(path):
        groups.setdefault(r['obj'], []).append(r)
    split = collections.defaultdict(lambda: {'along': [], 'lat': []})
    rc = collections.Counter()
    rc_examples = []
    for rows in groups.values():
        carried = None
        for n in range(len(rows) - 1):
            a, b = rows[n], rows[n + 1]
            if b['frame'] != a['frame'] + 1:
                carried = None
                continue
            e = a['e'] if a['e'] is not None else carried
            w, e_new, v, brows, pos = R.step(a, e, a['dt_ms'])
            carried = tuple(e_new)
            f = R._norm(b['fwd'])
            cond = [a['pos'][i] + f[i] * float(b['v']) * a['dt_ms'] / 1000.0 for i in range(3)]
            d = [b['pos'][i] - cond[i] for i in range(3)]
            along = R._dot(d, f)
            lat = math.sqrt(max(0.0, R._dot(d, d) - along * along))
            turning = any(a['rate']) or any(b['rate'])
            key = (a['order'], 'turning' if turning else 'straight')
            split[key]['along'].append(abs(along) / 500.0)
            split[key]['lat'].append(lat / 500.0)
            dr = [abs(w[i] - b['rate'][i]) for i in range(3)]
            if a['order'] == b['order'] and max(dr) > 0.5:
                i = dr.index(max(dr))
                big = 'big' if dr[i] > 1000 else 'small'
                # does the actual rate equal the controller's output for e = 0 (the remainder cleared) or for the actual e(n+1) + turned?
                acc = R.axis_acc(a['W'], a['rot'], a['rot_pct'])
                _, w0, _ = R.integrate_axis(0, a['rate'][i], a['W'][i], acc[i], a['dt_ms'])
                rc[(a['order'], ('yaw', 'pitch', 'roll')[i], big, 'leaf' if a['e'] is not None else 'carried',
                    'e=0' if e[i] == 0 else ('e*w<0' if e[i] * a['rate'][i] < 0 else 'e*w>=0'),
                    'actual==ctl(e=0)' if w0 == b['rate'][i] else 'actual!=ctl(e=0)')] += 1
                if len(rc_examples) < 6 and big == 'small':
                    rc_examples.append((a['frame'], a['class'], i, e[i], a['rate'][i], w[i], b['rate'][i], a['W'][i], acc[i], a['dt_ms'],
                                        b['e'][i] if b['e'] else None))
    print('pos_c split (m): key steps along_p50 along_p95 lat_p50 lat_p95')
    for k in sorted(split):
        s = split[k]
        print('  order=%d %-8s %6d  %.4f %.4f  %.4f %.4f' % (k[0], k[1], len(s['along']), pct(s['along'], 50), pct(s['along'], 95),
                                                            pct(s['lat'], 50), pct(s['lat'], 95)))
    print('rate-controller steps (order, worst axis, size, e source, e/w sign, e=0 hypothesis): count')
    for k, c in rc.most_common():
        print('  %s %d' % (k, c))
    print('examples (frame class axis e w pred actual W acc dt e_next):')
    for x in rc_examples:
        print('  ', x)


if __name__ == '__main__':
    main(sys.argv[1])
