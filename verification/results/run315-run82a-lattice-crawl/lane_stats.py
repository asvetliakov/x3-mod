# RT2 lane (depth_1 = .r z/w or -1 sentinel, .g -1 owned, .b w, .a 1-thin / 1) and motion_1 stats on a crop, one frame.
# usage: lane_stats.py RUN FRAME X0 Y0 X1 Y1
import sys,numpy as np,collections
r=sys.argv[1]; f=int(sys.argv[2]); X0,Y0,X1,Y1=map(int,sys.argv[3:7])
a=np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[Y0:Y1,X0:X1]
m=np.fromfile(f"/tmp/x3-bottleX3-run{r}/motion_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[Y0:Y1,X0:X1]
n=a.shape[0]*a.shape[1]; print('pixels',n)
for c,name in enumerate('rgba'):
    v=a[...,c]; u,cnt=np.unique(np.round(v,4),return_counts=True); o=np.argsort(-cnt)[:8]
    print(f"depth.{name}: min {v.min():.6g} max {v.max():.6g} top", ', '.join(f"{u[i]:.6g}x{cnt[i]}" for i in o))
for c,name in enumerate('rgba'):
    v=m[...,c]; u,cnt=np.unique(np.round(v,4),return_counts=True); o=np.argsort(-cnt)[:6]
    print(f"motion.{name}: min {v.min():.6g} max {v.max():.6g} top", ', '.join(f"{u[i]:.6g}x{cnt[i]}" for i in o))
