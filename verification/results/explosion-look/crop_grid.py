#!/usr/bin/env python3
"""crop_grid.py <dump_dir> <out_dir>: the death explosion's judging grids (docs/architecture/explosion-look.md section 9)
from the native fixture's stage dump (weapon_effects_boom_cases.h: <case>_<W>x<H>-<ms>.ppm, the scene target's linear
values clamped to [0, 1]): per case and size one PNG of the five ages side by side, each a crop about the frame's centre
(the full frame for the eye-inside case), shown sRGB-encoded (v^(1/2.2): the dump has no tonemap) and enlarged by an
integer factor so a 1080p crop reads at the size it has on a 1440-row screen. Prints one row per grid."""
import re
import sys
from pathlib import Path

from PIL import Image

CROPS = {'boom_fighter_600m': (360, 240), 'boom_fighter_600m_hull': (360, 240), 'boom_missile_300m': (300, 200),
         'boom_nested_3_fighters': (520, 260), 'boom_eye_inside_fighter': None}


def load(p):
    im = Image.open(p).convert('RGB')
    return im.point(lambda v: round(255 * (v / 255) ** (1 / 2.2)))


def main(src, dst):
    src, dst = Path(src), Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    frames = {}
    for p in src.glob('*.ppm'):
        m = re.fullmatch(r'(.+)_(\d+)x(\d+)-(\d+)', p.stem)
        if m:
            frames.setdefault((m.group(1), int(m.group(2)), int(m.group(3))), []).append((int(m.group(4)), p))
    for (case, w, h), items in sorted(frames.items()):
        items.sort()
        crop = CROPS.get(case, (400, 260))
        tiles = []
        for ms, p in items:
            im = load(p)
            if crop:
                cw, ch = crop if h == 1440 else (int(crop[0] * 0.75), int(crop[1] * 0.75))
                im = im.crop((w // 2 - cw // 2, h // 2 - ch // 2, w // 2 + cw // 2, h // 2 + ch // 2))
                k = 2 if h == 1440 else 3
                im = im.resize((im.width * k, im.height * k), Image.NEAREST)
            else:
                im = im.resize((w // 4, h // 4), Image.BILINEAR)
            tiles.append(im)
        W = sum(t.width for t in tiles) + 4 * (len(tiles) - 1)
        grid = Image.new('RGB', (W, max(t.height for t in tiles)), (40, 40, 48))
        x = 0
        for t in tiles:
            grid.paste(t, (x, 0))
            x += t.width + 4
        out = dst / f'{case}_{w}x{h}.png'
        grid.save(out)
        print(out.name, 'ages_ms=' + ','.join(str(ms) for ms, _ in items), f'size={grid.width}x{grid.height}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
