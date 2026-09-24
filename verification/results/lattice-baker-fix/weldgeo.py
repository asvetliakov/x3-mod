"""Reviewer check (2026-09-25) of the weld on the real terran_spp_panel record 1: face areas, winding (ny sign)
before/after, strip shape. Read-only; from the repository root:
  python3 verification/results/lattice-baker-fix/weldgeo.py > verification/results/lattice-baker-fix/weldgeo_out.txt"""
import sys, numpy as np
sys.path.insert(0,'tools/analysis')
import bob1, lod_overlay, lod_recipes
a,_=lod_overlay.original_assets(bob1.DEFAULT_GAME)
name='stations/x3tc/terran_spp_panel'
L=bob1.lods(bob1.parse(a.read_entry(bob1.resolve_body(a,name)),lod_overlay.MAX_TRAILING))
n,rec,rep=lod_recipes.prepare(*lod_recipes.lookup(name),L)
print('op',{k:v for k,v in rep['ops'][0].items()})
P0=np.array([p[1:4] for p in L[1]['points']],float); P1=np.array([p[1:4] for p in rec['points']],float)
F=np.array([f[:3] for f in lod_recipes.material_faces(L[1],21)])
def nrm(P):
    a,b,c=P[F[:,0]],P[F[:,1]],P[F[:,2]]; return np.cross(b-a,c-a)
n0,n1=nrm(P0),nrm(P1)
area1=np.linalg.norm(n1,axis=1)/2
print('faces',len(F),'zero-area after',int((area1<1e-6).sum()),'min area after',area1.min(),'min area before',(np.linalg.norm(n0,axis=1)/2).min())
print('ny sign before +/-',int((n0[:,1]>0).sum()),int((n0[:,1]<0).sum()),'after +/-',int((n1[:,1]>0).sum()),int((n1[:,1]<0).sum()))
print('orientation flips',int((np.sign(n0[:,1])!=np.sign(n1[:,1])).sum()))
ss=lod_recipes.strips(L[1],21,'z')
print('thickness max',max(s['thickness'] for s in ss),'tilt range',min(s['tilt_deg'] for s in ss),max(s['tilt_deg'] for s in ss))
print('across range',min(s['across'] for s in ss),max(s['across'] for s in ss),'long set',sorted({round(s['long']) for s in ss}))
# normals attribute kept? stored normal vs new geometric normal
nrm_attr=np.array([p[6:9] for p in rec['points']],float) if len(rec['points'][0])>8 else None
print('point tuple len',len(rec['points'][0]), rec['points'][F[0,0]])
# distinct y positions of strip points before
idx=sorted(set(F.ravel()))
print('before y range',P0[idx,1].min(),P0[idx,1].max(),'girder/other max y', max(p[2] for i,p in enumerate(L[1]['points']) if i not in set(idx)))
# duplicates: points with same position before but different after
from collections import defaultdict
g=defaultdict(set)
for i in idx: g[tuple(P0[i])].add(tuple(P1[i]))
print('positions mapped to >1 new position',sum(1 for v in g.values() if len(v)>1))
# edge-sharing within strip: count edges whose endpoints coincide pre but not post
print('distinct x values after per strip (first 3)',[len({P1[i,0] for i in s['idx']}) for s in ss[:3]], [len({P0[i,0] for i in s['idx']}) for s in ss[:3]])
