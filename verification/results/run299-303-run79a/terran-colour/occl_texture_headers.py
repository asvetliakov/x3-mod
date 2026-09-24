"""DDS header (size, mip count, FourCC) and mean RGBA of textures resolved as the engine does (lod_atlas, read-only).
Identifies the 32x32 / 6-level / DXT5 texture bound at s5 on every lod>0 draw as dds/NONE_OCCL_DECAL.pck.
Usage: python3 occl_texture_headers.py NAME [NAME ...]"""
import sys, struct
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import bob1, lod_atlas
assets = bob1._archive_modules().Assets(Path(bob1.DEFAULT_GAME))
for name in sys.argv[1:]:
    data, kind, info = lod_atlas.texture_source(assets, name.encode())
    h, w, _, _, mips = struct.unpack_from('<5I', data, 12)
    img = np.asarray(lod_atlas.decode_dds(data)).astype(np.float64)
    if img.max() <= 1.0: img *= 255
    print(name, f"{info['source']}:{info['member']}", f'{w}x{h}', 'mips', mips, 'fourcc', data[84:88].decode(),
          'mean_rgba', np.round(img.reshape(-1, img.shape[-1]).mean(0), 1).tolist(),
          'min_alpha', round(float(img[..., 3].min()), 1))
