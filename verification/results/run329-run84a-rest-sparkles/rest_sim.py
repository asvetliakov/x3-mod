# Steady-state replay of the resolve at REST on a crop: the 8 dumped hdr frames (one jitter period, camera static) are
# replayed cyclically (CYC cycles) through resolve.hlsl's out-of-region path (history = previous output texel, f = 0 at
# rest) under variants, then the last cycle is scored with the sparkles.py detector (output code c_N - max3x3(c_N-1, c_N+1)
# > MG, cyclic neighbours) on plant pixels. The base variant reproduces the shipped Run83 / Run84 behaviour at rest
# (farOpen = 1 under both gates). Weighted domain k (KX), farw from depth .r as rest_pixels.py.
# Variants: base      keep = 0.9 + farw (0.9846 - 0.9), 3x3 min/max ∩ mean +- 1.25 sigma clip (shipped)
#           far1      keep = 0.9846 on every far routed px (farw = 1: e.g. far_f1 <= the plant footprint), same clip
#           gamma075  base keep, clip mean +- 0.75 sigma (a tighter variance clip)
#           box7      base keep, clip = 7x7 min/max only (a wider, jitter-tolerant neighbourhood)
#           far1box7  keep 0.9846 + 7x7 min/max clip
#           region    keep = max(base, 0.97), no clip (the thin region hold at rest: a = b = openS = 1, relax 1)
# usage: rest_sim.py RUN X0 Y0 X1 Y1 F0 [env N=8 CYC=12 MG=6 WMIN=20000 KX=1.7605 P00 P22 P32 CMP=1 (compare base to taa_1)]
import sys,os,numpy as np
E=os.environ.get; RUN=sys.argv[1]; X0,Y0,X1,Y1,F0=map(int,sys.argv[2:7]); N=int(E('N',8)); CYC=int(E('CYC',12))
MG=float(E('MG',6)); WMIN=float(E('WMIN',20000)); K=float(E('KX',1.7605))
P00=float(E('P00',0.4999979)); P22=float(E('P22',1.00000298)); P32=float(E('P32',-6.00001812)); sc=P00*5120/2
D0=P22+P32/(80*sc); D1=P22+P32/(130*sc)
TD=f'/tmp/x3-bottleX3-run{RUN}'; M=4; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; cy,cx=slice(M,-M),slice(M,-M)
LU=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch))[y0:y1,x0:x1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def weigh(c): return c/(1+K*np.maximum(c@LU,0))[...,None]
def unweigh(c): return c/np.maximum(1-K*np.maximum(c@LU,0),1/65504)[...,None]
def sh(x,dy,dx): return np.roll(np.roll(x,-dy,0),-dx,1)
def mx3(x):
    o=x.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): o=np.maximum(o,sh(x,dy,dx))
    return o
def dil(m,r):
    o=m.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1): o|=sh(m,dy,dx)
    return o
cur=[];farw=None;plant=None;own=None
for i in range(N):
    h=mm(f'{TD}/hdr_1_{F0+i}.rgba16f',np.float16,4)[...,:3].astype(np.float32); h=np.where(np.isfinite(h),h,0); cur.append(weigh(h))
    if i==0:
        a=mm(f'{TD}/depth_1_{F0}.rgba32f',np.float32,4); r=a[...,0]; routed=r!=-1; far=routed&(a[...,2]>WMIN)
        own=far&(a[...,1]==-1)&(a[...,3]==1); plant=far|((~routed)&dil(far,3))
        fw=np.where((r>=0)&(r<=1),np.clip((r-D0)/(D1-D0),0,1),0); farw=np.floor(fw*255+0.5)/255
def bounds(c,mode):
    if mode in('mm7',):
        lo=c.copy(); hi=c.copy()
        for dy in range(-3,4):
            for dx in range(-3,4): n_=sh(c,dy,dx); lo=np.minimum(lo,n_); hi=np.maximum(hi,n_)
        return lo,hi
    g=1.25 if mode=='var' else 0.75
    lo=c.copy(); hi=c.copy(); s=0; s2=0
    for dy in(-1,0,1):
        for dx in(-1,0,1): n_=sh(c,dy,dx); lo=np.minimum(lo,n_); hi=np.maximum(hi,n_); s=s+n_; s2=s2+n_*n_
    m=s/9; sg=np.sqrt(np.maximum(s2/9-m*m,0)); return np.maximum(lo,m-g*sg),np.minimum(hi,m+g*sg)
B=[bounds(c,'var') for c in cur]; G=[bounds(c,'g075') for c in cur]; B7=[bounds(c,'mm7') for c in cur]
kb=0.9+farw*(64/65-0.9); kf=np.where(plant,64/65,kb)
V={'base':(kb,B),'far1':(kf,B),'gamma075':(kb,G),'box7':(kb,B7),'far1box7':(kf,B7),'region':(np.maximum(kb,0.97),None)}
jm=np.mean([code(unweigh(c)@LU) for c in cur],0)  # per-pixel mean input code over the period (display reference)
print(f'run{RUN} crop x{X0}-{X1} y{Y0}-{Y1} frames {F0}..{F0+N-1} cycles {CYC} k={K}; plant px {int(plant[cy,cx].sum())}, owned {int(own[cy,cx].sum())}; keep base on owned p50 {np.median(kb[cy,cx][own[cy,cx]]):.4f}')
ic=np.array([code(unweigh(c)@LU) for c in cur]); isc=[ic[i]-np.maximum(mx3(ic[i-1]),mx3(ic[(i+1)%N])) for i in range(N)]
print(f'input (hdr) one-frame spikes on plant px per frame: {[int(((s>MG)&plant)[cy,cx].sum()) for s in isc]}; input temporal std (code) on plant p50 {np.median(ic.std(0)[cy,cx][plant[cy,cx]]):.2f} p99 {np.percentile(ic.std(0)[cy,cx][plant[cy,cx]],99):.2f}')
res={}
for name,(keep,bd) in V.items():
    out=cur[0].copy(); outs=[None]*N
    for c in range(CYC):
        for i in range(N):
            old=out if bd is None else np.clip(out,bd[i][0],bd[i][1])
            out=cur[i]+(old-cur[i])*keep[...,None]; outs[i]=out
    oc=np.array([code(unweigh(o)@LU) for o in outs])
    sc_=[oc[i]-np.maximum(mx3(oc[i-1]),mx3(oc[(i+1)%N])) for i in range(N)]
    cnt=[int(((s>MG)&plant)[cy,cx].sum()) for s in sc_]; mx=max(float(s[cy,cx][plant[cy,cx]].max()) for s in sc_)
    stdv=oc.std(0)[cy,cx][plant[cy,cx]]; bias=(oc.mean(0)-jm)[cy,cx][plant[cy,cx]]
    edge=ic.std(0)[cy,cx][plant[cy,cx]]>8
    res[name]=oc
    print(f'{name:9s} sparkles/frame {cnt} total {sum(cnt)} max score {mx:.1f} | out temporal std p50 {np.median(stdv):.3f} p99 {np.percentile(stdv,99):.2f} | mean(out)-mean(input) on edge px (input std>8, n {int(edge.sum())}) p50 {np.median(bias[edge]):+.1f} p10 {np.percentile(bias[edge],10):+.1f}')
if E('CMP','1')=='1' and os.path.exists(f'{TD}/taa_1_{F0}.rgba16f'):
    tc=np.array([code(mm(f'{TD}/taa_1_{F0+i}.rgba16f',np.float16,4)[...,:3].astype(np.float32)@LU) for i in range(N)])
    BD=os.environ.get('AGEDIR','/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures')
    hs=[]
    for i in range(N):
        v=np.abs(np.array(np.memmap(f'{BD}/taa_age_1_{F0+i}.r32f',np.float32,'r',shape=(1440,5120))[y0:y1,x0:x1])); hs.append(np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64)%128)
    outr=plant&~dil(np.any(np.array(hs)>0,0),1)  # never in the region hold in the burst, 1 px margin
    e=np.abs(res['base']-tc)[:,cy,cx][:,outr[cy,cx]]
    print(f'out-of-region plant px (never h>0 in the burst, 1 px margin) n {int(outr[cy,cx].sum())}: base replay vs actual taa_1 |err| code p50 {np.median(e):.3f} p99 {np.percentile(e,99):.3f}')
    for nm,arr in (('actual taa_1',tc),)+tuple((k,res[k]) for k in V):
        c=[int((((arr[i]-np.maximum(mx3(arr[i-1]),mx3(arr[i+1])))>MG)&outr)[cy,cx].sum()) for i in range(1,N-1)]
        print(f'  interior frames 1..{N-2} (non-cyclic), out-of-region plant px: {nm:12s} sparkles {c} total {sum(c)}')
