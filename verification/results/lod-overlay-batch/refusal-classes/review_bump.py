import sys, numpy as np
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import lod_overlay, lod_atlas
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets,_=lod_overlay.original_assets(GAME)
T=lod_atlas.Textures(assets)
for n in [b'environments\\asteroids\\asteroids_01_bump.tga', b'environments\\asteroids\\asteroids_01_diff.tga', b'environments\\asteroids\\asteroids_01_decal.tga', b'environments\\planets\\planets_haze_red_dark_diff.tga', b'x38\\Stockmarket_ad.tga', b'others\\glass_variations_diff.tga']:
    try:
        data,kind,info=lod_atlas.texture_source(assets,n)
        fmt=lod_atlas.dds_format(data) if kind=='dds' else kind
        a=np.asarray(T.get(n)).astype(float)
        print(n.decode(), info['member'], fmt, a.shape, 'meanRGBA', np.round(a.reshape(-1,a.shape[-1]).mean(0),1), 'R==G %.2f'%np.mean(a[...,0]==a[...,1]), 'A std %.1f'%a[...,-1].std())
    except Exception as e: print(n,'ERR',type(e).__name__,e)
