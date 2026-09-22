"""Temporal signature of dark-sky pixels by distance class (darksky.py definition), frames f:
hdr luma at p over f-7..f normalised by its mean: coefficient of variation (flicker), lag-1
autocorrelation (smooth passage > 0, jitter flicker <= 0), share with a dark 8-neighbour,
dark density per sky pixel; taa_f / mean8(hdr); taa_{f-1}/mean8. Control: same for d>=13.
Usage: mid_band_series.py <dir> f1 f2 ..."""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
LW=np.array([0.2126,0.7152,0.0722])
def lu(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW
dr=sys.argv[1]
for f in map(int,sys.argv[2:]):
    col,pre,sky,d=frame(dr,f);dkall=sky&((col-pre)>40)
    S=np.array([lu(dr,'hdr',g) for g in range(f-7,f+1)]);mu=S.mean(0);sd=S.std(0)
    x=S-mu;ac=(x[1:]*x[:-1]).sum(0)/np.maximum((x*x).sum(0),1e-12)
    t=lu(dr,'taa',f);tp=lu(dr,'taa',f-1)
    p=np.pad(dkall,1);nb=sum(p[1+dy:1+dy+H,1+dx:1+dx+W] for dy in(-1,0,1) for dx in(-1,0,1) if dy or dx)>0
    for name,m in(('d3-12',dkall&(d>=3)&(d<=12)),('d>=13',dkall&(d>=13))):
        s=sky&((d>=3)&(d<=12) if name=='d3-12' else (d>=13))
        md=lambda a:np.median(a[m])
        print(f'f{f} {name}: n={int(m.sum())} density={m.sum()/max(s.sum(),1)*1e3:.2f}/1000 CV={md(sd/np.maximum(mu,1e-6)):.2f} lag1={md(ac):.2f} dark_nb={(m&nb).sum()/max(m.sum(),1):.0%} hdr/mean8={md(S[-1]/np.maximum(mu,1e-6)):.2f} taa/mean8={md(t/np.maximum(mu,1e-6)):.2f} prevtaa/mean8={md(tp/np.maximum(mu,1e-6)):.2f} mean8={md(mu):.3f}')
