#!/usr/bin/env python3
"""Read-only census for a fleet-wide merged-LOD atlas overlay batch (lod_overlay.py --collapse atlas).

Walks every winning binary body (.pbb and unpacked .bob members; mods ship .bob) and, unless
--binary-only, every winning text body (.pbd/.bod, compiled by bob1.parse_text) of the
installed catalogues, with every catalogue carrying a valid x3m-lod marker skipped
(lod_overlay.original_assets; an orphaned marker's catalogue is a mod's and is read), and prints
one row per body: ladder, record 0 faces/points/groups, the coarsest record's groups, effect
files, material table kind, second UV set, alpha materials, the reasons `--collapse atlas`
(source record 0, compact placement) would refuse it, the atlas size the texel rule picks at
--screen-width (default 1920, the reference for every total; 1280 and 2560 are extra columns),
draws saved against record 0 and against the coarsest record, and the estimated overlay cost.
Nothing is baked, built or written into the game directory: the atlas checks run
lod_atlas.collapse (effect classes, occlusion check, layout, UV rewrite and group split; no
baking) on a copy of the material table. With include_text (the batch), winning text bodies
(.pbd/.bod without a binary twin) are compiled by bob1.parse_text and censused like binary ones
(column text; the compile follows the engine's text loader 0x00483f20; a text scene is skipped
like CUT1, a MATERIAL3 text body is mat3, any other body outside the grammar or whose compile does
not re-parse equal is text_parse_error); a stem with both a binary
and a text member is ambiguous_body_ext (bob1.resolve_body). Bodies with up to
lod_overlay.MAX_TRAILING stray bytes after /BOB parse with a warning column (trailing); more
is trailing_bytes. Each row carries inputs_sha256 (the decoded body plus every texture the
tiles read) for lod_overlay.py --batch --sync.

Switch-size rule (parameters --ship-min/--ship-factor/--station-t/--t-cap):
  ships:    T_pad = min(200, max(80, 2.5 * T_1))   (T_1 = record 1 threshold; 80 for a single-LOD body)
  stations: T_pad = min(200, 150)                  (objects/stations and objects/others)
  other top directories (effects, environments, cockpits, ...) are excluded unless --include-other
  (then the station rule). compact placement's T_pad >= T_1 guard is waived for source record 0
  (lod_overlay.py, "Batch mode"), so T_pad below T_1 is a column, not a refusal.

Costs: atlas bytes = DDS bytes of the slots the tool writes with --atlas-specular (assumed;
bump only when the dominant material has a bump slot) with a full mip chain at the chosen side.
A lower bound: diffuse is counted DXT1 and light/bump/specular DXT5 as on the pilot, but
lod_atlas.encode picks DXT5 for any slot whose level-0 alpha is not all 255, which is not known
without baking. Body member bytes ~ record 0 bytes x 1.2. Before any check, the body must resolve
through bob1.resolve_body as lod_overlay.plan_body does (both .pbb and .pbd -> ambiguous_body_ext).
Atlas member names use lod_overlay.qualified_stem (stem + path hash), so stems never collide.
Sizes are tried from --atlas-size up to --atlas-max-size (default 4096, above the tool's 2048
default, so the need is visible); the summary also gives the 2048-capped totals.

  python3 tools/analysis/lod_batch_census.py [--game DIR] [--out DIR] [--jobs N] [--limit N] [--screen-width 1920]
      [--binary-only]
"""
import argparse
import hashlib
import math
import multiprocessing
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1            # noqa: E402
import lod_atlas       # noqa: E402
import lod_overlay     # noqa: E402

RULE = dict(ship_min=80.0, ship_factor=2.5, station_t=150.0, t_cap=200.0)
SCREEN_WIDTH = 1920       # the user's display; the texel rule's reference width
EXTRA_WIDTHS = (1280, 2560)
SLOT_FORMAT = {'diffuse': 'DXT1', 'light': 'DXT5', 'bump': 'DXT5', 'specular': 'DXT5'}
MEMBER_FACTOR = 1.2
STATION_DIRS = ('stations', 'others')
SECTORS = (('run255_burst2', 'verification/results/run255-census/node_census_out.txt', '14286'),
           ('run257_burst1', 'verification/results/run257-pilot/census_run257_out.txt', '3615'),
           ('run260', 'verification/results/lod-overlay-batch/census_run260_out.txt', '9868'))
ATLAS_REASONS = (('occlusion textures', 'occlusion_mismatch'), ('outside the material table', 'material_outside_table'),
                 ('not an effect material', 'non_effect_material'), ('no diffuse', 'no_diffuse'),
                 ('no opaque faces', 'no_opaque'), ('without UV', 'no_uv'), ('do not fit', 'atlas_fit'),
                 ('does not resolve', 'texture_unresolved'), ('not a DDS', 'texture_not_dds'),
                 ('Pillow', 'pil_missing'), ('cannot decode image', 'texture_decode'),
                 ('Ambiguous', 'texture_ambiguous'), ('parameter', 'dominant_slot_missing'))
TEXT_EXTENSIONS = ('.pbd', '.bod')
BINARY_EXTENSIONS = ('.pbb', '.bob')


def dds_bytes(n, fmt):
    """DDS file bytes of an n x n texture with a full mip chain to 1x1 (128-byte header)."""
    block = 8 if fmt == 'DXT1' else 16
    total, side = 0, n
    while True:
        total += max(1, math.ceil(side / 4)) ** 2 * block
        if side == 1:
            break
        side //= 2
    return 128 + total


def atlas_bytes(n, slots):
    return sum(dds_bytes(n, SLOT_FORMAT[s]) for s in slots) if n else 0


def category(path):
    top = path.replace('\\', '/').lower().split('/')
    top = top[1] if top[0] == 'objects' and len(top) > 1 else top[0]
    return 'ship' if top == 'ships' else 'station' if top in STATION_DIRS else 'other'


def t_pad(cat, thresholds, rule=RULE):
    if cat == 'ship':
        t1 = thresholds[0] if thresholds else 0
        t = max(rule['ship_min'], rule['ship_factor'] * t1)
    else:
        t = rule['station_t']
    return int(min(rule['t_cap'], t))


def body_key(name):
    return bob1.body_stem(name).lower()


SizedTextures = lod_atlas.Textures      # sizes and sources are cached in lod_atlas.Textures now


def atlas_reason(exc):
    text = str(exc)
    return next((code for needle, code in ATLAS_REASONS if needle in text), 'atlas_other')


def drawn_groups(lod):
    return sum(len(p['groups']) for p in lod['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART)


def census_body(assets, textures, entry, opts):
    """One row dict for a winning .pbb/.bob entry; 'skip' set for CUT1 scenes (not bodies)."""
    path = entry['path']
    name = bob1.body_stem(path)[len('objects/'):]
    row = dict(name=name, member=f'{entry["source"]}:{path}', source=entry['source'], path=path,
               cat=category(path), refuse=[], filter=[], trailing=0)
    data = assets.read_entry(entry)
    k = bob1.kind(data)
    if path.lower().endswith(TEXT_EXTENSIONS):
        row['text'] = True
        if not k and bob1.text_kind(data) == 'scene':
            return dict(row, skip='scene')          # the text twin of a CUT1 scene: not a body
        try:
            if k or bytes(data[:3]) == b'BOB':
                raise bob1.FormatError(f'text member holding binary data (magic {bytes(data[:4])!r})')
            tree = bob1.parse_text(data)
        except bob1.FormatError as exc:
            row['refuse'].append('mat3' if 'MATERIAL3' in str(exc) else 'text_parse_error')
            row['atlas_error'] = str(exc)[:160]
            return row
        if not lod_overlay.text_compiles(tree):
            row['refuse'].append('text_parse_error')
            row['atlas_error'] = 'the compiled text body does not serialise and parse back'
            return row
    elif k == 'CUT1':
        return dict(row, skip='CUT1')
    elif k != 'BOB1':
        row['refuse'].append('not_bob1')
        return row
    else:
        try:
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING)
        except bob1.FormatError as exc:
            row['refuse'].append('trailing_bytes' if 'trailing bytes' in str(exc) else 'parse_error')
            row['atlas_error'] = str(exc)[:160]
            return row
    row['trailing'] = tree.get('trailing_bytes', 0)
    body_data = data[:len(data) - row['trailing']] if row['trailing'] else data
    row['source_decoded_sha256'] = hashlib.sha256(data).hexdigest()
    try:                                   # the resolver lod_overlay.plan_body uses first
        won = bob1.resolve_body(assets, name)
        if (won['source'], won['path']) != (entry['source'], entry['path']):
            row['refuse'].append('resolve_mismatch')
    except bob1.FormatError:               # both .pbb and .pbd exist: engine order unverified
        row['refuse'].append('ambiguous_body_ext')
    if not row.get('text') and bob1.serialise(tree) != body_data:
        row['refuse'].append('writer_mismatch')
    if 'loose' in entry:
        row['refuse'].append('loose_winner')
    mat_tag = next((t for t, _ in tree['sections'] if t in bob1.MATVER), '-')
    ladder, mats = bob1.lods(tree), bob1.materials(tree)
    r0, last = ladder[0], ladder[-1]
    th = [l['value'] for l in ladder[1:]]
    tp = t_pad(row['cat'], th, opts['rule'])
    s0 = bob1.lod_summary(r0)
    alpha = lod_overlay.alpha_materials(mats)
    used = {g['material'] for p in r0['parts'] for g in p['groups']}
    opaque = {g['material'] for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART
              for g in p['groups'] if g['material'] not in alpha}
    effects = {mats[m].get('effect', b'').decode('latin1').lower() for m in opaque
               if 0 <= m < len(mats) and 'params' in mats[m]}
    uv2 = sum(1 for p in r0['points'] if p[0] & 4)
    row.update(lods=len(ladder), thresholds=th, t_pad=tp, mat=mat_tag,
               r0_faces=s0['faces'], r0_points=s0['points'], r0_groups=s0['draws'], r0_drawn=drawn_groups(r0),
               coarse_groups=drawn_groups(last), effects=len(effects), uv2=uv2,
               alpha=len(used & alpha), r0_bytes=lod_overlay.record_bytes(r0))
    row['member_bytes'] = int(row['r0_bytes'] * MEMBER_FACTOR)
    if row['cat'] == 'other' and not opts['include_other']:
        row['filter'].append('category_other')
    if mat_tag not in ('MAT5', 'MAT6'):
        row['refuse'].append('mat3')
    row['t_pad_below_t1'] = bool(th and th[0] > tp)          # guard waived for source record 0 (a column only)
    if tp < 2:
        row['refuse'].append('t_pad_below_2')
    if any(not 0 <= g['material'] < len(mats) for p in r0['parts'] for g in p['groups']):
        row['refuse'].append('material_outside_table')      # negative indices on the ad signs
    if {'mat3', 'material_outside_table'} & set(row['refuse']):
        return row
    stem = lod_overlay.qualified_stem(path)
    row['atlas_stem'] = stem
    try:
        res = lod_atlas.collapse(assets, stem, list(mats), r0, alpha, tp * opts['widths'][0] / 1280, opts['sizes'],
                                 specular=True, synth=True, textures=textures, bump=True)
    except lod_atlas.AtlasError as exc:
        row['refuse'].append(atlas_reason(exc))
        row['atlas_error'] = str(exc)[:160]
        return row
    lay = res['layout']
    row.update(slots=list(res['slots']), tiles=len(lay['tiles']), dup=res['info']['duplicated'],
               c_drawn=drawn_groups(res['record']), c_groups=sum(len(p['groups']) for p in res['record']['parts']),
               atlas_materials=len(res['atlas_indices']), occlusion=res['occlusion'])
    tex_shas = sorted({(textures.source(v) or {}).get('decoded_sha256') or 'unresolved:' + v.decode('latin1').lower()
                       for t in lay['tiles'] for v in t['names'].values() if v is not None})
    row['inputs_sha256'] = hashlib.sha256('\n'.join([row['source_decoded_sha256']] + tex_shas).encode()).hexdigest()
    row['texture_sources'] = sorted({v.split(':', 1)[-1] for t in lay['tiles'] for v in t['sources'].values() if v})
    sizes = {opts['widths'][0]: (lay['size'], lay['min_ratio'])}
    for w in opts['widths'][1:]:
        l2 = lod_atlas.plan_layout(r0, mats, alpha, textures, tp * w / 1280, opts['sizes'],
                                   lod_atlas.GUTTER, res['slots'])
        sizes[w] = (l2['size'], l2['min_ratio'])
    row['atlas'] = {w: dict(size=n, ratio=r, bytes=atlas_bytes(n, res['slots']),
                            bytes_cap2048=atlas_bytes(min(n, 2048), res['slots']))
                    for w, (n, r) in sizes.items()}
    names = lod_atlas.texture_names(stem, res['slots'])[1]
    taken = sorted({e['source'] for m in names.values() for ext in lod_atlas.DDS_LOOKUP[1]
                    for e in assets.candidates(m.rsplit('.', 1)[0] + ext)})
    if taken:
        row['refuse'].append('atlas_name_taken')
        row['taken'] = taken
    row['saved_r0'] = row['r0_drawn'] - row['c_drawn']
    row['saved_coarse'] = row['coarse_groups'] - row['c_drawn']
    if row['saved_r0'] <= 0:
        row['filter'].append('no_draw_gain')
    return row


_WORKER = {}


def _init(game, opts):
    assets, skipped = lod_overlay.original_assets(Path(game))
    _WORKER.update(assets=assets, textures=SizedTextures(assets), opts=opts, skipped=skipped)


def _work(key):
    w = _WORKER
    entry = w['assets'].entries[key][-1]
    try:
        row = census_body(w['assets'], w['textures'], entry, w['opts'])
    except Exception as exc:                        # one bad body must not end the census
        row = dict(name=key, member=entry['path'], cat=category(entry['path']), refuse=['exception'],
                   filter=[], atlas_error=f'{type(exc).__name__}: {exc}'[:160])
    finally:
        w['assets'].cache.clear()                   # body bytes; texture sizes stay in SizedTextures
    return row


def body_keys(assets):
    """Winning binary bodies (.pbb and unpacked .bob members share one canonical key)."""
    return sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith(BINARY_EXTENSIONS))


def text_body_keys(assets):
    """Winning text bodies (.pbd/.bod) whose stem has no binary member anywhere (those are the
    ambiguous_body_ext rows of the binary key); cut scenes (objects/cut) are left out."""
    binary = {k[:-4] for k in body_keys(assets)}
    return sorted(k for k, v in assets.entries.items()
                  if v[-1]['path'].lower().endswith(TEXT_EXTENSIONS) and k[:-4] not in binary
                  and not k.startswith(('objects/cut/', 'addon/objects/cut/')))


def run(game, opts, jobs=1, limit=None, only=None, include_text=False):
    """(rows, skipped marker sources) over every winning binary body (and, with include_text, the
    winning text bodies, compiled by bob1.parse_text); `only` restricts to a set of body keys
    (body_key(name))."""
    assets, skipped = lod_overlay.original_assets(Path(game))
    keys = body_keys(assets) + (text_body_keys(assets) if include_text else [])
    if only is not None:
        keys = [k for k in keys if body_key(k) in only]
    keys = keys[:limit]
    if jobs <= 1:
        _WORKER.update(assets=assets, textures=SizedTextures(assets), opts=opts, skipped=skipped)
        rows = [_work(k) for k in keys]
    else:
        with multiprocessing.get_context('spawn').Pool(jobs, _init, (str(game), opts)) as pool:
            rows = pool.map(_work, keys, chunksize=4)
    rows = [r for r in rows if 'skip' not in r]
    for r in rows:
        r['eligible'] = not r['refuse'] and not r['filter']
    return rows, skipped


# --- output ---------------------------------------------------------------------------------

def mb(n):
    return f'{n / 1e6:.2f}'


def format_row(r):
    base = f'{r["name"]} cat={r["cat"]}'
    if 'lods' not in r:
        return f'{base} refuse={",".join(r["refuse"])} {r.get("atlas_error", "")}'.rstrip()
    th = ','.join(str(t) for t in r['thresholds']) or '-'
    s = (f'{base} lods={r["lods"]} thr={th} T_pad={r["t_pad"]}{"<T_1" if r.get("t_pad_below_t1") else ""} mat={r["mat"]}'
         f' r0_faces={r["r0_faces"]} r0_points={r["r0_points"]} r0_groups={r["r0_groups"]}'
         f' r0_drawn={r["r0_drawn"]} coarse_drawn={r["coarse_groups"]} effects={r["effects"]} uv2={r["uv2"]}'
         f' alpha_mats={r["alpha"]}')
    if 'atlas' in r:
        a = r['atlas']
        s += (f' C_drawn={r["c_drawn"]} saved_vs_r0={r["saved_r0"]} saved_vs_coarse={r["saved_coarse"]} tiles={r["tiles"]} slots={len(r["slots"])}'
              + ''.join(f' atlas@{w}={a[w]["size"]}(ratio {a[w]["ratio"]:.2f}, {mb(a[w]["bytes"])} MB)'
                        for w in a)
              + f' member~{mb(r["member_bytes"])} MB')
    if r.get('trailing'):
        s += f' trailing={r["trailing"]}'
    if r.get('text'):
        s += ' text'
    if r.get('atlas_materials', 1) > 1:
        s += f' atlas_materials={r["atlas_materials"]}'
    s += f' refuse={",".join(r["refuse"]) or "-"} filter={",".join(r["filter"]) or "-"}'
    s += ' ELIGIBLE' if r['eligible'] else ''
    if r.get('taken'):
        s += f' taken={",".join(r["taken"])}'
    if r.get('atlas_error'):
        s += f' error="{r["atlas_error"]}"'
    return s


def parse_census(path, frame):
    """{body key: draws} of one frame section of a node census (run255 node rows or run257 'B' rows)."""
    out, inside, have_b = {}, False, False
    rows = []
    for line in Path(path).read_text().splitlines():
        if line.startswith('=='):
            inside = line.startswith(f'== frame {frame} ')
            continue
        if not inside:
            continue
        tok = line.split()
        if line.startswith('B ') and len(tok) >= 6:
            have_b = True
            rows.append(('B', tok[-1], int(tok[1])))
        elif line.startswith('N ') and len(tok) >= 5:
            rows.append(('N', tok[-1], int(tok[3])))
        elif tok and re.fullmatch(r'[0-9a-f]{8}', tok[0]) and len(tok) >= 5 and tok[2].isdigit():
            rows.append(('N', tok[-1], int(tok[2])))
    for kind, body, draws in rows:
        if body == '-' or (have_b and kind != 'B'):
            continue
        k = body_key(body)
        out[k] = out.get(k, 0) + draws
    return out


def sector_report(label, census, by_key, widths):
    lines = [f'== {label}: {len(census)} bodies drawn']
    tot = {w: [0, 0] for w in widths}
    n_el = 0
    for k, draws in sorted(census.items(), key=lambda x: -x[1]):
        r = by_key.get(k)
        if r is None:
            lines.append(f'  {draws:4d} draws {k} (no binary body in the catalogues)')
            continue
        if r['eligible']:
            n_el += 1
            for w in widths:
                tot[w][0] += r['atlas'][w]['bytes']
                tot[w][1] += r['atlas'][w]['bytes_cap2048']
            sz = ' '.join(f'@{w}={r["atlas"][w]["size"]}/{mb(r["atlas"][w]["bytes"])}MB' for w in widths)
            lines.append(f'  {draws:4d} draws {r["name"]} T_pad={r["t_pad"]} r0_drawn={r["r0_drawn"]}'
                         f' C_drawn={r["c_drawn"]} {sz}')
        else:
            lines.append(f'  {draws:4d} draws {r["name"]} not eligible: {",".join(r["refuse"] + r["filter"])}')
    lines.append(f'  eligible {n_el}; resident atlas estimate (one set per distinct body, added to the'
                 ' source textures): ' + '; '.join(
                     f'{w} wide {mb(tot[w][0])} MB (cap 2048: {mb(tot[w][1])} MB)' for w in widths))
    return lines, n_el, tot


def summary(rows, skipped, opts, sectors):
    widths = opts['widths']
    el = [r for r in rows if r['eligible']]
    n_text = sum(1 for r in rows if r.get('text'))
    lines = [f'bodies {len(rows)} (winning .pbb/.bob BOB1/non-CUT1 resources'
             f'{f" + {n_text} text bodies (.pbd/.bod, scenes skipped)" if n_text else ""};'
             f' overlay sources skipped: {skipped or "none"})',
             f'rule: ships T_pad = min({opts["rule"]["t_cap"]:g}, max({opts["rule"]["ship_min"]:g},'
             f' {opts["rule"]["ship_factor"]:g} x T_1)); stations/others {opts["rule"]["station_t"]:g}'
             f' (cap {opts["rule"]["t_cap"]:g}); other top dirs {"station rule" if opts["include_other"] else "excluded"};'
             f' source record 0, compact; sizes {list(opts["sizes"])}; texel rule at {widths[0]} wide'
             f' (reference; extra columns {list(widths[1:])})',
             f'eligible {len(el)} (no refusal, draws saved >= 1): '
             + ', '.join(f'{c} {sum(1 for r in el if r["cat"] == c)}' for c in ('ship', 'station', 'other'))]
    for w in widths:
        lines.append(f'  eligible totals at {w} wide: atlases {mb(sum(r["atlas"][w]["bytes"] for r in el))} MB'
                     f' (cap 2048: {mb(sum(r["atlas"][w]["bytes_cap2048"] for r in el))} MB),'
                     f' sizes {dict(sorted(_count(r["atlas"][w]["size"] for r in el).items()))},'
                     f' ratio < 2 {sum(1 for r in el if r["atlas"][w]["ratio"] < 2)}'
                     f' (at cap 2048: {sum(1 for r in el if min(r["atlas"][w]["size"], 2048) < r["atlas"][w]["size"] or r["atlas"][w]["ratio"] < 2)})')
    lines.append(f'  eligible body members ~{mb(sum(r["member_bytes"] for r in el))} MB'
                 f' (record 0 x {MEMBER_FACTOR}); draws saved per instance below T_pad, summed over bodies:'
                 f' {sum(r["saved_r0"] for r in el)} against record 0,'
                 f' {sum(r["saved_coarse"] for r in el)} against the coarsest record'
                 f' ({sum(1 for r in el if r["saved_coarse"] <= 0)} eligible bodies save none against it)')
    lines.append('  atlas bytes are a lower bound: diffuse is counted DXT1 (lod_atlas.encode picks DXT5 when the'
                 ' level-0 alpha is not all 255), light/bump/specular DXT5; the slot set assumes --atlas-specular')
    lines.append('refusals (a body counts once per reason; first reason in brackets):')
    reasons = _count(x for r in rows for x in r['refuse'])
    first = _count(r['refuse'][0] for r in rows if r['refuse'])
    for k, v in sorted(reasons.items(), key=lambda x: -x[1]):
        lines.append(f'  {k}: {v} [{first.get(k, 0)}]')
    lines.append('filters on otherwise accepted bodies: ' + ', '.join(
        f'{k} {v}' for k, v in sorted(_count(x for r in rows if not r['refuse'] for x in r['filter']).items())))
    lines.append('refusals among bodies with r0_drawn >= 10: ' + ', '.join(
        f'{k} {v}' for k, v in sorted(_count(x for r in rows if r.get('r0_drawn', 0) >= 10
                                              for x in r['refuse']).items(), key=lambda x: -x[1])))
    lines.append('top 40 by record 0 drawn groups:')
    top = sorted((r for r in rows if 'r0_drawn' in r), key=lambda r: (-r['r0_drawn'], r['name']))[:40]
    for r in top:
        a = r.get('atlas')
        sz = ' '.join(f'@{w}={a[w]["size"]}' for w in widths) if a else ''
        lines.append(f'  {r["r0_drawn"]:3d} {r["name"]} ({r["cat"]}, thr {r["thresholds"]}, T_pad {r["t_pad"]})'
                     f' C_drawn={r.get("c_drawn", "-")} coarse_drawn={r["coarse_groups"]} {sz}'
                     f' {"ELIGIBLE" if r["eligible"] else "refuse=" + ",".join(r["refuse"] + r["filter"])}')
    lines.append('flown sectors:')
    for label, (n_el, tot) in sectors.items():
        lines.append(f'  {label}: eligible {n_el}; ' + '; '.join(
            f'{w} wide {mb(tot[w][0])} MB (cap 2048 {mb(tot[w][1])} MB)' for w in widths))
    return lines


def _count(it):
    out = {}
    for x in it:
        out[x] = out.get(x, 0) + 1
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', default=str(bob1.DEFAULT_GAME))
    ap.add_argument('--out', help='directory for census.txt, summary.txt, sectors.txt, eligible_bodies.txt')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument('--limit', type=int)
    ap.add_argument('--screen-width', type=int, default=SCREEN_WIDTH,
                    help='reference width for the texel rule and the totals (lod_overlay.py --screen-width)')
    ap.add_argument('--atlas-size', type=int, default=1024)
    ap.add_argument('--atlas-max-size', type=int, default=4096)
    ap.add_argument('--ship-min', type=float, default=RULE['ship_min'])
    ap.add_argument('--ship-factor', type=float, default=RULE['ship_factor'])
    ap.add_argument('--station-t', type=float, default=RULE['station_t'])
    ap.add_argument('--t-cap', type=float, default=RULE['t_cap'])
    ap.add_argument('--include-other', action='store_true', help='apply the station rule to other top directories')
    ap.add_argument('--binary-only', action='store_true',
                    help='leave the winning text bodies (.pbd/.bod) out (the census before bob1.parse_text)')
    ap.add_argument('--sector', action='append', metavar='LABEL=FILE:FRAME',
                    help='flown body set from a node census (default: run255 burst 2, run257 burst 1, run260)')
    a = ap.parse_args(argv)
    sizes, n = [], a.atlas_size
    while n <= a.atlas_max_size:
        sizes.append(n); n *= 2
    widths = (a.screen_width,) + tuple(w for w in EXTRA_WIDTHS if w != a.screen_width)
    opts = dict(sizes=tuple(sizes), include_other=a.include_other, widths=widths,
                rule=dict(ship_min=a.ship_min, ship_factor=a.ship_factor, station_t=a.station_t, t_cap=a.t_cap))
    t0 = time.time()
    rows, skipped = run(a.game, opts, a.jobs, a.limit, include_text=not a.binary_only)
    root = Path(__file__).resolve().parents[2]
    specs = SECTORS if not a.sector else [
        (s.split('=', 1)[0],) + tuple(s.split('=', 1)[1].rsplit(':', 1)) for s in a.sector]
    by_key = {body_key(r['name']): r for r in rows}
    sector_lines, sectors = [], {}
    for label, path, frame in specs:
        p = Path(path) if Path(path).is_absolute() else root / path
        if not p.exists():
            sector_lines.append(f'== {label}: {path} missing')
            continue
        lines, n_el, tot = sector_report(f'{label} ({path} frame {frame})', parse_census(p, frame), by_key, widths)
        sector_lines += lines
        sectors[label] = (n_el, tot)
    census = [format_row(r) for r in sorted(rows, key=lambda r: r['name'].lower())]
    summ = summary(rows, skipped, opts, sectors) + [f'elapsed {time.time() - t0:.0f} s, jobs {a.jobs}']
    eligible = [f'{r["name"]}={r["t_pad"]}@0' for r in sorted(rows, key=lambda r: r['name'].lower()) if r['eligible']]
    if a.out:
        out = Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'census.txt').write_text('\n'.join(census) + '\n')
        (out / 'summary.txt').write_text('\n'.join(summ) + '\n')
        (out / 'sectors.txt').write_text('\n'.join(sector_lines) + '\n')
        (out / 'eligible_bodies.txt').write_text('\n'.join(eligible) + '\n')
    else:
        print('\n'.join(census + [''] + summ + [''] + sector_lines))
    return rows


if __name__ == '__main__':
    main()
