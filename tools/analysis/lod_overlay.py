#!/usr/bin/env python3
"""Build a numbered addon CAT/DAT overlay that adds one coarser LOD record to bodies.

Merged-LOD pilot mechanism (docs/architecture/merged-lod-feasibility.md section 6,
docs/reverse-engineering/body-format-bob1.md): for each named body, copy its
coarsest existing LOD (points, part flags, per-group 7-int records and the 10
part ints are copied, nothing is recomputed), collapse every part's groups and
add the result as a new LOD record. No decimation. Alpha materials are those
whose effect parameters enable alpha testing or blending (g_AlphaTestEnable or
g_AlphaBlendEnable non-zero); an alpha texture alone does not count (the pilot
ships' lattice materials have both off and draw opaque, run257). Collapse
(--collapse), per part:
  glow (default): groups whose material has a mostly bright light map (share of
    texels with Rec.601 luma above --glow-luma at least --glow-share, measured on
    the smallest mip with a side >= 64; tools/analysis/body_materials.py) keep
    their own material, one group per such material: these carry the engine
    glows (exhaust materials' light maps, run257). The remaining faces collapse
    like two. Window lights on near-black light maps are dropped.
  two: at most two groups per part: faces of alpha materials go into a second
    group with the dominant alpha material among them by face count; the rest
    into one group with the dominant opaque material.
  one: one group per part with the dominant material (alpha faces turn solid).
Faces of merged materials get the dominant material's textures over their own
UVs (a look limit of the pilot).
MAT3 bodies (no MAT5/MAT6 section) are refused unless --force-mat3: the loader
gives every group of the coarsest record material 0x485 when such a body has
more than 3 LODs (body-format-bob1.md section 4), which would hit the pad.

Record placement (lod-selection.md, "What the selection really does, end to
end"): the loop picks sel = highest i with s < trunc(T_i*f), else 0; at View
Distance Very High (the X3 bottle) the tail draws clamp(sel - 1, 0, n-1), so the
last record is never drawn in the main view and a two-record body always draws
LOD 0. A plain append would never be seen there. The record at index k is drawn
at Very High exactly when record k+1 is the first hit. Hence:
  pad (default, every body; T_pad from --threshold or NAME=T, required): append
    the coarse record C with T_last (T_pad on a single-LOD body), then a pad copy
    of C with T_pad: ladder [T_0 .. T_last, C:T_last, pad:T_pad]. Walking from the
    end, s < T_pad*f hits the pad first, and the Very High -1 draws C; s >= T_pad*f
    falls through every original threshold (all below T_pad) and C's to record 0.
    So at Very High C replaces the whole ladder below T_pad and record 0 draws
    above it; LOD 1..n-1 of the original ladder become unreachable at every
    setting (intended: they carry most of the draws). At Low..High the pad (same
    mesh) draws below T_pad. T_pad must exceed every original threshold of
    records 1..n-1 (refused otherwise unless --force-threshold) and be >= 2.
    Original records are untouched.
  before-last (kept for the record; draws C only below T_last*f): insert the
    coarse record before the last one with T_new = T_last (--threshold overrides with any T <= T_last).
    With T_new <= T_last the walk hits the old last record first whenever
    s < T_last*f, so at Very High the -1 lands on the new record exactly in the
    band the old n-2 record used to cover, and at High and below the new record
    is never drawn (the old last still wins that band). The engine does not
    require descending thresholds, only the first hit from the top, so every
    ladder shape (including x/y/z/30) is accepted. At Very High this shows the
    old last record's geometry, collapsed, where it was never shown before.
  append-pad (kept for the record; --threshold T always required,
    T >= 3, and T < T_last for a multi-LOD body): append the coarse record with
    T, then a pad copy of it with T - 1, so the coarse record sits at index n-2
    of the new ladder. At Very High it draws below (T-1)*f and the band
    (T-1)*f <= s < T*f draws LOD 0 (single-LOD) or the old last record
    (multi-LOD). At Low..High the pad draws below (T-1)*f and the record itself
    in that band, i.e. the coarse mesh below T*f.
Hide-at-coarsest (0047d4d7): a node with node+0x12c & 0x8000 hides when its
final index is n-1. At Very High the final index never reaches n-1 in the main
view, so the hide does not fire there at all; at Low..High it fires at the
(new) last record, i.e. with pad for flagged nodes below T_pad*f. With
append-pad on a single-LOD body it becomes reachable at Low..High for the first
time (flagged nodes hide below (T-1)*f). The old --keep-coarsest-hidden option
was removed: its premise (the last record is drawn) does not hold at Very High. The pilot flight
must still check flagged nodes.

Placement: addon/NN.cat/.dat with NN the next contiguous free addon slot. The
engine resolver (body-format-bob1.md section 7, 0x004e7590) takes a loose file
first, otherwise the highest-numbered catalogue holding the name under any body
extension, with no mod selection needed; extension order applies only within
that layer. The overlay member keeps the winning member's exact archive path.
A body whose winning resource is loose is refused.

Safety: the game's archives are only read. Every installed CAT/DAT is hashed
before and after a real run; any change fails the run and removes the outputs.
Outputs go to --out DIR (mirroring the game layout); the game directory is a
target only with --install, which refuses to overwrite anything. Source bodies
are always read with every marker-carrying catalogue skipped. While an overlay is
installed (addon/NN.x3m-lod.json) the tool refuses unless --replace: then the
marker's originals hash must match the installed archives other than slot NN,
the new overlay takes slot NN. With --install the old three files are renamed to
*.x3m-replaced, the new ones written and the asides deleted last. If moving
aside, writing or the originals check fails, every file actually moved is put
back (os.replace over any new output at its path) and the new outputs are
removed only once all of them are back; a file that cannot be put back is left
as *.x3m-replaced and reported. A failure to delete an aside after success is a
warning only (the new overlay stays; delete the aside by hand).
--install refuses while X3AP.exe runs (verification/probe/game_guard.py; an
unreadable process table also refuses) unless --force-running: the game keeps
the old CAT/DAT open with the catalogue index in memory. On native Windows the
process check needs `ps` (absent there, so it refuses without --force-running),
and renaming or deleting a CAT/DAT the game holds open fails unless the game
opened it with FILE_SHARE_DELETE; the rename then fails and the restore path
runs.

  python3 tools/analysis/lod_overlay.py --dry-run --threshold 50 ships/argon/argon_TL
  python3 tools/analysis/lod_overlay.py --out /tmp/x3m-lod ships/argon/argon_TL=50 stations/others/military_outpost_middleb=100
"""
import argparse
import gzip
import hashlib
import json
import os
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
from sector_fog_census import Assets, write_catalogue  # noqa: E402

MARKER_SUFFIX = '.x3m-lod.json'
REPLACED_SUFFIX = '.x3m-replaced'


def running_game():
    """Process lines of a running X3AP game (verification/probe/game_guard.py); raises
    RuntimeError when the process table cannot be read (callers refuse)."""
    probe = str(Path(__file__).resolve().parents[2] / 'verification' / 'probe')
    if probe not in sys.path:
        sys.path.insert(0, probe)
    from game_guard import game_running
    return game_running()


def original_archives(game, exclude_slot=None):
    cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat'))
    skip = None if exclude_slot is None else game / 'addon' / f'{exclude_slot:02d}.cat'
    return [p for cat in cats if cat != skip for p in (cat, cat.with_suffix('.dat'))]


def originals_digest(hashes):
    return hashlib.sha256(json.dumps(sorted(hashes.items())).encode()).hexdigest()


def original_assets(game):
    """(Assets, skipped sources): every catalogue with a marker beside it is left out."""
    assets = Assets(Path(game))
    marked = lambda e: 'cat' in e and e['cat'].with_name(e['cat'].stem + MARKER_SUFFIX).exists()
    skipped = sorted({e['source'] for v in assets.entries.values() for e in v if marked(e)})
    if skipped:
        for key in list(assets.entries):
            kept = [e for e in assets.entries[key] if not marked(e)]
            if kept:
                assets.entries[key] = kept
            else:
                del assets.entries[key]
    return assets, skipped


def hash_files(paths):
    out = {}
    for p in paths:
        h = hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        out[str(p)] = h.hexdigest()
    return out


def next_slot(game):
    nums = sorted(int(p.stem) for p in (game / 'addon').glob('[0-9][0-9].cat'))
    if nums != list(range(1, len(nums) + 1)):
        raise SystemExit(f'addon catalogues are not contiguous from 01: {nums}')
    if len(nums) >= 99:
        raise SystemExit('no free addon slot')
    return len(nums) + 1


def dominant_material(groups):
    faces = Counter()
    for g in groups:                       # Counter keeps first-insertion order for ties
        faces[g['material']] += len(g['faces'])
    return max(faces, key=lambda m: faces[m]) if faces else None


COLLAPSES = ('glow', 'two', 'one')
GLOW_LUMA = 0.5          # texel counts as bright above this Rec.601 luma
GLOW_SHARE = 0.25        # light map is "mostly bright" at this bright-texel share
ALPHA_PARAMS = (b'g_alphatestenable', b'g_alphablendenable')


def alpha_materials(materials):
    """Group material indices (array positions, not the record's u16) whose effect
    parameters enable alpha testing or alpha blending."""
    return {i for i, m in enumerate(materials)
            if any(typ in (0, 1) and name.lower() in ALPHA_PARAMS and val and val[0]
                   for name, typ, val in m.get('params', ()))}


def glow_materials(assets, materials, used=None, luma=GLOW_LUMA, share=GLOW_SHARE):
    """Indices (of `used`, default all) whose light map has a bright-texel share >= share."""
    import body_materials                      # texture decoding lives there
    cache, out = {}, set()
    for i in (range(len(materials)) if used is None else used):
        light = body_materials.slots(materials[i]).get('light') if 0 <= i < len(materials) else None
        if light is not None and body_materials.texture_info(assets, light, cache, luma).get('bright', 0) >= share:
            out.add(i)
    return out


def merged_group(groups, precomputed):
    g = {'material': dominant_material(groups), 'faces': [f for g in groups for f in g['faces']]}
    if precomputed:
        g['extra'] = [e for g in groups for e in g['extra']]
    return g


def coarse_record(coarsest, threshold, alpha=frozenset(), collapse='two', glow=frozenset()):
    """New LOD: coarsest's points and part data copied; per part the groups are collapsed
    to one (collapse 'one'), to opaque + alpha groups (collapse 'two', alpha = material
    indices that alpha-test or alpha-blend; the alpha group follows the opaque one), or
    (collapse 'glow') to the opaque group, one group per glow material in first-use
    order, then the alpha group."""
    if collapse not in COLLAPSES:
        raise ValueError(f'unknown collapse {collapse!r}')
    parts = []
    for part in coarsest['parts']:
        new = {'flags': part['flags'], 'groups': part['groups']}
        if part['groups']:
            pre = part['flags'] & bob1.PART_PRECOMPUTED
            kept = glow if collapse == 'glow' else frozenset()
            rest = [g for g in part['groups'] if g['material'] not in kept]
            if collapse == 'one':
                classes = [rest]
            else:
                own = list(dict.fromkeys(g['material'] for g in part['groups'] if g['material'] in kept))
                classes = ([[g for g in rest if g['material'] not in alpha]]
                           + [[g for g in part['groups'] if g['material'] == m] for m in own]
                           + [[g for g in rest if g['material'] in alpha]])
            new['groups'] = [merged_group(c, pre) for c in classes if c]
        if 'bounds' in part:
            new['bounds'] = list(part['bounds'])
        parts.append(new)
    lod = {'value': threshold, 'flags': coarsest['flags']}
    if 'bones' in coarsest:
        lod['bones'] = list(coarsest['bones'])
    lod['points'] = coarsest['points']
    if 'weights' in coarsest:
        lod['weights'] = coarsest['weights']
    lod['parts'] = parts
    return lod


PLACEMENTS = ('pad', 'before-last', 'append-pad')


def default_placement(ladder):
    return 'pad'


def place(ladder, coarse, placement, threshold, name='body', force_threshold=False):
    """Insert the coarse record (value set here); returns (new index, pad index or None).

    pad: C at index n with T_last (T_pad for a single-LOD body), then a pad copy at
      n+1 with T_pad; T_pad required, >= 2, and above every original threshold of
      records 1..n-1 unless force_threshold.
    before-last: new record at index n-1 (old last moves to n), 1 <= T_new <= T_last,
      default T_last (any ladder shape, since only the first hit from the top matters).
    append-pad: new record at index n with T, then a pad copy at n+1 with T - 1;
      T is always required, 3 <= T, and T < T_last for a multi-LOD body."""
    n = len(ladder)
    if placement == 'pad':
        if threshold is None:
            raise SystemExit(f'{name}: pad placement needs a pad threshold (--threshold T or NAME=T)')
        t = threshold
        if t < 2:
            raise SystemExit(f'{name}: pad threshold {t} must be >= 2 (s >= 1, so a smaller one is never hit)')
        above = [l['value'] for l in ladder[1:] if l['value'] >= t]
        if above and not force_threshold:
            raise SystemExit(f'{name}: pad threshold {t} must exceed every original threshold'
                             f' {[l["value"] for l in ladder[1:]]} (records 1..n-1); --force-threshold overrides')
        coarse['value'] = ladder[-1]['value'] if n >= 2 else t
        pad = dict(coarse, value=t)
        ladder.extend([coarse, pad])
        return n, n + 1
    if placement == 'before-last':
        if n < 2:
            raise SystemExit(f'{name}: before-last needs at least two LOD records; use append-pad')
        t_last = ladder[-1]['value']
        t = t_last if threshold is None else threshold
        if not 1 <= t <= t_last:
            raise SystemExit(f'{name}: before-last threshold {t} must satisfy 1 <= T <= T_last = {t_last}')
        coarse['value'] = t
        ladder.insert(n - 1, coarse)
        return n - 1, None
    if threshold is None:
        raise SystemExit(f'{name}: append-pad needs --threshold')
    t = threshold
    if t < 3:
        raise SystemExit(f'{name}: append-pad threshold {t} must be >= 3 (the pad gets T - 1 >= 2)')
    if n >= 2 and t >= ladder[-1]['value']:
        raise SystemExit(f'{name}: append-pad threshold {t} must be below the last threshold {ladder[-1]["value"]}')
    coarse['value'] = t
    pad = dict(coarse, value=t - 1)
    ladder.extend([coarse, pad])
    return n, n + 1


def plan_body(assets, name, threshold, placement=None, force_threshold=False, collapse='glow', force_mat3=False,
              glow_luma=GLOW_LUMA, glow_share=GLOW_SHARE):
    entry = bob1.resolve_body(assets, name)
    if 'loose' in entry:
        raise SystemExit(f'{name}: winning resource is loose file {entry["path"]}; a catalogue cannot override it')
    data = assets.read_entry(entry)
    if bob1.kind(data) != 'BOB1':
        raise SystemExit(f'{name}: {entry["path"]} is not a BOB1 body (magic {data[:4]!r})')
    tree = bob1.parse(data)
    if bob1.serialise(tree) != data:
        raise SystemExit(f'{name}: writer does not reproduce this body byte for byte; refusing')
    if not any(t in ('MAT5', 'MAT6') for t, _ in tree['sections']) and not force_mat3:
        raise SystemExit(f'{name}: MAT3 body (no per-body materials); the loader rewrites the coarsest'
                         ' record of such a body with > 3 LODs to material 0x485; --force-mat3 overrides')
    ladder = bob1.lods(tree)
    before = list(ladder)
    placement = placement or default_placement(ladder)
    mats = bob1.materials(tree)
    alpha = alpha_materials(mats)
    used = sorted({g['material'] for p in ladder[-1]['parts'] for g in p['groups']})
    glow = glow_materials(assets, mats, used, glow_luma, glow_share) if collapse == 'glow' else set()
    new = coarse_record(ladder[-1], None, alpha, collapse, glow)
    new_index, pad_index = place(ladder, new, placement, threshold, name, force_threshold)
    out = bob1.serialise(tree)
    check = bob1.lods(bob1.parse(out))
    if [l['value'] for l in check] != [l['value'] for l in ladder]:
        raise SystemExit(f'{name}: re-parse of the written body failed')
    with entry['cat'].with_suffix('.dat').open('rb') as f:
        f.seek(entry['offset'])
        head = bytes(v ^ 0x33 for v in f.read(2))
    stored = gzip.compress(out, mtime=0) if head == b'\x1f\x8b' or entry['path'].lower().endswith('.pbb') else out
    return dict(name=name, source=entry['source'], member=entry['path'], before=before, ladder=ladder,
                new=new, new_index=new_index, collapse=collapse, alpha=alpha, glow=glow, pad_index=pad_index, placement=placement,
                source_decoded_sha256=hashlib.sha256(data).hexdigest(),
                overlay_decoded_sha256=hashlib.sha256(out).hexdigest(),
                decoded_bytes=(len(data), len(out)), stored=stored)


def record_bytes(lod):
    w = bob1.Writer()
    bob1.write_lod(w, lod)
    return len(w.b)


def ladder_text(ladder):
    return (f'thresholds {["-"] + [l["value"] for l in ladder[1:]]}'
            f' draws {[bob1.lod_summary(l)["draws"] for l in ladder]}'
            f' record bytes {[record_bytes(l) for l in ladder]}')


def group_kind(plan, g):
    return ('glow' if g['material'] in plan.get('glow', ()) else
            'alpha' if g['material'] in plan['alpha'] else 'opaque')


def describe(plan, out=None):
    out = out or sys.stdout
    s_old, s_new = bob1.lod_summary(plan['before'][-1]), bob1.lod_summary(plan['new'])
    print(f'{plan["name"]}: {plan["source"]}:{plan["member"]}', file=out)
    print(f'  ladder before: {ladder_text(plan["before"])}', file=out)
    print(f'  ladder after:  {ladder_text(plan["ladder"])}', file=out)
    groups = [f'{group_kind(plan, g)}:mat{g["material"]}:{len(g["faces"])}f'
              for p in plan['new']['parts'] for g in p['groups']]
    pad = (f' + pad copy LOD{plan["pad_index"]} threshold={plan["ladder"][plan["pad_index"]]["value"]}'
           if plan['pad_index'] else '')
    th = [l['value'] for l in plan['before'][1:]]
    vh = bob1.drawable([l['value'] for l in plan['ladder'][1:]], 'very-high')
    print(f'  {plan["placement"]}: new LOD{plan["new_index"]} threshold={plan["new"]["value"]}{pad}'
          f' flags={plan["new"]["flags"]:#x} points={s_new["points"]} parts={s_new["parts"]}'
          f' groups/part {s_old["groups_per_part"]} -> {s_new["groups_per_part"]} draws {s_old["draws"]} ->'
          f' {s_new["draws"]} faces={s_new["faces"]} collapse={plan["collapse"]} groups={groups}'
          f' part_flags={[hex(f) for f in s_new["part_flags"]]}', file=out)
    print(f'  main-view drawable at Very High (f=1): before {bob1.drawable(th, "very-high")} after {vh}'
          f' (new record drawable: {plan["new_index"] in vh})', file=out)
    new_th = [l['value'] for l in plan['ladder'][1:]]
    for view in bob1.VIEW_DISTANCE:
        print(f'  {view:>9} drawable {bob1.drawable(new_th, view)} by s: before'
              f' {bob1.format_bands(bob1.selection_bands(th, view))} | after'
              f' {bob1.format_bands(bob1.selection_bands(new_th, view))}', file=out)
    print(f'  decoded bytes {plan["decoded_bytes"][0]} -> {plan["decoded_bytes"][1]},'
          f' stored {len(plan["stored"])} (gzip), overlay sha256 {plan["overlay_decoded_sha256"][:16]}', file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bodies', nargs='+', help='body name as the scene references it, or member path;'
                                              ' NAME=T sets that body\'s threshold (overrides --threshold)')
    ap.add_argument('--threshold', type=int,
                    help='pad: T_pad, required, above every original threshold of records 1..n-1;'
                         ' before-last: T <= T_last, default T_last;'
                         ' append-pad: required, 3 <= T, below T_last for multi-LOD bodies')
    ap.add_argument('--force-threshold', action='store_true',
                    help='pad: accept a T_pad that does not exceed every original threshold')
    ap.add_argument('--placement', choices=PLACEMENTS, help='placement (default: pad)')
    ap.add_argument('--collapse', choices=COLLAPSES, default='glow',
                    help='glow (default): bright-light-map materials keep their group, the rest as two;'
                         ' two: opaque + alpha-tested/blended group per part; one: a single group per part')
    ap.add_argument('--glow-luma', type=float, default=GLOW_LUMA,
                    help=f'glow: luma above which a light-map texel is bright (default {GLOW_LUMA})')
    ap.add_argument('--glow-share', type=float, default=GLOW_SHARE,
                    help=f'glow: bright-texel share that makes a light map a glow map (default {GLOW_SHARE})')
    ap.add_argument('--replace', action='store_true',
                    help='replace the installed x3m-lod overlay (its slot; originals hash must match)')
    ap.add_argument('--force-running', action='store_true',
                    help='allow --install while X3AP.exe is running or the process table cannot be read. A'
                         ' running game keeps the old CAT/DAT open (and its catalogue index in memory); a DAT'
                         ' it reopens after the swap would not match that index')
    ap.add_argument('--force-mat3', action='store_true', help='accept a MAT3 body (see the module notes)')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--out', type=Path, help='output root (receives addon/NN.cat/.dat)')
    ap.add_argument('--install', action='store_true', help='write into the game directory (never overwrites)')
    ap.add_argument('--slot', type=int, help='addon catalogue number 1..99 (default and required: the next'
                                              ' contiguous free slot)')
    ap.add_argument('--force-slot', action='store_true', help='allow a --slot other than the next contiguous one')
    ap.add_argument('--dry-run', action='store_true', help='print the planned records; write nothing')
    a = ap.parse_args(argv)
    if a.force_slot and a.slot is None:
        ap.error('--force-slot needs --slot')
    if not (0 <= a.glow_luma < 1 and 0 < a.glow_share <= 1):
        ap.error('--glow-luma must be in [0, 1) and --glow-share in (0, 1]')
    game = a.game.resolve()
    markers = sorted((game / 'addon').glob('*' + MARKER_SUFFIX))
    replace_slot = None
    if markers and not a.replace:
        raise SystemExit(f'an x3m-lod overlay is already installed ({markers[0].name}); remove it first'
                         ' or pass --replace')
    if a.replace:
        if len(markers) != 1:
            raise SystemExit(f'--replace needs exactly one installed x3m-lod overlay, found {len(markers)}')
        try:
            manifest = json.loads(markers[0].read_text())
            replace_slot = int(manifest['slot'])
            expected = manifest['originals_sha256']
        except (ValueError, KeyError, TypeError) as exc:
            raise SystemExit(f'--replace: unreadable marker {markers[0].name} ({exc})') from None
        if markers[0].name != f'{replace_slot:02d}{MARKER_SUFFIX}':
            raise SystemExit(f'--replace: marker {markers[0].name} does not name slot {replace_slot}')
        if originals_digest(hash_files(original_archives(game, replace_slot))) != expected:
            raise SystemExit(f'--replace: the installed archives other than addon/{replace_slot:02d} do not match'
                             f' the originals hash in {markers[0].name}; refusing')
        if a.slot is not None and a.slot != replace_slot:
            raise SystemExit(f'--replace: --slot {a.slot} differs from the installed overlay slot {replace_slot}')
    if not a.dry_run:
        if a.install == (a.out is not None):
            raise SystemExit('pass exactly one of --out DIR or --install')
        if a.install and not a.force_running:
            try:
                lines = running_game()
            except RuntimeError as exc:
                raise SystemExit(f'--install: cannot tell whether the game is running ({exc});'
                                 ' pass --force-running to override') from None
            if lines:
                raise SystemExit(f'--install: the game is running ({lines[0]}); quit it first'
                                 ' (--force-running overrides)')
        root = game if a.install else a.out.resolve()
        if not a.install and (root == game or root.is_relative_to(game)):
            raise SystemExit('--out must be outside the game directory (use --install to target it)')
    if replace_slot is not None:
        slot = replace_slot
    elif a.slot is None:
        slot = next_slot(game)
    else:
        slot = a.slot
        if not 1 <= slot <= 99:
            raise SystemExit(f'--slot {slot}: must be 1..99')
        if not a.force_slot and slot != next_slot(game):
            raise SystemExit(f'--slot {slot}: the next contiguous free slot is {next_slot(game)}'
                             ' (pass --force-slot to override)')
    cat_rel = f'addon/{slot:02d}.cat'
    for rel in (cat_rel, f'addon/{slot:02d}.dat', f'addon/{slot:02d}{MARKER_SUFFIX}'):
        if (game / rel).exists() and replace_slot is None:
            raise SystemExit(f'{rel} already exists in the game directory')

    before = None if a.dry_run else hash_files(original_archives(game, replace_slot))
    assets, skipped = original_assets(game)
    if skipped:
        print(f'source bodies read without the overlay catalogue(s) {skipped}')
    plans = []
    for arg in a.bodies:
        name, sep, t = arg.partition('=')
        try:
            threshold = int(t) if sep else a.threshold
        except ValueError:
            raise SystemExit(f'{arg}: NAME=T needs an integer threshold') from None
        plans.append(plan_body(assets, name, threshold, a.placement, a.force_threshold, a.collapse, a.force_mat3,
                               a.glow_luma, a.glow_share))
    members = [p['member'] for p in plans]
    if len({m.lower() for m in members}) != len(members):
        raise SystemExit('the same body was named twice')
    for p in plans:
        describe(p)
    print(f'target {cat_rel} + .dat: {len(plans)} member(s),'
          f' dat bytes {sum(len(p["stored"]) for p in plans)}')
    if a.dry_run:
        print('dry run: nothing written')
        return 0

    cat = root / cat_rel
    written = [cat, cat.with_suffix('.dat'), cat.with_name(cat.stem + MARKER_SUFFIX)]
    replacing = a.install and replace_slot is not None
    asides = [(w, w.with_name(w.name + REPLACED_SUFFIX)) for w in written]
    if replacing:
        for _, aside in asides:
            if aside.exists():
                raise SystemExit(f'{aside} exists (an interrupted --replace?); resolve it by hand')
    elif any(p.exists() for p in written):
        raise SystemExit(f'refusing to overwrite existing {cat_rel} outputs under {root}')

    moved = []               # (target, aside) pairs actually moved aside, in order
    state = {'writing': False}

    def restore():
        """Put every moved file back (replacing a new output at its path), then, only if all
        of them are back and writing had started, remove the remaining new outputs."""
        stuck = []
        for w, aside in reversed(moved):
            if aside.exists():
                try:
                    os.replace(aside, w)
                except OSError as exc:
                    stuck.append(f'{aside} -> {w}: {exc}')
        if stuck:
            print('error: could not restore the replaced overlay; left in place: ' + '; '.join(stuck),
                  file=sys.stderr)
            return
        if state['writing']:
            restored = {w for w, _ in moved}
            for w in written:
                if w not in restored:
                    w.unlink(missing_ok=True)
    try:
        if replacing:
            for w, aside in asides:
                if w.exists():
                    w.rename(aside)
                    moved.append((w, aside))
        state['writing'] = True
        write_overlay(a, game, plans, slot, before, written, replace_slot)
    except BaseException:
        restore()
        raise
    left = []
    for _, aside in moved:                 # last step; a failure here is only a warning
        try:
            aside.unlink()
        except OSError as exc:
            left.append(f'{aside} ({exc})')
    if left:
        print('warning: the new overlay is installed but these replaced files could not be removed'
              ' (delete them by hand): ' + '; '.join(left), file=sys.stderr)
    print(f'wrote {", ".join(str(w) for w in written)}; {len(before)} original archive files unchanged'
          + (f'; replaced the installed addon/{slot:02d} overlay' if moved else ''))
    return 0


def write_overlay(a, game, plans, slot, before, written, replace_slot):
    write_catalogue(written[0], [(p['member'], p['stored']) for p in plans])
    manifest = dict(tool='tools/analysis/lod_overlay.py', slot=slot, collapse=a.collapse,
                    glow_luma=a.glow_luma, glow_share=a.glow_share, bodies=[
        dict(name=p['name'], source=p['source'], member=p['member'], placement=p['placement'], collapse=p['collapse'],
             glow=sorted(p['glow']), new_lod=p['new_index'], pad_lod=p['pad_index'],
             threshold=p['new']['value'],
             pad_threshold=p['ladder'][p['pad_index']]['value'] if p['pad_index'] else None,
             source_decoded_sha256=p['source_decoded_sha256'],
             overlay_decoded_sha256=p['overlay_decoded_sha256']) for p in plans],
        originals=len(before), originals_sha256=originals_digest(before))
    written[2].write_text(json.dumps(manifest, indent=1) + '\n')
    after = hash_files(original_archives(game, replace_slot))
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k)
                     and not any(Path(k) == w for w in written))
    if changed:
        raise SystemExit(f'original archives changed during the run ({changed}); outputs removed')


def cli():
    try:
        return main()
    except (FileNotFoundError, bob1.FormatError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(cli())
