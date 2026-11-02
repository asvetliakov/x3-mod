#!/usr/bin/env python3
"""FL run 18: dt distribution, per-second FPS timeline, drop episodes. Usage: python3 -I episodes.py frames.pkl"""
import pickle, sys, statistics as st
D = pickle.load(open(sys.argv[1], 'rb'))
F = D['frames']
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {}
for a, b in zip(ns, ns[1:]):
    if b == a + 1:
        dt[b] = (F[b]['qpc'] - F[a]['qpc']) / 1e4  # ms
vals = sorted(dt.values())
q = lambda p: vals[int(p * (len(vals) - 1))]
print('frames', len(vals), 'dt_ms p50 %.2f p90 %.2f p99 %.2f p999 %.2f max %.1f' % (q(.5), q(.9), q(.99), q(.999), vals[-1]))
t0 = F[ns[0]]['qpc']
sec = {}
for n, d in dt.items():
    s = int((F[n]['qpc'] - t0) / 1e7)
    sec.setdefault(s, []).append((n, d))
fps = {s: len(v) / (sum(d for _, d in v) / 1e3) for s, v in sec.items()}
mode = sys.argv[2] if len(sys.argv) > 2 else 'timeline'
if mode == 'timeline':
    for s in sorted(sec):
        v = sec[s]
        print('%4d f%6d-%6d fps %6.1f max_dt %7.1f' % (s, v[0][0], v[-1][0], fps[s], max(d for _, d in v)))
