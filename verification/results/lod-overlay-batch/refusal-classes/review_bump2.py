import sys, numpy as np
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import lod_overlay, lod_atlas
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets,_=lod_overlay.original_assets(GAME)
T=lod_atlas.Textures(assets)
for n in [b'environments\\asteroids\\asteroids_01_bump.tga', b'ships\\argon\\argon_m3_bump.tga']:
    try:
        a=np.asarray(T.get(n)).astype(float).reshape(-1,4)
        c=np.corrcoef(a.T)
        x=a[:,3]*2/255-1; y=a[:,1]*2/255-1; r=a[:,0]*2/255-1; b=a[:,2]*2/255-1
        print(n.decode(),'G==B %.2f R==B %.2f'%(np.mean(a[:,1]==a[:,2]),np.mean(a[:,0]==a[:,2])),'corr RA %.2f GA %.2f RG %.2f GB %.2f'%(c[0,3],c[1,3],c[0,1],c[1,2]),
              'len(A,G) mean %.2f'%np.mean(np.sqrt(x*x+y*y)),'len(R,G,B) mean %.2f'%np.mean(np.sqrt(r*r+y*y+b*b)), 'max|x,y|>1 frac %.3f'%np.mean(x*x+y*y>1))
    except Exception as e: print(n,'ERR',e)
