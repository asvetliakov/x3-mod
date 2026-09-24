# per frame: motion_route draws grouped by (model, lod, vs/ps, zwrite, atest, blend, routed, fade_arm, vertex_count, primitives, unmatched)
# plus object_bounds union box per group. usage: plant_draws.py RUN FRAME [X0 Y0 X1 Y1 screen filter on object_bounds]
import sys,glob,collections
r,f=sys.argv[1],sys.argv[2]; L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
flt=list(map(float,sys.argv[3:7])) if len(sys.argv)>6 else None
R={};B={};C={}
pre=tuple(f"{t} device=1 frame={f} ".encode() for t in ('motion_route','object_bounds','object_context'))
for l in open(L,'rb'):
    if not l.startswith(pre): continue
    t=l.split(b' ',1)[0]; d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv)
    {b'motion_route':R,b'object_bounds':B,b'object_context':C}[t][int(d['index'])]=d
G=collections.defaultdict(list)
for i,d in sorted(R.items()):
    b=B.get(i)
    if flt and (not b or float(b['sx1'])<flt[0] or float(b['sx0'])>flt[2] or float(b['sy1'])<flt[1] or float(b['sy0'])>flt[3]): continue
    k=(d['model'],d['lod'],d['vs'][:8]+'/'+d['ps'][:8],'z'+d['zwrite'],'at'+d['atest'],f"b{d['blend']}:{d['src']}/{d['dst']}",'R'+d['routed'],'arm'+d['fade_arm'],'p'+d['fade_permille'],'v'+d['vertex_count'],'prim'+d['primitives'],d['unmatched'])
    G[k].append((i,d['node'],b))
for k,v in G.items():
    nodes={n for _,n,_ in v}; bs=[b for *_,b in v if b]
    box=f"{min(float(b['sx0']) for b in bs):.0f},{min(float(b['sy0']) for b in bs):.0f}-{max(float(b['sx1']) for b in bs):.0f},{max(float(b['sy1']) for b in bs):.0f} z{min(float(b['zmin']) for b in bs):.6f}" if bs else '-'
    print(f"n={len(v):3d} nodes={len(nodes):2d} idx={v[0][0]}..{v[-1][0]}",' '.join(k),box)
print('total motion_route',len(R))
