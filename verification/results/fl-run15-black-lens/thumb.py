# usage: python3 thumb.py <capture.bgra8> <out.png> [x0 y0 w h scale]  (5120x1440 BGRA8)
import sys, numpy as np
from PIL import Image
W,H=5120,1440
a=np.memmap(sys.argv[1],np.uint8,'r',shape=(H,W,4))
if len(sys.argv)>3:
    x,y,w,h,s=map(int,sys.argv[3:8]); a=a[y:y+h,x:x+w]
else: s=4
img=np.ascontiguousarray(a[::s,::s,2::-1])
Image.fromarray(img).save(sys.argv[2])
