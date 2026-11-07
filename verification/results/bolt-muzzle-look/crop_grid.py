"""crop_grid.py <png_dir> <out.png> [W x H]: the step-4 dump's crop grid (600x360 centre crops; one row per scenario,
one column per dumped time), from the PNGs ppm2png.py wrote (<case>_<W>x<H>-<ms>.png). Pillow."""
import re
import sys
from pathlib import Path
from PIL import Image, ImageDraw

src, out = Path(sys.argv[1]), Path(sys.argv[2])
size = sys.argv[3] if len(sys.argv) > 3 else '1920x1080'
rows = {}
for p in sorted(src.glob(f'*_{size}-*.png')):
    m = re.fullmatch(rf'(.+)_{size}-(\d+)', p.stem)
    if m:
        rows.setdefault(m[1], []).append((int(m[2]), p))
cw, ch, label = 600, 360, 18
order = sorted(rows)
grid = Image.new('RGB', (cw * 4, (ch + label) * len(order)), (40, 40, 40))
draw = ImageDraw.Draw(grid)
for r, case in enumerate(order):
    for c, (ms, p) in enumerate(sorted(rows[case])[:4]):
        im = Image.open(p)
        x0, y0 = (im.width - cw) // 2, (im.height - ch) // 2
        grid.paste(im.crop((x0, y0, x0 + cw, y0 + ch)), (c * cw, r * (ch + label) + label))
        draw.text((c * cw + 4, r * (ch + label) + 2), f'{case} {size} t={ms} ms', fill=(230, 230, 230))
grid.save(out)
print('grid', out, 'rows', len(order), 'size', grid.size)
