"""What the 3-12 px dark-sky pixels are: for each dark pixel p (darksky.py definition, d 3..12)
on frame f: (1) share that are strict 3x3 hdr-luma local maxima (point features);
(2) mean hdr luma at p over f-7..f vs taa_f (convergence to the jitter average);
(3) block match of the 7x7 hdr-luma patch around p in frame f against frame f-1, search
+-12 px: best displacement (moving unrouted feature vs static sky); SSD of the zero shift
vs the best shift. Usage: mid_band_motion.py <dir> f1 f2 ..."""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
LW=np.array([0.2126,0.7152,0.0722])
def lu(dr,k,f):return rd(dr,k,f,'rgba16f',4).astype(np.float64)[...,:3]@LW
dr=sys.argv[1];R=3;S=12
for f in map(int,sys.argv[2:]):
    col,pre,sky,d=frame(dr,f)
    dk=sky&((col-pre)>40)&(d>=3)&(d<=12)
    h=lu(dr,'hdr',f);hp=lu(dr,'hdr',f-1);t=lu(dr,'taa',f)
    hist=np.mean([lu(dr,'hdr',g) for g in range(f-7,f+1)],0)
    p=np.pad(h,1,mode='edge');lm=np.all([h>=p[1+dy:1+dy+H,1+dx:1+dx+W] for dy in(-1,0,1) for dx in(-1,0,1) if dy or dx],0)
    ys,xs=np.nonzero(dk);disp=[];zero_better=0;ratio=[]
    for y,x in zip(ys,xs):
        if y<R+S or x<R+S or y>=H-R-S or x>=W-R-S:continue
        a=h[y-R:y+R+1,x-R:x+R+1];best=(1e18,0,0);z=None
        for dy in range(-S,S+1):
            for dx in range(-S,S+1):
                b=hp[y+dy-R:y+dy+R+1,x+dx-R:x+dx+R+1];e=((a-b)**2).sum()
                if dy==0 and dx==0:z=e
                if e<best[0]:best=(e,dy,dx)
        disp.append(np.hypot(best[1],best[2]));ratio.append(z/max(best[0],1e-12))
    disp=np.array(disp);ratio=np.array(ratio)
    print(f'f{f}: n={int(dk.sum())} local_max(hdr) {int((dk&lm).sum())}; taa/mean8(hdr) median {np.median((t/np.maximum(hist,1e-6))[dk]):.3f} p10/p90 {np.percentile((t/np.maximum(hist,1e-6))[dk],10):.3f}/{np.percentile((t/np.maximum(hist,1e-6))[dk],90):.3f}; hdr/mean8 median {np.median((h/np.maximum(hist,1e-6))[dk]):.3f}')
    print(f'  block match f vs f-1 (n={len(disp)}): disp 0 {int((disp==0).sum())}, <3 {int(((disp>0)&(disp<3)).sum())}, 3-12 {int((disp>=3).sum())}, median {np.median(disp):.2f}; SSD(zero)/SSD(best) median {np.median(ratio):.2f}')
