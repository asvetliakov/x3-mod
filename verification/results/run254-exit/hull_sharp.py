"""Station/hull blur evidence. Hull = interior non-sentinel pixels (3x3 all depth != -1). Per pixel:
routed displacement |(p+0.5) - motion.xy*(W,H)| px/frame (motion.w>0 only; unrouted hull has no stored
vector), resolve age |taa_age| (bottle capture dir). High-frequency energy: sum of squared 4-neighbour
Laplacian of compressed luma c=l/(1+l) of the resolved output (taa) and of the same frame's current
pre-resolve input (hdr); ratio E(taa)/E(hdr) (1 = as sharp as the current sample; the current sample
is jittered and aliased, so <1 also comes from AA). LDR pair: present (after RCAS) vs color.
Rows: per burst, by motion bin and by age bin. Usage: hull_sharp.py <run_dir> <age_dir> <first> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import rd,W,H
dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32
LW=np.array([0.2126,0.7152,0.0722]);L8=np.array([0.114,0.587,0.299])
yy,xx=np.mgrid[0:H,0:W]
def lap(c):
    p=np.pad(c,1,mode='edge');return 4*c-p[:-2,1:-1]-p[2:,1:-1]-p[1:-1,:-2]-p[1:-1,2:]
def cl(k,f):l=np.maximum(rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW,0);return l/(1+l)
def l8(k,f):return rd(dr,k,f,'bgra8',4)[...,:3].astype(np.float64)@L8/255
MB=[0,0.5,1,2,4,8,16,1e9];AB=[0,1.5,4.5,16.5,63.5,65]
M=np.zeros((len(MB)-1,5));G=np.zeros((len(AB)-1,5));hulls=[];rout=[0,0];mags=[]
for f in range(f0+1,f0+n):
    dep=rd(dr,'depth',f,'rgba32f',4)[...,0];g=dep!=-1
    p=np.pad(g,1);inn=np.ones_like(g)
    for dy in(0,1,2):
        for dx in(0,1,2):inn&=p[dy:dy+H,dx:dx+W]
    m=rd(dr,'motion',f,'rgba32f',4);rt=m[...,3]>0
    disp=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H)
    a=np.abs(np.fromfile(f'{ad}/taa_age_1_{f}.r32f',np.float32).reshape(H,W))
    et=lap(cl('taa',f))**2;eh=lap(cl('hdr',f))**2;ep=lap(l8('present',f))**2;ec=lap(l8('color',f))**2
    hulls.append(int(inn.sum()));rout[0]+=int(inn.sum());rout[1]+=int((inn&rt).sum());mags.append(disp[inn&rt])
    for i in range(len(MB)-1):
        s=inn&rt&(disp>=MB[i])&(disp<MB[i+1]);M[i]+=[s.sum(),et[s].sum(),eh[s].sum(),ep[s].sum(),ec[s].sum()]
    for i in range(len(AB)-1):
        s=inn&(a>=AB[i])&(a<AB[i+1]);G[i]+=[s.sum(),et[s].sum(),eh[s].sum(),ep[s].sum(),ec[s].sum()]
mg=np.concatenate(mags)
print(f'{dr} {f0+1}-{f0+n-1}: interior hull px/frame median {int(np.median(hulls))} (min {min(hulls)} max {max(hulls)}); routed {rout[1]/rout[0]:.1%}; routed disp px/frame p10/p50/p90/p99 {np.percentile(mg,[10,50,90,99]).round(2).tolist()}')
print(' motion_bin px E_taa/E_hdr E_present/E_color')
for i in range(len(MB)-1):
    r=M[i];print(f'  {MB[i]}-{MB[i+1]:g} {int(r[0])} {r[1]/max(r[2],1e-12):.3f} {r[3]/max(r[4],1e-12):.3f}')
print(' age_bin px E_taa/E_hdr E_present/E_color')
for i,l in enumerate(['1','2-4','5-16','17-63','64']):
    r=G[i];print(f'  {l} {int(r[0])} {r[1]/max(r[2],1e-12):.3f} {r[3]/max(r[4],1e-12):.3f}')
