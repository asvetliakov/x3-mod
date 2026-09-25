# Q3: why the rest sparkles of rest_pixels.py are outside the thin region: per sparkle pixel the three flag sources of
# resolve.hlsl thinFlag (vote: lane .a < 1; search: fragmentedDepth, 7-tap lines, two class changes; emissive: L > 1 and
# 3x3 min * 3 < L), the depth context of its 7x7 (any sentinel / background-class texel), the distance to the nearest
# region pixel (taa_age h > 0) and the region share of the plant crop.
# usage: flag_context.py X0 Y0 X1 Y1 F0 N   (run327 dumps; sparkle list recomputed as rest_pixels.py)
import sys,os,numpy as np
X0,Y0,X1,Y1,F0,N=map(int,sys.argv[1:7]); MG=6; WMIN=20000
TD='/tmp/x3-bottleX3-run327'; BD='/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'
M=12; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; cy,cx=slice(M,-M),slice(M,-M); LU=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120))[y0:y1,x0:x1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
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
def valid(v): return (v>=0)&(v<=1)
def sent(v): return (v<=-0.5)&(v>=-1e30)
def bgof(q,d): return sent(q)|(valid(q)&((1-q)*1.1<1-d))
def cc(a,b): return (valid(a)&bgof(b,a))|(valid(b)&bgof(a,b))
def search(r):
    out=np.zeros(r.shape,bool)
    for dy,dx in ((0,1),(1,0),(1,1),(-1,1)):
        ch=np.zeros(r.shape,np.int32); prev=sh(r,-3*dy,-3*dx)
        for t in range(-2,4):
            nxt=sh(r,t*dy,t*dx); ch+=cc(prev,nxt); prev=nxt
        out|=ch>=2
    return out
fr=[]
for i in range(N):
    f=F0+i; hdr=mm(f'{TD}/hdr_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32); L=np.where(np.isfinite(hdr),hdr,0)@LU
    tc=code(mm(f'{TD}/taa_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32)@LU); a=mm(f'{TD}/depth_1_{f}.rgba32f',np.float32,4)
    v=np.abs(mm(f'{BD}/taa_age_1_{f}.r32f',np.float32,1)); h=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64)%128
    r=a[...,0]; routed=r!=-1; far=routed&(a[...,2]>WMIN); plant=far|((~routed)&dil(far,3))
    mn=L.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): mn=np.minimum(mn,sh(L,dy,dx))
    emi=valid(r)&(L>1)&(mn*3<L); vote=valid(r)&(a[...,3]>=0)&(a[...,3]<1)
    bgnb=np.zeros(r.shape,bool)
    for dy in range(-3,4):
        for dx in range(-3,4): bgnb|=bgof(sh(r,dy,dx),r)|~valid(sh(r,dy,dx))
    fr.append(dict(tc=tc,h=h,plant=plant,own=far&(a[...,1]==-1)&(a[...,3]==1),vote=vote,srch=search(r),emi=emi,bgnb=bgnb))
tot={'n':0,'vote':0,'search':0,'emissive':0,'7x7 has sentinel/bg texel':0,'region(h>0)':0}; dist=[]
for i in range(1,N-1):
    d=fr[i]; st=d['tc']-np.maximum(mx3(fr[i-1]['tc']),mx3(fr[i+1]['tc'])); sp=((st>MG)&d['plant'])[cy,cx]
    reg=d['h']>0; ys,xs=np.nonzero(reg)
    for y,x in zip(*np.nonzero(sp)):
        y+=M; x+=M; tot['n']+=1; tot['vote']+=int(d['vote'][y,x]); tot['search']+=int(d['srch'][y,x]); tot['emissive']+=int(d['emi'][y,x]); tot['7x7 has sentinel/bg texel']+=int(d['bgnb'][y,x]); tot['region(h>0)']+=int(reg[y,x])
        dist.append(float(np.sqrt(((ys-y)**2+(xs-x)**2).min())) if len(ys) else 1e9)
print(f'crop x{X0}-{X1} y{Y0}-{Y1} frames {F0}..{F0+N-1}: sparkle flag sources {tot}')
dist=np.array(dist); print(f'distance to nearest region px (h>0): min {dist.min():.1f} p50 {np.median(dist):.1f} max {dist.max():.1f}; <=2 px {int((dist<=2).sum())}')
own=np.concatenate([f['own'][cy,cx].ravel() for f in fr]); pl=np.concatenate([f['plant'][cy,cx].ravel() for f in fr])
for nm in ('vote','srch','emi'):
    v=np.concatenate([f[nm][cy,cx].ravel() for f in fr]); print(f'  {nm}: share of owned plant px {v[own].mean():.4f}, of plant px {v[pl].mean():.4f}')
h=np.concatenate([(f['h'][cy,cx]>0).ravel() for f in fr]); print(f'  region h>0: share of owned plant px {h[own].mean():.4f}, of plant px {h[pl].mean():.4f}')
bg=np.concatenate([f['bgnb'][cy,cx].ravel() for f in fr]); print(f'  7x7 has sentinel/bg texel: share of owned plant px {bg[own].mean():.4f}')
