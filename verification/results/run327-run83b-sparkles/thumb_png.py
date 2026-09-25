# Downscaled PNG of a present_1 (bgra8) dump, optional crop, for visual identification (scratch output only).
# usage: thumb_png.py RUN FRAME OUT.png SCALE [X0 Y0 X1 Y1] [env KIND=present|taa|hdr]
import sys,os,zlib,struct,numpy as np
run,f,out,s=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4]); K=os.environ.get('KIND','present')
D=os.environ.get('D',f"/tmp/x3-bottleX3-run{run}")
if K=='present': img=np.fromfile(f"{D}/present_1_{f}.bgra8",np.uint8).reshape(1440,5120,4)[...,[2,1,0]]
else:
    h=np.fromfile(f"{D}/{K}_1_{f}.rgba16f",np.float16).reshape(1440,5120,4)[...,:3].astype(np.float32)
    img=np.clip(np.nan_to_num(np.maximum(h,0)/(1+np.maximum(h,0)))**(1/2.2)*255,0,255).astype(np.uint8)
if len(sys.argv)>8: X0,Y0,X1,Y1=map(int,sys.argv[5:9]); img=img[Y0:Y1,X0:X1]
img=np.ascontiguousarray(img[::s,::s]) if s>0 else np.ascontiguousarray(np.repeat(np.repeat(img,-s,0),-s,1))
hh,ww,_=img.shape; raw=b''.join(b'\x00'+img[y].tobytes() for y in range(hh))
c=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',ww,hh,8,2,0,0,0))+c(b'IDAT',zlib.compress(raw))+c(b'IEND',b''))
