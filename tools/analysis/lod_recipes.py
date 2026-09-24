"""Per-body baker recipes for the merged-LOD overlay (lod_overlay.py --batch; design
docs/architecture/lattice-baker-fix.md).

A recipe names the vanilla record the coarse record C is built from (source_record) and geometry ops
applied to that record before the alpha split and the atlas collapse. Its `expect` block is a geometric
self-check: a body of the same stem whose geometry does not match (a mod's replacement body, a future
asset change) is baked plainly (source record 0, no op) and the census row carries recipe_skipped.

Hashing: the recipe and this module's source enter the inputs_sha256 of the bodies that apply a recipe
(lod_batch_census.census_body), and this module is not in lod_overlay.TOOL_FILES, so a recipe-only change
rebuilds the bodies with a recipe under --sync and reuses every other body.

weld_strips (the Terran solar-plant louvres): every connected component of the material's faces is a planar
strip tilted about the named horizontal axis; each strip is rotated flat about that axis, stretched or
narrowed across to the neighbour pitch so adjacent strips share their edge coordinate exactly (the midpoint
of the two strip centres, rounded once), and set to y = plane_y. Point normals, UVs, flags, the 7-int
tangent records and the faces are kept. The part bounds are kept; the welded strips must stay inside the
strips' old bounding box, so the stored bounds (boxes) still enclose them.
"""
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np

RECIPES = {
    'stations/x3tc/terran_spp_panel': dict(
        source_record=1,
        ops=[('weld_strips', dict(material=21, plane_y=105, axis='z'))],
        expect=dict(strips=132, strip_size=(7851, 1372), pitch=1222.6, tilt_deg=14.6, tol=0.02)),
}
AXES = {'x': 0, 'z': 2}
ACROSS = {'x': 2, 'z': 0}


class RecipeMismatch(ValueError):
    pass


def lookup(name):
    """(key, recipe) for a body name ('stations/x3tc/terran_spp_panel', any case, with or without
    objects/ and extension), else None."""
    key = name.replace('\\', '/').lower()
    if key.startswith('objects/'):
        key = key[len('objects/'):]
    for ext in ('.pbb', '.bob', '.pbd', '.bod'):
        if key.endswith(ext):
            key = key[:-len(ext)]
    recipe = RECIPES.get(key)
    return (key, recipe) if recipe is not None else None


def digest(key, recipe):
    """sha256 over the recipe and this module's source (folded into the body's inputs_sha256)."""
    h = hashlib.sha256(json.dumps([key, recipe], sort_keys=True).encode())
    h.update(Path(__file__).read_bytes())
    return h.hexdigest()


def material_faces(record, material):
    return [f for p in record['parts'] for g in p['groups'] if g['material'] == material for f in g['faces']]


def strips(record, material, axis):
    """Connected components of the material's faces (shared point index or identical position), each
    as dict(idx, lo, hi, centre, long, across, thickness, tilt_deg, axis_dev_deg)."""
    faces = material_faces(record, material)
    pts = record['points']
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    by_pos = {}
    for f in faces:
        for i in f[:3]:
            j = by_pos.setdefault(tuple(pts[i][1:4]), i)
            ra, rb = find(j), find(i)
            if ra != rb:
                parent[rb] = ra
        for i in f[1:3]:
            ra, rb = find(f[0]), find(i)
            if ra != rb:
                parent[rb] = ra
    comps = defaultdict(set)
    for f in faces:
        for i in f[:3]:
            comps[find(i)].add(i)
    ax, ac = AXES[axis], ACROSS[axis]
    out = []
    for idx in comps.values():
        idx = sorted(idx)
        Q = np.array([pts[i][1:4] for i in idx], float)
        mean = Q.mean(0)
        _, _, vt = np.linalg.svd(Q - mean)                  # a face component has >= 3 points: vt is 3 x 3
        e = (Q - mean) @ vt.T
        ext = e.max(0) - e.min(0)
        n = vt[2] * (1 if vt[2][1] >= 0 else -1)
        lo, hi = Q.min(0), Q.max(0)
        out.append(dict(idx=idx, lo=lo, hi=hi, centre=(lo + hi) / 2, long=float(ext[0]), across=float(ext[1]),
                        thickness=float(ext[2]), tilt_deg=math.degrees(math.acos(min(1.0, abs(n[1])))),
                        axis_dev_deg=math.degrees(math.acos(min(1.0, abs(vt[0][ax])))), ac=ac, ax=ax))
    out.sort(key=lambda s: (round(s['centre'][ax]), s['centre'][ac]))
    return out


def neighbours(ss, pitch, tol):
    """right[i] = j for strips whose across-centre distance is within tol of the pitch and whose long
    intervals overlap by more than half the shorter one; raises RecipeMismatch on an ambiguous pairing."""
    right, left = {}, {}
    for i, a in enumerate(ss):
        best = None
        for j, b in enumerate(ss):
            d = b['centre'][a['ac']] - a['centre'][a['ac']]
            if abs(d - pitch) > tol * pitch:
                continue
            ov = min(a['hi'][a['ax']], b['hi'][a['ax']]) - max(a['lo'][a['ax']], b['lo'][a['ax']])
            if ov <= 0.5 * min(a['long'], b['long']):
                continue
            if best is None or abs(d - pitch) < abs(best[0] - pitch):
                best = (d, j)
        if best is not None:
            if best[1] in left:
                raise RecipeMismatch(f'strips {left[best[1]]} and {i} both abut strip {best[1]}')
            right[i], left[best[1]] = best[1], i
    return right, left


def check(ss, record, material, expect):
    """Raise RecipeMismatch unless the strips match expect; returns (right, left, measured pitch)."""
    tol = expect['tol']
    n = expect['strips']
    if len(ss) != n:
        raise RecipeMismatch(f'material {material}: {len(ss)} strips, expected {n}')
    L, W = expect['strip_size']
    T = expect['tilt_deg']
    bad = [k for k, s in enumerate(ss)
           if abs(s['across'] - W) > tol * W or s['thickness'] > max(2.0, tol * W)
           or abs(s['tilt_deg'] - T) > tol * max(T, 1.0) or math.sin(math.radians(s['axis_dev_deg'])) > tol]
    if bad:
        s = ss[bad[0]]
        raise RecipeMismatch(f'material {material}: {len(bad)} strips off the expected shape (first: across'
                             f' {s["across"]:.1f} of {W}, thickness {s["thickness"]:.1f}, tilt {s["tilt_deg"]:.2f}'
                             f' of {T} deg, axis deviation {s["axis_dev_deg"]:.2f} deg)')
    longest = max(s['long'] for s in ss)
    if abs(longest - L) > tol * L:
        raise RecipeMismatch(f'material {material}: longest strip {longest:.1f}, expected {L}')
    others = {i for p in record['parts'] for g in p['groups'] if g['material'] != material
              for f in g['faces'] for i in f[:3]}
    shared = sum(1 for s in ss for i in s['idx'] if i in others)
    if shared:
        raise RecipeMismatch(f'material {material}: {shared} strip points shared with other materials')
    right, left = neighbours(ss, expect['pitch'], tol)
    alone = [k for k in range(len(ss)) if k not in right and k not in left]
    if alone:
        raise RecipeMismatch(f'material {material}: {len(alone)} strips without a neighbour at the pitch')
    ac = ss[0]['ac']
    d = [ss[j]['centre'][ac] - ss[i]['centre'][ac] for i, j in right.items()]
    pitch = float(np.median(d))
    if abs(pitch - expect['pitch']) > tol * expect['pitch']:
        raise RecipeMismatch(f'material {material}: pitch {pitch:.1f}, expected {expect["pitch"]}')
    return right, left, pitch


def inside_old_box(record, points, idx):
    """The moved points stay inside the bounding box of the same points before the op: the stored part
    bounds (an AABB and an L-inf radius about a pivot, both boxes) then still enclose them."""
    old = np.array([record['points'][i][1:4] for i in idx], float)
    new = np.array([points[i][1:4] for i in idx], float)
    return bool((new.min(0) >= old.min(0)).all() and (new.max(0) <= old.max(0)).all())


def weld_strips(record, material, plane_y, axis, expect):
    """(new record, report): the strips flattened about the axis, abutting at the pitch, at y = plane_y."""
    ss = strips(record, material, axis)
    right, left, pitch = check(ss, record, material, expect)
    ac = ss[0]['ac']
    pts = list(record['points'])
    widths, scales = [], []
    for k, s in enumerate(ss):
        c = s['centre'][ac]
        bl = round((ss[left[k]]['centre'][ac] + c) / 2) if k in left else round(c - pitch / 2)
        br = round((ss[right[k]]['centre'][ac] + c) / 2) if k in right else round(c + pitch / 2)
        lo, hi = int(s['lo'][ac]), int(s['hi'][ac])
        if hi <= lo or br <= bl:
            raise RecipeMismatch(f'material {material}: degenerate strip {k}')
        for i in s['idx']:
            p = list(pts[i])
            v = p[1 + ac]
            p[1 + ac] = bl if v == lo else br if v == hi else int(round(bl + (v - lo) * (br - bl) / (hi - lo)))
            p[2] = int(plane_y)
            pts[i] = tuple(p)
        widths.append(br - bl)
        scales.append((br - bl) / s['across'])
    if not inside_old_box(record, pts, [i for s in ss for i in s['idx']]):
        raise RecipeMismatch(f'material {material}: the welded strips leave the strips\' old bounding box')
    new = dict(record, points=pts)
    return new, dict(op='weld_strips', material=material, plane_y=int(plane_y), strips=len(ss),
                     pitch=round(pitch, 2), abutting=len(right), width_min=min(widths), width_max=max(widths),
                     across_scale=round(float(np.mean(scales)), 4),
                     points=sum(len(s['idx']) for s in ss))


OPS = {'weld_strips': weld_strips}


def above_share(record, material, res=20.0, eps=0.5):
    """Poke-through check (not called by the baker; tests and verification/results/lattice-baker-fix/): a height
    map over the xz footprint of the material's faces (cell size res) against every other face of the record,
    rasterised with their edges splatted so vertical walls count. Returns dict(cells, above, share, max_above):
    footprint cells where another face lies more than eps above the material's own surface."""
    P = np.array([p[1:4] for p in record['points']], float)
    own = material_faces(record, material)
    other = [f for p in record['parts'] for g in p['groups'] if g['material'] != material for f in g['faces']]
    idx = sorted({i for f in own for i in f[:3]})
    lo = P[idx][:, [0, 2]].min(0) - res
    shape = tuple((np.ceil((P[idx][:, [0, 2]].max(0) + res - lo) / res)).astype(int) + 1)
    h_own, h_oth = np.full(shape, -np.inf), np.full(shape, -np.inf)

    def raster(f, H):
        tri = P[list(f[:3])]
        u, v, h = (tri[:, 0] - lo[0]) / res - 0.5, (tri[:, 2] - lo[1]) / res - 0.5, tri[:, 1]
        i0, i1 = max(0, int(np.ceil(u.min()))), min(shape[0] - 1, int(np.floor(u.max())))
        j0, j1 = max(0, int(np.ceil(v.min()))), min(shape[1] - 1, int(np.floor(v.max())))
        if i0 > i1 or j0 > j1:
            return
        d = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0])
        if abs(d) < 1e-9:
            return
        I, J = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1), indexing='ij')
        l1 = ((v[2] - v[0]) * (I - u[0]) - (u[2] - u[0]) * (J - v[0])) / d
        l2 = (-(v[1] - v[0]) * (I - u[0]) + (u[1] - u[0]) * (J - v[0])) / d
        l0 = 1 - l1 - l2
        inside = (l0 >= -1e-9) & (l1 >= -1e-9) & (l2 >= -1e-9)
        hh = l0 * h[0] + l1 * h[1] + l2 * h[2]
        sub = H[i0:i1 + 1, j0:j1 + 1]
        np.maximum(sub, np.where(inside, hh, -np.inf), out=sub)

    def splat(f, H):
        tri = P[list(f[:3])]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            n = int(np.ceil(np.hypot(*(tri[b, [0, 2]] - tri[a, [0, 2]])) / (res / 2))) + 2
            t = np.linspace(0, 1, n)[:, None]
            q = tri[a] + t * (tri[b] - tri[a])
            i = np.floor((q[:, 0] - lo[0]) / res).astype(int)
            j = np.floor((q[:, 2] - lo[1]) / res).astype(int)
            ok = (i >= 0) & (i < shape[0]) & (j >= 0) & (j < shape[1])
            np.maximum.at(H, (i[ok], j[ok]), q[ok, 1])

    for f in own:
        raster(f, h_own)
    for f in other:
        raster(f, h_oth)
        splat(f, h_oth)
    foot = np.isfinite(h_own)
    both = foot & np.isfinite(h_oth)
    diff = np.full(shape, -np.inf)
    diff[both] = h_oth[both] - h_own[both]
    above = int((diff > eps).sum())
    return dict(cells=int(foot.sum()), above=above, share=above / max(1, int(foot.sum())),
                max_above=float(diff.max()) if np.isfinite(diff.max()) else None)


def prepare(key, recipe, ladder):
    """(source record index, record with the ops applied, report) for a recipe on a body's ladder; raises
    RecipeMismatch when the body does not match (the caller then bakes plainly)."""
    n = recipe.get('source_record', 0)
    if not 0 <= n < len(ladder):
        raise RecipeMismatch(f'source record {n} outside the body\'s records 0..{len(ladder) - 1}')
    record, reports = ladder[n], []
    for op, args in recipe.get('ops', ()):
        record, rep = OPS[op](record, expect=recipe['expect'], **args)
        reports.append(rep)
    return n, record, dict(recipe=key, source_record=n, ops=reports)
