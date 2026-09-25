# Locate far-owner clusters (routed, .a==1, w>WMIN) in a capture frame: 64x64 tile counts, connected tile groups with bbox and pixel count
import sys,glob,os,numpy as np
from collections import deque
run,f=sys.argv[1],sys.argv[2]; WMIN=float(os.environ.get('WMIN',20000)); T=32
a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)
own=(a[...,0]!=-1)&(a[...,3]==1)&(a[...,2]>WMIN)
print('far_owner px',int(own.sum()),'w range',(a[...,2][own].min(),a[...,2][own].max()) if own.any() else None)
t=own.reshape(1440//T,T,5120//T,T).sum((1,3)); seen=np.zeros_like(t,bool)
for y,x in zip(*np.nonzero(t)):
    if seen[y,x]: continue
    q=deque([(y,x)]); seen[y,x]=True; cells=[]
    while q:
        cy,cx=q.popleft(); cells.append((cy,cx))
        for dy in(-1,0,1):
            for dx in(-1,0,1):
                ny,nx=cy+dy,cx+dx
                if 0<=ny<t.shape[0] and 0<=nx<t.shape[1] and t[ny,nx] and not seen[ny,nx]: seen[ny,nx]=True; q.append((ny,nx))
    ys=[c[0] for c in cells]; xs=[c[1] for c in cells]; n=sum(t[c] for c in cells)
    if n<50: continue
    m=own[min(ys)*T:(max(ys)+1)*T, min(xs)*T:(max(xs)+1)*T]; yy,xx=np.nonzero(m)
    print(f"cluster px {n} bbox x{min(xs)*T+xx.min()}-{min(xs)*T+xx.max()} y{min(ys)*T+yy.min()}-{min(ys)*T+yy.max()} w p50 {np.median(a[min(ys)*T:(max(ys)+1)*T, min(xs)*T:(max(xs)+1)*T][...,2][m]):.0f}")
