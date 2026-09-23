"""Moving interior hull pixels (routed disp >= 1 px/frame, 5x5 all geometry): split by the taa_mask
thin-region strength b (file byte 0 = B channel, resolve.hlsl lines 98-119, camera gate) into b=0 / 0<b<255 /
b=255; per split: px, E_taa/E_hdr (squared Laplacian, compressed luma), best Gaussian sigma and residual
ratio (as hull_blurfit.py), median |age|. Usage: hull_region_split.py <run_dir> <cap_dir> <first> [n=32] [step=3]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import rd,W,H
from hull_blurfit import gaussian_filter,cl,SG
import hull_blurfit
def lap(c):
    p=np.pad(c,1,mode='edge');return 4*c-p[:-2,1:-1]-p[2:,1:-1]-p[1:-1,:-2]-p[1:-1,2:]
dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32;st=int(sys.argv[5]) if len(sys.argv)>5 else 3
hull_blurfit.DR=dr;yy,xx=np.mgrid[0:H,0:W]
names=['b=0','0<b<255','b=255'];E=np.zeros((3,len(SG)));L=np.zeros((3,2));N=np.zeros(3,int);AG=[[],[],[]]
for f in range(f0+1,f0+n,st):
    dep=rd(dr,'depth',f,'rgba32f',4)[...,0];g=dep!=-1;p=np.pad(g,2);inn=np.ones_like(g)
    for dy in range(5):
        for dx in range(5):inn&=p[dy:dy+H,dx:dx+W]
    m=rd(dr,'motion',f,'rgba32f',4);disp=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H)
    mv=inn&(m[...,3]>0)&(disp>=1)
    b=np.fromfile(f'{ad}/taa_mask_1_{f}.bgra8',np.uint8).reshape(H,W,4)[...,0]
    a=np.abs(np.fromfile(f'{ad}/taa_age_1_{f}.r32f',np.float32).reshape(H,W))
    t=cl('taa',f);h=cl('hdr',f);bl=[gaussian_filter(h,s) if s else h for s in SG];lt=lap(t)**2;lh=lap(h)**2
    for i,s in enumerate([mv&(b==0),mv&(b>0)&(b<255),mv&(b==255)]):
        N[i]+=s.sum();L[i]+=[lt[s].sum(),lh[s].sum()];AG[i].append(a[s])
        for j,x in enumerate(bl):E[i,j]+=((x-t)[s]**2).sum()
print(f'{dr} {f0+1}..{f0+n-1} step {st}, moving hull (>=1 px/frame): split px E_taa/E_hdr best_sigma resid_ratio median_age')
for i in range(3):
    if N[i]:j=int(np.argmin(E[i]));print(f'  {names[i]} {N[i]} {L[i,0]/L[i,1]:.3f} {SG[j]} {E[i,j]/E[i,0]:.3f} {np.median(np.concatenate(AG[i])):.0f}')
