#!/usr/bin/env python3
"""fire_round2.py [constants-json [ramp-json]]: the round-2 fire law (fire-material.md section 12) on fire_model.py's
replica against the section-6 gates as amended in round 2 (thin 0.10-0.35 and the equal-area disc's dark share >= 0.20 at
0.3 L, aniso <= 0.75, licks >= 2.0, the ramp's classes at 0.2 L, the core at 0.1 L, the AgX pink share < 0.02 at 0.1-0.3
L), hull burst R(t) 100 px and death body 90 px, medians over 6 seeds. No argument: the shipped constants (fire_model.K,
fire_model.RAMP). Prints the failing gates and the 0.3 L measures, then the erosion's equal-area radii and the 30 px far
hit's lit share. The 2026-11-07 search: random over heat_soft, rim_t, rim_cool, thr_r, gain, thr_t, fine_bias and the red
and orange stops' brightness (G / R 0.32 / 0.55), 150 samples about the first passing point, the largest least margin
rounded to the shipped values."""
import sys, json
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fire_model as M


def run(K, ramp=None, seeds=(3.1, 17.3, 29.9, 41.7, 53.2, 7.7)):
    if ramp is not None:
        M.RAMP[:] = np.array(ramp)
    out = {}
    for name, R, L, E, cg, cx, rs in (('hull', 100, 400, 1.5, 2.0, 0.5, 1.25), ('death', 90, 2000, 1.5, 5.0, 0.35, 1.4)):
        for frac in (0.1, 0.2, 0.3):
            acc = {}
            for sd in seeds:
                g = cg * (1 - frac / cx) ** 2 if frac < cx else 0
                T = 1 - frac if name == 'hull' else max(0, 1 - 0.8 * frac)
                ms = M.measures(M.render(int(R * 3.2), R, frac, T, E, g, frac * L, sd, 1.0, K, rs_over_r=rs), R)
                for k, v in ms.items():
                    acc.setdefault(k, []).append(v)
            out[(name, frac)] = {k: float(np.median(v)) for k, v in acc.items()}
    fails = []
    for nm in ('hull', 'death'):
        a, b, c = out[(nm, 0.3)], out[(nm, 0.2)], out[(nm, 0.1)]
        if not 0.10 <= a['thin'] <= 0.35: fails.append(nm + ':thin %.3f' % a['thin'])
        if a['disc_dark'] < 0.2: fails.append(nm + ':disc %.3f' % a['disc_dark'])
        if a['aniso'] > 0.75: fails.append(nm + ':aniso')
        if a['licks_disc'] < 2.0: fails.append(nm + ':licks %.2f' % a['licks_disc'])
        if not (b['hot'] >= 0.25 and b['hot_r'] <= 0.5): fails.append(nm + ':hot %.2f@%.2f' % (b['hot'], b['hot_r']))
        if not (0.05 <= b['cold'] <= 0.25 and b['cold_r'] >= 0.65): fails.append(nm + ':cold %.3f@%.2f' % (b['cold'], b['cold_r']))
        if b['mid'] > 0.5: fails.append(nm + ':mid %.2f' % b['mid'])
        if not (c['core'] >= 3 and c['out'] <= 1.5): fails.append(nm + ':core %.2f/%.2f' % (c['core'], c['out']))
        for f in (0.1, 0.2, 0.3):
            if out[(nm, f)]['pink'] > 0.02: fails.append(nm + ':pink@%.1f %.3f' % (f, out[(nm, f)]['pink']))
    return fails, out


if __name__ == '__main__':
    K = dict(M.K)
    K.update(json.loads(sys.argv[1]) if len(sys.argv) > 1 else {})
    ramp = json.loads(sys.argv[2]) if len(sys.argv) > 2 else None
    f, o = run(K, ramp)
    print('failing', f)
    for k in (('hull', 0.3), ('death', 0.3)):
        print(k, {a: round(v, 3) for a, v in o[k].items() if a in ('thin', 'disc_dark', 'pink', 'aniso', 'licks_disc', 'lit_in')})
    for R, frac in ((30, 0.4), (100, 0.1), (100, 0.5), (100, 0.9)):
        v = []
        for sd in (3.1, 17.3, 29.9, 41.7, 53.2, 7.7, 11.9, 61.3):
            rgb = M.render(int(R * 3.2), R, frac, 1 - frac, 1.5, 0, frac * 400, sd, 1.0, K, rs_over_r=1.25)
            l = rgb.max(axis=2)
            v.append((M.measures(rgb, R).get('lit_in', 0), np.sqrt((l > 0.1 * l.max()).sum() / np.pi) / R))
        print('hull R %d px at %.1f L: lit share inside R(t) %.3f, equal-area radius / R(t) %.3f' % ((R, frac) + tuple(np.median(np.array(v), axis=0))))
