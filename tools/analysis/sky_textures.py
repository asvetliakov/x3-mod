#!/usr/bin/env python3
"""Star-texture marks for the sky (docs/architecture/sector-background-modern.md, Decision A and answer 1).

numpy only. The painted star textures dds/nebula_<family>_stars_<colour>_diff.pck (the engine's winning member) are
copied byte for byte, texels and mips identical, with the X3ML header mark in dwReserved1[0..2] (file bytes 32-43,
src/proxy/lod_texture_mark.h): version 1, descriptor slot = fnv1a32(family) & 0xff, lane = hue class of the colour
token, K = bright-peak count // 8 (3x3 local maxima with luma > 96), payload 2 (sky stars). The runtime star pass
finds the star-body draw and seeds its procedural star field from that mark alone.

The nebula background part re-authoring (Decision B) was removed on 2026-11-04 after the flight-level check found the
baked parts worse on screen than the stock parts magnified by the GPU; a nebula part is not a member any more, so a
--sync overlay run drops part members an earlier version wrote.

Used by lod_overlay.py --batch (the `sky` row group of the overlay, reuse keyed on inputs_sha256). Standalone:

  python3 tools/analysis/sky_textures.py --game DIR [--families a,b]

lists every star texture with its descriptor; it writes nothing.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import hashlib
import json
import multiprocessing
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
_STARS = re.compile(r'dds/nebula_(?P<family>[a-z0-9_]+?)_stars_(?P<colour>[a-z0-9]+)_diff\.(?P<ext>pck|dds)')

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
    """(family, colour) of a star-texture member path (any case, any separator, an addon/ or addon2/ prefix ignored),
    else None."""
    m = _STARS.fullmatch(re.sub(r'^addon2?/', '', path.replace('\\', '/').lower()))
    return (m['family'], m['colour']) if m else None


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


def bake_stars(source, family, colour):
    """(dds, info): the source with the payload-2 mark; texels and mips byte-identical."""
    rgba = lod_atlas.decode_dds(source).astype(np.float32)
    slot, lane, k, peaks = star_descriptor(family, colour, rgba)
    return with_mark(source, mark_bytes(slot, lane, k, PAYLOAD_SKY_STARS)), dict(
        slot=slot, lane=lane, k=k, peaks=peaks, payload=PAYLOAD_SKY_STARS)


def _bake_worker(row_source):
    row, source = row_source
    dds, info = bake_stars(source, row['family'], row['colour'])
    return dict(member=row['member'], stored=store(dds, row['member']), info=info)


def scan(assets, families=None):
    """(rows, skipped): every star texture the engine would load from assets (sector_fog_census.Assets view, the
    caller's choice of layers; the expansion namespace wins), rows = dicts(member, source, family, colour, width,
    height, inputs_sha256, data), skipped = [(member, reason)]."""
    rows, skipped, seen = [], [], set()
    for key in sorted(assets.entries):
        base = re.sub(r'^addon2?/', '', key)
        if base in seen or not base.startswith('dds/nebula_'):
            continue
        seen.add(base)
        cands = assets.candidates(base)
        if not cands:
            continue
        e = cands[-1]                                      # the engine's winner: the highest layer
        c = classify(e['path'])
        if c is None or (families is not None and c[0] not in families):
            continue
        if 'loose' in e:
            skipped.append((e['path'], 'loose_file'))
            continue
        data = assets.read_entry(e)
        try:
            w, h, _, _ = lod_atlas.dds_format(data)
        except lod_atlas.AtlasError:
            skipped.append((e['path'], 'not_dds'))
            continue
        rows.append(dict(member=e['path'].replace('\\', '/'), source=e['source'], family=c[0], colour=c[1], width=w,
                         height=h, inputs_sha256=inputs_sha256(data), data=data))
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
    a = ap.parse_args(argv)
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
