"""Run336: frames >=906 with any flip, attributed to camera turn, leased change, or footprint_refused change.
Usage: flip_frames.py session.log rows.json"""
import sys,json,math,collections
cam={}
for line in open(sys.argv[1],errors='replace'):
    if line.startswith('camera_state '):
        d=dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
        try: cam[int(d['frame'])]=([float(d[f'r2{j}']) for j in range(3)],[float(x) for x in d['t'].split(',')])
        except Exception: pass
rot=lambda f: math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(cam[f-1][0],cam[f][0])))))) if f in cam and f-1 in cam else float('nan')
R=json.load(open(sys.argv[2])); can={int(d['frame']):d for d in R['shadow_replay_candidates']}
g=lambda f,k: float(can[f].get(k,0))
cls=collections.Counter(); rows=[]
for f in sorted(can):
    if f<906 or f-1 not in can: continue
    fl=[g(f,f'flip_c{i}') for i in range(5)]
    if sum(fl)==0: continue
    dl=g(f,'leased')-g(f-1,'leased'); dfp=[g(f,f'footprint_refused{i}')-g(f-1,f'footprint_refused{i}') for i in range(5)]
    r=rot(f)
    c='turn' if r>=0.5 else ('leased_change' if dl!=0 else ('footprint_change' if any(dfp) else 'other'))
    cls[c]+=1; rows.append((f,sum(fl),c,round(r,2),dl,dfp))
print('flip frames >=906', len(rows), dict(cls))
print('flip frames by 1000-frame bucket', sorted(collections.Counter(f//1000*1000 for f,*_ in rows).items()))
for r in sorted(rows,key=lambda x:-x[1])[:12]: print(' ',r)
