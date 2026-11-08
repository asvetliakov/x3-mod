#!/usr/bin/env python3
"""The projectile stage's figures docs/verification/weapon-effects.md ("Projectile IRE on FL (2026-11-08)") quotes, from
the native records of this directory (run_weapon_effects.py --host native): the three fixtures' verdicts and commits,
the FL batch-site checks, and every PROJECTILE row of the D3D fixture.
usage: python3 verification/results/native-windows/weapon-effects/projectile_rows.py"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
hook = json.loads((HERE / 'hook-summary.json').read_text())
fl = json.loads((HERE / 'hook-fl-summary.json').read_text())
d3d = json.loads((HERE / 'summary.json').read_text())
for name, r in (('ap_hook', hook), ('fl_hook', fl), ('d3d', d3d)):
    print(name, r['passed'], r['run']['result'], r['source']['commit'])
print('fl_batch', [(c['name'], c['passed']) for c in fl['checks'] if 'batch' in c['name'] or 'projectile' in c['name']
                   or c['name'].startswith('object_tap_registers')])
print('fl_stub_ok', fl['build']['audit']['stub_ok'].get('_x3m_weapon_effects_stub_batch_fl'),
      'windows', fl['build']['audit']['windows_match_exe'].get('batch'), 'stack', fl['stack_depth'])
print('d3d_projectile_checks', [(n, ok) for n, ok in d3d['checks'] if n.startswith('projectile')])
for row in d3d['metrics']['projectile']:
    print(row)
