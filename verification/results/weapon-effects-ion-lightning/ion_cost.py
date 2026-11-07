#!/usr/bin/env python3
"""The ion lightning's cost (docs/verification/weapon-effects.md "Beam colour and ion lightning"): the ion and beam
CLASS_TIMING rows, the program on every pixel (filament_alone_fills_frame), the 64-hit CPU row and the ion / beam gate
rows, from two run_weapon_effects.py --host native records (before, after), with the after / before ratio.
The before record is the committed summary at d41a60f01 (the noise filament glow):
  git show d41a60f01:verification/results/native-windows/weapon-effects/summary.json > before-summary.json
usage: python3 verification/results/weapon-effects-ion-lightning/ion_cost.py BEFORE.json AFTER.json"""
import json
import sys


def load(path):
    s = json.load(open(path))
    t = {}
    for r in s['metrics']['class_timing']:
        if 'gpu_us' in r and (r.get('class') in ('ion', 'beam') or r['case'] == 'filament_alone_fills_frame'):
            t[(r['width'], r.get('class', '-'), r['case'])] = r
    cpu = {r['case']: r['best_us'] for r in s['metrics']['class_cpu']}
    return s, t, cpu


bs, bt, bc = load(sys.argv[1])
as_, at, ac = load(sys.argv[2])
print('before', bs['source']['commit'], 'passed', bs['passed'], '| after', as_['source']['commit'], 'passed', as_['passed'],
      'failed', as_['failed_checks'])
for key in sorted(bt):
    b, a = bt[key], at.get(key)
    if not a:
        continue
    print(f"{key[0]:>5} {key[1]:<5} {key[2]:<28} filaments {b.get('filaments')}->{a.get('filaments')} "
          f"screens {b.get('screens')}->{a.get('screens')} gpu {b['gpu_us']:.1f}->{a['gpu_us']:.1f} us x{a['gpu_us'] / b['gpu_us']:.2f}")
for case in bc:
    if case in ac:
        print(f'cpu {case:<32} {bc[case]:.1f}->{ac[case]:.1f} us')
for r in as_['metrics']['class']:
    if r.get('case') in ('beam_colour', 'ion_law', 'ion_lightning', 'ion_profile', 'ion_no_heat'):
        print('row', json.dumps(r))
print('slots', as_['metrics']['class_slots'][0])
