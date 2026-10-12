"""Per-class residuals of a ship_trace_replay.py --json output (the calibration table of the ledger's "Replay
conformance"): pos_c along / lateral to fwd(n+1) per order and turning flag, divergent steps per order and class, the
unmodelled steps by kind, the rate-controller steps by axis and leaf flag, the final-zone checks (step magnitude, the
Euler step applied to the previous basis, the glide speed) and the order-8 position residuals.
Usage: python3 -I replay_classes.py out.json [warm_ms, default 3000] (a pre-calibration JSON lacks the split and the unmodelled tags)"""
import collections
import json
import sys


def pct(v, p):
    v = sorted(v)
    return v[int((len(v) - 1) * p / 100)] if v else float('nan')


def main(path, warm_ms=3000):
    d = json.load(open(path))
    steps = [s for v in d['steps'].values() for s in v]
    print('source', d['summary']['source'], 'steps', len(steps))
    split = collections.defaultdict(lambda: {'along': [], 'lat': []})
    for s in steps:
        if s.get('pos_c_lat') is None or s['boundary']:
            continue
        key = (s['order'], 'turning' if s['turning'] else 'straight', 'warm' if s.get('run_ms', 1e9) >= warm_ms else 'early')
        split[key]['along'].append(s['pos_c_along'])
        split[key]['lat'].append(s['pos_c_lat'])
    print('pos_c (m, non-boundary steps; warm = warm_ms or more after the first row of the run, where the slip starts at 0): order kind run steps | '
          'along p50 p95 p99 | lateral p50 p95 p99 max')
    for k in sorted(split):
        a, lat = split[k]['along'], split[k]['lat']
        print('  order=%d %-8s %-7s %6d | %.4f %.4f %.4f | %.4f %.4f %.4f %.3f' % (k[0], k[1], k[2], len(a), pct(a, 50), pct(a, 95), pct(a, 99),
                                                                              pct(lat, 50), pct(lat, 95), pct(lat, 99), max(lat)))
    print('divergent steps: order -> class counts')
    for o in sorted({s['order'] for s in steps}):
        so = [s for s in steps if s['order'] == o]
        print('  order %d steps %d divergent %d %s' % (o, len(so), sum(1 for s in so if s['class']),
                                                     dict(collections.Counter(s['class'] for s in so if s['class']))))
    print('all classes', dict(collections.Counter(s['class'] for s in steps if s['class'])))
    print('unmodelled steps', dict(collections.Counter(s.get('unmodelled') for s in steps if s.get('unmodelled'))))
    rc = collections.Counter()
    for s in steps:
        if s['class'] == 'rate controller':
            i = max(range(3), key=lambda k: s['rate'][k])
            rc[(s['order'], ('yaw', 'pitch', 'roll')[i], 'leaf' if s['leaf_call'] else 'carried',
                'small' if s['rate'][i] <= 1000 else 'big')] += 1
    print('rate-controller steps (order, worst axis, e source, size):', dict(rc))
    rb = collections.Counter()
    for s in steps:
        if s['class'] == 'rate controller':
            m = max(s['rate'])
            rb[(s['order'], '<=1' if m <= 1 else '<=3' if m <= 3 else '>3')] += 1
    print('rate-controller steps by order and worst residual (binary/s):', dict(sorted(rb.items())))
    pb = collections.Counter()
    for s in steps:
        if s['class'] == 'position':
            pb[(s['order'], 'early' if s.get('run_ms', 1e9) < warm_ms else 'warm', '<=0.02m' if s['pos_c'] <= 0.02 else '>0.02m')] += 1
    print('position steps by order, run age and pos_c:', dict(sorted(pb.items())))
    sb = collections.Counter()
    for s in steps:
        if s['class'] == 'speed law' and 'v_obs' in s:
            v, des, v1 = s['v_obs']
            sb[(s['order'], 'held below des' if v1 == v and v < des else 'other')] += 1
    print('speed-law steps (order, actual):', dict(sorted(sb.items())))
    fz = [s for s in steps if s.get('unmodelled') == 'final zone']
    if fz:
        print('final zone steps %d: rate magnitude off %d; Euler-step heading p50 %.4f p95 %.4f max %.3f deg; glide m/s p50 %.2f max %.2f' % (
            len(fz), sum(1 for s in fz if s['rate'][0] > 0), pct([s['fz_euler_heading'] for s in fz], 50),
            pct([s['fz_euler_heading'] for s in fz], 95), max(s['fz_euler_heading'] for s in fz),
            pct([s['fz_glide_ms'] for s in fz if s['fz_glide_ms'] is not None], 50),
            max(s['fz_glide_ms'] for s in fz if s['fz_glide_ms'] is not None)))
    o8 = [s for s in steps if s['order'] == 8 and not s['boundary']]
    if o8:
        print('order 8 (non-boundary) steps %d: pos p50 %.4f max %.4f m; launch-axis (magnitude only) %d' % (
            len(o8), pct([s['pos'] for s in o8], 50), max(s['pos'] for s in o8), sum(1 for s in o8 if s.get('unmodelled') == 'launch axis')))
    worst = sorted((s for s in steps if s['class'] and s['class'] != 'order boundary'), key=lambda s: -(s['pos_c'] or 0))[:5]
    print('worst non-boundary pos_c steps (frame, order, class, pos_c m, rates):',
          [(s['frame'], s['order'], s['class'], round(s['pos_c'] or 0, 3), s['rate']) for s in worst])


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3000)
