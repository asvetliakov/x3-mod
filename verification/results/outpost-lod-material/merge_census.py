#!/usr/bin/env python3
"""Read-only census of installed overlay bodies (full mount, addon/13..15 win): for each body, the atlas
material(s) of record 1 (C) and, over record 0's faces of materials with alpha test and blend off (the opaque
set the atlas absorbs; kept/glow groups are not separated, so shares are upper bounds), the face share whose
g_CullMode differs from the atlas material's, and the g_MatDiffuseStrength range against the atlas value.
Usage (repo root): python3 .../merge_census.py GAME BODY [BODY ...]"""
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1

def num(m):
    return {n.decode('latin1'): v for n, t, v in m.get('params', ()) if t != 8}

def tex(m):
    return {n.decode('latin1'): v.decode('latin1') for n, t, v in m.get('params', ()) if t == 8}

game = Path(sys.argv[1]); assets = bob1._archive_modules().Assets(game)
for name in sys.argv[2:]:
  try:
      e = bob1.resolve_body(assets, name)
      tree = bob1.parse(assets.read_entry(e), None)
      mats = bob1.materials(tree); recs = bob1.lods(tree)
      if len(recs) < 2:
          print(name, 'single record'); continue
      c_use = {}
      for p in recs[1]['parts']:
          for g in p['groups']:
              c_use[g['material']] = c_use.get(g['material'], 0) + len(g['faces'])
      atl = [m for m in c_use if 0 <= m < len(mats) and 'x3m_lod' in tex(mats[m]).get('t_DiffuseTexture', '')]
      if not atl:
          print(f'{name} ({e["source"]}): no atlas material in record 1'); continue
      am = num(mats[atl[0]])
      acull = am.get('g_CullMode', [None])[0]; adiff = am.get('g_MatDiffuseStrength', [0])[0] / 65536
      use = {}
      for p in recs[0]['parts']:
          for g in p['groups']:
              use[g['material']] = use.get(g['material'], 0) + len(g['faces'])
      op = {m: n for m, n in use.items() if 0 <= m < len(mats) and not num(mats[m]).get('g_ALPHATESTENABLE', [0])[0]
            and not num(mats[m]).get('g_AlphaBlendEnable', [0])[0]}
      tot = sum(op.values()) or 1
      other = sum(n for m, n in op.items() if num(mats[m]).get('g_CullMode', [None])[0] != acull)
      ds = [num(mats[m]).get('g_MatDiffuseStrength', [0])[0] / 65536 for m in op] or [0.0]
      off = sum(n for m, n in op.items() if abs(num(mats[m]).get('g_MatDiffuseStrength', [0])[0] / 65536 - adiff) > 0.25 * max(adiff, 1e-6))
      print(f'{name} ({e["source"]}): atlas mats {atl} cull {acull} diffuse {adiff:.3f}; record0 opaque faces {tot}; '
            f'cull differs {other / tot:.1%}; diffuse range {min(ds):.2f}..{max(ds):.2f}; faces >25% off the merged diffuse {off / tot:.1%}')
  except Exception as ex:
    print(f'{name}: error {type(ex).__name__}: {ex}')
