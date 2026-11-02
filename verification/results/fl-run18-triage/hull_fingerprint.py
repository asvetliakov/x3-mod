#!/usr/bin/env python3
"""FL run 18 frame 15972: name the scoped hull node models by their draw fingerprint (draw count + per-draw
primitive counts from object_context/draw rows) against every LOD of every ship body in the FL mounted catalogues.
Prints names, counts and matches only.

  python3 -I verification/results/fl-run18-triage/hull_fingerprint.py > verification/results/fl-run18-triage/hull_fingerprint_out.txt
"""
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402

# measured, frame 15972 (object_context model -> draw primitives in draw order)
TARGETS = {
    '0x4f77': [78, 16, 8, 8, 68, 508, 178, 36, 823, 290, 2009, 4459, 233, 780, 3270, 78, 226, 144, 28, 56, 148, 16, 32, 52,
               12, 2176, 34, 198, 856, 893, 304, 58],
    '0x4f74': [68, 16, 8, 8, 56, 364, 176, 36, 641, 282, 1661, 3338, 233, 780, 2930, 10, 218, 138, 28, 56, 148, 16, 8, 8,
               1846, 34, 192, 746, 877, 304, 58],
    '0x4fd8': [1693, 174, 216, 196, 130, 480, 87, 24, 80, 10, 2831, 736, 164, 746, 1276, 56, 78, 14, 892, 396, 36],
    '0x5654': [108, 76, 40, 2008, 4806, 3471, 8247, 352, 3892, 296, 4, 20, 1134, 328, 128, 294],
    '0x5540': [198, 33, 8, 6, 2, 509, 6, 16, 3803, 366, 335, 8341, 632, 197, 229],
}


def main():
    root = Path('/mnt/c/X3FL')
    cats = [p.relative_to(root).as_posix() for p in catalogue_mounts.mounted_catalogues(root, 'fl')]
    a = Assets(root, catalogues=cats)
    want = {k: sorted(v) for k, v in TARGETS.items()}
    hits = {k: [] for k in TARGETS}
    near = {k: [] for k in TARGETS}
    best = {k: [] for k in TARGETS}
    n = 0
    for key, entries in a.entries.items():
        if not key.startswith('objects/') or not key.endswith(('.bob', '.bod')):
            continue
        e = entries[-1]
        try:
            data = a.read_entry(e)
            if data[:4] == b'BOB1':
                tree = bob1.parse(data, None)
            elif key.endswith('.bod') and not re.search(rb'^P \d+;', data[:4096], re.M):
                tree = bob1.parse_text(data)
            else:
                continue
            lods = bob1.lods(tree)
        except Exception:
            continue
        n += 1
        for li, lod in enumerate(lods):
            faces = sorted(len(g['faces']) for p in lod['parts'] for g in p['groups'])
            fc = collections.Counter(faces)
            for k, w in TARGETS.items():   # multiset overlap: drawn per-draw primitive counts found among the LOD's groups
                best[k].append((sum((fc & collections.Counter(w)).values()), key, li, len(faces), sum(faces)))
            for k, w in want.items():
                if faces == w:
                    hits[k].append((key, e['source'], li))
                elif len(faces) == len(w) and abs(sum(faces) - sum(w)) <= 0.02 * sum(w):
                    near[k].append((key, e['source'], li, sum(faces)))
    print(f'bodies scanned (objects/**, .bob and text .bod): {n}')
    for k in TARGETS:
        print(f'{k}: draws={len(TARGETS[k])} prims={sum(TARGETS[k])} exact={hits[k]} near={near[k][:5]}')
        print('   best overlap (matched draws, body, lod, groups, faces): ' + '; '.join(
            f'{o} {b} lod{l} {g} {f}' for o, b, l, g, f in sorted(best[k], reverse=True)[:3]))


if __name__ == '__main__':
    main()
