#!/usr/bin/env python3
"""Read-only census of ship engine effects: stock X3AP layers vs the installed Mayhem 3 tree (bottle X3).

Views: `stock` = sector_fog_census.STOCK_AP_CATALOGUES (01..13 + addon/01..04, no loose files);
`mayhem` = every installed catalogue and loose file except the x3m LOD overlay slots (lod_overlay.original_assets).
No game launch, no Wine, nothing written into the game tree; outputs go next to this script.

Chains followed per TShips row (columns 0-based, the X3 Editor 2 TShips layout):
  col 16 ship scene -> scene parts whose body is a types/Bodies SBTYPE_JET body (the drawn engine glows,
         objects/effects/engines/*) -> body geometry, materials, diffuse texture colour;
  col 11 engine effect -> types/Effects.txt effect (elements: body id, lens flare, light);
  col 12 engine glow (named so by X3 Editor; values unresolved, see README);
  col 49 engine trail -> types/Particles3.txt particle generator (material id -> types/Materials);
  col 52 class, col 45 race, col 46 hull, col 7/8 speed/acceleration.

  python3 verification/results/engine-effects-census/engine_effects_census.py
"""
import collections
import colorsys
import csv
import hashlib
import json
import re
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import bob1  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402

GAME = bob1.DEFAULT_GAME
RACE = {1: 'Argon', 2: 'Boron', 3: 'Split', 4: 'Paranid', 5: 'Teladi', 6: 'Xenon', 7: "Kha'ak", 8: 'Pirate',
        9: 'Goner', 12: 'race12', 13: 'race13', 14: 'race14', 15: 'race15', 16: 'race16', 17: 'ATF',
        18: 'Terran', 19: 'Yaki'}
COL = dict(subtype=5, name=6, speed=7, accel=8, effect=11, glow=12, scene=16, race=45, hull=46, trail=49, cls=52)
PART = re.compile(rb'^P (\d+); B ([^;]+);([^\n]*)$', re.M)
FRAME = re.compile(rb'\{([^}]*)\}')
ENGINE_MAT = re.compile(r'fx_engine|engine_glow|thrust|exhaust|nozzle', re.I)
TRAIL_NAME = re.compile(r'trail|streak|exhaust|contrail|plume|thrust|afterburn', re.I)


def norm(name):
    return name.strip().replace('\\', '/').lower()


def text(assets, path):
    data, prov = assets.get(path)
    return data.decode('latin1'), prov


# ---------------------------------------------------------------- type files

def tships(assets):
    t, prov = text(assets, 'types/TShips.txt')
    lines = [l for l in t.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    count = int(lines[0].split(';')[1])
    rows = []
    for i, line in enumerate(lines[1:count + 1]):
        f = line.split(';')
        ident = [x for x in f if x.strip()][-1].strip()
        rows.append(dict(index=i, id=ident, **{k: f[c].strip() for k, c in COL.items()}))
    if len(rows) != count:
        raise ValueError(f'TShips: {len(rows)} rows, header says {count}')
    return rows, prov


def jet_bodies(assets):
    """types/Bodies SBTYPE_JET entries (names normalised, numeric ids as v/NNNNN)."""
    t, _ = text(assets, 'types/Bodies.txt')
    t = re.sub(r'//[^\n]*', '', t)
    m = re.search(r'SBTYPE_JET;(\d+);(.*?)(?=SBTYPE_|\Z)', t, re.S)
    toks = [x.strip() for x in m.group(2).split(';') if x.strip()]
    out = set()
    for tok in toks:
        out.add(f'v/{int(tok):05d}' if re.fullmatch(r'\d+', tok) else norm(tok))
    return int(m.group(1)), len(toks), out


def num(tok):
    try:
        return int(tok, 16) if tok.lower().startswith('0x') else int(tok) if re.fullmatch(r'-?\d+', tok) else float(tok)
    except ValueError:
        return tok


def effects(assets):
    """Effects.txt: id -> dict(comment, elements=[dict(flags, i3d, body, lensflare, minsize, maxsize, offset)]).
    After an effect header the non-comment lines pair up as (flags line, values line), n elements."""
    t, _ = text(assets, 'types/Effects.txt')
    out, cur, lines = {}, None, []

    def close():
        if cur is None:
            return
        for fl, v in zip(lines[0::2], lines[1::2]):
            if len(cur['elements']) >= cur['n']:
                break
            el = dict(flags=fl[0], i3d=fl[1] if len(fl) > 1 else '')
            if 'EEDF_PROPAGATE' in el['flags']:
                el.update(prop_effect=v[0])
            else:
                el.update(body=num(v[0]), minsize=num(v[1]), maxsize=num(v[2]),
                          offset=[num(x) for x in v[5:8]], lensflare=num(v[8]) if len(v) > 8 else None)
            cur['elements'].append(el)
    for raw in t.splitlines():
        code, _, comment = raw.partition('//')
        m = re.match(r'\s*(\d+);(\d+);(\d+);(\d+);([\d.]+);\s*$', code)
        if m:
            close()
            cur = out[int(m.group(1))] = dict(n=int(m.group(2)), comment=comment.strip(), elements=[])
            lines = []
            continue
        toks = [x.strip() for x in code.split(';') if x.strip()]
        if cur is not None and toks:
            lines.append(toks)
    close()
    return out


def particles(assets):
    """Particles3: id -> dict(comment, emitters=[dict(flags, mat, size)])."""
    t, _ = text(assets, 'types/Particles3.txt')
    out, cur = {}, None
    for raw in t.splitlines():
        code, _, comment = raw.partition('//')
        toks = [x.strip() for x in code.split(';')]
        if toks[0].isdigit() and len(toks) >= 3 and toks[1].isdigit() and not toks[2].startswith('PEDF'):
            cur = out[int(toks[0])] = dict(comment=comment.strip(), emitters=[])
            continue
        if cur is not None and toks[0].startswith('PEDF'):
            cur['emitters'].append(dict(flags=toks[0], mat=int(toks[1]), uv=tuple(float(x) for x in toks[2:6]),
                                        lifetime=float(toks[13]),
                                        size=(float(toks[14]), float(toks[15]))))
    return out


def lensflares(assets):
    """types/LensFlares: [[(body id, position, min size, max size, type)]] in file order."""
    t, _ = text(assets, 'types/LensFlares.txt')
    toks = [x.strip() for line in t.splitlines() for x in line.split('/', 1)[0].split(';') if x.strip()]
    it = iter(toks)
    out = []
    for _ in range(int(next(it))):
        n = int(next(it))
        out.append([(num(next(it)), float(next(it)), float(next(it)), float(next(it)), int(next(it)))
                    for _ in range(n)])
    return out


def numbered_colour(assets, ident):
    """(hex mean, hex peak, cluster, diffuse) of body v/NNNNN's tint material, or the error text."""
    name = 'v\\%05d' % ident if isinstance(ident, int) else ident
    try:
        b = body_info(assets, name)
    except Exception as exc:
        try:                        # MATERIAL3 text body: texture id + ambient/diffuse colour triples, raw
            data = assets.read_entry(bob1.resolve_body(assets, name)).decode('latin1', 'replace')
            m = re.search(r'MATERIAL3:\s*\d+;\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);\s*(\d+);',
                          data)
            tex = m.group(1)
            c = colour(assets, tex.encode())
            return dict(body=ident, material3_texture_id=int(tex), material3_ambient=m.group(2, 3, 4),
                        material3_diffuse=m.group(5, 6, 7), mean=hexrgb(c['mean']) if c else None,
                        amean=hexrgb(c['amean']) if c else None, peak=hexrgb(c['peak']) if c else None,
                        cluster=cluster(c['amean']) if c else None, texture=c['members'][0] if c else None)
        except Exception as exc2:
            return dict(body=ident, error=f'{str(exc)[:40]} / {str(exc2)[:40]}')
    m = primary(b)
    if m is None:
        return dict(body=ident, diffuse=b['materials'][0]['diffuse'] if b['materials'] else None, colour=None)
    c = m['colour']
    return dict(body=ident, diffuse=m['diffuse'], blend=m['blend'], classic_diffuse_rgb=m['classic_diffuse_rgb'],
                mean=hexrgb(c['mean']), peak=hexrgb(c['peak']),
                cluster=cluster(c['mean']))


# ---------------------------------------------------------------- scenes

def scene_member(assets, ref):
    stem = bob1.body_stem(ref)
    for ext in ('.bod', '.bob'):
        if assets.candidates(stem + ext):
            return assets.get(stem + ext)
    raise FileNotFoundError(stem)


def scene_parts(data):
    parts = list(PART.finditer(data))
    out = []
    for i, m in enumerate(parts):
        end = parts[i + 1].start() if i + 1 < len(parts) else len(data)
        frames = []
        for f in FRAME.finditer(data[m.end():end]):
            v = [x.strip() for x in f.group(1).decode('latin1').split(';') if x.strip()]
            frames.append(v)
        c = re.search(rb'\bC (\d+)', m.group(3))
        out.append(dict(index=int(m.group(1)), body=m.group(2).decode('latin1').strip(),
                        c=int(c.group(1)) if c else None, frames=frames))
    return out


def body_key(ref):
    r = norm(ref)
    return f'v/{int(r):05d}' if re.fullmatch(r'\d+', r) else r


# ---------------------------------------------------------------- bodies and textures

def load_body(assets, name):
    e = bob1.resolve_body(assets, name)
    data = assets.read_entry(e)
    return bob1.parse(data, None), e, hashlib.sha256(data).hexdigest(), data


def extent(record):
    """bbox in scene units (normalised points x LOD0 value / 65536; the scaling is inferred, bob1 notes)."""
    p = np.array([q[1:4] for q in record['points']], np.float64)
    s = record['value'] / 65536.0
    lo, hi = p.min(0) * s, p.max(0) * s
    return lo, hi


def visible_extent(record, share=0.001):
    """bbox (scene units) of the points of faces whose area is at least `share` of the largest face: drops the
    degenerate pin triangles some glow bodies carry to make their bounds symmetric."""
    p = np.array([q[1:4] for q in record['points']], np.float64) * (record['value'] / 65536.0)
    faces = np.array([f[:3] for part in record['parts'] for g in part['groups'] for f in g['faces']], np.int64)
    if not len(faces):
        return None, None
    a, b, c = p[faces[:, 0]], p[faces[:, 1]], p[faces[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    keep = faces[area >= share * area.max()]
    q = p[np.unique(keep)]
    return q.min(0), q.max(0)


def text_extent(data):
    t = data.decode('latin1', 'replace')
    v = re.findall(r'(?m)^\s*(-?\d+);\s*(-?\d+);\s*(-?\d+);\s*(?:/[^\n]*)?$', t)
    if not v:
        return None
    a = np.array(v, np.float64)
    return a.min(0), a.max(0)


def param(m, name):
    for n, typ, val in m.get('params', []):
        if n.decode('latin1').lower() == name.lower():
            return val.decode('latin1') if typ == 8 else val[0]
    return None


def texture_frames(assets, name):
    """[(texture name, entry info)] the engine draws for a diffuse name: an animation row gives all its frames."""
    s = lod_atlas.strip_texture_name(name)   # str
    if s is None:
        return []
    if s[:1] == '-' and re.match(r'-\d+', s):
        rows = lod_atlas.animation_rows(assets)
        row = rows[int(re.match(r'-(\d+)', s).group(1))]
        names = [f[1] for f in row['frames']] or [row['first']]
    else:
        names = [name]
    return names


COLOUR_CACHE = {}


def colour(assets, name):
    """dict(mean, amean, peak, p95, size, member, placeholder) of level 0, mean over animation frames."""
    key = (id(assets), name)
    if key in COLOUR_CACHE:
        return COLOUR_CACHE[key]
    stats, members = [], []
    for fname in texture_frames(assets, name.decode('latin1')):
        src = lod_atlas.texture_source(assets, fname.encode('latin1'))
        if src is None:
            continue
        data, kind, info = src
        img = lod_atlas.decode_dds(data, 0) if kind == 'dds' else lod_atlas.decode_image(data, fname)
        px = img.reshape(-1, 4).astype(np.float64)
        rgb, a = px[:, :3], px[:, 3] / 255.0
        luma = (rgb * np.array([0.2126, 0.7152, 0.0722])).sum(1)
        order = np.argsort(luma)
        top = order[int(0.95 * len(order)):]
        stats.append(dict(mean=rgb.mean(0), amean=(rgb * a[:, None]).sum(0) / max(a.sum(), 1e-9),
                          peak=rgb[order[-1]], p95=rgb[top].mean(0), alpha_mean=a.mean(),
                          size=f'{img.shape[1]}x{img.shape[0]}'))
        members.append(f'{info["source"]}:{info["member"]}' + (f' [placeholder {info["placeholder"]}]'
                                                                  if info['placeholder'] else ''))
    if not stats:
        out = None
    else:
        out = dict(mean=np.mean([s['mean'] for s in stats], 0), amean=np.mean([s['amean'] for s in stats], 0),
                   peak=np.max([s['peak'] for s in stats], 0), p95=np.mean([s['p95'] for s in stats], 0),
                   alpha_mean=float(np.mean([s['alpha_mean'] for s in stats])), size=stats[0]['size'],
                   frames=len(stats), members=members)
    COLOUR_CACHE[key] = out
    return out


def cluster(rgb):
    r, g, b = (float(x) / 255 for x in rgb)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if v < 0.04:
        return 'black'
    if s < 0.18:
        return 'white/grey'
    h *= 360
    for limit, label in ((15, 'red'), (40, 'orange'), (70, 'yellow'), (165, 'green'), (200, 'cyan'),
                         (255, 'blue'), (290, 'purple'), (335, 'magenta'), (360, 'red')):
        if h < limit:
            return label


def hexrgb(rgb):
    return '#%02x%02x%02x' % tuple(int(round(min(255, max(0, x)))) for x in rgb)


def body_info(assets, name):
    tree, e, sha, data = load_body(assets, name)
    rec = bob1.lods(tree)[0]
    mats = bob1.materials(tree)
    lo, hi = extent(rec)
    faces = collections.Counter()
    for part in rec['parts']:
        for g in part['groups']:
            faces[g['material']] += len(g['faces'])
    mrows = []
    for mi, n in faces.most_common():
        if mi < 0:      # animation group: the material whose diffuse names -N (texture-lookup.md section 10)
            m = next((x for x in mats if 'params' in x and param(x, 't_DiffuseTexture') and
                      re.match(r'%d\b' % mi, param(x, 't_DiffuseTexture'))), mats[0] if mats else {})
        else:
            m = mats[mi] if 0 <= mi < len(mats) else {}
        if 'params' in m:
            eff = m['effect'].decode('latin1')
            diffuse = param(m, 't_DiffuseTexture') or ''
            blend = (f"{param(m, 'g_SrcBlend')}/{param(m, 'g_DestBlend')}" if param(m, 'g_AlphaBlendEnable')
                     else 'off')
            zw = param(m, 'g_ZWriteEnable')
        else:
            eff, diffuse = '(classic)', (m.get('texture', b'') or b'').decode('latin1') if isinstance(
                m.get('texture', b''), bytes) else str(m.get('texture', ''))
            blend, zw = f'classic flags {m.get("flags", "?")}', None
        c = None
        try:
            c = colour(assets, diffuse.encode('latin1')) if diffuse else None
            err = ''
        except Exception as exc:          # unresolvable name: recorded, not fatal
            err = str(exc)[:80]
        hsbc = ' '.join(str(round(param(m, k) / 65536, 3)) if isinstance(param(m, k), int) else '-'
                        for k in ('g_Hue', 'g_Saturation', 'g_Brightness', 'g_Contrast')) if 'params' in m else ''
        crgb = ('#%02x%02x%02x' % tuple(min(255, x) for x in m['colors'][3:6])) if 'colors' in m else ''
        mrows.append(dict(classic_diffuse_rgb=crgb, hsbc=hsbc, group_material=mi, faces=n, effect=eff, blend=blend, zwrite=zw, diffuse=diffuse,
                          colour=c, error=err))
    te = text_extent(data) if not data.startswith(b'BOB1') else None
    vlo, vhi = visible_extent(rec)
    return dict(source=e['source'], member=e['path'], sha=sha[:12], binary=data.startswith(b'BOB1'), vlo=vlo, vhi=vhi,
                lods=len(bob1.lods(tree)), value=rec['value'], points=len(rec['points']),
                lo=lo, hi=hi, text_lo=te[0] if te else None, text_hi=te[1] if te else None, materials=mrows)


# ---------------------------------------------------------------- census

def view_census(label, assets):
    ships, tprov = tships(assets)
    jet_declared, jet_listed, jets = jet_bodies(assets)
    eff = effects(assets)
    par = particles(assets)
    scenes, scene_err, binary_scenes = {}, collections.Counter(), 0
    engine_refs = collections.Counter()
    under_engines_not_jet, jet_not_under = collections.Counter(), collections.Counter()
    hull_cache = {}
    for s in ships:
        try:
            data, prov = scene_member(assets, s['scene'])
        except FileNotFoundError:
            scene_err['missing'] += 1
            s['parts'] = None
            continue
        if data[:4] == b'CUT1':
            binary_scenes += 1
            s['parts'] = None
            continue
        parts = scene_parts(data)
        s['scene_source'] = prov['source']
        eng = []
        for p in parts:
            k = body_key(p['body'])
            is_jet = k in jets
            under = k.startswith('effects/engines/')
            if under and not is_jet:
                under_engines_not_jet[k] += 1
            if is_jet and not under:
                jet_not_under[k] += 1
            if is_jet or under:
                f0 = p['frames'][0] if p['frames'] else []
                fl = int(f0[0], 0) if f0 else 0
                pos = [float(x) for x in f0[1:4]] if fl & 2 and len(f0) >= 4 else None
                eng.append(dict(body=k, c=p['c'], role=role(k, p['c']), pos=pos, flags=sorted({fr[0] for fr in p['frames']}),
                                nframes=len(p['frames']), fields=sorted({len(fr) for fr in p['frames']}),
                                rot=[float(x) for x in f0[4:8]] if pos else None))
                engine_refs[k] += 1
        s['engine_parts'] = eng
        hull = next((p for p in parts if not re.search(r'props|dummy|^effects|^-1$|^\\d+$|animprops|cockpit',
                                                       norm(p['body']))), None)
        s['hull_ref'] = body_key(hull['body']) if hull else None
        h = hull['body'] if hull else None
        if h and h not in hull_cache:
            try:
                tree, e, sha, _ = load_body(assets, h)
                rec = bob1.lods(tree)[0]
                lo, hi = extent(rec)
                glow_mats = 0
                for m in bob1.materials(tree):
                    names = ' '.join([m.get('effect', b'').decode('latin1')] +
                                     [v.decode('latin1') for _, t, v in m.get('params', []) if t == 8] +
                                     [str(m.get('texture', ''))])
                    if ENGINE_MAT.search(names):
                        glow_mats += 1
                hull_cache[h] = dict(value=rec['value'], lo=lo, hi=hi, engine_mats=glow_mats)
            except Exception as exc:
                hull_cache[h] = dict(error=str(exc)[:60])
        s['hull_body'] = hull_cache.get(h)
    bodies = {}
    for k in sorted(engine_refs):
        try:
            bodies[k] = body_info(assets, k.replace('/', '\\'))
        except Exception as exc:
            bodies[k] = dict(error=str(exc)[:100])
            try:                                   # text body the bob1 parser refuses (MATERIAL3): raw extent only
                e = bob1.resolve_body(assets, k.replace('/', '\\'))
                data = assets.read_entry(e)
                te = text_extent(data)
                if te is not None:
                    bodies[k].update(source=e['source'], member=e['path'], raw_lo=te[0], raw_hi=te[1],
                                     raw_materials=re.findall(r'(?m)^(MATERIAL\d?:[^\n]{0,60})',
                                                              data.decode('latin1', 'replace')))
            except Exception:
                pass
    return dict(label=label, ships=ships, tships=tprov, jets=jets, jet_declared=jet_declared, jet_listed=jet_listed,
                effects=eff, particles=par, bodies=bodies, engine_refs=engine_refs, binary_scenes=binary_scenes,
                scene_err=scene_err, under_not_jet=under_engines_not_jet, jet_not_under=jet_not_under)


def r1(x):
    return [round(float(v), 1) for v in x]


def primary(b):
    """The body's tint material: the first material by face count that has a colour."""
    for m in b.get('materials', []):
        if m['colour'] is not None:
            return m
    return None


def role(k, c):
    """emitter: fx_engine_emitter* (anchor of the TShips col 11 effect, inferred); rcs: a jet whose C word has bit 0
    clear (direction masks 0x10/0x20/0x44/0x48/0x84/0x88, v/00566); main: the rest (C 0x7001 / 0x3001 / 0xf001)."""
    if 'emitter' in k:
        return 'emitter'
    if c is not None and not c & 1:
        return 'rcs'
    return 'main'


def main():
    stock = Assets(GAME, catalogues=STOCK_AP_CATALOGUES)
    mayhem, skipped = lod_overlay.original_assets(GAME)
    views = dict(stock=view_census('stock', stock), mayhem=view_census('mayhem', mayhem))
    assets_of = dict(stock=stock, mayhem=mayhem)
    summary = dict(views={}, skipped_overlay=skipped)

    # ---- ships.csv
    with open(HERE / 'ships.csv', 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['view', 'row', 'id', 'class', 'race', 'speed', 'col11_engine_effect', 'col12_engine_glow',
                    'col49_engine_trail', 'scene', 'glow_parts', 'emitter_parts', 'rcs_parts', 'hull_engine_mats', 'glow_bodies', 'glow_tint',
                    'glow_cluster', 'glow_len_z', 'glow_width', 'nozzle_min_z', 'hull_len_z', 'hull_value',
                    'part_C_values', 'part_frame_flags'])
        for label, v in views.items():
            for s in v['ships']:
                eng = s.get('engine_parts') or []
                glows = [p for p in eng if p['role'] == 'main']
                emit = [p for p in eng if p['role'] == 'emitter']
                gb = collections.Counter(p['body'].removeprefix('effects/engines/') for p in glows)
                tints, lens, widths = [], [], []
                for p in glows:
                    b = v['bodies'].get(p['body'], {})
                    m = primary(b) if 'lo' in b else None
                    if m:
                        tints.append(m['colour']['mean'])
                    if 'lo' in b:
                        lens.append(b['vhi'][2] - b['vlo'][2])
                        widths.append(max(b['vhi'][0] - b['vlo'][0], b['vhi'][1] - b['vlo'][1]))
                tint = np.mean(tints, 0) if tints else None
                h = s.get('hull_body') or {}
                zs = [p['pos'][2] for p in eng if p['pos']]
                s['glow_len'] = max(lens) if lens else None
                s['glow_width'] = max(widths) if widths else None
                s['tint'] = tint
                s['hull_len'] = float(h['hi'][2] - h['lo'][2]) if 'lo' in h else None
                w.writerow([label, s['index'], s['id'], s['cls'].removeprefix('OBJ_SHIP_'), s['race'], s['speed'],
                            s['effect'], s['glow'], s['trail'], s['scene'], len(glows), len(emit),
                            sum(p['role'] == 'rcs' for p in eng), h.get('engine_mats', ''),
                            ' '.join(f'{k}x{n}' for k, n in gb.most_common()),
                            hexrgb(tint) if tint is not None else '', cluster(tint) if tint is not None else '',
                            round(max(lens)) if lens else '', round(max(widths)) if widths else '',
                            round(min(zs)) if zs else '', round(s['hull_len']) if s['hull_len'] else '',
                            h.get('value', ''), ' '.join(sorted({str(p['c']) for p in eng})),
                            ' '.join(sorted({f for p in eng for f in p['flags']}))])

    # ---- engine_bodies.csv
    with open(HERE / 'engine_bodies.csv', 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['view', 'body', 'ships', 'parts', 'source', 'sha12', 'format', 'lods', 'value', 'points',
                    'min_x', 'max_x', 'min_y', 'max_y', 'min_z', 'max_z', 'vis_min_z', 'vis_max_z', 'vis_width',
                    'text_min_z', 'text_max_z', 'hue_sat_bright_contrast', 'classic_material_diffuse_rgb',
                    'materials', 'effect', 'blend_src_dst', 'zwrite', 'diffuse', 'texture_members', 'tex_size',
                    'frames', 'mean_rgb', 'alpha_weighted_rgb', 'p95_rgb', 'peak_rgb', 'alpha_mean', 'cluster'])
        for label, v in views.items():
            users = collections.Counter()
            for s in v['ships']:
                for k in {p['body'] for p in s.get('engine_parts') or []}:
                    users[k] += 1
            for k, b in v['bodies'].items():
                if 'error' in b:
                    w.writerow([label, k, users[k], v['engine_refs'][k], 'ERROR ' + b['error']])
                    continue
                m = primary(b) or (b['materials'][0] if b['materials'] else {})
                c = m.get('colour') if m else None
                w.writerow([label, k.removeprefix('effects/engines/'), users[k], v['engine_refs'][k], b['source'],
                            b['sha'], 'BOB1' if b['binary'] else 'text', b['lods'], b['value'], b['points'],
                            *[round(float(x)) for pair in zip(b['lo'], b['hi']) for x in pair],
                            round(float(b['vlo'][2])), round(float(b['vhi'][2])),
                            round(float(max(b['vhi'][0] - b['vlo'][0], b['vhi'][1] - b['vlo'][1]))),
                            round(float(b['text_lo'][2])) if b['text_lo'] is not None else '',
                            round(float(b['text_hi'][2])) if b['text_hi'] is not None else '',
                            m.get('hsbc', ''), m.get('classic_diffuse_rgb', ''),
                            len(b['materials']), m.get('effect', ''), m.get('blend', ''), m.get('zwrite', ''),
                            m.get('diffuse', ''), ' | '.join(c['members'][:4]) if c else m.get('error', ''),
                            c['size'] if c else '', c['frames'] if c else '',
                            hexrgb(c['mean']) if c else '', hexrgb(c['amean']) if c else '',
                            hexrgb(c['p95']) if c else '', hexrgb(c['peak']) if c else '',
                            round(c['alpha_mean'], 3) if c else '', cluster(c['mean']) if c else ''])

    # ---- class x body (long form)
    with open(HERE / 'class_body.csv', 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['view', 'class', 'glow_body', 'ships'])
        for label, v in views.items():
            cb = collections.Counter()
            for s in v['ships']:
                for k in {p['body'] for p in s.get('engine_parts') or [] if p['role'] == 'main'} or {'(none)'}:
                    cb[s['cls'].removeprefix('OBJ_SHIP_'), k.removeprefix('effects/engines/')] += 1
            for (c, k), n in sorted(cb.items()):
                w.writerow([label, c, k, n])

    # ---- class x glow-body size tier (the Mayhem xtc family encodes colour and size in the body name)
    with open(HERE / 'class_tier.csv', 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['view', 'class', 'tier', 'body_value', 'ships'])
        for label, v in views.items():
            ct = collections.Counter()
            for s in v['ships']:
                for k in {p['body'] for p in s.get('engine_parts') or [] if p['role'] == 'main'}:
                    m = re.search(r'_xtc_[a-z]+_(verytiny|tiny|nor\d?|big\d?|huge\d?)$', k)
                    b = v['bodies'].get(k, {})
                    ct[s['cls'].removeprefix('OBJ_SHIP_'), m.group(1) if m else 'other',
                       b.get('value', '') if m else ''] += 1
            for (c, t, val), n in sorted(ct.items(), key=lambda x: (x[0][0], str(x[0][2]).zfill(8), x[0][1])):
                w.writerow([label, c, t, val, n])

    # ---- summary.json
    for label, v in views.items():
        ships = v['ships']
        with_scene = [s for s in ships if s.get('engine_parts') is not None]
        glow_bodies = {p['body'] for s in ships for p in s.get('engine_parts') or [] if p['role'] == 'main'}
        eff_ids = collections.Counter(s['effect'] for s in ships)
        trail_ids = collections.Counter(s['trail'] for s in ships)
        clusters = collections.defaultdict(collections.Counter)
        body_clusters = collections.Counter()
        for k in glow_bodies:
            b = v['bodies'].get(k, {})
            m = primary(b) if 'lo' in b else None
            body_clusters[cluster(m['colour']['mean']) if m else 'no-colour'] += 1
        for s in ships:
            if s.get('tint') is not None:
                clusters[cluster(s['tint'])][RACE.get(int(s['race']), s['race'])] += 1
        classes = collections.defaultdict(list)
        for s in ships:
            classes[s['cls'].removeprefix('OBJ_SHIP_')].append(s)

        def rng(vals):
            vals = [x for x in vals if x]
            return [round(float(np.min(vals))), round(float(np.median(vals))), round(float(np.max(vals))),
                    len(vals)] if vals else None
        size = {c: dict(ships=len(ss), hull_len=rng([s['hull_len'] for s in ss]),
                        hull_value=rng([s['hull_body']['value'] for s in ss if s.get('hull_body') and 'value' in s['hull_body']]),
                        glow_len=rng([s['glow_len'] for s in ss]), glow_width=rng([s['glow_width'] for s in ss]),
                        glow_parts=rng([len([p for p in s.get('engine_parts') or [] if p['role'] == 'main'])
                                        for s in ss]))
                for c, ss in sorted(classes.items())}
        pairs = [(s['glow_width'], s['hull_len']) for s in ships if s.get('glow_width') and s.get('hull_len')]
        corr = float(np.corrcoef(np.log([p[0] for p in pairs]), np.log([p[1] for p in pairs]))[0, 1]) if pairs else None
        frame_flags = collections.Counter(f for s in ships for p in s.get('engine_parts') or [] for f in p['flags'])
        cvals = collections.Counter(p['c'] for s in ships for p in s.get('engine_parts') or [])
        rotated = sum(1 for s in ships for p in s.get('engine_parts') or [] if p['rot'] and any(p['rot']))
        nparts = sum(len(s.get('engine_parts') or []) for s in ships)
        origin = collections.Counter()
        for k in glow_bodies:
            b = v['bodies'].get(k, {})
            if 'lo' in b:
                lo, hi = b['vlo'][2], b['vhi'][2]
                origin['nozzle origin, plume to -z (vis max_z within 5% of len of 0)' if abs(hi) <= 0.05 * (hi - lo)
                       else 'plume to +z (vis min_z ~ 0)' if abs(lo) <= 0.05 * (hi - lo) else
                       'origin inside (both sides)'] += 1
        trail_eff = {i: dict(comment=v['particles'].get(int(i), {}).get('comment', 'MISSING'),
                             emitters=[(e['flags'], e['mat'], e['size']) for e in
                                       v['particles'].get(int(i), {}).get('emitters', [])])
                     for i in sorted(trail_ids, key=int) if i != '0'}
        eng_eff = {}
        for i in sorted(eff_ids, key=int):
            e = v['effects'].get(int(i))
            eng_eff[i] = dict(ships=eff_ids[i], comment=e['comment'] if e else 'MISSING',
                              elements=[{k: x for k, x in el.items() if k in ('flags', 'i3d', 'body', 'lensflare',
                                                                               'maxsize', 'offset')}
                                        for el in (e['elements'] if e else [])])
        flares = lensflares(assets_of[label])
        for i, e in eng_eff.items():
            for el in e['elements']:
                if isinstance(el.get('body'), int) and el['body'] >= 0:
                    el['body_colour'] = numbered_colour(assets_of[label], el['body'])
                lf = el.get('lensflare')
                if isinstance(lf, int) and 0 <= lf < len(flares):
                    el['flare_rows'] = len(flares[lf])
                    el['flare_rows_colour'] = [numbered_colour(assets_of[label], row[0]) for row in flares[lf]]
        rot_main = collections.Counter(' '.join(f'{x:g}' for x in p['rot']) for s in ships
                                       for p in s.get('engine_parts') or [] if p['role'] == 'main' and p['rot'])
        col11_vs_cluster = collections.Counter()
        for s in ships:
            e = v['effects'].get(int(s['effect']))
            word = re.search(r'- (\w+),', e['comment']).group(1) if e and re.search(r'- (\w+),', e['comment']) else \
                (e['comment'][:30] if e else 'missing')
            col11_vs_cluster[f"{word} -> {cluster(s['tint']) if s.get('tint') is not None else 'no glow'}"] += 1
        race_ids = collections.defaultdict(list)
        for s in ships:
            if len(race_ids[s['race']]) < 6:
                race_ids[s['race']].append(s['id'])
        summary['views'][label] = dict(
            main_part_rotations=dict(rot_main.most_common(8)), col11_colour_vs_glow_cluster=dict(col11_vs_cluster),
            race_sample_ids=dict(race_ids),
            tships=v['tships']['source'], ships=len(ships), ships_with_text_scene=len(with_scene),
            binary_scenes=v['binary_scenes'], missing_scenes=dict(v['scene_err']),
            ships_with_glow_parts=sum(1 for s in with_scene if any(p['role'] == 'main'
                                                                  for p in s['engine_parts'])),
            ships_without_engine_parts=sum(1 for s in with_scene if not s['engine_parts']),
            engine_parts=nparts, distinct_glow_bodies=len(glow_bodies),
            sbtype_jet=dict(declared=v['jet_declared'], listed=v['jet_listed']),
            scene_engine_bodies_not_in_jet=dict(v['under_not_jet']),
            jet_bodies_used_outside_effects_engines=dict(v['jet_not_under']),
            body_errors={k: b['error'] for k, b in v['bodies'].items() if 'error' in b},
            class_counts=dict(collections.Counter(s['cls'] for s in ships)),
            col11_engine_effects=eng_eff, col12_engine_glow=dict(collections.Counter(s['glow'] for s in ships)),
            col49_engine_trails=dict(ships=dict(trail_ids), generators=trail_eff),
            glow_body_tint_clusters=dict(body_clusters),
            ship_tint_cluster_by_race={c: dict(r) for c, r in clusters.items()},
            part_frame_flags=dict(frame_flags), part_C_values={str(k): n for k, n in cvals.items()},
            rotated_engine_parts=rotated, glow_body_origin=dict(origin),
            size_by_class=size, log_corr_glow_width_vs_hull_len=corr)

    # ---- Mayhem vs stock
    s_b, m_b = views['stock']['bodies'], views['mayhem']['bodies']
    stock_members = {k.removeprefix('addon/') for k in stock.entries}
    may_members = {k.removeprefix('addon/') for k in mayhem.entries}

    def member_sha(assets, k):
        try:
            return assets.get(k)[1]['decoded_sha256'][:12]
        except FileNotFoundError:
            return None
    eng_members = sorted(k for k in may_members if k.startswith(('objects/effects/engines/',)))
    new_eng = sorted(k for k in eng_members if k not in stock_members)
    changed_eng = sorted(k for k in eng_members if k in stock_members and
                         member_sha(stock, k) != member_sha(mayhem, k))
    tex = sorted(k for k in may_members if re.search(r'(^|/)fx_engine|engine', k) and
                 k.split('/')[0] in ('dds', 'textures', 'tex'))
    new_tex = [k for k in tex if k not in stock_members]
    changed_tex = [k for k in tex if k in stock_members and member_sha(stock, k) != member_sha(mayhem, k)]
    trail_assets = {lab: sorted(k for k in (may_members if lab == 'mayhem' else stock_members)
                                if TRAIL_NAME.search(k.rsplit('/', 1)[-1]) and
                                k.split('/')[0] in ('objects', 'dds', 'textures', 'tex'))
                    for lab in ('stock', 'mayhem')}
    eff_s, eff_m = views['stock']['effects'], views['mayhem']['effects']
    par_s, par_m = views['stock']['particles'], views['mayhem']['particles']
    summary['mayhem_vs_stock'] = dict(
        engine_members_installed=len(eng_members), engine_members_new=len(new_eng),
        engine_members_changed=changed_eng, new_engine_members=new_eng,
        glow_bodies_used_new=sorted(k for k in m_b if k not in s_b),
        glow_bodies_used_dropped=sorted(k for k in s_b if k not in m_b),
        glow_bodies_used_changed=sorted(k for k in m_b if k in s_b and 'sha' in m_b[k] and 'sha' in s_b[k]
                                        and m_b[k]['sha'] != s_b[k]['sha']),
        engine_textures_new=new_tex, engine_textures_changed=changed_tex,
        effects_new=sorted(set(eff_m) - set(eff_s)), effects_removed=sorted(set(eff_s) - set(eff_m)),
        particles_new=sorted(set(par_m) - set(par_s)), particles_removed=sorted(set(par_s) - set(par_m)),
        trail_named_assets={k: dict(count=len(v), names=v[:80]) for k, v in trail_assets.items()},
        trail_named_assets_new=sorted(set(trail_assets['mayhem']) - set(trail_assets['stock'])),
        particle_trail_flag={lab: sorted(i for i, p in pp.items() if any('PEDF_TRAIL' in e['flags']
                                                                           for e in p['emitters']))
                             for lab, pp in (('stock', par_s), ('mayhem', par_m))},
        effects_trail_comment={lab: sorted(f'{i}: {e["comment"]}' for i, e in ee.items()
                                           if re.search('trail|streak', e['comment'], re.I))
                               for lab, ee in (('stock', eff_s), ('mayhem', eff_m))})
    # materials named by particle trails
    for lab, assets in (('stock', stock), ('mayhem', mayhem)):
        rows = lod_atlas.materials_rows(assets)
        mats = sorted({e['mat'] for p in views[lab]['particles'].values() for e in p['emitters']})
        summary['views'][lab]['particle_materials'] = {
            m: dict(row=list(rows[m]) if rows and m < len(rows) else None) for m in mats}
        for m in mats:
            name = str(m).encode()
            try:
                c = colour(assets, name)
                summary['views'][lab]['particle_materials'][m].update(
                    texture=c['members'][0] if c else None, size=c['size'] if c else None,
                    mean=hexrgb(c['mean']) if c else None)
            except Exception as exc:
                summary['views'][lab]['particle_materials'][m]['error'] = str(exc)[:80]
    # trail generator colours: mean of the first emitter's atlas cell (alpha-weighted)
    for lab, assets in (('stock', stock), ('mayhem', mayhem)):
        gens = summary['views'][lab]['col49_engine_trails']['generators']
        imgs = {}
        for gid, g in gens.items():
            p = views[lab]['particles'].get(int(gid))
            if not p or not p['emitters']:
                continue
            e = p['emitters'][0]
            try:
                if e['mat'] not in imgs:
                    data, kind, info = lod_atlas.texture_source(assets, str(e['mat']).encode())
                    imgs[e['mat']] = (lod_atlas.decode_dds(data, 0) if kind == 'dds' else
                                      lod_atlas.decode_image(data, str(e['mat'])), info['member'])
                img, member = imgs[e['mat']]
                h, w = img.shape[:2]
                u1, v1, u2, v2 = e['uv']
                x0, x1 = sorted((int(u1 * w), int(u2 * w)))
                y0, y1 = sorted((int(v1 * h), int(v2 * h)))
                cell = img[y0:max(y1, y0 + 1), x0:max(x1, x0 + 1)].reshape(-1, 4).astype(np.float64)
                a = cell[:, 3] / 255
                mean = (cell[:, :3] * a[:, None]).sum(0) / max(a.sum(), 1e-9)
                g['cell'] = dict(material=e['mat'], member=member, uv=e['uv'], alpha_weighted_mean=hexrgb(mean),
                                 plain_mean=hexrgb(cell[:, :3].mean(0)), cluster=cluster(mean),
                                 lifetime_ms=e['lifetime'], size=e['size'])
            except Exception as exc:
                g['cell'] = dict(error=str(exc)[:80])
    # which installed engine members name each diffuse texture (text bodies: the raw MATERIAL lines)
    users = collections.defaultdict(list)
    for k in sorted(may_members):
        if k.startswith('objects/effects/engines/'):
            try:
                d = mayhem.get(k)[0]
            except FileNotFoundError:
                continue
            for t in set(re.findall(rb't_DiffuseTexture;SPTYPE_STRING;([^;]+);', d)) | \
                    set(re.findall(rb't_DiffuseTexture\x00\x00\x08([^\x00]+)\x00', d)):
                users[t.decode('latin1').lower()].append(k.rsplit('/', 1)[-1])
    summary['mayhem_vs_stock']['engine_member_diffuse_users'] = {t: dict(count=len(v), sample=v[:4])
                                                                 for t, v in sorted(users.items())}
    (HERE / 'summary.json').write_text(json.dumps(summary, separators=(',', ':'), default=lambda o: o.tolist()
                                                  if isinstance(o, np.ndarray) else str(o)))
    for lab in views:
        sv = summary['views'][lab]
        print(lab, 'ships', sv['ships'], 'glow ships', sv['ships_with_glow_parts'], 'distinct glow bodies',
              sv['distinct_glow_bodies'], 'parts', sv['engine_parts'])


if __name__ == '__main__':
    main()
