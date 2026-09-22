"""The d3-12 dark pixels whose output is below 0.7 x their own 8-frame HDR mean (dark_vs_own_mean.py):
minimum Chebyshev distance to the silhouette over the previous 8 frames (was the pixel in
the 1-2 px band, or covered, earlier: hull share carried outward by the static sky path),
and the silhouette distance trend (d_f - d_{f-8}). Usage: mid_band_refined.py <dir> f1 f2 ..."""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H,cheb
LW=np.array([0.2126,0.7152,0.0722])
def lu(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW
dr=sys.argv[1]
for f in map(int,sys.argv[2:]):
    col,pre,sky,d=frame(dr,f);mu=np.mean([lu(dr,'hdr',g) for g in range(f-7,f+1)],0);t=lu(dr,'taa',f)
    dk=sky&((col-pre)>40)&(d>=3)&(d<=12);r=dk&(t<0.7*mu);o=dk&~r
    D=[cheb(rd(dr,'depth',g,'rgba32f',4)[...,0]!=-1) for g in range(f-8,f)]
    mn=np.min(D,0)
    for nm,m in(('refined',r),('rest',o)):
        v=mn[m];print(f'f{f} {nm} n={int(m.sum())}: min prior dist 0 (covered) {int((v==0).sum())}, 1-2 {int(((v>=1)&(v<=2)).sum())}, 3-12 {int(((v>=3)&(v<=12)).sum())}, >=13 {int((v>=13).sum())}; d_f - d_(f-8) median {np.median((d-D[0])[m]) if m.any() else 0:.0f}')
