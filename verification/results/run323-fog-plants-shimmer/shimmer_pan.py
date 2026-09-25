# shimmer.py from run313 with the horizontal search range as env SX (default 4 = original); M must exceed max(S,SX).
# Input-side (pre-resolve hdr_ dump: jittered FP16 scene, NOT the resolved output; taa_debug=0) frame-to-frame change on a
# station crop. Display codes c = 255 * (L/(1+L))^(1/2.2) of Rec.709 luma. Consecutive frames aligned by the integer
# shift (search +-S px) minimising mean |dc| over the crop. Classes from frame f: owned = far owner (routed, .a==1,
# w > WMIN); unowned = station detail (as station_pixels.py) on sentinel .r == -1; bg = sentinel, not detail, > 12 px
# from any routed pixel. Prints per pair: shift, mean |dc| per class; then per class the 8-frame per-pixel temporal std
# (only meaningful at rest: shift 0,0 on every pair).
# usage: shimmer.py RUN X0 Y0 X1 Y1 F0 NFRAMES [env S=16 WMIN=20000 REL=0.10 DIL=6]
import sys,os,numpy as np
run=sys.argv[1]; X0,Y0,X1,Y1,F0,N=map(int,sys.argv[2:8]); S=int(os.environ.get('S',16)); SX=int(os.environ.get('SX',4))
WMIN=float(os.environ.get('WMIN',20000)); REL=float(os.environ.get('REL',0.10)); DIL=int(os.environ.get('DIL',6)); M=int(os.environ.get("M",64))
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def box(x,r):
    c=np.pad(x,((r+1,r),(r+1,r)),mode='edge').cumsum(0).cumsum(1); n=2*r+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
def dil(m,r): return box(m.astype(np.float32),r)>0
y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M
def load(f):
    h=np.fromfile(f"/tmp/x3-bottleX3-run{run}/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[y0:y1,x0:x1,:3].astype(np.float32)
    a=np.fromfile(f"/tmp/x3-bottleX3-run{run}/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[y0:y1,x0:x1]
    l=h@LUMA; d=l/(1+l); c=255*np.clip(d,0,1)**(1/2.2)
    routed=a[...,0]!=-1; own=routed&(a[...,3]==1)&(a[...,2]>WMIN)
    keep=(~dil(routed,12)).astype(np.float32); bg=box(d*keep,40)/np.maximum(box(keep,40),1e-6)
    det=(np.abs(d-bg)>REL*bg)&dil(own,DIL)
    return c,{'owned':own,'unowned':det&~routed,'bg':(~routed)&~det&(keep>0)}
fr=[load(F0+i) for i in range(N)]
cy=slice(M,M+Y1-Y0); cx=slice(M,M+X1-X0)
acc={k:[] for k in fr[0][1]}; shifts=[]
for i in range(N-1):
    ca,cla=fr[i]; cb,_=fr[i+1]; best=None
    for dy in range(-S,S+1):
        for dx in range(-SX,SX+1):
            e=np.abs(cb[M+dy:M+dy+Y1-Y0, M+dx:M+dx+X1-X0]-ca[cy,cx]).mean()
            if best is None or e<best[0]: best=(e,dy,dx)
    _,dy,dx=best; shifts.append((dy,dx))
    D=np.abs(cb[M+dy:M+dy+Y1-Y0, M+dx:M+dx+X1-X0]-ca[cy,cx])
    row=f"pair {F0+i}->{F0+i+1} shift dy{dy:+d} dx{dx:+d}"
    for k,m in cla.items():
        v=D[m[cy,cx]]; acc[k].append(v.mean()); row+=f" {k} n{v.size} mean|dc| {v.mean():.2f} p95 {np.percentile(v,95):.1f}"
    print(row)
for k in acc: print(f"class {k}: mean over pairs {np.mean(acc[k]):.2f}  pair min/max {min(acc[k]):.2f}/{max(acc[k]):.2f}")
if all(s==(0,0) for s in shifts):
    st=np.stack([c[cy,cx] for c,_ in fr]).std(0)
    for k,m in fr[0][1].items(): v=st[m[cy,cx]]; print(f"rest 8-frame per-pixel std {k}: mean {v.mean():.2f} p95 {np.percentile(v,95):.1f} frac>4 {np.mean(v>4):.3f}")
    means={k:[c[cy,cx][m[cy,cx]].mean() for c,_ in fr] for k,m in fr[0][1].items()}
    for k,v in means.items(): print(f"per-frame class mean {k}: "+' '.join(f'{x:.2f}' for x in v))
