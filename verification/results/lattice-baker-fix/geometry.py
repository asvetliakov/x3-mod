import sys, math
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import defaultdict, Counter
import numpy as np
import bob1, lod_overlay
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
entry = bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')
tree = bob1.parse(assets.read_entry(entry), lod_overlay.MAX_TRAILING)
lod0 = bob1.lods(tree)[0]
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float)
N = np.array([[p[6],p[7],p[8]] for p in pts], float)/65536
def faces_of(mats): return [f for part in lod0['parts'] for g in part['groups'] if g['material'] in mats for f in g['faces']]
def components(faces):
    key = lambda i: tuple(P[i].astype(int))
    parent = list(range(len(faces)))
    def find(a):
        while parent[a]!=a: parent[a]=parent[parent[a]]; a=parent[a]
        return a
    edges = defaultdict(list)
    for fi,f in enumerate(faces):
        ks=[key(i) for i in f[:3]]
        for a,b in ((0,1),(1,2),(0,2)):
            e=tuple(sorted((ks[a],ks[b]))); edges[e].append(fi)
    for e,fs in edges.items():
        for x in fs[1:]:
            ra,rb=find(fs[0]),find(x)
            if ra!=rb: parent[rb]=ra
    comps=defaultdict(list)
    for fi in range(len(faces)): comps[find(fi)].append(fi)
    return list(comps.values()), edges
def describe(name, mats):
    faces = faces_of(mats); comps, edges = components(faces)
    print(f'--- {name}: mats {sorted(mats)} faces {len(faces)} components {len(comps)}; faces/comp {Counter(len(c) for c in comps).most_common(6)}')
    shared = sum(1 for fs in edges.values() if len(fs)>1); print(f'  edges {len(edges)} shared {shared}')
    rows=[]
    for c in comps:
        idx = sorted({i for fi in c for i in faces[fi][:3]}); Q=P[idx]
        ctr=Q.mean(0); n=N[idx].mean(0)
        # principal extents in the plane
        u,s,vt=np.linalg.svd(Q-ctr, full_matrices=False)
        ext=(Q-ctr)@vt.T; sz=ext.max(0)-ext.min(0)
        rows.append((ctr,n,sz,vt,len(c)))
    return faces, comps, rows
cf, cc, crows = describe('cells', {21})
sz=np.array([r[2] for r in crows]); ctr=np.array([r[0] for r in crows]); nrm=np.array([r[1] for r in crows])
print('  cell sizes (long, short, thickness) p5/p50/p95:', np.percentile(sz,[5,50,95],axis=0).round(1).tolist())
print('  cell normals mean', nrm.mean(0).round(3), 'abs mean', np.abs(nrm).mean(0).round(3))
print('  cell centre bbox', ctr.min(0).round(0).tolist(), ctr.max(0).round(0).tolist())
# pitch: nearest neighbour among cell centres along the grid
def knn(A,B,k):
    D=np.linalg.norm(A[:,None,:]-B[None,:,:],axis=-1); i=np.argsort(D,axis=1)[:,:k]; return np.take_along_axis(D,i,1), i
d,i=knn(ctr,ctr,5)
print('  nn distance p5/p50/p95 (k=1..4):', [np.percentile(d[:,k],[5,50,95]).round(1).tolist() for k in (1,2,3,4)])
# gap along the nearest neighbour direction: pitch - (half sizes projected)
gaps=[]
for a in range(len(ctr)):
    for k in (1,2):
        b=i[a,k]; v=ctr[b]-ctr[a]; L=np.linalg.norm(v); v/=L
        # extent of each quad along v
        def half(q):
            idx=sorted({j for fi in cc[q] for j in cf[fi][:3]}); return ((P[idx]-ctr[q])@v).max()
        gaps.append(L-half(a)-half(b))
gaps=np.array(gaps); print('  gap between neighbouring cells along nn dir p5/p50/p95:', np.percentile(gaps,[5,50,95]).round(1).tolist(), 'frac<=0', np.mean(gaps<=0).round(3))
# y (thickness axis) of cells
print('  cell y (thickness axis) p5/p50/p95', np.percentile(ctr[:,1],[5,50,95]).round(1).tolist(), 'distinct planes', np.unique(np.round(ctr[:,1],-1)).size)
# strip internals: distinct in-plane coordinates of one strip
c0=cc[0]; idx=sorted({j for fi in c0 for j in cf[fi][:3]}); Q=P[idx]; vt=crows[0][3]; e=(Q-crows[0][0])@vt.T
print('  strip 0: points', len(idx), 'distinct long-axis coords', np.unique(np.round(e[:,0],0)).size, 'short-axis', np.unique(np.round(e[:,1],0)).size, 'long coords', np.unique(np.round(e[:,0],0)).tolist()[:20])
print('  strip long-axis direction', vt[0].round(3), 'short', vt[1].round(3))
# substrate/frame material 9
ff, fc, frows = describe('frame mat9', {9})
fsz=np.array([r[2] for r in frows]); fctr=np.array([r[0] for r in frows]); fn=np.array([r[1] for r in frows])
print('  frame comp sizes (long, short, thick) p5/p50/p95:', np.percentile(fsz,[5,50,95],axis=0).round(1).tolist())
print('  frame comp count by faces', Counter(r[4] for r in frows).most_common(5))
print('  frame y p5/p50/p95', np.percentile(fctr[:,1],[5,50,95]).round(1).tolist(), 'normals abs mean', np.abs(fn).mean(0).round(3))
# for frame comps that are quads (2 faces): are they planar with cells (|n_y|~1) and where relative to cells?
big = fsz[:,0]
for nm, m in (('frame quads normal |ny|>0.9', np.abs(fn[:,1])>0.9), ('frame quads other normals', np.abs(fn[:,1])<=0.9)):
    if m.any(): print(f'  {nm}: n {m.sum()} sizes p50 {np.median(fsz[m],axis=0).round(1).tolist()} y p5/p50/p95 {np.percentile(fctr[m,1],[5,50,95]).round(1).tolist()}')
# y offset between cells and the in-plane frame quads near them
m=np.abs(fn[:,1])>0.9
if m.any():
    d2,i2=knn(ctr[:,[0,2]],fctr[m][:,[0,2]],1); d2=d2[:,0]; i2=i2[:,0]
    dy = fctr[m][i2,1]-ctr[:,1]
    print('  nearest in-plane frame quad to each cell: xz dist p50', np.median(d2).round(1), 'dy (frame-cell) p5/p50/p95', np.percentile(dy,[5,50,95]).round(1).tolist())
# struts
sf, sc, srows = describe('struts mats 4,5,6,18,0', {4,5,6,18,0})
ssz=np.array([r[2] for r in srows]); sctr=np.array([r[0] for r in srows])
print('  strut comp sizes p50', np.median(ssz,axis=0).round(1).tolist(), 'y p5/p50/p95', np.percentile(sctr[:,1],[5,50,95]).round(1).tolist())
# other opaque mats: y ranges
for mset,nm in (({1,2,3,7,8,10,11,12,13,14,15,16,17,19,20},'other opaque'),):
    of=faces_of(mset); idx=sorted({i for f in of for i in f[:3]}); print(f'--- {nm}: faces {len(of)} y range', P[idx][:,1].min(), P[idx][:,1].max(), 'x range', P[idx][:,0].min(), P[idx][:,0].max())
# per-material y ranges
for part in lod0['parts']:
    for g in part['groups']:
        idx=sorted({i for f in g['faces'] for i in f[:3]}); Q=P[idx]
        print(f'  mat {g["material"]:2d} faces {len(g["faces"]):5d} bbox x {Q[:,0].min():6.0f}..{Q[:,0].max():6.0f} y {Q[:,1].min():6.0f}..{Q[:,1].max():6.0f} z {Q[:,2].min():6.0f}..{Q[:,2].max():6.0f}')
