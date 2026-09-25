# owner pixels (.a==1 exactly, routed .r!=-1) in RT2 lane: w (.b) range, bbox, and non-owner pixels in the same w range/bbox; .a vs .b agreement elsewhere
import sys,glob,numpy as np
for p in sorted(glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/depth_1_*.rgba32f")):
    f=int(p.split('_')[-1].split('.')[0])
    if len(sys.argv)>2 and str(f) not in sys.argv[2:]: continue
    a=np.fromfile(p,dtype=np.float32).reshape(1440,5120,4); routed=a[...,0]!=-1
    own=routed&(a[...,3]==1.0); oth=routed&~own
    aw=np.abs(a[...,3][oth]-a[...,2][oth]); 
    s=f"{f} owner {int(own.sum())} other {int(oth.sum())} other_a==w(|d|<1e-3) {float((aw<1e-3).mean()) if oth.any() else 0:.4f}"
    if own.any():
        w=a[...,2][own]; ys,xs=np.nonzero(own); x0,x1,y0,y1=xs.min(),xs.max(),ys.min(),ys.max()
        box=np.zeros_like(own); box[y0:y1+1,x0:x1+1]=True
        inr=oth&box&(a[...,2]>=w.min())&(a[...,2]<=w.max())
        s+=f" owner_w min/p50/max {w.min():.0f}/{np.median(w):.0f}/{w.max():.0f} bbox x{x0}-{x1} y{y0}-{y1} other_in_bbox_same_w {int(inr.sum())} other_w_max {a[...,2][oth].max():.0f}"
    print(s)
