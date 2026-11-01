#!/usr/bin/env python3
"""Compact per-node table from the tv_capture*.json outputs of temporal_variance.py (farm model 556e/556f/5570, stations).
Usage: summarize.py [DIR]  -> prints and writes DIR/summary.json"""
import json, sys, glob, os
d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__)
FARMS = {'0000556e', '0000556f', '00005570'}
rows = []
for p in sorted(glob.glob(os.path.join(d, 'tv_capture*.json'))):
    r = json.load(open(p))
    for h, n in r['nodes'].items():
        l = n.get('lit')
        if not l or l['n'] < 50: continue
        row = {'capture': r['capture'], 'file': os.path.basename(p), 'node': h, 'model': n['model'], 'farm': n['model'] in FARMS,
               'lod': n['lod'], 'draws': n['draws'], 'alphatest_draws': n['alphatest_draws'], 'lit_px': l['n'],
               'view_w_p50': round(n['view_w_p50']), 'speed_capture_px': round(n['speed_px_p50'], 2), 'sun_share': round(n['sun_share_mean'], 3),
               'surface_switch_frac': round(n['coverage_change_frac'], 3), 'pre_dC': l['pre_dC'], 'pre_std8': l['pre_std8'],
               'model_taa_dH': l['taa_dH'], 'model_lock_dH': l['taa_lock_dH'], 'model_hist_clip': l['hist_reject'], 'lockmodel_dH': l.get('lockmodel_dH'), 'lockmodel_locked': l.get('lockmodel_locked_last3'),
               'flicker_px_dC_gt8': l['flicker_px_frac_dC_gt8'], 'flicker_px_dH_gt2': l['flicker_px_frac_dH_gt2']}
        for k in ('sun_cov', 'sun_nocov', 'dark_cov', 'dark_nocov'):
            if k in n: row[k] = [n[k]['n'], n[k]['pre_dC'], n[k]['taa_dH']]
        rows.append(row)
rows.sort(key=lambda x: (x['capture'], not x['farm'], -x['lit_px']))
json.dump(rows, open(os.path.join(d, 'summary.json'), 'w'), indent=1)
for x in rows:
    print(x['capture'], x['node'], x['model'], 'F' if x['farm'] else ' ', 'lod', x['lod'][-1], 'at %d/%d' % (x['alphatest_draws'], x['draws']), 'px', x['lit_px'],
          'w', x['view_w_p50'], 'spd', x['speed_capture_px'], 'share', x['sun_share'], 'sw', x['surface_switch_frac'], 'pre', x['pre_dC'],
          'taa', x['model_taa_dH'], 'lock', x['model_lock_dH'], 'clip', x['model_hist_clip'], 'lockm', x['lockmodel_dH'], 'locked', x['lockmodel_locked'], 'gt2', x['flicker_px_dH_gt2'],
          'sun_cov', x.get('sun_cov'), 'sun_nocov', x.get('sun_nocov'), 'dark_cov', x.get('dark_cov'), 'dark_nocov', x.get('dark_nocov'))
