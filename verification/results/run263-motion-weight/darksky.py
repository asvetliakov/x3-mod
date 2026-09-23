"""Dark-sky census for a capture burst (method of temporal-resolve.md 'Run 244').
dark = luma(color) - luma(present) > T (bgra8, Rec.601), sky = depth.x == -1 sentinel,
dist = Chebyshev distance to current non-sentinel pixel (9999 beyond 19).
Usage: darksky.py <dir> <first_frame> [n=32] [T=40] [variant]"""
import sys,warnings,numpy as np
warnings.filterwarnings("ignore")
W,H=1280,768;L=np.array([0.114,0.587,0.299])
def rd(dr,k,f,dt,c):return np.fromfile(f'{dr}/{k}_1_{f}.{dt}',{'bgra8':np.uint8,'rgba16f':np.float16}.get(dt,np.float32)).reshape(H,W,c)
def luma(a):return a[...,:3].astype(np.float64)@L
def cheb(geo,cap=19):
    d=np.full(geo.shape,9999,np.int32);d[geo]=0;cur=geo.copy()
    for r in range(1,cap+1):
        p=np.pad(cur,1);n=np.zeros_like(cur)
        for dy in(0,1,2):
            for dx in(0,1,2):n|=p[dy:dy+H,dx:dx+W]
        d[n&~cur]=r;cur=n
    return d
def frame(dr,f):
    col=luma(rd(dr,'color',f,'bgra8',4));pre=luma(rd(dr,'present',f,'bgra8',4))
    dep=rd(dr,'depth',f,'rgba32f',4)[...,0];sky=dep==-1
    return col,pre,sky,cheb(~sky)
if __name__=='__main__':
    dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32;T=float(sys.argv[4]) if len(sys.argv)>4 else 40
    tot=0;b12=0;b312=0;b13=0;unc=[];prevsky=None
    for f in range(f0,f0+n):
        col,pre,sky,d=frame(dr,f)
        if prevsky is not None:
            dk=sky&((col-pre)>T);c=int(dk.sum());tot+=c
            b12+=int((dk&(d>=1)&(d<=2)).sum());b312+=int((dk&(d>=3)&(d<=12)).sum());b13+=int((dk&(d>=13)).sum())
            if c:unc.append((dk&~prevsky).sum()/c)
        prevsky=sky
    print(f'{dr} {f0}+{n}: dark_sky={tot} d1-2={b12} ({b12/max(tot,1):.0%}) d3-12={b312} ({b312/max(tot,1):.0%}) d>=13={b13} ({b13/max(tot,1):.0%}) mean_uncovered_frac={np.mean(unc) if unc else 0:.3f}')
