"""Equivalent blur of the resolve on interior hull pixels, by routed motion bin: the Gaussian sigma (px)
that makes blur(hdr_f) closest (MSE, compressed luma l/(1+l)) to taa_f; residual at best sigma relative to
sigma 0 (a residual near 1 = not explained by blur: lag/ghosting or clip). Also the taa_mask bgra8 channel
medians (R=filter weight, G=far weight gate, B=thin-region strength b, A=a per resolve.hlsl lines 88-119;
channel order B,G,R,A in the file) in the bin, from the bottle capture dir.
Usage: hull_blurfit.py <run_dir> <mask_dir> <first> [n=32] [step=3]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import rd,W,H
LW=np.array([0.2126,0.7152,0.0722]);yy,xx=np.mgrid[0:H,0:W]
SG=[0,0.35,0.5,0.7,1.0,1.4,2.0,2.8,4.0]
def gaussian_filter(a,sg):
    r=int(np.ceil(3*sg));x=np.arange(-r,r+1);k=np.exp(-x*x/(2*sg*sg));k/=k.sum()
    p=np.pad(a,((0,0),(r,r)),mode='edge');a=sum(k[i]*p[:,i:i+a.shape[1]] for i in range(2*r+1))
    p=np.pad(a,((r,r),(0,0)),mode='edge');return sum(k[i]*p[i:i+a.shape[0]] for i in range(2*r+1))
def cl(k,f,dr=None):
    dr=dr or DR
    l=np.maximum(rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW,0);return l/(1+l)

if __name__=='__main__':
    dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32;st=int(sys.argv[5]) if len(sys.argv)>5 else 3
    DR=dr
    MB=[0,0.5,1,2,4,8,16,1e9];SG=[0,0.35,0.5,0.7,1.0,1.4,2.0,2.8,4.0]
    E=np.zeros((len(MB)-1,len(SG)));N=np.zeros(len(MB)-1,int);MK=[[] for _ in MB[:-1]]
    for f in range(f0+1,f0+n,st):
        dep=rd(dr,'depth',f,'rgba32f',4)[...,0];g=dep!=-1;p=np.pad(g,2);inn=np.ones_like(g)
        for dy in range(5):
            for dx in range(5):inn&=p[dy:dy+H,dx:dx+W]
        m=rd(dr,'motion',f,'rgba32f',4);disp=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H);rt=m[...,3]>0
        t=cl('taa',f);h=cl('hdr',f);bl=[gaussian_filter(h,s) if s else h for s in SG]
        mk=np.fromfile(f'{ad}/taa_mask_1_{f}.bgra8',np.uint8).reshape(H,W,4)
        for i in range(len(MB)-1):
            s=inn&rt&(disp>=MB[i])&(disp<MB[i+1]);N[i]+=s.sum()
            for j,b in enumerate(bl):E[i,j]+=((b-t)[s]**2).sum()
            if s.any():MK[i].append(mk[s][::7])
    print(f'{dr} {f0+1}..{f0+n-1} step {st}: bin px best_sigma resid(best)/resid(0) mask_median(R,G,B,A) mask_nonzero%(R,G,B,A)')
    for i in range(len(MB)-1):
        if not N[i]:continue
        j=int(np.argmin(E[i]));q=np.concatenate(MK[i])[:,[2,1,0,3]]
        print(f'  {MB[i]}-{MB[i+1]:g} {N[i]} {SG[j]} {E[i,j]/E[i,0]:.3f} {np.median(q,0).astype(int).tolist()} {(100*(q>0).mean(0)).round(1).tolist()}')
