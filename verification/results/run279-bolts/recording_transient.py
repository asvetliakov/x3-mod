# Transient bright blobs on distinct recording frames: g = max(RGB) of frame k minus the 5x5-dilated max of
# frames k-1 and k+1 > dthr, g > gthr, in region. Prints per distinct frame the blobs (centre, bbox, area, peak).
import numpy as np, sys, glob
from PIL import Image
seg=sys.argv[1]; x0,y0,x1,y1=[int(v) for v in sys.argv[2].split(',')]; gthr=int(sys.argv[3]); dthr=int(sys.argv[4])
fs=sorted(glob.glob(seg+'/*.png')); prev=None; D=[]
for f in fs:
    a=np.asarray(Image.open(f).convert('RGB'),dtype=np.int16)
    s=a[::4,::4]
    if prev is not None and np.abs(s-prev).mean()<0.01: continue
    prev=s; D.append((f.split('/')[-1][:-4], a[y0:y1,x0:x1].max(2)))
def dil(g,r=2):
    o=g.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1):
            o=np.maximum(o,np.roll(np.roll(g,dy,0),dx,1))
    return o
for k in range(1,len(D)-1):
    n,g=D[k]; nb=np.maximum(dil(D[k-1][1]),dil(D[k+1][1]))
    m=(g>gthr)&(g-nb>dthr)
    pts=set(zip(*np.nonzero(m))); out=[]
    while pts:
        st=[pts.pop()]; c=[]
        while st:
            p=st.pop(); c.append(p)
            for dy in (-1,0,1):
                for dx in (-1,0,1):
                    q=(p[0]+dy,p[1]+dx)
                    if q in pts: pts.remove(q); st.append(q)
        ys=np.array([p[0] for p in c]); xs=np.array([p[1] for p in c])
        out.append((len(c),'(%d,%d %dx%d a%d pk%d)'%(xs.mean()+x0,ys.mean()+y0,np.ptp(xs)+1,np.ptp(ys)+1,len(c),g[ys,xs].max())))
    out.sort(reverse=True)
    print(k, n, len(out), ' '.join(o[1] for o in out[:8]))
