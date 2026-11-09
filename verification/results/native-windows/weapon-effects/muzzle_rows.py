#!/usr/bin/env python3
"""The muzzle flash's figures docs/verification/weapon-effects.md ("IRE muzzle flash (2026-11-09)") quotes, from the
native records of this directory (run_weapon_effects.py --host native): the three fixtures' verdicts and commits, the FL
muzzle checks and MUZZLE row, and the D3D fixture's flash / pulse checks with the size, streak, LOD, life, pulse and
budget rows (compact: the fields the ledger cites).
usage: python3 verification/results/native-windows/weapon-effects/muzzle_rows.py"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
hook = json.loads((HERE / 'hook-summary.json').read_text())
fl = json.loads((HERE / 'hook-fl-summary.json').read_text())
d3d = json.loads((HERE / 'summary.json').read_text())
for name, r in (('ap_hook', hook), ('fl_hook', fl), ('d3d', d3d)):
    print(name, r['passed'], r['run']['result'], r['source']['commit'])
print('fl_muzzle', [(c['name'], c['passed']) for c in fl['checks'] if 'muzzle' in c['name'] or 'flash' in c['name']])
print('fl_muzzle_row', fl.get('muzzle'), 'stub_ok', fl['build']['audit']['stub_ok'].get('_x3m_weapon_effects_stub_muzzle_fl'),
      'muzzle_calls', fl['build']['audit']['handler_calls'].get('_x3m_weapon_muzzle_fl'), 'stack', fl['stack_depth'])
print('d3d_flash_checks', [(n, ok) for n, ok in d3d['checks'] if n.startswith(('flash', 'pulse'))])
keys = {'size': ('distance_m', 'flash_stops', 'core_white_px', 'core_white_measured_px', 'rule_white_px', 'white_px',
                 'ratio', 'fwhm_px', 'peak', 'capped', 'dot', 'share80_10'),
        'streak': ('distance_m', 'rule_px', 'length_px', 'white_run_px', 'white_px', 'folded', 'dot'),
        'lod': ('distance_m', 'dot', 'pulse', 'total', 'near_total', 'peak'),
        'life': ('name', 'step_ms', 'frame_us', 'seta', 'life_ms', 'tap_age_ms', 'drawn', 'want', 'back_us', 'want_back_us',
                 'fade_err', 'ride_err_px',
                 'axis_err_deg', 'lost'),
        'over_hull': ('worst_excess',),
        'budget_combined': ('tune', 'bolts', 'drawn', 'quads', 'flashes', 'pulses', 'pulse_clamped', 'cpu_us', 'gpu_us'),
        'budget_combined_bounds': ('cpu_us', 'cpu_within_100', 'flash_work_cpu_us', 'newborn_bolts_cpu_us',
                                   'bolts_448_cpu_us', 'gpu_us', 'gpu_within_60'),
        'budget': ('flashes', 'pulses', 'cpu_us', 'gpu_us', 'gpu_net_us', 'gpu_flashes_us', 'gpu_pulses_us',
                   'gpu_far_pulses_us', 'gpu_own_pulses_us', 'gpu_bolts_us', 'gpu_bolts_flashes_us', 'gpu_increment_us',
                   'fence_us')}
for row in d3d['metrics']['flash']:
    print('flash', row.get('case'), {k: row.get(k) for k in keys.get(row.get('case'), ())})
print('pulse_cap', d3d['metrics'].get('pulse_cap'))
print('pulse_ghost', d3d['metrics'].get('pulse_ghost'))
for row in d3d['metrics']['pulse']:
    print('pulse', {k: row.get(k) for k in ('distance_m', 'gain', 'radius_px', 'half_over_r', 'lit', 'sky_lit', 'peak',
                                             'peak_want', 'worst')})
