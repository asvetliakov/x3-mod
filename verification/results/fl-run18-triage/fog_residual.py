#!/usr/bin/env python3
"""FL run 18: fit per-second frame_ms = a + b * draws on fog-off seconds, then the residual of the fog-applied seconds.
Also sector-name changes and hitch frames (dt > 50 ms). Usage: python3 -I fog_residual.py frames.pkl <x3m.log>"""
import pickle, sys, re
F = pickle.load(open(sys.argv[1], 'rb'))['frames']
ns = sorted(n for n in F if 'qpc' in F[n])
dt = {b: (F[b]['qpc'] - F[a]['qpc']) / 1e4 for a, b in zip(ns, ns[1:]) if b == a + 1}
t0 = F[ns[0]]['qpc']; sec = {}
for n, d in dt.items(): sec.setdefault(int((F[n]['qpc'] - t0) / 1e7), []).append(n)
fog_first = None; names = []; prev = None; fogrows = []
for line in open(sys.argv[2], errors='replace'):
    if line.startswith('volumetric_fog_frame ') and ' applied=1 ' in line and fog_first is None:
        fog_first = int(re.search(r' frame=(\d+)', line)[1])
    if line.startswith('volumetric_fog_frame '):
        m = re.search(r' frame=(\d+) applied=(\d).*? calls=(\d+).*? mote_calls=(\d+)', line)
        fogrows.append((int(m[1]), int(m[2]), int(m[3]), int(m[4])))
    if line.startswith('sector_background '):
        m = re.search(r' frame=(\d+).*name="([^"]*)"', line)
        if m and m[2] != prev: names.append((int(m[1]), m[2])); prev = m[2]
sec_of = lambda n: int((F[n]['qpc'] - t0) / 1e7)
print('fog first applied frame', fog_first, 'second', sec_of(fog_first))
print('fog_frame rows applied=1:', [(f, c, mc) for f, a, c, mc in fogrows if a][:9])
for f, nm in names: print('sector change frame %d t=%ds %s' % (f, sec_of(f) if f in F and 'qpc' in F[f] else -1, nm))
pts = []
for s, v in sec.items():
    ms = sum(dt[n] for n in v) / len(v)
    if ms > 50: continue
    pts.append((s, sum(F[n]['draws'] for n in v) / len(v), ms, v[0] >= fog_first))
off = [(x, y) for _, x, y, f in pts if not f]
mx = sum(x for x, _ in off) / len(off); my = sum(y for _, y in off) / len(off)
b = sum((x - mx) * (y - my) for x, y in off) / sum((x - mx) ** 2 for x, _ in off); a = my - b * mx
res_off = sorted(y - (a + b * x) for x, y in off)
print('fit on %d fog-off seconds: frame_ms = %.2f + %.5f * draws; residual p10 %.2f p50 %.2f p90 %.2f ms' % (len(off), a, b,
      res_off[len(res_off) // 10], res_off[len(res_off) // 2], res_off[9 * len(res_off) // 10]))
on = [(s, x, y) for s, x, y, f in pts if f]
res_on = sorted(y - (a + b * x) for _, x, y in on)
print('fog-on seconds %d: draws mean %.0f frame_ms mean %.2f residual p10 %.2f p50 %.2f p90 %.2f ms' % (len(on),
      sum(x for _, x, _ in on) / len(on), sum(y for _, _, y in on) / len(on), res_on[len(res_on) // 10], res_on[len(res_on) // 2], res_on[9 * len(res_on) // 10]))
hitch = [(n, dt[n]) for n in sorted(dt) if dt[n] > 50]
print('hitch frames dt>50ms:', len(hitch), [(n, sec_of(n), round(d)) for n, d in hitch][:40])
