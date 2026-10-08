#!/usr/bin/env python3
"""Main engine nozzles per ship scene: the plate count the engine light needs (docs/architecture/engine-light.md,
"Plate cap"). Read-only on the game tree, no Wine.

Per TShips row of the installed view (every catalogue and loose file minus the x3m LOD overlay slots, as the census's
`mayhem` view) and of stock X3AP: the ship scene's parts whose body is a types/Bodies SBTYPE_JET body or lives under
effects/engines/, role `main` by the census rule (engine_effects_census.role: not an fx_engine_emitter anchor, C word
bit 0 set; RCS jets have it clear). Each such part is its own scene node, so its own node handle: one plate each
(engine_light_core.h add_plate keeps one plate per handle; a co-located smaller layer is still a plate). Scene parts
carry no parent field (flat under the ship root, the jets' node+0x18), so every main part of a scene feeds one ship.

    python3 verification/results/engine-light-plate-cap/nozzle_counts.py   # ~60 s; prints one JSON object
"""
import collections
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'engine-effects-census'))
import engine_effects_census as census  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import lod_overlay  # noqa: E402


def view(assets):
    ships, _ = census.tships(assets)
    jets = census.jet_bodies(assets)[2]
    per_scene, rows, binary, missing = {}, [], 0, 0
    for s in ships:
        key = census.norm(s['scene'])
        if key not in per_scene:
            try:
                data, _ = census.scene_member(assets, s['scene'])
            except FileNotFoundError:
                per_scene[key] = None
                missing += 1
                continue
            if data[:4] == b'CUT1':
                per_scene[key] = None
                binary += 1
                continue
            main = 0
            for p in census.scene_parts(data):
                k = census.body_key(p['body'])
                if (k in jets or k.startswith('effects/engines/')) and census.role(k, p['c']) == 'main':
                    main += 1
            per_scene[key] = main
        if per_scene[key] is not None:
            rows.append((s['id'], s['cls'], key, per_scene[key]))
    dist = collections.Counter(n for *_, n in rows)
    top = sorted(rows, key=lambda r: -r[3])[:10]
    scenes = collections.Counter(n for n in per_scene.values() if n is not None)
    return dict(ships=len(rows), scenes=sum(scenes.values()), binary_scenes=binary, missing_scenes=missing,
                max=max(dist), distribution={str(k): dist[k] for k in sorted(dist)},
                ships_above={str(c): sum(v for k, v in dist.items() if k > c) for c in (8, 16, 32, 48, 64)},
                top=[dict(id=i, cls=c, scene=sc, main=n) for i, c, sc, n in top])


def main():
    mayhem, _ = lod_overlay.original_assets(census.GAME)
    out = dict(installed=view(mayhem), stock=view(Assets(census.GAME, catalogues=STOCK_AP_CATALOGUES)))
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
