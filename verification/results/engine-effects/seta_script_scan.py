#!/usr/bin/env python3
"""Find game data members (cat/dat and loose, installed view, read-only) that name the time-warp natives.

Prints member path, source and the offsets of each hit; no member bytes are written anywhere.
  python3 seta_script_scan.py > seta_script_scan_out.txt
"""
import collections
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
from sector_fog_census import Assets  # noqa: E402

ROOT = pathlib.Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
NEEDLES = [b'TI_SetTimeWarpFactor', b'TI_GetTimeWarpFactor', b'TI_GetTimeWarpMultiplier', b'X2_SetPause']
SKIP = ('.dds', '.jpg', '.tga', '.bod', '.pbd', '.bob', '.pbb', '.wav', '.mp3', '.ogg', '.fx', '.ani', '.dat')


def main():
    assets = Assets(ROOT)
    exts = collections.Counter()
    hits = []
    for key, entries in sorted(assets.entries.items()):
        entry = entries[-1]
        if key.lower().endswith(SKIP):
            continue
        exts[pathlib.PurePosixPath(key).suffix.lower()] += 1
        try:
            data = assets.read_entry(entry)
        except Exception as error:  # noqa: BLE001 - report and continue
            print('unreadable', key, type(error).__name__)
            continue
        for needle in NEEDLES:
            at = data.find(needle)
            if at >= 0:
                hits.append((key, entry['source'], needle.decode(), at, data.count(needle)))
    print('scanned extensions', dict(exts.most_common()))
    for key, source, needle, at, count in hits:
        print('hit', needle, key, source, 'first@%d' % at, 'count %d' % count)


if __name__ == '__main__':
    main()
