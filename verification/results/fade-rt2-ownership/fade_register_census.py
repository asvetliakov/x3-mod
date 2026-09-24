#!/usr/bin/env python3
"""Which vertex programs of the shader-sweep inventory declare the distance-fade
registers (g_AlphaValue / g_FogClip / g_EnableFog), and the registers of the four
run214 pan-flicker families beside the fade arm's seven-row table
(docs/architecture/fade-rt2-ownership.md section 1). Host-only, no Wine."""
import json, sys
from pathlib import Path
root = Path(__file__).resolve().parents[3]
d = json.load(open(root / 'verification/results/shader-sweep-inventory.json'))
want = ['494fe349b8bc12ec', '53a0a641107ed76c', '4944d81dfe531b37', 'b0602757fce6e870', '167eb2d5629ab9d3', 'c30104cb0efb6675', '37c34a7478544c14']
rows = []
for v in d['programs']:
    if not str(v.get('model', '')).startswith('vs') and v.get('stage') not in ('vertex', 'vs'):
        continue
    regs = {c['name']: c['register'] for c in v['ctab'] if c.get('register_set') != 3}
    if 'g_FogClip' in regs:
        rows.append((v['id'], v.get('model'), regs.get('g_AlphaValue'), regs['g_FogClip'], regs.get('g_EnableFog'), v.get('runtime_captured')))
print('vertex programs declaring g_FogClip:', len(rows))
print('runtime captured among them:', sum(1 for r in rows if r[5]))
for r in rows:
    if any(r[0].startswith('vs_' + w[:13]) for w in want):
        print(r)
