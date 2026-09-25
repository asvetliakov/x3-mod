# Edge dimming: taa code minus hdr code (same frame, codes as in sparkles.py) on edge px of owned far plant px
# (edge = dilate1(|c_hdr - mean5x5| > 4) & owned, owned = routed .g==-1 .a==1 w>20000). Also the burst-mean variant
# (mean taa code minus mean hdr code over the 8 frames, rest bursts only: removes the jitter noise of a single frame).
# usage: edge_dim.py RUN X0 Y0 X1 Y1 F0 N
import sys,numpy as np
RUN=sys.argv[1]; X0,Y0,X1,Y1,F0,N=map(int,sys.argv[2:8]); TD=f"/tmp/x3-bottleX3-run{RUN}"
L=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch))[Y0:Y1,X0:X1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def box(x,r):
    c=np.pad(x,((r+1,r),(r+1,r)),mode='edge').cumsum(0).cumsum(1); n=2*r+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
D=[];H=[];T=[];E=[]
for f in range(F0,F0+N):
    h=code(mm(f"{TD}/hdr_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@L)
    t=code(mm(f"{TD}/taa_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@L)
    a=mm(f"{TD}/depth_1_{f}.rgba32f",np.float32,4)
    own=(a[...,0]!=-1)&(a[...,2]>20000)&(a[...,1]==-1)&(a[...,3]==1)
    e=(box((np.abs(h-box(h,2))>4).astype(np.float32),1)>0)&own
    D.append((t-h)[e]); H.append(h);T.append(t);E.append(e)
d=np.concatenate(D); p=lambda x,q: np.percentile(x,q)
print(f"run{RUN} x{X0}-{X1} y{Y0}-{Y1} {F0}..{F0+N-1} per-frame taa-hdr on edge px n={d.size}: p10 {p(d,10):.2f} p25 {p(d,25):.2f} p50 {p(d,50):.2f} mean {d.mean():.2f}")
e=np.logical_and.reduce(E); hm=np.mean(H,0); tm=np.mean(T,0); line=np.abs(hm-box(hm,2))>4
m=e&line; dm=(tm-hm)[m]
print(f"  burst-mean taa-hdr on edge px present in all frames n={e.sum()}: p10 {p((tm-hm)[e],10):.2f} p50 {p((tm-hm)[e],50):.2f} mean {(tm-hm)[e].mean():.2f}; on bright line side (mean hdr above local mean by >4) n={int((m&(hm>box(hm,2))).sum())}: p10 {p((tm-hm)[m&(hm>box(hm,2))],10):.2f} p50 {p((tm-hm)[m&(hm>box(hm,2))],50):.2f}")
