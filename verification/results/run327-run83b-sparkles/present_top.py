# Top one-frame sparkles on present_1 (displayed bytes, after sharpen) vs taa_1 (resolved) at the same pixel, plant pixels
# more than EXSHIP px from the player ship; region state from taa_age_1. Same detector/alignment as sparkles.py.
# usage: present_top.py X0 Y0 X1 Y1 F0 N [env MG=6 EXSHIP=8 TOP=12]
import sys,os,numpy as np
X0,Y0,X1,Y1,F0,N=map(int,sys.argv[1:7]); E=os.environ.get; MG=float(E('MG',6)); XS=int(E('EXSHIP',8)); TOP=int(E('TOP',12))
TD='/tmp/x3-bottleX3-run327'; BD='/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'
M=24; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; H,W=Y1-Y0,X1-X0; cy,cx=slice(M,M+H),slice(M,M+W); LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120))[y0:y1,x0:x1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def mxr(x,r):
    o=x.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1): o=np.maximum(o,np.roll(np.roll(x,dy,0),dx,1))
    return o
fr=[]
for i in range(N):
    f=F0+i; a=mm(f"{TD}/depth_1_{f}.rgba32f",np.float32,4); mo=mm(f"{TD}/motion_1_{f}.rgba32f",np.float32,4)
    routed=a[...,0]!=-1; far=routed&(a[...,2]>20000); near=routed&~far
    plant=(far|((~routed)&mxr(far.astype(np.uint8),3).astype(bool)))&~mxr(near.astype(np.uint8),XS).astype(bool)
    v=np.abs(mm(f"{BD}/taa_age_1_{f}.r32f",np.float32,1)); h=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64)%128
    gy,gx=np.mgrid[y0:y1,x0:x1]; sel=far[cy,cx]
    disp=(float(np.median((mo[...,1]*1440-(gy+0.5))[cy,cx][sel])),float(np.median((mo[...,0]*5120-(gx+0.5))[cy,cx][sel])))
    fr.append(dict(p=mm(f"{TD}/present_1_{f}.bgra8",np.uint8,4)[...,[2,1,0]].astype(np.float32)@LUMA,
                   t=code(mm(f"{TD}/taa_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA),
                   hd=code(mm(f"{TD}/hdr_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA),plant=plant,h=h,disp=disp,own=(far&(a[...,1]==-1))))
def s(i,j):
    d=fr[i]['disp'] if j==i-1 else tuple(-x for x in fr[j]['disp']); return int(round(d[0])),int(round(d[1]))
def sh(x,t): return x[M+t[0]:M+t[0]+H,M+t[1]:M+t[1]+W]
rows=[]; tot={'p':0,'t':0,'both':0}
for i in range(1,N-1):
    sc={k:fr[i][k][cy,cx]-np.maximum(sh(mxr(fr[i-1][k],1),s(i,i-1)),sh(mxr(fr[i+1][k],1),s(i,i+1))) for k in ('p','t','hd')}
    pl=fr[i]['plant'][cy,cx]; sp=(sc['p']>MG)&pl; st=(sc['t']>MG)&pl
    tot['p']+=int(sp.sum()); tot['t']+=int(st.sum()); tot['both']+=int((sp&mxr(st.astype(np.uint8),1).astype(bool)).sum())
    for y,x in zip(*np.nonzero(sp)): rows.append((sc['p'][y,x],F0+i,X0+x,Y0+y,sc['t'][y,x],sc['hd'][y,x],fr[i]['h'][cy,cx][y,x],int(fr[i]['own'][cy,cx][y,x])))
print(f"crop x{X0}-{X1} y{Y0}-{Y1} {F0}..{F0+N-1} EXSHIP={XS} MG={MG}: present sparkles {tot['p']}, taa sparkles {tot['t']}, present sparkles with a taa sparkle within 1 px {tot['both']}")
rows.sort(reverse=True)
for r in rows[:TOP]: print(f"  present score {r[0]:.1f} frame {r[1]} x{r[2]} y{r[3]} taa score {r[4]:.1f} hdr score {r[5]:.1f} h {r[6]} owned {r[7]}")
big=[r for r in rows if r[0]>12]; print(f"present score>12: {len(big)}; of those taa score>6: {sum(r[4]>6 for r in big)}, taa score in (2,6]: {sum(2<r[4]<=6 for r in big)}, region(h>0): {sum(r[6]>0 for r in big)}")
