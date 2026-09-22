"""Refined dark-sky census: a darksky.py dark pixel counts as history-darkened only when the
resolve output is below k * the pixel's own 8-frame mean of the current HDR sample
(taa_f < k*mean(hdr_{f-7..f}), camera path is identity on this rotation-free leg).
Prints per distance class: darksky.py count vs refined count, frames f0+7..f0+n-1.
Usage: dark_vs_own_mean.py <dir> <first_frame> [n=32] [k=0.7]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
LW=np.array([0.2126,0.7152,0.0722])
def lu(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32;k=float(sys.argv[4]) if len(sys.argv)>4 else 0.7
cls={'d1-2':(1,2),'d3-12':(3,12),'d>=13':(13,99999)};A={c:[0,0] for c in cls}
win=[lu(dr,'hdr',g) for g in range(f0,f0+7)]
for f in range(f0+7,f0+n):
    win.append(lu(dr,'hdr',f));mu=np.mean(win,0);win.pop(0)
    col,pre,sky,d=frame(dr,f);dk=sky&((col-pre)>40);t=lu(dr,'taa',f);r=dk&(t<k*mu)
    for c,(a,b) in cls.items():s=(d>=a)&(d<=b);A[c][0]+=int((dk&s).sum());A[c][1]+=int((r&s).sum())
print(f'{dr} frames {f0+7}-{f0+n-1} k={k}: '+'; '.join(f'{c} dark {v[0]} below-own-mean {v[1]} ({v[1]/max(v[0],1):.0%})' for c,v in A.items()))
