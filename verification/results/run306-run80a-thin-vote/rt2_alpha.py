# RT2 lane readback (depth_<dev>_<frame>.rgba32f, 5120x1440 RGBA32F): .a distribution on routed pixels (.r != -1)
import sys,glob,numpy as np
for p in sorted(glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/depth_1_*.rgba32f")):
    a=np.fromfile(p,dtype=np.float32).reshape(1440,5120,4)
    routed=a[...,0]!=-1; A=a[...,3][routed]; n=routed.sum()
    thin=(A>=0)&(A<1)
    print(p.split('/')[-1],'routed',int(n),f"frac_routed {n/a[...,0].size:.4f}",'a<1',int(thin.sum()),f"frac {thin.sum()/max(n,1):.5f}",
      'a==1',int((A==1).sum()),'a>1',int((A>1).sum()),'a<0',int((A<0).sum()),
      'thin a min/p50/max', (float(A[thin].min()),float(np.median(A[thin])),float(A[thin].max())) if thin.any() else None,
      'distinct thin values', len(np.unique(A[thin])))
