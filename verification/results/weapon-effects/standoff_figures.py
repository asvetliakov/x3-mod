#!/usr/bin/env python3
"""The stand-off and drained-shield ledger figures from the native records (docs/verification/weapon-effects.md,
"Stand-off and drained shield"): the binding, the slots, the drained hook cases, the placement rows about the hit's
floated image, the tilted plate, the CPU twin's parallax steps and the GPU timing at 5120x1440 against efce60957's
ledger figures.

  python3 verification/results/weapon-effects/standoff_figures.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
REC = ROOT / 'verification/results/native-windows/weapon-effects'
# efce60957's 5120x1440 rows (docs/verification/weapon-effects.md, "The screen-space patch").
BEFORE = {'fighter_two_hits_one_region': 21.0, 'capital_close_two_hits_one_region': 176.4,
          'worst_four_regions': 365.1, 'capital_spread_spill': 680.1}
d3d = json.loads((REC / 'summary.json').read_text())
hook = json.loads((REC / 'hook-summary.json').read_text())
m = d3d['metrics']
audit = hook['build']['audit']
print('d3d', d3d['run']['result'], 'commit', d3d['source']['commit'])
print('hook', hook['run']['result'], 'calls', audit['handler_calls'], 'x87', audit['handler_x87'],
      'instructions', audit['handler_instructions'], 'event_us', hook['event_cost']['median_us'])
print('slots', m['attach_slots'][0])
for x in hook['drained']:
    print('drained', x)
for x in m['placement']:
    print('placement', x['case'], 'draws', x['patches'], 'lit', x['lit_pixels'], 'sky', x['sky_lit'], 'outside',
          x['outside_lit'], 'coverage', x['coverage'], 'centroid', x['centroid_error_cells'], 'image_shift',
          x['image_shift_cells'], 'ratio', x['lit_ratio'])
for x in m['standoff']:
    print('plate', {k: x[k] for k in ('standoff_m', 'lit_pixels', 'sky_lit', 'shift_view', 'predicted_view',
                                      'shift_error_cells', 'implied_standoff_m', 'implied_error_cells', 'theta_deg')})
for x in m['standoff_steps']:
    print('steps', {k: x[k] for k in ('case', 'steps', 'pixels', 'refused_no_normal', 'drawn', 'rejected', 'px_mean',
                                      'px_p99', 'px_max', 'silhouette_px_p99', 'silhouette_px_max', 'cells_p99')})
for x in m['timing']:
    if x['width'] == 5120:
        ratio = x['gpu_us'] / BEFORE[x['case']] if x['case'] in BEFORE else None
        print('timing5120', x['case'], 'us', x['gpu_us'], 'raster', x['raster_share'], 'lit', x['lit_share'],
              'vs_efce60957', None if ratio is None else round(ratio, 3))
print('cockpit', [(x['case'], x.get('dimmed')) for x in m['cockpit']])
print('nopop', [(x['case'], x.get('popped_pixels')) for x in m['nopop']])
print('stage_cpu', [(x['case'], x['best_us'], x['mean_us']) for x in m['stage_cpu']])
