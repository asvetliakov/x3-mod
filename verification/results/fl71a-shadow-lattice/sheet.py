#!/usr/bin/env python3
"""Contact sheet: per frame, hdr thumbnail | nearest occlusion | cascade map, scaled to 640x180 each.
usage: sheet.py OUT FRAME..."""
import sys
from PIL import Image
out, frames = sys.argv[1], sys.argv[2:]
tiles = []
for f in frames:
    row = [Image.open(p).convert('RGB').resize((640, 180)) for p in (f'hdr_{f}.png', f'occ_{f}_occl.png', f'occ_{f}_casc.png')]
    tiles.append(row)
S = Image.new('RGB', (640 * 3, 180 * len(tiles)))
for i, row in enumerate(tiles):
    for j, im in enumerate(row): S.paste(im, (640 * j, 180 * i))
S.save(out)
