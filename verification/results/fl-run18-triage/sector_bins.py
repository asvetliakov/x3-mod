#!/usr/bin/env python3
"""FL runs 17 vs 18: mean frame interval per (sector, game-draw bin). Usage: python3 -I sector_bins.py frames17.pkl log17 frames18.pkl log18"""
import pickle, sys, re
EDGES = [0, 200, 400, 600, 800, 1000, 1300, 1700, 99999]
def load(pk, log):
    F = pickle.load(open(pk, 'rb'))['frames']
    ns = sorted(n for n in F if 'qpc' in F[n])
    dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
    ev = []; prev = None
    for line in open(log, errors='replace'):
        if line.startswith('sector_background '):
            m = re.search(r' frame=(\d+).*name="([^"]*)"', line)
            if m and m[2] != prev: ev.append((int(m[1]), m[2])); prev = m[2]
    out = {}; i = 0; cur = ''
    for n in sorted(dt):
        while i < len(ev) and ev[i][0] <= n: cur = ev[i][1]; i += 1
        if not cur or dt[n] > 100: continue
        d = F[n]['draws']; b = max(j for j in range(len(EDGES) - 1) if EDGES[j] <= d)
        out.setdefault((cur, b), []).append(dt[n])
    return out
A = load(sys.argv[1], sys.argv[2]); B = load(sys.argv[3], sys.argv[4])
print('sector | draws bin | run17 n | run17 mean ms | run18 n | run18 mean ms | delta ms')
for k in sorted(set(A) | set(B)):
    a, b = A.get(k, []), B.get(k, [])
    if len(a) < 200 or len(b) < 200: continue
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    print('%s | [%d,%d) | %d | %.2f | %d | %.2f | %+.2f' % (k[0], EDGES[k[1]], EDGES[k[1] + 1], len(a), ma, len(b), mb, mb - ma))
