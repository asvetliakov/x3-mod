#!/usr/bin/env python3
"""Counts of an FL merged-LOD overlay batch (lod_overlay.py --batch --out OUT on C:\\X3FL): bodies baked and
skipped per category, the skip reasons (unreadable or unparsable bodies, single-LOD ladders), slots, bytes, draws and the sha256 of the produced pair. Reads OUT/x3m-lod-batch.json and
OUT/addon2/NN.cat/.dat only; prints counts and hashes (the pair is derived from game data and stays untracked).
usage: bake_counts.py OUT"""
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

UNREADABLE = ('parse_error', 'text_parse_error', 'mat3', 'material_outside_table', 'exception', 'not_found')

out = Path(sys.argv[1])
r = json.loads((out / 'x3m-lod-batch.json').read_text())
c = r['counts']
print(f"game {r['game']}; enumerated {c['enumerated']} {c['by_category']}; eligible {c['eligible']};"
      f" built {c['built']} + reused {c['reused']} = {c['overlay_bodies']} overlay bodies")
for cat in ('ship', 'station'):
    rows = [b for b in r['bodies'] if b['cat'] == cat]
    baked = [b for b in rows if b['eligible']]
    single = [b for b in rows if not b.get('thresholds')]
    print(f"{cat}: enumerated {len(rows)}, baked {len(baked)} (single-LOD among them {sum(1 for b in baked if not b.get('thresholds'))}),"
          f" not baked {len(rows) - len(baked)}: single-LOD {sum(1 for b in single if not b['eligible'])},"
          f" unreadable {sum(1 for b in rows if set(b['refuse']) & set(UNREADABLE))}")
    print(f"  refused {Counter(x for b in rows for x in b['refuse']).most_common()}")
    print(f"  filtered {Counter(x for b in rows for x in b['filter']).most_common()}")
print('all refused', r['refused'])
print('all filtered', r['filtered'])
print('slots', r['slots'])
print('bytes', r['bytes'], 'draws', r['draws'])
for s in r['slots']:
    for ext in ('.cat', '.dat'):
        p = out / 'addon2' / f"{s['slot']:02d}{ext}"
        h = hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        print(f'{p.name} {p.stat().st_size} B sha256 {h.hexdigest()}')
print('settings tool_sha256', r['settings']['tool_sha256'])
print('timing', r['timing'])
