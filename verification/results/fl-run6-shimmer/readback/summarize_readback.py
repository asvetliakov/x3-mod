#!/usr/bin/env python3
"""Fold rb_<frame>.json (readback_hold.py) into summary.json: per capture, the farm / station / whole-frame hold state,
the TAA-output and pre-TAA frame-to-frame change pooled over the 7 pairs, the split by hold class, and the first-frame
step versus the stalled steps.

Reproduce: python3 ../extract_draws.py /mnt/c/X3FL/x3m.log DRAWS.json
           python3 readback_hold.py /mnt/c/X3FL/x3-modern-captures DRAWS.json 13630 rb_13630.json   (and 13940)
           python3 summarize_readback.py DRAWS.json"""
import json, sys, numpy as np
D = json.load(open(sys.argv[1])); here = sys.argv[2] if len(sys.argv) > 2 else '.'
def pool_class(r, reg, tag):
    n = 0; acc = {}
    for pr in r['pairs']:
        e = pr['regions'].get(reg, {}).get('by_' + tag)
        if not e: continue
        n += e['px']
        for k in ('taa_dQ_mean', 'taa_dQ_cubic', 'taa_interp_floor', 'taa_gt4_frac', 'hdr_dQ_mean'): acc[k] = acc.get(k, 0) + e['px']*e[k]
    return {'px_frames': n, **{k: round(v/n, 3) for k, v in acc.items()}} if n else None
out = {'source': 'readback_hold.py on /mnt/c/X3FL/x3-modern-captures, log /mnt/c/X3FL/x3m.log (run 6, 879f3e8c), taa_debug=1',
       'decoding': 'taa_lock byte0 = t + 32 class + 64 sign + 128 mode, locked t>0; taa_age frac*65536 mod 128 = thin-region hold h (region h>0, flagged h==8); count floor(|age|)',
       'flicker_metric': 'mean |Q_k(x) - Q_{k-1}(prev)| in display codes, prev = motion RT1 q + current jitter (taa, present) or + previous jitter (hdr); hdr_nojit = temporal_variance.py form; interp_floor = |bilinear - Catmull-Rom| at prev',
       'captures': {}}
for f0 in (13630, 13940):
    r = json.load(open('%s/rb_%d.json' % (here, f0)))
    c = {'crop': r['crop'], 'farm_nodes': r['farm_nodes'], 'station_nodes': r['station_nodes'], 'frames': {}}
    for f in range(f0, f0 + 8):
        fr = D['frame_rows'][str(f)]
        c['frames'][str(f)] = {'dt_ms': float(fr['hdr_frame']['dt_ms']), 'cam_z': float(fr['camera_state']['t'].split(',')[2]),
                               'cut_median_px': float(fr['motion_output_cut']['median_px']), 'jitter_index': int(fr['motion_output_frame']['jitter_index'])}
    regs = ('farm_all', 'farm_alphatest', 'farm_nonalphatest', 'station_54ca_54cb', 'model_57df', 'whole_frame_geo') + tuple('node_' + n for n in r['farm_nodes'])
    hs = {}
    for reg in regs:
        rows = [row['regions'].get(reg) for row in r['per_frame']]
        rows = [x for x in rows if x]
        if not rows: continue
        hs[reg] = {k: [x[k] for x in rows] for k in ('px', 'lock_frac', 'lock_full_frac', 'lock_motion_mode_frac', 'thin_region_frac', 'thin_flagged_now_frac', 'closure_held_frac', 'age_p50', 'age_p10', 'age_le1_frac', 'age_exit_neg_frac')}
    c['hold_per_frame'] = hs
    fl = {}
    for reg in regs:
        vals = [pr['regions'][reg] for pr in r['pairs'] if reg in pr['regions']]
        if not vals: continue
        e = {'speed_px_p50_per_pair': [v['speed_px_p50'] for v in vals]}
        for k in ('taa', 'present', 'hdr', 'hdr_nojit'):
            d = [v[k]['dQ_mean'] for v in vals]
            e[k] = {'dQ_per_pair': d, 'dQ_mean': round(float(np.mean(d)), 3), 'dQ_cubic_mean': round(float(np.mean([v[k]['dQ_cubic_mean'] for v in vals])), 3),
                    'interp_floor_mean': round(float(np.mean([v[k]['interp_floor_mean'] for v in vals])), 3), 'gt4_frac_mean': round(float(np.mean([v[k]['gt4_frac'] for v in vals])), 4)}
        e['by_hold_class'] = {t: pool_class(r, reg, t) for t in ('locked', 'lock_full', 'thin_region', 'locked_and_thin', 'locked_rest_mode', 'neither')}
        fl[reg] = e
    c['flicker'] = fl
    out['captures'][str(f0)] = c
json.dump(out, open('%s/summary.json' % here, 'w'), indent=1)
print('wrote summary.json', {k: len(v['flicker']) for k, v in out['captures'].items()})
