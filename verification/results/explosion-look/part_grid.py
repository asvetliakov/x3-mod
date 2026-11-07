#!/usr/bin/env python3
"""part_grid.py <dump_dir> <out_dir> [part ...]: the death explosion's parts at 1920x1080 (weapon_effects_boom_cases.h:
<case>_1920x1080-<ms>-<part>.pfm, float, cleared black or over the grey hull plane) side by side over the five ages, each
crop about the centre tonemapped 1 - e^(-v) and sRGB-encoded (the composite PPM clamps a 1.5 body flat), enlarged x3.
Default parts: body, all (the sum of the in-air parts' light over black: flash, body, shock, ember, halo)."""
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

CROPS = {'boom_fighter_600m': (270, 180), 'boom_fighter_600m_hull': (270, 180), 'boom_missile_300m': (225, 150),
         'boom_nested_3_fighters': (390, 195), 'boom_eye_inside_fighter': None}


def read_pfm(p):
    with open(p, 'rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        a = np.fromfile(f, dtype='<f4' if scale < 0 else '>f4').reshape(h, w, 3)
    return np.flipud(a)


def tone(a):
    return (255 * (1 - np.exp(-np.maximum(a, 0))) ** (1 / 2.2)).clip(0, 255).astype(np.uint8)


def main(src, dst, parts):
    src, dst = Path(src), Path(dst)
    found = {}
    for p in src.glob('*_1920x1080-*-*.pfm'):
        m = re.fullmatch(r'(.+)_1920x1080-(\d+)-(\w+)', p.stem)
        if m:
            found.setdefault(m.group(1), {}).setdefault(int(m.group(2)), {})[m.group(3)] = p
    for case, ages in sorted(found.items()):
        for part in parts:
            tiles = []
            for ms in sorted(ages):
                if part == 'all':
                    a = sum(read_pfm(ages[ms][q]) for q in ('flash', 'body', 'shock', 'ember', 'halo') if q in ages[ms])
                elif part in ages[ms]:
                    a = read_pfm(ages[ms][part])
                else:
                    continue
                h, w = a.shape[:2]
                crop = CROPS.get(case, (300, 200))
                if crop:
                    a = a[h // 2 - crop[1] // 2:h // 2 + crop[1] // 2, w // 2 - crop[0] // 2:w // 2 + crop[0] // 2]
                    im = Image.fromarray(tone(a)).resize((crop[0] * 3, crop[1] * 3), Image.NEAREST)
                else:
                    im = Image.fromarray(tone(a)).resize((w // 3, h // 3), Image.BILINEAR)
                tiles.append(im)
            if not tiles:
                continue
            grid = Image.new('RGB', (sum(t.width for t in tiles) + 4 * (len(tiles) - 1), tiles[0].height), (40, 40, 48))
            x = 0
            for t in tiles:
                grid.paste(t, (x, 0))
                x += t.width + 4
            out = dst / f'{case}_1920x1080-{part}-tonemapped.png'
            grid.save(out)
            print(out.name, 'ages_ms=' + ','.join(map(str, sorted(ages))))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:] or ['body', 'all'])
