# Station crop classes (background: normalised 81x81 box of luma over pixels farther than 12 px from any routed pixel) per capture frame. Far owner = routed (.r != -1) & .a == 1 & w (.b) > WMIN (fade band, not the
# player's ship / near station). Station detail = display-luma deviation from a 41x41 box background above REL, within
# DIL px of the far-owner mask (so stars / dust motes away from the station do not count). Unowned station pixel =
# detail & sentinel (.r == -1); routed-other = detail & routed & not far owner.
# usage: station_pixels.py RUN X0 Y0 X1 Y1 FRAME... [env WMIN=20000 REL=0.06 DIL=6 PNG=dir]
import sys,os,numpy as np
run=sys.argv[1]; X0,Y0,X1,Y1=map(int,sys.argv[2:6]); frames=[int(f) for f in sys.argv[6:]]
WMIN=float(os.environ.get('WMIN',20000)); REL=float(os.environ.get('REL',0.10)); DIL=int(os.environ.get('DIL',6)); M=48
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def box(x,r):
    c=np.pad(x,((r+1,r),(r+1,r)),mode='edge').cumsum(0).cumsum(1); n=2*r+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
def dilate(m,r):
    return box(m.astype(np.float32),r)>0
def load(f):
    y0,y1,x0,x1=max(Y0-M,0),min(Y1+M,1440),max(X0-M,0),min(X1+M,5120)
    h=np.fromfile(f"/tmp/x3-bottleX3-run{run}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[y0:y1,x0:x1,:3].astype(np.float32)
    a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[y0:y1,x0:x1]
    return h,a,(y0,x0)
out={}
for f in frames:
    h,a,(oy,ox)=load(f); l=h@LUMA; d=l/(1+l)            # display-like luma (Reinhard), 0..1
    routed=a[...,0]!=-1; own=routed&(a[...,3]==1)&(a[...,2]>WMIN); oth=routed&~own
    keep=(~dilate(routed,12)).astype(np.float32); bg=box(d*keep,40)/np.maximum(box(keep,40),1e-6); det=(np.abs(d-bg)>REL*bg)&dilate(own,DIL)
    sl=(slice(Y0-oy,Y1-oy),slice(X0-ox,X1-ox))
    O,Ot,D=own[sl],oth[sl],det[sl]; S=D&~routed[sl]
    n=int(D.sum())
    print(f"frame {f} far_owner {int(O.sum())} routed_other {int(Ot.sum())} detail {n} detail_owned {int((D&O).sum())} detail_routed_other {int((D&Ot).sum())} detail_sentinel {int(S.sum())} valid_depth_frac {((D&routed[sl]).sum()/max(n,1)):.3f} sentinel_dark {int((S&(d[sl]<bg[sl])).sum())} sentinel_bright {int((S&(d[sl]>=bg[sl])).sum())}")
    out[f]=(d,own,routed,det,(oy,ox))
    if os.environ.get('PNG'):
        import zlib,struct
        t=np.clip((h/(1+h))**(1/2.2)*255,0,255).astype(np.uint8)[sl]; o=t.copy(); o[O]=[255,0,255]; o[S]=[0,255,255]; o[D&Ot]=[255,255,0]
        img=np.ascontiguousarray(np.concatenate([t,o],0).repeat(2,0).repeat(2,1)); hh,ww,_=img.shape
        raw=b''.join(b'\x00'+img[y].tobytes() for y in range(hh)); c=lambda t_,d_: struct.pack('>I',len(d_))+t_+d_+struct.pack('>I',zlib.crc32(t_+d_)&0xffffffff)
        open(f"{os.environ['PNG']}/station_{run}_{f}.png",'wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',ww,hh,8,2,0,0,0))+c(b'IDAT',zlib.compress(raw))+c(b'IEND',b''))
