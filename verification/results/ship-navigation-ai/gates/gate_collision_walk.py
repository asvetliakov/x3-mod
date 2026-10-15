#!/usr/bin/env python3
"""Which installed gate models carry a collision tree: every TGates row, its scene and bodies, every catalogue copy of
each body (vanilla, Mayhem 4, the x3m LOD overlay), the gate root body (TGates column 0) and the gate subtypes the
installed universe places.

Owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9 ("Collision walk"). Read-only on the game
folder; prints derived numbers, names, sizes and catalogue slots only (no game bytes).

    python3 -I verification/results/ship-navigation-ai/gates/gate_collision_walk.py \
        > verification/results/ship-navigation-ai/gates/gate_collision_walk.txt

Rules applied (EXE byte strings re-read in section 0; RESULT line at the end):
* A node collides only with a collision tree: [node+0x12c] & 0x1000000 and not 0x8000000 (narrow phase 0x0048a929).
  0x0047eb90 builds the tree from the requested LOD record (or the last), skipping parts with flags & 0xa0 and groups
  with [+0xc] & 1; a LOD record flagged 0x10 takes all groups. No colliding group -> no tree.
* Group [+0xc] = 1 when the material flags have any of 0x486. Binary bodies: the file material (0x00482adb; effect
  materials: blend without alpha test, 0x00481a46..0x00481a8e). Text bodies with per-body materials (MAT5/MAT6): the
  file material; text bodies without (MATERIAL3): the types/Materials row flags (+0x10) of the row the record was
  matched to (0x0048586b..0x00485896), and 0x004f71f0 only considers rows whose texture id (+0x0c) equals the
  record's texture (0x004f72c3).
* The gate object root: 0x0043ffa0 (class 0x12 takes the generic branch) creates the root node, sets the body from
  the type row's column 0 (0x0044126d, 0x00441276) and builds its tree from LOD 0 (0x00441280). A body listed in
  types/BodyDummies sets slot +0x14 (0x0046fb06), copied into model +0x50 & 0x40 (0x0048411c..0x00484125), which
  gives the node 0x8100000 (no collision, 0x00487eea). 0x0043ce30 flags the root 0x8000000 only for body 0x3ac or -1.
"""
import hashlib, math, re, struct, sys, xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/game-scale'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES   # noqa: E402
import bob1, ships, scene_radius as sr, lod_atlas            # noqa: E402
import map_stats                                             # noqa: E402

GAME = Path('/mnt/c/X3')
U = 500.0

# ---------------------------------------------------------------- 0. EXE byte strings
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
    (0x0048a92f, 'f7 c1 00 00 00 01', 'narrow phase: node A has a collision tree'),
    (0x0048a94c, 'f7 c1 00 00 00 08', 'narrow phase: node A not 0x8000000'),
    (0x0044126d, '0f bf 44 24 10', 'object create (generic class branch, incl. 0x12): root body id = type row column 0'),
    (0x00441276, 'e8 b5 6b 04 00', '... call 0x00487e30 (set body on the new root node)'),
    (0x0044127e, '53 51 e8 0b d9 03 00', '... push 0, push root; call 0x0047eb90 (collision tree from LOD 0)'),
    (0x0047eb9b, '8b 87 40 01 00 00', 'tree build: body id [+0x140] (< 0 or unloadable -> no tree)'),
    (0x0047ebd8, 'f7 87 2c 01 00 00 00 00 00 08', 'tree build: node 0x8000000 -> no tree'),
    (0x0047ebed, '83 f8 ff', 'tree build: body slot [+4] == -1 -> no tree'),
    (0x0047ec95, 'f6 46 60 a0', 'tree build: part flags & 0xa0 -> part skipped'),
    (0x0047ecb4, 'f6 40 0c 01', 'tree build: group [+0xc] & 1 -> group skipped'),
    (0x00482adb, 'a9 86 04 00 00', 'binary body: material flags & 0x486 -> group [+0xc] = 1'),
    (0x0048586b, 'f6 42 50 10', 'text body: model +0x50 & 0x10 (per-body materials) ? file material : Materials row'),
    (0x00485886, 'a1 b0 8d 60 00', 'text body: Materials table 0x00608db0 ...'),
    (0x0048588b, '8b 44 90 10', '... row flags +0x10 (row stride 0x3c)'),
    (0x0048588f, 'a9 86 04 00 00', '... & 0x486'),
    (0x00485896, 'c7 43 0c 01 00 00 00', '... -> group [+0xc] = 1 (not colliding)'),
    (0x004f72c3, '66 3b 54 2e 0c', 'MATERIAL3 match 0x004f71f0: only rows whose texture id +0x0c equals the record texture'),
    (0x0046fb06, '89 74 8a 14', 'types/BodyDummies (0x0046f9c0): listed body -> slot +0x14 = 1'),
    (0x0048411c, 'e8 1f 9f fe ff 85 c0 74 04 83 4f 50 40', 'body load: slot +0x14 (0x0046e040) -> model +0x50 |= 0x40'),
    (0x00487eea, '81 8e 2c 01 00 00 00 00 10 08', 'body set: model +0x50 & 0x40 -> node 0x8100000 (no collision)'),
    (0x0043d0f3, '3d ac 03 00 00', 'scene build 0x0043ce30: root body 0x3ac (or -1) -> 0x8000000; nothing else'),
]
fails = 0
print('Gate collision walk (owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9)')
print('EXE %s sha256 %s' % (EXE, hashlib.sha256(d).hexdigest()[:16]))
for va, hexs, claim in BYTES:
    ok = rd(va, len(bytes.fromhex(hexs))) == bytes.fromhex(hexs)
    fails += not ok
    print('  %s %#010x %-40s %s' % ('ok  ' if ok else 'FAIL', va, hexs, claim))
n_bytes = len(BYTES)


# ---------------------------------------------------------------- helpers (as gate_hole.py)
def param(m, name):
    for p in m.get('params', []):
        if p[0] == name.encode():
            return p
    return None


def group_colliding(m):
    if 'params' in m:
        b, t = param(m, 'g_AlphaBlendEnable'), param(m, 'g_ALPHATESTENABLE')
        blend = b is not None and b[1] == 0 and b[2][0] != 0
        atest = t is not None and t[1] == 0 and t[2][0] != 0
        return not (blend and not atest), 'effect blend=%d atest=%d' % (blend, atest)
    f = m.get('flags', 0) | m.get('flagword', 0)
    return not (f & 0x486), 'classic flags %#x' % f


def tris(tree, lod_idx, ok, off):
    L = bob1.lods(tree)
    l = L[lod_idx]
    P = np.array([p[1:4] for p in l['points']], float) * L[0]['value'] / 65536.0 + np.array(off, float)
    F = [f[:3] for p in l['parts'] if not (p.get('flags', 0) & 0xa0) for g in p['groups'] if ok(g['material'])
         for f in g['faces']]
    return P[np.array(F, int)] if F else np.zeros((0, 3, 3))


def dist2d(T):
    A = T[:, :, :2]
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


def ring(T):
    """clear coaxial radius, outer radius, z extent (units) of a triangle set."""
    if not len(T):
        return None
    return dist2d(T).min(), np.hypot(T[..., 0], T[..., 1]).max(), T[..., 2].min(), T[..., 2].max()


def mount_rank(a, source):
    return a.layers.index(source) if source in a.layers else -1


def members(a, stem):
    """Every catalogue / loose member of objects/<stem> under the four body extensions, in mount order."""
    out = {}
    for ext in ('.pbb', '.bob', '.pbd', '.bod'):    # packed/unpacked aliases share a key: dedupe by member
        for e in a.candidates('objects/' + stem.replace('\\', '/') + ext):
            out[(e['source'], e['path'])] = e
    return sorted(out.values(), key=lambda e: (('loose' in e), mount_rank(a, e['source'])))


def body_verdict(a, e, off=(0, 0, 0)):
    """Collision verdict of one member as the engine would load it."""
    raw = a.read_entry(e)
    if raw[:4] != b'BOB1' and len(raw) < 64 and not re.search(rb'^\s*-?\d', raw, re.M):
        return dict(kind='comment-only stub (%d B): no model [i]' % len(raw), tree=False)
    try:
        tree = bob1.parse(raw, None)
    except bob1.FormatError as ex:
        return dict(kind='text not parsed by bob1 (%s)' % str(ex)[:60], tree=None)
    L = bob1.lods(tree)
    mats = bob1.materials(tree)
    col_lod = next((i for i, l in enumerate(L) if l['flags'] & 0x10), len(L) - 1)
    all_mode = bool(L[col_lod]['flags'] & 0x10)
    coll = {i: group_colliding(m) for i, m in enumerate(mats)}
    ok = (lambda mi: True) if all_mode else (lambda mi: coll.get(mi, (True,))[0])
    Tc = tris(tree, col_lod, ok, off)
    Tv = tris(tree, 0, lambda mi: True, off)
    used = sorted({g['material'] for pp in L[col_lod]['parts'] for g in pp['groups']})
    return dict(kind='%s %d LOD' % ('BOB1' if raw[:4] == b'BOB1' else 'text', len(L)), value=L[0]['value'],
                nlod=len(L), col_lod=col_lod, all_mode=all_mode, tree=bool(len(Tc)), ntri=len(Tc),
                mats=['%d:%s:%s' % (mi, coll[mi][1], 'in' if (all_mode or coll[mi][0]) else 'out') for mi in used],
                ring_c=ring(Tc), ring_v=ring(Tv))


def fmt_ring(rg):
    if rg is None:
        return '-'
    return 'hole %.0f m outer %.0f m z %+.0f..%+.0f m' % (rg[0] / U, rg[1] / U, rg[2] / U, rg[3] / U)


def winner(a, stem):
    try:
        return bob1.resolve_body(a, stem)
    except FileNotFoundError:
        return None


# ---------------------------------------------------------------- testbody (MATERIAL3 text body, root of every gate)
def text_body_facts(a, raw):
    """Minimal reading of a one-record MATERIAL3 text body: record value, L-inf extent, part and LOD flags (radix 2,
    body-text-loader.md section 3), face materials, and the Materials-row flags each MATERIAL3 record can map to."""
    txt = raw.decode('latin-1')
    lines = [l.split('/')[0].strip() for l in txt.splitlines()]
    mats = []
    for l in txt.splitlines():
        if l.startswith('MATERIAL3:'):
            f = [x.strip() for x in l[len('MATERIAL3:'):].split(';')]
            mats.append(dict(idx=int(f[0]), tex=int(f[1]), blend=int(f[15]), twosided=int(f[16]), wire=int(f[17])))
    toks = [t for l in lines if l and not l.startswith('MATERIAL') for t in (x.strip() for x in l.split(';')) if t]
    value = int(toks[0]); i = 1; pts = []
    while toks[i:i + 3] != ['-1', '-1', '-1']:
        pts.append(tuple(int(v) for v in toks[i:i + 3])); i += 3
    i += 3
    m = max(abs(c) for p in pts for c in p)
    parts, faces = [], Counter()
    while toks[i] != '-99':
        while toks[i] != '-99':
            mat = int(toks[i]); j = i + 1
            while int(toks[j]) >= 0:
                j += 1
            k = j - i - 1; fl = -int(toks[j]); faces[mat] += 1
            i = j + 1 + (1 if fl & 0x10 else 0) + (2 * k if fl & 8 else 0) + (2 * k if fl & 0x80 else 0)
        parts.append(int(toks[i + 1], 2)); i += 2
    lodflags = int(toks[i + 1], 2)
    rows = lod_atlas.materials_rows(a)
    for mt in mats:
        hits = [(n, f) for n, (tex, f, _) in enumerate(rows) if tex == mt['tex']]
        mt['rows'] = hits or [(0, rows[0][1])]          # no texture match: every distance equal, row 0 wins
    return dict(value=value, npts=len(pts), m=m, half_cube=min(max(abs(c) for c in p) for p in pts) * value / m,
                extent=value, parts=parts, lodflags=lodflags, faces=dict(faces), mats=mats)


def body_dummies(a):
    for k in ('types/BodyDummies.pck', 'types/BodyDummies.txt'):
        c = a.candidates(k)
        if c:
            t = a.read_entry(c[-1]).decode('latin-1')
            toks = [x.strip() for l in t.splitlines() if not l.lstrip().startswith('/') for x in l.split(';') if x.strip()]
            return c[-1]['source'], [x.lower() for x in toks[1:1 + int(toks[0])]]
    return None, []


# ---------------------------------------------------------------- 1-3. TGates rows, scenes, bodies
VIEWS = (('stock', Assets(GAME, catalogues=STOCK_AP_CATALOGUES)), ('installed', Assets(GAME)))
summary = {}
chains = {}
for label, a in VIEWS:
    sr._BV.clear()
    _, rows, prov = ships.table(a, 'TGates')
    print('\n==== %s view: TGates from %s (sha %s), %d rows' % (label, prov['source'], prov['decoded_sha256'][:12], len(rows)))
    src, dummies = body_dummies(a)
    print('  types/BodyDummies (%s): %s' % (src, dummies))
    scenes = defaultdict(list)
    for i, row in enumerate(rows):
        scenes[(row[7], row[0])].append(i)
    for (scene, col0), idx in scenes.items():
        rad = sr.radii(a, scene)
        r = rad[0] if rad else 0
        sm = members(a, scene)
        sw = winner(a, scene)
        print('\n-- rows %s (%s)  root body col0 = %s  scene %s  r = %d units = %.1f m'
              % (','.join(map(str, idx)), '/'.join(sorted({rows[i][17] for i in idx}))[:60], col0, scene, r, r / U))
        print('   scene members: %s; engine winner %s:%s' % (', '.join('%s:%s' % (e['source'], e['path'].rsplit('/', 1)[-1]) for e in sm),
                                                         sw['source'] if sw else '-', sw['path'].rsplit('/', 1)[-1] if sw else '-'))
        text, _ = sr.scene_text(a, scene)
        parts = sr.parts(text) if text else []
        if text:
            commented = re.findall(r'^\s*//\s*P\s+\d+\s*;\s*B\s+([^;]+);', text, re.M)
            if commented:
                print('   commented-out parts: %s' % commented)
        any_tree = False
        for p in parts:
            name = p['body'] or ''
            stem = ('v/%05d' % int(name)) if name.isdigit() else name
            ms = members(a, stem)
            w = winner(a, stem) if ms else None
            listed = name.lower() in dummies
            print('   part %d body %s offset %s%s' % (p['idx'], name, p['pos'], '  (BodyDummies: no collision)' if listed else ''))
            if not ms:
                print('      no member in this view: no body, no tree')
                continue
            for e in ms:
                v = body_verdict(a, e, p['pos'])
                chains.setdefault(stem.lower(), {})[(e['source'], e['path'])] = (v, e.get('size'))
                win = w is not None and (e['source'], e['path']) == (w['source'], w['path'])
                tree = v['tree'] and not listed
                if win and tree:
                    any_tree = True
                print('      %s %-9s %s:%s  %s  %s' % ('WIN' if win else '   ', 'TREE' if v['tree'] else ('no tree' if v['tree'] is False else '?'),
                                                    e['source'], e['path'].rsplit('/', 1)[-1], v['kind'],
                                                    '' if 'value' not in v else 'LOD0 value %d, collision LOD %d%s, %d tris, mats %s; collision %s; visual %s'
                                                    % (v['value'], v['col_lod'], ' (all groups)' if v['all_mode'] else '', v['ntri'],
                                                       ' '.join(v['mats']), fmt_ring(v['ring_c']), fmt_ring(v['ring_v']))))
                if win and v.get('ring_c') and r:
                    rc = v['ring_c']
                    print('          as fraction of r: hole %.3f outer %.3f z %+.3f..%+.3f' % (rc[0] / r, rc[1] / r, rc[2] / r, rc[3] / r))
        summary.setdefault(label, []).append((idx, scene, col0, r, any_tree))

    # root body (TGates column 0)
    print('\n  -- gate root bodies (TGates column 0)')
    for col0 in sorted({row[0] for row in rows}, key=int):
        stem = 'v/%05d' % int(col0)
        ms = members(a, stem)
        w = winner(a, stem) if ms else None
        if not w:
            print('   body %s: no member (%s): 0x0047eb90 finds no model -> no tree' % (col0, stem))
            continue
        raw = a.read_entry(w)
        f = text_body_facts(a, raw)
        listed = stem.lower() in dummies or ('v\\%05d' % int(col0)) in dummies
        mat_rows = [(mt['idx'], mt['tex'], [(n, hex(fl), hex(fl & 0x486)) for n, fl in mt['rows']], mt['blend']) for mt in f['mats']]
        colliding = all(fl & 0x486 == 0 for mt in f['mats'] for _, fl in mt['rows'])
        part_ok = any(not (pf & 0xa0) for pf in f['parts'])
        print('   body %s = %s:%s (%d B): record value %d units, %d points, L-inf extent %d units (%.3f m), central cube half-side %.1f units (%.3f m)'
              % (col0, w['source'], w['path'], len(raw), f['value'], f['npts'], f['extent'], f['extent'] / U, f['half_cube'], f['half_cube'] / U))
        print('      part flags %s, LOD flags %#x (0x10 all-groups: %s), faces per material %s' % ([hex(x) for x in f['parts']], f['lodflags'], bool(f['lodflags'] & 0x10), f['faces']))
        print('      MATERIAL3 records (idx, texture, Materials rows with that texture (row, flags, &0x486), blend): %s' % mat_rows)
        print('      BodyDummies listed: %s; body 0x3ac/-1: %s' % (listed, int(col0) in (0x3ac, -1)))
        print('      -> root node collision tree: %s' % ('YES (a %.2f m object at the gate origin)' % (2 * f['extent'] / U)
                                                       if colliding and part_ok and not listed else 'no'))

# ---------------------------------------------------------------- 3. body chains across catalogues
# every gate body stem of either view, plus every member the installed catalogues carry for it (the Mayhem 4
# catalogues addon/05..12 and the x3m LOD overlay addon/13..15 included), whether or not an installed scene uses it
inst = VIEWS[1][1]
for stem in sorted(set(chains) | {'others\\argon_gate', 'others\\argon_gate_destroyed', 'others\\terran_catapult_gate',
                                  'others\\terraformer_gate', 'patch20\\terran_gate'}):
    for e in members(inst, stem):
        if (e['source'], e['path']) not in chains.get(stem, {}):
            chains.setdefault(stem, {})[(e['source'], e['path'])] = (body_verdict(inst, e), e.get('size'))
print('\n==== body members across catalogues (both views merged; the installed engine winner marked *)')
for stem, ch in sorted(chains.items()):
    w = winner(inst, stem) if members(inst, stem) else None
    print('  %s' % stem)
    for (src, path), (v, size) in sorted(ch.items(), key=lambda kv: mount_rank(inst, kv[0][0])):
        star = '*' if w and (w['source'], w['path']) == (src, path) else ' '
        print('   %s %-18s %-40s %-10s %s' % (star, src, path.rsplit('/', 1)[-1], 'TREE' if v['tree'] else ('no tree' if v['tree'] is False else '?'),
                                         v['kind'] if 'value' not in v else '%s, collision %d tris, %s' % (v['kind'], v['ntri'], fmt_ring(v['ring_c']))))

# ---------------------------------------------------------------- 4. universe
print('\n==== gate objects in the universe map (t=18, s = TGates row)')
for label, a in VIEWS:
    data, prov = a.get('addon/maps/x3_universe.xml')
    root = ET.fromstring(data)
    sr._BV.clear()
    ast, _ = map_stats.type_radii(a, 'TAsteroids', 10, body_col=0, key='row')
    sub = Counter(); near = defaultdict(list); nsec = 0; ast_gap = []
    for sec in root.iter('o'):
        if sec.get('t') != '1':
            continue
        nsec += 1
        objs = [(o.get('t'), o.get('s'), tuple(float(o.get(k, 0)) for k in 'xyz')) for o in sec if o.tag == 'o']
        gates = [p for t, s, p in objs if t == '18']
        gate_ids = {p: (sec.get('x'), sec.get('y'), o.get('gid'), o.get('s')) for o in sec if o.tag == 'o' and o.get('t') == '18'
                    for p in [tuple(float(o.get(k, 0)) for k in 'xyz')]}
        for t, s, p in objs:
            if t == '18':
                sub[int(s)] += 1
        for g in gates:
            for t, s, p in objs:
                if t in ('1', '2', '3', '4') or p == g:
                    continue
                dd = math.dist(g, p) / U
                if dd < 10000:
                    near[t].append(dd)
                if t == '17' and s and s.isdigit() and int(s) in ast:
                    ast_gap.append((dd - ast[int(s)]['radius_u'] / U, dd, ast[int(s)]['radius_u'] / U, int(s), gate_ids[g]))
    tg = {i: r[17] for i, r in enumerate(ships.table(a, 'TGates')[1])}
    print('  %s: %s (sha %s), %d sectors, %d gates' % (label, prov['source'], prov['decoded_sha256'][:12], nsec, sum(sub.values())))
    print('    subtypes: %s' % ', '.join('%d %s x%d' % (k, tg.get(k, '?'), v) for k, v in sorted(sub.items())))
    for t, v in sorted(near.items(), key=lambda kv: int(kv[0])):
        v.sort()
        print('    map objects t=%s within 10 km of a gate centre: %d; nearest %.0f m; within 2 km: %d, within 5 km: %d'
              % (t, len(v), v[0], sum(x < 2000 for x in v), sum(x < 5000 for x in v)))
    if ast_gap:
        ast_gap.sort()
        print('    asteroid surface (centre distance - TAsteroids body radius) to a gate centre: min %.0f m (centre %.0f m,'
              ' radius %.0f m); within 1.5 km: %d' % (ast_gap[0][:3] + (sum(x[0] < 1500 for x in ast_gap),)))
        arows = ships.table(a, 'TAsteroids')[1]
        rmax = {}
        for gap, dd, rad, row, gid in ast_gap:
            if gap >= 1500:
                break
            if row not in rmax:
                tree = bob1.parse(a.read_entry(bob1.resolve_body(a, arows[row][0])), None)
                T = tris(tree, 0, lambda mi: True, (0, 0, 0))
                v = np.linalg.norm(T.reshape(-1, 3), axis=1)
                # inner radius: smallest distance from the body origin to any LOD-0 triangle (closest point on it)
                A, B, C = T[:, 0], T[:, 1], T[:, 2]
                n = np.cross(B - A, C - A); nn = np.linalg.norm(n, axis=1); nn[nn == 0] = 1
                dplane = np.abs((A * n).sum(1)) / nn
                rin = max(dplane.min(), 0)          # lower bound of the closest surface point (plane distance)
                rmax[row] = (v.max() / U, rin / U)
            print('      sector %s,%s gate gid %s (row %s): asteroid row %d (%s) centre %.0f m away; LOD-0 L-inf %.0f m, max vertex'
                  ' radius %.0f m, surface planes no closer than %.0f m to its centre -> surface between %.0f and %.0f m'
                  ' of the gate centre depending on orientation (negative: may enclose it)'
                  % (gid[0], gid[1], gid[2], gid[3], row, arows[row][0].rsplit('\\', 1)[-1], dd, rad, rmax[row][0], rmax[row][1],
                     dd - rmax[row][0], dd - rmax[row][1]))
mg = GAME / 'mayhem_galaxies/My/addon/maps/x3_universe.xml'
lo = GAME / 'addon/maps/x3_universe.xml'
if mg.is_file():
    print('  mayhem_galaxies/My map identical to the loose addon/maps map: %s' % (mg.read_bytes() == lo.read_bytes()))

# ---------------------------------------------------------------- summary
print('\n==== summary: winning scene parts with a collision tree (excludes the root testbody, see above)')
for label in ('stock', 'installed'):
    for idx, scene, col0, r, t in summary[label]:
        print('  %-9s rows %-22s %-40s r %6.0f m  %s' % (label, ','.join(map(str, idx)), scene, r / U, 'SOLID (tree)' if t else 'no tree'))
print('RESULT %s (%d of %d EXE byte strings match)' % ('PASS' if not fails else 'FAIL', n_bytes - fails, n_bytes))
