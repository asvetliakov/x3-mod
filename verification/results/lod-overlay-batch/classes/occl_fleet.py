#!/usr/bin/env python3
"""Fleet cross-check over every winning BOB1 .pbb: record-0 second-UV presence vs a body-specific t_OcclusionTexture
(basename not NONE_*/NULL) among record-0 materials, and Tex2 value per material use. Read-only."""
import re, sys
from collections import Counter
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1, lod_overlay  # noqa
assets, _ = lod_overlay.original_assets(bob1.DEFAULT_GAME)
tab = Counter(); tex2 = Counter(); both = []
for k, v in assets.entries.items():
    e = v[-1]
    if not e['path'].lower().endswith('.pbb'): continue
    try:
        d = assets.read_entry(e)
        if bob1.kind(d) != 'BOB1': continue
        t = bob1.parse(d)
    except Exception: continue
    finally: assets.cache.clear()
    r0, mats = bob1.lods(t)[0], bob1.materials(t)
    uv2 = any(p[0] & 4 for p in r0['points'])
    used = {g['material'] for p in r0['parts'] for g in p['groups'] if 0 <= g['material'] < len(mats)}
    occ = False
    for mi in used:
        for n, ty, val in mats[mi].get('params', []):
            if n.lower() == b't_occlusiontexture' and ty == 8 and not re.search(rb'^NONE_|^NULL$|^$', re.split(rb'[\\/]', val)[-1], re.I): occ = True
            if n == b'Tex2': tex2[(uv2, val[0])] += 1
    tab[(uv2, occ)] += 1
    if not uv2 and occ: both.append(e['path'])
print('bodies (has_uv2, body-specific occlusion tex in r0 mats):', dict(tab))
print('material uses (has_uv2, Tex2):', dict(tex2))
print('no-uv2 but occlusion tex:', both)
