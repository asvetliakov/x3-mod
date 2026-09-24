# one frame: per draw index join motion_route (pair, state, routed, fade_arm, permille, unmatched) with object_context node and object_bounds screen box
# usage: draw_table.py RUN FRAME  -> prints one row per motion_route draw, then per-node class counts
import sys,glob,collections
r,f=sys.argv[1],sys.argv[2]; L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
R={};C={};B={}
pre=tuple(f"{t} device=1 frame={f} ".encode() for t in ('motion_route','object_context','object_bounds'))
for l in open(L,'rb'):
    if not l.startswith(pre): continue
    t=l.split(b' ',1)[0].decode(); d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv)
    i=int(d['index']); {'motion_route':R,'object_context':C,'object_bounds':B}[t][i]=d
N=collections.defaultdict(collections.Counter)
for i in sorted(R):
    d=R[i]; node=C.get(i,{}).get('node','-'); b=B.get(i)
    box=f"{b['sx0']},{b['sy0']}-{b['sx1']},{b['sy1']} z{b['zmin']}" if b else '-'
    cls=f"{d['vs'][:8]}/{d['ps'][:8]} b{d['blend']}:{d['src']}/{d['dst']} z{d['zwrite']} at{d['atest']} R{d['routed']} arm{d['fade_arm']} p{d['fade_permille']} {d['unmatched']}"
    print(i,node,cls,box)
    N[node][f"{d['vs'][:8]}/{d['ps'][:8]} z{d['zwrite']}at{d['atest']} R{d['routed']}arm{d['fade_arm']} {d['unmatched']}"]+=1
print('--- per node')
for n,c in N.items(): print(n,dict(c))
