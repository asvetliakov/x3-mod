#!/usr/bin/env python3
"""Every FL body the overlay batch refuses as unreadable or for a format reason (mat3, parse_error, text_parse_error,
classic_unsupported, material_outside_table, texture_generated), from OUT/x3m-lod-batch.json, compared by body name
with the installed AP batch record C:\\X3\\addon\\x3m-lod-batch.json (the Mayhem-modded AP view, read only): same body
refused identically there, refused differently, baked or filtered there, or absent (FL-only). Also the FL winning
member (catalogue:path) of each. Prints a table only.   usage: format_refusals.py OUT"""
import json
import sys
from collections import Counter
from pathlib import Path

FORMAT = ('mat3', 'parse_error', 'text_parse_error', 'classic_unsupported', 'material_outside_table',
          'texture_generated')
fl = json.loads((Path(sys.argv[1]) / 'x3m-lod-batch.json').read_text())
ap = {b['name'].lower(): b for b in json.loads(Path('/mnt/c/X3/addon/x3m-lod-batch.json').read_text())['bodies']}
rows = []
for b in sorted(fl['bodies'], key=lambda b: b['name'].lower()):
    why = [r for r in b['refuse'] if r in FORMAT]
    if not why:
        continue
    a = ap.get(b['name'].lower())
    if a is None:
        cmp = 'FL-only (not in the AP view)'
    elif set(why) <= set(a['refuse']):
        cmp = 'AP refused identically'
    elif a['eligible']:
        cmp = 'FL-different (AP baked it)'
    else:
        cmp = f"FL-different (AP: {','.join(a['refuse'] + a['filter'])})"
    rows.append((b['name'], b['cat'], ','.join(why), b['member'], cmp))
print('| body | category | refusal | FL member | AP view |')
print('| --- | --- | --- | --- | --- |')
for r in rows:
    print('| ' + ' | '.join(r) + ' |')
print()
print('by refusal', Counter(w for r in rows for w in r[2].split(',')).most_common())
print('by category', Counter(r[1] for r in rows).most_common())
print('by AP comparison', Counter(r[4].split(' (')[0] for r in rows).most_common())
