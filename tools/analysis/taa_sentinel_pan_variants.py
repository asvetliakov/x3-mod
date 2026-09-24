#!/usr/bin/env python3
"""Variant table for taa_sentinel_pan_replay.py: station flicker by speed bin, gradient, box-bind, trail and emitter excess.
usage: taa_sentinel_pan_variants.py <dump dir> <f0-f1> <x0,y0,x1,y1> <out.json> <variant|variant...> [station track json {pos:{frame:[x,y]}}]
X3M_AGE_DIR: directory holding taa_age_1_<frame>.r32f when the dump dir lacks them. Host only."""
import sys, json, numpy as np, warnings; warnings.filterwarnings('ignore')
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__))); import taa_sentinel_pan_replay as T
L=__import__("os").environ.get("X3M_AGE_DIR") or sys.argv[1]  # taa_age_1_* may live only in the bottle captures dir
D,frames,roi,outp,names=sys.argv[1],sys.argv[2],tuple(map(int,sys.argv[3].split(','))),sys.argv[4],sys.argv[5].split('|')
posf=sys.argv[6] if len(sys.argv)>6 else None
V=T.variant
VARS={'installed':V(), 'inst_S1':V(S=1.), 'inst_W094':V(W_=.94), 'inst_box11':V(box=5), 'inst_boxoff':V(box=0,nobox=True),
 'fix':V(oracle_reject=False), 'fix_S1':V(S=1.,oracle_reject=False), 'fix_W094':V(W_=.94,oracle_reject=False), 'fix_box11':V(box=5,oracle_reject=False),
 'fix_boxoff':V(box=0,oracle_reject=False), 'fix_keys0':V(keys=0.,oracle_reject=False), 'fix_S085':V(S=.85,oracle_reject=False), 'fix_var1':V(varbox=1.,oracle_reject=False), 'fix_var2':V(varbox=2.,oracle_reject=False),
 'fix_keys065':V(keys=-.65,oracle_reject=False), 'fix_S1_box11':V(S=1.,box=5,oracle_reject=False), 'fix_E0':V(E=0.,oracle_reject=False), 'inst_E0':V(E=0.), 'S0':V(S=0.), 'fix_S0':V(S=0.,oracle_reject=False)}
m=T.build(D,frames,roi); fr=m['frames']; meta=m['meta']; X0,Y0,X1,Y1=roi; W,H=1280,768; h,w=Y1-Y0,X1-X0
age0=np.floor(np.abs(np.fromfile('%s/taa_age_1_%d.r32f'%(L,fr[0]),np.float32).reshape(768,1280)[Y0:Y1,X0:X1].astype(float)))  # floor(abs): a negative count is the exit reset's mark (seta-sky-hull-share-decay.md)
def bil(a,x,y):
    x=np.clip(x,0,a.shape[1]-1.001); y=np.clip(y,0,a.shape[0]-1.001); xx=np.floor(x).astype(int); yy=np.floor(y).astype(int); fx=x-xx; fy=y-yy
    return a[yy,xx]*(1-fx)*(1-fy)+a[yy,xx+1]*fx*(1-fy)+a[yy+1,xx]*(1-fx)*fy+a[yy+1,xx+1]*fx*fy
pos=json.load(open(posf))['pos'] if posf else None
cur=[m['codes'](m['load']('hdr',f,'rgba16f',np.float16,4)[Y0-1:Y1+1,X0-1:X1+1],meta[f]['k']) for f in fr[1:]]
max3=[np.max([c[1+a:1+a+h,1+b:1+b+w] for a in (-1,0,1) for b in (-1,0,1)],0) for c in cur]
raw=[np.maximum(m['load']('hdr',f,'rgba16f',np.float16,4)[Y0:Y1,X0:X1,:3].astype(float)@m['LUMA'],0) for f in fr[1:]]
res={}
for n in names:
    o=VARS[n]
    outs,dg=T.run(m,o,age0); C=[m['codes'](x,meta[f]['k']) for x,f in zip(outs,fr[1:])]
    rows=[]
    for i,f in enumerate(fr[1:]):
        ex=np.maximum(C[i]-max3[i],0); ex[:8]=0; ex[-8:]=0; ex[:,:8]=0; ex[:,-8:]=0
        near=T.winf(raw[i],8,np.maximum)>1.0
        r=dict(frame=f,trail_gt4=int((ex>4).sum()),trail_max=float(ex.max()),emit_near_px=int(near.sum()),emit_trail_gt4=int((ex[near]>4).sum()),emit_trail_max=float(ex[near].max()) if near.any() else 0.,
               emit_trail_mean=float(ex[near].mean()) if near.any() else 0., boxhit=float(dg[i]['boxhit'].mean()), reject=float((~dg[i]['accept']).mean()))
        if pos and i>0:
            cx,cy=pos[str(f)]; x0,x1,y0,y1=int(cx)-80-X0,int(cx)+81-X0,int(cy)-36-Y0,int(cy)+37-Y0
            if x0>40 and x1<w-40:
                sl=(slice(y0,y1),slice(x0,x1)); px=dg[i]['posx'][sl]-X0; py=dg[i]['posy'][sl]-Y0
                d=C[i][sl]-bil(C[i-1],px,py); di=cur[i][1:-1,1:-1][sl]-bil(cur[i-1][1:-1,1:-1],px,py)
                r.update(speed=float(dg[i]['speed'][sl].mean()),flicker=float(np.sqrt((d**2).mean())),in_flicker=float(np.sqrt((di**2).mean())),grad=float((np.diff(C[i][sl],axis=1)**2).mean()+(np.diff(C[i][sl],axis=0)**2).mean()),
                         st_boxhit=float(dg[i]['boxhit'][sl].mean()),st_reject=float((~dg[i]['accept'][sl]).mean()),st_keep=float(dg[i]['keep'][sl].mean()))
        rows.append(r)
    res[n]=rows; json.dump(res,open(outp,'w'))
    st=[r for r in rows if 'flicker' in r and r['frame']>=fr[0]+6]
    def b(lo,hi,key='flicker'):
        s=[r[key]**2 if key=='flicker' else r[key] for r in st if lo<=r['speed']<hi]; return (round(float(np.sqrt(np.mean(s))) if key=='flicker' else float(np.mean(s)),3),len(s)) if s else None
    orj={r['frame']:r['st_reject'] for r in res.get('installed',rows) if 'st_reject' in r}
    print(n,'IN',b(0,99,'in_flicker'),'keep',b(0,99,'st_keep'),'rejhalf',round(float(np.sqrt(np.mean([r['flicker']**2 for r in st if orj[r['frame']]>.01]))),3),'cleanhalf>15',round(float(np.sqrt(np.mean([r['flicker']**2 for r in st if orj[r['frame']]<=.01 and r['speed']>15]))),3),'flicker <5',b(0,5),'5-15',b(5,15),'>15',b(15,99),'| grad',b(0,99,'grad'),'boxhit >15',b(15,99,'st_boxhit'),'<5',b(0,5,'st_boxhit'),'| trail>4 px/frame',np.mean([r['trail_gt4'] for r in rows]),'max',max(r['trail_max'] for r in rows),'| emit gt4',np.mean([r['emit_trail_gt4'] for r in rows]),'max',max(r['emit_trail_max'] for r in rows),flush=True)
