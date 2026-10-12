"""Test the order-5 remainder writers and the slip law of ship-movement.md section 13 against a ship_trace log.

Reads `ship_trace` rows (a trace file or a whole x3m.log, streamed through ship_trace_replay.iter_rows) and, for
each pair of consecutive frames (n, n+1) of one ship, reports:
  A. order-5 steps per target class and leaf flag (row n has e / e=-);
  B2. on port legs: toggles of the match condition between consecutive leaf frames, roll-rate cuts to 0 in one frame
     (condition false, the module's e_roll = 0 meets the e == 0 rule) and roll starts (condition true);
  B. roll: the replay model's predicted roll rate (remainder = the module's written e_roll) against the actual one;
     a divergence is |pred - actual| > 0.5 binary/s. Cross-tabulated with the port-roll-match condition of
     0x00454760..0x00454795 (target class != 0x12 and |e_yaw|, |e_pitch|, |e_roll| < 0x51e as written), and with
     the engine's e == 0 rule (0x00459a03/0x00459a99: rate := 0 when the remainder is 0) for the steps where the
     condition fails; for a divergent step under the condition, whether the actual roll rate is the controller's
     output for some remainder |e| <= 0x8000 (the match's roll error is not in the row);
  C. e=- rows on order 5 (no leaf call: the final docking zone 0x004543da..0x00454647): v, des and the magnitude of
     rate(n+1) read as the signed Euler angles of the per-frame step (0x0045463f) against the step cap
     0x38e or 0x71c binary/s * dt / 1000 (0x004542b7, 0x004542e4, 0x00454587..0x004545a9);
  D. the slip law of the integrator (0x0045a5d4..0x0045a778): slip += v_old - (v_old . fwd') fwd' when the order is
     not 5 and v' != 0 (v_old = fwd(n) * v(n), fwd' = fwd(n+1), v' = v(n+1)), pos(n+1) = pos(n) + (slip + fwd' v')
     dt / 1000, then slip *= (2000 - dt) / 2000; the conditional position residual (lateral to fwd', m) with and
     without the slip term, per order, turning steps (rate != 0 at n or n+1) and straight ones; slip starts at 0 at
     the first row of each contiguous run, so the first 3 s of a run are reported separately;
  D2. the modelled |slip| relative to the speed on the same steps;
  E. order 8 (0x004551fe): the displacement from the first order-8 row against s(u) = u^2 * 1665000 / 1e6 (+ v0 u /
     1000, the P5 term, v0 = the speed of the row before), u = t - t(first row), and that v, des and fwd stay put;
  F. the integrator's e == 0 rule (0x00459a03/0x00459b1d/0x00459c3b: the rate is set to 0, not braked) on every
     leaf row with a zero written remainder and a non-zero rate, against the replay model (which brakes).
Usage: python3 -I order5_trace_check.py /mnt/c/X3/x3m.log"""
import collections
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_replay as R  # noqa: E402

MATCH = 0x51e


def pct(v, p):
    v = sorted(v)
    return v[int((len(v) - 1) * p / 100)] if v else float('nan')


def wrap(x):
    x &= 0xffff
    return x - 0x10000 if x >= 0x8000 else x


def feasible_e(a, b, dt):
    # is the actual roll rate(n+1) the controller output for some remainder e (|e| <= 0x8000)?
    acc = R.axis_acc(a['W'], a['rot'], a['rot_pct'])[2]
    w, W, target = a['rate'][2], a['W'][2], b['rate'][2]
    for sgn in (1, -1):
        lo, hi = 1, 0x8000
        while lo < hi:  # smallest |e| whose output (sign-normalised) reaches the target
            mid = (lo + hi) // 2
            out = R.integrate_axis(sgn * mid, w, W, acc, dt)[1] * sgn
            if out >= target * sgn:
                hi = mid
            else:
                lo = mid + 1
        for e in range(max(1, lo - 2), lo + 3):
            if R.integrate_axis(sgn * e, w, W, acc, dt)[1] == target:
                return True
    return False


def lateral(d, f):
    a = R._dot(d, f)
    return math.sqrt(max(0.0, R._dot(d, d) - a * a))


def main(path):
    groups = collections.OrderedDict()
    for r in R.iter_rows(path):
        groups.setdefault(r['obj'], []).append(r)
    steps5 = collections.Counter()
    roll = collections.Counter()
    yp_carried = collections.Counter()
    fz = collections.Counter()
    pos = collections.defaultdict(lambda: {'no': [], 'slip': []})
    smag = collections.defaultdict(list)
    toggles = collections.Counter()
    launch = collections.Counter()
    zero_rule = collections.Counter()
    for rows in groups.values():
        for i, r in enumerate(rows):  # E: order-8 launch law
            if r['order'] == 8 and (i == 0 or rows[i - 1]['order'] != 8):
                v0 = rows[i - 1]['v'] if i else 0
                j = i
                while j < len(rows) and rows[j]['order'] == 8:
                    q = rows[j]
                    u = q['t_ms'] - r['t_ms']
                    s_pred = (u * u * 1665000) // 1000000 + v0 * u // 1000
                    d = math.sqrt(sum((q['pos'][k] - r['pos'][k]) ** 2 for k in range(3)))
                    launch['rows'] += 1
                    launch['|disp - s(u)| <= 1 + s/65536 (16.16 unit axis)' if abs(d - s_pred) <= 1 + s_pred / 65536.0
                           else '|disp - s(u)| > 1 + s/65536'] += 1
                    launch['disp == fx(0xffff, s(u)) +-1' if abs(d - ((s_pred * 0xffff + 0x8000) >> 16)) <= 1
                           else 'disp != fx(0xffff, s(u))'] += 1
                    launch['v == 0 and des == 0' if q['v'] == 0 and q['des'] == 0 else 'v or des != 0'] += 1
                    launch['fwd unchanged' if q['fwd'] == r['fwd'] else 'fwd changed'] += 1
                    launch['v0 (speed before) == 0' if v0 == 0 else 'v0 != 0'] += 1
                    j += 1
        carried = None
        last_cond = None
        slip = [0.0, 0.0, 0.0]
        run_t0 = rows[0]['t_ms']
        for n in range(len(rows) - 1):
            a, b = rows[n], rows[n + 1]
            if b['frame'] != a['frame'] + 1:
                carried = None
                slip = [0.0, 0.0, 0.0]
                run_t0 = b['t_ms']
                continue
            dt = a['dt_ms']
            # D: slip
            f0, f1 = R._norm(a['fwd']), R._norm(b['fwd'])
            vold = [f0[i] * a['v'] for i in range(3)]
            if a['order'] != 5 and b['v'] != 0:
                k = R._dot(vold, f1)
                slip = [slip[i] + vold[i] - k * f1[i] for i in range(3)]
            base = [a['pos'][i] + f1[i] * float(b['v']) * dt / 1000.0 for i in range(3)]
            d_no = [b['pos'][i] - base[i] for i in range(3)]
            d_sl = [d_no[i] - slip[i] * dt / 1000.0 for i in range(3)]
            slip = [s * (2000 - dt) / 2000.0 for s in slip]
            if a['order'] == 5 and a['v'] == 0 and b['v'] == 0:
                slip = [0.0, 0.0, 0.0]  # final zone zeroes slip (0x00454402)
            turning = any(a['rate']) or any(b['rate'])
            warm = (a['t_ms'] - run_t0) >= 3000
            if a['order'] == b['order']:
                kind = 'turning' if turning else 'straight'
                if a['order'] == 5 and a['v'] == 0 and b['v'] == 0:
                    kind = 'v=0'  # final zone: kinematic glide in pass A (0x004544f3), rates hold the per-frame step
                key = (a['order'], kind, 'warm' if warm else 'first3s')
                pos[key]['no'].append(lateral(d_no, f1) / 500.0)
                pos[key]['slip'].append(lateral(d_sl, f1) / 500.0)
                if b['v'] > 0:
                    smag[key].append(math.sqrt(R._dot(slip, slip)) / (2000.0 - dt) * 2000.0 / b['v'])
            # model prediction (as the replay)
            e = a['e'] if a['e'] is not None else carried
            if e is None:
                continue
            w, e_new, v, brows, p = R.step(a, e, dt)
            carried = tuple(e_new)
            # F: the e == 0 rule on leaf rows, all orders (port-match frames excluded: the engine rewrote the roll)
            if a['e'] is not None and a['order'] == b['order'] and not (
                    a['order'] == 5 and a['tgt_class'] != 0x12 and all(abs(x) < MATCH for x in a['e'])):
                for i in range(3):
                    if a['e'][i] == 0 and a['rate'][i] != 0:
                        zero_rule[('actual rate(n+1) == 0' if b['rate'][i] == 0 else 'actual rate(n+1) != 0',
                                   'model (brakes) == 0' if w[i] == 0 else 'model (brakes) != 0')] += 1
            if not (a['order'] == 5 and b['order'] == 5):
                continue
            gate = a['tgt_class'] == 0x12
            leaf = a['e'] is not None
            steps5[('gate' if gate else 'tgt_class=%d' % a['tgt_class'], 'leaf' if leaf else 'e=-')] += 1
            # B: roll
            if leaf:
                cond = (not gate) and all(abs(x) < MATCH for x in a['e'])
                if not gate:
                    if last_cond is not None and last_cond[0] == a['frame'] - 1 and last_cond[1] != cond:
                        toggles['cond toggles (consecutive port leaf frames)'] += 1
                    last_cond = (a['frame'], cond)
                    if not cond and a['rate'][2] != 0 and b['rate'][2] == 0:
                        toggles['roll rate cut to 0 in one frame (condition false, |w| > 0)'] += 1
                    if cond and a['rate'][2] == 0 and b['rate'][2] != 0:
                        toggles['roll starts (condition true, w = 0 -> w != 0)'] += 1
                div = abs(w[2] - b['rate'][2]) > 0.5
                tag = ['gate' if gate else 'port', 'match_cond' if cond else 'no_match_cond', 'roll_div' if div else 'roll_ok']
                if cond and div:
                    tag.append('feasible(some e)' if feasible_e(a, b, dt) else 'infeasible')
                if cond and not div:
                    tag.append('rates 0->0' if a['rate'][2] == 0 and b['rate'][2] == 0 else 'rate moving')
                if not cond:
                    tag.append('actual_roll_rate==0' if b['rate'][2] == 0 else 'actual_roll_rate!=0')
                    tag.append('e_roll==0' if a['e'][2] == 0 else 'e_roll!=0')
                roll[tuple(tag)] += 1
            else:
                # C: final zone rows
                sw = [wrap(x) for x in b['rate']]
                mag = math.sqrt(sum(x * x for x in sw))
                step_lo, step_hi = 0x38e * b['dt_ms'] / 1000.0, 0x71c * b['dt_ms'] / 1000.0
                kind = ('0' if mag == 0 else '~0x38e*dt/1000' if abs(mag - step_lo) <= 2.5 else
                        '~0x71c*dt/1000' if abs(mag - step_hi) <= 2.5 else '<cap*dt/1000' if mag < step_hi else '>0x71c*dt/1000')
                fz[('v==0' if a['v'] == 0 else 'v!=0', 'des==0' if a['des'] == 0 else 'des!=0', b['class'],
                    '|wrapped rate(n+1)| ' + kind)] += 1
                if any(abs(w[i] - b['rate'][i]) > 0.5 for i in (0, 1)):
                    yp_carried[('gate' if gate else 'port', 'v==0' if a['v'] == 0 else 'v!=0')] += 1
    print('source %s: %d ship_trace rows, %d ships' % (path, sum(len(r) for r in groups.values()), len(groups)))
    print('A. order-5 steps (target, leaf flag): count')
    for k, c in sorted(steps5.items()):
        print('  %s %d' % (k, c))
    print('B. order-5 leaf steps, roll (target, port-roll-match condition, divergence[, e=0 rule]): count')
    for k, c in sorted(roll.items()):
        print('  %s %d' % (k, c))
    print('B2. port-roll-match dynamics on port legs: count')
    for k, c in sorted(toggles.items()):
        print('  %s %d' % (k, c))
    print('C. order-5 e=- rows (v, des, class, |rate(n+1)| as signed per-frame Euler step vs 0x38e/0x71c * dt(n+1)/1000 +-2.5): count')
    for k, c in sorted(fz.items()):
        print('  %s %d' % (k, c))
    print('   yaw/pitch divergent e=- steps (target, v): count')
    for k, c in sorted(yp_carried.items()):
        print('  %s %d' % (k, c))
    print('D. lateral pos_c (m) without / with the slip term: order kind run steps | p50 p95 p99 | p50 p95 p99')
    for k in sorted(pos):
        s = pos[k]
        print('  order=%d %-8s %-7s %6d | %.3f %.3f %.3f | %.3f %.3f %.3f' % (
            k[0], k[1], k[2], len(s['no']), pct(s['no'], 50), pct(s['no'], 95), pct(s['no'], 99),
            pct(s['slip'], 50), pct(s['slip'], 95), pct(s['slip'], 99)))

    print('D2. modelled |slip| / v (before the decay of the step): order kind run steps p50 p95 max')
    for k in sorted(smag):
        v = smag[k]
        print('  order=%d %-8s %-7s %6d  %.3f %.3f %.3f' % (k[0], k[1], k[2], len(v), pct(v, 50), pct(v, 95), max(v)))

    print('E. order-8 rows: s(u) = trunc(u^2 * 1665000 / 1e6) + v0 u / 1000, u = t - t(first order-8 row): count')
    for k, c in sorted(launch.items()):
        print('  %s %d' % (k, c))

    print('F. leaf rows with e_axis == 0 and rate_axis != 0 (port-match frames excluded), all orders: count')
    for k, c in sorted(zero_rule.items()):
        print('  %s %d' % (k, c))


if __name__ == '__main__':
    main(sys.argv[1])
