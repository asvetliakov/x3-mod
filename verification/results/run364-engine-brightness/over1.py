import numpy as np
W,H=5120,1440
c=np.fromfile('/tmp/x3-bottleX3-run364/hdr_1_4490.rgba16f',np.float16).reshape(H,W,4)[...,:3].astype(np.float32)
mx=c.max(-1)
print('frame >1:',int((mx>1).sum()),' >1.5:',int((mx>1.5).sum()),' >2.5:',int((mx>2.5).sum()), 'max', float(mx.max()))
ys,xs=np.nonzero(mx>1)
box=(xs>=2000)&(xs<2800)&(ys>=350)&(ys<800)
print('inside ship box:',int(box.sum()),' outside:',int((~box).sum()))
if (~box).any():
    o=~box; print(' outside bbox x',xs[o].min(),xs[o].max(),'y',ys[o].min(),ys[o].max())
    H2,_,_=np.histogram2d(ys[o],xs[o],bins=[[0,360,720,1080,1440],[0,1280,2560,3840,5120]]); print(H2.astype(int))
for (x,y) in ((160.6,1344.7),(2580.9,1331.7)):
    x,y=int(x),int(y); r=150
    sub=mx[max(0,y-r):y+r, max(0,x-r):x+r]
    print('card origin',x,y,'r150 px>1',int((sub>1).sum()),'max',float(sub.max()),'px>0.5',int((sub>0.5).sum()))
