# RT2 lane statistics on a crop per capture frame: routed (.r != -1), .a == 1, .a < 1 (voted thin) with .a histogram,
# .g == -1 among routed, .b (w) median over routed; unrouted pixels within 3 px of a routed pixel (edge/hole remainder).
# usage: lane_stats.py RUN X0 Y0 X1 Y1 FRAME...
import sys,numpy as np
run=sys.argv[1]; X0,Y0,X1,Y1=map(int,sys.argv[2:6])
def box(x,r):
    c=np.pad(x,((r+1,r),(r+1,r)),mode='edge').cumsum(0).cumsum(1); n=2*r+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
for f in sys.argv[6:]:
    a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[Y0:Y1,X0:X1]
    r=a[...,0]!=-1; A=a[...,3]; thin=r&(A<1)
    near=(box(r.astype(np.float32),3)>0)&~r
    h=np.histogram(A[thin],bins=[0,0.25,0.5,0.75,0.9,0.999999])[0] if thin.any() else []
    g=a[...,1][r]
    print(f"frame {f} routed {int(r.sum())} a==1 {int((r&(A==1)).sum())} a<1 {int(thin.sum())} a<1_hist[0,.25,.5,.75,.9,1) {list(map(int,h))} g==-1 {int((g==-1).sum())} g_other {int((g!=-1).sum())} w_p50 {np.median(a[...,2][r]) if r.any() else 0:.0f} unrouted_within3px {int(near.sum())} nonfinite_r {int((~np.isfinite(a[...,0])).sum())}")
