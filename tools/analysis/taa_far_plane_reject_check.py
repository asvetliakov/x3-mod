#!/usr/bin/env python3
"""Far-plane camera path: share of current-only pixels (taa == hdr) in the previousClip.w > 1 and <= 1 halves of the sky.
usage: taa_far_plane_reject_check.py <dump dir> <frame>   (docs/verification/temporal-resolve.md, 2026-09-22 replay diagnosis)"""
import sys, re, numpy as np, subprocess
D=sys.argv[1].rstrip('/')+'/'; f=int(sys.argv[2]); W,H=1280,768
log=[p for p in __import__('os').listdir(D) if p.startswith('session-')][0]
ls=subprocess.run(['grep','-E',r'^(camera_state|motion_output_frame) device=1 frame=(%d|%d) '%(f,f-1),D+log],capture_output=True,text=True).stdout.splitlines()
R={};J={}
for l in ls:
    kv=dict(re.findall(r'(\w+)=([-\w.+]+)',l)); fr=int(kv['frame'])
    if l.startswith('camera'): R[fr]=np.array([[float(kv['r%d%d'%(i,j)]) for j in range(3)] for i in range(3)])
    else: J[fr]=(float(kv['jitter_x']),float(kv['jitter_y']))
p00,p11=.8,4/3.
A=np.array([[1/p00,0,0],[0,1/p11,0],[0,0,1.]]); B=np.array([[p00,0,0],[0,p11,0],[0,0,1.]])
rc=R[f].ravel(); rp=R[f-1].ravel()
Q=np.array([[sum(rc[k*3+i]*rp[k*3+j] for k in range(3)) for j in range(3)] for i in range(3)])
N=A@Q@B; row3=np.array([N[0][2],N[1][2],0,N[2][2]],np.float32)
ys,xs=np.mgrid[0:H,0:W]; jx,jy=J[f]
# uv - half texel - jitter uv (float32 as the shader)
u=((xs+.5)/W).astype(np.float32); v=((ys+.5)/H).astype(np.float32)
ux=(u-np.float32(.5/W)-np.float32(jx/W)).astype(np.float32); uy=(v-np.float32(.5/H)-np.float32(jy/H)).astype(np.float32)
cx=(ux*np.float32(2)-np.float32(1)).astype(np.float32); cy=(np.float32(1)-uy*np.float32(2)).astype(np.float32)
w=(row3[0]*cx+row3[1]*cy+row3[3]).astype(np.float32)
e=(w*(np.float32(1)/w)).astype(np.float32)
t=np.fromfile(D+'taa_1_%d.rgba16f'%f,np.float16).reshape(H,W,4); h=np.fromfile(D+'hdr_1_%d.rgba16f'%f,np.float16).reshape(H,W,4); eq=(t[...,:3]==h[...,:3]).all(-1)
sky=np.zeros((H,W),bool); sky[40:440,100:1150]=True
print('frame',f,'row3',row3,'w range',w.min(),w.max())
for name,m_ in (('w>1',w>1),('w<=1',w<=1)):
    s=sky&m_; print(name,'px',s.sum(),'observed cur-only %.3f'%eq[s].mean(),'sim x*(1/x)>1 %.3f'%(e[s]>1).mean(),'sim !=1 %.3f'%(e[s]!=1).mean())
xb=np.where((w[300]>1))[0]; print('w>1 columns at row 300:',xb.min() if xb.size else None,xb.max() if xb.size else None)
