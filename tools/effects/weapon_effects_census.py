#!/usr/bin/env python3
"""Census of what the game's own data says about every weapon's visual effects (read-only, host side).

Three catalogue views: stock AP (STOCK_AP_CATALOGUES under /mnt/c/X3), installed Mayhem 3 (every mounted
layer of /mnt/c/X3) and FL (/mnt/c/X3FL, edition fl). Per view it writes
verification/results/weapon-effects-census/<view>.json and prints a short summary. Only names, numbers and
hashes are written; no body, texture or table bytes.

Column meanings are those of docs/reverse-engineering/weapon-effect-spawn.md sections 1 and 4 (X3AP.exe).
Per TBullets row: weapon id (col 48), body (col 0) with its materials (effect file, blend, zwrite, texture
names, as verification/results/engine-effects/engine_effects_data.py reads them, via tools/analysis/bob1.py)
and LOD 0 half-extents, lifetime ms (10), speed (11), flag word (12) decoded into named bits, light colour
(13-15), quad size (16, 17), beam length (18), flight effect (19), impact effect (20; also the bullet's own death
explosion with flag 0x100), muzzle effect (21), trail Particles3 id (23, resolved to its emitters' MatIDs and
tex/true members) and col 24 (+0x88, not an Effects id; kept raw). An effect value <= 0 is read as no reference.
Every referenced Effects row is listed once under `effects` with its elements and the materials of each element
body (numeric body id N -> objects/v/%05d).
Death rows (row chooser 0x00413040, default 48): TShips col 47 per ship class (col 52), TDocks/TFactories col 14,
TAsteroids col 8, TMissiles col 21, TBullets col 20 for flag 0x100; a 0 value is counted as the default row
(inferred from the note's Mayhem TMissiles row). TShields rows are listed raw; TShips also gives the per-class
count of distinct col 11 (engine effect) values as context.
"""
import collections
import hashlib
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/analysis'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import bob1  # noqa: E402

OUT = REPO / 'verification/results/weapon-effects-census'
BLEND = {1: 'ZERO', 2: 'ONE', 3: 'SRCCOLOR', 4: 'INVSRCCOLOR', 5: 'SRCALPHA', 6: 'INVSRCALPHA',
         7: 'DESTALPHA', 8: 'INVDESTALPHA', 9: 'DESTCOLOR', 10: 'INVDESTCOLOR'}
BULLET_COLUMNS = 50
EFFECT_COLUMNS = {'flight_effect': 19, 'impact_effect': 20, 'muzzle_effect': 21}
TRAIL_COLUMN = 23
FLAG_BITS = {0x2: 'beam_gun_tied', 0x4: 'beam_kind_b', 0x8: 'beam_fixed_length', 0x20: 'hull_damage_through_shields',
             0x40: 'ammunition', 0x80: 'repair', 0x100: 'area_flak', 0x200: 'on_hit_slow',
             0x400: 'on_hit_energy_drain', 0x800: 'on_hit_col31_32', 0x1000: 'fragments', 0x2000: 'continuous_beam'}
AREA_FLAK = 0x100
DEFAULT_DEATH_ROW = 48
DEATH_COLUMNS = {'TShips': 47, 'TDocks': 14, 'TFactories': 14, 'TAsteroids': 8, 'TMissiles': 21}
# Effects.txt header comment: flags;i3dflags;partdelay;partspeedmultiplier; then the value record below.
# Shipped files carry the flags line with two fields and a 12-field value line (bodyid..timeoffset).
ELEMENT_VALUES = ('bodyid', 'minsize', 'maxsize', 'duration', 'rndsize', 'xoffset', 'yoffset', 'zoffset',
                  'lensflarenr', 'atten', 'halfspeed', 'timeoffset')
ELEMENT_FLAGS = ('flags', 'i3dflags', 'partdelay', 'partspeedmultiplier')
PROPAGATE_VALUES = ('prop_effect_id', 'min_props', 'max_props', 'min_prop_delay', 'max_prop_delay',
                    'prop_length', 'prop_delay', 'prop_steps', 'timeoffset')
SHIP_CLASS_COLUMN = 52
SHIP_ENGINE_COLUMN = 11


def _str(v):
    return v.decode('latin1') if isinstance(v, bytes) else v


def _num(s):
    s = s.strip()
    try:
        return int(s)
    except ValueError:
        try:
            return float(s)
        except ValueError:
            return s


def data_rows(text):
    """Non-empty, non-comment lines of a T file, each split on ';' with the trailing empty field dropped."""
    out = []
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith('/'):
            continue
        f = [x.strip() for x in s.split(';')]
        if f and f[-1] == '':
            f.pop()
        out.append(f)
    return out


def parse_tbullets(text):
    rows = data_rows(text)
    out = []
    for i, f in enumerate(rows[1:]):
        if len(f) < 49:
            raise ValueError(f'TBullets row {i}: {len(f)} fields, expected {BULLET_COLUMNS}')
        out.append(dict(
            index=i, weapon_id=f[48], body=f[0],
            lifetime_ms=_num(f[10]), speed=_num(f[11]), flag_word=_num(f[12]), flag_bits=flag_bits(_num(f[12])),
            light_colour=[_num(x) for x in f[13:16]], size=[_num(x) for x in f[16:18]], beam_length=_num(f[18]),
            trail_particles=_num(f[TRAIL_COLUMN]), col24_not_effect=_num(f[24]),
            **{k: _num(f[c]) for k, c in EFFECT_COLUMNS.items()}))
    return out


def flag_bits(word):
    """Named bits of the TBullets flag word (col 12); bits the EXE never tests as unknown_0x<bit>."""
    if not isinstance(word, int):
        return []
    return [FLAG_BITS.get(1 << b, f'unknown_{1 << b:#x}') for b in range(32) if word >> b & 1]


def row_name(f):
    return next((x for x in reversed(f) if x.startswith('SS_')), None)


def death_column(text, column):
    """(row name, death row) per data row of a T file; 0 counts as the default row."""
    out = []
    for f in data_rows(text)[1:]:
        if len(f) > column:
            v = _num(f[column])
            out.append((row_name(f), DEFAULT_DEATH_ROW if v == 0 else v))
    return out


def ship_death_rows(text):
    classes = collections.defaultdict(collections.Counter)
    for f in data_rows(text)[1:]:
        if len(f) > SHIP_CLASS_COLUMN:
            v = _num(f[DEATH_COLUMNS['TShips']])
            classes[ship_class(f)][DEFAULT_DEATH_ROW if v == 0 else v] += 1
    return {k: {str(r): n for r, n in sorted(c.items(), key=lambda x: str(x[0]))} for k, c in sorted(classes.items())}


PARTICLE_HEADER = re.compile(r'^(\d+);\s*(\d+);\s*NULL;')


def parse_particles(text):
    """{PGDescID: [emitter]} as engine_effects_data.particle_rows reads Particles3.txt: a `id; n; NULL;` header,
    then PEDF emitter lines (flags; MatID; u1; v1; u2; v2; Density; ...; Lifetime; MinSize; MaxSize)."""
    out, cur = {}, None
    for line in text.splitlines():
        s = line.split('//')[0].strip()
        if not s:
            continue
        m = PARTICLE_HEADER.match(s)
        if m:
            cur = int(m.group(1))
            out[cur] = []
            continue
        if cur is not None and s.startswith('PEDF'):
            f = [x.strip() for x in s.split(';')]
            out[cur].append(dict(flags=f[0].split('|'), mat_id=_num(f[1]), density=_num(f[6]),
                                 speed=[_num(f[11]), _num(f[12])], lifetime=_num(f[13]),
                                 size=[_num(f[14]), _num(f[15])]))
    return out


def parse_tshields(text):
    rows = data_rows(text)
    return [dict(index=i, shield_id=next((x for x in f if x.startswith('SS_SHIELD')), None),
                 fields=[_num(x) for x in f]) for i, f in enumerate(rows[1:])]


def ship_class(f):
    """Col 52 (+0xc8, text-id reader 0x004ea440): OBJ_SHIP_* name, or a raw numeric class id as class_id:N."""
    v = f[SHIP_CLASS_COLUMN]
    return v if not v.lstrip('-').isdigit() else f'class_id:{v}'


def tships_engine_classes(text):
    classes = collections.defaultdict(set)
    rows = [f for f in data_rows(text)[1:] if len(f) > SHIP_CLASS_COLUMN]
    for f in rows:
        classes[ship_class(f)].add(f[SHIP_ENGINE_COLUMN])
    return dict(rows=len(rows), class_column=SHIP_CLASS_COLUMN, engine_effect_column=SHIP_ENGINE_COLUMN,
                distinct_engine_effects_per_class={k: len(v) for k, v in sorted(classes.items())})


def _int(s):
    try:
        return int(s)
    except ValueError:
        return None


def parse_effects(text):
    """{effectnr: {numelements, effectflags, header_extra, elements, [warning]}} by the Effects.txt grammar:
    a header effectnr;numelements;effectflags;... then numelements pairs of a flags line and a value line
    (12 fields, or the EEDF_PROPAGATE record)."""
    lines = []
    for line in text.splitlines():
        s = line.split('//')[0].strip()
        if s:
            f = [x.strip() for x in s.split(';')]
            if f[-1] == '':
                f.pop()
            lines.append(f)
    effects, i = {}, 0
    while i < len(lines):
        f = lines[i]
        i += 1
        if len(f) < 3 or _int(f[0]) is None or _int(f[1]) is None:
            continue                                   # version line or stray record
        eid, n = int(f[0]), int(f[1])
        eff = dict(numelements=n, effectflags=_num(f[2]), header_extra=[_num(x) for x in f[3:]], elements=[])
        for k in range(n):
            if i + 1 >= len(lines) or (len(lines[i]) >= 3 and _int(lines[i][0]) is not None):
                eff['warning'] = f'{k} of {n} elements present'
                break
            flags, values = lines[i], lines[i + 1]
            i += 2
            names = [x.strip() for x in flags[0].split('|') if x.strip()] if flags else []
            el = dict(zip(ELEMENT_FLAGS, [_num(x) for x in flags]))
            el['flag_names'] = names
            if 'EEDF_PROPAGATE' in names:
                el.update(zip(PROPAGATE_VALUES, [_num(x) for x in values]))
            else:
                fields = values if len(values) != 14 else values[2:]
                if len(values) == 14:
                    el['partdelay'], el['partspeedmultiplier'] = _num(values[0]), _num(values[1])
                el.update(zip(ELEMENT_VALUES, [_num(x) for x in fields]))
                if len(fields) != len(ELEMENT_VALUES):
                    el['warning'] = f'value line has {len(values)} fields'
                el['light'] = 'EEDF_LIGHT' in names
                if 'EEDF_PARTICLEGENERATOR' in names:
                    el['particlenr'] = el.pop('lensflarenr', None)     # header comment rule
            eff['elements'].append(el)
        effects[eid] = eff
    return effects


def element_body_name(bodyid):
    """Body reference -> body name: numeric id N -> v\\%05d (game-scale-and-ship-classes.md), -1 none."""
    if isinstance(bodyid, str) and bodyid.isdigit():
        bodyid = int(bodyid)
    if isinstance(bodyid, int):
        return None if bodyid < 0 else f'v\\{bodyid:05d}'
    return bodyid or None


def material_summary(m):
    """One material: effect materials as engine_effects_data reads them (effect, blend pair, zwrite, textures);
    classic materials with their texture name or id and map names."""
    out = {'index': m.get('index'), 'flags': m.get('flags', m.get('flagword'))}
    if 'params' in m:
        p = {_str(n).lower(): v for n, _, v in m['params']}
        on = p.get('g_alphablendenable', [0])[0]
        src = BLEND.get(p.get('g_srcblend', [0])[0], '?')
        dst = BLEND.get(p.get('g_destblend', [0])[0], '?')
        out.update(kind='effect', effect=_str(m['effect']).lower(), technique=m.get('technique'),
                   blend=f'{src}/{dst}' if on else 'off', srcblend=src, destblend=dst, alphablendenable=on,
                   zwrite=bool(p.get('g_zwriteenable', [0])[0]),
                   textures={_str(n): _str(v) for n, t, v in m['params'] if t == 8 and _str(n).lower().startswith('t_')})
    else:
        tex = m.get('texture')
        out.update(kind='classic', texture=_str(tex) if not isinstance(tex, int) else tex,
                   maps=[_str(t) for t, _ in m.get('maps', []) if t not in (0, b'')],
                   blend_switch=bool((out['flags'] or 0) & 0x2))
    return out


def extents(lod):
    pts = [p[1:4] for p in lod['points'] if p[0] & 1]
    if not pts:
        return None
    half = [(max(c) - min(c)) / 2 for c in zip(*pts)]
    centre = [(max(c) + min(c)) / 2 for c in zip(*pts)]
    lateral = max(half[0], half[1])
    return dict(scale=lod['value'], points=len(pts), half_extents=half, centre=centre,
                z_over_xy=round(half[2] / lateral, 4) if lateral else None)


MATERIAL3 = re.compile(rb'^[ \t]*MATERIAL3[ \t]*:([^\n]*)$', re.M | re.I)


def material3_records(data):
    """MATERIAL3 rows of a text body: index and second field (texture id, by the MATERIAL5 layout; inferred).
    bob1 refuses these bodies (the engine maps each to the nearest global material at run time)."""
    out = []
    for m in MATERIAL3.finditer(data):
        f = [x.strip() for x in m.group(1).decode('latin1').split(';') if x.strip()]
        out.append(dict(kind='material3', index=_num(f[0]) if f else None,
                        texture_id_inferred=_num(f[1]) if len(f) > 1 else None))
    return out


def body_summary(data):
    out = dict(sha256=hashlib.sha256(data).hexdigest()[:16], format='binary' if data[:3] == b'BOB' else 'text')
    try:
        tree = bob1.parse(data)
        out['materials'] = [material_summary(m) for m in bob1.materials(tree)]
    except bob1.FormatError as err:
        mats = material3_records(data)
        if not mats:
            out['error'] = str(err)[:200]
            return out
        out['materials'] = mats
        try:                                           # geometry only: the same text without the MATERIAL3 rows
            tree = bob1.parse(MATERIAL3.sub(b'', data))
            out['geometry_from'] = 'material3_rows_stripped'
        except bob1.FormatError as err2:
            out['error'] = str(err2)[:200]
            return out
    try:
        out['extents'] = extents(bob1.lods(tree)[0])
    except bob1.FormatError as err:
        out['error'] = str(err)[:200]
    return out


def body_textures(summary):
    """Distinct texture names of a body summary, lower case; numeric legacy ids as id:N, NULL dropped."""
    out = set()
    for m in summary.get('materials', []):
        if m['kind'] == 'effect':
            out.update(v.lower() for v in m['textures'].values() if v and v.upper() != 'NULL')
        elif m['kind'] == 'classic':
            t = m['texture']
            if isinstance(t, int):
                out.add(f'id:{t}')
            elif t:
                out.add(t.lower())
            out.update(x.lower() if isinstance(x, str) else f'id:{x}' for x in m['maps'])
        elif m.get('texture_id_inferred') is not None:
            out.add(f'id:{m["texture_id_inferred"]}')
    return sorted(out)


def assets_reader(assets):
    def read(name):
        e = bob1.resolve_body(assets, name)
        return assets.read_entry(e), f'{e["source"]}:{e["path"]}'
    return read


def numeric_hints(assets):
    """For an absent v\\NNNNN body: other members named NNNNN under objects/ (e.g. objects/cut/). A hint for the
    lab page only; the engine's mapping of such ids is not established here."""
    index = collections.defaultdict(list)
    for k in assets.entries:
        m = re.fullmatch(r'(?:addon2?/)?(objects/\w+/(\d{5}))\.\w+', k)
        if m and not m.group(1).startswith('objects/v/'):
            index[m.group(2)].append(k)
    return lambda name: sorted(index.get(name[-5:], [])) if re.fullmatch(r'v\\\d{5}', name) else []


def census(assets, read_body=None, hint=None):
    """Census of one view. read_body(name) -> (decoded bytes, provenance) raising FileNotFoundError or
    bob1.FormatError; default resolves the engine's winning body in `assets`. hint(name) -> member list for an
    absent body (default numeric_hints)."""
    if read_body is None:
        read_body, hint = assets_reader(assets), hint or numeric_hints(assets)
    hint = hint or (lambda name: [])
    tex_index = collections.defaultdict(list)
    for k in getattr(assets, 'entries', {}):
        m = re.fullmatch(r'(?:addon2?/)?tex/true/(\d+)\.\w+', k)
        if m:
            tex_index[int(m.group(1))].append(k)

    def texture_members(mat):
        return sorted(tex_index.get(mat, [])) if isinstance(mat, int) else []
    tables, texts = {}, {}
    for name in ('TBullets', 'Effects', 'TShields', 'TShips', 'TDocks', 'TFactories', 'TAsteroids', 'TMissiles',
                 'Particles3'):
        data, prov = assets.get(f'types/{name}.txt')
        texts[name] = data.decode('latin1')
        tables[name] = dict(source=prov['source'], member=prov['member'], sha256=prov['decoded_sha256'][:16])
    bodies = {}

    def body(name):
        key = bob1.body_stem(name).lower()
        if key not in bodies:
            try:
                data, prov = read_body(name)
                bodies[key] = dict(name=name, resolved=prov, **body_summary(data))
            except (FileNotFoundError, bob1.FormatError) as err:
                bodies[key] = dict(name=name, error=f'{type(err).__name__}: {str(err)[:160]}')
                if isinstance(err, FileNotFoundError) and hint(name):
                    bodies[key]['other_members_hint'] = hint(name)
            bodies[key]['textures'] = body_textures(bodies[key])
        return key

    effects_all = parse_effects(texts['Effects'])
    particles_all = parse_particles(texts['Particles3'])
    effects = {}

    def effect(eid):
        if not isinstance(eid, int) or eid <= 0 or eid in effects:
            return
        eff = effects_all.get(eid)
        if eff is None:
            effects[eid] = dict(missing=True)
            return
        effects[eid] = eff
        for el in eff['elements']:
            bn = element_body_name(el.get('bodyid'))
            if bn:
                el['body_key'] = body(bn)

    weapons = parse_tbullets(texts['TBullets'])
    trails = {}
    for w in weapons:
        w['body_key'] = body(element_body_name(w['body']) or w['body'])
        for k in EFFECT_COLUMNS:
            effect(w[k])
        w['death_effect'] = w['impact_effect'] if 'area_flak' in w['flag_bits'] else None
        pid = w['trail_particles']
        if isinstance(pid, int) and pid > 0 and pid not in trails:
            em = particles_all.get(pid)
            trails[pid] = dict(missing=True) if em is None else dict(emitters=[
                dict(e, texture_members=texture_members(e['mat_id'])) for e in em])
    death = dict(default_row=DEFAULT_DEATH_ROW, columns=dict(DEATH_COLUMNS, TBullets_area_flak=20),
                 zero_rule='0 counted as the default row (inferred)',
                 ship_classes=ship_death_rows(texts['TShips']))
    for name in ('TDocks', 'TFactories', 'TMissiles'):
        death[name] = {str(r): n for r, n in sorted(collections.Counter(
            r for _, r in death_column(texts[name], DEATH_COLUMNS[name])).items(), key=lambda x: str(x[0]))}
    death['TAsteroids'] = [dict(id=n, row=r) for n, r in death_column(texts['TAsteroids'], DEATH_COLUMNS['TAsteroids'])]
    death['TBullets_area_flak'] = [dict(id=w['weapon_id'], row=w['death_effect']) for w in weapons
                                   if w['death_effect'] is not None]
    rows = {int(r) for c in death['ship_classes'].values() for r in c}
    rows |= {int(r) for k in ('TDocks', 'TFactories', 'TMissiles') for r in death[k]}
    rows |= {x['row'] for x in death['TAsteroids'] + death['TBullets_area_flak']} | {DEFAULT_DEATH_ROW}
    death['rows'] = sorted(r for r in rows if isinstance(r, int))
    for r in death['rows']:
        effect(r)
    return dict(tables=tables, column_labels=dict(
                    weapon_id=48, body=0, lifetime_ms=10, speed=11, flag_word=12, light_colour=[13, 14, 15],
                    size=[16, 17], beam_length=18, **EFFECT_COLUMNS, trail_particles=TRAIL_COLUMN, col24_not_effect=24,
                    source='docs/reverse-engineering/weapon-effect-spawn.md sections 1 and 4',
                    note='effect value <= 0 read as none; death_effect = impact_effect when flag 0x100'),
                weapons=weapons, effects={str(k): v for k, v in sorted(effects.items())},
                trails={str(k): v for k, v in sorted(trails.items())}, death_rows=death,
                bodies=dict(sorted(bodies.items())), shields=parse_tshields(texts['TShields']),
                ships_engine_context=tships_engine_classes(texts['TShips']))


def summary(view, result):
    w = result['weapons']
    eff = result['effects']

    def present(key):
        return {x[key] for x in w if str(x[key]) in eff and not eff[str(x[key])].get('missing')}
    textures = set().union(*[b['textures'] for b in result['bodies'].values()]) if result['bodies'] else set()
    errors = sum(1 for b in result['bodies'].values() if 'error' in b)
    flags = sorted({x['flag_word'] for x in w}, key=lambda v: (isinstance(v, str), v))
    bits = collections.Counter(b for x in w for b in x['flag_bits'])
    eedf = sorted({n for e in eff.values() for el in e.get('elements', []) for n in el['flag_names']})
    d = result['death_rows']
    ship = ' '.join(c.removeprefix('OBJ_SHIP_') + ':' + ','.join(f'{r}x{n}' for r, n in rows.items())
                    for c, rows in d['ship_classes'].items())
    t = result['tables']['TBullets']
    return '\n'.join([
        f'{view}: TBullets {t["source"]}:{t["member"]} {t["sha256"]}',
        f'  weapons={len(w)} distinct bullet bodies={len({x["body_key"] for x in w})} '
        f'distinct impact rows={len(present("impact_effect"))} flight rows={len(present("flight_effect"))} '
        f'muzzle rows={len(present("muzzle_effect"))} trails={len(result["trails"])} referenced effects={len(eff)}',
        f'  bodies resolved={len(result["bodies"]) - errors}/{len(result["bodies"])} distinct textures={len(textures)}',
        f'  flag word values={flags}',
        f'  flag bits={dict(sorted(bits.items()))}',
        f'  element flags present={len(eedf)}: {" ".join(f.replace("EEDF_", "") for f in eedf)}',
        f'  death rows={d["rows"]} docks={d["TDocks"]} factories={d["TFactories"]} missiles={d["TMissiles"]} '
        f'asteroids={sorted({x["row"] for x in d["TAsteroids"]})} flak={len(d["TBullets_area_flak"])}',
        f'  ship class -> death row x ships: {ship}',
        f'  shields={len(result["shields"])} ship rows={result["ships_engine_context"]["rows"]} '
        f'classes={len(result["ships_engine_context"]["distinct_engine_effects_per_class"])}'])


def views():
    ap, fl = pathlib.Path('/mnt/c/X3'), pathlib.Path('/mnt/c/X3FL')
    return {'stock_ap': lambda: Assets(ap, catalogues=STOCK_AP_CATALOGUES),
            'mayhem3': lambda: Assets(ap, edition='ap'),
            'fl': lambda: Assets(fl, edition='fl')}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for view, make in views().items():
        result = census(make())
        path = OUT / f'{view}.json'
        path.write_text(json.dumps(dict(view=view, **result), indent=1, sort_keys=False) + '\n')
        print(summary(view, result))
        print(f'  -> {path.relative_to(REPO)} ({path.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
