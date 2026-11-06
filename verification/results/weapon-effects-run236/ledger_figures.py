#!/usr/bin/env python3
"""The screen-space patch's ledger figures from the native records (docs/verification/weapon-effects.md, "The
screen-space patch"): the record's binding, the placement rows, the surface, cull, cockpit, sort, no-pop, stage-CPU
and GPU timing rows, and the screen budget's bound (8 x the full-screen box at 5120x1440).

  python3 verification/results/weapon-effects-run236/ledger_figures.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
REC = ROOT / 'verification/results/native-windows/weapon-effects'
d3d = json.loads((REC / 'summary.json').read_text())
hook = json.loads((REC / 'hook-summary.json').read_text())
m = d3d['metrics']
audit = hook['build']['audit']
print('d3d', d3d['run']['result'], 'commit', d3d.get('source', {}).get('commit'))
print('hook', hook['run']['result'], 'calls', audit['handler_calls'], 'x87', audit['handler_x87'],
      'instructions', audit['handler_instructions'], 'event_us', hook['event_cost']['median_us'])
print('slots', m['attach_slots'][0])
for x in m['placement']:
    print('placement', x['case'], 'draws', x['patches'], 'lit', x['lit_pixels'], 'sky', x['sky_lit'], 'outside',
          x['outside_lit'], 'coverage', x['coverage'], 'centroid', round(x['centroid_error_cells'], 3), 'ratio',
          x['lit_ratio'])
print('surface', m['surface'][0])
print('seam', m['seam'][0])
for x in m['cull']:
    print('cull', x)
for x in m['cockpit']:
    print('cockpit', x['case'], {k: x[k] for k in x if k in ('dimmed', 'luma_sum', 'lit_pixels', 'own_cockpit_ratio',
                                                                 'other_ratio', 'unknown_ratio', 'stale_ratio')})
for x in m['sort']:
    print('sort', x)
for x in m['nopop']:
    print('nopop', x['case'], 'popped', x['popped_pixels'], 'excess', x['excess'], 'max_draws', x['max_patches'])
for x in m['normals']:
    print('normals', x)
for x in m['stage_cpu']:
    print('stage_cpu', x['case'], 'draws', x['patches'], 'best', x['best_us'], 'mean', x['mean_us'], 'dropped',
          x['dropped'], 'cells_dropped', x['cells_dropped'])
full = None
for x in m['timing']:
    print('gpu', x['width'], x['case'], 'draws', x['patches'], 'us', x['gpu_us'], 'raster', x['raster_share'])
    if x['width'] == 5120 and x['case'] == 'capital_fill_full_map':
        full = x['gpu_us']
print('screen_budget_bound_us', None if full is None else round(8 * full, 1), '(8 x the full-screen box at 5120x1440)')
