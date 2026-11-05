#!/usr/bin/env python3
"""Star-texture marks for the sky (docs/architecture/sector-background-modern.md, Decision A and answer 1).

numpy only. A star texture is a .dds/.pck under dds/ whose file stem contains both `nebula` and `stars` (any case,
any position: the stock dds/nebula_<family>_stars_<colour>_diff, the Mayhem/Litcube nebula_litcube<N>_stars_diff.dds)
and whose level-0 texels are a star body (at least 90 % below luma 16: black with sparse peaks); a name that matches but
fails that check is skipped not_star_body and left alone. Name rule (classify): the stem nebula_<family>_stars_<colour>
_diff and nebula_<family>_stars_diff keep the family and colour token exactly as before (any colour word, e.g. redfog,
is the token; lane 7 when it is not a STAR_LANES name); any other matching stem has family = its _-tokens without
nebula, stars, diff and the last STAR_LANES token (the colour), joined by one underscore (the whole stem when nothing
is left). The engine's winning copy is copied byte for byte, texels and mips identical, with the X3ML header mark in
dwReserved1[0..2] (file bytes 32-43, src/proxy/lod_texture_mark.h): version 1, descriptor slot = fnv1a32(family) &
0xff, lane = STAR_LANES index of the colour token, else 7 (no token included; a white tint at run time), K =
bright-peak count // 8 (3x3 local maxima with luma > 96), payload 2 (sky stars). The runtime star pass
finds the star-body draw and seeds its procedural star field from that mark alone.

The nebula background part re-authoring (Decision B) was removed on 2026-11-04 after the flight-level check found the
baked parts worse on screen than the stock parts magnified by the GPU; a nebula part is not a member any more, so a
--sync overlay run drops part members an earlier version wrote.

Used by lod_overlay.py --batch (the `sky` row group of the overlay, reuse keyed on inputs_sha256). Standalone:

  python3 tools/analysis/sky_textures.py --game DIR [--families a,b]

lists every star texture with its descriptor; it writes nothing.

Loose star textures (an AP mod ships addon/dds/nebula_*_stars*_diff.dds) beat every catalogue, so no overlay member
can carry their mark: x3m-regenerate's sky step (mark_loose) writes the same mark into the loose file's own header in
place (bytes 32-43; texels untouched) and records the original header in <game>/x3m/sky-loose.json; when the overlay's
sky group is off, or a file is no longer a star-texture winner, the recorded header goes back byte for byte
(restore_loose; standalone: --restore-loose). A loose .pck is reported skipped (loose_pck), never rewritten; a loose
file that fails the star-body check is skipped not_star_body (its ledger row, if any, restored).
"""
from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import hashlib
import json
import multiprocessing
import os
import re
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
import lod_atlas  # noqa: E402

RECIPE_ID = 'sky-stars-1'           # bump on any change of the output for the same inputs
TOOL = 'tools/analysis/sky_textures.py'
PAYLOAD_SKY_STARS = 2               # lod_texture_mark.h descriptor bits 24-31
STAR_LANES = ('white', 'blue', 'red', 'orange', 'yellow', 'green', 'cyan')   # lane = index; anything else 7
LANE_OTHER = 7
BRIGHT_PEAK_LUMA = 96
STAR_BODY_DARK_LUMA = 16             # a star body: at least STAR_BODY_DARK_SHARE of level 0 below this luma
STAR_BODY_DARK_SHARE = 0.9
_STARS = re.compile(r'dds/nebula_(?P<family>[a-z0-9_]+?)_stars(?:_(?P<colour>[a-z0-9]+))?_diff\.(?P<ext>pck|dds)')

progress = None   # optional callable(row, done, total): lod_overlay / x3m-regenerate per-texture console line


def fnv1a32(data):
    """fog_families.fnv1a32 (the profile-id hash), repeated here so the marks do not import the fog stack."""
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


SKY_TOOL_FILES = ('sky_textures.py', 'lod_atlas.py')   # the mark's sources (lod_atlas: DDS parse and decode)


def tool_sha256():
    """sha256 over sky_textures.py then lod_atlas.py (the frozen x3m-regenerate bundles both beside the modules, as
    lod_overlay's): either change re-marks the star textures. Outside lod_overlay.TOOL_FILES: never rebakes bodies."""
    h = hashlib.sha256()
    for name in SKY_TOOL_FILES:
        h.update((HERE / name).read_bytes())
    return h.hexdigest()


def classify(path):
    """(family, colour) of a star-texture name (any case, any separator, an addon/ or addon2/ prefix ignored; colour
    None when the name has no colour token), else None. The content check is star_body's."""
    p = re.sub(r'^addon2?/', '', path.replace('\\', '/').lower())
    m = _STARS.fullmatch(p)
    if m:                                  # the stock shape: family and colour token as the sky-stars-1 recipe has them
        return m['family'], m['colour']
    name = p.rsplit('/', 1)[-1]
    stem, dot, ext = name.rpartition('.')
    if not p.startswith('dds/') or not dot or ext not in ('dds', 'pck') or 'nebula' not in stem or 'stars' not in stem:
        return None
    tokens = [t for t in stem.split('_') if t and t not in ('nebula', 'stars', 'diff')]
    lanes = [i for i, t in enumerate(tokens) if t in STAR_LANES]
    colour = tokens.pop(lanes[-1]) if lanes else None
    return '_'.join(tokens) or stem, colour


def options_of(overrides=None):
    """The mark has no options; kept as the reuse settings' options field (always {})."""
    if overrides:
        raise ValueError(f'unknown sky options {sorted(overrides)}')
    return {}


def inputs_sha256(source):
    """sha256(unpacked source DDS bytes || recipe id): the reuse key (the whole DDS, header and every mip, because
    the output is those bytes plus the mark)."""
    h = hashlib.sha256(source)
    h.update(RECIPE_ID.encode())
    return h.hexdigest()


def mark_bytes(slot, lane, k, payload):
    """dwReserved1[0..2]: 'X3ML', version 1, slot | lane << 8 | K << 16 | payload << 24 (lod_texture_mark.h)."""
    for v in (slot, lane, k, payload):
        if not 0 <= v <= 255:
            raise ValueError(f'mark field {v} outside 0..255')
    return lod_atlas.DDS_MARK + struct.pack('<II', lod_atlas.DDS_MARK_VERSION, slot | lane << 8 | k << 16 | payload << 24)


def with_mark(data, mark):
    d = bytearray(data)
    d[lod_atlas.DDS_MARK_OFFSET:lod_atlas.DDS_MARK_OFFSET + len(mark)] = mark
    return bytes(d)


def store(dds, member):
    """Member bytes as the catalogue holds them: gzip (mtime 0) for a .pck, raw for a .dds."""
    return gzip.compress(dds, mtime=0) if member.lower().endswith('.pck') else dds


def star_descriptor(family, colour, rgba):
    """(slot, lane, K, peaks) of answer 1 from the level-0 texels."""
    lum = 0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2]
    pad = np.pad(lum, 1, mode='edge')
    h, w = lum.shape
    mx = np.max(np.stack([pad[i:i + h, j:j + w] for i in range(3) for j in range(3)]), 0)
    peaks = int(((lum == mx) & (lum > BRIGHT_PEAK_LUMA)).sum())
    lane = STAR_LANES.index(colour) if colour in STAR_LANES else LANE_OTHER
    return fnv1a32(family.encode('ascii', 'replace')) & 255, lane, min(peaks // 8, 255), peaks


class NotStarBody(ValueError):
    """The texels are not a painted star body (star_body)."""


def star_body(rgba):
    """True when at least STAR_BODY_DARK_SHARE of the level-0 texels are below luma STAR_BODY_DARK_LUMA."""
    lum = 0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2]
    return float((lum < STAR_BODY_DARK_LUMA).mean()) >= STAR_BODY_DARK_SHARE


def bake_stars(source, family, colour):
    """(dds, info): the source with the payload-2 mark; texels and mips byte-identical. NotStarBody when the texels
    fail star_body."""
    rgba = lod_atlas.decode_dds(source).astype(np.float32)
    if not star_body(rgba):
        raise NotStarBody('not_star_body')
    slot, lane, k, peaks = star_descriptor(family, colour, rgba)
    return with_mark(source, mark_bytes(slot, lane, k, PAYLOAD_SKY_STARS)), dict(
        slot=slot, lane=lane, k=k, peaks=peaks, payload=PAYLOAD_SKY_STARS)


def _bake_worker(row_source):
    row, source = row_source
    dds, info = bake_stars(source, row['family'], row['colour'])
    return dict(member=row['member'], stored=store(dds, row['member']), info=info)


def _winners(assets, families=None):
    """(entry, family, colour) of every star texture the engine would load from assets, the winner per resource (the
    highest layer: a loose file beats every catalogue; the expansion namespace wins)."""
    seen = set()
    for key in sorted(assets.entries):
        base = re.sub(r'^addon2?/', '', key)
        if base in seen or not base.startswith('dds/') or 'nebula' not in base:
            continue
        seen.add(base)
        cands = assets.candidates(base)
        if not cands:
            continue
        e = cands[-1]                                      # the engine's winner: the highest layer
        c = classify(e['path'])
        if c is None or (families is not None and c[0] not in families):
            continue
        yield e, c[0], c[1]


def scan(assets, families=None):
    """(rows, skipped): every star texture the engine would load from assets (sector_fog_census.Assets view, the
    caller's choice of layers; the expansion namespace wins), rows = dicts(member, source, family, colour, width,
    height, inputs_sha256, data), skipped = [(member, reason)]: loose_file (a loose winner: no catalogue member can
    beat it; mark_loose marks it in place), not_dds, not_star_body (the name matches, the texels fail star_body)."""
    rows, skipped = [], []
    for e, family, colour in _winners(assets, families):
        if 'loose' in e:
            skipped.append((e['path'], 'loose_file'))
            continue
        data = assets.read_entry(e)
        try:
            w, h, _, _ = lod_atlas.dds_format(data)
            body = star_body(lod_atlas.decode_dds(data).astype(np.float32))
        except (lod_atlas.AtlasError, ValueError, IndexError):
            skipped.append((e['path'], 'not_dds'))
            continue
        if not body:
            skipped.append((e['path'], 'not_star_body'))
            continue
        rows.append(dict(member=e['path'].replace('\\', '/'), source=e['source'], family=family, colour=colour,
                         width=w, height=h, inputs_sha256=inputs_sha256(data), data=data))
    return rows, skipped


def bake_rows(rows, jobs=1):
    """Results in the order of rows (jobs > 1: a spawned process pool); progress(row, done, total) as each finishes."""
    args = [({k: v for k, v in r.items() if k != 'data'}, r['data']) for r in rows]
    results, done = [None] * len(rows), 0
    if jobs <= 1 or len(rows) <= 1:
        for i, a in enumerate(args):
            results[i] = _bake_worker(a)
            done += 1
            if progress is not None:
                progress(rows[i], done, len(rows))
        return results
    ctx = multiprocessing.get_context('spawn')
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(rows)), mp_context=ctx) as pool:
        futs = {pool.submit(_bake_worker, a): i for i, a in enumerate(args)}
        for f in concurrent.futures.as_completed(futs):
            i = futs[f]
            results[i] = f.result()
            done += 1
            if progress is not None:
                progress(rows[i], done, len(rows))
    return results


def manifest_row(row, res):
    """The overlay manifest's sky row: member, source, inputs_sha256, member_sha256, metrics (the descriptor)."""
    return dict(member=row['member'], source=row['source'], inputs_sha256=row['inputs_sha256'],
                member_sha256=hashlib.sha256(res['stored']).hexdigest(),
                metrics=dict(kind='stars', family=row['family'], width=row['width'], height=row['height'],
                             bytes=len(res['stored']), **res['info']))


# --- loose star textures: the mark written into the file's own header, in place --------------------------------
# A loose file beats every catalogue (the engine resolver takes it first), so no overlay member can carry its mark.
# The mark is a 12-byte edit inside the 128-byte DDS header that the game ignores (dwReserved1[0..2], bytes 32-43);
# the ledger <game>/x3m/sky-loose.json keeps, per file, the original header (hex), the original file's sha256 and the
# file's sha256 after marking: a rerun recognises a marked file (reused) and a changed one (a mod update: re-marked,
# its new header recorded as the original), and restore_loose puts the recorded header back byte for byte.

LOOSE_LEDGER = 'sky-loose.json'
DDS_HEADER_BYTES = 128


def ledger_path(game):
    return Path(game) / 'x3m' / LOOSE_LEDGER


def read_ledger(game):
    """(rows, same_tool): the ledger's rows ([] when absent or unreadable: nothing recorded, nothing restored) and
    whether this recipe and sky tool wrote them."""
    try:
        g = json.loads(ledger_path(game).read_text())
    except (OSError, ValueError):
        return [], False
    if not isinstance(g, dict):
        return [], False
    rows = [r for r in g.get('rows') or () if isinstance(r, dict) and isinstance(r.get('loose'), str)]
    return rows, g.get('recipe_id') == RECIPE_ID and g.get('tool_sha256') == tool_sha256()


def write_ledger(game, rows):
    """Rows sorted by path, written to a .tmp and renamed; no rows removes the ledger."""
    path = ledger_path(game)
    if not rows:
        path.unlink(missing_ok=True)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(dict(tool=TOOL, recipe_id=RECIPE_ID, tool_sha256=tool_sha256(),
                                   rows=sorted(rows, key=lambda r: r['loose'].lower())), indent=1) + '\n')
    tmp.replace(path)


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _write_header(path, header, want_sha256):
    """Writes the 128 header bytes at offset 0 in place, then checks the whole file's sha256."""
    with open(path, 'r+b') as f:
        f.write(header)
        f.flush()
        os.fsync(f.fileno())
    if _sha(Path(path).read_bytes()) != want_sha256:
        raise OSError(f'{path}: sha256 after the header write does not match')


def scan_loose(assets, families=None):
    """(rows, skipped) of the loose star-texture winners: rows = dicts(loose, file, family, colour); skipped =
    [(path, 'loose_pck')]: a loose .pck is compressed, so its mark is not a header edit in place (the catalogue path
    re-encodes members; no writer re-encodes a loose file)."""
    rows, skipped = [], []
    for e, family, colour in _winners(assets, families):
        if 'loose' not in e:
            continue
        rel = e['path'].replace('\\', '/')
        if rel.lower().endswith('.pck'):
            skipped.append((rel, 'loose_pck'))
            continue
        rows.append(dict(loose=rel, file=Path(e['loose']), family=family, colour=colour))
    return rows, skipped


def _restore_one(game, row):
    """'restored' | 'original' (already the recorded original) | 'gone' | 'changed' (neither marked nor original: a
    mod replaced it; left alone) | 'read_only' (not written; the row is kept)."""
    f = Path(game) / row['loose']
    try:
        data = f.read_bytes()
    except FileNotFoundError:
        return 'gone'
    except OSError:
        return 'read_only'
    cur = _sha(data)
    if cur == row.get('original_sha256'):
        return 'original'
    if cur != row.get('marked_sha256'):
        return 'changed'
    if not os.access(f, os.W_OK):
        return 'read_only'
    try:
        _write_header(f, bytes.fromhex(row['original_header']), row['original_sha256'])
    except (OSError, ValueError, KeyError):
        return 'read_only'
    return 'restored'


def restore_loose(game, keep=()):
    """Restores the header of every ledger row whose path (any case) is not in keep; [(path, status)] (_restore_one).
    A restored row leaves the ledger; one whose file could not be written (read_only) stays."""
    keep = {k.lower() for k in keep}
    rows, _ = read_ledger(game)
    results, kept = [], []
    for r in rows:
        if r['loose'].lower() in keep:
            kept.append(r)
            continue
        status = _restore_one(game, r)
        results.append((r['loose'], status))
        if status == 'read_only':
            kept.append(r)
    if results:
        write_ledger(game, kept)
    return results


def mark_loose(game, assets, families=None):
    """Marks every loose star-texture winner of assets (game: the root its paths are relative to) in place and
    restores the ledger rows of files that are no longer one. dict(rows=[ledger rows with status built|reused],
    refused=[(path, reason)], skipped=[(path, 'not_star_body')], restored=[(path, status)]). Reasons: loose_pck, loose_unreadable, loose_not_dds (no
    parsable DDS header or texels), loose_read_only, loose_write_failed; a refusal never raises. The ledger records
    every original before the first header is written."""
    found, refused = scan_loose(assets, families)
    skipped = []
    rows, same_tool = read_ledger(game)
    old = {r['loose'].lower(): r for r in rows}
    done, held, pending = [], [], []   # held: old rows kept untouched (read-only file still marked)
    for r in found:
        o = old.get(r['loose'].lower())
        try:
            data = r['file'].read_bytes()
        except OSError:
            refused.append((r['loose'], 'loose_unreadable'))
            continue
        cur = _sha(data)
        if o and same_tool and cur == o.get('marked_sha256'):
            done.append(dict(o, status='reused'))
            continue
        source = data
        if o and len(data) >= DDS_HEADER_BYTES:   # the recorded header over the current one: still that file?
            try:
                cand = bytes.fromhex(o['original_header']) + data[DDS_HEADER_BYTES:]
            except (KeyError, TypeError, ValueError):
                cand = None
            if cand is not None and _sha(cand) == o.get('original_sha256'):
                source = cand
        try:
            w, h, _, _ = lod_atlas.dds_format(source)
            marked, info = bake_stars(source, r['family'], r['colour'])
        except NotStarBody:
            skipped.append((r['loose'], 'not_star_body'))
            continue
        except (lod_atlas.AtlasError, ValueError, IndexError):
            refused.append((r['loose'], 'loose_not_dds'))
            continue
        row = dict(loose=r['loose'], family=r['family'], colour=r['colour'], width=w, height=h,
                   original_header=source[:DDS_HEADER_BYTES].hex(), original_sha256=_sha(source),
                   marked_sha256=_sha(marked), metrics=dict(kind='stars', **info))
        if marked == data:
            done.append(dict(row, status='reused'))
        elif not os.access(r['file'], os.W_OK):
            refused.append((r['loose'], 'loose_read_only'))
            if o and cur == o.get('marked_sha256'):
                held.append(o)
        else:
            pending.append((row, r['file'], marked[:DDS_HEADER_BYTES]))
    keep = {r['loose'].lower() for r in done + held} | {row['loose'].lower() for row, _, _ in pending}
    restored = restore_loose(game, keep)
    plain = lambda r: {k: v for k, v in r.items() if k != 'status'}
    write_ledger(game, [plain(r) for r in done] + held + [row for row, _, _ in pending])
    for row, f, header in pending:
        try:
            _write_header(f, header, row['marked_sha256'])
            done.append(dict(row, status='built'))
        except OSError:              # the row stays: restore_loose finds the file original, marked or changed
            refused.append((row['loose'], 'loose_write_failed'))
    return dict(rows=done, refused=refused, skipped=skipped, restored=restored)


def loose_families(rows):
    """{family: dict(stars, built, reused)} of mark_loose rows."""
    out = {}
    for r in rows:
        f = out.setdefault(r['family'], dict(stars=0, built=0, reused=0))
        f['stars'] += 1
        f[r['status']] += 1
    return out


def source_view(game, edition=None):
    """The game's catalogue view without our x3m-lod overlay slots (lod_overlay.original_assets)."""
    import lod_overlay
    assets, _ = lod_overlay.original_assets(Path(game), edition=edition)
    return assets


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--families', help='comma-separated family names (default: every family)')
    ap.add_argument('--edition', choices=('auto', 'ap', 'fl'), default='auto')
    ap.add_argument('--restore-loose', action='store_true',
                    help='write: put the recorded original header back into every loose star texture marked in place')
    a = ap.parse_args(argv)
    if a.restore_loose:
        res = restore_loose(a.game.resolve())
        for path, status in res:
            print(f'{path} {status}')
        print(f'{sum(1 for _, s in res if s == "restored")} loose star textures restored, {len(res)} ledger rows')
        return 0
    fams = {f.strip().lower() for f in a.families.split(',') if f.strip()} if a.families else None
    rows, skipped = scan(source_view(a.game.resolve(), None if a.edition == 'auto' else a.edition), fams)
    for r, res in zip(rows, bake_rows(rows)):
        i = res['info']
        print(json.dumps(dict(member=r['member'], source=r['source'], slot=i['slot'], lane=i['lane'], k=i['k'],
                              peaks=i['peaks'])))
    print(f'{len(rows)} star textures, {len(skipped)} skipped' + ''.join(f'; {m} {why}' for m, why in skipped))
    return 0


if __name__ == '__main__':
    multiprocessing.freeze_support()
    sys.exit(main())
