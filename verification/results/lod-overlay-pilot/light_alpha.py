"""Alpha statistics of the diffuse and light-map textures of argon_TL and the outpost's
coarsest record (mip 0; counts and correlation only).

  python3 verification/results/lod-overlay-pilot/light_alpha.py
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import numpy as np
import lod_overlay, bob1, body_materials, lod_atlas
assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
seen = set()
for body in ('ships/argon/argon_TL', 'Stations/others/military_outpost_middleb'):
    tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, body)))
    mats = bob1.materials(tree)
    rec = bob1.lods(tree)[-1]
    used = sorted({g['material'] for p in rec['parts'] for g in p['groups']})
    for mi in used:
        s = body_materials.slots(mats[mi])
        for slot in ('diffuse', 'light'):
            n = s.get(slot)
            if n is None or body_materials.is_null(n) or n.lower() in seen:
                continue
            seen.add(n.lower())
            img = lod_atlas.decode_dds(lod_atlas.texture_bytes(assets, n))
            a = img[:, :, 3].astype(int)
            lum = img[:, :, :3].mean(2)
            corr = np.corrcoef(a.ravel(), lum.ravel())[0, 1] if a.std() > 0 and lum.std() > 0 else float('nan')
            print(slot, body_materials.short(n), img.shape[:2], 'alpha mean', round(a.mean(), 1), 'min', a.min(), 'max', a.max(),
                  'share255', round((a == 255).mean(), 3), 'share0', round((a == 0).mean(), 3), 'corr(alpha,rgb)', round(corr, 2))
