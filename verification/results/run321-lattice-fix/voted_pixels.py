# Full-frame RT2 .a in [0,1) (voted opaque draws, 1 - thin) on routed valid-depth pixels: count, distinct values, connected bboxes (coarse 64-px tiles)
# usage: voted_pixels.py RUN FRAME
import sys,numpy as np
r,f=sys.argv[1],int(sys.argv[2])
a=np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)
v=(a[...,0]>=0)&(a[...,0]<=1)&(a[...,3]>=0)&(a[...,3]<1)
print('voted pixels',int(v.sum()),'distinct .a',np.unique(np.round(a[...,3][v],4))[:12])
t=v.reshape(1440//32,32,5120//64,64).any((1,3)); ys,xs=np.nonzero(t)
for vals in sorted(set(np.round(a[...,3][v],4))):
    m=v&(np.abs(a[...,3]-vals)<5e-5); yy,xx=np.nonzero(m); print(f" .a={vals} n {m.sum()} bbox x{xx.min()}-{xx.max()} y{yy.min()}-{yy.max()} w p50 {np.median(a[...,2][m]):.0f}")
