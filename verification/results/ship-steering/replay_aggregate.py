"""Aggregate a ship_trace_replay.py --json output over every ship: residual percentiles, step and window classes,
per order, and the dt convention. Usage: python3 -I replay_aggregate.py out.json"""
import collections
import json
import sys


def pct(v, p):
    v = sorted(v)
    return v[int((len(v) - 1) * p / 100)] if v else float('nan')


def main(path):
    d = json.load(open(path))
    ships = d['summary']['ships']
    steps = [s for v in d['steps'].values() for s in v]
    print('source', d['summary']['source'], 'dt_source', d['summary']['dt_source'])
    print('ships', len(ships), 'rows', sum(s['rows'] for s in ships), 'steps', len(steps),
          'skipped_gap', sum(s['skipped']['gap'] for s in ships), 'skipped_no_e', sum(s['skipped']['no_e'] for s in ships))
    print('dt_vs_t_ms', {k: sum(s['dt_vs_t_ms'][k] for s in ships) for k in ('row', 'next', 'neither')})
    print('ship classes', dict(collections.Counter(s['class'] for s in ships)))
    for k in ('speed', 'heading', 'pos', 'pos_c'):
        v = [s[k] for s in steps]
        print('%-8s p50 %.4f p95 %.4f p99 %.4f max %.3f' % (k, pct(v, 50), pct(v, 95), pct(v, 99), max(v)))
    for i, a in enumerate(('yaw', 'pitch', 'roll')):
        v = [s['rate'][i] for s in steps]
        print('rate_%-5s p50 %.1f p95 %.1f p99 %.1f max %d' % (a, pct(v, 50), pct(v, 95), pct(v, 99), max(v)))
    print('step classes', dict(collections.Counter(s['class'] for s in steps)))
    print('window classes', {c: sum(s['window_classes'][c] for s in ships) for c in ships[0]['window_classes']})
    for o in sorted({s['order'] for s in steps}):
        so = [s for s in steps if s['order'] == o]
        print('order %d steps %d divergent %d classes %s' % (o, len(so), sum(1 for s in so if s['class']),
                                                           dict(collections.Counter(s['class'] for s in so if s['class']))))


if __name__ == '__main__':
    main(sys.argv[1])
