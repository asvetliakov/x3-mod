#!/usr/bin/env python3
"""The ship-bubble ledger figures from the native records (docs/verification/weapon-effects.md, "Ship bubble, station
decal"): the binding, the hook audit, the slots, the placement rows against the hit's bubble point (the station on
the hull decal), the axis-plane pair, the bubble's two sides (the chase camera's nose hit, the eye inside), the hull
walk's cost (the fixture reader and the production engine_memory::read), the stage CPU, the no-pop and cockpit rows
and the GPU timing at both sizes (with the eye inside and the cockpit), the bubble's rows against 6720724d3's ledger
figures at 5120x1440.

  python3 verification/results/weapon-effects/bubble_figures.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
REC = ROOT / 'verification/results/native-windows/weapon-effects'
# 6720724d3's 5120x1440 rows (docs/verification/weapon-effects.md, "The screen-space patch"), the bubble's counterparts.
BEFORE = {'bubble_fighter_two_hits': ('fighter_two_hits_one_region', 21.0),
          'bubble_capital_close_two_hits': ('capital_close_two_hits_one_region', 176.4),
          'bubble_worst_one_owner_four_hits': ('worst_four_regions', 365.1),
          'capital_spread_spill': ('capital_spread_spill', 680.1)}
d3d = json.loads((REC / 'summary.json').read_text())
hook = json.loads((REC / 'hook-summary.json').read_text())
m = d3d['metrics']
audit = hook['build']['audit']
print('d3d', d3d['run']['result'], 'commit', d3d['source']['commit'], 'checks', len(d3d['checks']))
print('hook', hook['run']['result'], 'calls', audit['handler_calls'], 'x87', audit['handler_x87'],
      'instructions', audit['handler_instructions'], 'event_us', hook['event_cost']['median_us'])
print('slots', m['attach_slots'][0])
for x in m['placement']:
    print('placement', x['case'], x['surface'], 'draws', x['patches'], 'lit', x['lit_pixels'], 'sky', x['sky_lit'],
          'outside', x['outside_lit'], 'off_bubble', x['off_bubble_lit'], 'coverage', x['coverage'], 'centroid',
          x['centroid_error_cells'], 'b_shift', x['bubble_shift_cells'], 'ratio', x['lit_ratio'])
for x in m['bubble_side']:
    keys = ('lit_pixels', 'hidden_lit', 'sky_lit', 'eligible', 'coverage', 'hidden', 'hidden_share',
            'centroid_error_cells') if x['side'] != 'both' else ('luma', 'far_luma', 'ratio')
    print('side', x['case'], x['side'], {k: x[k] for k in keys})
print('axis_plane', m['axis_plane'][0])
print('hull_walk', m['hull_walk'][0])
print('stage_cpu', [(x['case'], x['patches'], x['best_us'], x['mean_us']) for x in m['stage_cpu']])
print('nopop', [(x['case'], x['popped_pixels'], x['max_patches']) for x in m['nopop']])
print('cockpit', [(x['case'], x.get('patches'), x.get('dimmed')) for x in m['cockpit']])
print('seam', m['seam'][0])
for x in m['timing']:
    ratio = None
    if x['width'] == 5120 and x['case'] in BEFORE:
        ratio = round(x['gpu_us'] / BEFORE[x['case']][1], 3)
    print('timing', x['width'], x['case'], 'us', x['gpu_us'], 'raster', x['raster_share'], 'lit', x['lit_share'],
          'vs_6720724d3', ratio)
