#!/usr/bin/env python3
"""FL run 18: per-second least squares frame_ms ~ 1 + game draws + shadow replay depth draws + plume records (fog-off and all).
Usage: python3 -I multifit.py frames.pkl"""
import pickle, sys
F = pickle.load(open(sys.argv[1], 'rb'))['frames']
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
t0 = F[ns[0]]['qpc']; sec = {}
for n in dt: sec.setdefault(int((F[n]['qpc'] - t0) / 1e7), []).append(n)
X, Y = [], []
K = sys.argv[2].split(',') if len(sys.argv) > 2 else ['draws', 'shadow_replay_depth.draws', 'engine_stage.records']
for s, v in sec.items():
    ms = sum(dt[n] for n in v) / len(v)
    if ms > 50 or (len(sys.argv) > 3 and v[0] >= int(sys.argv[3])): continue
    X.append([1.0] + [sum(F[n].get(k, 0) for n in v) / len(v) for k in K]); Y.append(ms)
m = len(X[0])
A = [[sum(x[i] * x[j] for x in X) for j in range(m)] for i in range(m)]
bb = [sum(x[i] * y for x, y in zip(X, Y)) for i in range(m)]
for i in range(m):  # Gauss-Jordan
    p = A[i][i]
    for j in range(m): A[i][j] /= p
    bb[i] /= p
    for r in range(m):
        if r != i:
            f = A[r][i]
            for j in range(m): A[r][j] -= f * A[i][j]
            bb[r] -= f * bb[i]
pred = [sum(c * xi for c, xi in zip(bb, x)) for x in X]
my = sum(Y) / len(Y)
r2 = 1 - sum((y - p) ** 2 for y, p in zip(Y, pred)) / sum((y - my) ** 2 for y in Y)
print('fog-off seconds', len(X), 'coef: intercept %.3f ms' % bb[0], ' '.join('%s %.5f ms/unit' % (k, c) for k, c in zip(K, bb[1:])), 'R2 %.3f' % r2)
