import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import Counter
import numpy as np
import bob1, lod_overlay
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')), lod_overlay.MAX_TRAILING)
lod0 = bob1.lods(tree)[0]
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float); UV=np.array([[p[4],p[5]] for p in pts], float)/65536
def faces_of(m): return [f for part in lod0['parts'] for g in part['groups'] if g['material'] in m for f in g['faces']]
mf=faces_of({4})
# faces in the first row segment (z centre -54900) and x centre -14200
C=np.array([P[list(f[:3])].mean(0) for f in mf]); E=np.array([P[list(f[:3])].max(0)-P[list(f[:3])].min(0) for f in mf])
sel=(np.abs(C[:,2]+54900)<4500)&(np.abs(C[:,0]+14200)<4000)
print('faces in segment', sel.sum())
# longitudinal faces: long in z, short in x
lon=sel&(E[:,2]>1000)&(E[:,0]<400)
xs=np.sort(np.round(C[lon,0])); print('longitudinal faces', lon.sum(), 'x centres clusters:')
cl=[]; 
for x in xs:
    if cl and x-cl[-1][-1]<120: cl[-1].append(x)
    else: cl.append([x])
cent=[np.mean(c) for c in cl]; print('  clusters', [(round(np.mean(c)), len(c), round(max(c)-min(c))) for c in cl])
print('  pitch between clusters', np.diff(cent).round(0).tolist())
# cross faces: long in x, short in z
cro=sel&(E[:,0]>1000)&(E[:,2]<400); zs=np.sort(np.round(C[cro,2])); print('cross faces', cro.sum(), 'z centres', np.unique(np.round(zs,-2)).tolist())
# cross-section of a longitudinal beam: faces of the first cluster: y range and x range
c0=cl[0]; m=lon&(np.abs(C[:,0]-np.mean(c0))<130); idx=sorted({i for f,k in zip(mf,m) if k for i in f[:3]}); Q=P[idx]
print('  beam 0: x', Q[:,0].min(), Q[:,0].max(), 'y', Q[:,1].min(), Q[:,1].max(), 'z', Q[:,2].min(), Q[:,2].max(), 'faces', m.sum())
# per-face normals of beam 0
def fn(f):
    a,b,c=[P[i] for i in f[:3]]; v=np.cross(b-a,c-a); l=np.linalg.norm(v); return (v/l).round(2) if l else v
print('  beam 0 face normals:', Counter(tuple(fn(f)) for f,k in zip(mf,m) if k).most_common(6))
# uv span per beam face (texture repeats along z)
uvs=np.array([UV[list(f[:3])].max(0)-UV[list(f[:3])].min(0) for f,k in zip(mf,m) if k]); print('  beam 0 face uv spans p50', np.median(uvs,axis=0).round(2).tolist(), 'max', uvs.max(0).round(2).tolist())
# compare with slat x centres in the same row/segment
cf=faces_of({21}); Cc=np.array([P[list(f[:3])].mean(0) for f in cf]); s2=(np.abs(Cc[:,2]+54900)<4500)&(np.abs(Cc[:,0]+14200)<4000)
print('slat x centres in this segment:', np.unique(np.round(Cc[s2,0],-2)).tolist())
