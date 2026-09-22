"""Exit-reset census from the taa_age readbacks (r32f; a negative count is the exit mark, docs 2026-09-22
exit reset). Per frame: marked px (age<0), marked px by sky distance class; 'reset takers' = strict-sky
(depth sentinel) pixels of f whose own texel was marked on f-1 (identity path: far-sky shift is (0,0),
mid_band_chain_out.txt), with their age on f and whether taa==hdr (current-only) on f.
Age files were not copied into the preserved run dir; read from the bottle capture dir (mtime-checked,
colour bytes identical to the preserved copy). Usage: age_census.py <run_dir> <age_dir> <first> [n=32]"""
import sys,warnings,numpy as np
warnings.filterwarnings('ignore')
from darksky import frame,rd,W,H
dr,ad,f0=sys.argv[1],sys.argv[2],int(sys.argv[3]);n=int(sys.argv[4]) if len(sys.argv)>4 else 32
age=lambda f:np.fromfile(f'{ad}/taa_age_1_{f}.r32f',np.float32).reshape(H,W)
prev=None;T=np.zeros(6,np.int64);rows=[]
for f in range(f0,f0+n):
    a=age(f);col,pre,sky,d=frame(dr,f)
    t=rd(dr,'taa',f,'rgba16f',4);h=rd(dr,'hdr',f,'rgba16f',4);co=(t[...,:3]==h[...,:3]).all(-1)
    neg=a<0
    if prev is not None:
        tk=sky&(prev<0)
        r=[int(neg.sum()),int((neg&~sky).sum()),int((neg&sky&(d<=2)).sum()),int(tk.sum()),int((tk&co).sum()),int((tk&(a==1)).sum())]
        T+=r;rows.append((f,*r))
    prev=a
print(f'{dr} {f0}+{n}: frame marked marked_hull marked_sky_d1-2 reset_takers takers_cur_only takers_age1')
for r in rows:print(' ',*r)
v=np.array([r[1:] for r in rows]);print(' per-frame median',*np.median(v,0).astype(int),' max',*v.max(0),' total',*T)
a=age(f0+n-1);print(' age histogram last frame (sky / hull):',np.histogram(np.abs(a[sky]),[0,1.5,4.5,16.5,63.5,65])[0],np.histogram(np.abs(a[~sky]),[0,1.5,4.5,16.5,63.5,65])[0])
