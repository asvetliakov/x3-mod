# Offline model of the tests draw's thin flag (src/temporal/line_mask_ps.hlsl:282-293) on the dumped RT2 lane (depth_1 .r, .a):
# vote = valid depth && 0 <= .a < 1; search = along one of 4 directions, 7 taps (-3..3, clamp), >= 2 class changes with
# classChange(a,b) = (valid(a) && bg(b,a)) || (valid(b) && bg(a,b)), bg(q,d) = sentinel(q) || (valid(q) && (1-q)*1.1 < 1-d).
# source both = vote || search; vote = vote only. A' region (resolve.hlsl:753) at rest with L = 8 = union of the flags
# over the 8 jitter phases (identity reprojection; camera static, measured). Emissive vote not modelled (needs luma rule).
# Emissive vote (line_mask_ps.hlsl:145-160) modelled with E = env E (default 1.0, thin_emissive=1.000): routed (motion .a == 1),
# valid depth, Rec.709 luma of hdr_1 > E and 3x3 min * 3 < centre; runs under both sources when the pixel is not flagged yet.
# Reports over the panel region (as rest_lattice.py) per frame and the union, split by class (O cells / L lines / flipping).
# usage: search_sim.py RUN F0 N X0 Y0 X1 Y1 [env DIL=2]
import sys,os,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); X0,Y0,X1,Y1=map(int,sys.argv[4:8]); DIL=int(os.environ.get('DIL',2)); P=4
sl=(slice(Y0-P,Y1+P),slice(X0-P,X1+P))
def box(x,rr):
    c=np.pad(x,((rr+1,rr),(rr+1,rr)),mode='edge').cumsum(0).cumsum(1); n=2*rr+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
valid=lambda v:(v>=0)&(v<=1); sent=lambda v:(v<=-0.5)&(v>=-1e30)
def bg(q,d): return sent(q)|(valid(q)&((1-q)*1.1<1-d))
def cc(a,b): return (valid(a)&bg(b,a))|(valid(b)&bg(a,b))
def shift(z,dy,dx):
    return np.pad(z,P,mode='edge')[P+dy:P+dy+z.shape[0],P+dx:P+dx+z.shape[1]]
A=[np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl] for i in range(N)]
E=float(os.environ.get('E',1.0)); LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
Ys=[np.nan_to_num(np.fromfile(f"/tmp/x3-bottleX3-run{r}/hdr_1_{F0+i}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[sl][...,:3].astype(np.float32)@LUMA) for i in range(N)]
Ma=[np.fromfile(f"/tmp/x3-bottleX3-run{r}/motion_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl][...,3] for i in range(N)]
inner=(slice(P,-P),slice(P,-P))
Fs=[];Fv=[];cls=[]
Fe=[]
for ii,a in enumerate(A):
    z=a[...,0]; Y=np.maximum(Ys[ii],0); lo=np.min(np.stack([shift(Y,dy,dx) for dy in (-1,0,1) for dx in (-1,0,1)]),0)
    emis=valid(z)&(Ma[ii]==1)&(Y>E)&(lo*3<Y); srch=np.zeros(z.shape,bool)
    for dy,dx in ((0,1),(1,0),(1,1),(-1,1)):
        ch=np.zeros(z.shape,np.int32); prev=shift(z,-3*dy,-3*dx)
        for t in range(-2,4):
            nx=shift(z,t*dy,t*dx); ch+=cc(prev,nx); prev=nx
        srch|=ch>=2
    vote=valid(z)&(a[...,3]>=0)&(a[...,3]<1)
    Fs.append((srch|vote|emis)[inner]); Fv.append((vote|emis)[inner]); Fe.append(emis[inner])
    S=sent(z); O=(~S)&(a[...,1]==-1); cls.append((O[inner],((~S)&~O)[inner],S[inner]))
O=np.stack([c[0] for c in cls]); L=np.stack([c[1] for c in cls]); S=np.stack([c[2] for c in cls])
R=box(O.any(0).astype(np.float32),DIL)>0
nO,nL,nS=O.sum(0),L.sum(0),S.sum(0)
groups={'region':R,'always O cells':R&(nO==N),'ever L (lines/struts)':R&(nL>0),'always L':R&(nL==N),'flipping':R&(nO<N)&(nL<N)&(nS<N)}
for nm,F in (('both (search|vote|emissive)',Fs),('vote (vote|emissive)',Fv),('emissive alone',Fe)):
    U=np.stack(F).any(0)
    print(f"source {nm}: per-frame flagged fraction of region "+' '.join(f"{F[i][R].mean():.3f}" for i in range(N))+f"; union over {N} frames (A' region at rest):")
    for g,m in groups.items(): print(f"   {g}: n {int(m.sum())} in A' region {U[m].mean():.3f}")
