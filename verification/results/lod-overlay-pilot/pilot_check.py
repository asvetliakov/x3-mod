#!/usr/bin/env python3
"""Merged-LOD pilot overlay checks (prints numbers only; reads game archives, writes nothing).

A. The documented selection rule (bob1.final_index, Very High) against every
   finite-s multi-LOD row of a node census (run255: drawn_lod and census lod).
B. The produced overlay: per member, parse + serialise byte-identical, original
   records identical to the installed source, C equal to the source's last record
   in points, record flags, part flags, part ints and per-part face/7-int multisets
   (only the grouping may differ), C's groups (material, faces, alpha), pad a copy
   of C, Very High bands; CAT/DAT sizes and SHA-256.

  python3 verification/results/lod-overlay-pilot/pilot_check.py \
      <node_census_out.txt> <overlay root containing addon/NN.cat>
"""
import hashlib
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import Assets, unpack  # noqa: E402

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
    assets = Assets(bob1.DEFAULT_GAME)
    for e in read_catalogue(cat):
        data = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
        tree = bob1.parse(data)
        ladder = bob1.lods(tree)
        src_tree = bob1.parse(assets.get(e['path'])[0])
        source = bob1.lods(src_tree)
        alpha = lod_overlay.alpha_materials(bob1.materials(src_tree))
        n = len(source)
        th = [l['value'] for l in ladder[1:]]
        copy = ladder[n]['parts'] == ladder[-1]['parts'] and ladder[n]['points'] == ladder[-1]['points']
        print(f'B {e["path"]}: stored {e["size"]} decoded {len(data)} roundtrip_equal={bob1.serialise(tree) == data}'
              f' originals_identical={ladder[:n] == source} lods {n}->{len(ladder)} thresholds {th}'
              f' C/pad groups {[sum(len(p["groups"]) for p in l["parts"]) for l in ladder[n:]]}'
              f' pad_is_copy={copy} C_equals_source_last={same_geometry(ladder[n], source[-1])}'
              f' C_groups {[("alpha" if g["material"] in alpha else "opaque", g["material"], len(g["faces"])) for p in ladder[n]["parts"] for g in p["groups"]]}'
              f' very-high {bob1.format_bands(bob1.selection_bands(th))}')
    for p in (cat, dat):
        print(f'B file {p.name} bytes {p.stat().st_size} sha256 {sha(p)}')


if __name__ == '__main__':
    census(sys.argv[1])
    overlay(sys.argv[2])
