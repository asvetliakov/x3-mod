# Design evidence for docs/architecture/taa-thin-classification.md (read-only, no Wine).
# Per-pixel "temporal witness" census over a REST burst: at every routed valid-depth pixel, is the previous
# resolved history (taa_{N-1}, same texel: f = 0 at rest) OUTSIDE the current frame's 3x3 box (the resolve's
# clip bound, weighted domain, luma code margin T)? That is the per-pixel signature of a feature the current
# jittered sample missed (history above the 3x3 max) or hit (history below the 3x3 min), and the set of pixels a
# depth-independent witness rule would send to the 7x7 box. Binned by view z (the pixel footprint) and by the
# installed far ramp (80/130 footprint units), with the region hold h, the thin vote (lane .a < 1) and whether the
# 7x7 box of the current frame would contain the history (the bounded fix) or not (the 7x7 also clips).
# Second census: the "mark edges as thin" rule = the emissive vote at lower thresholds E (routed, L > E, 3x3 luma
# min < L / 3): how many pixels it flags per frame by view-z bin.
# usage: witness_census.py RUN F0 N [KX=1.7605 P00 P22 P32 T=2 AGEDIR]
import sys,os,numpy as np
E=os.environ.get; RUN=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3])
K=float(E('KX',1.7605)); P00=float(E('P00',0.4999979)); P22=float(E('P22',1.00000298)); P32=float(E('P32',-6.00001812)); WD=5120
T=float(E('T',2))
sc=P00*WD/2; D0=P22+P32/(80*sc); D1=P22+P32/(130*sc)
TD=f'/tmp/x3-bottleX3-run{RUN}'; BD=E('AGEDIR','/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures')
LU=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120)))
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def weigh(c): return c/(1+K*np.maximum(c@LU,0))[...,None]
def sh(x,dy,dx): return np.roll(np.roll(x,-dy,0),-dx,1)
def box(x,r):
    lo=x.copy(); hi=x.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1):
            if dy==0 and dx==0: continue
            n=sh(x,dy,dx); lo=np.minimum(lo,n); hi=np.maximum(hi,n)
    return lo,hi
def mn3(x):
    o=x.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): o=np.minimum(o,sh(x,dy,dx))
    return o
def load(f):
    hdr=mm(f'{TD}/hdr_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32); hdr=np.where(np.isfinite(hdr),hdr,0)
    a=mm(f'{TD}/depth_1_{f}.rgba32f',np.float32,4)
    t=mm(f'{TD}/taa_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32); t=np.where(np.isfinite(t),t,0)
    ap=f'{BD}/taa_age_1_{f}.r32f'
    if not os.path.exists(ap): ap=f'{TD}/taa_age_1_{f}.r32f'
    v=np.abs(mm(ap,np.float32,1)); held=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64); h=held%128
    return dict(hdr=hdr,a=a,taa=t,h=h)
zb=[(0,2e4),(2e4,6.4e4),(6.4e4,1.024e5),(1.024e5,1.664e5),(1.664e5,1e12)]
zn=['<20k','20-64k','64-102k','102-166k','>166k']
print(f'run{RUN} frames {F0+1}..{F0+N-1} (history = previous frame, rest) k={K} T={T} codes; far ramp 80/130 = view z {80*sc:.0f}..{130*sc:.0f}')
prev=load(F0); tot={}
for i in range(1,N):
    f=F0+i; d=load(f)
    a=d['a']; r=a[...,0]; wz=a[...,2]; routed=(r>=0)&(r<=1)&(a[...,0]!=-1); valid=routed
    cur=weigh(d['hdr']); old=weigh(prev['taa'])
    lo3,hi3=box(cur,1); lo7,hi7=box(cur,3)
    # luma code of the history against the 3x3 box's luma-code bounds (per-channel clamp collapsed to luma: conservative)
    oc=code(old@LU); hc3=code(hi3@LU); lc3=code(lo3@LU); hc7=code(hi7@LU); lc7=code(lo7@LU)
    above3=oc-hc3>T; below3=lc3-oc>T; out3=(above3|below3)&valid
    in7=(oc<=hc7+T)&(oc>=lc7-T)
    vote=(a[...,3]>=0)&(a[...,3]<1)&routed; reg=d['h']>0
    nv=valid.sum()
    print(f'frame {f}: valid routed px {nv} | history outside 3x3 box by >{T:g} codes: {out3.sum()} ({100*out3.sum()/nv:.3f} % of valid) above {(above3&valid).sum()} below {(below3&valid).sum()} | of those: in region(h>0) {(out3&reg).sum()} voted {(out3&vote).sum()} inside 7x7 box {(out3&in7).sum()} outside 7x7 too {(out3&~in7).sum()}')
    for (z0,z1),name in zip(zb,zn):
        sel=valid&(wz>=z0)&(wz<z1); n=sel.sum()
        if not n: continue
        o=(out3&sel).sum(); key=name; t=tot.setdefault(key,[0,0,0,0,0])
        t[0]+=n; t[1]+=o; t[2]+=(out3&sel&in7).sum(); t[3]+=(out3&sel&reg).sum(); t[4]+=(out3&sel&vote).sum()
        print(f'   z {name:>9}: px {n:8d} outside3x3 {o:6d} ({100*o/n:.3f} %) in7x7 {(out3&sel&in7).sum():6d} region {(out3&sel&reg).sum():6d} voted {(out3&sel&vote).sum():6d}')
    # "mark edges as thin": the emissive vote at lower E, per view-z bin
    L=d['hdr']@LU; m3=mn3(L)
    for Ev in (1.0,0.5,0.25,0.1):
        flag=routed&(L>Ev)&(m3<L/3)
        row=' '.join(f'{name}:{(flag&(wz>=z0)&(wz<z1)).sum()}' for (z0,z1),name in zip(zb,zn))
        print(f'   edge-vote E={Ev}: flagged routed px {flag.sum()} ({100*flag.sum()/nv:.2f} % of valid) by z {row}')
    prev=d
print('totals over frames by z bin: px, outside3x3, in7x7, region, voted')
for k,t in tot.items(): print(f'   {k:>9}: {t[0]} {t[1]} ({100*t[1]/t[0]:.3f} %) {t[2]} {t[3]} {t[4]}')
