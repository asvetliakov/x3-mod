# Side-by-side zoomed PNG of a crop: tone-mapped hdr_1 | lane classes (cyan sentinel .r==-1, magenta routed .g==-1 (owner),
# yellow routed .a<1 (voted), grey routed other, shade by w). usage: class_png.py RUN FRAME X0 Y0 X1 Y1 ZOOM OUTDIR
import sys,numpy as np
from PIL import Image
r=sys.argv[1]; f=int(sys.argv[2]); X0,Y0,X1,Y1,Z=map(int,sys.argv[3:8]); out=sys.argv[8]
h=np.fromfile(f"/tmp/x3-bottleX3-run{r}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[Y0:Y1,X0:X1,:3].astype(np.float32)
a=np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[Y0:Y1,X0:X1]
t=(255*np.clip(h/(1+h),0,1)**(1/2.2)).astype(np.uint8)
c=np.full_like(t,128); S=a[...,0]==-1; O=(~S)&(a[...,1]==-1); V=(~S)&(a[...,3]<1)&(a[...,3]>=0)
c[S]=[0,255,255]; c[O]=[255,0,255]; c[V]=[255,255,0]
img=np.concatenate([t,c],1); Image.fromarray(img).resize((img.shape[1]*Z,img.shape[0]*Z),Image.NEAREST).save(f"{out}/cls_{r}_{f}_{X0}_{Y0}.png")
print('S',int(S.sum()),'O',int(O.sum()),'V',int(V.sum()),'other',int((~S&~O&~V).sum()))
