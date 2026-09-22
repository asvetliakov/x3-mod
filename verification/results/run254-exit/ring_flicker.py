"""Sky-border flicker: for pixels at Chebyshev distance k (1,2,3) in both frame f-1 and f (sky
in both: excludes freshly uncovered), mean |present luma(f) - present luma(f-1)|; control =
sky at d>=40 in both frames. Also d1 current-only share (taa==hdr). Not the ledger's exact
estimator (per-frame-mean std); compare only across runs with this script.
Usage: ring_flicker.py <dir> <first_frame> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,cheb
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32
acc={k:[] for k in (1,2,3,'far')};co1=[0,0];prev=None
for f in range(f0,f0+n):
    col,pre,sky,d=frame(dr,f);dfar=cheb(~sky,40)
    if prev is not None:
        pp,pd,pfar=prev
        for k in (1,2,3):
            s=(d==k)&(pd==k);acc[k].append(np.abs(pre-pp)[s].mean())
        s=(dfar>=9999)&(pfar>=9999);acc['far'].append(np.abs(pre-pp)[s].mean() if s.any() else np.nan)
        t=rd(dr,'taa',f,'rgba16f',4);h=rd(dr,'hdr',f,'rgba16f',4);c=(t[...,:3]==h[...,:3]).all(-1)
        co1[0]+=(d==1).sum();co1[1]+=(c&(d==1)).sum()
    prev=(pre,d,dfar)
print(f'{dr} {f0}: mean|dLuma| d1 {np.nanmean(acc[1]):.2f} d2 {np.nanmean(acc[2]):.2f} d3 {np.nanmean(acc[3]):.2f} far {np.nanmean(acc["far"]):.2f}; d1 cur_only {100*co1[1]/co1[0]:.1f}%')
