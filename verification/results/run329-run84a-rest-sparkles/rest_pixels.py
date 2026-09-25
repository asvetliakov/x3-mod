# Run 83 B (run327) REST burst: per one-frame TAA sparkle pixel, the resolve's terms rebuilt from the dumps and checked
# against the actual resolved taa_1. Model = resolve.hlsl (camera-gate hold program) outside the region (h = 0):
#   weighted domain w(c) = c / (1 + k luma(c)), k = the hdr_frame row's exposure multiplier (KX env, default run327 1.76050)
#   clip: per channel 3x3 min/max of the current hdr intersected with mean +- 1.25 sigma (lines 916-953)
#   history at rest: the previous taa_1 texel (f = 0 at rest: 5-tap Catmull-Rom is the texel), clipped = clamp(old, low, high)
#   keep = 0.9 + farw * farOpen * (min(age/(age+1), 0.985) - 0.9)  (line 1060), age = floor|taa_age_{N-1}| (<= 64)
#   farw = quant255(saturate((d - d0) / (d1 - d0))), d = depth .r, d0/d1 = p22 + p32 / (f * p00 * W / 2), f = 80 / 130
#   farOpen = screen gate 1 - saturate((speed - 0.03) / 0.22) (Run83); camera gate openC at rest also 1 (see q2)
#   out = unweigh(lerp(w(cur), clipped, keep)); delta term split: cur term (1-keep)(w(cur)-old), clip term keep(clipped-old)
# Sparkle = same definition as ../run327-run83b-sparkles/sparkles.py (taa code c_N - max3x3(c_N-1, c_N+1) > MG, shift 0 at
# rest) on plant pixels (far routed w > WMIN, or unrouted within 3 px of one).
# usage: rest_pixels.py RUN X0 Y0 X1 Y1 F0 N [env MG=6 WMIN=20000 KX=1.7605 P00 P22 P32 EX=40 AGEDIR]
import sys,os,numpy as np
E=os.environ.get; RUN=sys.argv[1]; X0,Y0,X1,Y1,F0,N=map(int,sys.argv[2:8])
MG=float(E('MG',6)); WMIN=float(E('WMIN',20000)); K=float(E('KX',1.7605))
P00=float(E('P00',0.4999979)); P22=float(E('P22',1.00000298)); P32=float(E('P32',-6.00001812)); WD=5120
sc=P00*WD/2; D0=P22+P32/(80*sc); D1=P22+P32/(130*sc)
TD=f'/tmp/x3-bottleX3-run{RUN}'; BD=E('AGEDIR','/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures')
M=4; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; H,W=Y1-Y0,X1-X0; cy,cx=slice(M,M+H),slice(M,M+W)
LU=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120))[y0:y1,x0:x1])
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
HAVE_TAA=os.path.exists(f'{TD}/taa_1_{F0}.rgba16f')
fr=[]
for i in range(N):
    f=F0+i; hdr=mm(f'{TD}/hdr_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32); hdr=np.where(np.isfinite(hdr),hdr,0)
    a=mm(f'{TD}/depth_1_{f}.rgba32f',np.float32,4); mo=mm(f'{TD}/motion_1_{f}.rgba32f',np.float32,4)
    d=dict(hdr=hdr,hc=code(hdr@LU),a=a,mo=mo)
    if HAVE_TAA:
        t=mm(f'{TD}/taa_1_{f}.rgba16f',np.float16,4)[...,:3].astype(np.float32); d['taa']=t; d['tc']=code(t@LU)
        ap=f'{BD}/taa_age_1_{f}.r32f'
        if os.path.exists(ap):
            v=np.abs(mm(ap,np.float32,1)); d['age']=np.where(v<=65,np.floor(v),1)
            held=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64); d['h']=held%128
    routed=a[...,0]!=-1; far=routed&(a[...,2]>WMIN); d['far']=far; d['own']=far&(a[...,1]==-1)&(a[...,3]==1)
    d['plant']=far|((~routed)&dil(far,3))
    r=a[...,0]; fw=np.where((r>=0)&(r<=1),np.clip((r-D0)/(D1-D0),0,1),0); d['farw']=np.floor(fw*255+0.5)/255
    gy,gx=np.mgrid[y0:y1,x0:x1]; d['disp']=np.stack([mo[...,1]*1440-(gy+0.5),mo[...,0]*5120-(gx+0.5)],-1)
    fr.append(d)
print(f'run{RUN} crop x{X0}-{X1} y{Y0}-{Y1} frames {F0}..{F0+N-1} k={K} farw d0={D0:.8f} d1={D1:.8f} (view z {80*sc:.0f} .. {130*sc:.0f})')
# speed: per-pixel displacement minus the crop's far-routed median (= -current jitter at rest); motion.w==1 routed
for i,d in enumerate(fr):
    sel=d['far'][cy,cx]; med=np.median(d['disp'][cy,cx][sel],0); d['speed']=np.linalg.norm(d['disp']-med,axis=-1)
own=fr[1]['own'][cy,cx]
fw=fr[1]['farw'][cy,cx][own]; wz=fr[1]['a'][cy,cx][...,2][own]
print(f'owned plant px {own.sum()}: view z p5/p50/p95 {np.percentile(wz,5):.0f}/{np.median(wz):.0f}/{np.percentile(wz,95):.0f}; farw p5/p50/p95 {np.percentile(fw,5):.3f}/{np.median(fw):.3f}/{np.percentile(fw,95):.3f}; farw==1 {np.mean(fw>=1):.3f} farw==0 {np.mean(fw<=0):.3f}')
sp=np.concatenate([d['speed'][cy,cx][d['far'][cy,cx]] for d in fr]); print(f'far routed speed (px/frame, rel. to crop median) p50 {np.median(sp):.4f} p99 {np.percentile(sp,99):.4f}; farOpen(screen) at p99 {1-np.clip((np.percentile(sp,99)-0.03)/0.22,0,1):.3f}')
if not HAVE_TAA: sys.exit(0)
if 'age' in fr[0]:
    ag=np.concatenate([fr[i]['age'][cy,cx][fr[i+1]['own'][cy,cx]] for i in range(N-1)])
    print(f'owned plant age_prev: ==64 {np.mean(ag>=64):.3f} <16 {np.mean(ag<16):.4f} <8 {np.mean(ag<8):.4f} min {ag.min():.0f}')
rows=[]; keeps_all=[]
for i in range(1,N-1):
    d=fr[i]; p=fr[i-1]; nx=fr[i+1]
    st=d['tc']-np.maximum(mx3(p['tc']),mx3(nx['tc'])); hs=d['hc']-np.maximum(mx3(p['hc']),mx3(nx['hc']))
    spk=(st>MG)&d['plant']
    cur=weigh(d['hdr']); old=weigh(p['taa'])
    lo=cur.copy(); hi=cur.copy(); s=np.zeros_like(cur); s2=np.zeros_like(cur)
    for dy in(-1,0,1):
        for dx in(-1,0,1): n_=sh(cur,dy,dx); lo=np.minimum(lo,n_); hi=np.maximum(hi,n_); s+=n_; s2+=n_*n_
    mean=s/9; sig=np.sqrt(np.maximum(s2/9-mean*mean,0)); lo3,hi3=lo,hi; lo=np.maximum(lo,mean-1.25*sig); hi=np.minimum(hi,mean+1.25*sig)
    clp=np.clip(old,lo,hi)
    age=np.minimum(p['age'],64) if 'age' in p else np.full(cur.shape[:2],64.)
    ramp=age/(age+1); fo=1-np.clip((d['speed']-0.03)/0.22,0,1)
    keep=0.9+d['farw']*fo*(np.minimum(ramp,0.985)-0.9)
    reg=(d['h']>0) if 'h' in d else np.zeros(cur.shape[:2],bool)
    pred=unweigh(cur+(clp-cur)*keep[...,None]); pc=code(pred@LU)
    ok=d['plant'][cy,cx]&~reg[cy,cx]; err=np.abs(pc-d['tc'])[cy,cx][ok]; keeps_all.append(keep[cy,cx][d['own'][cy,cx]&~reg[cy,cx]])
    print(f'frame {F0+i}: model vs actual taa code on out-of-region plant px: |err| p50 {np.median(err):.3f} p99 {np.percentile(err,99):.3f} max {err.max():.2f}; sparkles {int(spk[cy,cx].sum())} hdr-spikes(plant) {int(((hs>MG)&d["plant"])[cy,cx].sum())}')
    ys,xs=np.nonzero(spk[cy,cx])
    for y,x in zip(ys+M,xs+M):
        L=lambda c:float(c[y,x]@LU)
        dcur=(1-keep[y,x])*(L(cur)-L(old)); dclip=keep[y,x]*(L(clp)-L(old))
        rows.append(dict(f=F0+i,x=X0+x-M,y=Y0+y-M,own=bool(d['own'][y,x]),reg=int(reg[y,x]),w=float(d['a'][y,x,2]),farw=float(d['farw'][y,x]),age=float(age[y,x]),speed=float(d['speed'][y,x]),keep=float(keep[y,x]),
            hc=[float(q['hc'][y,x]) for q in fr],tc=[float(q['tc'][y,x]) for q in fr],pc=float(pc[y,x]),
            cur=L(cur),old=L(old),clp=L(clp),lo=L(lo),hi=L(hi),lo3=L(lo3),hi3=L(hi3),dcur=dcur,dclip=dclip,tscore=float(st[y,x]),hscore=float(hs[y,x]),
            spike1=bool((hs[y-1:y+2,x-1:x+2]>MG).any()),oldin=bool(np.all((old[y,x]>=lo[y,x])&(old[y,x]<=hi[y,x])))))
# the frame after each sparkle: the sparkle's brightness left in the history and how much of it the N+1 clip removes
nxt=[]
for r in rows:
    i=r['f']-F0+1
    if i>=N: continue
    d=fr[i]; y=r['y']-Y0+M; x=r['x']-X0+M; c=weigh(d['hdr'][y-3:y+4,x-3:x+4]); old=weigh(fr[i-1]['taa'][y,x][None,None])[0,0]
    n9=c[2:5,2:5].reshape(-1,3); lo=n9.min(0); hi=n9.max(0); m=n9.mean(0); sg=np.sqrt(np.maximum((n9*n9).mean(0)-m*m,0)); lo=np.maximum(lo,m-1.25*sg); hi=np.minimum(hi,m+1.25*sg)
    n49=c.reshape(-1,3); clp=np.clip(old,lo,hi); L=lambda v:float(v@LU)
    prevold=L(weigh(fr[i-2]['taa'][y,x][None,None])[0,0])
    rise=L(old)-prevold; removed=L(old)-L(clp)
    nxt.append((rise,removed,L(n49.max(0))>L(old),L(hi)<L(old)))
if nxt:
    a=np.array(nxt); fr_=a[:,1]/np.maximum(a[:,0],1e-9)
    print(f'frame after a sparkle (n {len(a)}): history clamped down by the 3x3 clip {int((a[:,1]>1e-6).sum())}; removed/rise p50 {np.median(fr_):.2f} min {fr_.min():.2f}; 3x3 clip high below the sparkle history {int(a[:,3].sum())}; 7x7 max of the current above the sparkle history {int(a[:,2].sum())}')
ka=np.concatenate(keeps_all); print(f'keep on owned out-of-region plant px: p5/p50/p95 {np.percentile(ka,5):.4f}/{np.median(ka):.4f}/{np.percentile(ka,95):.4f}; >=0.98 {np.mean(ka>=0.98):.3f}')
print(f'sparkles n {len(rows)}; owned {sum(r["own"] for r in rows)}; in region {sum(r["reg"]>0 for r in rows)}; input spike within 1px {sum(r["spike1"] for r in rows)}; history inside clip box {sum(r["oldin"] for r in rows)}')
if rows:
    for key in ('w','farw','age','speed','keep','tscore','hscore','dcur','dclip'):
        v=np.array([r[key] for r in rows]); print(f'  {key}: min {v.min():.4g} p50 {np.median(v):.4g} max {v.max():.4g}')
    dom=sum(abs(r['dcur'])>abs(r['dclip']) for r in rows); print(f'  bright-frame term: current term dominates {dom}, clip term dominates {len(rows)-dom}; clip raised history (clp>old) {sum(r["clp"]>r["old"]+1e-6 for r in rows)}, lowered {sum(r["clp"]<r["old"]-1e-6 for r in rows)}')
    perr=np.array([abs(r['pc']-r['tc'][r['f']-F0]) for r in rows]); print(f'  model vs actual on sparkles |err| code p50 {np.median(perr):.3f} max {perr.max():.3f}')
    for r in sorted(rows,key=lambda r:-r['tscore'])[:int(E('EX',40))]:
        print(f"  f{r['f']} x{r['x']} y{r['y']} own{int(r['own'])} w{r['w']:.0f} farw{r['farw']:.3f} age{r['age']:.0f} spd{r['speed']:.3f} keep{r['keep']:.4f} score t{r['tscore']:.1f} h{r['hscore']:.1f} | hdr "+' '.join(f'{v:.0f}' for v in r['hc'])+' | taa '+' '.join(f'{v:.1f}' for v in r['tc'])+f" | pred {r['pc']:.1f} | wL cur {r['cur']:.4f} old {r['old']:.4f} clip[{r['lo']:.4f},{r['hi']:.4f}] mm[{r['lo3']:.4f},{r['hi3']:.4f}] dcur {r['dcur']:+.4f} dclip {r['dclip']:+.4f}")
