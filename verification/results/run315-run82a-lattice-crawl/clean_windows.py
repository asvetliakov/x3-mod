# Find 32x32 windows inside the panels with few holes (8-frame mean S share < SMAX) and many cells; print shares.
# usage: clean_windows.py RUN F0 N X0 Y0 X1 Y1 [SMAX=0.03]
import sys,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); X0,Y0,X1,Y1=map(int,sys.argv[4:8]); SMAX=float(sys.argv[8]) if len(sys.argv)>8 else .03
sl=(slice(Y0,Y1),slice(X0,X1))
A=np.stack([np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl] for i in range(N)])
O=((A[...,0]!=-1)&(A[...,1]==-1)).mean(0); S=(A[...,0]==-1).mean(0)
res=[]
for y in range(0,O.shape[0]-32,8):
    for x in range(0,O.shape[1]-32,8):
        o=O[y:y+32,x:x+32].mean(); s=S[y:y+32,x:x+32].mean()
        if s<SMAX and o>0.4: res.append((o,s,X0+x,Y0+y))
res.sort(reverse=True); print(len(res),'windows'); 
for o,s,x,y in res[:8]: print(f"  {x},{y}-{x+32},{y+32} cell {o:.3f} hole {s:.3f} line {1-o-s:.3f}")
