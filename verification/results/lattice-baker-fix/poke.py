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
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float)
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
cf=faces_of({21}); cc=comps(cf); ff=faces_of({9}); fc=comps(ff)
pidx=sorted({i for fi in cc[0] for i in cf[fi][:3]}); pc=P[pidx].mean(0)
pn=np.mean([fnormal(cf[fi]) for fi in cc[0]],axis=0); pn/=np.linalg.norm(pn)
fctr=np.array([P[sorted({i for fi in c for i in ff[fi][:3]})].mean(0) for c in fc]); k=int(np.linalg.norm(fctr-pc,axis=1).argmin()); frame=[ff[fi] for fi in fc[k]]
e1=np.array([0,0,1.0]); e2=np.cross(pn,e1); e2/=np.linalg.norm(e2)
ang=Counter(); 
for f in frame: ang[int(np.degrees(np.arccos(min(1,abs(fnormal(f)@pn)))))//5*5]+=1
print('frame face angle to pane normal (deg, 5-deg bins):', sorted(ang.items()))
# height map of the frame surface over the pane (max height along pn of any frame face covering the point)
pe=P[pidx]-pc; L1=(pe@e1).max(); L2=(pe@e2).max()
res=10.0; zs=np.arange(-L1,L1,res); xs=np.arange(-L2,L2,res); Z,X=np.meshgrid(zs,xs,indexing='ij')
H=np.full(Z.shape,-1e9); cnt=np.zeros(Z.shape,int)
for f in frame:
    tri=P[list(f[:3])]-pc; u=tri@e1; v=tri@e2; h=tri@pn
    zmin,zmax=u.min(),u.max(); xmin,xmax=v.min(),v.max()
    sl=(slice(max(0,int((zmin+L1)//res)),min(len(zs),int((zmax+L1)//res)+2)), slice(max(0,int((xmin+L2)//res)),min(len(xs),int((xmax+L2)//res)+2)))
    zz=Z[sl]; xx=X[sl]
    d=(u[1]-u[0])*(v[2]-v[0])-(u[2]-u[0])*(v[1]-v[0])
    if abs(d)<1e-9: continue
    l1=((v[2]-v[0])*(zz-u[0])-(u[2]-u[0])*(xx-v[0]))/d; l2=(-(v[1]-v[0])*(zz-u[0])+(u[1]-u[0])*(xx-v[0]))/d; l0=1-l1-l2
    inside=(l0>=-1e-6)&(l1>=-1e-6)&(l2>=-1e-6)
    hh=l0*h[0]+l1*h[1]+l2*h[2]
    H[sl]=np.where(inside, np.maximum(H[sl],hh), H[sl]); cnt[sl]+=inside
cov=cnt>0
print(f'pane grid {Z.shape} cells of {res} units; frame covers {cov.mean():.3f} of the pane footprint')
h=H[cov]; print('frame surface height above pane where covered: p1/p5/p25/p50/p75/p95/p99', np.percentile(h,[1,5,25,50,75,95,99]).round(2).tolist())
print('share of pane area with frame above the pane (h>0):', np.mean(H>0).round(3), ' h>0.5:', np.mean(H>0.5).round(3), ' |h|<0.5:', np.mean((cov)&(np.abs(H)<0.5)).round(3))
# profile across the slat (mean over z of the height), and along
prof=np.where(cov,H,np.nan)
across=np.nanmean(prof,axis=0); along=np.nanmean(prof,axis=1)
print('height profile across the slat (x from -L2..L2, every 5th sample):', np.round(across[::5],1).tolist())
print('height profile along the slat (z, 20 samples):', np.round(along[::max(1,len(along)//20)],1).tolist())
# where is h>0: columns across
print('columns (across) with any h>0 fraction:', np.round((H>0).mean(axis=0)[::5],2).tolist())
