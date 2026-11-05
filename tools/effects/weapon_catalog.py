#!/usr/bin/env python3
"""Weapon catalogue: what each SS_BULLET_* id is, per catalogue view (read-only, host side).

  python3 tools/effects/weapon_catalog.py [--check]

Inputs: the weapon-effects census verification/results/weapon-effects-census/<view>.json
(tools/effects/weapon_effects_census.py: TBullets columns, bodies, Effects rows), the classifier
verification/results/weapon-effects-census/classify_preview.py (style), and the view's catalogues
(weapon_effects_census.views()) for TLaser, TShips, TCockpits and the English text files.
Outputs: tools/effects/weapon_catalog/<view>.json (tracked, names and numbers only) and
docs/reference/weapon-catalog.md. --check writes nothing and fails when either differs from what the inputs give.

Column meanings (docs/reverse-engineering/weapon-effect-spawn.md sections 1, 2 and 4; game-scale-and-ship-classes.md
sections 3 and 8): TLaser col 6 name text id (page 17), col 7 refire interval ms (+0x44), col 9 bullet index
(+0x4c), col 5 subtype SG_LASER_* (the EXE's table 0x00552888 numbers them 0..31 in SG_LASER_ORDER). TShips col 18
main-cockpit laser mask, cols 31..42 six (TCockpits index, turret) pairs, col 52 OBJ_SHIP_* class; TCockpits col 8
the turret's laser mask. A mask bit n admits the lasers whose subtype value is n (inferred: the Kha'ak
SS_COCKPIT_K_BIG_ALL mask is bit 31 = SG_LASER_KYON alone, a TS turret admits bit 28 = SG_LASER_SPECIAL, the
mining laser). Range = lifetime ms x speed / 1000 in engine units, 500 units = 1 m.
"""
import argparse
import collections
import importlib.util
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/analysis'))
sys.path.insert(0, str(REPO / 'tools/effects'))
import weapon_effects_census as wec  # noqa: E402


def _load_classifier():
    spec = importlib.util.spec_from_file_location(
        'classify_preview', REPO / 'verification/results/weapon-effects-census/classify_preview.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


classify = _load_classifier()

CENSUS = REPO / 'verification/results/weapon-effects-census'
OUT = REPO / 'tools/effects/weapon_catalog'
NOTE = REPO / 'docs/reference/weapon-catalog.md'
VIEWS = ('stock_ap', 'mayhem3', 'fl')
VIEW_LABELS = {'stock_ap': 'Stock AP (pinned 01-13 + addon/01-04)', 'mayhem3': 'Mayhem 3 (installed AP layers)',
               'fl': "Farnham's Legacy (addon2)"}
UNITS_PER_M = 500
# X3AP.exe 0x00552888 (TLaser col 5 reader table), value = position [m].
SG_LASER_ORDER = ('IRE', 'PAC', 'MASS', 'ARGON_LIGHT', 'TELADI_LIGHT', 'PARANID_LIGHT', 'HEPT', 'BORON_LIGHT', 'PBE',
                  'PIRATE_LIGHT', 'TERRAN_LIGHT', 'CIG', 'BORON_MEDIUM', 'SPLIT_MEDIUM', 'TERRAN_MEDIUM', 'TELADI_AF',
                  'ARGON_AF', 'SPLIT_AF', 'PARANID_AF', 'TERRAN_AF', 'PPC', 'BORON_HEAVY', 'TELADI_HEAVY',
                  'PIRATE_HEAVY', 'TERRAN_HEAVY', 'ARGON_BEAM', 'PARANID_BEAM', 'TERRAN_BEAM', 'SPECIAL', 'UNKNOWN1',
                  'UNKNOWN2', 'KYON')
SG_LASER = {f'SG_LASER_{n}': i for i, n in enumerate(SG_LASER_ORDER)}
TLASER_NAME, TLASER_REFIRE, TLASER_BULLET, TLASER_SUBTYPE = 6, 7, 9, 5
TSHIPS_MAIN_MASK, TSHIPS_COCKPITS, TSHIPS_CLASS = 18, range(31, 43, 2), 52
TCOCKPIT_MASK = 8
TEXT_PAGE = 17
# Size tiers by OBJ_SHIP_* class (prefix removed). Classes outside these (drones, satellites, mines, UFO, Kha'ak
# cluster, goner, astronaut, numeric ids) do not decide the size.
SIZE_TIERS = (('small', ('M5', 'M4', 'M3')),
              ('medium', ('M6', 'M8', 'TS', 'TS_PIRATE', 'TP', 'TM')),
              ('big', ('M7', 'MISSILE_FRIGATE', 'M2', 'M1', 'M0', 'TL', 'DRONECARRIER')))
SIZE_ORDER = ('small', 'medium', 'big', 'other', 'unmounted')
SIZE_SHARE = 0.1        # a tier with fewer of the mounting rows is an outlier (one odd M3 row fitting a PSP)
# TShips rows left out of used-by and size: a static turret object classed M5 and station-like rows classed M6/TL,
# each with a catch-all mask of 26-27 subtypes (measured in stock AP and FL).
EXCLUDED_SHIPS = ('SS_SH_TURRET', 'SS_SH_TLS', 'SS_SH_TL_P', 'SS_SH_TL_P2')
SIZE_RULE = ('size = the smallest tier holding at least 10 % of the tier-class TShips rows that can mount a laser '
             'firing the bullet (fewer is treated as an outlier row): small '
             'M5/M4/M3; medium M6/M8/TS/TS_PIRATE/TP/TM; big M7/MISSILE_FRIGATE/M2/M1/M0/TL/DRONECARRIER; other = only '
             'non-tier classes (drones, satellites, UFO, Kha\'ak cluster, ...) mount it; unmounted = no ship mask admits '
             'its laser subtype or no TLaser row fires it; the TShips rows ' + ', '.join(EXCLUDED_SHIPS)
             + ' (a static turret and station-like rows with catch-all masks) are left out')
FLAG_LEGEND = {   # weapon-effect-spawn.md section 1 flag table
    'unknown_0x1': '0x1: set in rows, no EXE test found',
    'beam_gun_tied': '0x2: beam kind A, tied to the gun and refreshed each fire tick',
    'beam_kind_b': '0x4: beam kind B, length col 18',
    'beam_fixed_length': '0x8: fixed length col 18',
    'unknown_0x10': '0x10: set in rows, no EXE test found',
    'hull_damage_through_shields': '0x20: hull damage even when the shields absorbed',
    'ammunition': '0x40: uses ammunition',
    'repair': '0x80: repair, hull damage negated',
    'area_flak': '0x100: area/flak, explodes on death with the impact row (col 20), speed zeroed',
    'on_hit_slow': '0x200: on hit slows the victim (cols 28/29)',
    'on_hit_energy_drain': '0x400: on hit drains victim energy (col 30)',
    'on_hit_col31_32': '0x800: on-hit victim effect from cols 31/32',
    'fragments': '0x1000: spawns fragment bullets at lifetime expiry (cols 33/34)',
    'continuous_beam': '0x2000: continuous beam with its own end time (col 27)',
}
STYLE_LEGEND = {
    'beam': 'flag 0x2 or 0x4',
    'wave': 'flag 0x8 (fixed length)',
    'tracer': f'body LOD-0 z/xy >= {classify.ELONGATION_TRACER}',
    'slug': f'{classify.ELONGATION_SLUG} <= z/xy < {classify.ELONGATION_TRACER}, or no body extents',
    'orb': f'z/xy < {classify.ELONGATION_SLUG}',
    '+burst': 'the bullet ends in its own death explosion (0x100) or fragments (0x1000)',
}


def data_rows(text):
    return wec.data_rows(text)


def row_id(f, prefix):
    return next((x for x in reversed(f) if x.startswith(prefix)), None)


def _int(s):
    try:
        return int(str(s).strip())
    except ValueError:
        return None


# ---------- text files ----------

COMMENT = re.compile(r'(?<!\\)\((?:[^()\\]|\\.)*?(?<!\\)\)')


def clean_text(s):
    """Displayed form of a t-file string: unescaped (...) is an author comment the game drops; \\( \\) are literal."""
    return COMMENT.sub('', s).replace('\\(', '(').replace('\\)', ')').strip()


def text_table(documents):
    """{(page id, text id): string} from t-file XML documents in load order; a later document overrides."""
    texts = {}
    for doc in documents:
        root = ET.fromstring(doc)
        for page in root.findall('page'):
            pid = int(page.attrib['id'])
            for t in page.findall('t'):
                texts[pid, int(t.attrib['id'])] = ''.join(t.itertext())
    return texts


def resolver(texts):
    """resolve(page, id) -> displayed string or None. Page versions (17, 300017, 350017, ...) are tried highest
    first, as sector_fog_census.names does; {page,id} references are expanded."""
    versions = collections.defaultdict(list)
    for pid, _ in texts:
        versions[pid % 10000].append(pid)
    versions = {p: sorted(set(v), reverse=True) for p, v in versions.items()}

    def raw(page, ident, depth=0):
        if depth > 12:
            return None
        value = next((texts[p, ident] for p in versions.get(page % 10000, []) if (p, ident) in texts), None)
        if value is None:
            return None
        parts = []
        pos = 0
        for m in re.finditer(r'\{(\d+),\s*(\d+)\}', value):
            sub = raw(int(m[1]), int(m[2]), depth + 1)
            if sub is None:
                return None
            parts.append(value[pos:m.start()] + sub)
            pos = m.end()
        return ''.join(parts) + value[pos:]

    def resolve(page, ident):
        v = raw(page, ident)
        v = clean_text(v) if v is not None else None
        return v or None
    return resolve


T_FILE = re.compile(r'(?:(addon2?)/)?t/(\d{4})-l044\.xml')


def text_files(assets):
    """English t-file keys in load order: base t/ then the expansion's t/, 0001 first then ascending."""
    keys = []
    for k in assets.entries:
        m = T_FILE.fullmatch(k)
        if m and (m[1] is None or m[1] == assets.expansion):
            keys.append((m[1] is not None, int(m[2]), k))
    return [k for *_, k in sorted(keys)]


def assets_resolver(assets):
    keys = text_files(assets)
    docs, prov = [], []
    for k in keys:
        e = assets.entries[k][-1]
        docs.append(assets.read_entry(e))
        prov.append(f'{e["source"]}:{e["path"]}')
    return resolver(text_table(docs)), prov


# ---------- tables ----------

def parse_tlaser(text, resolve):
    out = []
    for i, f in enumerate(data_rows(text)[1:]):
        if len(f) <= TLASER_BULLET:
            continue
        name_id = _int(f[TLASER_NAME])
        refire = _int(f[TLASER_REFIRE])
        out.append(dict(index=i, id=row_id(f, 'SS_LASER'), subtype=f[TLASER_SUBTYPE],
                        subtype_bit=SG_LASER.get(f[TLASER_SUBTYPE]), name_id=name_id,
                        name=resolve(TEXT_PAGE, name_id) if name_id else None,
                        refire_ms=refire, shots_per_min=round(60000 / refire, 1) if refire and refire > 0 else None,
                        bullet=_int(f[TLASER_BULLET])))
    return out


def parse_tcockpits(text):
    return [(_int(f[TCOCKPIT_MASK]) or 0) & 0xffffffff if len(f) > TCOCKPIT_MASK else 0
            for f in data_rows(text)[1:]]


def ship_masks(text, cockpit_masks):
    """([(class, mask)] per TShips row: col 18 OR the masks of the col 31..42 TCockpits indices; EXCLUDED_SHIPS rows
    dropped), bad cockpit index count, excluded row count."""
    out, bad, excluded = [], 0, 0
    for f in data_rows(text)[1:]:
        if len(f) <= TSHIPS_CLASS:
            continue
        if row_id(f, 'SS_SH') in EXCLUDED_SHIPS:
            excluded += 1
            continue
        mask = (_int(f[TSHIPS_MAIN_MASK]) or 0) & 0xffffffff
        for c in TSHIPS_COCKPITS:
            k = _int(f[c])
            if k is None or not 0 <= k < len(cockpit_masks):
                bad += 1
                continue
            mask |= cockpit_masks[k]
        cls = f[TSHIPS_CLASS]
        cls = cls.removeprefix('OBJ_SHIP_') if not cls.lstrip('-').isdigit() else f'class_id:{cls}'
        out.append((cls, mask))
    return out, bad, excluded


def used_by(bit, ships):
    """{class: ship rows whose mask admits subtype bit} sorted by class."""
    if bit is None:
        return {}
    c = collections.Counter(cls for cls, mask in ships if mask >> bit & 1)
    return dict(sorted(c.items()))


def size_of(classes):
    """Size from {class: mounting ship rows}: the smallest tier holding at least SIZE_SHARE of the tier-class rows."""
    tiers = {cls: name for name, members in SIZE_TIERS for cls in members}
    per = collections.Counter()
    for c, n in classes.items():
        if c in tiers:
            per[tiers[c]] += n
    total = sum(per.values())
    for name, _ in SIZE_TIERS:
        if total and per[name] >= SIZE_SHARE * total:
            return name
    return 'other' if classes else 'unmounted'


def class_key(cls):
    order = [c for _, members in SIZE_TIERS for c in members]
    return (order.index(cls) if cls in order else len(order), cls)


# ---------- per-weapon facts ----------

def light_hex(rgb):
    return '#' + ''.join(f'{max(0, min(255, int(v))):02x}' for v in rgb)


def stem(path):
    return re.split(r'[\\/]', str(path))[-1].rsplit('.', 1)[0] if path else path


def texture_label(name):
    """Short texture name: the file stem; a -N name is a types/Animations reference, id:N a legacy texture id."""
    s = stem(name)
    return f'animation {s}' if re.fullmatch(r'-\d+', s or '') else s


def main_texture(textures):
    """The texture a reader would name: not an alpha mask nor a NONE_* placeholder when another exists."""
    for keep in (lambda t: 'alpha' not in t.lower() and not stem(t).lower().startswith('none_'),
                 lambda t: 'alpha' not in t.lower(), lambda t: True):
        found = [t for t in textures if keep(t)]
        if found:
            return found[0]
    return None


def element_summary(el, bodies):
    names = el.get('flag_names', [])
    if 'EEDF_PROPAGATE' in names:
        return f'propagate row {el.get("prop_effect_id")} x{el.get("min_props")}-{el.get("max_props")}'
    if 'EEDF_PARTICLEGENERATOR' in names:
        what = f'particles {el.get("particlenr")}'
    elif 'EEDF_LIGHT' in names:
        what = 'light'
    elif 'EEDF_LENSFLARE' in names:
        what = f'lensflare {el.get("lensflarenr")}'
    else:
        b = bodies.get(el.get('body_key') or '', {})
        tex = main_texture(b.get('textures', []))
        what = stem(el.get('body_key')) or 'no body'
        if tex:
            what += f' [{texture_label(tex)}]'
    size = f' {el.get("minsize")}-{el.get("maxsize")}' if 'minsize' in el else ''
    dur = f' {el.get("duration")} ms' if el.get('duration') else ''
    return what + size + dur


def effect_summary(row, effects, bodies):
    """'<n> elements: a; b' for an Effects row id, None for no reference."""
    if not isinstance(row, int) or row <= 0:
        return None
    e = effects.get(str(row))
    if e is None or e.get('missing'):
        return f'row {row} missing from Effects'
    els = e.get('elements', [])
    return f'{len(els)} element{"s" if len(els) != 1 else ""}: ' + '; '.join(element_summary(el, bodies) for el in els)


def describe(entry):
    """One sentence from the entry's facts only."""
    names = [l['name'] or l['id'] for l in entry['lasers']]
    who = ', '.join(dict.fromkeys(names)) if names else f'{entry["id"]} (no TLaser row fires it)'
    classes = list(entry['used_by'])
    if entry['size'] in ('small', 'medium', 'big'):
        shown = classes[:5]
        rest = len(classes) - len(shown)
        mount = (f'{entry["size"]} weapon, mountable on {", ".join(shown)}'
                 + (f' and {rest} more class{"es" if rest > 1 else ""}' if rest else ''))
    elif entry['size'] == 'other':
        mount = f'mountable only on {", ".join(classes)}'
    else:
        mount = 'no ship row can mount it'
    rof = sorted({l['shots_per_min'] for l in entry['lasers'] if l['shots_per_min']})
    rof_txt = (f', {"/".join(f"{r:g}" for r in rof)} shots/min' if rof else '')
    tex = f', texture {texture_label(entry["texture"])}' if entry.get('texture') else ''
    flags = f'; flags {", ".join(entry["flags"])}' if entry['flags'] else ''
    eff = [f'{k} row {v["row"]}' for k, v in entry['effects'].items() if v]
    eff_txt = f'; {", ".join(eff)}' if eff else ''
    return (f'{who}: {mount}; fires a {entry["style"]} (body {stem(entry["body"])}{tex}) lit {entry["light"]} at '
            f'{entry["speed_mps"]:g} m/s over {entry["range_m"]:g} m{rof_txt}, {entry["damage_shield"]} shield / '
            f'{entry["damage_hull"]} hull damage per shot{flags}{eff_txt}.')


def weapon_entry(w, census, lasers, ships):
    bodies = census['bodies']
    body = bodies.get(w['body_key'], {})
    ext = body.get('extents') or {}
    elongation = ext.get('z_over_xy')
    flag_word = w['flag_word'] if isinstance(w['flag_word'], int) else 0
    textures = body.get('textures', [])
    tex = main_texture(textures)
    fired_by = [l for l in lasers if l['bullet'] == w['index']]
    union = collections.Counter()
    laser_out = []
    for l in fired_by:
        u = used_by(l['subtype_bit'], ships)
        laser_out.append(dict(index=l['index'], id=l['id'], name=l['name'], name_id=l['name_id'],
                              subtype=l['subtype'], refire_ms=l['refire_ms'], shots_per_min=l['shots_per_min'],
                              size=size_of(u), used_by=u))
    bits = {l['subtype_bit'] for l in fired_by if l['subtype_bit'] is not None}
    for cls, mask in ships:
        if any(mask >> b & 1 for b in bits):
            union[cls] += 1
    classes = dict(sorted(union.items(), key=lambda x: class_key(x[0])))
    speed = w['speed'] if isinstance(w['speed'], (int, float)) else 0
    life = w['lifetime_ms'] if isinstance(w['lifetime_ms'], (int, float)) else 0
    effects = {}
    for slot, col in (('muzzle', 'muzzle_effect'), ('flight', 'flight_effect'), ('impact', 'impact_effect'),
                      ('death', 'death_effect')):
        row = w.get(col)
        effects[slot] = (dict(row=row, summary=effect_summary(row, census['effects'], bodies))
                         if isinstance(row, int) and row > 0 else None)
    trail = w.get('trail_particles')
    entry = dict(id=w['weapon_id'], bullet_index=w['index'], lasers=laser_out, size=size_of(classes),
                 used_by=classes, style=classify.style(flag_word, elongation), body=w['body_key'],
                 elongation=elongation, texture=tex, textures=textures,
                 light=light_hex(w['light_colour']), light_rgb=w['light_colour'],
                 speed_mps=round(speed / UNITS_PER_M, 1), lifetime_ms=life,
                 range_m=round(life * speed / 1000 / UNITS_PER_M, 1),
                 damage_shield=w['damage_shield'], damage_hull=w['damage_hull'],
                 flag_word=f'{flag_word:#x}', flags=w['flag_bits'], effects=effects,
                 trail_particles=trail if isinstance(trail, int) and trail > 0 else None)
    entry['description'] = describe(entry)
    return entry


def sort_key(entry):
    name = next((l['name'] for l in entry['lasers'] if l['name']), None) or entry['id']
    return (SIZE_ORDER.index(entry['size']), name.lower(), entry['id'])


def build_view(view, census, tlaser_text, tships_text, tcockpits_text, resolve, sources):
    lasers = parse_tlaser(tlaser_text, resolve)
    ships, bad, excluded = ship_masks(tships_text, parse_tcockpits(tcockpits_text))
    weapons = sorted((weapon_entry(w, census, lasers, ships) for w in census['weapons']), key=sort_key)
    unresolved = [dict(index=l['index'], id=l['id'], name_id=l['name_id']) for l in lasers if not l['name']]
    unknown_subtype = [l['id'] for l in lasers if l['subtype_bit'] is None]
    orphan = [l['id'] for l in lasers if not any(l['bullet'] == w['index'] for w in census['weapons'])]
    return dict(view=view, label=VIEW_LABELS.get(view, view), sources=sources,
                rules=dict(size=SIZE_RULE, range='lifetime_ms x speed / 1000 / 500 m',
                           rate='60000 / TLaser col 7 refire interval ms',
                           mounting='TShips col 18 | TCockpits col 8 of TShips cols 31,33,..41; bit = SG_LASER value '
                                    '(inferred)'),
                counts=dict(weapons=len(weapons), lasers=len(lasers),
                            laser_names_resolved=len(lasers) - len(unresolved),
                            laser_names_unresolved=len(unresolved),
                            weapons_without_laser=sum(1 for w in weapons if not w['lasers']),
                            ships=len(ships), excluded_ships=excluded, bad_cockpit_refs=bad),
                unresolved_names=unresolved, unknown_subtypes=unknown_subtype, lasers_without_bullet=orphan,
                weapons=weapons)


def load_view(view, make_assets, census_dir=CENSUS):
    census = json.loads((census_dir / f'{view}.json').read_text())
    assets = make_assets()
    resolve, text_prov = assets_resolver(assets)
    texts, sources = {}, {}
    for name in ('TLaser', 'TShips', 'TCockpits'):
        data, prov = assets.get(f'types/{name}.txt')
        texts[name] = data.decode('latin1')
        sources[name] = f'{prov["source"]}:{prov["member"]} {prov["decoded_sha256"][:16]}'
    t = census['tables']['TBullets']
    sources['TBullets'] = f'{t["source"]}:{t["member"]} {t["sha256"]}'
    sources['text'] = text_prov
    return build_view(view, census, texts['TLaser'], texts['TShips'], texts['TCockpits'], resolve, sources)


# ---------- markdown ----------

def md_cell(s):
    return str(s).replace('|', '\\|').replace('\n', ' ')


def markdown(views):
    out = ['# Weapon catalogue', '',
           'Generated by `python3 tools/effects/weapon_catalog.py` from the weapon-effects census, TLaser, TShips, '
           'TCockpits and the English text files of each catalogue view; do not edit by hand '
           '(`--check` fails when this file is stale). Per-weapon JSON with effect element summaries: '
           '[`tools/effects/weapon_catalog/`](../../tools/effects/weapon_catalog/). Column sources: '
           '[weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) sections 1 and 2, '
           '[game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md) sections 3 and 8.',
           '', '## Legend', '',
           f'* **Size**: {SIZE_RULE}.',
           '* **Used by**: ship classes (TShips col 52) with the number of TShips rows whose main-cockpit mask (col 18) '
           'or a turret cockpit mask (TCockpits col 8 of the indices in TShips cols 31..41) has the bit of the laser\'s '
           'SG_LASER_* subtype (TLaser col 5; values from the EXE table 0x00552888). The bit-to-subtype reading is '
           'inferred; lasers sharing a subtype (SPECIAL, KYON, UNKNOWN1/2) share their used-by.',
           '* **Name**: TLaser col 6 on text page 17 (highest page version first, `{page,id}` expanded, `(...)` '
           'comments dropped). **Rate**: 60000 / TLaser col 7 (refire interval ms).',
           '* **Speed** = TBullets col 11 / 500 m/s; **range** = col 10 (ms) x col 11 / 1000 / 500 m; **damage** = '
           'col 7 shield / col 22 hull per shot; **light** = cols 13-15 as hex.',
           '* **Style** (classifier `verification/results/weapon-effects-census/classify_preview.py`): '
           + '; '.join(f'`{k}` {v}' for k, v in STYLE_LEGEND.items()) + '.',
           '* **Flags** (TBullets col 12): ' + '; '.join(f'`{k}` {v}' for k, v in FLAG_LEGEND.items()) + '.',
           '* **Effects**: Effects row ids of muzzle (col 21), flight (col 19), impact (col 20) and, with 0x100, '
           'death (= impact).', '']
    for view, d in views.items():
        c = d['counts']
        out += [f'## {d["label"]} (`{view}`)', '',
                f'{c["weapons"]} bullets, {c["lasers"]} TLaser rows ({c["laser_names_resolved"]} names resolved, '
                f'{c["laser_names_unresolved"]} unresolved), {c["weapons_without_laser"]} bullets no TLaser row fires, '
                f'{c["ships"]} TShips rows. TBullets {d["sources"]["TBullets"]}; TLaser {d["sources"]["TLaser"]}; '
                f'TShips {d["sources"]["TShips"]}; TCockpits {d["sources"]["TCockpits"]}.', '']
        if d['unresolved_names']:
            out += ['Unresolved names: ' + ', '.join(f'{u["id"]} (TLaser {u["index"]}, text id {u["name_id"]})'
                                                     for u in d['unresolved_names']) + '.', '']
        if d['unknown_subtypes']:
            out += ['Subtype not in the EXE table: ' + ', '.join(d['unknown_subtypes']) + '.', '']
        out += ['| bullet | laser (name) | size | used by | style | m/s | range m | shots/min | dmg S/H | light | '
                'flags | effects M/F/I/D |',
                '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
        for w in d['weapons']:
            lasers = '<br>'.join(f'{l["id"].removeprefix("SS_LASER_") if l["id"] else l["index"]} '
                                 f'({l["name"] or "?"})' for l in w['lasers']) or '-'
            used = ' '.join(f'{k}:{v}' for k, v in w['used_by'].items()) or '-'
            rof = '/'.join(f'{l["shots_per_min"]:g}' for l in w['lasers'] if l['shots_per_min']) or '-'
            eff = '/'.join(str(v['row']) if v else '-' for v in w['effects'].values())
            out.append('| ' + ' | '.join(md_cell(x) for x in (
                w['id'].removeprefix('SS_BULLET_'), lasers, w['size'], used, w['style'], f'{w["speed_mps"]:g}',
                f'{w["range_m"]:g}', rof, f'{w["damage_shield"]}/{w["damage_hull"]}', w['light'],
                ' '.join(w['flags']) or '-', eff)) + ' |')
        out.append('')
    return '\n'.join(out)


def dump(d):
    return json.dumps(d, indent=1, ensure_ascii=False) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check', action='store_true', help='fail when an output differs from the inputs')
    args = ap.parse_args(argv)
    makers = wec.views()
    views = {v: load_view(v, makers[v]) for v in VIEWS}
    outputs = {OUT / f'{v}.json': dump(d) for v, d in views.items()}
    outputs[NOTE] = markdown(views)
    stale = [p for p, text in outputs.items() if not p.exists() or p.read_text() != text]
    for v, d in views.items():
        c = d['counts']
        print(f'{v}: weapons={c["weapons"]} lasers={c["lasers"]} names resolved={c["laser_names_resolved"]} '
              f'unresolved={c["laser_names_unresolved"]}'
              + (f' ({", ".join(u["id"] for u in d["unresolved_names"])})' if d['unresolved_names'] else '')
              + f' bullets without laser={c["weapons_without_laser"]} ships={c["ships"]} '
              f'excluded={c["excluded_ships"]} bad cockpit refs={c["bad_cockpit_refs"]} sizes='
              + str(dict(collections.Counter(w['size'] for w in d['weapons']))))
    if args.check:
        if stale:
            print('FAIL: stale ' + ', '.join(str(p.relative_to(REPO)) for p in stale)
                  + '; run python3 tools/effects/weapon_catalog.py')
            return 1
        print(f'PASS: {len(outputs)} outputs match the inputs')
        return 0
    for p in stale:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(outputs[p])
    print('updated ' + ', '.join(str(p.relative_to(REPO)) for p in stale) if stale else 'already current')
    return 0


if __name__ == '__main__':
    sys.exit(main())
