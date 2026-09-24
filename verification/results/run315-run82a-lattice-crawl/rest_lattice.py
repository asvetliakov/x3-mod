# Input-side (pre-resolve hdr_1, jittered, taa_debug=0) lattice analysis over one F8 burst at rest.
# Region: pixels within DIL px of any fade-owner cell pixel (routed .r!=-1 & .g==-1) in any frame, inside crop.
# Per frame classes: O owner (panel cell, fade arm 64bac8bb), L routed non-owner (.g!=-1: lines, struts, frames; opaque or
# alpha-tested), S sentinel (.r==-1). Display code c = 255*(Y/(1+Y))^(1/2.2) of Rec.709 luma.
# Prints: per-frame class counts, class-membership stability over N frames, per-pixel 8-frame std of c per class
# (stable class vs flipping), RT2 .a distribution, motion vector (prev unjittered UV - pixel centre) magnitude,
# and the same std on a hull control crop (HX0 HY0 HX1 HY1, all routed pixels).
# usage: rest_lattice.py RUN F0 N X0 Y0 X1 Y1 HX0 HY0 HX1 HY1 [env DIL=2]
import sys,os,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); X0,Y0,X1,Y1,HX0,HY0,HX1,HY1=map(int,sys.argv[4:12]); DIL=int(os.environ.get('DIL',2))
W,H=5120,1440; LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def ld(kind,f,dt,ch,sl):
    return np.fromfile(f"/tmp/x3-bottleX3-run{r}/{kind}_1_{f}.{'rgba16f' if kind=='hdr' else 'rgba32f'}",dtype=dt).reshape(H,W,ch)[sl].astype(np.float32)
def code(h): Y=np.nan_to_num(h[...,:3]@LUMA); return 255*np.clip(Y/(1+Y),0,1)**(1/2.2)
def box(x,rr):
    c=np.pad(x,((rr+1,rr),(rr+1,rr)),mode='edge').cumsum(0).cumsum(1); n=2*rr+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
sl=(slice(Y0,Y1),slice(X0,X1)); hs=(slice(HY0,HY1),slice(HX0,HX1))
C=[];A=[];Mv=[];HC=[];HA=[]
for i in range(N):
    f=F0+i; C.append(code(ld('hdr',f,np.float16,4,sl))); A.append(ld('depth',f,np.float32,4,sl))
    m=ld('motion',f,np.float32,4,sl); ys,xs=np.mgrid[Y0:Y1,X0:X1]
    Mv.append(np.stack([m[...,0]*W-(xs+0.5), m[...,1]*H-(ys+0.5)],-1)*(m[...,3:4]==1))
    HC.append(code(ld('hdr',f,np.float16,4,hs))); HA.append(ld('depth',f,np.float32,4,hs))
C=np.stack(C);A=np.stack(A);Mv=np.stack(Mv);HC=np.stack(HC);HA=np.stack(HA)
S=A[...,0]==-1; O=(~S)&(A[...,1]==-1); L=(~S)&~O
R=box(O.any(0).astype(np.float32),DIL)>0
print(f"region pixels {int(R.sum())} (crop {X0},{Y0}-{X1},{Y1}, DIL {DIL})")
for i in range(N): print(f"frame {F0+i} in region: O {int((O[i]&R).sum())} L {int((L[i]&R).sum())} S {int((S[i]&R).sum())}")
nO,nL,nS=O.sum(0),L.sum(0),S.sum(0)
stable_O=R&(nO==N); stable_L=R&(nL==N); stable_S=R&(nS==N); flip=R&~stable_O&~stable_L&~stable_S
print(f"class stability over {N} frames in region: always O {int(stable_O.sum())}, always L {int(stable_L.sum())}, always S {int(stable_S.sum())}, flipping {int(flip.sum())} ({flip.sum()/R.sum():.3f})")
fl_L=R&(nL>0); print(f"pixels ever L {int(fl_L.sum())}; always L {int(stable_L.sum())} ({stable_L.sum()/max(fl_L.sum(),1):.3f}); mean L coverage on ever-L pixels {nL[fl_L].mean()/N:.3f}")
fl_S=R&(nS>0); print(f"pixels ever S {int(fl_S.sum())}; always S {int(stable_S.sum())}; mean S coverage on ever-S {nS[fl_S].mean()/N:.3f}")
# flip kinds
for a_,b_,nm in ((nO,nL,'O<->L'),(nO,nS,'O<->S'),(nL,nS,'L<->S')):
    k=flip&(a_>0)&(b_>0)&((a_+b_)==N); print(f"  flipping only {nm}: {int(k.sum())}")
print(f"  flipping among all three: {int((flip&(nO>0)&(nL>0)&(nS>0)).sum())}")
std=C.std(0); mean=C.mean(0)
def rep(m,nm):
    v=std[m]; print(f"8-frame per-pixel std of c, {nm}: n {v.size} mean {v.mean():.2f} p50 {np.median(v):.2f} p95 {np.percentile(v,95):.2f} frac>4 {np.mean(v>4):.3f} frac>10 {np.mean(v>10):.3f}")
rep(stable_O,'always O (cells)'); rep(stable_L,'always L'); rep(stable_S,'always S'); rep(flip,'flipping'); rep(R,'whole region')
for nm,m in (('O<->L',flip&(nO>0)&(nL>0)&(nS==0)),('with S',flip&(nS>0))):
    if m.any(): rep(m,'flipping '+nm)
print(f"mean c: cells {mean[stable_O].mean():.1f} always-L {mean[stable_L].mean():.1f} always-S {mean[stable_S].mean():.1f}")
# frame-to-frame rms of c
d=np.diff(C,axis=0)
print("frame-to-frame rms dc region: "+' '.join(f"{np.sqrt((d[i][R]**2).mean()):.2f}" for i in range(N-1)))
hstd=HC.std(0); hr=(HA[...,0]!=-1).all(0); hd=np.diff(HC,axis=0)
v=hstd[hr]; print(f"hull control {HX0},{HY0}-{HX1},{HY1} always-routed n {v.size}: std mean {v.mean():.2f} p95 {np.percentile(v,95):.2f} frac>4 {np.mean(v>4):.3f} frac>10 {np.mean(v>10):.3f}; rms dc "+' '.join(f"{np.sqrt((hd[i][hr]**2).mean()):.2f}" for i in range(N-1)))
# RT2 .a and w
a_=A[...,3][(~S)&R[None]]; print(f"RT2 .a on routed region pixels: n {a_.size} ==1 {np.mean(a_==1):.4f} in[0,1) {np.mean((a_>=0)&(a_<1)):.4f} other {np.mean((a_!=1)&~((a_>=0)&(a_<1))):.4f}")
w=A[...,2][L&R[None]]; print(f"w (.b) on L pixels: p5/p50/p95 {np.percentile(w,5):.0f}/{np.median(w):.0f}/{np.percentile(w,95):.0f}; on O: {np.median(A[...,2][O&R[None]]):.0f}")
g=A[...,1][L&R[None]]; u,c=np.unique(np.round(g,3),return_counts=True); o=np.argsort(-c)[:5]; print("RT2 .g on L pixels top: "+', '.join(f"{u[k]}x{c[k]}" for k in o))
mv=np.linalg.norm(Mv,axis=-1); valid=(Mv!=0).any(-1)|((~S)&R[None])
for i in range(N):
    v=mv[i][(~S[i])&R]; print(f"frame {F0+i} motion |v| px on routed region: p50 {np.median(v):.3f} p95 {np.percentile(v,95):.3f} max {v.max():.3f}")
