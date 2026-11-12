"""FL 67 A ledger figures (capitals take the hull decal): the native fixture record's capital decal case, the stage CPU
rows, the capital no-pop rows, the walk cases retargeted to corvette and the capital spread's GPU timing.
Usage: python3 -I verification/results/fl-run67/capital_decal_rows.py [summary.json] [old summary.json]
(old: e.g. `git show <commit>:verification/results/native-windows/weapon-effects/summary.json > /tmp/old.json`)."""
import json, sys
new = sys.argv[1] if len(sys.argv) > 1 else 'verification/results/native-windows/weapon-effects/summary.json'
m = json.load(open(new))['metrics']
old = json.load(open(sys.argv[2]))['metrics'] if len(sys.argv) > 2 else None
for x in m['placement']:
    if x['case'] in ('capital_titan_flank', 'station_flight_b_0p68R'):
        print('PLACEMENT', {k: x[k] for k in ('case', 'surface', 'patches', 'bubbles', 'walks', 'lit_pixels', 'sky_lit',
                                              'outside_lit', 'eligible', 'covered', 'centroid_error_cells')})
for x in m['stage_cpu']:
    print('STAGE_CPU', {k: x[k] for k in ('case', 'hits', 'patches', 'cells', 'spills', 'cells_dropped', 'bubbles', 'walks',
                                          'best_us', 'mean_us')})
for x in m['nopop']:
    if any(w in x['case'] for w in ('capital', 'station', 'corvette')):
        print('NOPOP', {k: x[k] for k in ('case', 'max_live_hits', 'max_patches', 'max_bubbles', 'max_region_cells', 'spills',
                                          'cells_dropped', 'excess', 'popped_pixels')})
for x in m['hull_walk']:
    if x.get('case') in ('capital_side_hit', 'capital_titan_scale') or 'case' not in x:
        print('HULL_WALK', {k: x[k] for k in ('case', 'frames', 'bubbles', 'e_rel_err', 'lit_pixels', 'centroid_error_cells',
                                              'shift_ratio', 'cell', 'warm_best_us', 'cold_best_us', 'patches')
                            if k in x})
def timing(mm):
    return {(x['width'], x['case']): x for x in mm['timing']}
t, o = timing(m), timing(old) if old else {}
for k in sorted(t):
    if k[1] == 'capital_spread_spill':
        print('TIMING', k, 'gpu_us', t[k]['gpu_us'], 'raster_share', t[k]['raster_share'], 'patches', t[k]['patches'],
              'old_gpu_us', o.get(k, {}).get('gpu_us'), 'old_raster_share', o.get(k, {}).get('raster_share'))
