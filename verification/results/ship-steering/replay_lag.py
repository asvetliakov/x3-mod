"""The velocity-vs-nose lag of a flight trace (amendment 14): ship_trace_replay.py's per-step lag_obs (the angle between a
step's displacement and fwd(n+1), deg) pooled over every followed ship, per order and per ship class, with the replay's
flown-minus-modelled difference (the slip model's fit) and, for rows that carry slip=, the carried slip's distance to it.
Usage: python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --json out.json > /dev/null
       python3 -I verification/results/ship-steering/replay_lag.py out.json"""
import json
import sys


def pct(v, p):
    if not v:
        return None
    s = sorted(v)
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def line(label, v):
    if not v:
        return '%-22s n=0' % label
    return '%-22s n=%7d p50=%6.2f p95=%6.2f p99=%6.2f max=%6.2f' % (label, len(v), pct(v, 50), pct(v, 95), pct(v, 99), max(v))


def main(path):
    d = json.load(open(path))
    cls = {s['obj']: s['class'] for s in d['summary']['ships']}
    by_order, by_cls, allv, diff, slip_res = {}, {}, [], [], []
    for obj, steps in d['steps'].items():
        for s in steps:
            v = s.get('lag_obs')
            if v is None:
                continue
            allv.append(v)
            by_order.setdefault('0x%x' % s['order'], []).append(v)
            by_cls.setdefault(cls[obj], []).append(v)
            if s.get('lag_model') is not None:
                diff.append(abs(v - s['lag_model']))
            if s.get('slip_res') is not None:
                slip_res.append(s['slip_res'])
    print('source %s ships=%d' % (d['summary']['source'], len(cls)))
    print(line('lag all', allv))
    for o in sorted(by_order, key=lambda x: int(x, 16)):
        print(line('lag order ' + o, by_order[o]))
    for c in sorted(by_cls):
        print(line('lag class ' + c, by_cls[c]))
    print(line('|flown - model| deg', diff))
    print(line('slip_res m/s', slip_res))


if __name__ == '__main__':
    main(sys.argv[1])
