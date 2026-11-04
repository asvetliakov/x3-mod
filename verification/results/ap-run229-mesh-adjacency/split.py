#!/usr/bin/env python3
"""Per-interval split of ID3DXMesh::GenerateAdjacency wrapper time into fast-module
time, fallback (module-to-refusal + native D3DX) and per-call overhead.
Usage: split.py <perf log>. Pairs the k-th loading_metric GenerateAdjacency row with
the k-th cumulative mesh_adjacency_metric row (both written once per report)."""
import re, sys
import numpy as np
kv = lambda line: dict(re.findall(r'(\w+)=([^ ]+)', line))
gen, met, other = [], [], {}
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('loading_metric op='):
        d = kv(line)
        if d['op'] == 'ID3DXMesh::GenerateAdjacency': gen.append(d)
        else: other.setdefault(d['op'], []).append(d)
    elif line.startswith('mesh_adjacency_metric '):
        met.append(kv(line))
assert len(gen) == len(met), (len(gen), len(met))
rows, prev = [], dict(calls=0, computed=0, fallbacks=0, fast_us=0.0, faces=0, vertices=0)
for g, m in zip(gen, met):
    cur = dict(calls=int(m['calls']), computed=int(m['computed']), fallbacks=int(m['fallbacks']),
               fast_us=float(m['fast_us']), faces=int(m['faces']), vertices=int(m['vertices']))
    d = {k: cur[k] - prev[k] for k in cur}
    prev = cur
    assert d['calls'] == int(g['count']), (d['calls'], g['count'])
    d['wrap_us'] = float(g['total_us']); d['wrap_max_us'] = float(g['max_us']); d['tail_us'] = float(g['wrapper_tail_us'])
    rows.append(d)
A = np.array([[r['fast_us'], r['fallbacks'], r['calls']] for r in rows]); y = np.array([r['wrap_us'] for r in rows])
tot = {k: sum(r[k] for r in rows) for k in rows[0]}
print('intervals', len(rows), 'totals', {k: round(v, 1) for k, v in tot.items()})
print('residual (wrap - fast) ms', round((tot['wrap_us'] - tot['fast_us']) / 1e3, 1))
nofb = [r for r in rows if r['fallbacks'] == 0 and r['calls']]
if nofb:
    c = sum(r['calls'] for r in nofb); res = sum(r['wrap_us'] - r['fast_us'] for r in nofb)
    print(f'fallback-free intervals {len(nofb)} calls {c} wrap-fast {res:.1f} us => {res/c:.2f} us/call overhead (lock/decl/copy/state)')
coef, *_ = np.linalg.lstsq(A, y, rcond=None)
print('lstsq wrap_us = a*fast_us + b*fallbacks + c*calls: a=%.3f b=%.1f us c=%.2f us' % tuple(coef))
A2 = np.array([[r['fallbacks'], r['calls']] for r in rows]); y2 = np.array([r['wrap_us'] - r['fast_us'] for r in rows])
coef2, *_ = np.linalg.lstsq(A2, y2, rcond=None)
print('lstsq (wrap-fast) = b*fallbacks + c*calls: b=%.1f us c=%.2f us' % tuple(coef2))
comp = tot['computed']
print('computed mesh mean faces %.0f vertices %.0f fast_us/mesh %.1f' % (tot['faces']/comp, tot['vertices']/comp, tot['fast_us']/comp))
print('wrap max per interval (top 8):', sorted(((round(r['wrap_max_us']/1e3,2), r['fallbacks'], r['computed']) for r in rows), reverse=True)[:8])
for op, lst in other.items():
    if op.startswith(('D3DX', 'ID3DXMesh')):
        print(op, 'count', sum(int(d['count']) for d in lst), 'total_ms %.1f' % (sum(float(d['total_us']) for d in lst)/1e3),
              'excl_ms %.1f' % (sum(float(d['exclusive_us']) for d in lst)/1e3), 'max_ms %.2f' % (max(float(d['max_us']) for d in lst)/1e3))
# Fallback-free intervals: mesh size, and per-fallback residual distribution elsewhere.
if nofb:
    print('fallback-free faces/call %.0f fast_us/call %.1f' % (sum(r['faces'] for r in nofb)/sum(r['calls'] for r in nofb),
          sum(r['fast_us'] for r in nofb)/sum(r['calls'] for r in nofb)))
ov = sum(r['wrap_us'] - r['fast_us'] for r in nofb) / sum(r['calls'] for r in nofb) if nofb else 0.0
fb_cost = (tot['wrap_us'] - tot['fast_us'] - ov * tot['calls']) / tot['fallbacks']
print('per-fallback cost (module-to-refusal + native D3DX) %.0f us, total fallback ms %.0f, overhead ms %.0f' %
      (fb_cost, fb_cost * tot['fallbacks'] / 1e3, ov * tot['calls'] / 1e3))
per = sorted((r['wrap_us'] - r['fast_us'] - ov * r['calls']) / r['fallbacks'] for r in rows if r['fallbacks'] >= 5)
print('per-interval residual/fallback us: n=%d p10=%.0f median=%.0f p90=%.0f' % (len(per), per[len(per)//10], per[len(per)//2], per[9*len(per)//10]))
fbshare = sorted(r['fallbacks'] / r['calls'] for r in rows if r['calls'] >= 20)
print('fallback share per interval (calls>=20): p10=%.2f median=%.2f p90=%.2f' % (fbshare[len(fbshare)//10], fbshare[len(fbshare)//2], fbshare[9*len(fbshare)//10]))
# per-mesh split over all 7402 calls (engine sequence per mesh = CreateMesh + CleanMesh + GenerateAdjacency + OptimizeInplace)
seq = {op: sum(float(d['total_us']) for d in other[op]) for op in ('D3DXCreateMesh', 'D3DXCleanMesh', 'ID3DXMesh::OptimizeInplace')}
n = tot['calls']
print('per-mesh us: fast_module(all calls) %.1f overhead %.1f fallback(amortised) %.1f | CreateMesh %.1f CleanMesh %.1f OptimizeInplace %.1f' %
      (tot['fast_us']/n, ov, fb_cost*tot['fallbacks']/n, seq['D3DXCreateMesh']/n, seq['D3DXCleanMesh']/n, seq['ID3DXMesh::OptimizeInplace']/n))
print('top residual intervals: resid_ms fallbacks computed fast_ms faces wrap_max_ms')
for r in sorted(rows, key=lambda r: r['wrap_us'] - r['fast_us'], reverse=True)[:12]:
    print('  %.1f %d %d %.1f %d %.2f' % ((r['wrap_us'] - r['fast_us'])/1e3, r['fallbacks'], r['computed'], r['fast_us']/1e3, r['faces'], r['wrap_max_us']/1e3))
small = [r for r in rows if r['wrap_us'] - r['fast_us'] < 20 * r['calls'] + 50 * r['fallbacks'] and r['calls']]
print('cheap intervals: n=%d calls=%d fallbacks=%d resid_ms=%.1f' % (len(small), sum(r['calls'] for r in small), sum(r['fallbacks'] for r in small), sum(r['wrap_us']-r['fast_us'] for r in small)/1e3))
big = [r for r in rows if r not in small]
print('expensive intervals: n=%d calls=%d fallbacks=%d computed=%d resid_ms=%.1f fast_ms=%.1f' % (len(big), sum(r['calls'] for r in big), sum(r['fallbacks'] for r in big), sum(r['computed'] for r in big), sum(r['wrap_us']-r['fast_us'] for r in big)/1e3, sum(r['fast_us'] for r in big)/1e3))
B = np.array([[r['fallbacks'], r['fast_us']] for r in big]); yb = np.array([r['wrap_us'] - r['fast_us'] for r in big])
cb, res, *_ = np.linalg.lstsq(B, yb, rcond=None)
pred = B @ cb
print('expensive intervals: resid = b*fallbacks + d*fast_us: b=%.0f us/fallback d=%.2f  R2=%.3f' % (cb[0], cb[1], 1 - ((yb-pred)**2).sum()/((yb-yb.mean())**2).sum()))
for cols, name in (([0], 'fallbacks only'), ([1], 'fast_us only')):
    c1, *_ = np.linalg.lstsq(B[:, cols], yb, rcond=None); p1 = B[:, cols] @ c1
    print('  %s: coef=%.2f R2=%.3f' % (name, c1[0], 1 - ((yb-p1)**2).sum()/((yb-yb.mean())**2).sum()))
# Intervals where the cumulative fast_max rose: the max mesh lies inside, so its size is bounded by the interval's faces.
pm, pf, pv, pc, pt = 0.0, 0, 0, 0, 0.0
print('fast_max rises: new_max_us interval_faces interval_vertices interval_computed interval_fast_us')
for m in met:
    mx, f, v, c, t = float(m['fast_max_us']), int(m['faces']), int(m['vertices']), int(m['computed']), float(m['fast_us'])
    if mx > pm and mx > 1000: print('  %.1f %d %d %d %.0f' % (mx, f - pf, v - pv, c - pc, t - pt))
    pm, pf, pv, pc, pt = max(pm, mx), f, v, c, t
