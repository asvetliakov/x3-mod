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

SCHEMA = 1
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


class GenerateError(Exception):
    pass


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
    return {k: v for k, v in out.items() if k in ('style', 'tint', 'calibre', 'hull')}


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


def row_entry(w, body, colour, override):
    ext = (body or {}).get('extents') or {}
    e = ext.get('z_over_xy')
    half = ext.get('half_extents')
    entry = dict(row=w['index'], id=w['weapon_id'], body=w['body_key'],
                 z_over_xy=e, half_extent=round(max(half[0], half[1]), 4) if half else None,
                 texture_rgb=[int(round(min(255.0, max(0.0, float(c))))) for c in colour['mean']] if colour else None,
                 shape=shape_from_elongation(e), style=None, tint=None, calibre=None)
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
        entries.append(row_entry(w, body, colour, override))
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
                   tint='override tint, linear -> 8-bit sRGB'),
        counts=dict(bullets=len(entries), missiles=missile_rows, textures=sum(1 for e in entries if e['texture_rgb']),
                    overrides=applied),
        tables=dict(bullets=dict(rows=len(entries), sha256=table['sha256'], entries=entries),
                    missiles=dict(rows=missile_rows)))


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
