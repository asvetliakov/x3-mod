"""Khaak_M6Main atlas after the engine texture lookup: do the specular and bump tiles carry the diffuse image?

texture-lookup.md section 2: 25_spec.jpg and 260_bump.jpg are Materials ids 25 and 260, so the engine binds the
diffuse image (tex/true/<n>.jpg) to the specular and bump slots. Reads a lod_overlay.py --batch --out DIR run over
ships/M6/Khaak_M6Main (a scratch directory, never the bottle): the marker's tiles (sources, origin, content) and
the written atlas members. Per tile it prints the slot sources and, over the tile content at mip 0:
  spec: mean |specular RGB - diffuse RGB| (both colour slots baked from the same member when the sources agree);
  bump: mean |bump G - f(diffuse G)|, f the baker's normal transform of a jpg read as a swizzled normal map
        (A = 255 -> x = 1, y = 2G/255 - 1, z = 0 after the clip; normalised; G_out = (y / |v| + 1) * 127.5).
Tiles whose spec/bump source differs from the diffuse source are the control.

    python3 tools/analysis/lod_overlay.py --batch --only <file naming ships/M6/Khaak_M6Main> --out <DIR> --jobs 1
    PYTHONPATH=tools/analysis python3 verification/results/lod-overlay-batch/khaak_spec_bump_check.py <DIR> \
        > verification/results/lod-overlay-batch/khaak_spec_bump_check_out.txt
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402


def members(cat):
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    return {e['path']: unpack(raw[e['offset']:e['offset'] + e['size']]) for e in read_catalogue(cat)}


def main(out):
    (marker,) = sorted(Path(out, 'addon').glob('*.x3m-lod.json'))
    body = next(b for b in json.loads(marker.read_text())['bodies'] if b['name'].endswith('Khaak_M6Main'))
    files = members(marker.with_name(marker.name.split('.')[0] + '.cat'))
    atlas = {t['slot']: lod_atlas.decode_dds(files[t['member']]).astype(float) for t in body['atlas']['textures']}
    print(f'{marker.name}: body {body["name"]}, atlas slots {sorted(atlas)}, size {body["atlas"]["size"]}')
    for i, t in enumerate(body['atlas']['tiles']):
        (x, y), (w, h) = t['origin'], t['content']
        box = {s: a[y:y + h, x:x + w] for s, a in atlas.items()}
        src = t['sources']
        spec = np.abs(box['specular'][..., :3] - box['diffuse'][..., :3]).mean() if 'specular' in box else None
        g = box['diffuse'][..., 1] * (2 / 255) - 1
        bump = np.abs(box['bump'][..., 1] - (g / np.sqrt(1 + g * g) + 1) * 127.5).mean() if 'bump' in box else None
        same = lambda s: 'same' if src.get(s) == src['diffuse'] else 'other'
        print(f'tile {i} mats {t["mats"]} names {t["names"]}\n'
              f'  sources {src}\n'
              f'  specular {same("specular")} source: mean |spec - diff| RGB {spec:.2f}; '
              f'bump {same("bump")} source: mean |bump G - f(diff G)| {bump:.2f}; diffuse RGB std '
              f'{box["diffuse"][..., :3].std():.1f}')


if __name__ == '__main__':
    main(sys.argv[1])
