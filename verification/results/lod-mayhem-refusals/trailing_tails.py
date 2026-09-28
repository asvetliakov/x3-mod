"""What follows /BOB in the Mayhem 3 bodies the batch refused as trailing_bytes (before the
2026-09-29 rule change). Read-only on the game tree. Prints per body: member, body and tail length,
tail head bytes, whether the tail parses as a second BOB1 body (and whether it equals the first,
another winning body or a vanilla body of the same name), printable share, and the tail sha256.

PYTHONPATH=verification/probe:tools/analysis python3 verification/results/lod-mayhem-refusals/trailing_tails.py [--stale | --members ROOT]

Per-body outcome (eight_dryrun_summary.txt; ROOT for --members is the same command without --dry-run):
PYTHONPATH=verification/probe:tools/analysis python3 tools/analysis/lod_overlay.py --batch --dry-run --jobs 2 \
    --only verification/results/lod-mayhem-refusals/only.txt --out $SCRATCH/dry --record $SCRATCH/dry/eight.json
"""
import hashlib
import sys
from pathlib import Path

import bob1
import lod_overlay

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
NAMES = ['ships/props/bigturret4_weapon', 'ships/props/usc_m1turretD_weapon', 'ships/props/usc_m7turretB_weapon',
         'ships/props/usc_m7turretC_weapon', 'ships/props/yaki_m1turretA_weapon', 'ships/props/yaki_m7turretA_base',
         'ships/props/yaki_m7turretA_weapon', 'ships/terran/terran_m2p_tobosaku/hull']


def describe_tail(tail):
    out = {'head': tail[:16].hex(), 'magic': tail[:4], 'printable': sum(32 <= b < 127 or b in (9, 10, 13) for b in tail) / len(tail)}
    try:
        t2 = bob1.parse(tail, None)
        out['second_body'] = dict(lods=[l['value'] for l in bob1.lods(t2)],
                                  mats=len(bob1.materials(t2) or []), rest=t2.get('trailing_bytes', 0),
                                  sections=[t for t, _ in t2['sections']])
    except Exception as exc:                  # noqa: BLE001 - classify whatever the tail is
        out['second_body'] = f'no ({str(exc)[:60]})'
    tags = [tag for tag in (b'BOB1', b'/BOB', b'INFO', b'MAT6', b'BODY', b'POIN', b'/POI', b'PART', b'/PAR', b'/BOD') if tag in tail]
    out['tags_inside'] = [t.decode() for t in tags]
    return out


def main():
    assets, _ = lod_overlay.original_assets(GAME)
    for name in NAMES:
        entry = bob1.resolve_body(assets, name)
        data = assets.read_entry(entry)
        tree = bob1.parse(data, None)
        n = tree.get('trailing_bytes', 0)
        body, tail = data[:len(data) - n], data[len(data) - n:]
        info = describe_tail(tail)
        cands = [e['source'] for e in assets.candidates(bob1.body_stem(name) + entry['path'][-4:].replace('.bob', '.pbb'))]
        same_as_body = tail[:len(body)] == body
        print(f'{name}: member {entry["source"]}:{entry["path"]} body {len(body)} B tail {n} B'
              f' tail_sha256 {hashlib.sha256(tail).hexdigest()[:16]} magic {info["magic"]!r} head {info["head"]}'
              f' printable {info["printable"]:.2f} tags {info["tags_inside"]} second_body {info["second_body"]}'
              f' tail_starts_with_body {same_as_body} lods {[l["value"] for l in bob1.lods(tree)]} candidates {cands}')
    return 0


if __name__ == '__main__' and not {'--stale', '--members'} & set(sys.argv):
    sys.exit(main())


def stale_shape():
    """Second pass: every tail's closing tags, its /BOB offset, and the share of 32-byte tail
    windows (step 32) that also occur inside the body before /BOB (a stale remainder of an older,
    longer version of the same body shares geometry records with the current one)."""
    assets, _ = lod_overlay.original_assets(GAME)
    for name in NAMES:
        data = assets.read_entry(bob1.resolve_body(assets, name))
        n = bob1.parse(data, None)['trailing_bytes']
        body, tail = data[:len(data) - n], data[len(data) - n:]
        wins = [tail[i:i + 32] for i in range(0, n - 32, 32)]
        shared = sum(1 for w in wins if w in body)
        print(f'{name}: ends /PAR/BOD/BOB {tail.endswith(b"/PAR/BOD/BOB")} first /BOB at {tail.find(b"/BOB")} of {n}'
              f' BOB1 header {b"BOB1" in tail} windows in body {shared}/{len(wins)}')


if __name__ == '__main__' and '--stale' in sys.argv:
    stale_shape()


def written_members(root):
    """Third pass (--members ROOT, ROOT = a scratch --out of `lod_overlay.py --batch --only only.txt`):
    every written body member parses strictly (max_trailing 0), i.e. carries no byte after /BOB."""
    import gzip
    from inspect_x3 import read_catalogue
    cat = next(Path(root, 'addon').glob('*.cat'))
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    for e in read_catalogue(cat):
        if e['path'].startswith('objects/'):
            m = raw[e['offset']:e['offset'] + e['size']]
            m = gzip.decompress(m) if m[:2] == b'\x1f\x8b' else m
            t = bob1.parse(m)                   # raises on any trailing byte
            print(f'{e["path"]}: {len(m)} B, strict parse ok, ends /BOB {m.endswith(b"/BOB")},'
                  f' trailing {t.get("trailing_bytes", 0)}')


if __name__ == '__main__' and '--members' in sys.argv:
    written_members(sys.argv[sys.argv.index('--members') + 1])
