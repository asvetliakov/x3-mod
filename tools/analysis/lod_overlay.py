#!/usr/bin/env python3
"""Build a numbered addon CAT/DAT overlay that appends one coarser LOD record to bodies.

Merged-LOD pilot mechanism (docs/architecture/merged-lod-feasibility.md section 6,
docs/reverse-engineering/body-format-bob1.md): for each named body, copy its
coarsest existing LOD (points, part flags, per-group 7-int records and the 10
part ints are copied, nothing is recomputed), collapse every part's groups into
one group carrying the part's dominant material by face count, and append it as
a new LOD record with threshold T. No decimation.

Placement: addon/NN.cat/.dat with NN the next contiguous free addon slot. The
engine resolver (body-format-bob1.md section 7, 0x004e7590) takes a loose file
first, otherwise the highest-numbered catalogue holding the name under any body
extension, with no mod selection needed; extension order applies only within
that layer. The overlay member keeps the winning member's exact archive path.
A body whose winning resource is loose is refused.

Hide-at-coarsest (0047d4d7, merged-lod-feasibility.md section 1): a node with
node+0x12c & 0x8000 is not rendered when its selected LOD is the body's
coarsest (count-1). The appended record becomes the coarsest, so such nodes now
hide only at the new record (s < T*f) and the old coarsest record draws them in
the band T..T_old where they used to be hidden. The pilot flight must check
flagged nodes. --keep-coarsest-hidden gives the new record the old coarsest
threshold instead: it then covers exactly the old coarsest range (the old
record is never selected), so hidden ranges are unchanged and unflagged nodes
get the collapsed record there.

Safety: the game's archives are only read. Every installed CAT/DAT is hashed
before and after a real run; any change fails the run and removes the outputs.
Outputs go to --out DIR (mirroring the game layout); the game directory is a
target only with --install, which refuses to overwrite anything.

  python3 tools/analysis/lod_overlay.py --dry-run stations/docks/argon_dock_center
  python3 tools/analysis/lod_overlay.py --out /tmp/x3m-lod stations/docks/argon_dock_center --threshold 20
"""
import argparse
import gzip
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
from sector_fog_census import Assets, write_catalogue  # noqa: E402

MARKER_SUFFIX = '.x3m-lod.json'


def original_archives(game):
    cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat'))
    return [p for cat in cats for p in (cat, cat.with_suffix('.dat'))]


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


def coarse_record(coarsest, threshold):
    """New LOD: coarsest's points and part data copied, groups collapsed to one per part."""
    parts = []
    for part in coarsest['parts']:
        new = {'flags': part['flags'], 'groups': part['groups']}
        if part['groups']:
            g = {'material': dominant_material(part['groups']),
                 'faces': [f for g in part['groups'] for f in g['faces']]}
            if part['flags'] & bob1.PART_PRECOMPUTED:
                g['extra'] = [e for g in part['groups'] for e in g['extra']]
            new['groups'] = [g]
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


def default_threshold(ladder):
    if len(ladder) < 2:
        return None                        # LOD 0's value is the object scale, not a threshold
    return max(1, ladder[-1]['value'] // 2)


def plan_body(assets, name, threshold, keep_coarsest_hidden=False):
    entry = bob1.resolve_body(assets, name)
    if 'loose' in entry:
        raise SystemExit(f'{name}: winning resource is loose file {entry["path"]}; a catalogue cannot override it')
    data = assets.read_entry(entry)
    if bob1.kind(data) != 'BOB1':
        raise SystemExit(f'{name}: {entry["path"]} is not a BOB1 body (magic {data[:4]!r})')
    tree = bob1.parse(data)
    if bob1.serialise(tree) != data:
        raise SystemExit(f'{name}: writer does not reproduce this body byte for byte; refusing')
    ladder = bob1.lods(tree)
    if keep_coarsest_hidden:
        if len(ladder) < 2:
            raise SystemExit(f'{name}: --keep-coarsest-hidden needs a body with at least two LOD records')
        t = ladder[-1]['value']            # new record takes over the old coarsest's range exactly
    else:
        t = threshold if threshold is not None else default_threshold(ladder)
    if t is None:
        raise SystemExit(f'{name}: single-LOD body; pass --threshold')
    if t <= 0:
        raise SystemExit(f'{name}: threshold must be positive')
    if len(ladder) > 1 and t >= ladder[-1]['value'] and not keep_coarsest_hidden:
        raise SystemExit(f'{name}: threshold {t} must be below the coarsest threshold {ladder[-1]["value"]}')
    new = coarse_record(ladder[-1], t)
    ladder.append(new)
    out = bob1.serialise(tree)
    check = bob1.lods(bob1.parse(out))
    if len(check) != len(ladder) or check[-1]['value'] != t:
        raise SystemExit(f'{name}: re-parse of the written body failed')
    with entry['cat'].with_suffix('.dat').open('rb') as f:
        f.seek(entry['offset'])
        head = bytes(v ^ 0x33 for v in f.read(2))
    stored = gzip.compress(out, mtime=0) if head == b'\x1f\x8b' or entry['path'].lower().endswith('.pbb') else out
    return dict(name=name, source=entry['source'], member=entry['path'], ladder=ladder, new=new,
                source_decoded_sha256=hashlib.sha256(data).hexdigest(),
                overlay_decoded_sha256=hashlib.sha256(out).hexdigest(),
                decoded_bytes=(len(data), len(out)), stored=stored)


def describe(plan, out=None):
    out = out or sys.stdout
    ladder = plan['ladder']
    src = ladder[-2]
    s_old, s_new = bob1.lod_summary(src), bob1.lod_summary(plan['new'])
    print(f'{plan["name"]}: {plan["source"]}:{plan["member"]}', file=out)
    print(f'  ladder before: thresholds {["-"] + [l["value"] for l in ladder[1:-1]]}'
          f' draws {[bob1.lod_summary(l)["draws"] for l in ladder[:-1]]}', file=out)
    mats = [g['material'] for p in plan['new']['parts'] for g in p['groups']]
    print(f'  new LOD{len(ladder) - 1}: threshold={plan["new"]["value"]} flags={plan["new"]["flags"]:#x}'
          f' points={s_new["points"]} parts={s_new["parts"]} groups/part {s_old["groups_per_part"]}'
          f' -> {s_new["groups_per_part"]} draws {s_old["draws"]} -> {s_new["draws"]} faces={s_new["faces"]}'
          f' dominant materials={mats} part_flags={[hex(f) for f in s_new["part_flags"]]}', file=out)
    print(f'  decoded bytes {plan["decoded_bytes"][0]} -> {plan["decoded_bytes"][1]},'
          f' stored {len(plan["stored"])} (gzip), overlay sha256 {plan["overlay_decoded_sha256"][:16]}', file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bodies', nargs='+', help='body name as the scene references it, or member path')
    ap.add_argument('--threshold', type=int, help='new LOD threshold T (default: coarsest threshold // 2)')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--out', type=Path, help='output root (receives addon/NN.cat/.dat)')
    ap.add_argument('--install', action='store_true', help='write into the game directory (never overwrites)')
    ap.add_argument('--keep-coarsest-hidden', action='store_true',
                    help='give the new record the old coarsest threshold, so it replaces the old coarsest'
                         ' range exactly and hide-at-coarsest nodes stay hidden where they were')
    ap.add_argument('--slot', type=int, help='addon catalogue number 1..99 (default and required: the next'
                                              ' contiguous free slot)')
    ap.add_argument('--force-slot', action='store_true', help='allow a --slot other than the next contiguous one')
    ap.add_argument('--dry-run', action='store_true', help='print the planned records; write nothing')
    a = ap.parse_args(argv)
    if a.keep_coarsest_hidden and a.threshold is not None:
        ap.error('--keep-coarsest-hidden sets the threshold; do not pass --threshold')
    if a.force_slot and a.slot is None:
        ap.error('--force-slot needs --slot')
    game = a.game.resolve()
    markers = sorted((game / 'addon').glob('*' + MARKER_SUFFIX))
    if markers:
        raise SystemExit(f'an x3m-lod overlay is already installed ({markers[0].name}); remove it first')
    if not a.dry_run:
        if a.install == (a.out is not None):
            raise SystemExit('pass exactly one of --out DIR or --install')
        root = game if a.install else a.out.resolve()
        if not a.install and (root == game or root.is_relative_to(game)):
            raise SystemExit('--out must be outside the game directory (use --install to target it)')
    if a.slot is None:
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
        if (game / rel).exists():
            raise SystemExit(f'{rel} already exists in the game directory')

    before = None if a.dry_run else hash_files(original_archives(game))
    assets = Assets(game)
    plans = [plan_body(assets, name, a.threshold, a.keep_coarsest_hidden) for name in a.bodies]
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
    if any(p.exists() for p in written):
        raise SystemExit(f'refusing to overwrite existing {cat_rel} outputs under {root}')
    write_catalogue(cat, [(p['member'], p['stored']) for p in plans])
    manifest = dict(tool='tools/analysis/lod_overlay.py', slot=slot, bodies=[
        dict(name=p['name'], source=p['source'], member=p['member'], new_lod=len(p['ladder']) - 1,
             threshold=p['new']['value'], source_decoded_sha256=p['source_decoded_sha256'],
             overlay_decoded_sha256=p['overlay_decoded_sha256']) for p in plans],
        originals=len(before), originals_sha256=hashlib.sha256(
            json.dumps(sorted(before.items())).encode()).hexdigest())
    written[2].write_text(json.dumps(manifest, indent=1) + '\n')
    after = hash_files(original_archives(game))
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k)
                     and not any(Path(k) == w for w in written))
    if changed:
        for w in written:
            w.unlink(missing_ok=True)
        raise SystemExit(f'original archives changed during the run ({changed}); outputs removed')
    print(f'wrote {", ".join(str(w) for w in written)}; {len(before)} original archive files unchanged')
    return 0


def cli():
    try:
        return main()
    except (FileNotFoundError, bob1.FormatError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(cli())
