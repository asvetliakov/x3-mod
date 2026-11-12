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
The bullet re-shade's per-texture luminance normaliser (docs/reverse-engineering/fl/bullet-draw-batch.md section 6):
per row the name of the texture its colour came from (texture), and tables.bullet_textures, one entry per distinct
texture frame those rows draw: the member, the byte count and FNV-1a 64 of the bytes the engine hands
D3DXCreateTextureFromFileInMemoryEx (the decoded member: head64 over the first 4096 bytes, fnv1a64 over all of them;
the DLL maps the created texture by them), the level-0 texel luminance's peak and 99th percentile (Rec. 709 weights on
the stored values / 255, every texel, as the re-shade program weighs the sample) and the rows that draw it. An older
DLL skips both keys (schema 2 unchanged).
The bullet re-shade's geometric size (docs/architecture/projectile-look.md "Geometric size (2026-11-10)"):
tables.bullet_bodies, one entry per distinct body the rows draw (keyed by the rows' body string; the DLL maps an engine
body id to it through the live TBullets row): the UV layout of every triangle of every LOD (uv_layout): the head group
(the triangle cluster whose UV box is roughly square and whose every triangle maps at least half of it:
the quads carrying the round glow sprite) as its box centre and half-side r, the fin
group (the cluster elongated along u) as the mean v of its axis vertices (radius about the z axis under a tenth of the
group's largest), of its rim vertices (over nine tenths) and its u range; null for a group not found or whose footprint
another triangle of the body samples (the shader would shrink that triangle too). An older DLL skips the key (the
schema stays 2: the DLL reads only 1 and 2, and an unknown table is skipped).
Schema 3 (2026-11-12, docs/architecture/weapon-effects.md 5.1.1 "Ship profiles"): tables.ship_profiles, one entry per
TShips row of the edition (AP addon/types/TShips, FL addon2/types/TShips) whose scene loads, giving the hull's lateral
profile along z for the shield bubble's lathe: tships_index (0-based data row), scene (column 16 as written), class
(column 52 as written), units 'scene', axis 'z', z0 and z1 (the hull's z extent after the thin-part and thin-end rules;
trim_lo and trim_hi how much z the ends lost, parts_excluded the thin parts dropped, smoothed whether the half-widths
were eased (--raw-profiles: not); additive keys an older reader skips), slices (32 x [cx, cy, hx, hy]: slice i
spans z0 + i (z1 - z0) / 32, the lateral box centre and half-widths of its points; an empty slice interpolates its
neighbours), radius (the largest point distance from the scene origin) and points (the count used); floats to 3
decimals. Geometry: LOD 0 of every non-effect part body the scene places (first key position and rotation, F parents
composed), each body's points x its LOD-0 value / 65536 (engine_bodies.py's body units), faces under 0.1 % of the
body's largest dropped. One scene unit is one engine (world) unit: a scene part's key position, and the runtime's part
half-extent ((ext x value + 0x8000) >> 16) x axis scale / 0x10000 at unit node scale (the removed runtime walk's
hull_scale16); 500 per metre (weapon_catalog.UNITS_PER_M). Rows without a profile are listed under
generated_from.ship_profiles_skipped (index, scene, class, reason); generated_from.ship_profiles records the TShips
source, counts and part tallies. Rules and skip reasons: tools/effects/ship_profiles.py.
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
import ship_profiles  # noqa: E402
import weapon_effects_census as census_reader  # noqa: E402
import weapon_catalog  # noqa: E402

SCHEMA = 3  # 2: mount_tier and range_m per bullet row (step 4); 3: tables.ship_profiles (2026-11-12)
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


# The bullet re-shade's texture keys (src/proxy/bullet_reshade_core.h texture_key): FNV-1a 64 of the first HEAD_BYTES
# bytes and of all of them. The DLL hashes a created texture's bytes only when their count is one of the table's.
HEAD_BYTES = 4096
LUMA = (0.2126, 0.7152, 0.0722)


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def luminance_stats(rgba):
    """(peak, p99) of the texel luminance of an RGBA uint8 array: Rec. 709 weights on the stored values / 255, every
    texel (the re-shade program's dot(t.rgb, w) on the raw sample, sRGB decoding off)."""
    import numpy as np
    lum = (rgba[..., :3].reshape(-1, 3).astype(np.float64) / 255.0) @ np.array(LUMA)
    return round(float(lum.max()), 4), round(float(np.percentile(lum, 99)), 4)


# The geometric size's footprints (src/proxy/bullet_reshade_core.h head_footprint, fin_low, fin_high: the program's
# literals): the head's square of half-side 1.02 r around its centre; the fin band d = (v - axis) / (rim - axis) in
# [-0.25, 1.02] at any u.
HEAD_FOOTPRINT, FIN_LOW, FIN_HIGH = 1.02, -0.25, 1.02
UV_TOLERANCE = 1e-4      # two triangles' UV boxes closer than this are one group
HEAD_ASPECT = (0.67, 1.5)  # the head group's box width over height: roughly square
FIN_ASPECT = 2.0           # the fin group's box is at least this much wider in u than tall in v


def body_triangles(tree):
    """Every triangle of every LOD of a parsed body as three (x, y, z, u, v): positions in body units (point / 65536 x
    LOD value / 65536), u, v in texture units (the 16.16 words / 65536); a face's first three indices."""
    import bob1
    out = []
    for lod in bob1.lods(tree):
        scale = lod['value'] / 65536.0 / 65536.0
        pts = lod['points']
        for part in lod['parts']:
            for g in part['groups']:
                for face in g['faces']:
                    tri = []
                    for i in face[:3]:
                        p = pts[i]
                        f = p[0]
                        xyz = p[1:4] if f & 1 else (0, 0, 0)
                        uv = p[4:6] if f & 1 and f & 2 else (p[1:3] if f & 2 else (0, 0))
                        tri.append((xyz[0] * scale, xyz[1] * scale, xyz[2] * scale, uv[0] / 65536.0, uv[1] / 65536.0))
                    out.append(tri)
    return out


def _uv_box(tris):
    us = [p[3] for t in tris for p in t]
    vs = [p[4] for t in tris for p in t]
    return min(us), max(us), min(vs), max(vs)


def uv_layout(tris):
    """The geometric size's record of one body (the DLL's tables.bullet_bodies entry without its name):
    dict(head=dict(u, v, r) | None, fin=dict(axis_v, rim_v, u0, u1) | None, groups, head_reason, fin_reason)."""
    n = len(tris)
    boxes = [_uv_box([t]) for t in tris]
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(n):
        a = boxes[i]
        for j in range(i + 1, n):
            b = boxes[j]
            if a[0] <= b[1] + UV_TOLERANCE and b[0] <= a[1] + UV_TOLERANCE and a[2] <= b[3] + UV_TOLERANCE and \
                    b[2] <= a[3] + UV_TOLERANCE:
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[rj] = ri
    clusters = {}
    for i in range(n):
        clusters.setdefault(find(i), []).append(i)
    groups = []
    for members in clusters.values():
        u0, u1, v0, v1 = _uv_box([tris[i] for i in members])
        w, h = u1 - u0, v1 - v0
        groups.append(dict(members=set(members), box=(u0, u1, v0, v1), aspect=w / h if h > 0 else float('inf')))
    out = dict(head=None, fin=None, groups=len(groups), head_reason='none_square', fin_reason='none_elongated')

    def others_hit(group, inside):
        return any(inside(boxes[i]) for i in range(n) if i not in group['members'])
    def sprite(g):  # every triangle maps at least half the box both ways: quads carrying the glow sprite, not a mesh
        u0, u1, v0, v1 = g['box']
        return all(boxes[i][1] - boxes[i][0] >= 0.5 * (u1 - u0) and boxes[i][3] - boxes[i][2] >= 0.5 * (v1 - v0)
                   for i in g['members'])
    heads = [g for g in groups if len(g['members']) >= 2 and HEAD_ASPECT[0] <= g['aspect'] <= HEAD_ASPECT[1] and
             sprite(g)]
    if heads:
        g = max(heads, key=lambda g: (g['box'][1] - g['box'][0]) * (g['box'][3] - g['box'][2]))
        u0, u1, v0, v1 = g['box']
        cu, cv, r = (u0 + u1) / 2, (v0 + v1) / 2, max(u1 - u0, v1 - v0) / 2
        half = HEAD_FOOTPRINT * r
        if others_hit(g, lambda b: b[0] <= cu + half and b[1] >= cu - half and b[2] <= cv + half and b[3] >= cv - half):
            out['head_reason'] = 'footprint_shared'
        else:
            out['head'] = dict(u=round(cu, 5), v=round(cv, 5), r=round(r, 5))
            out['head_reason'] = 'ok'
    fins = [g for g in groups if g['aspect'] >= FIN_ASPECT and g['box'][3] > g['box'][2]]
    if fins:
        g = max(fins, key=lambda g: len(g['members']))
        verts = [p for i in g['members'] for p in tris[i]]
        radius = [(p[0] * p[0] + p[1] * p[1]) ** 0.5 for p in verts]
        rmax = max(radius)
        axis = [p[4] for p, r in zip(verts, radius) if r <= 0.1 * rmax]
        rim = [p[4] for p, r in zip(verts, radius) if r >= 0.9 * rmax]
        if rmax <= 0 or not axis or not rim:
            out['fin_reason'] = 'no_axis_or_rim'
        else:
            axis_v, rim_v = sum(axis) / len(axis), sum(rim) / len(rim)
            span = rim_v - axis_v
            d = [(p[4] - axis_v) / span for p in verts] if abs(span) > 1e-6 else []
            if not d or min(d) < FIN_LOW or max(d) > FIN_HIGH:
                out['fin_reason'] = 'not_axis_to_rim'
            else:
                lo, hi = sorted((axis_v + FIN_LOW * span, axis_v + FIN_HIGH * span))
                if others_hit(g, lambda b: b[2] <= hi and b[3] >= lo):
                    out['fin_reason'] = 'footprint_shared'
                else:
                    out['fin'] = dict(axis_v=round(axis_v, 5), rim_v=round(rim_v, 5), u0=round(g['box'][0], 5),
                                      u1=round(g['box'][1], 5))
                    out['fin_reason'] = 'ok'
    return out


def bullet_body_layouts(assets, keys, bodies):
    """tables.bullet_bodies: per body key a row draws (bodies: the census's summaries, their 'name'), its uv_layout;
    an unreadable body gets both groups null (reason 'unreadable')."""
    import bob1
    read = census_reader.assets_reader(assets)
    out = []
    for key in sorted(keys):
        name = (bodies.get(key) or {}).get('name')
        entry = dict(body=key, head=None, fin=None)
        try:
            data, _ = read(name)
            try:
                tree = bob1.parse(data)
            except bob1.FormatError:
                tree = bob1.parse(census_reader.MATERIAL3.sub(b'', data))
            layout = uv_layout(body_triangles(tree))
            entry.update(head=layout['head'], fin=layout['fin'], groups=layout['groups'],
                         head_reason=layout['head_reason'], fin_reason=layout['fin_reason'])
        except Exception as exc:  # an unreadable body: no geometric size for it (the DLL keeps the accepted look)
            entry.update(head_reason='unreadable', fin_reason='unreadable', error=type(exc).__name__)
        out.append(entry)
    return out


def bullet_texture_frames(assets, name, cache):
    """The texture frames a diffuse name draws (engine_bodies.texture_frames: an animation row's frames, else the name),
    each as dict(name, member, bytes, head64, fnv1a64, peak, p99); [] when none decodes."""
    if name in cache:
        return cache[name]
    out = []
    for fname in engine_bodies.texture_frames(assets, name):
        src = engine_bodies.lod_atlas.texture_source(assets, fname.encode('latin1'))
        if src is None:
            continue
        data, kind, info = src
        img = engine_bodies.lod_atlas.decode_dds(data, 0) if kind == 'dds' else \
            engine_bodies.lod_atlas.decode_image(data, fname)
        peak, p99 = luminance_stats(img)
        out.append(dict(name=fname, member=info['member'], bytes=len(data), head64=f'{fnv1a64(data[:HEAD_BYTES]):016x}',
                        fnv1a64=f'{fnv1a64(data):016x}', peak=peak, p99=p99))
    cache[name] = out
    return out


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


def row_entry(w, body, colour, override, mounts=None, texture=None):
    ext = (body or {}).get('extents') or {}
    e = ext.get('z_over_xy')
    half = ext.get('half_extents')
    entry = dict(row=w['index'], id=w['weapon_id'], body=w['body_key'], texture=texture,
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


def generate(game_root, stock_only=False, edition='auto', overrides_path=OVERRIDES, smooth_profiles=True):
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
    cache, frames_cache = {}, {}
    entries, applied, textures = [], 0, {}
    for w in result['weapons']:
        body = result['bodies'].get(w['body_key'])
        colour, texture = None, None
        for name in diffuse_names(body):
            try:
                colour = engine_bodies.texture_colour(assets, name, cache)
            except Exception:  # an undecodable texture leaves the row without a texture colour (the DLL: light / white)
                colour = None
            if colour is not None:
                texture = name
                break
        if texture is not None:
            try:
                frames = bullet_texture_frames(assets, texture, frames_cache)
            except Exception:  # undecodable: the DLL keeps the IRE normaliser for it
                frames = []
            for f in frames:
                rows = textures.setdefault(f['fnv1a64'], dict(f, rows=[]))['rows']
                if w['index'] not in rows:
                    rows.append(w['index'])
        override = override_for(overrides, table['sha256'], w['weapon_id'])
        applied += bool(override)
        entries.append(row_entry(w, body, colour, override, mounts.get(w['index']), texture))
    missile_rows = len(census_reader.data_rows(missiles_text.decode('latin1'))[1:])
    layouts = bullet_body_layouts(assets, {e['body'] for e in entries if e['body']}, result['bodies'])
    try:
        profiles, profiles_skipped, profiles_from = ship_profiles.ship_profiles(assets, smooth_profiles)
    except FileNotFoundError as exc:
        raise GenerateError(f'types/TShips not found in the selected layers: {exc}') from None
    return dict(
        schema=SCHEMA, tool=TOOL,
        generated_from=dict(root='<game-root>', view='stock' if stock_only else 'installed',
                            **({'edition': 'fl'} if edition == 'fl' else {}), layers=list(assets.layers),
                            skipped_overlay_layers=list(skipped), loose_files=len(assets.loose),
                            tbullets=table, overrides=str(Path(overrides_path).resolve().relative_to(ROOT))
                            if Path(overrides_path).resolve().is_relative_to(ROOT) else str(overrides_path),
                            ship_profiles=profiles_from, ship_profiles_skipped=profiles_skipped),
        rules=dict(tracer_min_elongation=TRACER_MIN_ELONGATION, slug_min_elongation=SLUG_MIN_ELONGATION,
                   texture_rgb='alpha-weighted mean of the first decodable body texture, 8-bit sRGB',
                   tint='override tint, linear -> 8-bit sRGB',
                   mount_tier='the catalogue size of the mounting ship rows (weapon_catalog.size_of, the 10 % rule): '
                              'small fighter, medium corvette, big capital; other or unmounted fighter',
                   range_m='lifetime_ms x speed / 1000 / 500 m',
                   bullet_textures='per texture frame a row draws: bytes, FNV-1a 64 of the first 4096 and of all bytes '
                                   'the engine decodes, level-0 luminance peak and p99 (Rec. 709 on value / 255)',
                   bullet_bodies='per body a row draws, all LODs: head = the square UV cluster (box centre, half-side '
                                 'r), fin = the u-elongated cluster (mean v of the axis and rim vertices, u range); '
                                 'null when absent or its footprint is shared with another triangle',
                   ship_profiles='per TShips row: LOD 0 of every non-effect part body at its first scene key, points x '
                                 'value / 65536 (scene units = engine units), faces under 0.1 % of the largest '
                                 'dropped; parts narrower than 6 % of the hull dropped (parts_excluded), thin '
                                 'ends under 3 % of the widest slice area trimmed (trim_lo/trim_hi); 32 z slices '
                                 '[cx, cy, hx, hy], empty slices interpolated, half-widths eased over +-2 '
                                 'slices unless smoothed is false'),
        counts=dict(bullets=len(entries), missiles=missile_rows, textures=sum(1 for e in entries if e['texture_rgb']),
                    bullet_textures=len(textures), bullet_bodies=len(layouts),
                    bullet_body_heads=sum(1 for b in layouts if b['head']),
                    bullet_body_fins=sum(1 for b in layouts if b['fin']),
                    overrides=applied, ship_profiles=len(profiles), ship_profiles_skipped=len(profiles_skipped),
                    mount_fallback=sum(1 for e in entries if e['mount_fallback']),
                    **{t: sum(1 for e in entries if e['mount_tier'] == t) for t in MOUNT_TIERS}),
        tables=dict(bullets=dict(rows=len(entries), sha256=table['sha256'], entries=entries),
                    missiles=dict(rows=missile_rows),
                    bullet_textures=sorted(textures.values(), key=lambda t: (t['name'].lower(), t['fnv1a64'])),
                    bullet_bodies=layouts, ship_profiles=profiles))


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
    parser.add_argument('--raw-profiles', action='store_true',
                        help='ship profiles without the slice half-width easing (ship_profiles.smooth_widths)')
    engine_bodies.catalogue_mounts.add_edition_argument(parser)
    args = parser.parse_args(argv)
    root = args.game_root.resolve()
    out = args.out.resolve()
    if out.is_relative_to(root):
        parser.error('--out must be outside the game root (the game tree is read-only)')
    started = time.monotonic()
    try:
        table = generate(root, args.stock_only, args.edition, args.overrides, not args.raw_profiles)
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
