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
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float); N = np.array([[p[6],p[7],p[8]] for p in pts], float)/65536
UV = np.array([[p[4],p[5]] for p in pts], float)/65536
def faces_of(mats): return [f for part in lod0['parts'] for g in part['groups'] if g['material'] in mats for f in g['faces']]
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
cf=faces_of({21}); cc=comps(cf); ff=faces_of({9}); fc=comps(ff)
def centre(faces,c): idx=sorted({i for fi in c for i in faces[fi][:3]}); return P[idx].mean(0), idx
cctr=np.array([centre(cf,c)[0] for c in cc]); fctr=np.array([centre(ff,c)[0] for c in fc])
n=np.array([0.253,0.967,0.0]); n/=np.linalg.norm(n)
# pair each pane with the nearest frame comp
D=np.linalg.norm(cctr[:,None]-fctr[None],axis=-1); j=D.argmin(1)
print('pane->frame centre distance p50/p95', np.percentile(D[np.arange(len(cc)),j],[50,95]).round(1).tolist(), 'distinct frames used', len(set(j)))
# face normal helper
def fnormal(faces,fi):
    a,b,c=[P[i] for i in faces[fi][:3]]; v=np.cross(b-a,c-a); l=np.linalg.norm(v); return v/l if l else v
offs=[]; topfaces=Counter(); rimw=[]
for k in range(len(cc)):
    pc,pidx=centre(cf,cc[k]); fcomp=fc[j[k]]; fidx=sorted({i for fi in fcomp for i in ff[fi][:3]})
    # pane plane: fit normal from its faces
    pn=np.mean([fnormal(cf,fi) for fi in cc[k]],axis=0); pn/=np.linalg.norm(pn)
    d=(P[fidx]-pc)@pn
    offs.append(np.round(d,1))
    # frame faces roughly parallel to the pane and their offset
    for fi in fcomp:
        fn_=fnormal(ff,fi)
        if abs(fn_@pn)>0.99:
            dd=np.mean([(P[i]-pc)@pn for i in ff[fi][:3]]); topfaces[round(dd,1)]+=1
    # rim: extents in the pane's in-plane axes
    e1=np.array([0,0,1.0]); e2=np.cross(pn,e1)
    for e in (e1,e2):
        pe=(P[pidx]-pc)@e; fe=(P[fidx]-pc)@e
        rimw.append((round(fe.max()-pe.max(),1), round(pe.min()-fe.min(),1)))
allo=np.concatenate(offs)
u,c=np.unique(allo,return_counts=True); o=np.argsort(-c)[:12]
print('frame vertex offsets along the pane normal (units, + = above pane), top values:', [(float(u[k]),int(c[k])) for k in o])
print('frame faces parallel to the pane (|n.pn|>0.99) by offset:', sorted(topfaces.items())[:12])
print('rim beyond pane (along z: max,min | across: max,min) first 4 slats:', rimw[:8])
print('pane normal of slat 0', np.round(np.mean([fnormal(cf,fi) for fi in cc[0]],axis=0),3))
# slat rows: distinct z centres
zc=np.unique(np.round(cctr[:,2],-2)); print('pane centre z values:', zc.tolist())
xc=np.sort(cctr[np.abs(cctr[:,2]-cctr[0,2])<500][:,0]); print('one row: n', len(xc), 'x pitch p50', np.median(np.diff(xc)).round(1), 'x range', xc.min(), xc.max())
# pane alpha texture / diffuse: uv range of panes
print('pane UV range', UV[sorted({i for f in cf for i in f[:3]})].min(0).round(3).tolist(), UV[sorted({i for f in cf for i in f[:3]})].max(0).round(3).tolist())
# frame top-face texture: uv range of frame faces parallel to pane
fi_par=[fi for fi in ff and range(len(ff)) if abs(fnormal(ff,fi)@n)>0.99]
print('frame faces parallel to n:', len(fi_par), 'of', len(ff))
# where do the parallel frame faces sit relative to the pane footprint: fraction of their area inside the pane's in-plane rectangle
inside=0; tot=0
for k in range(len(cc)):
    pc,pidx=centre(cf,cc[k]); pn=n; e1=np.array([0,0,1.0]); e2=np.cross(pn,e1)
    pe=(P[pidx]-pc); l1=(pe@e1).max(); l2=(pe@e2).max()
    for fi in fc[j[k]]:
        if abs(fnormal(ff,fi)@pn)>0.99:
            q=np.mean([P[i] for i in ff[fi][:3]],axis=0)-pc; tot+=1
            if abs(q@e1)<=l1 and abs(q@e2)<=l2: inside+=1
print('parallel frame faces with centroid inside the pane footprint:', inside, 'of', tot)
