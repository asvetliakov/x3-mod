"""Refines age_census.py: of the pixels marked on f-1 that are sky on f, split into re-marked on f (still a
band blend pixel) and not re-marked (left the band = the exit-reset population); of the latter, share
current-only (taa==hdr exactly), age==1 (count restarted), and |age|>1 (not reset), by distance class on f;
plus d1 dark-sky pixels (darksky.py) that are marked on f.
Usage: age_takers.py <run_dir> <age_dir> <first> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32
age=lambda f:np.fromfile(f'{ad}/taa_age_1_{f}.r32f',np.float32).reshape(H,W)
prev=None;A=np.zeros(9,np.int64);per=[]
for f in range(f0,f0+n):
    a=age(f);col,pre,sky,d=frame(dr,f)
    t=rd(dr,'taa',f,'rgba16f',4);h=rd(dr,'hdr',f,'rgba16f',4);co=(t[...,:3]==h[...,:3]).all(-1)
    if prev is not None:
        tk=sky&(prev<0);re_=tk&(a<0);ex=tk&(a>=0);dk=sky&((col-pre)>40)&(d<=2)
        r=[tk.sum(),re_.sum(),ex.sum(),(ex&co).sum(),(ex&(a==1)).sum(),(ex&(a>1)).sum(),(ex&(d>=2)).sum(),dk.sum(),(dk&(a<0)).sum()]
        A+=r;per.append(r[2])
    prev=a
print(f'{dr} {f0}+{n} totals: takers {A[0]} re-marked {A[1]} left_band {A[2]} (per frame median {int(np.median(per))}): cur_only {A[3]} ({A[3]/max(A[2],1):.1%}) age1 {A[4]} ({A[4]/max(A[2],1):.1%}) age>1 {A[5]}; left_band at d>=2 {A[6]}; dark d1-2 {A[7]} of which marked {A[8]} ({A[8]/max(A[7],1):.1%})')
