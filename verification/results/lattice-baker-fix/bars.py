import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import defaultdict, Counter
import numpy as np
import bob1, lod_overlay, lod_atlas, body_materials
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')), lod_overlay.MAX_TRAILING)
lod0 = bob1.lods(tree)[0]; mats = bob1.materials(tree)
pts = lod0['points']; P = np.array([[p[1],p[2],p[3]] for p in pts], float)
def faces_of(mats_): return [f for part in lod0['parts'] for g in part['groups'] if g['material'] in mats_ for f in g['faces']]
def comps(faces, key=lambda i: tuple(P[i].astype(int))):
    parent=list(range(len(faces)))
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
def fnormal(faces,fi):
    a,b,c=[P[i] for i in faces[fi][:3]]; v=np.cross(b-a,c-a); l=np.linalg.norm(v); return v/l if l else v
cf=faces_of({21}); cc=comps(cf); ff=faces_of({9}); fc=comps(ff)
pc=P[sorted({i for fi in cc[0] for i in cf[fi][:3]})].mean(0)
pn=np.mean([fnormal(cf,fi) for fi in cc[0]],axis=0); pn/=np.linalg.norm(pn)
fctr=np.array([P[sorted({i for fi in c for i in ff[fi][:3]})].mean(0) for c in fc]); k=int(np.linalg.norm(fctr-pc,axis=1).argmin()); frame=fc[k]
e1=np.array([0,0,1.0]); e2=np.cross(pn,e1)
pidx=sorted({i for fi in cc[0] for i in cf[fi][:3]}); pe=(P[pidx]-pc); L1=(pe@e1).max(); L2=(pe@e2).max()
print(f'slat 0: pane half-extents along z {L1:.1f}, across {L2:.1f}; frame comp faces {len(frame)}')
par=[fi for fi in frame if abs(fnormal(ff,fi)@pn)>0.99]
off=Counter()
for fi in par: off[round(float(np.mean([(P[i]-pc)@pn for i in ff[fi][:3]])),0)]+=1
print('  parallel faces by offset (units above pane):', sorted(off.items()))
top=[fi for fi in par if np.mean([(P[i]-pc)@pn for i in ff[fi][:3]])>0]
tf=[ff[fi] for fi in top]; tc=comps(tf)
print(f'  top faces {len(top)} in {len(tc)} components')
rows=[]
for c in tc:
    idx=sorted({i for fi in c for i in tf[fi][:3]}); Q=P[idx]-pc; a=Q@e1; b=Q@e2
    rows.append((len(c), a.min(), a.max(), b.min(), b.max()))
rows.sort(key=lambda r:(r[1],r[3]))
for r in rows[:40]: print(f'    faces {r[0]:3d} z {r[1]:8.1f}..{r[2]:8.1f} (w {r[2]-r[1]:6.1f})  across {r[3]:8.1f}..{r[4]:8.1f} (w {r[4]-r[3]:6.1f})')
# bar widths over all top components
w=np.array([[r[2]-r[1], r[4]-r[3]] for r in rows]); print('  top comp extents (z, across) min:', np.min(w,axis=0).round(1).tolist(), 'p50', np.median(w,axis=0).round(1).tolist())
# actual bar width: for each top face its triangle's shortest altitude
alts=[]
for f in tf:
    a,b,c=[P[i] for i in f[:3]]; area=np.linalg.norm(np.cross(b-a,c-a))/2; longest=max(np.linalg.norm(b-a),np.linalg.norm(c-b),np.linalg.norm(a-c)); alts.append(2*area/longest)
print('  top face shortest altitudes p5/p50/p95', np.percentile(alts,[5,50,95]).round(1).tolist())
# top-face area vs pane area
area_top=sum(np.linalg.norm(np.cross(P[f[1]]-P[f[0]],P[f[2]]-P[f[0]]))/2 for f in tf); area_pane=sum(np.linalg.norm(np.cross(P[f[1]]-P[f[0]],P[f[2]]-P[f[0]]))/2 for fi in cc[0] for f in [cf[fi]])
print(f'  top-face area {area_top:.0f} pane area {area_pane:.0f} ratio {area_top/area_pane:.3f}')
# side faces of the frame: heights
side=[fi for fi in frame if abs(fnormal(ff,fi)@pn)<0.3]; print('  side faces', len(side), 'other', len(frame)-len(par)-len(side))
# textures
def tex_stats(name, kind):
    src=lod_atlas.texture_source(assets, name)
    if src is None: return 'none'
    data,k,info=src
    img=lod_atlas.decode_dds(data,0) if k=='dds' else lod_atlas.decode_image(data,name)
    return f'{k} {img.shape} rgb mean {img[...,:3].reshape(-1,3).mean(0).round(1).tolist()} alpha min/mean/max {img[...,3].min()}/{img[...,3].mean():.1f}/{img[...,3].max()}'
for m in (21, 9, 0, 4):
    s=body_materials.slots(mats[m])
    for slot in ('diffuse','alpha','light'):
        nm=s.get(slot)
        if nm and nm!=b'NULL':
            try: print(f'  mat {m} {slot} {nm.decode(errors="replace")[-45:]}: {tex_stats(nm, slot)}')
            except Exception as e: print(f'  mat {m} {slot}: {type(e).__name__} {e}')
