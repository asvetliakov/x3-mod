"""Crop the ion single-hit dump frames (1920x1080) about the hit, write one labelled grid PNG plus per-frame crops,
print the lit-pixel stats of each crop, the glow-only PFM's blue/red ratio and its lit radius."""
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

d = Path(sys.argv[1])
ages = [40, 200, 504, 904, 1144]
W, H = 1920, 1080
cx, cy, half = W // 2, H // 2, 200
tiles = []
for a in ages:
    img = Image.open(d / f'ion_single_{W}x{H}-{a}.ppm').convert('RGB')
    crop = img.crop((cx - half, cy - half, cx + half, cy + half))
    arr = np.asarray(crop).astype(float) / 255.0
    lum = arr @ [0.2126, 0.7152, 0.0722]
    lit = lum > 0.02
    tiles.append(crop.resize((2 * half, 2 * half)))
    g = d / f'ion_single_{W}x{H}-{a}-glow.pfm'
    gl = ''
    if g.exists():
        raw = g.read_bytes()
        hdr_end = 0
        for _ in range(3):
            hdr_end = raw.index(b'\n', hdr_end) + 1
        px = np.frombuffer(raw[hdr_end:], dtype='<f4').reshape(H, W, 3)
        glum = px @ [0.2126, 0.7152, 0.0722]
        on = glum > 1e-4
        ys, xs = np.nonzero(on)
        rad = float(np.max(np.hypot(xs - cx, ys - cy))) if len(xs) else 0.0
        tot = px[on].sum(axis=0) if on.any() else np.zeros(3)
        gl = f' glow_px={int(on.sum())} glow_radius_px={rad:.0f} glow_rgb_ratio={tot[0]/max(tot[2],1e-9):.3f},{tot[1]/max(tot[2],1e-9):.3f},1 glow_peak={float(glum.max()):.3f}'
    print(f'age={a} lit_px={int(lit.sum())} mean_lum_lit={float(lum[lit].mean()) if lit.any() else 0:.3f} '
          f'max_lum={float(lum.max()):.3f} saturated_px={int((arr.max(axis=2) >= 0.999).sum())}{gl}')
grid = Image.new('RGB', (len(tiles) * 2 * half + (len(tiles) - 1) * 6, 2 * half + 24), (40, 40, 40))
dr = ImageDraw.Draw(grid)
for i, (a, t) in enumerate(zip(ages, tiles)):
    x = i * (2 * half + 6)
    grid.paste(t, (x, 24))
    dr.text((x + 6, 6), f'{a} ms', fill=(230, 230, 230))
out = Path(sys.argv[2])
grid.save(out / 'ion_single_1080p_grid.png')
print('grid', out / 'ion_single_1080p_grid.png')
