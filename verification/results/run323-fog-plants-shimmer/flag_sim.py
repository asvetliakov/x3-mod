# CPU model of the folded resolve's thin flag (src/temporal/resolve.hlsl thinFlag / fragmentedDepth / emissiveVote) on a crop,
# from the F8 dumps: lane depth_1_N (.r z/w, .a vote), motion_1_N (.w == 1 routed), hdr_1_N (pre-resolve scene colour).
#   vote     = valid .r and .a in [0,1)
#   search   = any of four 7-tap lines (h, v, diag, anti-diag; taps -3..+3) has >= 2 class changes of .r (lineBackground:
#              sentinel <= -0.5, or valid and (1-q)*1.1 < 1-d); what --taa-thin-region-source both adds (and Run82's default)
#   emissive = valid .r, motion .w == 1, luma > E (=1) and 3x3 luma min * 3 < centre (X3M_TAA_THIN_REGION_EMISSIVE=1)
# Classes: owned = routed .a==1 .r valid; remainder = sentinel (.r == -1) within 3 px of a routed pixel.
# Also, for consecutive frames, 'new' = flagged this frame at a texel not flagged the previous frame (unreprojected; the
# folded box gate's one-frame-late set, upper bound: the real hold keeps h for L frames).
# usage: flag_sim.py RUN X0 Y0 X1 Y1 FRAME... [env E=1 SOURCE=vote|both]
import sys,os,numpy as np
run=sys.argv[1]; X0,Y0,X1,Y1=map(int,sys.argv[2:6]); frames=sys.argv[6:]; E=float(os.environ.get('E',1)); M=4
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def lane(f):
    y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M
    a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[y0:y1,x0:x1]
    m=np.fromfile(f"/tmp/x3-bottleX3-run{run}/motion_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[y0:y1,x0:x1]
    h=np.fromfile(f"/tmp/x3-bottleX3-run{run}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[y0:y1,x0:x1,:3].astype(np.float32)
    return a,m,h
def valid(v): return (v>=0)&(v<=1)
def sent(v): return (v<=-0.5)&(v>=-1e30)
def bgof(q,d): return sent(q)|(valid(q)&((1-q)*1.1<1-d))
def cc(a,b): return (valid(a)&bgof(b,a))|(valid(b)&bgof(a,b))
def shift(x,dy,dx):
    return np.roll(np.roll(x,-dy,0),-dx,1)   # x[y+dy, x+dx]; wrap only inside the 4 px margin
def search(r):
    out=np.zeros(r.shape,bool)
    for dy,dx in ((0,1),(1,0),(1,1),(-1,1)):
        ch=np.zeros(r.shape,np.int32); prev=shift(r,-3*dy,-3*dx)
        for t in range(-2,4):
            nxt=shift(r,t*dy,t*dx); ch+=cc(prev,nxt); prev=nxt
        out|=ch>=2
    return out
sl=(slice(M,-M),slice(M,-M)); prevflag=None
for f in frames:
    a,m,h=lane(f); r=a[...,0]; A=a[...,3]
    vote=valid(r)&(A>=0)&(A<1)
    s=search(r)
    L=np.nan_to_num(h@LUMA,nan=0,posinf=0,neginf=0); L=np.where(np.abs(L)<=65000,np.maximum(L,0),0)
    mn=L.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): mn=np.minimum(mn,shift(L,dy,dx))
    emi=valid(r)&(m[...,3]==1)&(L>E)&(mn*3<L)
    routed=r!=-1; own=routed&valid(r)&(A==1)
    near=np.zeros_like(routed)
    for dy in range(-3,4):
        for dx in range(-3,4): near|=shift(routed,dy,dx)
    rem=near&(r==-1)
    fv=vote|emi; fb=vote|s|emi
    row=f"frame {f}"
    for name,cls in (('owned',own),('remainder',rem)):
        c=cls[sl]; n=int(c.sum())
        row+=f" | {name} n{n} vote {int((vote[sl]&c).sum())} emissive {int((emi[sl]&c).sum())} search {int((s[sl]&c).sum())} flag_vote_src {int((fv[sl]&c).sum())} flag_both_src {int((fb[sl]&c).sum())}"
    if prevflag is not None:
        for name,fl,pf in (('vote',fv,prevflag[0]),('both',fb,prevflag[1])):
            nw=fl[sl]&~pf[sl]; row+=f" | new_{name} {int(nw.sum())}/{int(fl[sl].sum())}"
    prevflag=(fv,fb); print(row)
