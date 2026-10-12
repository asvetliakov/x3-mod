#!/usr/bin/env python3
"""Static LOD-0 half-extents and beam-to-length ratios of the installed capital ships (M1, M2, M7, TL).

Owning note: docs/reverse-engineering/render-node-bounds.md, section "Ship hull extents at run time".
Engine rules replicated (X3AP.exe fdbf3418..., instruction listings in that section):
  * a ship's root node [obj+0x70] carries TShips column 0 (body 0 'testbody' on every M1/M2/M7 row); the scene
    (TShips column 16, record +0x68 -> obj+0x98) is built under it by 0x0043ce30 / 0x0043d1d0, one child node per
    scene part 'P n; B body; [F parent]', local offset = the part's first key position in engine units;
  * a child's node scale +0x70 = its body's LOD-0 value (0x00487e30), per-axis scale 1.0 (jets aside);
  * LOD record 0 extents +0x20/+0x24/+0x28 = max over its mesh parts of |centre| + half (0x0047f350), with the part
    AABB from the file's 10 bound ints when part flags & 0x10000000 (0x00482d00..0x00482d5f), else from the int16
    (= file >> 2) positions of every referenced vertex, centre = (min+max)*4/2, half = (max-min)*4/2 (0x0047f3f0);
  * world half-extent = ((ext * value + 0x8000) >> 16) * 0x10000 / 65536 (0x004c6c56..0x004c6d1b), units; 500 / m.
Width figures (m), lateral = the ship's x (beam) and y (height); z is the nose axis:
  hull      the largest non-effect, non-dummy part (as scene_radius.py); box e = (ex, ey, ez) about its origin
            plus its offset; rot = its key frame has a non-zero angle (then the rotation-invariant |e| is used)
  lat_box   sqrt(bx^2 + by^2) of the hull box: the roll-invariant lateral radius a runtime LOD-0 read can give
  lat_vtx   max over the hull's LOD-0 vertices of sqrt(x^2 + y^2) (the exact roll-invariant lateral radius)
  all_x/y/z every resolved non-effect part as a box in the root frame (parts under a rotated part: offsets and
            extents bounded by their norms); effect parts (effects\\...) are excluded and counted
  r         the engine collision radius estimate [obj+0xa4] (scene_radius.py, validated there)
Usage: capital_widths.py -> capital_widths.txt and capital_widths.json beside this script. Reads /mnt/c/X3 only.
"""
import json, math, re, statistics, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis')); sys.path.insert(0, str(ROOT / 'verification/results/game-scale'))
from sector_fog_census import Assets, names
import bob1, ships as S, scene_radius as sr

X3 = Path('/mnt/c/X3')
U = 500.0
CLASSES = ('SG_SH_M1', 'SG_SH_M2', 'SG_SH_M7', 'SG_SH_TL')
PART = re.compile(r'^\s*P\s+(\d+)\s*;(.*?)$', re.M)
KEY = re.compile(r'\{\s*0x[0-9a-fA-F]+\s*;((?:\s*-?[\d.eE+-]+\s*;){7})')


def scene_parts(text):
    out = []
    ms = list(PART.finditer(text))
    for i, m in enumerate(ms):
        head = m.group(2)
        body = re.search(r'\bB\s+([^;]+);', head)
        par = re.search(r'\bF\s+(-?\d+)\s*;', head)
        end = ms[i + 1].start() if i + 1 < len(ms) else len(text)
        k = KEY.search(text, m.end(), end)
        v = [float(x) for x in k.group(1).split(';') if x.strip()] if k else [0.0] * 7
        out.append(dict(idx=int(m.group(1)), body=body.group(1).strip() if body else None,
                        parent=int(par.group(1)) if par else -1, pos=tuple(int(x) for x in v[:3]), angle=v[3]))
    return out


_LOD = {}


def lod0(assets, name):
    """(value, ext[3] in 1/65536 of the value, lateral vertex radius in the same units, n_pre, n_parts) or None."""
    if not name:
        return None
    key = name.lower()
    if key in _LOD:
        return _LOD[key]
    res = None
    try:
        e = bob1.resolve_body(assets, ('v/%05d' % int(name)) if name.isdigit() else name)
        data = assets.read_entry(e)
        tree = bob1.parse(data, max_trailing=None)
        ladder = bob1.lods(tree)
        lod = ladder[0]

        def extents(l):
            pts = [(p[1] >> 2, p[2] >> 2, p[3] >> 2) for p in l['points']]   # int16 = file value >> 2 (0x00482640)
            ext, lat, npre = [0, 0, 0], 0.0, 0
            for part in l['parts']:
                if part['flags'] & bob1.PART_PRECOMPUTED and 'bounds' in part:
                    b = part['bounds']; npre += 1
                    c, h = b[4:7], b[7:10]
                else:
                    mn, mx = [0x4000] * 3, [-0x4000] * 3
                    for g in part['groups']:
                        for f in g['faces']:
                            for vi in f[:3]:
                                for k in range(3):
                                    v = pts[vi][k]
                                    mn[k] = min(mn[k], v); mx[k] = max(mx[k], v)
                    c = [int((mn[k] + mx[k]) * 4 / 2) for k in range(3)]
                    h = [int((mx[k] * 4 - mn[k] * 4) / 2) for k in range(3)]
                for k in range(3):
                    ext[k] = max(ext[k], abs(c[k]) + h[k])
                for g in part['groups']:
                    for f in g['faces']:
                        for vi in f[:3]:
                            lat = max(lat, math.hypot(pts[vi][0] * 4, pts[vi][1] * 4))
            return ext, lat, npre
        ext, lat, npre = extents(lod)
        coarse = extents(ladder[-1])[0] if len(ladder) > 1 else ext   # collision LOD of a ship part (0x0043d8dd)
        anyl = list(ext)
        for l in ladder[1:]:
            anyl = [max(u, v) for u, v in zip(anyl, extents(l)[0])]
        res = (lod['value'], ext, lat, npre, len(lod['parts']), coarse, anyl)
    except Exception as ex:          # unresolved name (dummy id, scene reference, missing member)
        res = None
    _LOD[key] = res
    return res


def world(value, e):
    return ((e * value + 0x8000) >> 16) * 0x10000 / 65536.0      # 0x004c6c56..: units


def ship_widths(assets, scene):
    text, src = sr.scene_text(assets, scene)
    if text is None:
        return None
    ps = scene_parts(text)
    by = {p['idx']: p for p in ps}
    out = dict(src=src, parts=len(ps), effects=0, unresolved=0)
    allb = [0.0, 0.0, 0.0]
    hull = None
    for p in ps:
        b = (p['body'] or '')
        bl = b.lower()
        if bl.startswith(('effects\\', 'effects/')):
            out['effects'] += 1
            continue
        L = lod0(assets, b)
        if L is None:
            out['unresolved'] += 1
            continue
        value, ext, lat, npre, nparts, coarse, anyl = L
        e = [world(value, x) for x in ext]
        # chain to the root: exact offsets while no rotated ancestor, norms below one
        exact, slack, rotated, q, depth = [0, 0, 0], 0.0, p['angle'] != 0.0, p, 0
        chain = []
        while q is not None and depth < 32:
            chain.append(q); q = by.get(q['parent']) if q['parent'] >= 0 else None; depth += 1
        rot_above = False
        for q in reversed(chain):            # root side first
            if rot_above:
                slack += math.sqrt(sum(v * v for v in q['pos']))
            else:
                exact = [exact[k] + q['pos'][k] for k in range(3)]
            if q['angle'] != 0.0:
                rot_above = True
        ee = e if not (rotated or rot_above and len(chain) > 1) else [math.sqrt(sum(v * v for v in e))] * 3
        box = [abs(exact[k]) + slack + ee[k] for k in range(3)]
        for k in range(3):
            allb[k] = max(allb[k], box[k])
        is_hull_cand = not ('dummy' in bl or b.isdigit())
        if is_hull_cand and (hull is None or value + max(abs(v) for v in p['pos']) > hull['key']):
            hull = dict(key=value + max(abs(v) for v in p['pos']), name=b, idx=p['idx'], value=value, e=e, box=box,
                        off=exact, rot=bool(rotated or rot_above), lat_vtx=lat * value / 65536.0 + math.hypot(exact[0], exact[1]),
                        pre=npre, nparts=nparts, coarse=[world(value, x) for x in coarse],
                        anyl=[abs(exact[k]) + slack + ([world(value, x) for x in anyl] if not (rotated or rot_above and len(chain) > 1) else [math.sqrt(sum(world(value, x) ** 2 for x in anyl))] * 3)[k] for k in range(3)])
    out['all'] = allb
    out['hull'] = hull
    return out


def main():
    a = Assets(X3)
    head, rows, prov = S.table(a, 'TShips')
    res, _ = names(a)
    lines, recs = [], []
    hdr = ('%-4s %-34s %7s | %-44s %3s %6s %6s %6s %3s | %6s %6s %5s %5s | %6s %6s %6s %5s'
           % ('cls', 'name (TShips index)', 'r m', 'hull body', 'pt', 'ex m', 'ey m', 'ez m', 'rot',
              'latbox', 'latvtx', 'wb/r', 'wv/r', 'all_x', 'all_y', 'all_z', 'wa/r'))
    for i, r in enumerate(rows):
        if len(r) < 53 or r[5] not in CLASSES:
            continue
        rad = sr.radii(a, r[16])
        w = ship_widths(a, r[16])
        nm = res(17, int(r[6])) if r[6].lstrip('-').isdigit() else ''
        rec = dict(index=i, cls=r[5].replace('SG_SH_', ''), name=nm, body0=r[0], scene=r[16],
                   r_m=rad[0] / U if rad else None)
        if w and w['hull'] and rec['r_m']:
            h = w['hull']
            lat_box = math.hypot(h['box'][0], h['box'][1]) / U
            all_lat = math.hypot(w['all'][0], w['all'][1]) / U
            rec.update(hull=h['name'], hull_part=h['idx'], hull_value_m=h['value'] / U,
                       hull_e_m=[round(v / U, 1) for v in h['e']], hull_off_m=[round(v / U, 1) for v in h['off']],
                       hull_rot=h['rot'], hull_precomputed_parts=h['pre'], hull_mesh_parts=h['nparts'],
                       hull_coarse_e_m=[round(v / U, 1) for v in h['coarse']],
                       lat_anylod_m=round(math.hypot(h['anyl'][0], h['anyl'][1]) / U, 1),
                       lat_box_m=round(lat_box, 1), lat_vtx_m=round(h['lat_vtx'] / U, 1),
                       all_m=[round(v / U, 1) for v in w['all']], all_lat_m=round(all_lat, 1),
                       effects=w['effects'], unresolved=w['unresolved'], parts=w['parts'],
                       wb_r=round(lat_box / rec['r_m'], 3), wv_r=round(h['lat_vtx'] / U / rec['r_m'], 3),
                       wa_r=round(all_lat / rec['r_m'], 3), wl_r=round(math.hypot(h['anyl'][0], h['anyl'][1]) / U / rec['r_m'], 3), maxexy_r=round(max(h['box'][0], h['box'][1]) / U / rec['r_m'], 3))
            lines.append('%-4s %-34s %7.0f | %-44s %3d %6.0f %6.0f %6.0f %3s | %6.0f %6.0f %5.2f %5.2f | %6.0f %6.0f %6.0f %5.2f'
                         % (rec['cls'], ('%s (%d)' % (nm, i))[:34], rec['r_m'], h['name'][-44:], h['idx'],
                            h['box'][0] / U, h['box'][1] / U, h['box'][2] / U, 'y' if h['rot'] else '-',
                            lat_box, h['lat_vtx'] / U, rec['wb_r'], rec['wv_r'],
                            w['all'][0] / U, w['all'][1] / U, w['all'][2] / U, rec['wa_r']))
        else:
            lines.append('%-4s %-34s  no scene/hull' % (rec['cls'], '%s (%d)' % (nm, i)))
        recs.append(rec)
    summ = []
    for c in ('M1', 'M2', 'M7', 'TL'):
        rs = [x for x in recs if x['cls'] == c and 'wb_r' in x]
        if not rs:
            continue
        def q(key):
            v = sorted(x[key] for x in rs)
            return '%.2f/%.2f/%.2f' % (v[0], statistics.median(v), v[-1])
        summ.append('%-3s n=%3d  wb/r %s  wl/r %s  wv/r %s  wa/r %s  max(ex,ey)/r %s  hull rotated %d  hull ez/r %s'
                    % (c, len(rs), q('wb_r'), q('wl_r'), q('wv_r'), q('wa_r'), q('maxexy_r'), sum(x['hull_rot'] for x in rs),
                       '%.2f/%.2f/%.2f' % tuple((lambda v: (v[0], statistics.median(v), v[-1]))(sorted(x['hull_e_m'][2] / x['r_m'] for x in rs)))))
    cx = [x for x in recs if 'hull_coarse_e_m' in x]
    over = [x for x in cx if any(x['hull_coarse_e_m'][k] > 1.01 * x['hull_e_m'][k] + 0.5 for k in range(2))]
    summ.append('hulls whose coarsest LOD (the collision LOD of a ship part) exceeds LOD 0 laterally by > 1 %%: %d of %d%s'
                % (len(over), len(cx), (' (' + ', '.join('%s %s>%s' % (x['name'] or x['index'], x['hull_coarse_e_m'][:2], x['hull_e_m'][:2]) for x in over[:6]) + ')') if over else ''))
    unres = sum(1 for x in recs if x.get('unresolved'))
    summ.append('rows with unresolved part bodies (numeric ids outside objects/v, scene references): %d of %d; '
                'precomputed-bounds hulls: %d' % (unres, len(recs), sum(1 for x in recs if x.get('hull_precomputed_parts'))))
    # the amendment-16 adjacent-lane cases: w_A + w_B against 500 m (two ~1.1 km ships) and 523 m (two ~755 m M2s)
    pick = {x['index']: x for x in recs}
    summ.append('pairs (w_A + w_B m as lat_box / lat_anylod / lat_vtx / max(ex,ey)), limit 500 m for the 1.1 km pairs, 523 m for the 755 m pair:')
    for a_, b_, lim in ((11, 11, 500), (11, 10, 502), (53, 53, 500), (269, 269, 523), (276, 276, 523), (252, 252, 523)):
        A, B = pick[a_], pick[b_]
        mx = lambda x: max(x['hull_e_m'][0] + abs(x['hull_off_m'][0]), x['hull_e_m'][1] + abs(x['hull_off_m'][1]))
        summ.append('  %s (r %.0f) + %s (r %.0f): %.0f / %.0f / %.0f / %.0f  vs %d' % (A['name'], A['r_m'], B['name'], B['r_m'],
                    A['lat_box_m'] + B['lat_box_m'], A['lat_anylod_m'] + B['lat_anylod_m'], A['lat_vtx_m'] + B['lat_vtx_m'], mx(A) + mx(B), lim))
    txt = ['# capital_widths.py (static, installed /mnt/c/X3, TShips %s %s)' % (prov['source'], prov['decoded_sha256'][:12]),
           '# per class min/median/max; w = lateral (x, y) radius about the ship origin; r = engine radius estimate',
           *summ, '', hdr, *lines]
    (HERE / 'capital_widths.txt').write_text('\n'.join(txt) + '\n')
    (HERE / 'capital_widths.json').write_text(json.dumps(dict(source=prov, ships=recs), separators=(',', ':')) + '\n')
    print('\n'.join(summ))


if __name__ == '__main__':
    main()
