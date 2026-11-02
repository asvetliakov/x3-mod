#!/usr/bin/env python3
"""FL run 18 per-frame recogniser and provider call counts (perf mitigations, docs/verification/farnhams-legacy.md,
"FL run 18 performance"). Streams the log once and prints p50/p90/p99/max of engine_frame candidates and not_jet (frames
that carry an engine_frame row), and the per-frame means of the fl.trace / fl.lifetime_sample current_calls and the
engine_memory reads/queries (per-row deltas divided by the row's frame span where the row carries one).
Usage: python3 candidate_counts.py <x3m.log>
"""
import re
import sys

FRAMES = 91283  # Presents in run 18 (the last engine_memory summary row's frame=; the triage summary's 91,287 engine_frame frames)

FIELD = re.compile(r'(\w+)=(\S+)')


def pct(values, q):
    if not values:
        return float('nan')
    s = sorted(values)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def main():
    cand, notjet, frames = [], [], set()
    last_memory = None
    sums = {}
    with open(sys.argv[1], 'rb') as f:
        for raw in f:
            if raw.startswith(b'engine_frame '):
                d = dict(FIELD.findall(raw.decode('latin-1')))
                cand.append(int(d['candidates']))
                notjet.append(int(d['not_jet']))
            elif raw.startswith(b'engine_memory '):
                last_memory = raw.decode('latin-1')
            elif raw.startswith((b'fl.trace_sample', b'fl.lifetime_sample')):
                head = raw.split(b' ', 1)[0].decode()
                d = dict(FIELD.findall(raw.decode('latin-1')))
                acc = sums.setdefault(head, {})
                for k, v in d.items():
                    try:
                        acc[k] = acc.get(k, 0) + float(v)
                    except ValueError:
                        pass
                acc['_rows'] = acc.get('_rows', 0) + 1

    print('engine_frame rows=%d' % len(cand))
    for name, v in (('candidates', cand), ('not_jet', notjet)):
        print('%s p50=%d p90=%d p99=%d max=%d mean=%.1f' % (name, pct(v, .5), pct(v, .9), pct(v, .99), max(v), sum(v) / len(v)))
    for head, acc in sums.items():
        if head == 'engine_memory':
            continue  # cumulative counters: the last row below
        keys = [k for k in ('_rows', 'current_calls', 'current_scoped', 'current', 'known') if k in acc]
        print(head, 'sum', ' '.join('%s=%.0f' % (k, acc[k]) for k in keys),
              'per_frame', ' '.join('%s=%.1f' % (k, acc[k] / FRAMES) for k in keys if k != '_rows'))
    if last_memory:
        d = dict(FIELD.findall(last_memory))
        print('engine_memory last reads=%s queries=%s per_frame reads=%.0f queries=%.1f' % (
            d['reads'], d['queries'], int(d['reads']) / FRAMES, int(d['queries']) / FRAMES))


if __name__ == '__main__':
    main()
