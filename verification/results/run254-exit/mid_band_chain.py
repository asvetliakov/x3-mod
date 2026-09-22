"""(1) Camera-path check: global shift of the far sky (d>=40 from the silhouette in both frames)
between f-1 and f, by phase correlation of hdr luma (0,0 = rotation-free, identity history
lookup for unrouted sky). (2) Chain length: for the d3-12 dark pixels of frame f, how many
consecutive earlier frames the same pixel was dark (darksky.py definition), and whether it is
dark on f+1. Usage: mid_band_chain.py <dir> f1 f2 ..."""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H,cheb
LW=np.array([0.2126,0.7152,0.0722])
def lu(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW
dr=sys.argv[1]
def dark(f):
    col,pre,sky,d=frame(dr,f);return sky&((col-pre)>40),sky,d
for f in map(int,sys.argv[2:]):
    a=lu(dr,'hdr',f);b=lu(dr,'hdr',f-1)
    dc=rd(dr,'depth',f,'rgba32f',4)[...,0]==-1;dp=rd(dr,'depth',f-1,'rgba32f',4)[...,0]==-1
    far=(cheb(~dc,40)>=40)&(cheb(~dp,40)>=40)
    A=np.where(far,a-a[far].mean(),0);B=np.where(far,b-b[far].mean(),0)
    X=np.fft.fft2(A)*np.conj(np.fft.fft2(B));r=np.real(np.fft.ifft2(X/np.maximum(np.abs(X),1e-12)))
    iy,ix=np.unravel_index(np.argmax(r),r.shape);iy=iy-H if iy>H//2 else iy;ix=ix-W if ix>W//2 else ix
    dk,sky,d=dark(f);m=dk&(d>=3)&(d<=12);run=np.zeros((H,W),int);alive=m.copy()
    for g in range(f-1,f-9,-1):
        alive&=dark(g)[0];run+=alive
    nx=dark(f+1)[0]
    L=run[m];print(f'f{f}: far-sky shift f-1->f (dy,dx)=({iy},{ix}) peak {r.max():.3f} far_px={int(far.sum())}; d3-12 dark n={int(m.sum())}: dark also on f-1 {int((L>=1).sum())}, run>=2 {int((L>=2).sum())}, run>=4 {int((L>=4).sum())}, run>=8 {int((L>=8).sum())}; dark on f+1 {int((m&nx).sum())}')
