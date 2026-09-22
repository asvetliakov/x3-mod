"""Sky pixels at Chebyshev distance 1: routed displacement of the nearest-depth (min depth.x)
routed 3x3 neighbour, |(p+0.5) - motion.xy*(W,H)| in px/frame (jitter ignored, <=0.5 px;
rotation-free SETA: this approximates the band term's parallax). Bins <2, 2-3, 3-6, >=6:
sky d1 count, share current-only (taa==hdr), dark count, dark current-only.
Usage: band_parallax.py <dir> <first_frame> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32
yy,xx=np.mgrid[0:H,0:W]
bins=[0,2,3,6,1e9];R=np.zeros((4,4),np.int64)
for f in range(f0+1,f0+n):
    col,pre,sky,d=frame(dr,f)
    dep=rd(dr,'depth',f,'rgba32f',4)[...,0];m=rd(dr,'motion',f,'rgba32f',4)
    disp=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H);disp[m[...,3]<=0]=np.nan
    z=np.where(dep<0,np.inf,dep);best=np.full((H,W),np.inf);bd=np.full((H,W),np.nan)
    zp=np.pad(z,1,constant_values=np.inf);dp=np.pad(disp,1,constant_values=np.nan)
    for dy in(0,1,2):
        for dx in(0,1,2):
            zz=zp[dy:dy+H,dx:dx+W];u=zz<best;best[u]=zz[u];bd[u]=dp[dy:dy+H,dx:dx+W][u]
    t=rd(dr,'taa',f,'rgba16f',4);h=rd(dr,'hdr',f,'rgba16f',4);co=(t[...,:3]==h[...,:3]).all(-1)
    s=sky&(d==1)&np.isfinite(bd);dk=s&((col-pre)>40)
    for i in range(4):
        b=s&(bd>=bins[i])&(bd<bins[i+1]);R[i]+=[b.sum(),(b&co).sum(),(b&dk).sum(),(b&dk&co).sum()]
print(f'{dr} {f0}: bin d1_sky cur_only% dark dark_cur_only')
for i,l in enumerate(['<2','2-3','3-6','>=6']):print(' ',l,R[i,0],f'{100*R[i,1]/max(R[i,0],1):.1f}',R[i,2],R[i,3])
