# usage: python3 probe_px.py <capdir> <frame>  : print present/color BGRA, hdr RGB, depth.r at listed points
import sys, numpy as np
d,f=sys.argv[1],sys.argv[2];W,H=5120,1440
pr=np.memmap(f'{d}/present_1_{f}.bgra8',np.uint8,'r',shape=(H,W,4))
co=np.memmap(f'{d}/color_1_{f}.bgra8',np.uint8,'r',shape=(H,W,4))
hd=np.memmap(f'{d}/hdr_1_{f}.rgba16f',np.float16,'r',shape=(H,W,4))
de=np.memmap(f'{d}/depth_1_{f}.rgba32f',np.float32,'r',shape=(H,W,4))
pts={'disc_upleft':(955,870),'disc_left':(890,950),'disc_low':(990,990),'disc_right':(1110,935),'disc_upright':(1090,860),
     'hub':(1000,915),'fog_above':(980,820),'fog_between':(1000,960),'fog_far_left':(800,1000),'fog_right':(1175,1000)}
for k,(x,y) in pts.items():
    def m(a): return a[y-2:y+3,x-2:x+3].reshape(-1,a.shape[2]).astype(np.float64).mean(0)
    print(f"{k:13s} x={x} y={y} present_bgr={m(pr)[:3].round(1)} color_bgr={m(co)[:3].round(1)} hdr_rgb={m(hd)[:3].round(4)} depth={m(de)[:2].round(4)}")
