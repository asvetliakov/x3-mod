#!/usr/bin/env python3
"""Run 216 distant-dark triage: run the sun-shadow cascade twin
(verification/probe/sun_shadow_apply.py expected_factor_cascades) on the two F8 frames
and print, per region, the owning cascade histogram, the shadow factor and the
map coverage at the receivers' texels. Params come from the frames'
sun_shadow_apply_params lines (grep them out of x3m.log into argv[1] first)."""
import sys, numpy as np
sys.path.insert(0, '/home/asvetl/x3-mod/verification/probe')
import sun_shadow_apply as t
sys.path.insert(0, '/home/asvetl/x3-mod/verification/results/run216-distant-dark')
from regions import REGIONS, D, W, H
lines = {}
for line in open(sys.argv[1]):
    fl = t.line_fields(line); lines[int(fl['frame'])] = t.parse_apply_params(fl)
for f, regs in REGIONS.items():
    params, extra = lines[f]
    raw = open(D + 'depth_1_%d.rgba32f' % f, 'rb').read()
    d, s = t.unpack_rt2(raw, W, H, 'linear'); del raw
    maps = t.load_cascade_maps(D, 1, f, extra, params)
    for c, m in enumerate(maps):
        occ = None if m is None else float((m < 1.0).mean())
        print(f, 'cascade', c, 'map', None if m is None else m.shape, 'occupied(<1)=%s' % occ, 'extent', extra['cascades'][c].get('extent'))
    r = t.expected_factor_cascades(d, s, maps, params, coarse=True)
    for name, (x0, x1, y0, y1) in regs.items():
        sl = (slice(y0, y1), slice(x0, x1))
        valid = r['valid'][sl]; sel = r['selected'][sl]; fac = r['f'][sl]; z = d[sl]
        obj = z > 0
        hist = {int(k): int(v) for k, v in zip(*np.unique(sel[obj], return_counts=True))}
        fv = fac[obj & valid]
        print('%d %-16s obj=%d valid=%d selected=%s f_mean=%.3f f<0.5=%.3f' % (f, name, obj.sum(), (obj & valid).sum(), hist,
              fv.mean() if fv.size else -1, (fv < .5).mean() if fv.size else -1))
        for c in sorted(hist):
            if c < 0 or maps[c] is None: continue
            pos = r['sun'][c]
            own = obj & (sel == c)
            md = t_nearest = None
            N = maps[c].shape[0]
            mu = pos[0][sl] * .5 + .5; mv = .5 - pos[1][sl] * .5
            tu = np.clip(np.floor(np.nan_to_num(mu) * N + .5).astype(int), 0, N - 1); tv = np.clip(np.floor(np.nan_to_num(mv) * N + .5).astype(int), 0, N - 1)
            md = maps[c][tv, tu]; rz = pos[2][sl]
            rng = extra['cascades'][c]['depth_light'] + extra['cascades'][c]['depth_behind']
            margin = (md - rz)[own] * rng
            print('    cascade %d owned=%d map_occupied=%.3f margin_units p5/25/50/75/95=%s' % (c, own.sum(), (md[own] < 1.0).mean(),
                  np.round(np.percentile(margin, [5, 25, 50, 75, 95]), 1).tolist()))
    del d, s, maps, r
