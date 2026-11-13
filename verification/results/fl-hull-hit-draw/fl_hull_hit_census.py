"""FL hull-hit (impact) effect census (owning note: docs/reverse-engineering/fl/hull-hit-draw.md).

Per TBullets row (FL, root + addon2): the impact column 20 (+0x78, the only impact column: X3FL.exe 0x014550 /
0x01461d read it for whichever side is the projectile; the hit object's class is never read), the Effects row it
names, every element of that row and of its PROPAGATE children (recursively), each element body resolved like the
engine (bob1.resolve_body), its materials (effect file, the engine's effect / technique choice, blend, Z write,
cull, diffuse), a negative diffuse / group index resolved to its types/Animations row (type, frames, total
duration, coordinate stepping), CUT scenes resolved to their part bodies, classic MATERIAL3 texture ids to their
Materials row flags. Then the rows grouped by body. Prints ids, flags, counts and names only; no copyrighted bytes.

    python3 -I verification/results/fl-hull-hit-draw/fl_hull_hit_census.py [--game /mnt/c/X3FL] \
        > verification/results/fl-hull-hit-draw/fl_hull_hit_census_out.txt
"""
import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/effects'))
from sector_fog_census import Assets  # noqa: E402
import bob1  # noqa: E402
import weapon_effects_census as wec  # noqa: E402
import weapon_effects_lab_assets as lab  # noqa: E402

BLEND = wec.BLEND
MPF = {'MPF_ALPHATEST': 1, 'MPF_DESTINATIONBLEND': 2, 'MPF_ALPHABLEND': 4, 'MPF_2SIDED': 0x10, 'MPF_MULTIPLY2X': 0x40,
       'MPF_MULTIPLY': 0x80, 'MPF_NOFILTERING': 0x100, 'MPF_SRCCOLOR': 0x400, 'MPF_BESTQUALITY': 0x8000}
TAT = dict(TAT_LOOP=1, TAT_PINGPONG=2, TAT_ONESHOT=3, TAT_MOVIE=4, TAT_TAGLOOP=5, TAT_TAGPINGPONG=6,
           TAT_TAGONESHOT=7, TAT_TAGCOLLECTION=8, TAT_TAGONESHOT_REINIT=9, TAT_SINGLESTEP=10,
           TAT_TAGSINGLESTEP=11, TAT_TAGARRAYSINGLESTEP=12)
TAT_NAME = {v: k for k, v in TAT.items()}
AFLAG = dict(NULL=0, TADF_COORDS=2, TATF_REINITLOOP=1, TATF_COORDS=2)


def text(a, *keys):
    for key in keys:
        hits = a.entries.get(key.lower())
        if hits:
            return a.read_entry(hits[-1]).decode('latin1'), hits[-1]['source']
    raise FileNotFoundError(keys)


def anim_rows(a):
    data, src = text(a, 'addon2/types/animations.txt', 'types/animations.txt')
    toks = []
    for line in data.splitlines():
        toks += [t.strip() for t in line.split('//', 1)[0].split(';')]
    t = iter([x for x in toks if x])

    def flags(tok):
        v = 0
        for p in tok.split('|'):
            v |= AFLAG[p.strip()]
        return v
    rows = []
    for _ in range(int(next(t))):
        typ = TAT[next(t)]
        fl = flags(next(t))
        row = dict(type=typ, flags=fl, first=next(t), second=next(t), frames=[])
        if fl & 2:
            row['coords'] = [float(next(t)) for _ in range(4)]
        if typ == 4:
            row['movie'] = [int(next(t)) for _ in range(7)]
        else:
            for _ in range(int(next(t))):
                if typ == 8:
                    row['frames'].append(dict(name=next(t), dur=None, coords=None)); [next(t) for _ in range(6)]
                elif typ == 11:
                    row['frames'].append(dict(name=next(t), dur=None, coords=None))
                else:
                    ef = flags(next(t))
                    name, dur = next(t), int(next(t))
                    co = (float(next(t)), float(next(t))) if ef & 2 else None
                    row['frames'].append(dict(name=name, dur=dur, coords=co))
            row['total'] = int(next(t))
        rows.append(row)
    return rows, src


def anim_text(rows, n):
    if n >= len(rows):
        return f'Animations row {n}: outside the {len(rows)} rows'
    r = rows[n]
    names = list(dict.fromkeys(f['name'] for f in r['frames']))
    co = sum(1 for f in r['frames'] if f['coords'] is not None)
    durs = sorted({f['dur'] for f in r['frames'] if f['dur'] is not None})
    cs = sorted({f['coords'] for f in r['frames'] if f['coords'] is not None})
    grid = ''
    if cs:
        us, vs = sorted({c[0] for c in cs}), sorted({c[1] for c in cs})
        grid = f', cell offsets u {us} v {vs}'
    return (f'Animations row {n}: {TAT_NAME[r["type"]]} flags {r["flags"]}, {len(r["frames"])} frames '
            f'({co} with TATF_COORDS{grid}), frame ms {durs}, total {r.get("total")} ms, textures {names}')


def materials_rows(a):
    data, src = text(a, 'addon2/types/materials.txt', 'types/materials.txt')
    lines = [l for l in data.splitlines() if l.strip() and not l.lstrip().startswith('/')]
    rows = []
    for line in lines[1:int(lines[0].split(';')[0]) + 1]:
        f = [x.strip() for x in line.split(';')]
        fl = 0
        for tok in f[15].split('|'):
            fl |= MPF.get(tok.strip(), 0)
        rows.append(dict(texid=int(f[12], 0), flags=fl, flagtext=f[15], name=f[28] if len(f) > 28 else ''))
    return rows, src


def effect_choice(effect, cull):
    """0x0e175a..0x0e17e0: standard_lighting / max_standard_lighting -> standard_lighting, else the file's own
    effect; g_CullMode 1 (semantic 0x11 == 1, 0x0e1677) appends '2s'; technique DEFAULT unless a bump id > 0
    (0x0e1865..0x0e18ad)."""
    stem = effect.lower().removesuffix('.fx')
    if stem in ('standard_lighting', 'max_standard_lighting'):
        stem = 'standard_lighting'
    return stem + ('2s' if cull == 1 else '') + ' DEFAULT'


def describe_body(a, name, anim, mats, cache):
    if name in cache:
        return cache[name]
    try:
        e = bob1.resolve_body(a, name)
    except FileNotFoundError:
        cache[name] = dict(text=f'{name}: no body resource', key=name)
        return cache[name]
    data = a.read_entry(e)
    src = f'{e["source"]}:{e["path"]}'
    out = dict(key=name, src=src, groups=[], parts=[])
    if data[:4] == b'CUT1' or (data[:1] == b'/' and b'P ' in data[:4000] and b'MATERIAL' not in data[:20000]):
        out['scene'] = True
    if out.get('scene'):
        cache[name] = out
        return out
    lines = []
    try:
        tree = bob1.parse(data)
        lod0 = bob1.lods(tree)[0]
        groups = [(g['material'], len(g['faces'])) for p in lod0['parts'] for g in p['groups']]
        out['groups'] = groups
        pts = [p for p in lod0['points'] if p[0] & 1]
        uv = [p[4:6] for p in lod0['points'] if p[0] & 2] if pts else []
        uvr = ''
        if uv:
            us, vs = [x[0] / 65536 for x in uv], [x[1] / 65536 for x in uv]
            uvr = f', UV u {min(us):.3f}..{max(us):.3f} v {min(vs):.3f}..{max(vs):.3f}'
        half = wec.extents(lod0)['half_extents'] if pts else None
        lines.append(f'LOD0 scale {lod0["value"]}, {len(pts)} points, half extents {half}{uvr}, '
                     f'{len(bob1.lods(tree))} LODs, groups (material, faces) {groups}')
        for m in bob1.materials(tree):
            if 'params' in m:
                p = {wec._str(n).lower(): v for n, _, v in m['params']}
                on = p.get('g_alphablendenable', [0])[0]
                bl = f'{BLEND.get(p.get("g_srcblend", [0])[0], "?")}/{BLEND.get(p.get("g_destblend", [0])[0], "?")}' if on else 'off'
                cull = p.get('g_cullmode', [None])[0]
                diff = wec._str(p.get('t_diffusetexture', b''))
                ta = [k for k in ('texanimduration', 'texanimrotation') if p.get(k, [0])[0]]
                lines.append(f'mat {m["index"]} effect {wec._str(m["effect"])} (file technique word {m.get("technique")}) '
                             f'-> {effect_choice(wec._str(m["effect"]), cull)}; blend {bl}, zwrite {p.get("g_zwriteenable", ["-"])[0]}, '
                             f'zenable {p.get("g_zenable", ["-"])[0]}, cull {cull}, g_AlphaValue {p.get("g_alphavalue", ["-"])[0]}, '
                             f'diffuse {diff!r}, TexAnim params {ta or "none"}')
                mm = re.match(r'-(\d+)', diff or '')
                if mm:
                    lines.append('  ' + anim_text(anim, int(mm.group(1))))
            else:
                tex = wec._str(m.get('texture'))
                fl = m.get('flags', m.get('flagword')) or 0
                bits = '|'.join(k for k, v in MPF.items() if fl & v) or '0'
                lines.append(f'mat {m["index"]} classic (body record) texture {tex!r}, flags {fl:#x} ({bits}) -> classic '
                             f'standard_lighting{"2s" if fl & 0x10 else ""} DEFAULT')
        for gi, _ in groups:
            if gi < 0:
                lines.append('  negative group index ' + anim_text(anim, -gi))
    except bob1.FormatError:
        # MATERIAL3 text body: MATERIALn: index; texture id; ... (bob1 refuses MATERIAL3)
        s = data.decode('latin1')
        for t, idx, tex in re.findall(r'^\s*MATERIAL(\d)\s*:\s*(-?\d+)\s*;\s*(-?\d+)\s*;', s, re.M):
            tex = int(tex)
            if tex < 0:
                lines.append(f'MATERIAL{t} {idx}: texture id {tex} -> ' + anim_text(anim, -tex))
                continue
            r = mats[tex] if tex < len(mats) else None
            lines.append(f'MATERIAL{t} {idx}: texture id {tex} -> Materials row {tex} flags {r["flags"]:#x} '
                         f'({r["flagtext"]}), texid {r["texid"]}, name {r["name"]!r} -> classic standard_lighting'
                         f'{"2s" if r["flags"] & 0x10 else ""} DEFAULT' if r else f'MATERIAL{t} {idx}: texture id {tex}: no row')
        try:
            tree = bob1.parse(wec.MATERIAL3.sub(b'', data))
            lod0 = bob1.lods(tree)[0]
            pts = [p for p in lod0['points'] if p[0] & 1]
            groups = [(g['material'], len(g['faces'])) for p in lod0['parts'] for g in p['groups']]
            lines.insert(0, f'LOD0 scale {lod0["value"]}, {len(pts)} points, half extents '
                            f'{wec.extents(lod0)["half_extents"] if pts else None}, groups (material, faces) {groups}')
            for gi, _ in groups:
                if gi < 0:
                    lines.append('  negative group ' + anim_text(anim, -gi))
        except bob1.FormatError as err:
            lines.insert(0, f'geometry not parsed ({str(err)[:60]})')
    out['lines'] = lines
    cache[name] = out
    return out


def kind_of(el):
    names = el['flag_names']
    if 'EEDF_PROPAGATE' in names:
        return 'PROPAGATE'
    if 'EEDF_LIGHT' in names:
        return 'LIGHT'
    if 'EEDF_CUT' in names:
        return 'CUT'
    if 'EEDF_DISTANT' in names:
        return 'DISTANT'
    return 'BODY' if el.get('bodyid') not in (None, -1, 0) else 'EMPTY'


def body_name(bodyid):
    return wec.element_body_name(bodyid)


def run(game):
    a = Assets(pathlib.Path(game), edition='fl')
    tbt, tsrc = text(a, 'addon2/types/tbullets.txt', 'types/tbullets.txt')
    tb = wec.parse_tbullets(tbt)
    et, esrc = text(a, 'addon2/types/effects.txt', 'types/effects.txt')
    eff = wec.parse_effects(et)
    anim, asrc = anim_rows(a)
    mats, msrc = materials_rows(a)
    print(f'FL {game}: TBullets {tsrc} ({len(tb)} rows), Effects {esrc} ({len(eff)} rows), Animations {asrc} '
          f'({len(anim)} rows), Materials {msrc} ({len(mats)} rows)')
    impact = collections.OrderedDict()
    for w in tb:
        if w['impact_effect'] and w['impact_effect'] > 0:
            impact.setdefault(w['impact_effect'], []).append(f'{w["index"]}:{w["weapon_id"]}')
    none = [f'{w["index"]}:{w["weapon_id"]}' for w in tb if not (w['impact_effect'] and w['impact_effect'] > 0)]
    print(f'impact rows: {len(impact)} distinct; rows without an impact effect: {none or "none"}')
    cache, by_body, by_scene = {}, collections.defaultdict(set), collections.defaultdict(set)
    hulldmg = []

    def walk(rid, users, depth, seen):
        row = eff.get(rid)
        pad = '    ' * depth
        if row is None:
            print(f'{pad}row {rid}: missing')
            return
        print(f'{pad}row {rid} (EDF {row["effectflags"]}, header {row["header_extra"]}, {row["numelements"]} elements)'
              + (f' <- {", ".join(users)}' if users else ''))
        for k, el in enumerate(row['elements']):
            kd = kind_of(el)
            fl = '|'.join(x.replace('EEDF_', '') for x in el['flag_names'])
            if 'EEDF_HULLDAMAGE' in el['flag_names']:
                hulldmg.append(rid)
            if kd == 'PROPAGATE':
                print(f'{pad}  el{k} PROPAGATE [{fl}|{el.get("i3dflags")}] -> row {el["prop_effect_id"]} x{el["min_props"]}..{el["max_props"]}, '
                      f'gap {el["min_prop_delay"]}..{el["max_prop_delay"]} ms, length {el["prop_length"]}, delay {el["prop_delay"]}, steps {el["prop_steps"]}')
                if el['prop_effect_id'] not in seen:
                    walk(el['prop_effect_id'], None, depth + 1, seen | {el['prop_effect_id']})
                continue
            base = (f'{pad}  el{k} {kd} {el.get("bodyid")} [{fl}|{el.get("i3dflags")}] size {el.get("minsize")}..{el.get("maxsize")}'
                    f' rnd {el.get("rndsize")}, {el.get("duration")} ms from {el.get("timeoffset")} ms, offset '
                    f'{el.get("xoffset")}/{el.get("yoffset")}/{el.get("zoffset")}')
            print(base)
            if kd == 'BODY':
                d = describe_body(a, body_name(el['bodyid']), anim, mats, cache)
                by_body[d['key']].add(rid)
                print(f'{pad}      -> body {d["key"]}' + (' (resolves to a scene)' if d.get('scene') else ''))
            elif kd == 'CUT':
                sid = el['bodyid']
                by_scene[sid].add(rid)
                try:
                    se = bob1.resolve_body(a, 'cut\\%05d' % sid)
                    parts = lab.scene_parts(a.read_entry(se))
                    toks = [p[0] for p in parts]
                    print(f'{pad}      -> scene {se["source"]}:{se["path"]}: {len(parts)} parts, distinct bodies '
                          f'{sorted(set(toks), key=lambda x: (not x.isdigit(), int(x) if x.isdigit() else 0, x))}')
                    for tok in dict.fromkeys(toks):
                        nm = body_name(int(tok)) if tok.isdigit() else tok
                        d = describe_body(a, nm, anim, mats, cache)
                        by_body[d['key']].add(rid)
                except FileNotFoundError:
                    print(f'{pad}      scene cut\\{sid:05d}: no resource')

    for rid, users in impact.items():
        walk(rid, users, 0, {rid})
    print(f'impact rows (and children) with an EEDF_HULLDAMAGE element: {sorted(set(hulldmg)) or "none"}')
    # rows by body: which impact rows (direct or child) draw each body, and which TBullets rows reach them
    def reach(rid, acc):
        acc.add(rid)
        for el in eff.get(rid, {}).get('elements', []):
            if 'EEDF_PROPAGATE' in el['flag_names'] and el['prop_effect_id'] not in acc:
                reach(el['prop_effect_id'], acc)
        return acc
    users_of = collections.defaultdict(set)
    for rid, users in impact.items():
        for r in reach(rid, set()):
            users_of[r].update(users)
    print('bodies by TBullets rows reached (direct element, scene part or PROPAGATE child):')
    table = []
    for key, rows in by_body.items():
        u = sorted({x for r in rows for x in users_of[r]}, key=lambda s: int(s.split(':')[0]))
        table.append((len(u), key, sorted(rows), u))
    for n, key, rows, u in sorted(table, key=lambda t: (-t[0], t[1])):
        print(f'  {key}: {n} TBullets rows via Effects rows {rows}: {", ".join(u)}')
    print(f'distinct bodies {len(by_body)}; scenes {sorted(by_scene)}')
    # sharing: every Effects row whose element (or CUT scene part) draws one of these bodies, and who names that row
    names = collections.defaultdict(list)
    for w in tb:
        for col, key in ((19, 'flight_effect'), (20, 'impact_effect'), (21, 'muzzle_effect')):
            if w[key] and w[key] > 0:
                names[w[key]].append(f'TBullets c{col}:{w["index"]}')
    for tname, cols in (('TMissiles', (20, 21)), ('TShips', (47,)), ('TDocks', (14,)), ('TFactories', (14,)),
                        ('TAsteroids', (8,))):
        try:
            tt, _ = text(a, f'addon2/types/{tname}.txt', f'types/{tname}.txt')
        except FileNotFoundError:
            continue
        for i, f in enumerate(wec.data_rows(tt)[1:]):
            for c in cols:
                if len(f) > c:
                    v = wec._num(f[c])
                    v = wec.DEFAULT_DEATH_ROW if (v == 0 and c != 20) else v
                    if isinstance(v, int) and v > 0:
                        names[v].append(f'{tname} c{c}:{i}')
    child_of = collections.defaultdict(set)
    for rid, row in eff.items():
        for el in row['elements']:
            if 'EEDF_PROPAGATE' in el['flag_names']:
                child_of[el['prop_effect_id']].add(rid)
    scene_cache = {}

    def element_bodies(el):
        kd = kind_of(el)
        if kd == 'BODY':
            return {describe_body(a, body_name(el['bodyid']), anim, mats, cache)['key']}
        if kd == 'CUT':
            sid = el['bodyid']
            if sid not in scene_cache:
                try:
                    se = bob1.resolve_body(a, ('cut\\%05d' % sid) if isinstance(sid, int) else sid)
                    toks = dict.fromkeys(p[0] for p in lab.scene_parts(a.read_entry(se)))
                    scene_cache[sid] = {body_name(int(t)) if t.isdigit() else t for t in toks}
                except (FileNotFoundError, bob1.FormatError):
                    scene_cache[sid] = set()
            return scene_cache[sid]
        return set()
    impact_closure = set()
    for rid in impact:
        impact_closure |= reach(rid, set())
    print('every Effects row drawing one of these bodies (directly or as a CUT part), and who names the row '
          '(TBullets c19 flight / c20 impact / c21 muzzle, TMissiles c20 impact / c21 death, TShips c47, TDocks and '
          'TFactories c14, TAsteroids c8 death; 0 read as the default death row 48; PROPAGATE parents in brackets):')
    target = set(by_body)
    for rid in sorted(eff):
        hit = sorted({b for el in eff[rid]['elements'] for b in element_bodies(el)} & target)
        if not hit:
            continue
        tag = 'impact' if rid in impact_closure else 'OTHER'
        par = sorted(child_of.get(rid, ()))
        grp = collections.OrderedDict()
        for n in names.get(rid, []):
            k, i = n.split(':')
            grp.setdefault(k, []).append(i)
        nb = '; '.join(f'{k} x{len(v)} ({",".join(v[:8])}{",..." if len(v) > 8 else ""})' for k, v in grp.items())
        print(f'  [{tag}] row {rid}: {", ".join(hit)}; named by {nb or "none"}'
              + (f'; child of {par}' if par else ''))
    print('bodies (resolved once):')
    for key in sorted(by_body):
        d = cache[key]
        print(f'  {key}: {d.get("src", d.get("text"))}' + (' (a scene)' if d.get('scene') else ''))
        for line in d.get('lines', []):
            print(f'      {line}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default='/mnt/c/X3FL')
    run(ap.parse_args().game)


if __name__ == '__main__':
    main()
