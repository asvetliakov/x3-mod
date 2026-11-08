#!/usr/bin/env python3
"""Generate <game>/x3m/weapon_effects.json, the table the DLL's weapon-effects classifier reads (docs/architecture/
weapon-effects.md 3.2 and 4; the engine_bodies.json contract of tools/effects/engine_bodies.py).

  python3 tools/effects/weapon_effects_table.py --game-root <game> --out <path outside the game root>
         [--stock-only] [--edition auto|ap|fl] [--overrides tools/effects/weapon_overrides.json]

Per TBullets row index (the bullet object's subtype): the SS_BULLET_* id, the vanilla body's elongation z_over_xy
(LOD-0 half-extents, the census reader tools/effects/weapon_effects_census.py) and lateral half-extent, the body
diffuse texture's alpha-weighted mean colour as 8-bit sRGB (engine_bodies.py's texture_colour, the first body texture
that decodes), the shape the elongation gives (weapon_effects_core.h tracer/slug/orb thresholds, informational: the
DLL applies the same thresholds to z_over_xy and the live flag word), and the curated override fields from
tools/effects/weapon_overrides.json resolved to the row (style, tint as 8-bit sRGB, calibre; by_table wins over by_id).
Schema 2 (step 4, docs/architecture/bolt-muzzle-look.md section 1) adds per row the mount tier (mount_tier: the
catalogue's `size` of the TShips/TCockpits rows whose laser masks admit a TLaser row firing the bullet,
tools/effects/weapon_catalog.py's size_of: the smallest tier holding at least 10 % of the mounting tier-class rows,
small -> fighter, medium -> corvette, big -> capital; 2026-11-08 #4, before it any fighter row made a fighter mount, so
one odd M3 row fitting the PSP set its width; a bullet no laser fires, the fragments, or no mounting row -> fighter,
the note's unknown 7 fallback) and the range (range_m: lifetime ms x speed / 1000 / 500, metres; null when either column
is missing). The bolt hand pins of an override (bolt-muzzle-look.md 11.5) are written as the entry's bolt_e_body,
bolt_w_orb, bolt_halo and bolt_tint keys only where pinned (the DLL reads them; an older DLL skips them).
Per TMissiles row only the count. Also the TBullets table's decoded hash and row count (the DLL compares the count
with the live *0x00607038 and disables the lookups on a mismatch), the generator's schema and its `tool` field (the
ownership mark manage.py and x3m-regenerate test before replacing or removing the file). Only names, numbers and
hashes are written; no game bytes. The game tree is read-only (--out inside it is refused).
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/effects'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import engine_bodies  # noqa: E402
import weapon_effects_census as census_reader  # noqa: E402
import weapon_catalog  # noqa: E402

SCHEMA = 2  # 2: mount_tier and range_m per bullet row (step 4); the DLL reads 1 and 2
TOOL = 'tools/effects/weapon_effects_table.py'
OVERRIDES = ROOT / 'tools/effects/weapon_overrides.json'
# weapon_effects_core.h 4.1 (the host test pins both against the header).
TRACER_MIN_ELONGATION, SLUG_MIN_ELONGATION = 4.0, 1.5
SHAPES = ('tracer', 'slug', 'orb', 'wave', 'beam')
# The hull-hit values an override may pin, written as the entry's "hull" key only when pinned: the variants of
# docs/architecture/weapon-look.md 3.1 (the repair laser: glow and ember only, the weapon tint; the tug: the glow only,
# pale blue) and the classes of docs/architecture/hull-impact-classes.md section 2 (beam is not pinnable: a bolt pinned
# beam would hold a spot with no tick stream).
HULL_VARIANTS = ('repair', 'tug', 'energy', 'ballistic', 'plasma', 'heavy', 'ion')
# The override file's schemas: 1 (the variants only) and 2 (the classes, 2026-11-06).
OVERRIDE_SCHEMAS = (1, 2)
# The bolt hand pins (docs/architecture/bolt-muzzle-look.md 11.5, 2026-11-08): the body radiance E_b (0.01..2.55), the orb
# diameter in w (0.01..2.55), the halo (false: none) and the tint source ('texture': the texture mean even over a saturated
# light colour), written under the same names.
BOLT_PINS = ('bolt_e_body', 'bolt_w_orb', 'bolt_halo', 'bolt_tint')


class GenerateError(Exception):
    pass


# Step 4 (bolt-muzzle-look.md section 1): the mount tiers by OBJ_SHIP_* class (prefix removed).
MOUNT_TIERS = ('fighter', 'corvette', 'capital')
SIZE_TIERS = {'small': 'fighter', 'medium': 'corvette', 'big': 'capital'}  # weapon_catalog.SIZE_TIERS' names


def mount_tier_of(classes):
    """The mount tier from the ship rows that mount the weapon ({class: rows}; an iterable of class names counts one row
    each): the catalogue's size (weapon_catalog.size_of, the 10 % rule) by SIZE_TIERS; 'other' and unmounted -> fighter."""
    counts = dict(classes) if isinstance(classes, dict) else {c: 1 for c in classes}
    return SIZE_TIERS.get(weapon_catalog.size_of(counts), 'fighter')


def mount_classes(assets):
    """{bullet index: {OBJ_SHIP_* class: ship rows mounting a laser that fires it}} from the selected layers' TLaser,
    TShips and TCockpits (weapon_catalog's mask rule and excluded rows; a ship row counted once per bullet, as the
    catalogue's used_by union)."""
    texts = {}
    for name in ('TLaser', 'TShips', 'TCockpits'):
        data, _ = assets.get(f'types/{name}.txt')
        texts[name] = data.decode('latin1')
    lasers = weapon_catalog.parse_tlaser(texts['TLaser'], lambda page, ident: None)
    ships, _, _ = weapon_catalog.ship_masks(texts['TShips'], weapon_catalog.parse_tcockpits(texts['TCockpits']))
    bits = {}
    for laser in lasers:
        if laser['bullet'] is not None and laser['subtype_bit'] is not None:
            bits.setdefault(laser['bullet'], set()).add(laser['subtype_bit'])
    out = {}
    for bullet, bs in bits.items():
        counts = {}
        for cls, mask in ships:
            if any(mask >> b & 1 for b in bs):
                counts[cls] = counts.get(cls, 0) + 1
        if counts:
            out[bullet] = counts
    return out


def range_of(w):
    """Speed x lifetime in metres (the catalogue's range_m), None when either column is missing."""
    speed, life = w.get('speed'), w.get('lifetime_ms')
    if not isinstance(speed, (int, float)) or not isinstance(life, (int, float)) or speed <= 0 or life <= 0:
        return None
    return round(life * speed / 1000 / weapon_catalog.UNITS_PER_M, 1)


def shape_from_elongation(e):
    if e is None:
        return 'slug'
    return 'tracer' if e >= TRACER_MIN_ELONGATION else 'slug' if e >= SLUG_MIN_ELONGATION else 'orb'


def linear_to_srgb8(v):
    v = min(1.0, max(0.0, float(v)))
    s = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
    return int(round(s * 255))


def load_overrides(path=OVERRIDES):
    """The curated overrides; an absent file (the frozen x3m-regenerate carries none yet) is the empty set."""
    if not Path(path).is_file():
        return dict(schema=2, by_id={}, by_table={})
    data = json.loads(Path(path).read_text())
    if data.get('schema') not in OVERRIDE_SCHEMAS or not isinstance(data.get('by_id'), dict) or \
            not isinstance(data.get('by_table'), dict):
        raise GenerateError(f'{path}: schema 1 or 2 with by_id and by_table objects expected')
    return data


def override_for(overrides, table_hash, weapon_id):
    """The merged override fields of one row ({} when none): by_id, then by_table[hash][id] on top."""
    out = dict(overrides['by_id'].get(weapon_id, {}))
    out.update((overrides['by_table'].get(table_hash) or {}).get(weapon_id, {}) or {})
    return {k: v for k, v in out.items() if k in ('style', 'tint', 'calibre', 'hull') + BOLT_PINS}


def diffuse_names(body):
    """The body's diffuse texture names in material order (an effect material's t_DiffuseTexture, a classic material's
    texture); never an alpha mask or the NONE_WHITE placeholder."""
    out = []
    for m in (body or {}).get('materials', []):
        if m.get('kind') == 'effect':
            names = [v for k, v in (m.get('textures') or {}).items() if 'diffuse' in k.lower()]
        elif m.get('kind') == 'classic' and isinstance(m.get('texture'), str):
            names = [m['texture']]
        else:
            names = []
        for n in names:
            low = n.lower()
            if n and low != 'null' and 'none_white' not in low and 'alpha' not in low.rsplit('\\', 1)[-1] and n not in out:
                out.append(n)
    return out


def row_entry(w, body, colour, override, mounts=None):
    ext = (body or {}).get('extents') or {}
    e = ext.get('z_over_xy')
    half = ext.get('half_extents')
    entry = dict(row=w['index'], id=w['weapon_id'], body=w['body_key'],
                 z_over_xy=e, half_extent=round(max(half[0], half[1]), 4) if half else None,
                 texture_rgb=[int(round(min(255.0, max(0.0, float(c))))) for c in colour['mean']] if colour else None,
                 shape=shape_from_elongation(e), style=None, tint=None, calibre=None,
                 mount_tier=mount_tier_of(mounts or ()), range_m=range_of(w),
                 mount_fallback=not mounts)  # no mounting ship class: the fighter fallback (unknown 7), logged
    if 'style' in override:
        if override['style'] not in SHAPES:
            raise GenerateError(f"override for {w['weapon_id']}: unknown style {override['style']!r}")
        entry['style'] = override['style']
    if 'tint' in override:
        entry['tint'] = [linear_to_srgb8(c) for c in override['tint']]
    if 'calibre' in override:
        entry['calibre'] = min(1.0, max(0.0, float(override['calibre'])))
    if 'hull' in override:
        if override['hull'] not in HULL_VARIANTS:
            raise GenerateError(f"override for {w['weapon_id']}: unknown hull variant {override['hull']!r}")
        entry['hull'] = override['hull']
    for key in ('bolt_e_body', 'bolt_w_orb'):
        if key in override:
            v = float(override[key])
            if not 0.0 <= v <= 2.55 or round(v * 100) < 1:  # the DLL stores x 100 and drops a row whose pin is 0
                raise GenerateError(f"override for {w['weapon_id']}: {key} {v} outside the table's range")
            entry[key] = round(v, 2)
    if 'bolt_halo' in override:
        if override['bolt_halo'] is not False:
            raise GenerateError(f"override for {w['weapon_id']}: bolt_halo pins only false")
        entry['bolt_halo'] = False
    if 'bolt_tint' in override:
        if override['bolt_tint'] != 'texture':
            raise GenerateError(f"override for {w['weapon_id']}: bolt_tint pins only 'texture'")
        entry['bolt_tint'] = 'texture'
    return entry


def generate(game_root, stock_only=False, edition='auto', overrides_path=OVERRIDES):
    game_root = Path(game_root)
    try:
        edition = engine_bodies.resolve_edition(game_root, edition)
    except engine_bodies.GenerateError as exc:
        raise GenerateError(str(exc)) from None
    if edition == 'fl' and stock_only:
        raise GenerateError('--stock-only is an AP view (STOCK_AP_CATALOGUES)')
    overrides = load_overrides(overrides_path)
    assets, skipped = engine_bodies.load_assets(game_root, stock_only, edition)
    try:
        result = census_reader.census(assets)
        missiles_text, _ = assets.get('types/TMissiles.txt')
    except FileNotFoundError as exc:
        raise GenerateError(f'types table not found in the selected layers: {exc}') from None
    table = result['tables']['TBullets']
    try:
        mounts = mount_classes(assets)
    except FileNotFoundError as exc:
        raise GenerateError(f'mount tables not found in the selected layers: {exc}') from None
    cache = {}
    entries, applied = [], 0
    for w in result['weapons']:
        body = result['bodies'].get(w['body_key'])
        colour = None
        for name in diffuse_names(body):
            try:
                colour = engine_bodies.texture_colour(assets, name, cache)
            except Exception:  # an undecodable texture leaves the row without a texture colour (the DLL: light / white)
                colour = None
            if colour is not None:
                break
        override = override_for(overrides, table['sha256'], w['weapon_id'])
        applied += bool(override)
        entries.append(row_entry(w, body, colour, override, mounts.get(w['index'])))
    missile_rows = len(census_reader.data_rows(missiles_text.decode('latin1'))[1:])
    return dict(
        schema=SCHEMA, tool=TOOL,
        generated_from=dict(root='<game-root>', view='stock' if stock_only else 'installed',
                            **({'edition': 'fl'} if edition == 'fl' else {}), layers=list(assets.layers),
                            skipped_overlay_layers=list(skipped), loose_files=len(assets.loose),
                            tbullets=table, overrides=str(Path(overrides_path).resolve().relative_to(ROOT))
                            if Path(overrides_path).resolve().is_relative_to(ROOT) else str(overrides_path)),
        rules=dict(tracer_min_elongation=TRACER_MIN_ELONGATION, slug_min_elongation=SLUG_MIN_ELONGATION,
                   texture_rgb='alpha-weighted mean of the first decodable body texture, 8-bit sRGB',
                   tint='override tint, linear -> 8-bit sRGB',
                   mount_tier='the catalogue size of the mounting ship rows (weapon_catalog.size_of, the 10 % rule): '
                              'small fighter, medium corvette, big capital; other or unmounted fighter',
                   range_m='lifetime_ms x speed / 1000 / 500 m'),
        counts=dict(bullets=len(entries), missiles=missile_rows, textures=sum(1 for e in entries if e['texture_rgb']),
                    overrides=applied, mount_fallback=sum(1 for e in entries if e['mount_fallback']),
                    **{t: sum(1 for e in entries if e['mount_tier'] == t) for t in MOUNT_TIERS}),
        tables=dict(bullets=dict(rows=len(entries), sha256=table['sha256'], entries=entries),
                    missiles=dict(rows=missile_rows)))


def mount_fallback_rows(table):
    """The bullet rows that took the fighter tier because no ship class mounts them (unknown 7)."""
    return [e for e in table['tables']['bullets']['entries'] if e.get('mount_fallback')]


def dumps(table):
    return json.dumps(table, indent=1, sort_keys=True, ensure_ascii=True) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game-root', type=Path, default=engine_bodies.bob1.DEFAULT_GAME)
    parser.add_argument('--out', type=Path, required=True, help='output JSON path (outside the game root)')
    parser.add_argument('--stock-only', action='store_true', help='stock AP catalogues only, no mod layers or loose files')
    parser.add_argument('--overrides', type=Path, default=OVERRIDES)
    engine_bodies.catalogue_mounts.add_edition_argument(parser)
    args = parser.parse_args(argv)
    root = args.game_root.resolve()
    out = args.out.resolve()
    if out.is_relative_to(root):
        parser.error('--out must be outside the game root (the game tree is read-only)')
    started = time.monotonic()
    try:
        table = generate(root, args.stock_only, args.edition, args.overrides)
    except (GenerateError, ValueError) as exc:
        print(f'weapon_effects_table: {exc}', file=sys.stderr)
        return 1
    # the rows no ship class mounts (the fighter fallback, bolt-muzzle-look.md unknown 7): one line each
    for e in mount_fallback_rows(table):
        print(f"weapon_effects_table: mount fallback (fighter): row {e['row']} {e['id']} (no mounting ship class)",
              file=sys.stderr)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(out.name + '.tmp')
    tmp.write_text(dumps(table))
    os.replace(tmp, out)
    print(json.dumps(dict(table['counts'], tbullets=table['tables']['bullets']['sha256'],
                          view=table['generated_from']['view'], seconds=round(time.monotonic() - started, 2)),
                     sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
