"""Resolve textures as the engine does (lod_atlas.lookup, read-only) and report size and colour statistics:
mean RGBA, share of texels that are 'red' (R > 1.6 G and R > 1.6 B and R > 60), mean RGB of the red texels.
Usage: python3 occl_colour.py NAME [NAME ...]  (material texture names)"""
import sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1, lod_atlas
assets = bob1._archive_modules().Assets(Path(bob1.DEFAULT_GAME))
for name in sys.argv[1:]:
    src = lod_atlas.texture_source(assets, name.encode())
    if src is None:
        print(name, 'no texture'); continue
    data, kind, info = src
    img = lod_atlas.decode_dds(data) if kind == 'dds' else lod_atlas.decode_image(data, name)
    img = np.asarray(img)
    if img.dtype != np.uint8:
        img = np.clip(img * (255 if img.max() <= 1.0 else 1), 0, 255).astype(np.uint8)
    rgb = img[..., :3].astype(np.float32)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    red = (r > 1.6 * g) & (r > 1.6 * b) & (r > 60)
    mr = rgb[red].mean(0) if red.any() else [0, 0, 0]
    print(name.split('\\')[-1], info['source'] + ':' + info['member'], 'placeholder=%s' % info['placeholder'],
          '%dx%d' % (img.shape[1], img.shape[0]), 'mean_rgba=%s' % np.round(img.reshape(-1, img.shape[-1]).mean(0), 1).tolist(),
          'red_share=%.3f' % red.mean(), 'red_mean_rgb=%s' % np.round(mr, 1).tolist())
