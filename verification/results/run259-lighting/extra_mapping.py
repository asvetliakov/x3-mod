"""Per group of a LOD record: do the 7-int extra records (point index + tangent + binormal) cover exactly the
points the group's faces reference, in which order, and do tangent/binormal look orthogonal to the point normal?
usage: extra_mapping.py BODY RECORD [orig|inst]"""
import sys, math
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1, lod_overlay
from sector_fog_census import Assets
name, rec, src = sys.argv[1], int(sys.argv[2]), sys.argv[3]
assets = lod_overlay.original_assets(bob1.DEFAULT_GAME)[0] if src == 'orig' else Assets(Path(bob1.DEFAULT_GAME))
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name))); lod = bob1.lods(tree)[rec]
pts = lod['points']
def normal(p):
    f = p[0]; o = 1 + (3 if f & 1 else 0) + ((4 if f & 4 else 2) if f & 2 else 0)
    return p[o:o + 3]
def unit(v): m = math.sqrt(sum(x * x for x in v)); return [x / m for x in v] if m else [0, 0, 0]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
mats = bob1.materials(tree)
for pi, part in enumerate(lod['parts']):
    for gi, g in enumerate(part['groups']):
        used_order = []; seen = set()
        for fc in g['faces']:
            for v in fc[:3]:
                if v not in seen: seen.add(v); used_order.append(v)
        ex = g.get('extra', []); eidx = [e[0] for e in ex]
        zt = sum(1 for e in ex if not any(e[1:4])); zb = sum(1 for e in ex if not any(e[4:7]))
        dn = [abs(dot(unit(normal(pts[e[0]])), unit(e[1:4]))) for e in ex if any(e[1:4])]
        db = [abs(dot(unit(normal(pts[e[0]])), unit(e[4:7]))) for e in ex if any(e[4:7])]
        print(f"part{pi} g{gi} mat={g['material']} faces={len(g['faces'])} used={len(seen)} extra={len(ex)} "
              f"set_eq={set(eidx) == seen} order_eq_first_use={eidx == used_order} sorted={eidx == sorted(eidx)} "
              f"contiguous={eidx == list(range(min(eidx), max(eidx) + 1)) if eidx else '-'} range=[{min(eidx, default='-')},{max(eidx, default='-')}] "
              f"zero_t={zt} zero_b={zb} |n.t|max={max(dn, default=0):.3f} mean={sum(dn) / max(len(dn), 1):.3f} |n.b|max={max(db, default=0):.3f}")
