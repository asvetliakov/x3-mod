"""Synthetic drifting-lattice model for docs/architecture/source-antialiasing.md (throwaway, not a fixture).

Oblique 0.8-1 px lines, Halton-8 jitter, 1x/2x/4x sample patterns box-resolved in linear light, then an
exponential history (w 0.9, Catmull-Rom reprojection, luminance-weighted, no clip). Prints period 2-4 / 4-8 /
8-32 band rms in display codes per pixel and per 8x8 block mean. Usage: python3 msaa_lattice_model.py [kL_line kL_bg]
(default 0.77 0.30, the run148 medians on coverage-toggling px; 30 0.05 shows the bright-strut failure).
"""
import sys
import numpy as np
def halton(i,b):
    f=1.;r=0.
    while i>0: f/=b; r+=f*(i%b); i//=b
    return r
J=[(halton(i+1,2)-.5,halton(i+1,3)-.5) for i in range(8)]
PAT={1:[(0,0)],2:[(.25,.25),(-.25,-.25)],4:[(-.125,-.375),(.375,-.125),(-.375,.125),(.125,.375)]}
W,H,M=192,96,56; N=64; WARM=64
yy,xx=np.mgrid[0:H,0:W].astype(float); xx+=.5; yy+=.5
def cover(x,y,width,pitch,ang):
    d=x*np.cos(ang)+y*np.sin(ang)
    return ((d%pitch)<width).astype(float)
def catrom_shift(h,v):
    # history at x - v, 1-D Catmull-Rom along x
    i=int(np.floor(-v)); t=-v-i
    w=[-.5*t**3+t**2-.5*t, 1.5*t**3-2.5*t**2+1, -1.5*t**3+2*t**2+.5*t, .5*t**3-.5*t**2]
    return sum(wk*np.roll(h,-(i+k-1),axis=1) for k,wk in enumerate(w))
def bands(seq):
    f=np.fft.rfft(seq-seq.mean(0),axis=0)/len(seq); p=np.abs(f)**2*2
    n=len(seq); per=np.array([n/k if k else np.inf for k in range(len(f))])
    out=[]
    for lo,hi in((2,4),(4,8),(8,32)):
        m=(per>=lo)&(per<(hi if hi<32 else 32.01)) if lo>2 else (per>=2)&(per<4)
        out.append(np.sqrt(p[m].sum(0)).mean())
    return out
def run(ns,v,width,pitch,ang,Lline,Lbg,w=.9,k=1.,clampC=None):
    hist=None; frames=[];raws=[]
    for t in range(WARM+N):
        jx,jy=J[t%8]; img=np.zeros((H,W))
        for ox,oy in PAT[ns]:
            c=cover(xx+jx+ox-v*t,yy+jy+oy,width,pitch,ang)
            L=Lbg+(Lline-Lbg)*c
            if clampC: L=np.minimum(L,clampC)
            img+=L/ns
        if hist is None: hist=img
        else:
            # luminance-weighted blend as in the resolve (weights 1/(1+kL))
            hp=np.maximum(catrom_shift(hist,v),0); wh=w/(1+k*hp); wc=(1-w)/(1+k*img)
            hist=(wh*hp+wc*img)/(wh+wc)
        if t>=WARM:
            disp=lambda L:255*k*L/(1+k*L)
            frames.append(disp(hist)[:,M:W-M]); raws.append(disp(img)[:,M:W-M])
    res=[]
    for seq in (np.array(raws),np.array(frames)):
        px=bands(seq); hb,wb=seq.shape[1]//8*8,seq.shape[2]//8*8
        blk=seq[:,:hb,:wb].reshape(N,hb//8,8,wb//8,8).mean((2,4)); res.append((px,bands(blk)))
    return res
fmt=lambda b:' / '.join('%.2f'%x for x in b)
Lline,Lbg=(float(sys.argv[1]),float(sys.argv[2])) if len(sys.argv)>2 else (.77,.30)
for v in (0.,.29,.5):
    for ang,pitch,width in ((20,2.37,.8),(70,2.8,.8),(35,3.1,1.)):
        rows=[run(ns,v,width,pitch,np.radians(ang),Lline,Lbg) for ns in (1,2,4)]
        print('v=%.2f angle=%d pitch=%.2f width=%.1f'%(v,ang,pitch,width))
        for ns,r in zip((1,2,4),rows):
            print('  %dx raw blk %s | res px %s | res blk %s | blk vs 1x %s'%(ns,fmt(r[0][1]),fmt(r[1][0]),fmt(r[1][1]),
                  ' / '.join('%.2f'%(x/y) for x,y in zip(r[1][1],rows[0][1][1]))))
