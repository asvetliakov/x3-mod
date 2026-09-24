"""Atlas tile check after the alpha-rule fix: for each body of a scratch bake (`lod_overlay.py --batch --only <list>
--out <dir>`), decode the written diffuse atlas (level 0) and compare every tile's content with its own source
diffuse area-resampled over the tile's UV span (lod_atlas.level_weights, the baker's filter), and with the other
tiles' sources as controls. Mean |difference| over RGB, 0..255. Bottle X3 read-only.

    python3 atlas_tile_check.py <bake dir> [NAME_SUBSTRING ...] > atlas_tile_check.txt
"""
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def members(bake):
    out = {}
    for cat in sorted((Path(bake) / 'addon').glob('*.cat')):
        raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
        for e in read_catalogue(cat):
            out[e['path']] = (raw, e['offset'], e['size'])
    return out


def resampled(src, t):
    H, W = src.shape[:2]
    (cx, cy), (cw, ch) = t['origin'], t['content']
    my = lod_atlas.level_weights(cy, cy + ch, 1, cy, ch, t['lo'][1], t['span'][1], H)
    mx = lod_atlas.level_weights(cx, cx + cw, 1, cx, cw, t['lo'][0], t['span'][0], W)
    return np.tensordot(np.tensordot(my, src.astype(np.float32), axes=(1, 0)), mx, axes=(1, 1)).transpose(0, 2, 1)


def short(name):
    return name.replace('/', '\\').rsplit('\\', 1)[-1]


def main(bake, subs):
    assets, _ = lod_overlay.original_assets(GAME)
    textures = lod_atlas.Textures(assets)
    mem = members(bake)
    for marker in sorted((Path(bake) / 'addon').glob('*.x3m-lod.json')):
        for b in json.loads(marker.read_text())['bodies']:
            if subs and not any(s in b['name'].lower() for s in subs):
                continue
            atlas = b['atlas']
            diff = next(x for x in atlas['textures'] if x['slot'] == 'diffuse')
            raw, off, size = mem[diff['member']]
            img = lod_atlas.decode_dds(unpack(raw[off:off + size])).astype(np.float32)
            tiles = atlas['tiles']
            srcs = [textures.get(t['names']['diffuse'].encode('latin1')) for t in tiles]
            print(f'{b["name"].rsplit("/", 1)[-1]}: atlas {atlas["size"]} {diff["format"]} tiles {len(tiles)}'
                  f' groups {b.get("groups")}')
            for t in tiles:
                (cx, cy), (cw, ch) = t['origin'], t['content']
                got = img[cy:cy + ch, cx:cx + cw, :3]
                row = []
                for u, s in zip(tiles, srcs):
                    if s is None:
                        continue
                    d = float(np.abs(got - resampled(s, t)[:, :, :3]).mean())
                    row.append(f'{"OWN " if u is t else ""}{short(u["names"]["diffuse"])} {d:.2f}')
                print(f'  mats {t["mats"]} tile {cw}x{ch} at {cx},{cy} texels/px {t["texels_per_px"]}'
                      f' mean RGB {np.round(got.reshape(-1, 3).mean(0), 1).tolist()}: ' + '; '.join(row))


if __name__ == '__main__':
    main(sys.argv[1], [s.lower() for s in sys.argv[2:]])
