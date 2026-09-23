#!/usr/bin/env python3
"""UV / texture census of one BOB1 LOD record and a texture-atlas plan for it.

Question: can the record be drawn as one material with one diffuse atlas and one light-map
atlas? Per material of the record (default the original coarsest record n-1) it reports
faces, area share, the raw UV bounding box, the share of faces whose three UVs lie in
[0,1] and in [-0.01,1.01], the share whose own UV extent exceeds 1 (tiling), the share
that fits one texture period after an integer shift, the diffuse and light-map texture
(name, size, format, or NULL / NONE_*) and which textures several materials share.

UV storage: POIN point flag 0x02 carries one UV pair as two big-endian i32 16.16
fixed-point values (not float32); flag 0x04 a second pair. The census prints the point
flag histogram and counts points that need duplication in an atlas: points used by faces
of two or more materials, of two or more atlas tiles, and of faces that need different
integer UV shifts.

Atlas plan. Materials with the same (diffuse, light map) pair share one tile. Each face is
moved by an integer (floor of its min u, min v, tolerance 0.01) so its UVs start in the
first period; wrap addressing makes that invisible in the original. A tile covers the union
of the shifted face ranges, clipped to [0, K] periods per axis; faces reaching past K must
be split or clamped. Policies: span (K unbounded: tiling faces get their span, no split)
and period (K = 1: one texture period, every tiling or integer-straddling face is split or
clamped). The tile's full-resolution size is max(diffuse, real light map) per axis times
the covered span. Both atlases share one layout (one UV set per point). Tiles are rounded
up to 4 texels (DXT blocks), a gutter is added per side, and shelf packing (tallest first)
into N x N finds the largest uniform scale <= 1 that fits. Density = atlas texels /
original texels over the covered range (area ratio, 1.0 = original; the linear ratio is
its square root), per texture. A NULL light map (engine 32x32 placeholder) or a NONE_*
map is constant, so it needs no light-map density.

need = linear texels per period at which one texel meets one pixel when the body's
bounding radius (max |position| of the record's points) projects to --screen-px pixels,
sqrt(sum face area / sum face UV area) * px / radius (inferred; ignores foreshortening).
atlas/need >= 1 means the atlas tile is not the limit at that screen size.

Bodies come from the shipped catalogues with every lod_overlay-marked catalogue skipped
(as body_materials.py). Read-only; prints numbers and names only.

  python3 tools/analysis/atlas_census.py ships/argon/argon_TL [--lod N] [--screen-px 50]
"""
import argparse
import math
import sys
from collections import OrderedDict, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
import body_materials  # noqa: E402

EPS = 0.01                 # UV tolerance (texel-edge slack) for "within" tests and shifts
ATLAS_SIZES = (1024, 2048)
POLICIES = (('span', None), ('period', 1))
BLOCK = 4                  # DXT block: tile sides are multiples of 4 texels
UV_ONE = 65536.0


def uv(point):
    """(u, v) in texture periods of a point with flag 0x02, else None."""
    f = point[0]
    if not f & 2:
        return None
    o = 1 + (3 if f & 1 else 0)
    return point[o] / UV_ONE, point[o + 1] / UV_ONE


def face_uvs(points, face):
    out = [uv(points[i]) for i in face[:3]]
    return None if any(x is None for x in out) else out


def uv_area(uvs):
    (a, b), (c, d), (e, f) = uvs
    return 0.5 * abs((c - a) * (f - b) - (e - a) * (d - b))


def shift_of(uvs):
    return (math.floor(min(u for u, _ in uvs) + EPS), math.floor(min(v for _, v in uvs) + EPS))


def texture(assets, name, cache):
    """dict(name, kind: null|stock|real|missing|error|unknown, size, fmt)."""
    if name is None:
        return dict(name='-', kind='none', size=None, fmt=None)
    short = body_materials.short(name)
    if body_materials.is_null(name):
        return dict(name='NULL', kind='null', size=None, fmt=None)
    if assets is None:
        return dict(name=short, kind='stock' if body_materials.is_stock(name) else 'unknown', size=None, fmt=None)
    info = body_materials.texture_info(assets, name, cache)
    kind = info['status'] if info['status'] != 'real' else ('stock' if info.get('stock') else 'real')
    return dict(name=short, kind=kind, size=info.get('size'), fmt=info.get('fmt'))


def census(tree, lod_index=None, assets=None):
    ladder = bob1.lods(tree)
    k = len(ladder) - 1 if lod_index is None else lod_index
    rec = ladder[k]
    pts = rec['points']
    mats = bob1.materials(tree)
    alpha = body_materials.lod_overlay.alpha_materials(mats)
    cache = {}
    flags = defaultdict(int)
    for p in pts:
        flags[p[0]] += 1
    radius = max((math.sqrt(p[1] ** 2 + p[2] ** 2 + p[3] ** 2) for p in pts if p[0] & 1), default=0.0)
    rows = OrderedDict()
    point_mats, point_shifts = defaultdict(set), defaultdict(set)
    total = 0.0
    for part in rec['parts']:
        for g in part['groups']:
            mi = g['material']
            r = rows.setdefault(mi, dict(groups=0, faces=0, area=0.0, uv_area=0.0, no_uv=0, in01=0, in01e=0,
                                         tiling=0, one_period=0, bbox=None, shifted=[]))
            r['groups'] += 1
            for f in g['faces']:
                a = body_materials.face_area(pts, f)
                total += a
                r['faces'] += 1; r['area'] += a
                uvs = face_uvs(pts, f)
                if uvs is None:
                    r['no_uv'] += 1
                    continue
                us, vs = [x for x, _ in uvs], [y for _, y in uvs]
                lo_u, hi_u, lo_v, hi_v = min(us), max(us), min(vs), max(vs)
                b = r['bbox']
                r['bbox'] = (lo_u, hi_u, lo_v, hi_v) if b is None else \
                    (min(b[0], lo_u), max(b[1], hi_u), min(b[2], lo_v), max(b[3], hi_v))
                r['in01'] += lo_u >= 0 and hi_u <= 1 and lo_v >= 0 and hi_v <= 1
                r['in01e'] += lo_u >= -EPS and hi_u <= 1 + EPS and lo_v >= -EPS and hi_v <= 1 + EPS
                r['tiling'] += hi_u - lo_u > 1 or hi_v - lo_v > 1
                su, sv = shift_of(uvs)
                ext = (lo_u - su, hi_u - su, lo_v - sv, hi_v - sv)
                r['one_period'] += ext[1] <= 1 + EPS and ext[3] <= 1 + EPS
                r['uv_area'] += uv_area(uvs)
                r['shifted'].append((ext, a))
                for i in f[:3]:
                    point_mats[i].add(mi)
                    point_shifts[i].add((mi, su, sv))
    diffuse_users, light_users = defaultdict(list), defaultdict(list)
    for mi, r in rows.items():
        m = mats[mi] if 0 <= mi < len(mats) else {}
        s = body_materials.slots(m)
        d = s.get('diffuse', s.get('texture'))
        lm = s.get('light')
        r.update(share=r['area'] / total if total else 0.0, alpha=mi in alpha,
                 diffuse=texture(assets, d, cache), light=texture(assets, lm, cache),
                 key=(d.lower() if d else d, lm.lower() if lm and not body_materials.is_null(lm) else b'NULL'))
        if d and not body_materials.is_null(d):
            diffuse_users[d.lower()].append(mi)
        if lm and not body_materials.is_null(lm):
            light_users[lm.lower()].append(mi)
    for mi, r in rows.items():
        r['diffuse_shared'] = [x for x in diffuse_users.get(r['key'][0], []) if x != mi]
        r['light_shared'] = [x for x in light_users.get(r['key'][1], []) if x != mi]
    point_keys = {i: {rows[mi]['key'] for mi in ms} for i, ms in point_mats.items()}
    return dict(lod=k, lods=len(ladder), value=rec['value'], points=len(pts), point_flags=dict(flags),
                radius=radius, total_area=total, materials=rows,
                shared_points_materials=sum(len(v) > 1 for v in point_mats.values()),
                shared_points_tiles=sum(len(v) > 1 for v in point_keys.values()),
                shift_points=sum(any(len({(s[1], s[2]) for s in v if s[0] == mi}) > 1 for mi in {s[0] for s in v})
                                 for v in point_shifts.values()))


# --- atlas plan ---------------------------------------------------------------------------

def tiles(c, cap=None, screen_px=None):
    """Tiles by (diffuse, light) pair: dict(key, mats, span, base, split, need)."""
    groups = OrderedDict()
    for mi, r in sorted(c['materials'].items(), key=lambda kv: -kv[1]['area']):
        groups.setdefault(r['key'], []).append(mi)
    out = []
    for key, ms in groups.items():
        rs = [c['materials'][mi] for mi in ms]
        lo_u = lo_v = math.inf; hi_u = hi_v = -math.inf
        split = area = uva = uv_faces = 0
        for r in rs:
            area += r['area']; uva += r['uv_area']; uv_faces += len(r['shifted'])
            for ext, _ in r['shifted']:
                if cap is not None and (ext[1] > cap + EPS or ext[3] > cap + EPS):
                    split += 1
                lo_u, hi_u = min(lo_u, ext[0]), max(hi_u, ext[1])
                lo_v, hi_v = min(lo_v, ext[2]), max(hi_v, ext[3])
        if lo_u == math.inf:
            span = (0.0, 0.0)
        else:
            clip = (lambda lo, hi: (max(lo, 0.0), min(hi, cap) if cap is not None else hi))
            (a, b), (e, f) = clip(lo_u, hi_u), clip(lo_v, hi_v)
            span = (max(b - a, 0.0), max(f - e, 0.0))
        d, lm = rs[0]['diffuse'], rs[0]['light']
        sizes = [t['size'] for t in (d, lm) if t['kind'] == 'real' and t['size']]
        base = (max(s[0] for s in sizes), max(s[1] for s in sizes)) if sizes else (32, 32)
        need = None
        if screen_px and c['radius'] and uva > 0:
            need = math.sqrt(area / uva) * screen_px / c['radius']
        out.append(dict(key=key, mats=ms, span=span, base=base, split=split, need=need, uv_faces=uv_faces,
                        diffuse=d, light=lm, faces=sum(r['faces'] for r in rs)))
    return out


def _side(full, scale):
    return max(BLOCK, BLOCK * math.ceil(full * scale / BLOCK)) if full > 0 else 0


def shelf_fits(rects, n):
    """Shelf packing, tallest first, into n x n. rects = [(w, h)] including gutter."""
    x = y = shelf = 0
    for w, h in sorted(rects, key=lambda r: (-r[1], -r[0])):
        if w > n or h > n:
            return False
        if x + w > n:
            y += shelf; x = shelf = 0
        if y + h > n:
            return False
        x += w; shelf = max(shelf, h)
    return True


def plan(tile_list, n, gutter=4):
    """dict(fits_full, scale, rows=[dict(tile, content=(w, h), d_density, l_density, ratio_need)])."""
    # an axis covers at least one original texel (a UV range of zero width still samples one)
    full = [(max(1.0, t['base'][0] * t['span'][0]), max(1.0, t['base'][1] * t['span'][1])) if t['uv_faces']
            else (0, 0) for t in tile_list]

    def rects(s):
        return [(_side(w, s) + 2 * gutter, _side(h, s) + 2 * gutter) for w, h in full if w > 0 and h > 0]

    fits_full = shelf_fits(rects(1.0), n)
    if fits_full:
        scale = 1.0
    else:
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = (lo + hi) / 2
            lo, hi = (mid, hi) if shelf_fits(rects(mid), n) else (lo, mid)
        scale = lo
    rows = []
    for t, (w, h) in zip(tile_list, full):
        cw, ch = _side(w, scale), _side(h, scale)
        dens = {}
        for slot in ('diffuse', 'light'):
            tex = t[slot]
            if tex['kind'] == 'real' and tex['size'] and w > 0 and h > 0:
                orig = max(1.0, tex['size'][0] * t['span'][0]) * max(1.0, tex['size'][1] * t['span'][1])
                dens[slot] = cw * ch / orig
            else:
                dens[slot] = None
        per_period = math.sqrt(cw * ch / (max(t['span'][0], 1 / t['base'][0]) * max(t['span'][1], 1 / t['base'][1]))) \
            if w > 0 and h > 0 else None
        rows.append(dict(tile=t, content=(cw, ch), d_density=dens['diffuse'], l_density=dens['light'],
                         ratio_need=per_period / t['need'] if per_period and t['need'] else None))
    return dict(fits_full=fits_full, scale=scale, rows=rows, n=n,
                split=sum(t['split'] for t in tile_list), faces=sum(t['faces'] for t in tile_list))


# --- output -------------------------------------------------------------------------------

def _tex(t):
    if t['kind'] in ('null', 'none'):
        return t['name']
    size = f'{t["size"][0]}x{t["size"][1]}' if t['size'] else '?'
    return f'{t["name"]} {size} {t["fmt"] or "?"}' + ('' if t['kind'] == 'real' else f' [{t["kind"]}]')


def _rng(vals):
    vals = [v for v in vals if v is not None]
    return f'{min(vals):.3g}..{max(vals):.3g}' if vals else 'n/a'


def format_census(c, origin='', screen_px=None, gutter=4, out=None):
    out = out or sys.stdout
    p = lambda *a: print(*a, file=out)
    uv_pts = sum(v for f, v in c['point_flags'].items() if f & 2)
    uv2 = sum(v for f, v in c['point_flags'].items() if f & 4)
    p(f'{origin}  record LOD{c["lod"]} of {c["lods"]} (value={c["value"]})  materials={len(c["materials"])}'
      f'  faces={sum(r["faces"] for r in c["materials"].values())}  points={c["points"]}')
    p(f'uv storage: point flags {{{", ".join(f"{f:#x}: {n}" for f, n in sorted(c["point_flags"].items()))}}};'
      f' {uv_pts} points carry one UV as 2 x i32 16.16 fixed point (not float32), {uv2} a second UV set')
    p(f'points needing duplication: shared by >= 2 materials {c["shared_points_materials"]},'
      f' by >= 2 atlas tiles {c["shared_points_tiles"]}, by faces of one material with different integer'
      f' UV shifts {c["shift_points"]}')
    p('mat  alpha  faces  groups  area_share  uv_bbox(u0,u1,v0,v1)  in[0,1]  in[-.01,1.01]  tiling(>1)'
      '  one_period  diffuse | light  shared')
    for mi, r in sorted(c['materials'].items(), key=lambda kv: -kv[1]['area']):
        n = r['faces'] - r['no_uv'] or 1
        b = r['bbox']
        bb = f'({b[0]:.2f},{b[1]:.2f},{b[2]:.2f},{b[3]:.2f})' if b else '(no uv)'
        sh = []
        if r['diffuse_shared']:
            sh.append(f'diffuse with {r["diffuse_shared"]}')
        if r['light_shared']:
            sh.append(f'light with {r["light_shared"]}')
        p(f'{mi:3d}  {"A" if r["alpha"] else "-"}  {r["faces"]}  {r["groups"]}  {r["share"]:.3f}  {bb}'
          f'  {r["in01"] / n:.2f}  {r["in01e"] / n:.2f}  {r["tiling"] / n:.2f}  {r["one_period"] / n:.2f}'
          f'  {_tex(r["diffuse"])} | {_tex(r["light"])}  {"; ".join(sh) or "-"}')
    p(f'atlas plan (tiles by (diffuse, light) pair, gutter {gutter} texels/side, {BLOCK}-texel rounding,'
      f' need at screen radius {screen_px} px over body radius {c["radius"]:.4g})')
    for label, cap in POLICIES:
        tl = tiles(c, cap, screen_px)
        for n in ATLAS_SIZES:
            pl = plan(tl, n, gutter)
            rows = pl['rows']
            p(f'  {label:6s} {n}x{n}: tiles={len(tl)} fits_at_full_density={"yes" if pl["fits_full"] else "no"}'
              f' scale={pl["scale"]:.4f} split_or_clamp_faces={pl["split"]}/{pl["faces"]}'
              f' diffuse_density={_rng(r["d_density"] for r in rows)}'
              f' light_density={_rng(r["l_density"] for r in rows)}'
              f' atlas/need={_rng(r["ratio_need"] for r in rows)}')
        p(f'  {label} tiles: mats span(u,v) base -> 2048 content, diffuse/light density, atlas/need, split')
        for r in plan(tl, 2048, gutter)['rows']:
            t = r['tile']
            dd = f'{r["d_density"]:.3g}' if r['d_density'] is not None else '-'
            ld = f'{r["l_density"]:.3g}' if r['l_density'] is not None else 'const'
            rn = f'{r["ratio_need"]:.2f}' if r['ratio_need'] is not None else '-'
            p(f'    {t["mats"]} ({t["span"][0]:.2f},{t["span"][1]:.2f}) {t["base"][0]}x{t["base"][1]}'
              f' -> {r["content"][0]}x{r["content"][1]}  {dd}/{ld}  {rn}  {t["split"]}')


def main(argv=None):
    ap = argparse.ArgumentParser(description='UV/texture census and atlas plan of one BOB1 LOD record.')
    ap.add_argument('body', nargs='+', help='archive body name (ships/argon/argon_TL) or a decoded body file')
    ap.add_argument('--lod', type=int, help='record index (default: the original coarsest record n-1)')
    ap.add_argument('--screen-px', type=float, default=50.0,
                    help='projected body radius in pixels for the "need" estimate (default 50)')
    ap.add_argument('--gutter', type=int, default=4, help='gutter texels per tile side (default 4)')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    a = ap.parse_args(argv)
    assets, skipped = body_materials.original_assets(a.game)
    if skipped:
        print(f'skipped overlay catalogues: {skipped}')
    for name in a.body:
        path = Path(name)
        if path.is_file():
            data, origin = bob1.load(path)
        else:
            entry = bob1.resolve_body(assets, name)
            data, origin = assets.read_entry(entry), f'{entry["source"]}:{entry["path"]}'
        tree = bob1.parse(data)
        n = len(bob1.lods(tree))
        if a.lod is not None and not 0 <= a.lod < n:
            raise SystemExit(f'{name}: --lod {a.lod} outside 0..{n - 1}')
        format_census(census(tree, a.lod, assets), origin, a.screen_px, a.gutter)
    return 0


if __name__ == '__main__':
    sys.exit(main())
