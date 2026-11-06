"""The figures docs/verification/weapon-effects.md quotes for the Run235 Flight B placement fix (the per-face lattice at
the hit point), from the two native records run_weapon_effects.py --host native writes: the hook audit, the placement
rows, the no-pop runs and witnesses, the seam, the stage CPU cost and the fenced timing.
Usage: python3 verification/results/weapon-effects/run235-flight-b/placement_fix_rows.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parents[2] / 'native-windows/weapon-effects'
d = json.loads((HERE / 'summary.json').read_text())
h = json.loads((HERE / 'hook-summary.json').read_text())
a = h['build']['audit']
print('d3d', d['passed'], d['run']['result'], d['source']['commit'])
print('hook', h['passed'], h['run']['result'], 'calls', {k: len(v) for k, v in a['handler_calls'].items()},
      'x87', len(a['handler_x87']), 'frame', a['handler_frame_bytes'])
print('hook_dir', h.get('dir'), 'event_cost', h['event_cost'], 'stack', h['stack_depth'])
m = d['metrics']
print('placement_aft', m.get('placement_aft'))
print('seam_narrow', m.get('seam_narrow'))
keys = ('case', 'region', 'pos_r', 'cell_m', 'centroid_error_m', 'centroid_error_cells', 'old_projection_error_m',
        'screen_error_px', 'screen_error_m', 'lit_pixels', 'eye_r', 'hull_normal_angle_deg')
for p in m['placement']:
    print('placement', {k: p[k] for k in keys})
for x in m['nopop']:
    print('nopop', {k: x.get(k) for k in ('case', 'max_patches', 'max_region_cells', 'spills', 'cells_dropped', 'max_drop',
                                          'excess', 'popped_pixels', 'max_overlap')})
for x in m['stage_cpu']:
    print('stage_cpu', x)
print('seam', m['seam'][0])
print('attach', m['attach'][0])
print('grid', m['grid'][0])
print('adjacent', m['adjacent'][0]['cells'], m['adjacent'][0]['peak_pair'], m['adjacent'][0]['peak_single'])
for x in m['resolve']:
    print('resolve', x['width'], x['speed_px'], x['core'])
print('age', m['age'][0])
for t in m['timing']:
    print('timing', t['width'], t['case'], t['patches'], t['lit_share'], t['raster_share'], t['gpu_us'])
