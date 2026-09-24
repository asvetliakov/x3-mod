"""Post-install checks on the installed overlay, bottle X3 read-only, members read by seek (no whole-dat decode).

1. Terran red tile: atlas_tile_check.py's comparison (each tile vs its own source area-resampled over its span,
   other tiles' sources as controls; mean |RGB diff| 0..255) on the installed body records matching the given
   substrings (default usc_dock_e_tower).
2. Khaak_M6Main spec/bump: khaak_spec_bump_check.py's per-tile comparison on the installed record.

    python3 verification/results/lod-overlay-batch/install-fleet3/installed_checks.py [NAME_SUBSTRING ...] \
        > verification/results/lod-overlay-batch/install-fleet3/installed_checks_out.txt
"""
import importlib.util
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
spec = importlib.util.spec_from_file_location(
    'atc', ROOT / 'verification/results/run299-303-run79a/terran-colour/atlas_tile_check.py')
atc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(atc)


class Members:
    def __init__(self, markers):
        self.index = {}
        for m in markers:
            cat = m.with_name(m.name.split('.')[0] + '.cat')
            for e in read_catalogue(cat):
                self.index[e['path']] = (cat.with_suffix('.dat'), e['offset'], e['size'])

    def get(self, path):
        dat, off, size = self.index[path]
        with open(dat, 'rb') as f:
            f.seek(off)
            raw = np.frombuffer(f.read(size), np.uint8) ^ 0x33
        return unpack(raw.tobytes())


def bodies(markers):
    for m in markers:
        for b in json.loads(m.read_text())['bodies']:
            yield m, b


def terran(markers, mem, subs):
    assets, _ = lod_overlay.original_assets(GAME)
    textures = lod_atlas.Textures(assets)
    for m, b in bodies(markers):
        if not any(s in b['name'].lower() for s in subs):
            continue
        atlas = b['atlas']
        diff = next(x for x in atlas['textures'] if x['slot'] == 'diffuse')
        img = lod_atlas.decode_dds(mem.get(diff['member'])).astype(np.float32)
        tiles = atlas['tiles']
        srcs = [textures.get(t['names']['diffuse'].encode('latin1')) for t in tiles]
        print(f'{m.name} {b["name"]}: atlas {atlas["size"]} {diff["format"]} tiles {len(tiles)} groups {b.get("groups")}')
        for t in tiles:
            (cx, cy), (cw, ch) = t['origin'], t['content']
            got = img[cy:cy + ch, cx:cx + cw, :3]
            row = []
            for u, s in zip(tiles, srcs):
                if s is None:
                    continue
                d = float(np.abs(got - atc.resampled(s, t)[:, :, :3]).mean())
                row.append(f'{"OWN " if u is t else ""}{atc.short(u["names"]["diffuse"])} {d:.2f}')
            print(f'  mats {t["mats"]} tile {cw}x{ch} at {cx},{cy} texels/px {t["texels_per_px"]}: ' + '; '.join(row))


def khaak(markers, mem):
    found = next(((m, b) for m, b in bodies(markers) if b['name'].endswith('Khaak_M6Main')), None)
    if found is None:
        print('Khaak_M6Main: not in the installed overlay')
        return
    m, body = found
    atlas = {t['slot']: lod_atlas.decode_dds(mem.get(t['member'])).astype(float) for t in body['atlas']['textures']}
    print(f'{m.name}: body {body["name"]}, atlas slots {sorted(atlas)}, size {body["atlas"]["size"]}')
    for i, t in enumerate(body['atlas']['tiles']):
        (x, y), (w, h) = t['origin'], t['content']
        box = {s: a[y:y + h, x:x + w] for s, a in atlas.items()}
        src = t['sources']
        sp = np.abs(box['specular'][..., :3] - box['diffuse'][..., :3]).mean() if 'specular' in box else float('nan')
        g = box['diffuse'][..., 1] * (2 / 255) - 1
        bp = (np.abs(box['bump'][..., 1] - (g / np.sqrt(1 + g * g) + 1) * 127.5).mean()
              if 'bump' in box else float('nan'))
        same = lambda s: 'same' if src.get(s) == src['diffuse'] else 'other'  # noqa: E731
        print(f'tile {i} mats {t["mats"]} diffuse {src["diffuse"]}: specular {same("specular")} source '
              f'mean |spec - diff| RGB {sp:.2f}; bump {same("bump")} source mean |bump G - f(diff G)| {bp:.2f}')


def main(subs):
    markers = sorted((GAME / 'addon').glob('*.x3m-lod.json'))
    mem = Members(markers)
    print('# terran red tile')
    terran(markers, mem, subs or ['usc_dock_e_tower'])
    print('# khaak spec/bump')
    khaak(markers, mem)


if __name__ == '__main__':
    main([s.lower() for s in sys.argv[1:]])
