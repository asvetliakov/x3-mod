"""Frame-to-frame ripple and parallax key on interior hull (5x5 all geometry, routed). Per pixel: screen disp
|(p+0.5) - motion.xy*(W,H)| px/frame; far-plane (rotation-only) displacement from camera_state r matrices and p00/p11
(D3D row vectors; the sign convention is the one giving the smaller median parallax), parallax = |screen vector -
rotation vector|; key = min(parallax, disp) (the cap's input under camera policy 2, taa-motion-history-weight.md sec. 9).
Ripple: taa_f(p) - bilinear taa_{f-1}(q), q = previous position, compressed luma l/(1+l), previous depth at q non-sentinel;
same for hdr (the current jittered input: its ripple is jitter+aliasing, the reference). Rows: rms_taa, rms_hdr, ratio,
median |age|, by disp bin, by key bin, and co-moving hull (disp < 1, parallax >= 2) and the parallax percentiles of disp < 1 hull.
Usage: hull_ripple.py <run_dir> <age_dir> <first> [n=32]"""
import sys,re,glob,warnings,numpy as np
warnings.filterwarnings('ignore')
sys.path.insert(0,'/Users/asvetl/x3-mod/verification/results/run263-motion-weight')
from darksky import rd,W,H
dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32
LW=np.array([0.2126,0.7152,0.0722]);yy,xx=np.mgrid[0:H,0:W]
log=glob.glob(f'{dr}/session-*.log')[0];cam={}
pat=re.compile(rb'^camera_state .*? frame=(\d+) ')
want=set(range(f0-1,f0+n))
import subprocess
out=subprocess.run(['grep','-E',r'^camera_state .* frame=('+'|'.join(map(str,sorted(want)))+') ',log],capture_output=True,env={'LC_ALL':'C'}).stdout.decode()
for l in out.splitlines():
    d=dict(re.findall(r'(\w+)=(\S+)',l));cam[int(d['frame'])]=d
R=lambda c:np.array([[float(c[f'r{i}{j}']) for j in range(3)] for i in range(3)])
def cl(k,f):l=np.maximum(rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW,0);return l/(1+l)
def bil(a,qx,qy):
    x0=np.clip(np.floor(qx).astype(int),0,W-2);y0=np.clip(np.floor(qy).astype(int),0,H-2);fx=np.clip(qx-x0,0,1);fy=np.clip(qy-y0,0,1)
    return (a[y0,x0]*(1-fx)+a[y0,x0+1]*fx)*(1-fy)+(a[y0+1,x0]*(1-fx)+a[y0+1,x0+1]*fx)*fy
def rotflow(c,p,sgn):
    p00,p11=float(c['p00']),float(c['p11'])
    X=((xx+0.5)/W*2-1)/p00;Y=(1-(yy+0.5)/H*2)/p11;d=np.stack([X,Y,np.ones_like(X)],-1)
    M=R(c).T@R(p) if sgn else R(p).T@R(c)
    e=d@M;px=(e[...,0]/e[...,2]*p00+1)/2*W;py=(1-e[...,1]/e[...,2]*p11)/2*H
    return px-(xx+0.5),py-(yy+0.5)
MB=[0,0.5,1,2,4,5.83,8,16,1e9]
A={'disp':np.zeros((len(MB)-1,4)),'key':np.zeros((len(MB)-1,4))};AG={'disp':[[] for _ in MB[:-1]],'key':[[] for _ in MB[:-1]]}
CO=np.zeros(4);LOWP=[];COA=[];sg_votes=[0,0];PX=[]
tp=cl('taa',f0);hp=cl('hdr',f0);dp=rd(dr,'depth',f0,'rgba32f',4)[...,0]
for f in range(f0+1,f0+n):
    t=cl('taa',f);h=cl('hdr',f);dep=rd(dr,'depth',f,'rgba32f',4)[...,0];g=dep!=-1;pg=np.pad(g,2);inn=np.ones_like(g)
    for dy in range(5):
        for dx in range(5):inn&=pg[dy:dy+H,dx:dx+W]
    m=rd(dr,'motion',f,'rgba32f',4);rt=m[...,3]>0;qx=m[...,0]*W-0.5;qy=m[...,1]*H-0.5
    vx=m[...,0]*W-(xx+0.5);vy=m[...,1]*H-(yy+0.5);disp=np.hypot(vx,vy)
    ok=inn&rt&(qx>=0)&(qx<W-1)&(qy>=0)&(qy<H-1)
    ok&=dp[np.clip(np.rint(qy).astype(int),0,H-1),np.clip(np.rint(qx).astype(int),0,W-1)]!=-1
    a=np.abs(np.fromfile(f'{ad}/taa_age_1_{f}.r32f',np.float32).reshape(H,W))
    par=np.full((H,W),np.nan)
    if f in cam and f-1 in cam:
        best=None
        for sgn in (0,1):
            rx,ry=rotflow(cam[f],cam[f-1],sgn);pp=np.hypot(vx-rx,vy-ry);md=np.median(pp[ok]) if ok.any() else 1e9
            if best is None or md<best[0]:best=(md,sgn,pp)
        sg_votes[best[1]]+=1;par=best[2];PX.append(np.median(np.hypot(*rotflow(cam[f],cam[f-1],best[1]))))
    key=np.fmin(par,disp)
    dt=t-bil(tp,qx,qy);dh=h-bil(hp,qx,qy)
    for nm,v in(('disp',disp),('key',key)):
        for i in range(len(MB)-1):
            s=ok&(v>=MB[i])&(v<MB[i+1]);A[nm][i]+=[s.sum(),(dt[s]**2).sum(),(dh[s]**2).sum(),0]
            if s.any():AG[nm][i].append(a[s][::5])
    LOWP.append(par[ok&(disp<1)]);s=ok&(disp<1)&(par>=2);CO+=[s.sum(),(dt[s]**2).sum(),(dh[s]**2).sum(),0]
    if s.any():COA.append(a[s][::5])
    tp,hp,dp=t,h,dep
print(f'{dr} {f0+1}-{f0+n-1}: rotation-only flow (image median) px/frame median {np.median(PX) if PX else float("nan"):.2f}; sign votes {sg_votes}')
for nm in('disp','key'):
    print(f' {nm}_bin px rms_taa rms_hdr ratio median_age')
    for i in range(len(MB)-1):
        r=A[nm][i]
        if not r[0]:continue
        ag=np.median(np.concatenate(AG[nm][i]))
        print(f'  {MB[i]}-{MB[i+1]:g} {int(r[0])} {np.sqrt(r[1]/r[0]):.4f} {np.sqrt(r[2]/r[0]):.4f} {np.sqrt(r[1]/max(r[2],1e-12)):.3f} {ag:.0f}')
lp=np.concatenate(LOWP);lp=lp[np.isfinite(lp)];print(f' disp<1 hull parallax p50/p90/p99/max {np.percentile(lp,[50,90,99,100]).round(2).tolist()}')
r=CO;print(f' co-moving (disp<1, parallax>=2): px {int(r[0])}'+(f' rms_taa {np.sqrt(r[1]/r[0]):.4f} rms_hdr {np.sqrt(r[2]/r[0]):.4f} ratio {np.sqrt(r[1]/max(r[2],1e-12)):.3f} median_age {np.median(np.concatenate(COA)):.0f}' if r[0] else ''))
