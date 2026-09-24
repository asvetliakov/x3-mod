# 8-frame per-pixel std and frame-to-frame rms of display code c on always-routed pixels of control crops (pre-resolve hdr_1)
# usage: hull_std.py RUN F0 N name:X0,Y0,X1,Y1 ...
import sys,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
for spec in sys.argv[4:]:
    nm,b=spec.split(':'); X0,Y0,X1,Y1=map(int,b.split(',')); sl=(slice(Y0,Y1),slice(X0,X1))
    C=[];R=[]
    for i in range(N):
        h=np.fromfile(f"/tmp/x3-bottleX3-run{r}/hdr_1_{F0+i}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[sl][...,:3].astype(np.float32)
        a=np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl]
        Y=h@LUMA; C.append(255*np.clip(Y/(1+Y),0,1)**(1/2.2)); R.append(a[...,0]!=-1)
    C=np.stack(C); m=np.stack(R).all(0); s=C.std(0)[m]; d=np.diff(C,axis=0)
    print(f"{nm} {X0},{Y0}-{X1},{Y1} always-routed n {int(m.sum())}: std mean {s.mean():.2f} p50 {np.median(s):.2f} p95 {np.percentile(s,95):.2f} frac>4 {np.mean(s>4):.3f} frac>10 {np.mean(s>10):.3f} mean c {C.mean(0)[m].mean():.1f}; rms dc "+' '.join(f"{np.sqrt((d[i][m]**2).mean()):.2f}" for i in range(N-1)))
