#!/usr/bin/env python3
"""Bodies the installed record bakes (x3m-lod-batch.json, eligible) whose source record draws a visible, non-alpha
effect material with a NULL t_DiffuseTexture: the only input lod_atlas.NULL_DIFFUSE_TEXEL changes on the effect
path (black until 2026-09-29, the NONE_GRAY texel since). Written by the reviewer of the classic material bake
(2026-09-29); defaults added. Read-only.

  python3 nonfx_bake_null_diffuse_scan.py [WORKTREE] [GAME] > nonfx_bake_null_diffuse_scan_out.txt

Per body: (material, diffuse string, effect, faces of one drawing group). The scan names the bodies whose diffuse
atlas holds a NULL-diffuse tile; it does not bake them."""
import json, sys
from pathlib import Path
W = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3]
sys.path[:0] = [str(W / 'tools/analysis'), str(W / 'verification/probe')]
import bob1, lod_atlas, lod_overlay, body_materials
GAME = Path(sys.argv[2]) if len(sys.argv) > 2 else bob1.DEFAULT_GAME
rec = json.load(open(GAME / 'addon/x3m-lod-batch.json'))
assets = lod_overlay.original_assets(GAME)[0]
by = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in assets.entries.values() for e in lst}
hits, missing, errs, n = [], 0, 0, 0
for row in rec['bodies']:
    if not row.get('eligible'):
        continue
    n += 1
    e = by.get(row['member'].lower())
    if e is None:
        missing += 1; continue
    try:
        data = assets.read_entry(e)
        tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if e['path'].lower().endswith(('.pbb', '.bob')) else bob1.parse_text(data)
    except Exception:
        errs += 1; continue
    finally:
        assets.cache.clear()
    mats = bob1.materials(tree)
    r = bob1.lods(tree)[row.get('source_record', 0) or 0]
    alpha = lod_overlay.alpha_materials(mats, assets, record=r)
    ms = set()
    for p in r['parts']:
        if p['flags'] & lod_atlas.HIDDEN_PART: continue
        for g in p['groups']:
            m = g['material']
            if 0 <= m < len(mats) and m not in alpha and 'params' in mats[m] and g['faces']:
                d = body_materials.slots(mats[m]).get('diffuse', b'?')
                if d is not None and body_materials.is_null(d):
                    ms.add((m, d, mats[m].get("effect"), len(g["faces"])))
    if ms:
        hits.append((row['name'], sorted(ms)))
print('eligible', n, 'missing', missing, 'parse_errors', errs, 'bodies with a drawn NULL-diffuse effect material', len(hits))
for h in hits: print(' ', *h)
