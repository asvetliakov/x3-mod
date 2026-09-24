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

Switch-size rule (parameters --ship-min/--ship-factor/--station-t/--t-cap, --aspect-cap, --no-aspect):
  ships:    T_class = min(200, max(80, 2.5 * T_1))   (T_1 = record 1 threshold; 80 for a single-LOD body)
  stations: T_class = min(200, 150)                  (objects/stations and objects/others)
  aspect:   T_pad = round(T_class * clamp(k, 1, K_max)), K_max 1.5 ships / 2.0 stations and others;
            k = (r_box / r_eq) / sqrt(3) from the half-extents e = (max - min) / 2 per axis of record 0's
            position-carrying points (flag 1): r_box = |e|, r_eq = (ex * ey * ez)^(1/3), so a cube is 1.
            The engine compares s = r*640/D with the bounding-sphere radius r, which overstates a flat
            body's visible size, so without it the merged record appears too far out. A zero extent
            (a flat or linear body) takes r_eq over the nonzero extents (aspect_note); none: k = 1.
            --no-aspect keeps T_pad = T_class. T_pad drives the pad record, the T_1 guard, the atlas
            size and the texel ratio.
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

Switch distance: the engine's radius (model node +0xa0, `radius=` of the cull census) is in world units
and not derivable from the body (records are normalised to a largest coordinate of 65536), so it comes
from flight censuses (--radius-log; default RADIUS_SOURCES when present): lines carrying `r=N`/`radius=N`
and `body=NAME` (run272 burst_draws_out.txt, raw cull_census rows), the largest per body. D = r*640/T in
world units; km at ~505 units per metre (UNITS_PER_M, an inference of
docs/reverse-engineering/sector-collide.md); rows without a flown radius print '-'. r_body (layout
radius, normalised body units) is printed too.

Texel rule columns: per width the minimum tile ratio, the area-weighted
ratio and the starved share of lod_atlas.texel_floor at lod_overlay's defaults (--min-texels 0.5,
--texel-floor-share 0.10); each width's atlas entry keeps the per-tile rows (lod_atlas.tile_rows)
so the batch applies its own options. The census does not refuse texel_floor; the batch does.

Texel fallback (opts['texel'] fallback W; the CLI and lod_overlay.py --texel-fallback default 1.0, a run
without opts['texel'] and W = 0 disable it):
a body whose layout at T_pad would be refused texel_floor at the reference width gets a lower switch
size instead, so its merged record appears farther out. Texels per pixel scale as 1/T, so
T_fb = round(T * weighted / max(W, F)) (F = --min-texels; W at the defaults), the layout is rebuilt
at T_fb (the atlas size may change with it) and the step repeats, at most FALLBACK_STEPS layouts,
until weighted >= W and the body passes the floor. T_fb must stay >= max(T_1, T_pad / 4, 2) (the
ladder's record 1, a relative floor so a body is not pushed out to a few pixels, the engine's s = 1
minimum); a step below that, or no pass within the steps, leaves the refusal (row texel_fallback
accepted False, guard T_1 / relative / min_2: the binding floor).
An accepted fallback rebuilds the census columns at T_fb (row t_pad = T_fb, threshold_aspect keeps the
rule's T_pad) and records T_pad -> T_fb, weighted and starved share before/after, sizes and steps.

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

RULE = dict(ship_min=80.0, ship_factor=2.5, station_t=150.0, t_cap=200.0, aspect=True, aspect_ship=1.5,
            aspect_station=2.0)
UNITS_PER_M = 505.0       # world units per metre: inferred (docs/reverse-engineering/sector-collide.md)
FALLBACK_STEPS = 3        # texel fallback: at most this many layout rebuilds
FALLBACK_RELATIVE = 0.25  # texel fallback: T_fb >= T_pad * this (the relative guard)
RADIUS_SOURCES = ('verification/results/run272-batch-busy/burst_draws_out.txt',)
RADIUS_RE = re.compile(r'\b(?:r|radius)=(\d+)')
BODY_RE = re.compile(r'\bbody=(\S+)')
SCREEN_WIDTH = 1920       # the user's display; the texel rule's reference width
EXTRA_WIDTHS = (1280, 2560)
SLOT_FORMAT = {'diffuse': 'DXT1', 'light': 'DXT5', 'bump': 'DXT5', 'specular': 'DXT5'}
MEMBER_FACTOR = 1.2
STATION_DIRS = ('stations', 'others')
SECTORS = (('run255_burst2', 'verification/results/run255-census/node_census_out.txt', '14286'),
           ('run257_burst1', 'verification/results/run257-pilot/census_run257_out.txt', '3615'),
           ('run260', 'verification/results/lod-overlay-batch/census_run260_out.txt', '9868'))
# dominant_slot_missing ('parameter') is unreachable since 2026-09-24: plan_layout refuses a material without
# t_DiffuseTexture first (no_diffuse) and lod_atlas.required_slots asks for t_LightMapTexture only when a material of
# the class declares it, which class_dominant then picks; the needle stays as a guard of atlas_material's check.
ATLAS_REASONS = (('texture animation', 'texture_animation_unsupported'), ('excluded effect', 'excluded_effect'), ('occlusion textures', 'occlusion_mismatch'), ('outside the material table', 'material_outside_table'),
                 ('not an effect material', 'non_effect_material'), ('no diffuse', 'no_diffuse'),
                 ('no opaque faces', 'no_opaque'), ('without UV', 'no_uv'), ('do not fit', 'atlas_fit'),
                 ('does not resolve', 'texture_unresolved'), ('generated surface', 'texture_generated'), ('not a DDS', 'texture_not_dds'),
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


def aspect_k(points):
    """(k, note) of a record: k = (r_box / r_eq) / sqrt(3) over the half-extents of its position-carrying
    points (flag 1); a zero extent takes r_eq over the nonzero ones (note), none at all gives k = 1."""
    lo, hi = [math.inf] * 3, [-math.inf] * 3
    for p in points:
        if p[0] & 1:
            for k in range(3):
                v = p[k + 1]
                if v < lo[k]:
                    lo[k] = v
                if v > hi[k]:
                    hi[k] = v
    if lo[0] == math.inf:
        return 1.0, 'no positions'
    e = [(h - l) / 2 for l, h in zip(lo, hi)]
    nz = [x for x in e if x > 0]
    if not nz:
        return 1.0, 'zero extent on every axis'
    r_eq = math.prod(nz) ** (1 / len(nz))
    note = '' if len(nz) == 3 else f'{3 - len(nz)} zero extent(s): r_eq over the {len(nz)} nonzero'
    return math.sqrt(sum(x * x for x in e)) / r_eq / math.sqrt(3), note


def t_aspect(cat, t_class, k, rule=RULE):
    """T_pad = round(T_class * clamp(k, 1, K_max)) (K_max by category), T_class with the rule off."""
    if not rule.get('aspect', True):
        return t_class
    cap = rule.get('aspect_ship', 1.5) if cat == 'ship' else rule.get('aspect_station', 2.0)
    return int(round(t_class * min(max(k, 1.0), cap)))


def world_radii(paths):
    """{body key: largest world radius} from flight census text (r=N / radius=N with body=NAME)."""
    out = {}
    for path in paths:
        with open(path, errors='replace') as f:
            for line in f:
                b = BODY_RE.search(line)
                if not b:
                    continue
                radii = [int(x) for x in RADIUS_RE.findall(line)]
                if radii:
                    k = body_key(b.group(1))
                    out[k] = max(out.get(k, 0), max(radii))
    return out


def default_radius_logs():
    root = Path(__file__).resolve().parents[2]
    return [root / p for p in RADIUS_SOURCES if (root / p).exists()]


def switch_km(radius, t):
    return radius * 640 / t / UNITS_PER_M / 1000 if radius and t else None


def attach_world(rows, radii):
    """radius_world, switch_km (at T_pad) and switch_km_class (at T_class) for rows with a flown radius."""
    for r in rows:
        w = radii.get(body_key(r['name']))
        if w and 't_pad' in r:
            r.update(radius_world=w, switch_km=switch_km(w, r['t_pad']), switch_km_class=switch_km(w, r['t_class']))
            fb = r.get('texel_fallback')
            if fb:
                fb.update(km_before=switch_km(w, fb['t_pad']), km_after=switch_km(w, fb.get('t_fb')))


def texel_opts(opts):
    """(min_texels, floor_share, fallback W) of a census run: opts['texel'], else lod_overlay's floor defaults and
    no fallback (callers without the option keep the pre-fallback census)."""
    t = opts.get('texel') or {}
    return (t.get('min_texels', lod_overlay.MIN_TEXELS), t.get('floor_share', lod_overlay.TEXEL_FLOOR_SHARE),
            t.get('fallback', 0.0))


def texel_fallback(texel_at, t_pad, t1, x0, min_texels, floor_share, w_target, steps=FALLBACK_STEPS):
    """Lower switch size for a body refused texel_floor at t_pad (x0 = its lod_atlas.texel_floor result).
    texel_at(T) -> (texel_floor result, atlas size) of the layout rebuilt at T. Each step takes
    T' = min(T - 1, round(T * weighted / max(W, min_texels))); T' below max(T_1, T_pad * FALLBACK_RELATIVE, 2)
    stops with the binding floor as guard ('T_1', 'relative' or 'min_2'); a layout with weighted >= W that passes the floor is accepted. Returns the
    record dict (accepted, t_pad, t_fb, t1, W, weighted/starved/size before and after, steps, guard)."""
    target = max(w_target, min_texels)
    floors = sorted([(t1 or 0, 'T_1'), (t_pad * FALLBACK_RELATIVE, 'relative'), (2, 'min_2')],
                    key=lambda f: -f[0])
    floor_t, floor_name = floors[0]
    out = dict(accepted=False, W=w_target, t_pad=t_pad, t1=t1, t_fb=None, guard=None,
               weighted_before=x0['weighted_texels_per_px'], starved_before=x0['starved_share'], steps=[])
    t, w = t_pad, x0['weighted_texels_per_px'] or 0.0
    for _ in range(steps):
        nt = min(t - 1, int(round(t * w / target)))
        if nt < floor_t:
            out.update(guard=floor_name, guard_t=nt, guard_floor=floor_t)
            break
        x, size = texel_at(nt)
        w = x['weighted_texels_per_px'] or 0.0
        out['steps'].append(dict(t=nt, weighted=x['weighted_texels_per_px'], starved=x['starved_share'], size=size))
        t = nt
        if w >= w_target and not x['refuse']:
            out.update(accepted=True, t_fb=nt, weighted_after=x['weighted_texels_per_px'],
                       starved_after=x['starved_share'], size_after=size)
            break
    return out


def fallback_text(fb):
    """One-line description of a texel_fallback record."""
    f = lambda x: '-' if x is None else f'{x:.3f}'
    steps = ','.join(f'{s["t"]}:{f(s["weighted"])}' for s in fb['steps']) or '-'
    km = (f' D {fb["km_before"]:.2f}->{fb["km_after"]:.2f} km' if fb.get('km_after') else ' D - km')
    if fb['accepted']:
        return (f'T {fb["t_pad"]}->{fb["t_fb"]} weighted {f(fb["weighted_before"])}->{f(fb["weighted_after"])}'
                f' starved {100 * fb["starved_before"]:.1f}%->{100 * fb["starved_after"]:.1f}%'
                f' size {fb.get("size_before")}->{fb["size_after"]}{km} steps {steps}')
    return (f'refused T {fb["t_pad"]} weighted {f(fb["weighted_before"])} starved {100 * fb["starved_before"]:.1f}%'
            + (f' guard {fb["guard"]} (T_fb {fb["guard_t"]} < {fb["guard_floor"]:g})' if fb.get('guard')
               else f' W {fb["W"]:g} not reached in {len(fb["steps"])} steps'
                    f' (last T {fb["steps"][-1]["t"]} weighted {f(fb["steps"][-1]["weighted"])}; steps {steps})'))


def bleed_text(r):
    """' light_bleed=<tiles over the limit> counted=<remedied> ignored=<under the share> kept=<kept
    materials>' of a baked batch row (lod_overlay.bleed_fields; a row without the share split counts every
    tile); '' for a census-only row (the census does not bake, so it cannot run the check)."""
    b = r.get('baked') or {}
    if 'light_bleed' not in b:
        return ''
    ign = len(b.get('light_bleed_ignored') or [])
    return (f' light_bleed={b["light_bleed"]} counted={b.get("light_bleed_counted", b["light_bleed"] - ign)}'
            f' ignored={ign} kept={len(b["kept_light_bleed"])}')


def aspect_text(r):
    d = '-' if r.get('switch_km') is None else f'{r["switch_km_class"]:.2f}->{r["switch_km"]:.2f}'
    fb = r.get('texel_fallback') or {}
    t = f'{fb["t_pad"]}->{r["t_pad"]}(texel_fallback)' if fb.get('accepted') else r['t_pad']
    return (f'k={r["aspect_k"]:.2f} T_class={r["t_class"]} T={t} r_body={r.get("radius_body", 0):.0f}'
            f' r_world={r.get("radius_world", "-")} D={d} km'
            + (f' ({r["aspect_note"]})' if r.get('aspect_note') else ''))


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
    anim_error = None
    if mat_tag in ('MAT5', 'MAT6'):
        try:                                    # face groups -N: texture animations (lod_atlas.animated_record)
            r0, anim = lod_atlas.animated_record(mats, r0, assets)
            if anim['groups']:
                row['animation'] = anim
        except lod_atlas.AtlasError as exc:
            anim_error = exc
    th = [l['value'] for l in ladder[1:]]
    tc = t_pad(row['cat'], th, opts['rule'])
    k, note = aspect_k(r0['points'])
    tp = t_aspect(row['cat'], tc, k, opts['rule'])
    s0 = bob1.lod_summary(r0)
    alpha = lod_overlay.alpha_materials(mats)
    used = {g['material'] for p in r0['parts'] for g in p['groups']}
    opaque = {g['material'] for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART
              for g in p['groups'] if g['material'] not in alpha}
    effects = {mats[m].get('effect', b'').decode('latin1').lower() for m in opaque
               if 0 <= m < len(mats) and 'params' in mats[m]}
    uv2 = sum(1 for p in r0['points'] if p[0] & 4)
    row.update(lods=len(ladder), thresholds=th, t_pad=tp, t_class=tc, aspect_k=round(k, 4), threshold_aspect=tp,
               mat=mat_tag,
               r0_faces=s0['faces'], r0_points=s0['points'], r0_groups=s0['draws'], r0_drawn=drawn_groups(r0),
               coarse_groups=drawn_groups(last), effects=len(effects), uv2=uv2,
               alpha=len(used & alpha), r0_bytes=lod_overlay.record_bytes(r0))
    row['member_bytes'] = int(row['r0_bytes'] * MEMBER_FACTOR)
    if note:
        row['aspect_note'] = note
    if row['cat'] == 'other' and not opts['include_other']:
        row['filter'].append('category_other')
    if mat_tag not in ('MAT5', 'MAT6'):
        row['refuse'].append('mat3')
    row['t_pad_below_t1'] = bool(th and th[0] > tp)          # guard waived for source record 0 (a column only)
    if tp < 2:
        row['refuse'].append('t_pad_below_2')
    if anim_error:
        row['refuse'].append(atlas_reason(anim_error))
        row['atlas_error'] = str(anim_error)[:160]
    if any(g['material'] >= len(mats) or (g['material'] < 0 and not anim_error and not p['flags'] & lod_atlas.HIDDEN_PART)
           for p in r0['parts'] for g in p['groups']):
        row['refuse'].append('material_outside_table')      # a visible negative index animated_record could not map
    if {'mat3', 'material_outside_table', 'texture_animation_unsupported'} & set(row['refuse']) or anim_error:
        return row
    stem = lod_overlay.qualified_stem(path)
    row['atlas_stem'] = stem
    min_texels, floor_share, w_target = texel_opts(opts)
    collapse = lambda t: lod_atlas.collapse(assets, stem, list(mats), r0, alpha, t * opts['widths'][0] / 1280,
                                            opts['sizes'], specular=True, synth=True, textures=textures, bump=True)
    try:
        res = collapse(tp)
    except lod_atlas.AtlasError as exc:
        row['refuse'].append(atlas_reason(exc))
        row['atlas_error'] = str(exc)[:160]
        return row
    lay = res['layout']
    x0 = lod_atlas.texel_floor(lod_atlas.tile_rows(lay), min_texels, floor_share)
    if x0['refuse'] and w_target > 0:              # texel fallback: a lower switch size (the body appears farther out)
        def texel_at(t):
            L = lod_atlas.plan_layout(r0, mats, alpha, textures, t * opts['widths'][0] / 1280, opts['sizes'],
                                      lod_atlas.GUTTER, res['slots'], keep=frozenset(res['kept_effects']))
            return lod_atlas.texel_floor(lod_atlas.tile_rows(L), min_texels, floor_share), L['size']
        fb = texel_fallback(texel_at, tp, th[0] if th else None, x0, min_texels, floor_share, w_target)
        fb['size_before'] = lay['size']
        row['texel_fallback'] = fb
        if fb['accepted']:
            tp = fb['t_fb']
            row['t_pad'] = tp
            res = collapse(tp)
            lay = res['layout']
    row.update(slots=list(res['slots']), tiles=len(lay['tiles']), dup=res['info']['duplicated'],
               c_drawn=drawn_groups(res['record']), c_groups=sum(len(p['groups']) for p in res['record']['parts']),
               atlas_materials=len(res['atlas_indices']), occlusion=res['occlusion'])
    if res['kept_effects']:
        row['kept_effects'] = list(res['kept_effects'])     # own groups in C (lod_atlas.KEPT_EFFECTS)
    tex_shas = sorted({(textures.source(v) or {}).get('decoded_sha256') or 'unresolved:' + v.decode('latin1').lower()
                       for t in lay['tiles'] for v in t['names'].values() if v is not None})
    row['inputs_sha256'] = hashlib.sha256('\n'.join([row['source_decoded_sha256']] + tex_shas).encode()).hexdigest()
    row['texture_sources'] = sorted({v.split(':', 1)[-1] for t in lay['tiles'] for v in t['sources'].values() if v})
    row['radius_body'] = lay['radius']
    layouts = {opts['widths'][0]: lay}
    for w in opts['widths'][1:]:
        layouts[w] = lod_atlas.plan_layout(r0, mats, alpha, textures, tp * w / 1280, opts['sizes'],
                                           lod_atlas.GUTTER, res['slots'], keep=frozenset(res['kept_effects']))
    row['atlas'] = {}
    for w, L in layouts.items():
        tiles = lod_atlas.tile_rows(L)
        x = lod_atlas.texel_floor(tiles, min_texels, floor_share)
        row['atlas'][w] = dict(size=L['size'], ratio=L['min_ratio'], bytes=atlas_bytes(L['size'], res['slots']),
                               bytes_cap2048=atlas_bytes(min(L['size'], 2048), res['slots']),
                               weighted_ratio=x['weighted_texels_per_px'], starved_share=x['starved_share'],
                               clamped=bool(L.get('clamped')), tiles=tiles)
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
    s = (f'{base} lods={r["lods"]} thr={th} T_pad={r["t_pad"]}{"<T_1" if r.get("t_pad_below_t1") else ""} {aspect_text(r)}'
         f' mat={r["mat"]}'
         f' r0_faces={r["r0_faces"]} r0_points={r["r0_points"]} r0_groups={r["r0_groups"]}'
         f' r0_drawn={r["r0_drawn"]} coarse_drawn={r["coarse_groups"]} effects={r["effects"]} uv2={r["uv2"]}'
         f' alpha_mats={r["alpha"]}')
    if 'atlas' in r:
        a = r['atlas']
        wr = lambda x: '-' if x is None else f'{x:.2f}'
        s += (f' C_drawn={r["c_drawn"]} saved_vs_r0={r["saved_r0"]} saved_vs_coarse={r["saved_coarse"]} tiles={r["tiles"]} slots={len(r["slots"])}'
              + ''.join(f' atlas@{w}={a[w]["size"]}(ratio {a[w]["ratio"]:.2f}, weighted {wr(a[w].get("weighted_ratio"))},'
                        f' starved {100 * a[w].get("starved_share", 0):.1f}%{", clamped" if a[w].get("clamped") else ""},'
                        f' {mb(a[w]["bytes"])} MB)'
                        for w in a)
              + f' member~{mb(r["member_bytes"])} MB')
    if r.get('texel_fallback'):
        s += f' texel_fallback="{fallback_text(r["texel_fallback"])}"'
    if r.get('trailing'):
        s += f' trailing={r["trailing"]}'
    if r.get('text'):
        s += ' text'
    if r.get('animation'):
        s += (f' anim_groups={r["animation"]["groups"]} anim_rows={",".join(str(n) for n in r["animation"]["rows"])}'
              f' anim_material0={r["animation"]["material0"]}')
    if r.get('atlas_materials', 1) > 1:
        s += f' atlas_materials={r["atlas_materials"]}'
    s += bleed_text(r)
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
    fbs = [r for r in rows if r.get('texel_fallback')]
    lines.append(f'texel_fallback (W {texel_opts(opts)[2]:g} texels/px): tried {len(fbs)}, accepted'
                 f' {sum(1 for r in fbs if r["texel_fallback"]["accepted"])}'
                 + ''.join(f'; {r["name"]} {fallback_text(r["texel_fallback"])}' for r in fbs))
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


def parse_aspect_cap(text):
    try:
        ships, stations = (float(x) for x in text.split(','))
    except ValueError:
        raise argparse.ArgumentTypeError(f'--aspect-cap needs SHIPS,STATIONS, got {text!r}') from None
    if not (ships >= 1 and stations >= 1):
        raise argparse.ArgumentTypeError('--aspect-cap values must be >= 1')
    return ships, stations


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
    ap.add_argument('--aspect-cap', type=parse_aspect_cap, default=(RULE['aspect_ship'], RULE['aspect_station']),
                    metavar='SHIPS,STATIONS', help='K_max of the aspect rule (default 1.5,2.0)')
    ap.add_argument('--no-aspect', action='store_true', help='T_pad = T_class (no aspect factor)')
    ap.add_argument('--texel-fallback', type=float, default=lod_overlay.TEXEL_FALLBACK, metavar='W',
                    help='a body the texel floor would refuse at T_pad gets T_fb = round(T_pad * weighted / W),'
                         ' T_fb >= max(T_1, T_pad/4, 2) (lod_overlay.py --texel-fallback; default'
                         f' {lod_overlay.TEXEL_FALLBACK:g}, 0 disables)')
    ap.add_argument('--radius-log', action='append', type=Path, metavar='FILE',
                    help='flight census text with r=/radius= and body= (default: RADIUS_SOURCES present)')
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
    if a.texel_fallback < 0:
        ap.error('--texel-fallback must be >= 0')
    opts = dict(sizes=tuple(sizes), include_other=a.include_other, widths=widths,
                texel=dict(min_texels=lod_overlay.MIN_TEXELS, floor_share=lod_overlay.TEXEL_FLOOR_SHARE,
                           fallback=a.texel_fallback),
                rule=dict(ship_min=a.ship_min, ship_factor=a.ship_factor, station_t=a.station_t, t_cap=a.t_cap,
                          aspect=not a.no_aspect, aspect_ship=a.aspect_cap[0], aspect_station=a.aspect_cap[1]))
    t0 = time.time()
    rows, skipped = run(a.game, opts, a.jobs, a.limit, include_text=not a.binary_only)
    attach_world(rows, world_radii(a.radius_log or default_radius_logs()))
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
    rule = opts['rule']
    eligible = [f'# NAME=T_pad@0 at the census rule (aspect factor '
                + (f'on, K_max ships {rule["aspect_ship"]:g} stations {rule["aspect_station"]:g})' if rule['aspect']
                   else 'off)') + '; lod_overlay.py --batch --only reads the names, its own rule decides T_pad']
    eligible += [f'{r["name"]}={r["t_pad"]}@0' for r in sorted(rows, key=lambda r: r['name'].lower()) if r['eligible']]
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
