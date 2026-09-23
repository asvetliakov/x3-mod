"""POIN attribute census per LOD record: original (overlay catalogue skipped) vs installed overlay.
Prints per record: point count, point-flag histogram, normal stats (zero, |n| range, distinct),
second-UV share, part flags, groups, faces, 7-int extra records (tangent/binormal) count and
their vector norms. usage: point_attrs.py BODY..."""
import sys, math, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1, lod_overlay
from sector_fog_census import Assets

def rec_stats(tag, i, lod):
    pts = lod['points']; fh = collections.Counter(p[0] for p in pts)
    norms, zero, distinct = [], 0, set()
    for p in pts:
        f = p[0]; vals = p[1:]; o = 0
        if f & 1: o += 3
        if f & 2: o += 2 + (2 if f & 4 else 0)
        if f & 8:
            n = vals[o:o + 3]; m = math.sqrt(sum(x * x for x in n)) / 65536.0
            norms.append(m); distinct.add(n); zero += (m == 0)
    parts = lod['parts']; groups = sum(len(p['groups']) for p in parts)
    faces = sum(len(g['faces']) for p in parts for g in p['groups'])
    extra = [e for p in parts for g in p['groups'] for e in g.get('extra', [])]
    en = [math.sqrt(sum(x * x for x in e[1:4])) / 65536.0 for e in extra]
    eb = [math.sqrt(sum(x * x for x in e[4:7])) / 65536.0 for e in extra]
    eidx = {e[0] for e in extra}
    # faces referencing points: which points are referenced by faces
    used = {v for p in parts for g in p['groups'] for fc in g['faces'] for v in fc[:3]}
    print(f"{tag} rec{i} value={lod['value']} points={len(pts)} used={len(used)} flags={dict(fh)} "
          f"normals={len(norms)} zero={zero} |n|=[{min(norms, default=0):.3f},{max(norms, default=0):.3f}] distinct={len(distinct)} "
          f"parts={[hex(p['flags']) for p in parts]} groups={groups} faces={faces} "
          f"extra={len(extra)} extra_pts={len(eidx)} extra_in_used={len(eidx & used)} "
          f"|t|=[{min(en, default=0):.3f},{max(en, default=0):.3f}] |b|=[{min(eb, default=0):.3f},{max(eb, default=0):.3f}]")

game = bob1.DEFAULT_GAME
orig, skipped = lod_overlay.original_assets(game)
inst = Assets(Path(game))
for name in sys.argv[1:]:
    for tag, assets in (('orig', orig), ('inst', inst)):
        e = bob1.resolve_body(assets, name); tree = bob1.parse(assets.read_entry(e))
        L = bob1.lods(tree)
        print(f"== {name} {tag} {e['source']}:{e['path']} records={len(L)}")
        for i, lod in enumerate(L): rec_stats(tag, i, lod)
