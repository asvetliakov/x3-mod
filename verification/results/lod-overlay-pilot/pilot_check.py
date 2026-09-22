#!/usr/bin/env python3
"""Merged-LOD pilot overlay checks (prints numbers only; reads game archives, writes nothing).

A. The documented selection rule (bob1.final_index, Very High) against every
   finite-s multi-LOD row of a node census (run255: drawn_lod and census lod).
B. The produced overlay: per member, parse + serialise byte-identical, original
   records identical to the installed source, C equal to the source's last record
   in points, record flags, part flags, part ints and per-part face/7-int multisets
   (only the grouping may differ), C's groups (kind, material, faces), pad a copy
   of C, Very High bands; CAT/DAT sizes and SHA-256. Sources are read with any
   x3m-lod overlay catalogue skipped (lod_overlay.original_assets). Kind: glow =
   material in the overlay manifest's glow list, alpha = alpha-tests or
   alpha-blends (lod_overlay.alpha_materials), else opaque. Checks for --collapse
   glow: one opaque group, at most one alpha group, and exactly one group per glow
   material used by the source's last record (recomputed with glow_materials).

  python3 verification/results/lod-overlay-pilot/pilot_check.py \
      <node_census_out.txt> <overlay root containing addon/NN.cat>
"""
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402

ROW = re.compile(r'^\S+ \S+\s+\d+\s+\d+\s+\d+ \[(\d+)\] (\d+),(\d+),\w+ (\d+) ([\d,]+) (\S+)$')


def census(path):
    rows = ok = 0
    bad = []
    for line in Path(path).read_text().splitlines():
        m = ROW.match(line)
        if not m or int(m.group(4)) < 2:
            continue
        drawn, s, lod = int(m.group(1)), int(m.group(2)), int(m.group(3))
        th = [int(v) for v in m.group(5).split(',')[1:]]
        want = bob1.final_index(th, s, 'very-high')
        rows += 1
        if drawn == lod == want:
            ok += 1
        else:
            bad.append((m.group(6), s, th, drawn, lod, want))
    print(f'A census rule (very-high, f=1): multi-LOD rows {rows}, match {ok}, mismatch {len(bad)}')
    for b in bad:
        print(f'  mismatch {b}')


def same_geometry(c, src):
    if (c['points'], c['flags'], len(c['parts'])) != (src['points'], src['flags'], len(src['parts'])):
        return False
    for a, b in zip(c['parts'], src['parts']):
        if a['flags'] != b['flags'] or a.get('bounds') != b.get('bounds'):
            return False
        for key in ('faces', 'extra'):
            if sorted(x for g in a['groups'] for x in g.get(key, ())) != \
                    sorted(x for g in b['groups'] for x in g.get(key, ())):
                return False
    return True


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def overlay(root):
    (cat,) = sorted(Path(root).glob('addon/[0-9][0-9].cat'))
    dat = cat.with_suffix('.dat')
    raw = dat.read_bytes()
    marker = json.loads(cat.with_name(cat.stem + lod_overlay.MARKER_SUFFIX).read_text())
    glow_of = {b['member']: set(b.get('glow', ())) for b in marker['bodies']}
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    print(f'B collapse {marker.get("collapse")} glow_luma {marker.get("glow_luma")} glow_share'
          f' {marker.get("glow_share")}; sources read without {skipped}')
    for e in read_catalogue(cat):
        data = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
        tree = bob1.parse(data)
        ladder = bob1.lods(tree)
        src_data, src = assets.get(e['path'])
        src_tree = bob1.parse(src_data)
        source = bob1.lods(src_tree)
        n = len(source)
        mats = bob1.materials(src_tree)
        alpha = lod_overlay.alpha_materials(mats)
        glow = glow_of[e['path']]
        used = sorted({g['material'] for p in source[-1]['parts'] for g in p['groups']})
        glow_ok = glow == lod_overlay.glow_materials(assets, mats, used, marker['glow_luma'], marker['glow_share'])
        kind = lambda m: 'glow' if m in glow else 'alpha' if m in alpha else 'opaque'
        shape_ok = True
        for cp, sp in zip(ladder[n]['parts'], source[-1]['parts']):
            kinds = [kind(g['material']) for g in cp['groups']]
            glow_groups = sorted(g['material'] for g in cp['groups'] if g['material'] in glow)
            shape_ok &= (kinds.count('opaque') <= 1 and kinds.count('alpha') <= 1
                         and glow_groups == sorted({g['material'] for g in sp['groups']} & glow))
        c_groups = [(kind(g['material']), g['material'], len(g['faces'])) for p in ladder[n]['parts'] for g in p['groups']]
        th = [l['value'] for l in ladder[1:]]
        copy = ladder[n]['parts'] == ladder[-1]['parts'] and ladder[n]['points'] == ladder[-1]['points']
        print(f'B {e["path"]}: source {src["source"]} stored {e["size"]} decoded {len(data)}'
              f' roundtrip_equal={bob1.serialise(tree) == data}'
              f' originals_identical={ladder[:n] == source} lods {n}->{len(ladder)} thresholds {th}'
              f' C/pad groups {[sum(len(p["groups"]) for p in l["parts"]) for l in ladder[n:]]}'
              f' pad_is_copy={copy} C_equals_source_last={same_geometry(ladder[n], source[-1])}'
              f' glow_recomputed_equal={glow_ok} C_shape_ok={shape_ok}'
              f' C_groups {c_groups}'
              f' very-high {bob1.format_bands(bob1.selection_bands(th))}')
    for p in (cat, dat):
        print(f'B file {p.name} bytes {p.stat().st_size} sha256 {sha(p)}')


if __name__ == '__main__':
    census(sys.argv[1])
    overlay(sys.argv[2])
