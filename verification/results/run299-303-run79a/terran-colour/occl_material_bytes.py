"""What the engine reads for t_OcclusionTexture from an installed overlay body (read-only): per material the effect
parameter (name, SPTYPE, value), g_MatOcclStr and g_bIsDecalMap, whether the occlusion tuple equals material 0's, and per
LOD record its value, material indices and the share of points with a second UV pair (POIN flag 4).
Usage: python3 occl_material_bytes.py BODY [BODY ...]"""
import sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import bob1
KEYS = (b't_OcclusionTexture', b'g_MatOcclStr', b'g_bIsDecalMap')
for body in sys.argv[1:]:
    data, prov = bob1.load(body)
    tree = bob1.parse(data) if bob1.kind(data) == 'BOB1' else bob1.parse_text(data)
    print('==', body, prov)
    mats = bob1.materials(tree)
    ref = None
    for m in mats:
        p = {n: (t, v) for n, t, v in m.get('params', [])}
        occ = p.get(KEYS[0])
        ref = occ if ref is None else ref
        print(f"  mat{m['index']} flags={m.get('flags', 0):#x}", ' '.join(f"{k.decode()}={p.get(k)}" for k in KEYS),
              'same_as_mat0' if occ == ref else 'DIFFERS')
    for i, lod in enumerate(bob1.lods(tree)):
        f = Counter(pt[0] & 4 for pt in lod['points'])
        mi = sorted({g['material'] for part in lod['parts'] for g in part['groups']})
        print(f"  record{i} value={lod['value']} materials={mi} points={len(lod['points'])} uv2={f[4]}")
