#!/usr/bin/env python3
"""Gate hole geometry per TGates row, stock AP and installed (Mayhem 4), as a fraction of the engine radius [gate+0xa4].

Owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9. Reads the catalogues (never writes game bytes),
prints derived numbers only.

1. EXE facts (byte strings re-read through the PE map; RESULT line at the end): the pair filter that exempts a ship on
   order 5/0x1e to this gate, the narrow phase's collision-tree flag, which scene parts get a collision tree, from which
   LOD record, and which material groups are left out (blended effect materials).
2. Per scene: the radius rule of game-scale-and-ship-classes.md section 2 (scene_radius.radii, with a fresh body cache
   per catalogue view), every part's body, its collision LOD and colliding groups by that rule, the types/Bodies lists.
3. Geometry in gate-local units (points x LOD-0 value / 65536 + the part's scene offset; every gate part has zero
   rotation keys): the clear coaxial radius (minimum 2-D distance from the gate axis x = y = 0 to any triangle
   projected along z: a ship moving parallel to the axis at lateral rho never meets the mesh while rho + r_ship stays
   below it), the outer radius, the axial extent, the rim band (triangles within 0.05 r of the clear radius), a z-slab
   profile and the largest clear disc anywhere in the projection. 'visual' = LOD 0, all groups; 'collision' = the
   collision LOD, colliding groups only.

    python3 -I verification/results/ship-navigation-ai/gates/gate_hole.py > verification/results/ship-navigation-ai/gates/gate_hole.txt
"""
import json, math, re, struct, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/game-scale'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES   # noqa: E402
import bob1, ships, scene_radius as sr                       # noqa: E402

GAME = Path('/mnt/c/X3')
U = 500.0                                   # engine units per metre
EXIT_SPREAD = 0.30                          # amendment 8, site W lateral offset (x r_gate)
WARP_IN_UNITS = 1665000 * 810000 // 1000000  # 0x00465bec..0x00465bfd: 1,348,650 units along gate row 2

# ---------------------------------------------------------------- 1. EXE byte strings
EXE = GAME / 'X3AP.exe'
d = EXE.read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def rd(va, n):
    r = va - base
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            o = ra + r - sva
            return d[o:o + n]
    raise ValueError(hex(va))


BYTES = [
    (0x0045c7b3, '8b 46 40 c1 e8 14 a8 01', 'pair filter: first object (the gate) [+0x40] bit 0x100000'),
    (0x0045c7c1, '66 83 fa 07', 'pair filter: other object is class 7'),
    (0x0045c7d1, '66 3d 05 00', 'pair filter: its order [flight+0x258] == 5 (FlyObjectIntoContainer)'),
    (0x0045c7d7, '66 3d 1e 00', 'pair filter: or == 0x1e (ClearObjectFromHangar)'),
    (0x0045c7dd, '39 b1 24 02 00 00', 'pair filter: and [flight+0x224] == this gate -> skip pair (no contact at all)'),
    (0x0045d73b, '66 83 f9 04', 'CanWarp: gate subtype 4 excluded'),
    (0x0045d745, '66 83 f9 0f', 'CanWarp: subtypes 0xf..0x14 excluded (first)'),
    (0x0045d777, '66 83 f9 14', 'CanWarp: subtypes 0xf..0x14 excluded (last)'),
    (0x0048a92f, 'f7 c1 00 00 00 01', 'narrow phase: node A [+0x12c] & 0x1000000 (has a collision tree)'),
    (0x0048a941, 'a9 00 00 00 01', 'narrow phase: node B [+0x12c] & 0x1000000'),
    (0x0048a94c, 'f7 c1 00 00 00 08', 'narrow phase: node A not 0x8000000 (no-collision)'),
    (0x004346d0, '81 8e 2c 01 00 00 00 40 00 00', 'node class: SBTYPE_2D body -> [+0x12c] |= 0x4000'),
    (0x004346e7, '0d 00 00 00 10', 'node class: SBTYPE_2D2 body -> [+0x12c] |= 0x10000000'),
    (0x0043d822, '8b 0d 14 71 60 00', 'scene part: SBTYPE_DOCKPOINT list -> no collision'),
    (0x0043d88f, 'a9 00 40 00 10', 'scene part: [+0x12c] & 0x10004000 -> no collision'),
    (0x0043d8a3, '0d 00 00 00 08', 'scene part: ... -> [+0x12c] |= 0x8000000'),
    (0x0043d8c7, '66 83 7f 48 05', 'scene part: owner class 5 (or 0x0043fd00 ship) -> LOD record 1'),
    (0x0043d8dd, '0f bf 4e 10 83 e9 01', 'scene part: otherwise LOD record nLOD - 1 (the coarsest)'),
    (0x00487ee4, 'f6 45 50 40', 'body set: model [+0x50] & 0x40 (BodyData flag) ...'),
    (0x00487eea, '81 8e 2c 01 00 00 00 00 10 08', '... -> node [+0x12c] |= 0x8100000'),
    (0x00487f10, 'b3 10', 'body set: every LOD with record flags & 0x10 ...'),
    (0x00487f18, '84 59 30', '... builds the collision tree first (0x0047eb90)'),
    (0x0047ebed, '83 f8 ff', 'tree build: body slot [+4] == -1 -> no tree'),
    (0x0047ec43, 'a9 00 01 00 00', 'tree build: slot [+4] & 0x100 -> all groups'),
    (0x0047ec4a, 'f6 43 30 10', 'tree build: LOD record flags & 0x10 -> all groups'),
    (0x0047ec95, 'f6 46 60 a0', 'tree build: part flags & 0xa0 -> part skipped'),
    (0x0047ecb4, 'f6 40 0c 01', 'tree build: group [+0xc] & 1 -> group skipped'),
    (0x0047ecf5, '81 8f 2c 01 00 00 00 00 00 08', 'tree build: no colliding group -> no tree'),
    (0x0047eed0, '81 8f 2c 01 00 00 00 00 00 01', 'tree build: tree present -> node [+0x12c] |= 0x1000000'),
    (0x0046ed4b, 'c7 44 90 04 ff ff ff ff', '0x0046ed00: body slot [+4] = -1 (remember: no tree)'),
    (0x00482adb, 'a9 86 04 00 00', 'body load: material flags & 0x486 ...'),
    (0x00482ae2, 'c7 47 0c 01 00 00 00', '... -> group [+0xc] = 1 (not colliding)'),
    (0x00481a46, '83 3a 13', 'effect material: parameter semantic 0x13 (g_AlphaBlendEnable)'),
    (0x00481a5c, '66 83 78 20 00', 'effect material: parameter type word 0 (LONG)'),
    (0x00481a63, '83 78 24 00', 'effect material: value != 0'),
    (0x00481a69, 'ba 12 00 00 00', 'effect material: semantic 0x12 (g_ALPHATESTENABLE) looked up'),
    (0x00481a86, '83 4b 28 01', 'effect material: blend + alpha test -> flags |= 1 (still colliding)'),
    (0x00481a8e, '83 4b 28 02', 'effect material: blend, no alpha test -> flags |= 2 (0x486: not colliding)'),
    (0x00465bec, 'b8 e8 67 19 00', 'SA_WarpObjectIn: 1665000 ...'),
    (0x00465bf1, 'ba 10 5c 0c 00', '... x 810000 / 10^6 = 1,348,650 units'),
    (0x00465c0a, '8b 48 60', '... along gate row 2 ([gate+0x70]+0x60)'),
]
fails = 0
exe_rows = []
for va, hexs, claim in BYTES:
    ok = rd(va, len(bytes.fromhex(hexs))) == bytes.fromhex(hexs)
    fails += not ok
    exe_rows.append(f'  {"ok  " if ok else "FAIL"} {va:#010x} {hexs:32s} {claim}')
SEM = {}
for name, sem in (('g_AlphaBlendEnable', 0x13), ('g_ALPHATESTENABLE', 0x12)):
    SEM[name] = sem

# ---------------------------------------------------------------- 2./3. assets and geometry
KEY = re.compile(r'\{\s*0x[0-9a-fA-F]+\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;'
                 r'\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;')


def bodies_lists(a):
    """types/Bodies: {SBTYPE: set(lower-case names and ids)}."""
    for k in ('addon/types/Bodies.txt', 'addon/types/Bodies.pck'):
        if a.candidates(k):
            txt = a.get(k)[0].decode('latin-1')
            break
    toks = [t.strip() for l in txt.splitlines() if not l.lstrip().startswith('/') for t in l.split(';')]
    toks = [t for t in toks if t]
    out, i = {}, 0
    while i < len(toks):
        if toks[i].startswith('SBTYPE_'):
            n = int(toks[i + 1]); out[toks[i]] = {t.lower().replace('/', '\\') for t in toks[i + 2:i + 2 + n]}; i += 2 + n
        else:
            i += 1
    return out


def param(m, name):
    for p in m.get('params', []):
        if p[0] == name.encode():
            return p
    return None


def group_colliding(m):
    """Engine rule (0x00481a46..0x00481a8e, 0x00482adb): effect material -> blended without alpha test is out;
    classic material -> file flags & 0x486 (the engine may overwrite them from the Materials row, not modelled)."""
    if 'params' in m:
        b, t = param(m, 'g_AlphaBlendEnable'), param(m, 'g_ALPHATESTENABLE')
        blend = b is not None and b[1] == 0 and b[2][0] != 0
        atest = t is not None and t[1] == 0 and t[2][0] != 0
        return not (blend and not atest), 'effect %s blend=%d atest=%d' % (m['effect'].decode(), blend, atest)
    f = m.get('flags', 0) | m.get('flagword', 0)
    return not (f & 0x486), 'classic flags %#x (texture-row override not modelled)' % f


def load_body(a, name):
    try:
        e = bob1.resolve_body(a, name)
    except FileNotFoundError:
        return None, 'missing'
    try:
        return (e, bob1.parse(a.read_entry(e), None)), None
    except bob1.FormatError as ex:
        return None, 'no model (%s: %s)' % (e['source'], ex)


def tris(tree, lod_idx, groups_ok, off):
    L = bob1.lods(tree)
    l = L[lod_idx]
    P = np.array([p[1:4] for p in l['points']], float) * L[0]['value'] / 65536.0 + np.array(off, float)
    F = [f[:3] for p in l['parts'] for g in p['groups'] if groups_ok(g['material']) for f in g['faces']]
    return P[np.array(F, int)] if F else np.zeros((0, 3, 3))


def dist2d(T, c):
    A = T[:, :, :2] - np.asarray(c, float)[None, None, :]
    cr = lambda u, v: u[..., 0] * v[..., 1] - u[..., 1] * v[..., 0]
    s = [cr(A[:, (k + 1) % 3] - A[:, k], -A[:, k]) for k in range(3)]
    inside = ((s[0] >= 0) & (s[1] >= 0) & (s[2] >= 0)) | ((s[0] <= 0) & (s[1] <= 0) & (s[2] <= 0))
    dd = np.full(len(T), np.inf)
    for i, j in ((0, 1), (1, 2), (2, 0)):
        p, q = A[:, i], A[:, j]; e = q - p; ee = (e * e).sum(1); ee[ee == 0] = 1
        t = np.clip(-(p * e).sum(1) / ee, 0, 1); x = p + e * t[:, None]
        dd = np.minimum(dd, np.hypot(x[:, 0], x[:, 1]))
    dd[inside] = 0
    return dd


def best_disc(T, r):
    """Largest clear disc in the xy projection with its centre within 0.25 r of the axis (grid 0.01 r, then 0.002 r)."""
    if not len(T):
        return math.inf, (0.0, 0.0)
    d0 = dist2d(T, (0, 0))
    T = T[d0 < d0.min() + 0.55 * r]      # a disc centred within 0.25 r is bounded by these triangles only
    best, c0 = -1, (0.0, 0.0)
    for span, step in ((0.25, 0.01), (0.012, 0.002)):
        g = np.arange(-span, span + 1e-9, step) * r
        for cx in g + c0[0]:
            for cy in g + c0[1]:
                v = dist2d(T, (cx, cy)).min()
                if v > best:
                    best, c1 = v, (cx, cy)
        c0 = c1
    return best, c0


def metrics(T, r):
    if not len(T):
        return None
    dd = dist2d(T, (0, 0)); clear = dd.min()
    rho = np.hypot(T[..., 0], T[..., 1])
    near = dd < clear + 0.05 * r
    return dict(n=len(T), clear=clear, outer=rho.max(), z=(T[..., 2].min(), T[..., 2].max()),
                rim=(T[near][..., 2].min(), T[near][..., 2].max()), axis_tris=int((dd == 0).sum()))


def slab_profile(T, r, step=0.05):
    zmin, zmax = T[..., 2].min(1), T[..., 2].max(1)
    lo, hi = math.floor(zmin.min() / r / step) * step, math.ceil(zmax.max() / r / step) * step
    rows = []
    for z0 in np.arange(lo, hi - 1e-9, step):
        m = (zmax >= z0 * r) & (zmin <= (z0 + step) * r)
        if m.any():
            rows.append((z0, z0 + step, dist2d(T[m], (0, 0)).min() / r))
    return rows


def fmt(mx, r):
    if mx is None:
        return 'no triangles'
    return ('tris %d  clear %.3f r (%.0f m)  outer %.3f r (%.0f m)  z %+.3f..%+.3f r  rim z %+.3f..%+.3f r (%.0f m thick)%s'
            % (mx['n'], mx['clear'] / r, mx['clear'] / U, mx['outer'] / r, mx['outer'] / U, mx['z'][0] / r, mx['z'][1] / r,
               mx['rim'][0] / r, mx['rim'][1] / r, (mx['rim'][1] - mx['rim'][0]) / U,
               '  axis-covering tris %d' % mx['axis_tris'] if mx['axis_tris'] else ''))


inst_radii = json.loads((ROOT / 'verification/results/game-scale/ships_by_class.json').read_text())['installed']
NONCAP = ['M3', 'M4', 'M5', 'M6', 'M8', 'TS', 'TP', 'TM']
r_ship_max = max(inst_radii[c]['radius_m']['max'] for c in NONCAP)

print('Gate hole geometry (owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9)')
print('EXE %s' % EXE)
print('\n'.join(exe_rows))
print('installed non-capital ship radius max %.0f m (%s; ships_by_class.json)'
      % (r_ship_max, max(NONCAP, key=lambda c: inst_radii[c]['radius_m']['max'])))
summary = []
for label, a in (('stock', Assets(GAME, catalogues=STOCK_AP_CATALOGUES)), ('installed', Assets(GAME))):
    sr._BV.clear()                       # scene_radius caches body values by name across views: reset per view
    _, rows, prov = ships.table(a, 'TGates')
    blist = bodies_lists(a)
    print('\n==== %s TGates %s (%s)' % (label, prov['source'], prov['decoded_sha256'][:12]))
    scenes = {}
    for i, row in enumerate(rows):
        scenes.setdefault(row[7], []).append(i)
    for scene, idx in scenes.items():
        rad = sr.radii(a, scene)
        r = rad[0] if rad else 0
        text, src = sr.scene_text(a, scene)
        print('\n-- rows %s  root body col0 %s  scene %s (%s)  r = %s units = %.1f m'
              % (','.join(map(str, idx)), sorted({rows[i][0] for i in idx}), scene, src, r, r / U))
        if not text:
            continue
        keys = {}
        for m in re.finditer(r'^\s*P\s+(\d+)\s*;', text, re.M):
            k = KEY.search(text, m.end())
            keys[int(m.group(1))] = tuple(float(v) for v in k.groups()) if k else None
        vis_all, col_all = [], []
        for p in sr.parts(text):
            name = p['body'] or ''
            key = keys.get(p['idx'])
            rot0 = key is not None and all(v == 0 for v in key[3:7])
            listed = [s for s, names in blist.items() if name.lower().replace('/', '\\') in names]
            got, why = load_body(a, ('v/%05d' % int(name)) if name.isdigit() else name)
            head = '   part %d body %s offset %s rot0=%s Bodies-lists %s' % (p['idx'], name, p['pos'], rot0, listed or 'none')
            if got is None:
                print(head + ' -> ' + why)
                continue
            e, tree = got
            L = bob1.lods(tree)
            col_lod = next((i for i, l in enumerate(L) if l['flags'] & 0x10), len(L) - 1)
            all_mode = bool(L[col_lod]['flags'] & 0x10)
            mats = bob1.materials(tree)
            coll = {i: group_colliding(m) for i, m in enumerate(mats)}
            ok = (lambda mi: True) if all_mode else (lambda mi: coll.get(mi, (True,))[0])
            no_coll_reason = 'Bodies list' if any(s in listed for s in ('SBTYPE_2D', 'SBTYPE_2D2', 'SBTYPE_DOCKPOINT', 'SBTYPE_JET')) else None
            Tv = tris(tree, 0, lambda mi: True, p['pos'])
            Tc = tris(tree, col_lod, ok, p['pos']) if not no_coll_reason else np.zeros((0, 3, 3))
            print(head + ' -> %s:%s value %d, %d LOD, collision LOD %d%s' % (e['source'], e['path'], L[0]['value'], len(L), col_lod,
                                                                         ' (all groups, flag 0x10)' if all_mode else ''))
            used = sorted({g['material'] for pp in L[col_lod]['parts'] for g in pp['groups']})
            for mi in used:
                print('      material %d: %s -> %s' % (mi, coll[mi][1], 'collides' if (all_mode or coll[mi][0]) else 'no collision'))
            if p['body'] and 'effect' in name and len(Tv):
                c = np.array([0, 0, p['pos'][2]], float); dd = np.linalg.norm(Tv.reshape(-1, 3) - c, axis=1) / r
                print('      |vertex - (0,0,%d)| / r: min %.3f median %.3f max %.3f' % (p['pos'][2], dd.min(), np.median(dd), dd.max()))
            print('      visual    ' + fmt(metrics(Tv, r), r))
            print('      collision ' + (fmt(metrics(Tc, r), r) if len(Tc) else 'none (no colliding group: no tree, node never flagged 0x1000000)'))
            vis_all.append(Tv)
            if len(Tc):
                col_all.append(Tc)
        Tc = np.concatenate(col_all) if col_all else np.zeros((0, 3, 3))
        Tv = np.concatenate(vis_all) if vis_all else np.zeros((0, 3, 3))
        mc, mv = metrics(Tc, r), metrics(Tv, r)
        if mc:
            disc, cen = best_disc(Tc, r)
            prof = slab_profile(Tc, r)
            print('   scene collision: ' + fmt(mc, r))
            print('   largest clear disc %.3f r (%.0f m) centred at (%+.3f, %+.3f) r' % (disc / r, disc / U, cen[0] / r, cen[1] / r))
            print('   clear radius by z slab (r): ' + ' '.join('[%+.2f,%+.2f]%.3f' % s for s in prof))
            h = mc['clear'] / r
            half = max(abs(mc['rim'][0]), abs(mc['rim'][1])) / r
            print('   Q3 h = %.3f; axis-parallel crossing clear iff lateral <= %.3f r - r_ship; at 34 deg to the axis'
                  ' subtract rim half-thickness x tan = %.3f r' % (h, h, half * math.tan(math.radians(34))))
            print('   Q3 exit spread: lateral %.2f r + r_ship <= %.3f r iff r_ship <= %.0f m; with r_ship %.0f m margin %.0f m;'
                  ' warp-in start %d units = %.2f r along +z'
                  % (EXIT_SPREAD, h, (h - EXIT_SPREAD) * r / U, r_ship_max, (h - EXIT_SPREAD) * r / U - r_ship_max,
                     WARP_IN_UNITS, WARP_IN_UNITS / r))
            summary.append((label, idx, scene, r, h, 'ring' if h > 0.05 else 'see profile'))
        else:
            print('   scene collision: none -> no ship-gate contact possible; visual ' + fmt(mv, r) if mv else '   no geometry')
            summary.append((label, idx, scene, r, math.inf, 'no collision mesh'))

print('\n==== summary (h = clear coaxial radius of the collision mesh / r)')
for label, idx, scene, r, h, kind in summary:
    print('  %-9s rows %-22s %-46s r %6.0f m  h %s  %s' % (label, ','.join(map(str, idx)), scene, r / U,
                                                       'none' if h == math.inf else '%.3f (%.0f m)' % (h, h * r / U), kind))
print('RESULT %s (%d of %d EXE byte strings match)' % ('PASS' if not fails else 'FAIL', len(BYTES) - fails, len(BYTES)))
