"""Fine-binned variant of band_parallax.py for the exit-reset floor (seta-sky-hull-share-decay.md):
sky pixels at Chebyshev distance 1 whose nearest-depth routed 3x3 neighbour moves p px/frame
(rotation-free leg: displacement = translation parallax), bins <0.25, 0.25-0.5, 0.5-1, 1-2, 2-3, >=3:
sky d1 count, dark count (darksky.py, T=40), share of the dark d1 pixels in the bin.
Usage: band_parallax_fine.py <dir> <first_frame> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32
yy,xx=np.mgrid[0:H,0:W]
bins=[0,.25,.5,1,2,3,1e9];R=np.zeros((6,2),np.int64)
for f in range(f0+1,f0+n):
    col,pre,sky,d=frame(dr,f)
    dep=rd(dr,'depth',f,'rgba32f',4)[...,0];m=rd(dr,'motion',f,'rgba32f',4)
    disp=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H);disp[m[...,3]<=0]=np.nan
    z=np.where(dep<0,np.inf,dep);best=np.full((H,W),np.inf);bd=np.full((H,W),np.nan)
    zp=np.pad(z,1,constant_values=np.inf);dp=np.pad(disp,1,constant_values=np.nan)
    for dy in(0,1,2):
        for dx in(0,1,2):
            zz=zp[dy:dy+H,dx:dx+W];u=zz<best;best[u]=zz[u];bd[u]=dp[dy:dy+H,dx:dx+W][u]
    s=sky&(d==1)&np.isfinite(bd);dk=s&((col-pre)>40)
    for i in range(6):
        b=s&(bd>=bins[i])&(bd<bins[i+1]);R[i]+=[b.sum(),(b&dk).sum()]
tot=R[:,1].sum()
print(f'{dr} {f0}: bin d1_sky dark dark_share')
for i,l in enumerate(['<0.25','0.25-0.5','0.5-1','1-2','2-3','>=3']):print(' ',l,R[i,0],R[i,1],f'{100*R[i,1]/max(tot,1):.1f}%')
