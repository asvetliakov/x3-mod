# Render a crop of the pre-resolve hdr_ dump (Reinhard + 1/2.2) with far-owner (magenta) / sentinel-detail overlay to a PNG for visual identification (scratch output only).
# usage: crop_png.py RUN FRAME X0 Y0 X1 Y1 OUT.png [SCALE]
import sys,zlib,struct,numpy as np
run,f=sys.argv[1],sys.argv[2]; X0,Y0,X1,Y1=map(int,sys.argv[3:7]); out=sys.argv[7]; s=int(sys.argv[8]) if len(sys.argv)>8 else 1
h=np.fromfile(f"/tmp/x3-bottleX3-run{run}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[Y0:Y1,X0:X1,:3].astype(np.float32)
a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[Y0:Y1,X0:X1]
t=np.clip(np.nan_to_num(h/(1+h))**(1/2.2)*255,0,255).astype(np.uint8)
o=t.copy(); r=a[...,0]!=-1; own=r&(a[...,3]==1); o[own]=(o[own]*0.5+np.array([127,0,127])).astype(np.uint8); o[r&~own]=[255,255,0]; o[r&(a[...,3]<1)]=[0,255,0]
img=np.concatenate([t,o],1)[::s,::s]; img=np.ascontiguousarray(img); hh,ww,_=img.shape
raw=b''.join(b'\x00'+img[y].tobytes() for y in range(hh)); c=lambda t_,d_: struct.pack('>I',len(d_))+t_+d_+struct.pack('>I',zlib.crc32(t_+d_)&0xffffffff)
open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',ww,hh,8,2,0,0,0))+c(b'IDAT',zlib.compress(raw))+c(b'IEND',b''))
