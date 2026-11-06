#!/usr/bin/env python3
"""The step-2 (hull hit, docs/architecture/weapon-look.md 3.1) figures docs/verification/weapon-effects.md "Hull hit,
step 2" quotes, from the two native records run_weapon_effects.py --host native writes into
verification/results/native-windows/weapon-effects/: the hook fixture's shields,hull checks and rows, the build audit,
and the D3D fixture's HULL rows (placement, the hidden half, the glow, the cooling, the per-tier law, the floors, the
three-frame floor, no pop, far side, chase nose, cockpit, variants, velocity, orphan, budget, R32F lane, resolve, Reset)
with the HULL_CPU and HULL_TIMING rows and the slots.
usage: python3 verification/results/weapon-effects/hull_figures.py"""
import json
from pathlib import Path

RESULTS = Path(__file__).resolve().parents[1] / 'native-windows/weapon-effects'
hook = json.loads((RESULTS / 'hook-summary.json').read_text())
d3d = json.loads((RESULTS / 'summary.json').read_text())
a = hook['build']['audit']
# step 2's hook checks: the shields,hull block and the ring's eviction rule (hull_forwards_in_shields is step 1's)
STEP1 = {'hull_forwards_in_shields'}
hull_checks = [c for c in hook['checks'] if (c['name'].startswith(('hull_', 'drained_hull')) and c['name'] not in STEP1)
               or c['name'] == 'overflow_census_first_then_oldest_staged']
print('hook', hook['passed'], hook['run']['result'], 'hull_checks', len(hull_checks),
      'failed', [c['name'] for c in hull_checks if not c['passed']], 'calls', a['handler_calls'], 'x87',
      len(a['handler_x87']), 'instructions', a['handler_instructions'], 'frame', a['handler_frame_bytes'],
      'commit', hook['source']['commit'])
for row in hook.get('hull_mode', []):
    print('hull_mode', row)
print('event_cost', hook['event_cost'])
# step 2's D3D gates (hull_walk_measured is the ship bubble's, step 1)
hull_gates = [(n, ok) for n, ok in d3d['checks'] if (n.startswith(('hull_', 'velocity_', 'shields_mode_')) and
                                                     n != 'hull_walk_measured')]
print('d3d', d3d['passed'], d3d['run']['result'], 'hull_gates', len(hull_gates), 'failed',
      [n for n, ok in hull_gates if not ok], 'commit', d3d['source']['commit'], 'adapter',
      d3d['host']['adapter']['description'], d3d['host']['adapter']['driver_version'])
m = d3d['metrics']
print('slots', m['hull_slots'])
for row in m['hull']:
    print('hull', row)
for row in m['hull_cpu']:
    print('cpu', row)
print('timing_us (best of 60, fenced):')
for t in m['hull_timing']:
    print(f"  {t['width']}x{t['height']} {t['case']:<24} sparks={t['sparks']} rects={t['boxes']} "
          f"lit_share={t['lit_share']:.4f} raster_share={t['raster_share']:.4f} gpu_us={t['gpu_us']}")
glow = next(t for t in m['hull_timing'] if t['width'] == 5120 and t['case'] == 'glow_alone_fills_frame')
print('glow budget weight (5120 full screen / decal 177.5 us):', round(glow['gpu_us'] / 177.5, 3))
