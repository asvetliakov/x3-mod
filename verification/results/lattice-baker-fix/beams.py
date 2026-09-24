import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import defaultdict, Counter
import numpy as np
import bob1, lod_overlay
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')), lod_overlay.MAX_TRAILING)
lod0 = bob1.lods(tree)[0]
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float); UV=np.array([[p[4],p[5]] for p in pts], float)/65536
def faces_of(m): return [f for part in lod0['parts'] for g in part['groups'] if g['material'] in m for f in g['faces']]
def comps(faces):
    key=lambda i: tuple(P[i].astype(int)); parent=list(range(len(faces)))
    def find(a):
        while parent[a]!=a: parent[a]=parent[parent[a]]; a=parent[a]
        return a
    edges=defaultdict(list)
    for fi,f in enumerate(faces):
        ks=[key(i) for i in f[:3]]
        for a,b in ((0,1),(1,2),(0,2)): edges[tuple(sorted((ks[a],ks[b])))].append(fi)
    for fs in edges.values():
        for x in fs[1:]:
            ra,rb=find(fs[0]),find(x)
            if ra!=rb: parent[rb]=ra
    out=defaultdict(list)
    for fi in range(len(faces)): out[find(fi)].append(fi)
    return list(out.values())
def fnormal(f):
    a,b,c=[P[i] for i in f[:3]]; v=np.cross(b-a,c-a); l=np.linalg.norm(v); return v/l if l else v
cf=faces_of({21}); cc=comps(cf)
pn=np.array([0.252,0.967,0.0]); pn/=np.linalg.norm(pn); e1=np.array([0,0,1.0]); e2=np.cross(pn,e1); e2/=np.linalg.norm(e2)
for m in (4,6,18,14,16,17,7):
    mf=faces_of({m}); mc=comps(mf)
    rows=[]
    for c in mc:
        idx=sorted({i for fi in c for i in mf[fi][:3]}); Q=P[idx]; rows.append((len(c), *(Q.max(0)-Q.min(0)), *Q.mean(0)))
    rows=np.array(rows)
    print(f'mat {m}: faces {len(mf)} comps {len(mc)}; extent (dx,dy,dz) p50 {np.median(rows[:,1:4],axis=0).round(0).tolist()}; y centre p5/p50/p95 {np.percentile(rows[:,5],[5,50,95]).round(0).tolist()}; faces/comp {Counter(rows[:,0].astype(int)).most_common(4)}')
    if m==4:
        big=rows[rows[:,0]>=8]
        print('   mat4 comps >=8 faces: n', len(big), 'dx p50', np.median(big[:,1]).round(0), 'dz p50', np.median(big[:,3]).round(0), 'dy p50', np.median(big[:,2]).round(0))
        xs=np.sort(big[:,4]); zs=np.sort(big[:,6])
        print('   distinct x centres (rounded 50):', np.unique(np.round(big[:,4],-2)).size, ' distinct z centres:', np.unique(np.round(big[:,6],-2)).size)
        print('   z centres:', np.unique(np.round(big[:,6],-2)).tolist()[:40])
        print('   x centres:', np.unique(np.round(big[:,4],-2)).tolist()[:40])
# beams above the pane: raster of mat 4 faces over slat 0..3 (max height along pn)
mf=faces_of({4})
for s in (0,1,2,60):
    pidx=sorted({i for fi in cc[s] for i in cf[fi][:3]}); pc=P[pidx].mean(0); pe=P[pidx]-pc; L1=(pe@e1).max(); L2=(pe@e2).max()
    res=10.0; zs=np.arange(-L1,L1,res); xs=np.arange(-L2,L2,res); Z,X=np.meshgrid(zs,xs,indexing='ij'); H=np.full(Z.shape,-1e9); cnt=np.zeros(Z.shape,int)
    for f in mf:
        tri=P[list(f[:3])]-pc; u=tri@e1; v=tri@e2; h=tri@pn
        if u.max()<-L1 or u.min()>L1 or v.max()<-L2 or v.min()>L2: continue
        sl=(slice(max(0,int((u.min()+L1)//res)),min(len(zs),int((u.max()+L1)//res)+2)), slice(max(0,int((v.min()+L2)//res)),min(len(xs),int((v.max()+L2)//res)+2)))
        zz=Z[sl]; xx=X[sl]; d=(u[1]-u[0])*(v[2]-v[0])-(u[2]-u[0])*(v[1]-v[0])
        if abs(d)<1e-9: continue
        l1=((v[2]-v[0])*(zz-u[0])-(u[2]-u[0])*(xx-v[0]))/d; l2=(-(v[1]-v[0])*(zz-u[0])+(u[1]-u[0])*(xx-v[0]))/d; l0=1-l1-l2
        inside=(l0>=-1e-6)&(l1>=-1e-6)&(l2>=-1e-6); hh=l0*h[0]+l1*h[1]+l2*h[2]
        H[sl]=np.where(inside,np.maximum(H[sl],hh),H[sl]); cnt[sl]+=inside
    cov=cnt>0
    print(f'slat {s}: mat4 covers {cov.mean():.3f} of the pane footprint; above the pane (h>0) {np.mean(H>0):.3f}; h where covered p5/p50/p95 {np.percentile(H[cov],[5,50,95]).round(1).tolist() if cov.any() else "-"}')
    if cov.any():
        colcov=(cov).mean(axis=0); rowcov=(cov).mean(axis=1)
        print('   coverage across the slat (28 samples):', np.round(colcov[::5],2).tolist())
        print('   coverage along the slat (20 samples):', np.round(rowcov[::max(1,len(rowcov)//20)],2).tolist())
        above=(H>0)
        print('   above-pane across (28 samples):', np.round(above.mean(axis=0)[::5],2).tolist())
