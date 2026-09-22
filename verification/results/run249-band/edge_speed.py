"""Routed displacement |(p+0.5) - motion.xy*(W,H)| (px/frame, jitter ignored) on silhouette
edge pixels (routed geometry with a sentinel 8-neighbour), frames f0+5, +15, +25: median/p90,
and the geometry pixel count. Usage: edge_speed.py <dir> <first_frame>"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import rd,W,H
dr,f0=sys.argv[1],int(sys.argv[2]);yy,xx=np.mgrid[0:H,0:W];out=[]
for f in (f0+5,f0+15,f0+25):
    sky=rd(dr,'depth',f,'rgba32f',4)[...,0]==-1;m=rd(dr,'motion',f,'rgba32f',4)
    p=np.pad(sky,1);nb=np.zeros_like(sky)
    for dy in(0,1,2):
        for dx in(0,1,2):nb|=p[dy:dy+H,dx:dx+W]
    e=~sky&nb&(m[...,3]>0);v=np.hypot(xx+0.5-m[...,0]*W,yy+0.5-m[...,1]*H)[e]
    out.append(f'f{f} geo={int((~sky).sum())} edge={e.sum()} med={np.median(v):.2f} p90={np.percentile(v,90):.2f}')
print(dr,*out,sep='\n  ')
