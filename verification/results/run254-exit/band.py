"""Per-pixel band-term evidence for a burst. current-only proxy: taa_ rgb == hdr_ rgb exactly
(resolve output equals the current sample; inferred proxy, no shader counter exists).
Rows by Chebyshev distance class of sky pixels: share current-only; dark-sky pixels
(darksky.py definition) split by motion.w (1 routed, -1 unrouted) and current-only.
Also dark d>=13: median color/present luma (star-dimming check).
Usage: band.py <dir> <first_frame> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32
cls={'d1':(1,1),'d2':(2,2),'d3-6':(3,6),'d7-12':(7,12),'d>=13':(13,99999)}
A={k:np.zeros(6,np.int64) for k in cls};far=[]
prev=None
for f in range(f0,f0+n):
    col,pre,sky,d=frame(dr,f)
    t=rd(dr,'taa',f,'rgba16f',4).astype(np.float32);h=rd(dr,'hdr',f,'rgba16f',4).astype(np.float32)
    co=(t[...,:3]==h[...,:3]).all(-1);w=rd(dr,'motion',f,'rgba32f',4)[...,3]
    if prev is None:prev=1;continue
    dk=sky&((col-pre)>40)
    for k,(a,b) in cls.items():
        s=sky&(d>=a)&(d<=b);x=dk&s
        A[k]+=[s.sum(),(s&co).sum(),x.sum(),(x&(w>0)).sum(),(x&co).sum(),(s&(w>0)).sum()]
    x=dk&(d>=13)
    if x.any():far.append((np.median(col[x]),np.median(pre[x]),np.median((col-pre)[x])))
print(f'{dr} {f0}+{n}')
print('class sky_px cur_only% dark dark_routed% dark_cur_only% sky_routed%')
for k,v in A.items():print(k,v[0],f'{100*v[1]/max(v[0],1):.2f}',v[2],f'{100*v[3]/max(v[2],1):.1f}',f'{100*v[4]/max(v[2],1):.1f}',f'{100*v[5]/max(v[0],1):.2f}')
if far:f=np.array(far);print('dark d>=13 median color/present/diff luma over frames:',*np.round(np.median(f,0),1))
