#!/usr/bin/env python3
"""How often a straight in-sector line passes within clearance of a station or asteroid, and whether a one-waypoint
tangent detour around the first blocker is itself blocked (the case where a route planner would differ from a local
detour). Input for docs/architecture/sector-path-planning.md.

Lines (per sector, stock AP universe map `addon/02.cat`): gate->gate pairs (transit), gate->every station (arrival),
station->station pairs (trade runs). The endpoints' own stations are not obstacles for their line.
Station models: (a) the engine bounding sphere [obj+0xa4] (scene_radius.radii, the broad-phase radius) and (b) the
"body": one sphere per scene part, radius = the part body's LOD-0 L-inf half-extent, centre = the part's accumulated
local position rotated by the map's a/b/g angles (yaw about Y, pitch about X, roll about Z, 65536 = one turn; the
engine's angle-to-matrix order is not traced, so a no-rotation variant is reported as a sensitivity check).
(c) the "box": one axis-aligned box per part in station space from the LOD-0 vertex extents of the part body (bob1 points,
normalised to max |coord| = 65536, times the record value; part rotation inside the scene is not modelled), rotated by the
station's a/b/g angles, i.e. an oriented box in world space. It is evaluated only for line/station pairs the sphere blocks
(a box inside the sphere cannot block otherwise), by sampling the sub-segment inside the sphere at <= 50 m steps.
Asteroids: one sphere each, radius = the root body value (TAsteroids col 0); stock and installed (Mayhem 4) maps.
Clearance C is added to every obstacle radius. A line is "blocked" when its segment passes within C of a model.
Detour: the deepest blocker gets one waypoint T = sphere centre + m*1.05*(rho + C), with m the unit vector from the centre
toward the line's foot point (the near side). "chained_same" = P->T or T->Q is still blocked by another part of the same
station; "chained_other" = by a different station. A recursive tangent-insertion planner (the same step applied to each
blocked sub-leg, depth <= 6) gives the waypoint count a local one-step detour would need to grow into, and its extra path
length; "fail" = still blocked at depth 6. The "typical" variant drops station types with a bounding sphere above 15 km
(Aldrin bases 52 km, Earth Torus 37 km, Military Base 30 km, Orbital Defence Station 18 km) as obstacles and endpoints.
Derived numbers only; all positions in engine units (500 per metre). Output: line_blockage.txt and .json beside this file.
"""
import itertools, json, math, statistics, sys, xml.etree.ElementTree as ET
from pathlib import Path
HERE = Path(__file__).resolve().parent
GS = HERE.parent / 'game-scale'
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(GS))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES
import scene_radius as sr, map_stats as ms, bob1

U = 500.0
CLEAR_M = (100, 250, 500, 1000)


def rot(a, b, g):
    """Rotation matrix from the map's binary angles: yaw a about Y, pitch b about X, roll g about Z (R = Ry*Rx*Rz)."""
    ca, sa = math.cos(a / 65536 * 2 * math.pi), math.sin(a / 65536 * 2 * math.pi)
    cb, sb = math.cos(b / 65536 * 2 * math.pi), math.sin(b / 65536 * 2 * math.pi)
    cg, sg = math.cos(g / 65536 * 2 * math.pi), math.sin(g / 65536 * 2 * math.pi)
    ry = ((ca, 0, sa), (0, 1, 0), (-sa, 0, ca)); rx = ((1, 0, 0), (0, cb, -sb), (0, sb, cb)); rz = ((cg, -sg, 0), (sg, cg, 0), (0, 0, 1))
    def mul(m, n): return tuple(tuple(sum(m[i][k] * n[k][j] for k in range(3)) for j in range(3)) for i in range(3))
    return mul(mul(ry, rx), rz)


def apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


_AABB = {}


def body_aabb(assets, name):
    """(centre, half-extents) of the LOD-0 vertex cloud in engine units, body space; None when unreadable."""
    if not name: return None
    key = name.lower()
    if key in _AABB: return _AABB[key]
    out = None
    try:
        ref = ('v/%05d' % int(name)) if name.isdigit() else name
        data = assets.read_entry(bob1.resolve_body(assets, ref))
        tree = bob1.parse(data)
        lods = next(v for t, v in tree['sections'] if t == 'BODY')
        lod = lods[0]
        pts = [(q[1], q[2], q[3]) for q in lod['points'] if q[0] & 1]
        if pts and lod['value'] > 0:
            sc = lod['value'] / 65536.0
            lo = [min(q[i] for q in pts) * sc for i in range(3)]; hi = [max(q[i] for q in pts) * sc for i in range(3)]
            out = (tuple((lo[i] + hi[i]) / 2 for i in range(3)), tuple((hi[i] - lo[i]) / 2 for i in range(3)))
    except Exception:
        out = None
    _AABB[key] = out
    return out


def part_boxes(assets, scene):
    """[(local centre, half-extents)] per part with a readable body, station space, part rotation ignored."""
    text, _ = sr.scene_text(assets, scene)
    if text is None: return []
    ps = sr.parts(text)
    by = {p['idx']: p for p in ps}
    out = []
    for p in ps:
        pos, q, depth = (0, 0, 0), p, 0
        while q is not None and depth < 32:
            pos = tuple(pos[i] + q['pos'][i] for i in range(3))
            q = by.get(q['parent']); depth += 1
        bb = body_aabb(assets, p['body'])
        if bb: out.append((tuple(pos[i] + bb[0][i] for i in range(3)), bb[1]))
    return out


def transpose(m): return tuple(tuple(m[j][i] for j in range(3)) for i in range(3))


def box_blocked(p, q, st, clear):
    """segment pq within clear of any part box of station st (world), sampled inside the station sphere + clear"""
    c, R = st['pos'], st['rbs'] + clear
    d = tuple(q[i] - p[i] for i in range(3)); L2 = sum(x * x for x in d)
    if L2 == 0: return False
    L = math.sqrt(L2); u = tuple(x / L for x in d)
    w = tuple(p[i] - c[i] for i in range(3)); b = sum(w[i] * u[i] for i in range(3)); cc = sum(x * x for x in w) - R * R
    disc = b * b - cc
    if disc < 0: return False
    s0, s1 = max(0.0, -b - math.sqrt(disc)), min(L, -b + math.sqrt(disc))
    if s1 < s0: return False
    step = max(10 * U, min(clear / 2, 50 * U)); n = min(4000, int((s1 - s0) / step) + 1)
    Rt = transpose(st['rot'])
    for k in range(n + 1):
        sp = s0 + (s1 - s0) * k / n
        wp = tuple(p[i] + u[i] * sp - c[i] for i in range(3))
        v = apply(Rt, wp)
        for bc, h in st['boxes']:
            e = [max(abs(v[i] - bc[i]) - h[i], 0.0) for i in range(3)]
            if e[0] * e[0] + e[1] * e[1] + e[2] * e[2] < clear * clear: return True
    return False


def part_spheres(assets, scene):
    """[(local position accumulated through parents, radius)] for every part with a body value > 0."""
    text, _ = sr.scene_text(assets, scene)
    if text is None: return []
    ps = sr.parts(text)
    by = {p['idx']: p for p in ps}
    out = []
    for p in ps:
        pos, q, depth = (0, 0, 0), p, 0
        while q is not None and depth < 32:
            pos = tuple(pos[i] + q['pos'][i] for i in range(3))
            q = by.get(q['parent']); depth += 1
        r = sr.body_value(assets, p['body'])
        if r > 0: out.append((pos, r))
    return out


def seg_dist(p, q, c):
    """distance from point c to segment pq"""
    d = tuple(q[i] - p[i] for i in range(3)); w = tuple(c[i] - p[i] for i in range(3))
    dd = sum(x * x for x in d)
    t = 0.0 if dd == 0 else max(0.0, min(1.0, sum(w[i] * d[i] for i in range(3)) / dd))
    f = tuple(p[i] + t * d[i] for i in range(3))
    return math.sqrt(sum((c[i] - f[i]) ** 2 for i in range(3))), f, t


def blockers(p, q, obstacles, clear):
    """[(penetration, index, sphere)] for spheres (centre, r) that the segment passes within clear of"""
    out = []
    for idx, (c, r) in obstacles:
        d, f, t = seg_dist(p, q, c)
        if d < r + clear: out.append((r + clear - d, idx, (c, r), f, t))
    return out


def detour(p, q, blk, clear):
    pen, idx, (c, r), f, t = max(blk)      # deepest penetration
    m = tuple(f[i] - c[i] for i in range(3)); ln = math.sqrt(sum(x * x for x in m))
    if ln < 1:                              # line through the centre: any perpendicular
        d = tuple(q[i] - p[i] for i in range(3)); ax = (0, 1, 0) if abs(d[1]) < abs(d[0]) else (1, 0, 0)
        m = (d[1] * ax[2] - d[2] * ax[1], d[2] * ax[0] - d[0] * ax[2], d[0] * ax[1] - d[1] * ax[0]); ln = math.sqrt(sum(x * x for x in m))
    m = tuple(x / ln for x in m)
    return tuple(c[i] + m[i] * (r + clear) * 1.05 for i in range(3)), idx


def plan(p, q, obstacles, clear, depth=0):
    """recursive tangent insertion; returns (waypoints between p and q, failed)"""
    blk = blockers(p, q, obstacles, clear)
    if not blk: return [], False
    if depth >= 6: return [], True
    T, _ = detour(p, q, blk, clear)
    a, fa = plan(p, T, obstacles, clear, depth + 1); b, fb = plan(T, q, obstacles, clear, depth + 1)
    return a + [T] + b, fa or fb


def dist(p, q): return math.sqrt(sum((p[i] - q[i]) ** 2 for i in range(3)))


def pct(v, q):
    v = sorted(v); return v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))] if v else None


def run(label, a, with_stations, max_rbs_m=None):
    stations = {}
    if with_stations:
        docks, _ = ms.type_radii(a, 'TDocks', 11); facs, _ = ms.type_radii(a, 'TFactories', 11)
        stations = {k: v for k, v in {**docks, **facs}.items() if max_rbs_m is None or v['radius_u'] <= max_rbs_m * U}
    ast, _ = ms.type_radii(a, 'TAsteroids', 10, body_col=0, key='row')
    parts_cache, boxes_cache = {}, {}
    d, prov = a.get('addon/maps/x3_universe.xml')
    root = ET.fromstring(d)
    res = {c: {} for c in CLEAR_M}
    counts = dict(sectors=0, lines={}, stations_without_parts=0)
    for sec in (o for o in root.iter('o') if o.get('t') == '1'):
        counts['sectors'] += 1
        gates, sts, asts = [], [], []
        for c in sec:
            if c.get('x') is None: continue
            t = c.get('t'); pos = tuple(int(float(c.get(k, 0))) for k in 'xyz')
            if t == '18': gates.append(pos)
            elif t == '17' and c.get('s', '').isdigit() and int(c['s'] if False else c.get('s')) in ast:
                asts.append((pos, ast[int(c.get('s'))]['radius_u']))
            elif t in ('5', '6') and with_stations and c.get('s') in stations:
                st = stations[c.get('s')]
                if st['scene'] not in parts_cache:
                    parts_cache[st['scene']] = part_spheres(a, st['scene']); boxes_cache[st['scene']] = part_boxes(a, st['scene'])
                ang = tuple(int(c.get(k, 0)) for k in 'abg')
                sts.append(dict(pos=pos, rbs=st['radius_u'], parts=parts_cache[st['scene']], boxes=boxes_cache[st['scene']], rot=rot(*ang)))
                if not parts_cache[st['scene']]: counts['stations_without_parts'] += 1
        # obstacle sets per station index: sphere, body (rotated), body (unrotated)
        sph = [(i, (s['pos'], s['rbs'])) for i, s in enumerate(sts)]
        body, body0 = [], []
        for i, s in enumerate(sts):
            for lp, r in s['parts']:
                body.append((i, (tuple(s['pos'][k] + apply(s['rot'], lp)[k] for k in range(3)), r)))
                body0.append((i, (tuple(s['pos'][k] + lp[k] for k in range(3)), r)))
        astsp = [(1000 + i, sp) for i, sp in enumerate(asts)]
        lines = []
        for p, q in itertools.combinations(gates, 2): lines.append(('g2g', p, q, ()))
        for p in gates:
            for j, s in enumerate(sts): lines.append(('g2s', p, s['pos'], (j,)))
        for (i, s), (j, u) in itertools.combinations(enumerate(sts), 2): lines.append(('s2s', s['pos'], u['pos'], (i, j)))
        for kind, p, q, excl in lines:
            counts['lines'][kind] = counts['lines'].get(kind, 0) + 1
            L = dist(p, q)
            for C in CLEAR_M:
                cu = C * U
                r = res[C].setdefault(kind, dict(n=0, sphere=0, body=0, body0=0, box=0, sphere_only=0, sphere_only_box=0, body_multi=0, chained_same=0, chained_other=0,
                                                  wp1=0, wp2=0, wp3p=0, fail=0, ast=0, ast_multi=0, ast_chained=0, ast_wp2p=0,
                                                  extra_m=[], extra_pct=[], route_extra_m=[], route_extra_pct=[], ast_extra_m=[]))
                r['n'] += 1
                S = [x for x in sph if x[0] not in excl]; B = [x for x in body if x[0] not in excl]; B0 = [x for x in body0 if x[0] not in excl]
                bs = blockers(p, q, S, cu); bb = blockers(p, q, B, cu); bb0 = blockers(p, q, B0, cu)
                if bs:
                    r['sphere'] += 1
                    if any(box_blocked(p, q, sts[x[1]], cu) for x in bs): r['box'] += 1
                    else: r['sphere_only_box'] += 1
                if bb0: r['body0'] += 1
                if bs and not bb: r['sphere_only'] += 1
                if bb:
                    r['body'] += 1
                    if len({x[1] for x in bb}) >= 2: r['body_multi'] += 1
                    T, first = detour(p, q, bb, cu)
                    again = blockers(p, T, B, cu) + blockers(T, q, B, cu)
                    if any(x[1] != first for x in again): r['chained_other'] += 1
                    elif again: r['chained_same'] += 1
                    ex = dist(p, T) + dist(T, q) - L
                    r['extra_m'].append(ex / U); r['extra_pct'].append(100 * ex / L if L else 0)
                    wps, failed = plan(p, q, B, cu)
                    if failed: r['fail'] += 1
                    elif len(wps) == 1: r['wp1'] += 1
                    elif len(wps) == 2: r['wp2'] += 1
                    else: r['wp3p'] += 1
                    if not failed:
                        pts = [p] + wps + [q]; rl = sum(dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)) - L
                        r['route_extra_m'].append(rl / U); r['route_extra_pct'].append(100 * rl / L if L else 0)
                ba = blockers(p, q, astsp, cu)
                if ba:
                    r['ast'] += 1
                    if len(ba) >= 2: r['ast_multi'] += 1
                    T, _ = detour(p, q, ba, cu)
                    if blockers(p, T, astsp, cu) or blockers(T, q, astsp, cu): r['ast_chained'] += 1
                    wps, failed = plan(p, q, astsp, cu)
                    if failed or len(wps) >= 2: r['ast_wp2p'] += 1
                    r['ast_extra_m'].append((dist(p, T) + dist(T, q) - L) / U)
    out = dict(source=prov, counts=counts, clearance={})
    for C in CLEAR_M:
        out['clearance'][C] = {}
        for kind, r in res[C].items():
            n = r['n']
            keys = ('sphere', 'body', 'body0', 'box', 'sphere_only', 'sphere_only_box', 'body_multi', 'chained_same', 'chained_other', 'wp1', 'wp2', 'wp3p', 'fail', 'ast', 'ast_multi', 'ast_chained', 'ast_wp2p')
            row = {k: r[k] for k in ('n',) + keys}
            row.update({k + '_pct': round(100 * r[k] / n, 2) for k in keys})
            for k in ('extra_m', 'extra_pct', 'route_extra_m', 'route_extra_pct', 'ast_extra_m'):
                v = r[k]
                row[k] = dict(n=len(v), median=round(statistics.median(v), 1), p90=round(pct(v, .9), 1), max=round(max(v), 1)) if v else None
            out['clearance'][C][kind] = row
    return out


def main():
    res = {}
    lines = []
    stock = Assets(Path('/mnt/c/X3'), catalogues=STOCK_AP_CATALOGUES)
    for label, a, ws, cap in (('stock', stock, True, None), ('stock-typical', stock, True, 15000),
                              ('installed', Assets(Path('/mnt/c/X3')), False, None)):
        res[label] = run(label, a, ws, cap)
        lines.append(f"== {label} {res[label]['source']} sectors={res[label]['counts']['sectors']} lines={res[label]['counts']['lines']} stations_without_parts={res[label]['counts']['stations_without_parts']}")
        for C, kinds in res[label]['clearance'].items():
            for kind, row in kinds.items():
                lines.append(f"  C={C:4d} m {kind}: n={row['n']} sphere={row['sphere_pct']}% body={row['body_pct']}% (unrotated {row['body0_pct']}%) box={row['box_pct']}% "
                             f"sphere_only={row['sphere_only_pct']}% sphere_only_vs_box={row['sphere_only_box_pct']}% body_multi={row['body_multi_pct']}% chained_same={row['chained_same_pct']}% chained_other={row['chained_other_pct']}% "
                             f"waypoints 1/2/3+/fail={row['wp1_pct']}/{row['wp2_pct']}/{row['wp3p_pct']}/{row['fail_pct']}% "
                             f"one-step extra_m={row['extra_m']} extra_pct={row['extra_pct']} route extra_m={row['route_extra_m']} route extra_pct={row['route_extra_pct']}")
                lines.append(f"             asteroids: blocked={row['ast_pct']}% multi={row['ast_multi_pct']}% chained={row['ast_chained_pct']}% wp2+/fail={row['ast_wp2p_pct']}% extra_m={row['ast_extra_m']}")
    text = '\n'.join(lines) + '\n'
    print(text, end='')
    (HERE / 'line_blockage.txt').write_text(text)
    (HERE / 'line_blockage.json').write_text(json.dumps(res, indent=1))


if __name__ == '__main__':
    main()
