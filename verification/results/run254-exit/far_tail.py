"""Character of the far (d>=13) dark-sky pixels: share that are strict 3x3 colour-luma local
maxima (point sources), share with no dark 8-neighbour (isolated), and mean colour luma of the
3x3 ring around them. Usage: far_tail.py <dir> <first_frame> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,W,H
dr,f0=sys.argv[1],int(sys.argv[2]);n=int(sys.argv[3]) if len(sys.argv)>3 else 32
def nb(a):
    p=np.pad(a,1,mode='edge');return [p[1+dy:1+dy+H,1+dx:1+dx+W] for dy in(-1,0,1) for dx in(-1,0,1) if dy or dx]
tot=mx=iso=0;ring=[]
for f in range(f0+1,f0+n):
    col,pre,sky,d=frame(dr,f);dk=sky&((col-pre)>40)&(d>=13)
    N=nb(col);lm=np.all([col>q for q in N],0);D=nb(dk.astype(np.uint8));iso_=np.sum(D,0)==0
    tot+=dk.sum();mx+=(dk&lm).sum();iso+=(dk&iso_).sum();ring.append(np.mean(N,0)[dk])
r=np.concatenate(ring)
print(f'{dr} {f0}: far_dark={tot} local_max={mx/tot:.1%} isolated={iso/tot:.1%} ring_luma_median={np.median(r):.1f}')
