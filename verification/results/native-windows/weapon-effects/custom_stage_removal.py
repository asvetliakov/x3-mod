#!/usr/bin/env python3
"""The custom projectile stage's removal (2026-11-13; docs/archive/effects-look-2026-11-13-custom-projectile/index.md):
the native records before (git 04d518bf3) against the working tree's, per check family and the re-shade, muzzle
re-shade and lathe cost rows. Prints only the compared figures.
  python3 -I verification/results/native-windows/weapon-effects/custom_stage_removal.py"""
import collections
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
BASE = '04d518bf3'
DIR = 'verification/results/native-windows/weapon-effects'


def load(name, rev=None):
    if rev:
        text = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:{DIR}/{name}'], capture_output=True, text=True,
                              check=True).stdout
    else:
        text = (ROOT / DIR / name).read_text()
    return json.loads(text)


def family(name):
    for prefix in ('reshade', 'muzzle_reshade', 'lathe', 'projectile', 'flash', 'pulse'):
        if name.startswith(prefix):
            return prefix
    return 'other'


before, after = load('summary.json', BASE), load('summary.json')
for label, r in (('before', before), ('after', after)):
    c = collections.Counter(family(n) for n, _ in r['checks'])
    m = r['metrics']
    print(label, 'commit', r['source']['commit'][:9], 'passed', r['passed'], 'checks', len(r['checks']),
          'failed', len(r['failed_checks']), dict(sorted(c.items())),
          'rows', {k: len(m.get(k) or []) for k in ('reshade', 'muzzle_reshade', 'lathe', 'projectile', 'flash', 'pulse')})
for name in ('hook-summary.json', 'hook-fl-summary.json'):
    b, a = load(name, BASE), load(name)
    print(name, 'before', b['check_count'], b['passed'], 'after', a['check_count'], a['passed'],
          'removed', sorted({c['name'] for c in b['checks']} - {c['name'] for c in a['checks']}),
          'added', sorted({c['name'] for c in a['checks']} - {c['name'] for c in b['checks']}))
gone = sorted({n for n, _ in before['checks']} - {n for n, _ in after['checks']})
print('d3d checks removed', len(gone), 'outside projectile/flash/pulse:',
      [n for n in gone if family(n) not in ('projectile', 'flash', 'pulse')])
print('d3d checks added', sorted({n for n, _ in after['checks']} - {n for n, _ in before['checks']}))
COST = {'reshade': ('cost', 'lone_cost', 'layout_cost'), 'muzzle_reshade': ('cost',), 'lathe': ('build_cost',)}
for key, cases in COST.items():
    for case in cases:
        b = next(x for x in before['metrics'][key] if x.get('case') == case)
        a = next(x for x in after['metrics'][key] if x.get('case') == case)
        fields = [k for k in b if k.endswith('_us') or '_us_' in k or k.startswith('us_') or k.endswith('us_per_draw')
                  or k.endswith('_us_per_body_change')]
        print(key, case, {k: (b[k], a.get(k)) for k in fields})
tb = {(x['width'], x['case']): x['gpu_us'] for x in before['metrics']['timing']}
ta = {(x['width'], x['case']): x['gpu_us'] for x in after['metrics']['timing']}
print('timing lathe gpu_us', {f'{w}:{c}': (tb[(w, c)], ta.get((w, c))) for (w, c) in tb if 'lathe' in c})
