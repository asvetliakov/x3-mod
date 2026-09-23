#!/usr/bin/env python3
"""Per diffuse texture of record 0: faces whose own UV extent (after the integer shift) exceeds 8
periods, their area share of that texture's faces, the widest face (extent, area, UVs). Read-only.

  python3 verification/results/lod-overlay-batch/uv_outlier_faces.py BODY... > uv_outlier_faces_out.txt
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1            # noqa: E402
import body_materials  # noqa: E402
import lod_atlas       # noqa: E402
import lod_overlay     # noqa: E402

assets = lod_overlay.original_assets(bob1.DEFAULT_GAME)[0]
for name in sys.argv[1:]:
    won = bob1.resolve_body(assets, name)
    tree = bob1.parse(assets.read_entry(won), lod_overlay.MAX_TRAILING)
    r0, mats = bob1.lods(tree)[0], bob1.materials(tree)
    pts, by = r0['points'], {}
    for part in r0['parts']:
        for g in part['groups']:
            d = lod_atlas.material_slots(mats[g['material']]).get('diffuse') if 'params' in mats[g['material']] else None
            if d is None:
                continue
            for f in g['faces']:
                uvs = [lod_atlas.point_uv(pts[i]) for i in f[:3]]
                if None in uvs:
                    continue
                su, sv = lod_atlas.face_shift(uvs)
                ext = max(max(u for u, _ in uvs) - su, max(v for _, v in uvs) - sv)
                by.setdefault(d, []).append((ext, body_materials.face_area(pts, f), uvs))
    for d, fs in by.items():
        big = [x for x in fs if x[0] > 8]
        if not big:
            continue
        total = sum(x[1] for x in fs)
        e, a, uvs = max(big, key=lambda x: x[0])
        print(f'{name} {d.decode("latin1")} faces {len(fs)} extent>8 {len(big)} (>256 {sum(1 for x in big if x[0] > 256)})'
              f' area share {sum(x[1] for x in big) / total:.4f} widest {e:.0f} periods area {a:.0f}'
              f' uv {[(round(u, 1), round(v, 1)) for u, v in uvs]}')
