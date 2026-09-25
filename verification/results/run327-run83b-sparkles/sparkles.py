# Run 83 B (run327) output-side flicker and one-frame sparkles on a crop, input (hdr_1, pre-resolve) vs output (taa_1, the
# resolved FP16) vs present_1 (displayed bytes), with the resolve's actual region hold decoded from taa_age_1 (the age target
# the resolve wrote this frame: |age| = count + (h + 128 pairC) / 65536; h = 8 (L = jitter period) flagged this frame, 1..7
# held, 0 outside) and the box-target marker proxy (thin_box_*_half: computed where any pixel of the 2x2 block had the
# PREVIOUS frame's h > 0 at the same texel, unreprojected).
# Codes: c = 255 * (Y / (1 + Y))^(1/2.2) of Rec.709 luma (hdr, taa); present = Rec.709 luma of the bytes.
# Alignment (ALIGN=motion, default): rounded median of the motion_1 .xy (previous UV) displacement over the crop's far routed
# pixels (CTRL: all routed); ALIGN=search: per pair, integer shift (dy in +-S, dx in +-SX) minimising mean |dc_hdr| over the crop's plant (or all) pixels.
# Robust frame difference: min over the 3x3 of the aligned other frame (absorbs the sub-pixel residual of the integer shift).
# Sparkle at frame N (needs N-1 and N+1): c_N(p) - max3x3(c_{N-1} aligned) > MG and c_N(p) - max3x3(c_{N+1} aligned) > MG.
# Classes (frame N lane, depth_1: .r z/w, .g -1 owner, .b w, .a 1-thin): owned = routed .g==-1 .a==1 w>WMIN; thin = routed
# .a<1 w>WMIN; other = remaining routed w>WMIN; remainder = sentinel within 3 px of a far routed pixel; near = routed w<WMIN.
# Edge = dilate1(line) & plant, line = |c_hdr - mean5x5(c_hdr)| > DET codes (frame N).
# EXSHIP=R drops plant pixels within R px of a near routed pixel (the player ship silhouette, w < WMIN).
# usage: sparkles.py X0 Y0 X1 Y1 F0 N [env MG=6 JUMP=4 DET=4 S=16 SX=6 WMIN=20000 CTRL=0 (1: whole crop is the class 'all')]
import sys,os,numpy as np
X0,Y0,X1,Y1,F0,N=map(int,sys.argv[1:7]); E=os.environ.get
MG=float(E('MG',6)); JUMPS=[float(x) for x in E('JUMP','4,8,16').split(',')]; JUMP=JUMPS[0]; DET=float(E('DET',4)); S=int(E('S',16)); SX=int(E('SX',6)); WMIN=float(E('WMIN',20000)); CTRL=E('CTRL','0')=='1'
TD='/tmp/x3-bottleX3-run327'; BD='/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'
M=S+8; y0,y1,x0,x1=Y0-M,Y1+M,X0-M,X1+M; H,W=Y1-Y0,X1-X0
LUMA=np.array([0.2126,0.7152,0.0722],np.float32)
def mm(p,dt,ch): return np.array(np.memmap(p,dt,'r',shape=(1440,5120,ch) if ch>1 else (1440,5120))[y0:y1,x0:x1])
def code(Y): Y=np.nan_to_num(np.maximum(Y,0),nan=0,posinf=0); return 255*(Y/(1+Y))**(1/2.2)
def box(x,r):
    c=np.pad(x,((r+1,r),(r+1,r)),mode='edge').cumsum(0).cumsum(1); n=2*r+1
    return (c[n:,n:]-c[:-n,n:]-c[n:,:-n]+c[:-n,:-n])/(n*n)
def dil(m,r): return box(m.astype(np.float32),r)>0
def mx3(x):
    o=x.copy()
    for dy in(-1,0,1):
        for dx in(-1,0,1): o=np.maximum(o,np.roll(np.roll(x,dy,0),dx,1))
    return o
def load(f):
    hdr=mm(f"{TD}/hdr_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA
    taa=mm(f"{TD}/taa_1_{f}.rgba16f",np.float16,4)[...,:3].astype(np.float32)@LUMA
    pr=mm(f"{TD}/present_1_{f}.bgra8",np.uint8,4)[...,[2,1,0]].astype(np.float32)@LUMA
    a=mm(f"{TD}/depth_1_{f}.rgba32f",np.float32,4)
    v=np.abs(mm(f"{BD}/taa_age_1_{f}.r32f",np.float32,1)); held=np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64)
    h=held%128
    routed=a[...,0]!=-1; far=routed&(a[...,2]>WMIN); A=a[...,3]
    own=far&(a[...,1]==-1)&(A==1); thin=far&(A>=0)&(A<1); oth=far&~own&~thin
    rem=(~routed)&dil(far,3); near=routed&~far
    XS=int(E('EXSHIP',0))
    if XS and not CTRL:
        keep_=~dil(near,XS); own&=keep_; thin&=keep_; oth&=keep_; rem&=keep_
    cl={'owned':own,'thin':thin,'other':oth,'remainder':rem}
    if CTRL: cl={'all':np.ones_like(own)}
    plant=np.zeros_like(own)
    for m in cl.values(): plant|=m
    ch=code(hdr); line=np.abs(ch-box(ch,2))>DET; edge=dil(line,1)&plant
    mo=mm(f"{TD}/motion_1_{f}.rgba32f",np.float32,4); sel=(far if not CTRL else routed)[M:M+H,M:M+W]
    gy,gx=np.mgrid[y0:y1,x0:x1]; dy=mo[...,1]*1440-(gy+0.5); dx=mo[...,0]*5120-(gx+0.5)
    disp=(float(np.median(dy[M:M+H,M:M+W][sel])),float(np.median(dx[M:M+H,M:M+W][sel]))) if sel.any() else (0.0,0.0)
    return dict(disp=disp,c={'hdr':ch,'taa':code(taa),'present':pr},cl=cl,plant=plant,edge=edge,h=h,near=near)
fr=[load(F0+i) for i in range(N)]
cy,cx=slice(M,M+H),slice(M,M+W)
def align(i,j):   # shift of frame j relative to i
    if E('ALIGN','motion')=='motion':
        d=fr[i]['disp'] if j==i-1 else tuple(-x for x in fr[j]['disp'])
        return int(round(d[0])),int(round(d[1]))
    ci=fr[i]['c']['hdr'][cy,cx]; cj=fr[j]['c']['hdr']; m=fr[i]['plant'][cy,cx]; best=None
    for dy in range(-S,S+1):
        for dx in range(-SX,SX+1):
            e=np.abs(cj[M+dy:M+dy+H,M+dx:M+dx+W]-ci)[m].mean()
            if best is None or e<best[0]: best=(e,dy,dx)
    return best[1],best[2]
def sh(x,s): return x[M+s[0]:M+s[0]+H,M+s[1]:M+s[1]+W]
shifts={}
for i in range(N):
    for j in (i-1,i+1):
        if 0<=j<N: shifts[(i,j)]=align(i,j)
print(f"crop x{X0}-{X1} y{Y0}-{Y1} frames {F0}..{F0+N-1} MG={MG} JUMP={JUMP} DET={DET}")
print("motion disp (to previous frame, px dy,dx):",' '.join(f"{F0+i}:({fr[i]['disp'][0]:+.2f},{fr[i]['disp'][1]:+.2f})" for i in range(N)))
print("shifts (i->i+1):",' '.join(f"{F0+i}:{shifts[(i,i+1)]}" for i in range(N-1)))
# 1. frame-to-frame robust |dc| per class and on edges; jump fraction on edges
for k in ('hdr','taa','present'):
    rows=[]
    for i in range(1,N):
        s=shifts[(i,i-1)]; cur=fr[i]['c'][k][cy,cx]; prv=fr[i-1]['c'][k]
        d=np.full(cur.shape,1e9,np.float32)
        for dy in(-1,0,1):
            for dx in(-1,0,1): d=np.minimum(d,np.abs(cur-sh(prv,(s[0]+dy,s[1]+dx))))
        dd=np.abs(cur-sh(prv,s))
        e=fr[i]['edge'][cy,cx]; r={'edge_n':int(e.sum()),'edge_mean_min3':d[e].mean(),'edge_jump_frac':np.mean(d[e]>JUMP),'edge_mean_direct':dd[e].mean()}
        for J in JUMPS[1:]: r[f'edge_jump>{J:g}']=np.mean(d[e]>J)
        for n,m in fr[i]['cl'].items():
            m=m[cy,cx]; r[n+'_n']=int(m.sum()); r[n+'_min3']=d[m].mean() if m.any() else np.nan; r[n+'_jump']=np.mean(d[m]>JUMP) if m.any() else np.nan
        rows.append(r)
    keys=[x for x in rows[0] if not x.endswith('_n')]
    print(f"[{k}] mean over pairs: "+' '.join(f"{x} {np.nanmean([r[x] for r in rows]):.3f}" for x in keys)+f" | edge_jump_frac per pair "+' '.join(f"{r['edge_jump_frac']:.3f}" for r in rows))
    if all(shifts[(i,i+1)]==(0,0) for i in range(N-1)):
        st=np.stack([fr[i]['c'][k][cy,cx] for i in range(N)]).std(0); e=fr[0]['edge'][cy,cx]
        print(f"[{k}] rest 8-frame per-pixel std: edge mean {st[e].mean():.3f} p95 {np.percentile(st[e],95):.2f} p99 {np.percentile(st[e],99):.2f}; "+' '.join(f"{n} {st[m[cy,cx]].mean():.3f}" for n,m in fr[0]['cl'].items() if m[cy,cx].any()))
# 2. sparkles
tab={}
for k in ('hdr','taa','present'):
    tot=0; per=[]; allv=[]
    for i in range(1,N-1):
        cur=fr[i]['c'][k][cy,cx]
        ref=np.maximum(sh(mx3(fr[i-1]['c'][k]),shifts[(i,i-1)]),sh(mx3(fr[i+1]['c'][k]),shifts[(i,i+1)]))
        sv=cur-ref; pl=fr[i]['plant'][cy,cx]; allv.append(sv[pl])
        sp=(sv>MG)&pl; per.append(int(sp.sum())); tot+=sp.sum()
        if k=='taa':
            prevh=sh(fr[i-1]['h'],(0,0)); hN=fr[i]['h'][cy,cx]
            # marker proxy: any pixel of the aligned 2x2 block (absolute even coords) had prev h>0 at the same texel
            ph=fr[i-1]['h']>0; yy=(np.arange(y0,y1)//2)*2-y0; xx=(np.arange(x0,x1)//2)*2-x0
            blk=np.zeros_like(ph)
            for oy in(0,1):
                for ox in(0,1): blk|=ph[np.clip(yy+oy,0,ph.shape[0]-1)][:,np.clip(xx+ox,0,ph.shape[1]-1)]
            mk=blk[cy,cx]; reg=hN>0; regd=dil(reg,2)
            state={'flag8':reg&(hN==8),'held1-7':reg&(hN<8),'out_border2':~reg&regd,'out_far':~regd}
            boxsrc={'block_box(marker)':reg&mk,'inplace7x7(no marker)':reg&~mk,'clip3x3':~reg}
            for base,msk in (('sparkle',sp),('edge',fr[i]['edge'][cy,cx]),('plant',pl)):
                for n,c in fr[i]['cl'].items():
                    c=c[cy,cx]
                    for sn,sm in list(state.items())+list(boxsrc.items()):
                        tab[(base,n,sn)]=tab.get((base,n,sn),0)+int((msk&c&sm).sum())
    allv=np.concatenate(allv)
    print(f"[{k}] sparkles (plant px, margin {MG}) per interior frame {per} total {tot}; score p99 {np.percentile(allv,99):.2f} p99.9 {np.percentile(allv,99.9):.2f} max {allv.max():.1f}")
states=['flag8','held1-7','out_border2','out_far','block_box(marker)','inplace7x7(no marker)','clip3x3']
print("taa tabulation (sum over interior frames): base class | "+' '.join(states))
for base in ('sparkle','edge','plant'):
    for n in fr[0]['cl']:
        print(f"  {base:8s} {n:10s} | "+' '.join(f"{tab.get((base,n,s),0):7d}" for s in states))
