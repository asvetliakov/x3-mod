# Characterise the taa_1 one-frame sparkles found by sparkles.py (same definition, same alignment by the motion_1 median):
# per sparkle pixel p at frame N: lane class; region state from taa_age_1 (h_N: 8 flagged, 1..7 held, 0 out) and the previous
# frame's hold at the same texel (unreprojected, 2x2 block = the half-resolution box targets' marker); the CPU flag model of
# resolve.hlsl thinFlag (flag_sim.py: vote | search | emissive, source both) at N and at N-1 (same texel); whether the input
# (hdr_1) had its own one-frame spike within 1 px (input leak) and whether the resolved code exceeds the max of the current
# hdr codes over the 3x3 (the plain clip's bound) / 7x7 (the region box's bound) by 2 codes (history-sourced brightness;
# per-luma approximation of the per-channel box in the weighted domain). Also cross-tabulates the flag model against h_N == 8.
# Distance to the near routed (player ship, w < WMIN) pixels and current-only returns (|taa - hdr| < 0.05 code) separate the
# ship-silhouette disocclusion from the plant's own lines.
# 'moves with ship': the pixel is not a sparkle when the neighbour frames are aligned by the ship's median motion instead
# (bright content attached to the ship silhouette, not a one-frame event).
# G = genuine one-frame sparkles (not 'moves with ship').
# usage: sparkle_rows.py X0 Y0 X1 Y1 F0 N [env MG=6 WMIN=20000 EX=0 (example rows per frame)]
import sys,os,numpy as np
X0,Y0,X1,Y1,F0,N=map(int,sys.argv[1:7]); E=os.environ.get; MG=float(E('MG',6)); WMIN=float(E('WMIN',20000))
TD='/tmp/x3-bottleX3-run327'; BD='/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'
M=24; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; H,W=Y1-Y0,X1-X0; cy,cx=slice(M,M+H),slice(M,M+W)
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120))[y0:y1,x0:x1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def shift(x,dy,dx): return np.roll(np.roll(x,-dy,0),-dx,1)
def mxr(x,r):
    o=x.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1): o=np.maximum(o,shift(x,dy,dx))
    return o
def dil(m,r): return mxr(m.astype(np.uint8),r)>0
def valid(v): return (v>=0)&(v<=1)
def sent(v): return (v<=-0.5)&(v>=-1e30)
def bgof(q,d): return sent(q)|(valid(q)&((1-q)*1.1<1-d))
def cc(a,b): return (valid(a)&bgof(b,a))|(valid(b)&bgof(a,b))
def search(r):
    out=np.zeros(r.shape,bool)
    for dy,dx in ((0,1),(1,0),(1,1),(-1,1)):
        ch=np.zeros(r.shape,np.int32); prev=shift(r,-3*dy,-3*dx)
        for t in range(-2,4):
            nxt=shift(r,t*dy,t*dx); ch+=cc(prev,nxt); prev=nxt
        out|=ch>=2
    return out
def load(f):
    hl=mm(f"{TD}/hdr_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA
    tl=mm(f"{TD}/taa_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA
    a=mm(f"{TD}/depth_1_{f}.rgba32f",np.float32,4); mo=mm(f"{TD}/motion_1_{f}.rgba32f",np.float32,4)
    v=np.abs(mm(f"{BD}/taa_age_1_{f}.r32f",np.float32,1)); held=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64); h=held%128
    r=a[...,0]; A=a[...,3]; routed=r!=-1; far=routed&(a[...,2]>WMIN)
    own=far&(a[...,1]==-1)&(A==1); thin=far&(A>=0)&(A<1); oth=far&~own&~thin; rem=(~routed)&dil(far,3)
    L=np.nan_to_num(hl,nan=0,posinf=0,neginf=0); L=np.where(np.abs(L)<=65000,np.maximum(L,0),0); mn=L.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): mn=np.minimum(mn,shift(L,dy,dx))
    vote=valid(r)&(A>=0)&(A<1); emi=valid(r)&(mo[...,3]==1)&(L>1)&(mn*3<L); flag=vote|search(r)|emi
    gy,gx=np.mgrid[y0:y1,x0:x1]; dy=mo[...,1]*1440-(gy+0.5); dx=mo[...,0]*5120-(gx+0.5); sel=far[cy,cx]
    disp=(float(np.median(dy[cy,cx][sel])),float(np.median(dx[cy,cx][sel])))
    sn=(routed&~far)[cy,cx]; dispS=(float(np.median(dy[cy,cx][sn])),float(np.median(dx[cy,cx][sn]))) if sn.any() else disp
    near=routed&~far
    return dict(dispS=dispS,near=near,hc=code(hl),tc=code(tl),h=h,cl={'owned':own,'thin':thin,'other':oth,'remainder':rem},flag=flag,emi=emi,disp=disp,hl=hl)
fr=[load(F0+i) for i in range(N)]
def s(i,j,key='disp'):
    d=fr[i][key] if j==i-1 else tuple(-x for x in fr[j][key]); return int(round(d[0])),int(round(d[1]))
def sh(x,t): return x[M+t[0]:M+t[0]+H,M+t[1]:M+t[1]+W]
def score(i,k,key='disp'):
    return fr[i][k][cy,cx]-np.maximum(sh(mxr(fr[i-1][k],1),s(i,i-1,key)),sh(mxr(fr[i+1][k],1),s(i,i+1,key)))
cnt={}; rows=0; agree=np.zeros((2,2),int)
def add(key,n): cnt[key]=cnt.get(key,0)+n
for i in range(1,N-1):
    plant=np.zeros((H,W),bool)
    for m in fr[i]['cl'].values(): plant|=m[cy,cx]
    st=score(i,'tc'); sp=(st>MG)&plant
    sin=dil(score(i,'hc')>MG,1)[...,] if False else None
    hs=score(i,'hc')>MG; hsd=np.zeros_like(hs)
    for dy in(-1,0,1):
        for dx in(-1,0,1): hsd|=np.roll(np.roll(hs,dy,0),dx,1)
    tc=fr[i]['tc'][cy,cx]; ex3=tc>mxr(fr[i]['hc'],1)[cy,cx]+2; ex7=tc>mxr(fr[i]['hc'],3)[cy,cx]+2
    hN=fr[i]['h'][cy,cx]; reg=hN>0
    ph=fr[i-1]['h']>0; yy=(np.arange(y0,y1)//2)*2-y0; xx=(np.arange(x0,x1)//2)*2-x0; blk=np.zeros_like(ph)
    for oy in(0,1):
        for ox in(0,1): blk|=ph[np.clip(yy+oy,0,ph.shape[0]-1)][:,np.clip(xx+ox,0,ph.shape[1]-1)]
    mk=blk[cy,cx]; pflag=fr[i-1]['flag'][cy,cx]; flag=fr[i]['flag'][cy,cx]
    f8=(hN==8)
    nr=fr[i]['near']; ship3=dil(nr,3)[cy,cx]; ship8=dil(nr,8)[cy,cx]; cur_only=np.abs(tc-fr[i]['hc'][cy,cx])<0.05; big=st>12; shipmov=score(i,'tc','dispS')<=MG
    for a_ in (0,1):
        for b_ in (0,1): agree[a_,b_]+=int((plant&(flag==bool(a_))&(f8==bool(b_))).sum())
    for base,msk in (('sparkle',sp),('plant',plant)):
        for n,c in fr[i]['cl'].items():
            c=c[cy,cx]&msk; add((base,n,'n'),int(c.sum()))
            for nm,x in (('region',reg),('flag8',f8),('lead(reg,no marker)',reg&~mk),('lag(reg,marker)',reg&mk),('out,marker(trail)',~reg&mk),('out,no marker',~reg&~mk),
                         ('model_flag_N',flag),('model_flag_N-1',pflag),('model new(N&!N-1)',flag&~pflag),('input spike<=1px',hsd),('>max3x3 hdr+2',ex3),('>max7x7 hdr+2',ex7),('emissive',fr[i]['emi'][cy,cx]),('hdr L>1',fr[i]['hl'][cy,cx]>1),('ship<=3px',ship3),('ship<=8px',ship8),('current-only',cur_only),('score>12',big),('score>12&ship>8px',big&~ship8),('region&ship>8px',reg&~ship8),('out&ship>8px',~reg&~ship8),('cur-only&ship>8px',cur_only&~ship8),('moves with ship',shipmov),('ship<=8px&not ship-moving',ship8&~shipmov),('ship<=8px&not ship-moving&score>12',ship8&~shipmov&big),('G',~shipmov),('G&region',~shipmov&reg),('G&flag8',~shipmov&f8),('G&lead',~shipmov&reg&~mk),('G&lag',~shipmov&reg&mk),('G&out',~shipmov&~reg),('G&out&border2',~shipmov&~reg&dil(reg,2)),('G&spike',~shipmov&hsd),('G&score>12',~shipmov&big),('G&out&score>12',~shipmov&~reg&big),('G&modelN-1',~shipmov&pflag),('G&model new',~shipmov&flag&~pflag),('G&ship>8px',~shipmov&~ship8)):
                add((base,n,nm),int((c&x).sum()))
    ys,xs=np.nonzero(sp)
    for y,x in list(zip(ys,xs))[:int(E('EX',0))]:
        print(f"  ex frame {F0+i} x{X0+x} y{Y0+y} score {st[y,x]:.1f} taa {tc[y,x]:.1f} hdr {fr[i]['hc'][cy,cx][y,x]:.1f} h {hN[y,x]} marker {int(mk[y,x])} ex3 {int(ex3[y,x])} ex7 {int(ex7[y,x])} spike {int(hsd[y,x])}")
print(f"crop x{X0}-{X1} y{Y0}-{Y1} frames {F0}..{F0+N-1} MG={MG}")
names=['n','region','flag8','lead(reg,no marker)','lag(reg,marker)','out,marker(trail)','out,no marker','model_flag_N','model_flag_N-1','model new(N&!N-1)','input spike<=1px','>max3x3 hdr+2','>max7x7 hdr+2','emissive','hdr L>1','ship<=3px','ship<=8px','current-only','score>12','score>12&ship>8px','region&ship>8px','out&ship>8px','cur-only&ship>8px','moves with ship','ship<=8px&not ship-moving','ship<=8px&not ship-moving&score>12','G','G&region','G&flag8','G&lead','G&lag','G&out','G&out&border2','G&spike','G&score>12','G&out&score>12','G&modelN-1','G&model new','G&ship>8px']
for base in ('sparkle','plant'):
    for n in ('owned','thin','other','remainder'):
        if cnt.get((base,n,'n'),0): print(f"{base:7s} {n:9s} "+' | '.join(f"{nm} {cnt.get((base,n,nm),0)}" for nm in names))
print(f"flag model vs actual h==8 on plant px (rows model 0/1, cols actual 0/1): {agree.tolist()}")
