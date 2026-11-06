"""The bubble program's float32 precision for a far owner (review of the ship bubble, docs/verification/weapon-effects.md
"Ship bubble, station decal"): weapon_bubble_ps.hlsl's ray / ellipsoid arithmetic (the chord's middle and half chord in
the bubble's normalised space, per-draw constants from double) in float32 against float64 for a 1 km capital
(half-extents 150 x 120 x 500 m, centre offset, turned 0.3 rad) 10 km from the eye at 5120x1440, over a row of 20000
sub-pixel samples through its image: the hit/miss sign mismatches, the normalised-point error (and in metres) and the
view-depth error. numpy only.

  python3 verification/results/weapon-effects/bubble_prec.py
"""
import numpy as np
f=np.float32
W,H=5120,1440; m00=1.0/np.tan(np.radians(45)); m11=m00*W/H
e=np.array([150.,120.,500.])*5  # view units (m*5): capital 1 km long
c=np.array([0.,10.,30.])*5
dist=10000*5.0
# owner at view (0,0,dist), axes rotated a bit
ang=0.3; R=np.array([[np.cos(ang),0,np.sin(ang)],[0,1,0],[-np.sin(ang),0,np.cos(ang)]])
centre=np.array([0.,0.,dist])
sx,sy=2.0/W,2.0/H
r0=np.array([(-1.0)/m00,(1.0)/m11,1.0]); rx=sx/m00; ry=-sy/m11
X=np.array([rx*R[k][0]/e[k] for k in range(3)]); Y=np.array([ry*R[k][1]/e[k] for k in range(3)])
D0=np.array([(r0@R[k])/e[k] for k in range(3)]); O=np.array([(-(centre@R[k])-c[k])/e[k] for k in range(3)])
def run(dt,vx,vy):
    d=vx[:,None]*X.astype(dt)+vy[:,None]*Y.astype(dt)+D0.astype(dt)
    eq=O.astype(dt)
    a=(d*d).sum(1); tm=-(d@eq)/a; h=eq+tm[:,None]*d; disc=dt(1)-(h*h).sum(1)
    ht=np.sqrt(np.maximum(disc,0)/a); qn=h-ht[:,None]*d; tn=tm-ht
    return disc,qn,tn
# a row of pixels through the centre of the bubble's image at sub-pixel jitter
vy=np.full(20000,H/2,dtype=np.float64); vx=np.linspace(0,W,20000)
d64,q64,t64=run(np.float64,vx,vy); d32,q32,t32=run(np.float32,vx.astype(f),vy.astype(f))
hit=(d64>0)
print('pixels hit', hit.sum(), 'sign mismatches', int(((d32>0)!=(d64>0)).sum()))
m=hit&(d32>0)
print('max |q32-q64| (normalised)', np.abs(q32[m]-q64[m]).max(), ' metres', (np.abs(q32[m]-q64[m])*e/5).max())
print('max rel t err', (np.abs(t32[m]-t64[m])/t64[m]).max(), 'abs view units', np.abs(t32[m]-t64[m]).max())
