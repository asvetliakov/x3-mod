#!/usr/bin/env python3
"""FL run 18: per-field median in fast seconds (fps >= 230) vs slow seconds (fps < 100) and Pearson r with dt.
Usage: python3 -I correlate.py frames.pkl [min_abs_delta_us]"""
import pickle, sys, math
D = pickle.load(open(sys.argv[1], 'rb')); F = D['frames']
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
t0 = F[ns[0]]['qpc']; sec = {}
for n, d in dt.items(): sec.setdefault(int((F[n]['qpc'] - t0) / 1e7), []).append(n)
fps = {s: len(v) / (sum(dt[n] for n in v) / 1e3) for s, v in sec.items()}
fast = [n for s, v in sec.items() if fps[s] >= 230 for n in v]
slow = [n for s, v in sec.items() if 20 <= fps[s] < 100 for n in v]
keys = set()
for n in dt: keys.update(k for k in F[n] if k not in ('qpc', 'elapsed_ms', 'capture'))
def med(ns_, k):
    v = sorted(F[n].get(k, 0.0) for n in ns_); return v[len(v) // 2]
def mean(ns_, k): return sum(F[n].get(k, 0.0) for n in ns_) / len(ns_)
def corr(k):
    xs = [F[n].get(k, 0.0) for n in dt]; ys = [dt[n] for n in dt]
    mx = sum(xs) / len(xs); my = sum(ys) / len(ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys)); sxx = sum((x - mx) ** 2 for x in xs); syy = sum((y - my) ** 2 for y in ys)
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else 0.0
print('fast frames', len(fast), 'slow frames', len(slow), 'dt mean fast %.2f slow %.2f ms' % (sum(dt[n] for n in fast) / len(fast), sum(dt[n] for n in slow) / len(slow)))
rows = []
for k in keys:
    cnt = sum(1 for n in dt if k in F[n])
    if cnt < 1000: continue
    rows.append((mean(slow, k) - mean(fast, k), k, med(fast, k), med(slow, k), mean(fast, k), mean(slow, k), corr(k), cnt))
rows.sort(key=lambda r: -abs(r[0]))
print('%-55s %10s %10s %10s %10s %6s %6s' % ('field', 'p50_fast', 'p50_slow', 'mean_fast', 'mean_slow', 'r_dt', 'rows'))
for r in rows[:int(sys.argv[2]) if len(sys.argv) > 2 else 45]:
    print('%-55s %10.1f %10.1f %10.1f %10.1f %6.2f %6d' % (r[1], r[2], r[3], r[4], r[5], r[6], r[7]))
