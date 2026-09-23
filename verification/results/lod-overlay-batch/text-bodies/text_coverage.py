#!/usr/bin/env python3
"""Winning text members (.pbd/.bod) of a game root: scenes vs bodies by top directory, faces with and
without N blocks, lexical facts (CRLF, /! block kinds, face flag values), bob1.parse_text outcome
with every refusal. Read-only.

  PYTHONPATH=tools/analysis python3 text_coverage.py GAME
"""
import collections
import re
import sys
import time
from pathlib import Path

import bob1
import sector_fog_census as sfc

a = sfc.Assets(Path(sys.argv[1]))
keys = sorted(k for k, v in a.entries.items() if v[-1]['path'].lower().endswith(('.pbd', '.bod')))
C = collections.Counter
FACE = r'^[ \t]*-?[0-9]+;[ \t]*[0-9]+;[ \t]*[0-9]+;[ \t]*[0-9]+;[ \t]*(-[0-9]+);'   # one line (no newline in \s)
faces, with_n, lacking, bodies, errors, mats, blocks, fflags, crlf = C(), C(), C(), C(), [], C(), C(), C(), 0
scenes, t0 = 0, time.time()
for k in keys:
    e = a.entries[k][-1]
    d = a.read_entry(e)
    a.cache.clear()
    if bob1.text_kind(d) == 'scene':
        scenes += 1
        continue
    top = k.removeprefix('addon/').split('/')[1]
    bodies[top] += 1
    t = d.decode('latin1')
    crlf += '\r' in t
    for m in re.finditer(r'/!\s*([A-Z_]+):', t):
        blocks[m.group(1)] += 1
    for m in re.finditer(FACE, t, re.M):
        fflags[m.group(1)] += 1
    nf = len(re.findall(FACE, t, re.M))
    nn = len(re.findall(r'/!\s*N:', t))
    faces[top] += nf
    with_n[top] += nn
    lacking[top] += nn < nf
    try:
        tree = bob1.parse_text(d)
        mats[next((tg for tg, _ in tree['sections'] if tg in bob1.MATVER), '-')] += 1
    except bob1.FormatError as exc:
        errors.append(f'{e["source"]}:{e["path"]}: {exc}'[:160])
print(f'text members {len(keys)}: scenes {scenes}, bodies {sum(bodies.values())} {dict(bodies)}; CRLF {crlf}')
print('faces', dict(faces))
print('faces with an N block', dict(with_n))
print('bodies with faces lacking an N block', dict(lacking))
print('/! block kinds', dict(blocks), 'face flag values', dict(fflags))
print(f'parse_text: {sum(bodies.values()) - len(errors)} compiled, {len(errors)} text_parse_error; material tags {dict(mats)}')
for line in errors:
    print('  refused', line)
print(f'elapsed {time.time() - t0:.0f} s')
