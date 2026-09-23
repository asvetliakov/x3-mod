"""NULL texture slots per effect over every installed BOB1 body (read-only; counts only).

Prints (effect, technique, slot, NULL|set) counts for argon.fx, argon2s.fx and every
combination above 200, plus the first body with a NULL bump / specular on argon.fx.

  python3 verification/results/lod-overlay-pilot/null_slots.py
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import lod_overlay, bob1, body_materials
from collections import Counter
assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
keys = sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith('.pbb'))
c = Counter()
ex = {}
for k in keys:
    e = assets.entries[k][-1]
    d = assets.read_entry(e); assets.cache.clear()
    if bob1.kind(d) != 'BOB1':
        continue
    try:
        t = bob1.parse(d)
    except bob1.FormatError:
        continue
    for m in bob1.materials(t):
        if 'params' not in m:
            continue
        s = body_materials.slots(m)
        eff = m['effect'].decode('latin1').lower()
        tech = m['technique']
        for slot in ('bump', 'specular', 'light', 'alpha'):
            if slot in s:
                nul = body_materials.is_null(s[slot])
                c[(eff, tech, slot, 'NULL' if nul else 'set')] += 1
                if nul and slot in ('bump', 'specular') and (eff, slot) not in ex:
                    ex[(eff, slot)] = e['path']
for k, v in sorted(c.items()):
    if k[0] in ('argon.fx', 'argon2s.fx') or v > 200:
        print(k, v)
for k, v in ex.items():
    if k[0] in ('argon.fx',):
        print('example', k, v)
