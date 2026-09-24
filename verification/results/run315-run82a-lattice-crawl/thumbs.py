# Tone-mapped thumbnails of hdr_1 dumps (pre-resolve) into the scratch dir for visual lattice search.
# usage: thumbs.py RUN FRAME OUTDIR [SCALE=4] [X0 Y0 X1 Y1 crop at full res]
import sys,numpy as np
from PIL import Image
r,f,out=sys.argv[1],int(sys.argv[2]),sys.argv[3]; s=int(sys.argv[4]) if len(sys.argv)>4 else 4
h=np.fromfile(f"/tmp/x3-bottleX3-run{r}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[...,:3].astype(np.float32)
if len(sys.argv)>8:
    X0,Y0,X1,Y1=map(int,sys.argv[5:9]); h=h[Y0:Y1,X0:X1]
h=np.nan_to_num(h); c=(255*np.clip(h/(1+h),0,1)**(1/2.2)).astype(np.uint8)
im=Image.fromarray(c)
if s>1: im=im.resize((c.shape[1]//s,c.shape[0]//s),Image.BOX)
elif s<0: im=im.resize((c.shape[1]*-s,c.shape[0]*-s),Image.NEAREST)
im.save(f"{out}/r{r}_{f}_{'crop' if len(sys.argv)>8 else 'full'}.png"); print(im.size)
