"""Replay conformance of the ship steering fixture's engine model against ship_trace rows.

For every followed ship and every pair of consecutive frames (n, n+1) of a trace (a file of `ship_trace` rows or a
whole x3m.log, streamed), frame n's inputs (pos, fwd, up, rate, v, des, e, W, rot, rot_pct, vmax, acc, dt_ms) go
through a Python port of the engine model of verification/probe/ship_steering_host.cpp (`controller`,
`integrate_axis`, `Model::advance`, `Model::rotate`: the per-axis controller 0x0043ad20 in 16.16 integer arithmetic,
the slew of the speed toward the desired speed at acc*dt/1000 with the 1-unit minimum, the rotation of the flight
basis by the turned angles, position += row2 * speed * dt / 1000) and the prediction of frame n+1's rate, v, fwd and
pos is compared with the actual row n+1.

Residuals per step: rate (per axis, binary angle/s), speed (m/s), heading (deg between predicted and actual fwd),
position (m), and the conditional position residual pos_c (m): pos(n) + fwd(n+1) * v(n+1) * dt / 1000 against
pos(n+1), i.e. the translation law alone with the actual heading and speed (non-zero for a unit, dt or slip error).

A divergent step (any residual over its tolerance) is classified, first match wins:
  order boundary   the order differs between n-1, n or n+1
  position         pos_c over tolerance (the translation law: units, dt or the slip term)
  rate controller  a rate residual
  orientation      a heading residual while the rate matches (a sign or axis convention)
  speed law        a speed residual while the rate matches
  position         the remaining position residual
Consecutive divergent steps form a window; a window is "order boundary" when all its steps are, otherwise the most
frequent class of its other steps.

e: the row's written remainders when the module called the leaf; on an `e=-` row the remainder the model carried
from the previous step (the integrator consumes obj+0x30 across frames); a step with no known remainder is skipped.
The angular acceleration is acc_i = fx(W_i, fx(rot, rot_pct * 0x10000 / 100)) (ship-movement.md section 3: k =
f+0x2b0 x record+0xd0 / 100; the fixture's rot is 1.0, so this reduces to its fx(W, k_pct * 0x10000 / 100); the
engine's exact operation order for rot != 1.0 is [i]).

  python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --json out.json
  python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --gate
"""
import argparse
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ship_trace_rows as st  # noqa: E402

# core::kRadPerBinary = float(6.28318530717959f) / 65536.0f (src/proxy/ship_steering_core.h), as the fixture uses it
RAD_PER_BINARY = struct.unpack('f', struct.pack('f', 6.28318530717959))[0] / 65536.0
UNITS_PER_M = 500.0
CLASSES = ('order boundary', 'position', 'rate controller', 'orientation', 'speed law')
TOL = {'rate': 0.5, 'speed': 0.002, 'heading': 0.01, 'pos': 0.01, 'pos_c': 0.01}  # binary/s, m/s, deg, m, m


# ---- the port of ship_steering_host.cpp (same names, same integer arithmetic) ----
def fx(a, b):
    """(a*b + 0x8000) >> 16, arithmetic shift."""
    return (a * b + 0x8000) >> 16


def div16(a, b):
    """(a << 16) / b truncating toward zero."""
    if b == 0:
        return 0
    q = abs(a << 16) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def tdiv(a, b):
    """C integer division (truncating toward zero)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def controller(e, w, wmax, acc, dt_ms):
    """0x0043ad20: (new rate, angle turned) for remaining e >= 0, rate w, cap wmax, acceleration acc."""
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
    rest = e - d0 - dstop
    if rest < 0:
        rest = 0
    tr = div16(rest, nw) if nw else 0
    if t <= tr + ta:
        return nw, fx(nw, t - ta) + d0
    tb = t - tr - ta
    return nw - fx(tb, acc), fx(nw, tb) + rest + d0


def integrate_axis(e, w, wmax, acc, dt_ms):
    """The integrator's use of the controller: (e', w', turned binary). Sign-mirrored, delta clamped to |e|."""
    if e == 0:
        if w != 0:
            sw, _ = controller(0, -w if w < 0 else w, wmax, acc, dt_ms)
            w = -sw if w < 0 else sw
        return e, w, 0
    neg = e < 0
    sw, sd = controller(-e if neg else e, -w if neg else w, wmax, acc, dt_ms)
    mag = -e if neg else e
    if sd >= mag:
        sd = mag
        sw = 0
    e = e + sd if neg else e - sd
    w = -sw if neg else sw
    return e, w, (-sd if neg else sd)


def slew(speed, desired, vmax, accel, dt_ms):
    """Model::advance's speed slew (the max speed clamp; the script speed limit f+0x278 is not in the row)."""
    if desired > vmax:
        desired = vmax
    if speed != desired:
        step = tdiv(accel * dt_ms, 1000)
        if step < 1:
            step = 1
        if speed > desired:
            speed = desired if speed - step < desired else speed - step
        else:
            speed = desired if speed + step > desired else speed + step
    return speed


def _norm(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return [v[0] / n, v[1] / n, v[2] / n]


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def basis_from_row(fwd, up):
    """Rows 0, 1, 2 of the flight basis from the row's (rounded) fwd = row 2 and up = row 1; row 0 = row1 x row2
    (the fixture's right-handed basis)."""
    r2 = _norm(fwd)
    d = _dot(up, r2)
    r1 = _norm([up[i] - d * r2[i] for i in range(3)])
    return [_cross(r1, r2), r1, r2]


def rotate(rows, a):
    """Model::rotate: yaw row2 toward -row0, pitch row2 toward +row1, roll row0 toward row1, re-orthonormalised."""
    r0, r1, r2 = list(rows[0]), list(rows[1]), list(rows[2])
    c, s = math.cos(a[0]), math.sin(a[0])
    for i in range(3):
        n2, n0 = c * r2[i] - s * r0[i], c * r0[i] + s * r2[i]
        r2[i], r0[i] = n2, n0
    c, s = math.cos(a[1]), math.sin(a[1])
    for i in range(3):
        n2, n1 = c * r2[i] + s * r1[i], c * r1[i] - s * r2[i]
        r2[i], r1[i] = n2, n1
    c, s = math.cos(a[2]), math.sin(a[2])
    for i in range(3):
        n0, n1 = c * r0[i] + s * r1[i], c * r1[i] - s * r0[i]
        r0[i], r1[i] = n0, n1
    r2 = _norm(r2)
    d = _dot(r0, r2)
    r0 = _norm([r0[i] - d * r2[i] for i in range(3)])
    d, d1 = _dot(r1, r2), _dot(r1, r0)
    r1 = _norm([r1[i] - d * r2[i] - d1 * r0[i] for i in range(3)])
    return [r0, r1, r2]


def axis_acc(W, rot, rot_pct):
    k = fx(rot, tdiv(0x10000 * rot_pct, 100))
    return [fx(W[i], k) for i in range(3)]


def step(row, e, dt_ms):
    """One frame of the model from row n's state with remainders e: the predicted (rate, e', v, rows, pos)."""
    acc = axis_acc(row['W'], row['rot'], row['rot_pct'])
    v = slew(row['v'], row['des'], row['vmax'], row['acc'], dt_ms)
    w_new, e_new, turned = [], [], []
    for i in range(3):
        ei, wi, di = integrate_axis(e[i], row['rate'][i], row['W'][i], acc[i], dt_ms)
        e_new.append(ei)
        w_new.append(wi)
        turned.append(float(di) * RAD_PER_BINARY)
    rows = rotate(basis_from_row(row['fwd'], row['up']), turned)
    pos = [row['pos'][i] + rows[2][i] * float(v) * float(dt_ms) / 1000.0 for i in range(3)]
    return w_new, e_new, v, rows, pos


# ---- the replay ----
def iter_rows(path):
    """Stream the ship_trace rows of a file (a trace or a whole x3m.log)."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'ship_trace frame=' not in line:
                continue
            r = st.parse_row(line[line.index('ship_trace frame='):])
            if r is not None:
                yield r


def _angle_deg(a, b):
    a, b = _norm(a), _norm(b)
    c = max(-1.0, min(1.0, _dot(a, b)))
    return math.degrees(math.acos(c))


def classify(res, boundary, tol):
    if boundary:
        return 'order boundary'
    if res['pos_c'] > tol['pos_c']:
        return 'position'
    if max(res['rate']) > tol['rate']:
        return 'rate controller'
    if res['heading'] > tol['heading']:
        return 'orientation'
    if res['speed'] > tol['speed']:
        return 'speed law'
    return 'position'


def divergent(res, tol):
    return (max(res['rate']) > tol['rate'] or res['speed'] > tol['speed'] or res['heading'] > tol['heading']
            or res['pos'] > tol['pos'] or res['pos_c'] > tol['pos_c'])


def replay_ship(rows, tol=TOL, dt_source='row'):
    """The per-step residuals of one ship's rows (in order). dt_source 'row' = row n's dt_ms, 'next' = row n+1's."""
    steps, skipped = [], {'gap': 0, 'no_e': 0}
    dt_match = {'row': 0, 'next': 0, 'neither': 0}
    carried = None
    for n in range(len(rows) - 1):
        a, b = rows[n], rows[n + 1]
        if b['frame'] != a['frame'] + 1:
            skipped['gap'] += 1
            carried = None
            continue
        dt_t = b['t_ms'] - a['t_ms']
        dt_match['row' if dt_t == a['dt_ms'] else 'next' if dt_t == b['dt_ms'] else 'neither'] += 1
        e = a['e'] if a['e'] is not None else carried
        if e is None:
            skipped['no_e'] += 1
            continue
        dt = a['dt_ms'] if dt_source == 'row' else b['dt_ms']
        w, e_new, v, brows, pos = step(a, e, dt)
        carried = tuple(e_new)
        cond = [a['pos'][i] + _norm(b['fwd'])[i] * float(b['v']) * float(dt) / 1000.0 for i in range(3)]
        res = {
            'rate': [abs(w[i] - b['rate'][i]) for i in range(3)],
            'speed': abs(v - b['v']) / UNITS_PER_M,
            'heading': _angle_deg(brows[2], b['fwd']),
            'pos': math.dist(pos, b['pos']) / UNITS_PER_M,
            'pos_c': math.dist(cond, b['pos']) / UNITS_PER_M,
        }
        prev_order = rows[n - 1]['order'] if n > 0 and rows[n - 1]['frame'] == a['frame'] - 1 else a['order']
        boundary = a['order'] != b['order'] or prev_order != a['order']
        rec = {'frame': a['frame'], 't_ms': a['t_ms'], 'order': a['order'], 'dt_ms': dt, 'leaf_call': a['e'] is not None,
               'pred': {'rate': w, 'v': v, 'fwd': [round(x, 6) for x in brows[2]], 'pos': [round(x, 1) for x in pos]}}
        rec.update(res)
        rec['class'] = classify(res, boundary, tol) if divergent(res, tol) else None
        rec['boundary'] = boundary
        steps.append(rec)
    return steps, skipped, dt_match


def _pct(vals, p):
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo = int(math.floor(k))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def windows(steps):
    out, cur = [], None
    for s in steps:
        if s['class'] is None:
            cur = None
            continue
        if cur is not None and s['frame'] == cur['last'] + 1:
            cur['last'] = s['frame']
            cur['classes'].append(s['class'])
            continue
        cur = {'first': s['frame'], 'last': s['frame'], 'classes': [s['class']]}
        out.append(cur)
    result = []
    for wdw in out:
        other = [c for c in wdw['classes'] if c != 'order boundary']
        if not other:
            cls = 'order boundary'
        else:
            cls = max(CLASSES, key=lambda c: (other.count(c), -CLASSES.index(c)))
        result.append({'first': wdw['first'], 'last': wdw['last'], 'steps': len(wdw['classes']), 'class': cls})
    return result


def summarise(obj, rows, steps, skipped, dt_match, worst_n=5):
    q = {
        'rate_yaw': [s['rate'][0] for s in steps], 'rate_pitch': [s['rate'][1] for s in steps],
        'rate_roll': [s['rate'][2] for s in steps], 'speed': [s['speed'] for s in steps],
        'heading': [s['heading'] for s in steps], 'pos': [s['pos'] for s in steps], 'pos_c': [s['pos_c'] for s in steps],
    }
    stats = {k: {'p50': _pct(v, 50), 'p95': _pct(v, 95), 'max': max(v) if v else None} for k, v in q.items()}
    worst = {}
    for k, v in q.items():
        idx = sorted(range(len(v)), key=lambda i: -v[i])[:worst_n]
        worst[k] = [[steps[i]['frame'], v[i]] for i in idx if v[i] > 0]
    wins = windows(steps)
    counts = {c: 0 for c in CLASSES}
    for s in steps:
        if s['class']:
            counts[s['class']] += 1
    wcounts = {c: sum(1 for w in wins if w['class'] == c) for c in CLASSES}
    return {'obj': '0x%08x' % obj, 'class': rows[0]['class'], 'rows': len(rows), 'steps': len(steps), 'skipped': skipped,
            'dt_vs_t_ms': dt_match, 'divergent_steps': sum(counts.values()), 'step_classes': counts,
            'windows': len(wins), 'window_classes': wcounts, 'window_list': wins[:50], 'residuals': stats, 'worst_frames': worst,
            'orders': sorted({r['order'] for r in rows}), 'tgt_classes': sorted({r['tgt_class'] for r in rows})}


# ---- --gate ----
def gate_facts(rows):
    """Transition facts of a ship's gate legs (rows with tgt_class 0x12), per run of equal order."""
    g = [r for r in rows if r['tgt_class'] == 0x12]
    if not g:
        return None
    segs = []
    for r in g:
        if segs and segs[-1]['order'] == r['order'] and r['frame'] == segs[-1]['rows'][-1]['frame'] + 1:
            segs[-1]['rows'].append(r)
        else:
            segs.append({'order': r['order'], 'rows': [r]})

    def heading_err(r):
        d = [r['given'][i] - r['pos'][i] for i in range(3)]
        if math.sqrt(_dot(d, d)) < 1:
            return None
        return _angle_deg(r['fwd'], d)

    out, prev_given = [], None
    for s in segs:
        rs = s['rows']
        g0 = rs[0]['given']
        drift = max(math.dist(r['given'], g0) for r in rs) / UNITS_PER_M
        seg = {
            'order': s['order'], 'frame_first': rs[0]['frame'], 'frame_last': rs[-1]['frame'], 'frames': len(rs),
            'duration_ms': rs[-1]['t_ms'] - rs[0]['t_ms'] + rs[-1]['dt_ms'],
            'given_first': list(g0),
            'given_first_rel_prev_m': ([round((g0[i] - prev_given[i]) / UNITS_PER_M, 2) for i in range(3)] if prev_given else None),
            'given_drift_max_m': round(drift, 3),
            'v_first_ms': rs[0]['v'] / UNITS_PER_M, 'v_last_ms': rs[-1]['v'] / UNITS_PER_M,
            'v_min_ms': min(r['v'] for r in rs) / UNITS_PER_M,
            'dist_first_m': round(math.dist(rs[0]['given'], rs[0]['pos']) / UNITS_PER_M, 1),
            'heading_err_first_deg': heading_err(rs[0]), 'heading_err_last_deg': heading_err(rs[-1]),
        }
        out.append(seg)
        prev_given = g0
    o5 = [r['v'] for r in g if r['order'] == 5]
    return {'segments': out, 'order_sequence': [s['order'] for s in out],
            'min_v_order5_ms': (min(o5) / UNITS_PER_M) if o5 else None}


def run(path, obj=None, tol=TOL, dt_source='row', gate=False):
    groups = {}
    for r in iter_rows(path):
        if obj is not None and r['obj'] != obj:
            continue
        groups.setdefault(r['obj'], []).append(r)
    ships, per_step = [], {}
    for o, rows in groups.items():
        steps, skipped, dt_match = replay_ship(rows, tol, dt_source)
        summ = summarise(o, rows, steps, skipped, dt_match)
        if gate:
            summ['gate'] = gate_facts(rows)
        ships.append(summ)
        per_step['0x%08x' % o] = steps
    return {'source': path, 'dt_source': dt_source, 'tolerance': tol, 'ships': ships}, per_step


def _fmt(x, nd=3):
    return '-' if x is None else ('%.*f' % (nd, x))


def print_report(report, gate=False, out=sys.stdout):
    for s in report['ships']:
        print('ship %s %s rows=%d steps=%d skipped gap=%d no_e=%d dt(t_ms delta == row n %d, row n+1 %d, neither %d) '
              'divergent=%d windows=%d' % (s['obj'], s['class'], s['rows'], s['steps'], s['skipped']['gap'], s['skipped']['no_e'],
                                           s['dt_vs_t_ms']['row'], s['dt_vs_t_ms']['next'], s['dt_vs_t_ms']['neither'],
                                           s['divergent_steps'], s['windows']), file=out)
        for k, v in s['residuals'].items():
            print('  %-10s p50=%s p95=%s max=%s worst=%s' % (k, _fmt(v['p50'], 4), _fmt(v['p95'], 4), _fmt(v['max'], 4),
                                                           ','.join(str(f) for f, _ in s['worst_frames'][k])), file=out)
        print('  steps by class: ' + ' '.join('%s=%d' % (c.replace(' ', '_'), n) for c, n in s['step_classes'].items()), file=out)
        print('  windows by class: ' + ' '.join('%s=%d' % (c.replace(' ', '_'), n) for c, n in s['window_classes'].items()), file=out)
        if gate and s.get('gate'):
            gf = s['gate']
            print('  gate orders=%s min_v_order5=%s m/s' % ('/'.join(str(o) for o in gf['order_sequence']), _fmt(gf['min_v_order5_ms'], 1)),
                  file=out)
            for seg in gf['segments']:
                print('   order=%d frames=%d dur=%dms given_rel_prev=%s drift=%sm v first/last/min=%s/%s/%s dist_first=%sm '
                      'head_err first/last=%s/%s deg' % (seg['order'], seg['frames'], seg['duration_ms'], seg['given_first_rel_prev_m'],
                                                         _fmt(seg['given_drift_max_m'], 1), _fmt(seg['v_first_ms'], 1),
                                                         _fmt(seg['v_last_ms'], 1), _fmt(seg['v_min_ms'], 1), _fmt(seg['dist_first_m'], 1),
                                                         _fmt(seg['heading_err_first_deg'], 1), _fmt(seg['heading_err_last_deg'], 1)),
                      file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n\n', 1)[1])
    ap.add_argument('trace', help='a file of ship_trace rows or a whole x3m.log (streamed)')
    ap.add_argument('--json', help='write the summaries and the per-step residuals here')
    ap.add_argument('--obj', help='only this ship (0x...)')
    ap.add_argument('--gate', action='store_true', help='also print the gate transition facts of ships with a class-0x12 target')
    ap.add_argument('--dt-source', choices=('row', 'next'), default='row', help="row n's dt_ms (default) or row n+1's")
    for k, v in TOL.items():
        ap.add_argument('--tol-' + k.replace('_', '-'), type=float, default=v, help='tolerance (default %g)' % v)
    a = ap.parse_args(argv)
    tol = {k: getattr(a, 'tol_' + k) for k in TOL}
    report, per_step = run(a.trace, int(a.obj, 16) if a.obj else None, tol, a.dt_source, a.gate)
    print_report(report, a.gate)
    if a.json:
        with open(a.json, 'w') as f:
            json.dump({'summary': report, 'steps': per_step}, f)
    return 0


if __name__ == '__main__':
    sys.exit(main())
