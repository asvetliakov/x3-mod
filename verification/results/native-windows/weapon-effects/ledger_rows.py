#!/usr/bin/env python3
"""The figures docs/verification/weapon-effects.md quotes, from the two native records of this directory (written by
run_weapon_effects.py --host native) and, for the cull's before/after, the step's first record at commit 2fc8762ce.
usage: python3 verification/results/native-windows/weapon-effects/ledger_rows.py"""
import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
hook = json.loads((HERE / 'hook-summary.json').read_text())
d3d = json.loads((HERE / 'summary.json').read_text())
first = json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', '2fc8762ce:' + str((HERE / 'summary.json').relative_to(ROOT))],
                                  capture_output=True, text=True, check=True).stdout)
print('hook', hook['passed'], hook['run']['result'], 'stack', hook['stack_depth'], 'frame', hook['build']['audit']['handler_frame_bytes'],
      'calls', hook['build']['audit']['handler_calls'], 'x87', len(hook['build']['audit']['handler_x87']),
      'undefined', len(hook['build']['audit']['handler_undefined']), 'commit', hook['source']['commit'])
print('d3d', d3d['passed'], d3d['run']['result'], 'commit', d3d['source']['commit'], 'adapter', d3d['host']['adapter']['description'],
      d3d['host']['adapter']['driver_version'], 'windows', d3d['host']['windows']['version'])
m = d3d['metrics']
for k in ('shell', 'rim', 'rings', 'hull', 'decal', 'age', 'retired', 'resolve', 'refused'):
    print(k, m[k])
print('timing_us (best of 60, fenced):')
for t in m['timing']:
    print(f"  {t['width']}x{t['height']} {t['case']:<22} {t['gpu_us']:7.1f}")
print('first record (2fc8762ce, CULLMODE NONE):', [(t['width'], t['gpu_us']) for t in first['metrics']['timing']])
