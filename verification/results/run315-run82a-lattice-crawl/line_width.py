# Line width estimate on a panel-interior window: pitch of the cell grid from the two strongest 2-D spectrum peaks of the
# 8-frame mean cell indicator (O = fade-owner cell face), and non-cell share f of pixels (lines + holes, 8-frame mean);
# for a grid of two line families with perpendicular pitches p1, p2 and width w: f ~ w/p1 + w/p2 - w^2/(p1 p2).
# usage: line_width.py RUN F0 N X0 Y0 X1 Y1
import sys,numpy as np
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); X0,Y0,X1,Y1=map(int,sys.argv[4:8]); sl=(slice(Y0,Y1),slice(X0,X1))
A=np.stack([np.fromfile(f"/tmp/x3-bottleX3-run{r}/depth_1_{F0+i}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[sl] for i in range(N)])
O=((A[...,0]!=-1)&(A[...,1]==-1)).mean(0); S=(A[...,0]==-1).mean(0); L=1-O-S
f=1-O.mean(); print(f"window {X0},{Y0}-{X1},{Y1}: 8-frame mean shares cell {O.mean():.3f} line(L) {L.mean():.3f} hole(S) {S.mean():.3f}")
F=np.abs(np.fft.fft2(O-O.mean())); F[0,0]=0; h,w=O.shape
ky=np.fft.fftfreq(h); kx=np.fft.fftfreq(w); peaks=[]
Fc=F.copy()
for _ in range(6):
    i=np.unravel_index(np.argmax(Fc),Fc.shape); k=np.hypot(ky[i[0]],kx[i[1]]); peaks.append((1/k, np.degrees(np.arctan2(ky[i[0]],kx[i[1]]))%180, Fc[i]))
    for dy in (-1,0,1):
        for dx in (-1,0,1): Fc[(i[0]+dy)%h,(i[1]+dx)%w]=0; Fc[(-i[0]+dy)%h,(-i[1]+dx)%w]=0
for p in peaks: print(f"  peak pitch {p[0]:.2f} px angle {p[1]:.0f} deg amp {p[2]:.0f}")
# two families: strongest peak and the strongest one differing in angle by > 30 deg
p1=peaks[0]; p2=next((p for p in peaks[1:] if min(abs(p[1]-p1[1]),180-abs(p[1]-p1[1]))>30),None)
if p2:
    a,b=p1[0],p2[0]; 
    import math; w_=(a*b)*(1-math.sqrt(max(1-f,0)))*1.0  # solve f = w/a + w/b - w^2/(ab) approx via (1-w/a)(1-w/b)=1-f; take a~b symmetric approx below
    # exact solve of (1-w/a)(1-w/b) = 1-f
    A2=1/(a*b); B2=-(1/a+1/b); C2=f; disc=B2*B2-4*A2*C2; w_=(-B2-math.sqrt(disc))/(2*A2) if disc>=0 else float('nan')
    print(f"families pitch {a:.2f} / {b:.2f} px, non-cell share {f:.3f} -> line width w {w_:.2f} px (lines+holes)")
