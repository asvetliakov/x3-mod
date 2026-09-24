# Screen motion of the panel nodes (model 54b3) per frame from object_bounds box centres (same node, consecutive frames),
# and the routed motion-vector magnitude on the plant crop from motion_1 (prev unjittered UV - pixel centre, px).
# usage: plant_motion.py RUN F0 N MODEL
import sys,glob,numpy as np,collections
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); model=sys.argv[4]; L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
W=set(range(F0,F0+N)); B=collections.defaultdict(dict)
for l in open(L,'rb'):
    if not l.startswith(b'object_bounds '): continue
    d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv)
    f=int(d['frame'])
    if f in W and d['model']==model: B[f][d['node']]=((float(d['sx0'])+float(d['sx1']))/2,(float(d['sy0'])+float(d['sy1']))/2)
for f in range(F0+1,F0+N):
    common=set(B[f])&set(B[f-1]); v=[np.hypot(B[f][n][0]-B[f-1][n][0],B[f][n][1]-B[f-1][n][1]) for n in common]
    xs=[B[f][n][0] for n in B[f]]; ys=[B[f][n][1] for n in B[f]]
    print(f"frame {f}: nodes {len(common)} box-centre shift px p50 {np.median(v):.2f} min {min(v):.2f} max {max(v):.2f}; plant centre {np.mean(xs):.0f},{np.mean(ys):.0f}")
