# For region pixels that flip between the fade-owner cell face (O: .g==-1) and routed non-owner geometry (L) over a rest
# burst: w (.b) and z/w (.r) of the O frames vs the L frames at the same pixel (camera static; jitter only), to tell a
# coplanar depth-test race (|dw| ~ 0) from line geometry in front of the face plane (w_L < w_O).
# Also colour: mean display code in O frames vs L frames. usage: flip_depth.py RUN F0 N X0 Y0 X1 Y1
import sys,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); X0,Y0,X1,Y1=map(int,sys.argv[4:8]); sl=(slice(Y0,Y1),slice(X0,X1))
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
A=np.stack([np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl] for i in range(N)])
C=[]
for i in range(N):
    h=np.fromfile(f"/tmp/x3-bottleX3-run{r}/hdr_1_{F0+i}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[sl][...,:3].astype(np.float32); Y=h@LUMA; C.append(255*np.clip(Y/(1+Y),0,1)**(1/2.2))
C=np.stack(C)
S=A[...,0]==-1; O=(~S)&(A[...,1]==-1); L=(~S)&~O
m=(O.sum(0)>0)&(L.sum(0)>0)&(S.sum(0)==0)
w=A[...,2]; 
wO=np.where(O,w,np.nan); wL=np.where(L,w,np.nan)
mO=np.nanmean(wO[:,m],0); mL=np.nanmean(wL[:,m],0); rel=(mL-mO)/mO
print(f"O<->L flipping pixels {int(m.sum())}: rel dw (w_L-w_O)/w_O p5/p50/p95 {np.percentile(rel,5):.2e}/{np.median(rel):.2e}/{np.percentile(rel,95):.2e}; frac w_L<w_O {np.mean(rel<0):.3f}; frac |rel|<1e-4 {np.mean(np.abs(rel)<1e-4):.3f}")
# neighbour-scale reference: relative w change across 1 px on stable O cells
Os=O.all(0); gx=np.abs(np.diff(w[0],axis=1))/w[0][:,1:]; v=gx[Os[:,1:]&Os[:,:-1]]
print(f"reference: |dw|/w between horizontal neighbours on always-O cells p50 {np.median(v):.2e} p95 {np.percentile(v,95):.2e}")
cO=np.nanmean(np.where(O,C,np.nan)[:,m],0); cL=np.nanmean(np.where(L,C,np.nan)[:,m],0)
print(f"display code on flipping pixels: O frames mean {cO.mean():.1f}, L frames mean {cL.mean():.1f}, mean |cL-cO| {np.abs(cL-cO).mean():.1f}")
# L<->S flipping: colour of S frames (background) vs L frames
m2=(L.sum(0)>0)&(S.sum(0)>0)&(O.sum(0)==0)
cS=np.nanmean(np.where(S,C,np.nan)[:,m2],0); cL2=np.nanmean(np.where(L,C,np.nan)[:,m2],0)
print(f"L<->S flipping pixels {int(m2.sum())}: L frames mean c {cL2.mean():.1f}, S frames mean c {cS.mean():.1f}, mean |dc| {np.abs(cL2-cS).mean():.1f}")
