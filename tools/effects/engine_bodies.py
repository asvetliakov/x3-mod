#!/usr/bin/env python3
"""Generate engine_bodies.json: the per-body table of the engine jet bodies (types/Bodies SBTYPE_JET and
SBTYPE_SMALLJET) the engine-effects pass ships (docs/architecture/engine-effects-modern.md section 2,
"Tint, size class, extent"). Read-only on the game tree; derived from the census
verification/results/engine-effects-census/engine_effects_census.py.

  python3 tools/effects/engine_bodies.py --game-root <X3 dir> --out <engine_bodies.json> [--stock-only]

Views. Default: every installed catalogue (NN.cat, addon/NN.cat) and the loose files, as the engine mounts them,
minus the x3m LOD overlay slots (lod_overlay.original_assets: the table must not depend on whether our own overlay
is installed); bodies resolve by bob1.resolve_body (loose file first, then the highest mounted catalogue).
--stock-only: sector_fog_census.STOCK_AP_CATALOGUES (01..13 + addon/01..04), no loose files.

Body list. The SBTYPE_JET and SBTYPE_SMALLJET sections of the winning types/Bodies: `TYPE;count;` then `count`
tokens (an id or a name, `;`-separated, `//` comments dropped). Only the first `count` tokens are read (the
declared count; a surplus or shortfall is recorded under generated_from.lists).

Key rule (the spelling src/proxy/lens_flare_cull_core.h resolves against the engine body table): a name token as
written in types/Bodies (`effects\\engines\\fx_engine_argon_M3`, separators and case kept); a numeric token N as the
engine's default name of a fixed slot, `v\\%05d` (566 -> `v\\00566`). The consumer compares ASCII
case-insensitively with `\\` and `/` distinct (name_equal there); two tokens equal under that rule are one entry
(the first spelling is kept, both lists recorded). `id` is N for a numeric token, null for a name (its slot is
dynamic, 20000 + registration order, known only at run time).

Per loadable body (LOD 0 = bob1.lods(tree)[0]):
- value: the LOD-0 `value`; body units = normalised point x value / 65536 (the text loader's scaling).
- z_min, z_max, half_width [x, y]: bbox in body units of the points of faces whose area is >= 0.1 % of the
  largest face (drops the degenerate pin triangles some glow bodies carry); half_width = (max - min) / 2.
  z_extent: `negative` when z_max <= 5 % of the length (nozzle at the origin, plume to -z), `positive` when
  z_min >= -5 %, else `both`; `none` (and null extents) for a body without faces.
- material: the LOD-0 material with the most faces whose diffuse texture decodes (else the one with the most
  faces): effect (effect file name, null for a legacy material), kind effect|legacy, blend {src, dst, law} with
  the D3DBLEND numbers of g_SrcBlend/g_DestBlend (law `ONE/INVSRCCOLOR` etc.; `off` when g_AlphaBlendEnable is 0;
  legacy: law `legacy`), zwrite (g_ZWriteEnable, null for legacy), diffuse (the name as written).
- colour of the diffuse at level 0 (all frames of an animation row -N, averaged; peak over all frames), texels
  weighted by alpha / 255: mean_hex = the alpha-weighted mean of the 8-bit sRGB channels (the census figure,
  e.g. #5858f7); mean_linear = the alpha-weighted mean of the texels decoded by the IEC 61966-2-1 EOTF (linear
  light, 0..1); an all-transparent image is weighted uniformly. peak = the texel with nonzero alpha (any texel when
  none has) with the largest Rec. 709 luma of its 8-bit channels (first on ties), peak_hex its bytes, peak_linear
  its linear value; alpha_mean. Floats are rounded to 6 decimals, extents to 3.
- cluster: `grey` for a legacy (non-effect) material or a near-black mean (largest channel < DARK_MAX: the
  opaque standard_lighting nozzle bodies such as v\\00566's navjet); else the reference colour nearest to the
  unrounded mean (Euclidean, 8-bit sRGB; ties by label) among CLUSTERS: the eleven Mayhem 3 glow textures
  fx_engine_<label>.dds and `cyan`, the stock animated blue family (-79). No decodable diffuse: null.
- tier: the body-name suffix `_(verytiny|tiny|nor[N]|big[N]|huge[N])` (tier_rule `name`), else the value tier:
  the TIERS entry whose reference value is nearest on a log scale, i.e. the thresholds are the geometric means of
  neighbouring references (tier_rule `value`; TIER_THRESHOLDS lists them).
Bodies that do not load go to `missing` with reason not_found | refused:<code> | format:<message> | no_lod.
"""
import argparse
import hashlib
import json
import math
import os
import re
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

SCHEMA = 1
LISTS = (('jet', 'SBTYPE_JET'), ('smalljet', 'SBTYPE_SMALLJET'))
D3DBLEND = {1: 'ZERO', 2: 'ONE', 3: 'SRCCOLOR', 4: 'INVSRCCOLOR', 5: 'SRCALPHA', 6: 'INVSRCALPHA', 7: 'DESTALPHA',
            8: 'INVDESTALPHA', 9: 'DESTCOLOR', 10: 'INVDESTCOLOR', 11: 'SRCALPHASAT'}
# Alpha-weighted 8-bit sRGB means of the reference textures (measured 2026-10-01 on the X3 bottle: Mayhem 3
# addon catalogues for the eleven fx_engine_<label>.dds, stock 01..13 + addon/01..04 for the -79 animation).
CLUSTERS = {
    'darkblue': (0x58, 0x58, 0xf7), 'lightblue': (0x1b, 0x88, 0xa6), 'red': (0xd6, 0x3d, 0x3d),
    'green': (0x38, 0x86, 0x5a), 'yellow': (0x77, 0x77, 0x02), 'orange': (0xb1, 0x53, 0x26),
    'magenta': (0xa2, 0x43, 0x9f), 'purple': (0x84, 0x54, 0x9c), 'white': (0x6a, 0x6a, 0x6a),
    'peach': (0x73, 0x67, 0x61), 'lime': (0x19, 0xa3, 0x19), 'cyan': (0x4b, 0xa0, 0xbf)}
LEGACY_CLUSTER = 'grey'
DARK_MAX = 48       # a mean whose largest 8-bit channel is below this is near black: `grey` (opaque nozzle bodies)
# Size tiers of the Mayhem 3 glow family (body-name suffix -> LOD-0 value, census class_tier.csv).
TIERS = (('tiny', 504), ('nor', 1000), ('nor2', 2000), ('nor3', 4000), ('big', 6250), ('big2', 9366),
         ('big3', 18750), ('big4', 47272), ('huge', 93922), ('huge2', 141175), ('huge3', 211762))
TIER_THRESHOLDS = tuple(round(math.sqrt(a[1] * b[1]), 1) for a, b in zip(TIERS, TIERS[1:]))
TIER_NAME = re.compile(r'_(verytiny|tiny|nor\d?|big\d?|huge\d?)$', re.I)
VISIBLE_SHARE = 0.001
ORIGIN_SHARE = 0.05


class GenerateError(Exception):
    pass


# --------------------------------------------------------------------------- body list

def body_key(token):
    """(key, id) of a types/Bodies token: `v\\%05d` and N for a number, the token as written and None for a name."""
    if re.fullmatch(r'\d+', token):
        n = int(token)
        return 'v\\%05d' % n, n
    return token, None


def body_lists(text):
    """{list label: dict(declared, listed, tokens=[first `declared` tokens])} of SBTYPE_JET / SBTYPE_SMALLJET."""
    text = re.sub(r'//[^\n]*', '', text)
    out = {}
    for label, section in LISTS:
        m = re.search(r'(?<![A-Z0-9_])%s\s*;\s*(\d+)\s*;(.*?)(?=SBTYPE_|\Z)' % section, text, re.S)
        if m is None:
            raise GenerateError(f'types/Bodies has no {section} section')
        toks = [x.strip() for x in m.group(2).split(';') if x.strip()]
        declared = int(m.group(1))
        out[label] = dict(declared=declared, listed=len(toks), tokens=toks[:declared])
    return out


def jet_bodies(lists):
    """{key: dict(id, lists)} in first-seen order; keys equal under ASCII case folding merge into the first."""
    bodies, folded = {}, {}
    for label, _ in LISTS:
        for tok in lists[label]['tokens']:
            key, ident = body_key(tok)
            first = folded.setdefault(key.lower(), key)
            entry = bodies.setdefault(first, dict(id=ident, lists=[]))
            if label not in entry['lists']:
                entry['lists'].append(label)
    return bodies


# --------------------------------------------------------------------------- geometry

def visible_bbox(record):
    """(lo, hi) in body units of the points of faces with area >= VISIBLE_SHARE of the largest; (None, None)
    without faces."""
    p = np.array([q[1:4] for q in record['points']], np.float64) * (record['value'] / 65536.0)
    faces = np.array([f[:3] for part in record['parts'] for g in part['groups'] for f in g['faces']], np.int64)
    if not len(faces) or not len(p):
        return None, None
    a, b, c = p[faces[:, 0]], p[faces[:, 1]], p[faces[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    keep = faces[area >= VISIBLE_SHARE * area.max()]
    q = p[np.unique(keep)]
    return q.min(0), q.max(0)


def z_extent(z_min, z_max):
    length = z_max - z_min
    if length <= 0:
        return 'none'
    if z_max <= ORIGIN_SHARE * length:
        return 'negative'
    if z_min >= -ORIGIN_SHARE * length:
        return 'positive'
    return 'both'


# --------------------------------------------------------------------------- colour

def srgb_to_linear(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


LUMA = np.array([0.2126, 0.7152, 0.0722])


def texel_stats(rgba):
    """Colour statistics of one RGBA uint8 image (see the module doc)."""
    px = rgba.reshape(-1, 4).astype(np.float64)
    rgb, a = px[:, :3], px[:, 3] / 255.0
    w = a if a.sum() > 0 else np.ones_like(a)
    lin = srgb_to_linear(rgb / 255.0)
    luma = (rgb * LUMA).sum(1)
    peak = int(np.argmax(np.where(a > 0, luma, -1.0) if a.sum() > 0 else luma))
    return dict(mean=(rgb * w[:, None]).sum(0) / w.sum(), mean_linear=(lin * w[:, None]).sum(0) / w.sum(),
                peak=rgb[peak], peak_luma=float(luma[peak]), alpha_mean=float(a.mean()),
                size=f'{rgba.shape[1]}x{rgba.shape[0]}')


def texture_frames(assets, name):
    """Texture names the engine draws for a diffuse name: all frames of an animation row -N, else the name."""
    s = lod_atlas.strip_texture_name(name)
    if s is None:
        return []
    m = re.match(r'-(\d+)', s)
    if m:
        rows = lod_atlas.animation_rows(assets)
        if rows is None or int(m.group(1)) >= len(rows):
            raise lod_atlas.AtlasError(f'animation row {m.group(1)} unavailable')
        row = rows[int(m.group(1))]
        return [f[1] for f in row['frames']] or [row['first']]
    return [name]


def texture_colour(assets, name, cache):
    """dict(mean, mean_linear, peak, peak_linear, alpha_mean, frames, members) or None (no texture)."""
    if name in cache:
        return cache[name]
    stats, members = [], []
    for fname in texture_frames(assets, name):
        src = lod_atlas.texture_source(assets, fname.encode('latin1'))
        if src is None:
            continue
        data, kind, info = src
        img = lod_atlas.decode_dds(data, 0) if kind == 'dds' else lod_atlas.decode_image(data, fname)
        stats.append(texel_stats(img))
        members.append(dict(source=info['source'], member=info['member'], placeholder=info['placeholder'],
                            sha256=info['decoded_sha256'][:16], size=stats[-1]['size']))
    out = None
    if stats:
        top = max(range(len(stats)), key=lambda i: stats[i]['peak_luma'])     # first frame on ties
        peak = stats[top]['peak']
        out = dict(mean=np.mean([s['mean'] for s in stats], 0),
                   mean_linear=np.mean([s['mean_linear'] for s in stats], 0),
                   peak=peak, peak_linear=srgb_to_linear(peak / 255.0),
                   alpha_mean=float(np.mean([s['alpha_mean'] for s in stats])), frames=len(stats), members=members)
    cache[name] = out
    return out


def hexrgb(rgb):
    return '#%02x%02x%02x' % tuple(int(round(min(255.0, max(0.0, float(x))))) for x in rgb)


def cluster(mean, legacy):
    if legacy:
        return LEGACY_CLUSTER
    if mean is None:
        return None
    m = np.asarray(mean, np.float64)
    if float(m.max()) < DARK_MAX:
        return LEGACY_CLUSTER
    return min(CLUSTERS, key=lambda k: (float(((m - np.array(CLUSTERS[k])) ** 2).sum()), k))


def tier(name, value):
    m = TIER_NAME.search(name)
    if m:
        return m.group(1).lower(), 'name'
    if value is None or value <= 0:
        return None, None
    for (label, _), limit in zip(TIERS, TIER_THRESHOLDS):
        if value < limit:
            return label, 'value'
    return TIERS[-1][0], 'value'


# --------------------------------------------------------------------------- materials

def param(m, name):
    for n, typ, val in m.get('params', []):
        if n.decode('latin1').lower() == name.lower():
            return val.decode('latin1') if typ == 8 else val[0]
    return None


def material_info(m):
    if 'params' in m:
        on = param(m, 'g_AlphaBlendEnable')
        src, dst = param(m, 'g_SrcBlend'), param(m, 'g_DestBlend')
        law = (f'{D3DBLEND.get(src, src)}/{D3DBLEND.get(dst, dst)}' if on else 'off')
        zw = param(m, 'g_ZWriteEnable')
        return dict(kind='effect', effect=m['effect'].decode('latin1'), diffuse=param(m, 't_DiffuseTexture') or '',
                    blend=dict(src=src if on else None, dst=dst if on else None, law=law),
                    zwrite=None if zw is None else int(zw))
    tex = m.get('texture', b'')
    diffuse = tex.decode('latin1') if isinstance(tex, bytes) else str(tex)
    return dict(kind='legacy', effect=None, diffuse=diffuse, blend=dict(src=None, dst=None, law='legacy'),
                zwrite=None, legacy_flags=m.get('flags'))


def group_material(mats, index):
    """The material a face group draws with; a negative index is an animation group (the material whose diffuse
    names that -N, else material 0; texture-lookup.md section 10)."""
    if index < 0:
        return next((x for x in mats if 'params' in x and re.match(r'%d\b' % index, param(x, 't_DiffuseTexture') or '')),
                    mats[0] if mats else {})
    return mats[index] if index < len(mats) else {}


# --------------------------------------------------------------------------- per body

def r6(v):
    return [round(float(x), 6) for x in v]


def r3(x):
    return round(float(x), 3)


def describe(assets, key, info, cache):
    """(entry, None) for a loadable body or (None, reason)."""
    try:
        e = bob1.resolve_body(assets, key)
    except FileNotFoundError:
        return None, 'not_found'
    except bob1.BodyRefused as exc:
        return None, f'refused:{exc.reason}'
    data = assets.read_entry(e)
    try:
        tree = bob1.parse(data, None)
        record = bob1.lods(tree)[0] if bob1.lods(tree) else None
    except bob1.FormatError as exc:
        return None, f'format:{str(exc)[:160]}'
    if record is None:
        return None, 'no_lod'
    mats = bob1.materials(tree)
    faces = {}
    for part in record['parts']:
        for g in part['groups']:
            faces[g['material']] = faces.get(g['material'], 0) + len(g['faces'])
    order = sorted(faces, key=lambda k: (-faces[k], k))
    chosen, colour, errors = None, None, []
    for gi in order:
        mi = material_info(group_material(mats, gi))
        try:
            c = texture_colour(assets, mi['diffuse'], cache) if mi['diffuse'] else None
        except (lod_atlas.AtlasError, bob1.FormatError, OSError, ValueError, KeyError) as exc:
            c = None
            errors.append(f'{mi["diffuse"]}: {str(exc)[:120]}')
        if chosen is None:
            chosen = mi
        if c is not None:
            chosen, colour = mi, c
            break
    if chosen is None:
        chosen = dict(kind=None, effect=None, diffuse=None, blend=None, zwrite=None)
    lo, hi = visible_bbox(record)
    value = int(record['value'])
    t, rule = tier(key, value)
    out = dict(id=info['id'], lists=list(info['lists']), source=e['source'], member=e['path'],
               format='BOB1' if data.startswith(b'BOB1') else 'text', sha256=hashlib.sha256(data).hexdigest()[:16],
               value=value, lods=len(bob1.lods(tree)), materials=len(mats),
               z_min=r3(lo[2]) if lo is not None else None, z_max=r3(hi[2]) if hi is not None else None,
               z_extent=z_extent(float(lo[2]), float(hi[2])) if lo is not None else 'none',
               half_width=[r3((hi[0] - lo[0]) / 2), r3((hi[1] - lo[1]) / 2)] if lo is not None else None,
               material_kind=chosen['kind'], effect=chosen['effect'], blend=chosen['blend'], zwrite=chosen['zwrite'],
               diffuse=chosen['diffuse'], tier=t, tier_rule=rule)
    if chosen.get('legacy_flags') is not None:
        out['legacy_flags'] = chosen['legacy_flags']
    if colour is not None:
        out.update(mean_hex=hexrgb(colour['mean']), mean_linear=r6(colour['mean_linear']),
                   peak_hex=hexrgb(colour['peak']), peak_linear=r6(colour['peak_linear']),
                   alpha_mean=round(colour['alpha_mean'], 6), frames=colour['frames'],
                   texture=colour['members'][0])
    else:
        out.update(mean_hex=None, mean_linear=None, peak_hex=None, peak_linear=None, alpha_mean=None, frames=0,
                   texture=None)
    out['cluster'] = cluster(colour['mean'] if colour else None, chosen['kind'] == 'legacy')
    if errors and colour is None:
        out['colour_error'] = errors[0]
    return out, None


# --------------------------------------------------------------------------- driver

def load_assets(game_root, stock_only):
    if stock_only:
        return Assets(game_root, catalogues=STOCK_AP_CATALOGUES), []
    return lod_overlay.original_assets(game_root)


def generate(game_root, stock_only=False):
    game_root = Path(game_root)
    assets, skipped = load_assets(game_root, stock_only)
    try:
        data, prov = assets.get('types/Bodies.txt')
    except FileNotFoundError:
        raise GenerateError('types/Bodies not found in the selected layers') from None
    lists = body_lists(data.decode('latin1'))
    bodies, missing, cache = {}, [], {}
    for key, info in jet_bodies(lists).items():
        entry, reason = describe(assets, key, info, cache)
        if entry is None:
            missing.append(dict(name=key, id=info['id'], lists=list(info['lists']), reason=reason))
        else:
            bodies[key] = entry
    missing.sort(key=lambda m: (m['name'].lower(), m['name']))
    counts = dict(bodies=len(bodies), missing=len(missing))
    for k in ('cluster', 'effect', 'z_extent', 'tier_rule'):
        tally = {}
        for b in bodies.values():
            tally[str(b[k])] = tally.get(str(b[k]), 0) + 1
        counts[k] = tally
    return dict(
        schema=SCHEMA, tool='tools/effects/engine_bodies.py',
        generated_from=dict(
            root='<game-root>', view='stock' if stock_only else 'installed',
            catalogues=len([s for s in assets.layers if s not in skipped]), layers=[s for s in assets.layers if s not in skipped],
            skipped_overlay_layers=list(skipped), loose_files=len(assets.loose),
            bodies_txt=dict(source=prov['source'], member=prov['member'], sha256=prov['decoded_sha256']),
            lists={label: dict(declared=v['declared'], listed=v['listed']) for label, v in lists.items()}),
        rules=dict(clusters={k: hexrgb(v) for k, v in CLUSTERS.items()}, legacy_cluster=LEGACY_CLUSTER, dark_max=DARK_MAX,
                   tiers=dict(TIERS), tier_thresholds=list(TIER_THRESHOLDS), visible_face_share=VISIBLE_SHARE,
                   origin_share=ORIGIN_SHARE, key='types/Bodies token as written; number N -> v\\%05d; '
                                                  'ASCII case-insensitive, \\ and / distinct'),
        counts=counts, bodies=bodies, missing=missing)


def dumps(table):
    return json.dumps(table, indent=1, sort_keys=True, ensure_ascii=True) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game-root', type=Path, default=bob1.DEFAULT_GAME)
    parser.add_argument('--out', type=Path, required=True, help='output JSON path (outside the game root)')
    parser.add_argument('--stock-only', action='store_true', help='stock AP catalogues only, no mod layers or loose files')
    args = parser.parse_args(argv)
    root = args.game_root.resolve()
    out = args.out.resolve()
    if out.is_relative_to(root):
        parser.error('--out must be outside the game root (the game tree is read-only)')
    started = time.monotonic()
    try:
        table = generate(root, args.stock_only)
    except (GenerateError, ValueError) as exc:
        print(f'engine_bodies: {exc}', file=sys.stderr)
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(out.name + '.tmp')
    tmp.write_text(dumps(table))
    os.replace(tmp, out)
    print(json.dumps(dict(view=table['generated_from']['view'], bodies=table['counts']['bodies'],
                          missing=table['counts']['missing'], effect=table['counts']['effect'],
                          cluster=table['counts']['cluster'], seconds=round(time.monotonic() - started, 2)),
                     sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
