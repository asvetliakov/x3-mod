# motion_route rows on capture frames: per burst frame, draws grouped by (vs, ps, blend src/dst, zwrite, atest, routed, fade_arm, unmatched) with fade_permille range
# usage: route_rows.py RUN FRAME...   (station families flagged with *)
import sys,glob,re,collections
FAM={'494fe349b8bc12ec','53a0a641107ed76c','c78b4c68a87fce74','803ebfd17f79e413'}
r=sys.argv[1]; want=set(sys.argv[2:]); L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
G=collections.defaultdict(lambda: collections.defaultdict(list))
for l in open(L,'rb'):
    if not l.startswith(b'motion_route '): continue
    d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv)
    if want and d['frame'] not in want: continue
    k=(d['vs'],d['ps'],f"b{d['blend']}:{d['src']}/{d['dst']}",f"z{d['zwrite']}",f"at{d['atest']}",f"routed{d['routed']}",f"arm{d['fade_arm']}",d.get('unmatched','-'))
    G[d['frame']][k].append(int(d['fade_permille']))
for f in sorted(G,key=int):
    tot=sum(len(v) for v in G[f].values())
    print(f"frame {f} motion_route rows {tot}")
    for k,v in sorted(G[f].items(),key=lambda kv:(-len(kv[1]))):
        star='*' if k[0] in FAM else ' '
        print(f"  {star} n={len(v):3d} {' '.join(k)} permille {min(v)}-{max(v)}")
