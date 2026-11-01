# usage: python3 lens_px.py <capdir> <frame> : mean present RGB / hdr luminance at lens-draw centres (from lens_pos.py) vs a 300-px ring
import sys, numpy as np
d,f=sys.argv[1],sys.argv[2];W,H=5120,1440
pr=np.memmap(f'{d}/present_1_{f}.bgra8',np.uint8,'r',shape=(H,W,4))
hd=np.memmap(f'{d}/hdr_1_{f}.rgba16f',np.float16,'r',shape=(H,W,4))
def box(a,x,y,r): return a[max(0,y-r):y+r+1,max(0,x-r):x+r+1,:3].reshape(-1,3).astype(np.float64)
for name,(x,y) in {'572-576,582 sun':(1934,546),'577':(2310,651),'578':(2353,663),'579':(2372,668),'580':(2685,755),'581':(2717,763),'discs(draws 20-48)':(950,866)}.items():
    p=box(pr,x,y,8).mean(0)[::-1]; h=box(hd,x,y,8); L=(0.2126*h[:,0]+0.7152*h[:,1]+0.0722*h[:,2]).mean()
    ring=np.concatenate([box(pr,x+dx,y+dy,8) for dx,dy in ((300,0),(-300,0),(0,150),(0,-150)) if 0<=x+dx<W and 0<=y+dy<H]).mean(0)[::-1]
    print(f"{name:20s} present_rgb={p.round(1)} hdr_lum={L:.4f} ring_present_rgb={ring.round(1)}")
