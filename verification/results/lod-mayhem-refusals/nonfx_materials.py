#!/usr/bin/env python3
"""Q2-Q5 census: bodies the bake record refuses as non_effect_material (ship/station; --all adds 'other').

Per body: parse the recorded member (bob1.parse), take record 0 (the batch's source_record), and for
every classic (non-effect) material its visible groups use derive what 0x004c0150 draws
(docs/reverse-engineering/non-effect-materials.md):
  texture id  name -> id as 0x004f4cb0 (''/'0'/NULL -> 0, leading digits -> that id, else a named id past
              the Materials rows); MAT5: the u16
  flags       file flags, overwritten by the Materials row flags when 0 <= id < rows (0x0048206e..0x00482093,
              text 0x0048469b..0x004846be); MAT5 flag word OR-ed afterwards
  blend       0x004c1779..0x004c1908 on flags & 0x4c7
  technique   BUMPMAP_LOW when map1 (+0x32 bump), map0 (+0x2e cube, only without bump) or map2 (+0x36 light)
              is set, else DEFAULT (0x004c0b58..0x004c0b7d)
  constants   0x004c1399..0x004c151a (see the note)
Prints distinct draw shapes with counts and the helper/content split by rule. Read-only.
  PYTHONPATH=tools/analysis python3 nonfx_materials.py [game_root] [--all] > nonfx_materials_out.txt"""
import collections
import json
import re
import sys
from pathlib import Path

import bob1
import lod_atlas
import sector_fog_census as sfc

args = [a for a in sys.argv[1:] if not a.startswith('--')]
GAME = Path(args[0] if args else Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
ALL = '--all' in sys.argv
HIDDEN_PART = 0x8000          # lod_atlas.HIDDEN_PART (0x0047d9c0 skips it)
MPF = dict(MPF_NULL=0, MPF_ALPHATEST=1, MPF_DESTINATIONBLEND=2, MPF_ALPHABLEND=4, MPF_WIREFRAME=8,
           MPF_2SIDED=0x10, MPF_TEXTURE_NOTSWAPABLE=0x20, MPF_MULTIPLY2X=0x40, MPF_MULTIPLY=0x80,
           MPF_NOFILTERING=0x100, MPF_SRCCOLOR=0x400, MPF_ENVMAP=0x1000, MPF_BUMPMAP=0x2000,
           MPF_LIGHTMAP=0x4000, MPF_BESTQUALITY=0x8000, MPF_FONTSCALE=0x10000, MPF_READABLE=0x20000,
           MPF_WRITEABLE=0x40000, MPF_AUTOFREE=0x80000, MPF_RADIOSITY=0x100000, MPF_TEXTUREALPHA=0x200000,
           MPF_IMPORTPICTURE=0x400000, MPF_GENERATED=0x800000, MPF_XBOX_NOCOMPRESS=0x1000000,
           MPF_USEFXSHADER=0x2000000, MPF_HAZE=0x4000000)          # name table 0x0054dbe0


def materials_flags(assets):
    """[(texture id, MPF flags)] per Materials row, flags parsed with the full 0x0054dbe0 table."""
    data, meta = assets.get(lod_atlas.MATERIALS_MEMBER)
    lines = [l for l in data.decode('latin1').splitlines() if l.strip() and not l.lstrip().startswith('/')]
    count = int(lines[0].split(';')[0])
    rows = []
    for line in lines[1:count + 1]:
        f = [x.strip() for x in line.split(';')]
        fl = 0
        for tok in f[15].split('|'):
            tok = tok.strip()
            if tok:
                fl |= MPF[tok] if tok in MPF else int(tok, 0)
        rows.append((int(f[12], 0), fl))
    assert len(rows) == count
    return rows, meta['source']


def tex_id(t):
    if isinstance(t, int):
        return t
    s = t.decode('latin1')
    if s in ('', '0', 'NULL', 'Null', 'null'):
        return 0
    m = re.match(r'-?\d+', s)
    if m and (s[0].isdigit() or s[0] == '-'):
        return int(m.group())
    return None                                   # named: an id past the Materials rows


def blend(fl):
    if not fl & 0x4c7:
        return 'opaque'
    if fl & 0x80: return 'multiply(DESTCOLOR,ZERO)'
    if fl & 0x40: return 'multiply2x(DESTCOLOR,SRCCOLOR)'
    if fl & 0x400: return 'srccolor(SRCCOLOR,INVSRCCOLOR)'
    if fl & 4: return 'alpha(SRCALPHA,INVSRCALPHA)'
    if fl & 2 and fl & 1: return 'alpha(SRCALPHA,INVSRCALPHA)'
    if fl & 2: return 'additive(ONE,ONE)'
    return 'alphatest'


def named(v):
    return v not in (b'', 0, b'NULL', b'0', None)


def draw_shape(m, ver, rows):
    tid = tex_id(m['texture'])
    fl = m.get('flags', 0) if ver >= 6 else 0
    if tid is not None and 0 <= tid < len(rows):
        fl = rows[tid][1]
    if ver < 6:
        fl |= m.get('flagword', 0)
    c = m['colors']
    dif, spec, si = c[3:6], c[6:9], c[11]
    maps = [n for n, _ in m['maps']]
    extra = [n for n, _ in m.get('extra', [])]
    bump, env, light = named(maps[1]), named(maps[0]) and not named(maps[1]), named(maps[2])
    spec_map = bool(extra) and named(extra[0])
    if tid is None:
        diffuse = 'named'
    elif tid == 0:
        diffuse = 'none(id0->NONE_GRAY)'
    elif tid < len(rows) and rows[tid][0] == 0:
        diffuse = f'row{tid}(no file)'
    else:
        diffuse = f'row{tid}'
    return dict(
        diffuse=diffuse, blend=blend(fl), two_sided=bool(fl & 0x10), nofilter=bool(fl & 0x100),
        technique='BUMPMAP_LOW' if (bump or env or light) else 'DEFAULT',
        light=light, bump=bump, cube=env, specmap=spec_map,
        DiffuseStrength=0.0 if si else m['w2c'] / 100,
        Emissive=[round(x * si / 25500, 4) for x in dif] if si else [0, 0, 0],
        SpecularStrength=m['w26'] / 100,
        SpecularPower=(m['w24'] if m['w24'] else sum(spec) / 768 * 100) + 1,
        Reflection=m['maps'][0][1] * 0.04 if env else 0.0)


def main():
    rec = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
    bodies = [b for b in rec['bodies'] if 'non_effect_material' in (b.get('refuse') or [])
              and (ALL or b['cat'] != 'other')]
    assets = sfc.Assets(GAME)
    rows, rows_src = materials_flags(assets)
    by_member = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in assets.entries.values() for e in lst}
    shapes, shape_bodies, record_shapes = collections.Counter(), collections.defaultdict(set), collections.Counter()
    split, lines = collections.Counter(), []
    for b in sorted(bodies, key=lambda b: b['name'].lower()):
        tree = bob1.parse(assets.read_entry(by_member[b['member'].lower()]), max_trailing=16)
        mats = bob1.materials(tree)
        ver = next(bob1.MATVER[t] for t, _ in tree['sections'] if t in bob1.MATVER)
        use = collections.Counter()
        for p in bob1.lods(tree)[0]['parts']:
            if not p['flags'] & HIDDEN_PART:
                for g in p['groups']:
                    use[g['material']] += len(g['faces'])
        classic = {mi: n for mi, n in use.items() if 0 <= mi < len(mats) and 'params' not in mats[mi]}
        effect_faces = sum(n for mi, n in use.items() if 0 <= mi < len(mats) and 'params' in mats[mi])
        ds = {mi: draw_shape(mats[mi], ver, rows) for mi in classic}
        opaque_tex = [mi for mi, s in ds.items() if s['blend'] == 'opaque' and not s['diffuse'].startswith('none')]
        opaque_untex = [mi for mi, s in ds.items() if s['blend'] == 'opaque' and s['diffuse'].startswith('none')]
        blended = [mi for mi, s in ds.items() if s['blend'] != 'opaque']
        # rule (Q5): helper = no effect material drawn and no opaque textured classic material
        if not effect_faces and not opaque_tex:
            cls = 'helper'
        elif opaque_tex:
            cls = 'content:opaque_textured_classic'
        elif opaque_untex:
            cls = 'content:opaque_untextured_classic'
        else:
            cls = 'content:blended_classic_only'
        src = b['member'].split(':')[0]
        split[(cls, src)] += 1
        for mi, s in ds.items():
            k = json.dumps(s, sort_keys=True)
            shapes[k] += 1
            shape_bodies[k].add(b['name'])
            if cls.startswith('content'):
                m = mats[mi]
                raw = dict(ver=ver, file_flags=hex(m.get('flags', m.get('flagword', 0))),
                           texture=('none' if tex_id(m['texture']) == 0 else 'numbered' if tex_id(m['texture'])
                                    is not None else 'named'),
                           amb=m['colors'][0:3], dif=m['colors'][3:6], spec=m['colors'][6:9], w16=m['colors'][9],
                           transp=m['colors'][10], selfillum=m['colors'][11], shininess=m['w24'],
                           strength=m['w26'], texval=m['w2c'],
                           maps=[i for i, (n, _) in enumerate(m['maps']) if named(n)],
                           map_values=[v for _, v in m['maps']],
                           extra=[i for i, (n, _) in enumerate(m.get('extra', [])) if named(n)])
                rk = json.dumps(raw, sort_keys=True)
                record_shapes[rk] += 1
                shape_bodies['raw' + rk].add(b['name'])
        lines.append(f'{cls:34} {b["name"]}  {src} v{ver} faces0={sum(use.values())} effect_faces={effect_faces}'
                     f' classic_faces={sum(classic.values())} opaque_tex={len(opaque_tex)}'
                     f' opaque_untex={len(opaque_untex)} blended={len(blended)}')
    print(f'Materials table {rows_src}: {len(rows)} rows; row 0 flags {rows[0][1]:#x}; row 155 flags {rows[155][1]:#x}')
    print(f'bodies {len(bodies)} ({"all categories" if ALL else "ship+station"})')
    print('\nper body (class, name, source, MAT version, record-0 faces):')
    for l in lines:
        print('  ' + l)
    print('\nsplit by rule (class, source catalogue): count')
    for (c, s), n in sorted(split.items()):
        print(f'  {c:34} {s:13} {n}')
    tot = collections.Counter()
    for (c, s), n in split.items():
        tot[c.split(':')[0]] += n
    print('  totals: ' + json.dumps(dict(tot)))
    print('\nQ2: distinct classic record shapes in the content bodies (materials, bodies, example):')
    for k, n in record_shapes.most_common():
        bs = sorted(shape_bodies['raw' + k])
        print(f'  {n:4} mats {len(bs):3} bodies  {k}  e.g. {bs[0]}')
    print('\ndistinct classic draw shapes, all bodies (materials, bodies, example):')
    for k, n in shapes.most_common():
        bs = sorted(shape_bodies[k])
        print(f'  {n:4} mats {len(bs):3} bodies  {k}  e.g. {bs[0]}')


main()


def per_body_constants():
    """Q4: distinct (DiffuseStrength, SpecularStrength, SpecularPower, Emissive) per content body over its
    opaque textured classic materials, and whether the body also draws effect materials (and which)."""
    rec = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
    bodies = [b for b in rec['bodies'] if 'non_effect_material' in (b.get('refuse') or []) and b['cat'] != 'other']
    assets = sfc.Assets(GAME)
    rows, _ = materials_flags(assets)
    by_member = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in assets.entries.values() for e in lst}
    hist, effects = collections.Counter(), collections.Counter()
    for b in bodies:
        tree = bob1.parse(assets.read_entry(by_member[b['member'].lower()]), max_trailing=16)
        mats = bob1.materials(tree)
        ver = next(bob1.MATVER[t] for t, _ in tree['sections'] if t in bob1.MATVER)
        used = {g['material'] for p in bob1.lods(tree)[0]['parts'] if not p['flags'] & HIDDEN_PART for g in p['groups']}
        ds = [draw_shape(mats[mi], ver, rows) for mi in used if 0 <= mi < len(mats) and 'params' not in mats[mi]]
        ot = [s for s in ds if s['blend'] == 'opaque' and not s['diffuse'].startswith('none')]
        if not ot:
            continue
        consts = {(s['DiffuseStrength'], s['SpecularStrength'], s['SpecularPower'], tuple(s['Emissive'])) for s in ot}
        hist[len(consts)] += 1
        fx = sorted({mats[mi]['effect'].decode('latin1').lower() for mi in used
                     if 0 <= mi < len(mats) and 'params' in mats[mi]})
        effects[tuple(fx)] += 1
    print('\nQ4: content bodies with opaque textured classic materials: distinct constant sets per body -> bodies')
    print('  ' + json.dumps({str(k): v for k, v in sorted(hist.items())}))
    print('  effect files also drawn by those bodies -> bodies: ' + json.dumps({'+'.join(k) or '(none)': v
                                                                            for k, v in effects.items()}))


per_body_constants()
