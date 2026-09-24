# Record 2 (vanilla T=150) as a coarse source: its pane plate, what it loses against record 0, and which materials poke
# above the plate (share of the plate footprint with any face of that material above y = 0 / 105 / 140).
import sys; sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import Counter, defaultdict
import numpy as np, bob1, lod_overlay
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')), lod_overlay.MAX_TRAILING)
lods = bob1.lods(tree)
def counts(l):
    c = Counter()
    for p in l['parts']:
        for g in p['groups']: c[g['material']] += len(g['faces'])
    return c
c0, c2 = counts(lods[0]), counts(lods[2])
print('faces record 0 -> 2 per material:', {m: (c0[m], c2.get(m, 0)) for m in sorted(c0)})
l = lods[2]; P = np.array([[p[1], p[2], p[3]] for p in l['points']], float); UV = np.array([[p[4], p[5]] for p in l['points']], float)/65536
faces = [(g['material'], f) for p in l['parts'] for g in p['groups'] for f in g['faces']]
pane = [f for m, f in faces if m == 21]
def comps(fs):
    key = lambda i: tuple(P[i].astype(int)); parent = list(range(len(fs)))
    def find(a):
        while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
        return a
    edges = defaultdict(list)
    for fi, f in enumerate(fs):
        ks = [key(i) for i in f[:3]]
        for a, b in ((0, 1), (1, 2), (0, 2)): edges[tuple(sorted((ks[a], ks[b])))].append(fi)
    for v in edges.values():
        for x in v[1:]:
            ra, rb = find(v[0]), find(x)
            if ra != rb: parent[rb] = ra
    out = defaultdict(list)
    for fi in range(len(fs)): out[find(fi)].append(fi)
    return list(out.values())
pc = comps(pane)
print('record 2 pane: faces', len(pane), 'components', len(pc), 'faces/comp', Counter(len(c) for c in pc).most_common(3))
for c in pc[:3]:
    idx = sorted({i for fi in c for i in pane[fi][:3]}); Q = P[idx]
    print('  comp extents x', Q[:, 0].min(), Q[:, 0].max(), 'y', Q[:, 1].min(), Q[:, 1].max(), 'z', Q[:, 2].min(), Q[:, 2].max(), 'uv', UV[idx].min(0).round(2).tolist(), UV[idx].max(0).round(2).tolist())
# poke-through map: raster the plate footprint (x, z) at 25 units; per material, max y over the footprint
idx = sorted({i for f in pane for i in f[:3]}); Q = P[idx]
x0, x1, z0, z1 = Q[:, 0].min(), Q[:, 0].max(), Q[:, 2].min(), Q[:, 2].max(); res = 25.0
xs = np.arange(x0, x1, res); zs = np.arange(z0, z1, res); X, Z = np.meshgrid(xs, zs, indexing='ij')
plate = np.zeros(X.shape, bool)
def raster(fs, H, mark=None):
    for f in fs:
        tri = P[list(f[:3])]; u, v, h = tri[:, 0], tri[:, 2], tri[:, 1]
        if u.max() < x0 or u.min() > x1 or v.max() < z0 or v.min() > z1: continue
        sl = (slice(max(0, int((u.min()-x0)//res)), min(len(xs), int((u.max()-x0)//res)+2)), slice(max(0, int((v.min()-z0)//res)), min(len(zs), int((v.max()-z0)//res)+2)))
        xx, zz = X[sl], Z[sl]; d = (u[1]-u[0])*(v[2]-v[0])-(u[2]-u[0])*(v[1]-v[0])
        if abs(d) < 1e-9: continue
        l1 = ((v[2]-v[0])*(xx-u[0])-(u[2]-u[0])*(zz-v[0]))/d; l2 = (-(v[1]-v[0])*(xx-u[0])+(u[1]-u[0])*(zz-v[0]))/d; l0 = 1-l1-l2
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6); hh = l0*h[0]+l1*h[1]+l2*h[2]
        if H is not None: H[sl] = np.where(inside, np.maximum(H[sl], hh), H[sl])
        if mark is not None: mark[sl] |= inside
raster(pane, None, plate)
print(f'plate footprint cells {plate.sum()} of {plate.size} ({plate.mean():.3f}), {res}-unit cells')
for m in sorted(c2):
    if m == 21: continue
    H = np.full(X.shape, -1e9); raster([f for mm, f in faces if mm == m], H)
    on = plate & (H > -1e8)
    if on.any():
        print(f'  mat {m:2d}: covers {on.sum()/plate.sum():.3f} of the plate; above y=0 {np.mean((H>0)&plate)/plate.mean():.3f}, above 105 {np.mean((H>105)&plate)/plate.mean():.3f}, above 140 {np.mean((H>140)&plate)/plate.mean():.3f}, above 220 {np.mean((H>220)&plate)/plate.mean():.3f}; max y on plate {H[plate].max():.0f}')
