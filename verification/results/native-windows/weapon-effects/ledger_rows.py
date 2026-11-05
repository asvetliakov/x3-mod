#!/usr/bin/env python3
"""The figures docs/verification/weapon-effects.md quotes, from the two native records of this directory (written by
run_weapon_effects.py --host native): the hook fixture (checks, audit, stack, the shield read and the per-event cost)
and the D3D fixture of the hex patch (docs/architecture/weapon-effects.md 5.1.1: per-tier geometry, core, ring, fade,
colour, re-light and accumulation, the cap, decal, age law, retired, resolve, refusal) and its timing.
usage: python3 verification/results/native-windows/weapon-effects/ledger_rows.py"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
hook = json.loads((HERE / 'hook-summary.json').read_text())
d3d = json.loads((HERE / 'summary.json').read_text())
a = hook['build']['audit']
print('hook', hook['passed'], hook['run']['result'], 'stack', hook['stack_depth'], 'frame', a['handler_frame_bytes'],
      'calls', a['handler_calls'], 'x87', len(a['handler_x87']), 'undefined', len(a['handler_undefined']),
      'instructions', a['handler_instructions'], 'commit', hook['source']['commit'])
for s in hook['shield']:
    print('shield', s)
print('event_cost', hook['event_cost'])
print('d3d', d3d['passed'], d3d['run']['result'], 'commit', d3d['source']['commit'], 'adapter',
      d3d['host']['adapter']['description'], d3d['host']['adapter']['driver_version'], 'windows',
      d3d['host']['windows']['version'])
m = d3d['metrics']
for k in ('attach', 'patch', 'core', 'ring', 'fade', 'colour', 'relight', 'accumulate', 'capcheck', 'decal', 'age',
          'retired', 'resolve', 'refused'):
    print(k, m[k])
print('timing_us (best of 60, fenced):')
for t in m['timing']:
    print(f"  {t['width']}x{t['height']} {t['case']:<34} draws={t['patches']} lit_share={t['lit_share']:.4f} {t['gpu_us']:7.1f}")
pins = d3d.get('shell_pins', {})
print('full-shell record (bc30d8cf1):', pins)
