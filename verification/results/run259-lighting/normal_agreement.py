"""Per LOD record: agreement between each face's geometric normal (cross of edges, file winding) and its three
point normals; handedness of the per-point tangent frame (sign of dot(cross(N,T),B)); and N.T orthogonality.
Prints shares so records can be compared (a flipped or scrambled normal set shows as a different share).
usage: normal_agreement.py BODY [orig|inst]"""
import sys, math, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1, lod_overlay
from sector_fog_census import Assets
name, src = sys.argv[1], sys.argv[2]
assets = lod_overlay.original_assets(bob1.DEFAULT_GAME)[0] if src == 'orig' else Assets(Path(bob1.DEFAULT_GAME))
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)))
def sub(a, b): return [x - y for x, y in zip(a, b)]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def unit(v): m = math.sqrt(dot(v, v)); return [x / m for x in v] if m else None
for li, lod in enumerate(bob1.lods(tree)):
    pts = lod['points']
    P = [p[1:4] for p in pts]
    N = []
    for p in pts:
        f = p[0]; o = 1 + 3 + ((4 if f & 4 else 2) if f & 2 else 0); N.append(unit(p[o:o + 3]))
    agree = dis = degen = 0; hand = collections.Counter(); area_agree = area_all = 0.0
    tframe = {}
    for part in lod['parts']:
        for g in part['groups']:
            for e in g.get('extra', []): tframe[e[0]] = (unit(e[1:4]), unit(e[4:7]))
            for a, b, c, _ in g['faces']:
                fn = cross(sub(P[b], P[a]), sub(P[c], P[a])); ar = math.sqrt(dot(fn, fn)); u = unit(fn)
                if u is None: degen += 1; continue
                s = sum(dot(u, N[v]) for v in (a, b, c) if N[v]) / 3
                area_all += ar
                if s > 0: agree += 1; area_agree += ar
                else: dis += 1
    for i, (t, b) in tframe.items():
        if t is None or b is None or N[i] is None: hand['zero'] += 1; continue
        hand['+' if dot(cross(N[i], t), b) > 0 else '-'] += 1
    n = agree + dis
    print(f"{src} {name} rec{li} faces={n + degen} degen={degen} face.n>0 share={agree / max(n, 1):.3f} area-weighted={area_agree / max(area_all, 1e-9):.3f} handedness={dict(hand)}")
