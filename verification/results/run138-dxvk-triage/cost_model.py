#!/usr/bin/env python3
"""Run 137 A: separate test cost from skip saving. Per in-flight frame (qpc dt, ms): draws-issued (non-cull skips),
least-squares fits dt ~ issued (+ tested) per run, matched-draws comparison, and per frame_phases window
view_submit/views p50 vs the window's mean tested/skipped/issued.  usage: cost_model.py RUN16DIR RUN15DIR RUN14DIR"""
import re, sys, glob, statistics as st
from collections import defaultdict
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
def load(folder):
    log = glob.glob(f'{folder}/session-*.log')[0]; fe = {}; oc = {}; fp = []
    for raw in open(log, 'rb'):
        k = raw.split(b' ', 1)[0]
        if k not in (b'frame_end', b'occlusion_cull', b'frame_phases'): continue
        d = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}
        (fe if k == b'frame_end' else oc if k == b'occlusion_cull' else None).__setitem__(int(d['frame']), d) if k != b'frame_phases' else fp.append(d)
    fs = sorted(fe)
    for a, b in zip(fs, fs[1:]):
        if b == a + 1: fe[b]['dtq'] = (fe[b]['qpc'] - fe[a]['qpc']) / 1e4
    L = max(fs, key=lambda f: fe[f]['dt_ms']); lo, hi = L + 10, fs[-1] - 10
    S = [f for f in fs if lo <= f <= hi and 'dtq' in fe[f] and fe[f]['dtq'] < 33]  # hitches excluded from fits
    return fe, oc, fp, S, lo, hi
def lsq(X, y):  # normal equations, tiny
    n = len(X[0]); A = [[sum(r[i] * r[j] for r in X) for j in range(n)] for i in range(n)]; b = [sum(r[i] * t for r, t in zip(X, y)) for i in range(n)]
    for i in range(n):
        p = A[i][i]
        for j in range(i + 1, n):
            m = A[j][i] / p; A[j] = [a - m * c for a, c in zip(A[j], A[i])]; b[j] -= m * b[i]
    x = [0] * n
    for i in reversed(range(n)): x[i] = (b[i] - sum(A[i][j] * x[j] for j in range(i + 1, n))) / A[i][i]
    return x
R = {name: load(d) for name, d in zip(('run16', 'run15', 'run14'), sys.argv[1:4])}
for name, (fe, oc, fp, S, lo, hi) in R.items():
    other = [fe[f]['draws'] - fe[f]['issued'] - (oc[f]['skipped'] if f in oc else 0) for f in S]
    print(f'{name}: frames<33ms={len(S)} non-cull skips (draws-issued-skipped) p50={st.median(other):.0f} max={max(other):.0f} min={min(other):.0f}')
    sel = [f for f in S if fe[f]['draws'] >= 120]
    y = [fe[f]['dtq'] for f in sel]
    c = lsq([[1, fe[f]['issued']] for f in sel], y)
    print(f'  fit dt ~ a + b*issued (draws>=120, n={len(sel)}): a={c[0]:.2f} ms b={c[1]*1000:.1f} us/issued draw')
    if oc:
        c = lsq([[1, fe[f]['issued'], oc[f]['tested'] if f in oc else 0] for f in sel], y)
        print(f'  fit dt ~ a + b*issued + c*tested: a={c[0]:.2f} b={c[1]*1000:.1f} us/issued c={c[2]*1000:.1f} us/test')
        c = lsq([[1, fe[f]['draws'], oc[f]['tested'] if f in oc else 0, oc[f]['skipped'] if f in oc else 0] for f in sel], y)
        print(f'  fit dt ~ a + b*draws + c*tested + d*skipped: a={c[0]:.2f} b={c[1]*1000:.1f} us/draw c={c[2]*1000:.1f} us/test d={c[3]*1000:.1f} us/skip')
print('matched app draws (bins of 20, frames<33 ms): dt p50 / mean')
bins = {n: defaultdict(list) for n in R}
for n, (fe, oc, fp, S, lo, hi) in R.items():
    for f in S: bins[n][int(fe[f]['draws']) // 20 * 20].append(fe[f]['dtq'])
for b in sorted(set().union(*[set(x) for x in bins.values()])):
    if all(len(bins[n][b]) >= 30 for n in R):
        print(f'  draws {b:3d}-{b+19}: ' + '  '.join(f'{n} n={len(bins[n][b]):4d} p50={st.median(bins[n][b]):5.2f} mean={st.mean(bins[n][b]):5.2f}' for n in R))
print('frame_phases windows (run16): frame, view_submit_p50, views_p50, present_p50, dt_p50 vs window mean tested/skipped/issued/draws')
fe, oc, fp, S, lo, hi = R['run16']
rows = []
for w in fp:
    e = int(w['frame']); fr = [f for f in range(e - 299, e + 1) if f in fe and lo <= f <= hi]
    if len(fr) < 250: continue
    m = lambda g: st.mean(g(f) for f in fr)
    rows.append((e, w['view_submit_p50_us'], w['views_p50_us'], w['present_p50_us'], w['dt_p50_us'], m(lambda f: oc[f]['tested'] if f in oc else 0),
                 m(lambda f: oc[f]['skipped'] if f in oc else 0), m(lambda f: fe[f]['issued']), m(lambda f: fe[f]['draws'])))
for r in rows: print('  %5d vs=%5.0f views=%5.0f present=%4.0f dt=%5.0f | tested=%5.1f skipped=%5.1f issued=%5.1f draws=%5.1f' % r)
y = [r[1] for r in rows]
c = lsq([[1, r[7], r[5]] for r in rows], y); print(f'  window fit view_submit_p50 ~ a + b*issued + c*tested: a={c[0]:.0f} b={c[1]:.1f} us/issued c={c[2]:.1f} us/test')
c = lsq([[1, r[8], r[5], r[6]] for r in rows], y); print(f'  window fit view_submit_p50 ~ a + b*draws + c*tested + d*skipped: a={c[0]:.0f} b={c[1]:.1f} c={c[2]:.1f} d={c[3]:.1f} us')
for n in ('run15', 'run14'):
    fe, oc, fp, S, lo, hi = R[n]; rows = []
    for w in fp:
        e = int(w['frame']); fr = [f for f in range(e - 299, e + 1) if f in fe and lo <= f <= hi]
        if len(fr) < 250: continue
        rows.append((w['view_submit_p50_us'], st.mean(fe[f]['issued'] for f in fr)))
    c = lsq([[1, r[1]] for r in rows], [r[0] for r in rows]); print(f'  {n} window fit view_submit_p50 ~ a + b*issued: a={c[0]:.0f} b={c[1]:.1f} us/issued (n={len(rows)})')
