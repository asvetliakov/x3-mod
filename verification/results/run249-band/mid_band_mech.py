"""Mechanism of the 3-12 px dark-sky pixels (darksky.py definition) on sampled frames.
Per dark pixel p at Chebyshev distance 3..12 from the current silhouette, frame f:
 hdrL = luma(hdr_f[p]) current HDR sample, taaL = luma(taa_f[p]) resolve output,
 htaL = luma(taa_{f-1}[p]) history at the camera path (identity: SETA leg has no rotation;
 checked below by the far-sky shift test), pdep = depth_{f-1}[p] (sentinel or geometry),
 pdist = distance to the previous frame's silhouette, co = current-only (taa==hdr).
Classes: A prev depth at p is geometry (history is hull); B prev sky & prev taa already dark
(taa_{f-1} < 0.5*hdr_{f-1}); C taa ~= hdr (|taa-hdr| <= 10% hdr: output not darkened by TAA).
Usage: mid_band_mech.py <dir> f1 f2 ..."""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H,cheb
LW=np.array([0.2126,0.7152,0.0722])
def fl(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)
def lu(a):return a[...,:3]@LW
dr=sys.argv[1]
for f in map(int,sys.argv[2:]):
    col,pre,sky,d=frame(dr,f)
    dk=sky&((col-pre)>40)&(d>=3)&(d<=12)
    h=lu(fl(dr,'hdr',f));t=lu(fl(dr,'taa',f));tp=lu(fl(dr,'taa',f-1));hp=lu(fl(dr,'hdr',f-1))
    pdep=rd(dr,'depth',f-1,'rgba32f',4)[...,0];psky=pdep==-1;pd=cheb(~psky)
    m=rd(dr,'motion',f,'rgba32f',4)[...,3]
    co=(rd(dr,'taa',f,'rgba16f',4)[...,:3]==rd(dr,'hdr',f,'rgba16f',4)[...,:3]).all(-1)
    n=int(dk.sum())
    A=dk&~psky;B=dk&psky&(tp<0.5*hp);C=dk&(np.abs(t-h)<=0.1*np.abs(h)+1e-6)
    print(f'f{f}: dark d3-12 n={n} motion.w=-1 {int((dk&(m==-1)).sum())} cur_only {int((dk&co).sum())}')
    print(f'  A prev-depth geometry {int(A.sum())}; prev sky {int((dk&psky).sum())} (prev dist 1-2 {int((dk&psky&(pd<=2)).sum())}, 3-12 {int((dk&psky&(pd>=3)&(pd<=12)).sum())}, >=13 {int((dk&psky&(pd>=13)).sum())})')
    print(f'  B prev taa < 0.5 prev hdr {int(B.sum())};  C |taa-hdr|<=10% {int(C.sum())}; taa<0.5 hdr {int((dk&(t<0.5*h)).sum())}; taa>hdr {int((dk&(t>h)).sum())}')
    if n:
        q=lambda a:np.round(np.percentile(a[dk],[10,50,90]),4).tolist()
        print(f'  hdrL p10/50/90 {q(h)} taaL {q(t)} prev_taaL {q(tp)} prev_hdrL {q(hp)}')
        print(f'  colour/present luma(bgra8) median {np.median(col[dk]):.1f}/{np.median(pre[dk]):.1f}; ratio taa/hdr median {np.median((t/np.maximum(h,1e-6))[dk]):.3f}')
    # control: same classes on all sky pixels d3-12 (not only dark)
    s=sky&(d>=3)&(d<=12)
    print(f'  control sky d3-12 n={int(s.sum())}: taa/hdr median {np.median((t/np.maximum(h,1e-6))[s]):.3f}; cur_only {int((s&co).sum())}; prev geometry {int((s&~psky).sum())}')
