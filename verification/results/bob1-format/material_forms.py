#!/usr/bin/env python3
"""Count material forms (MAT6 effect / MAT6 classic / MAT5 / MAT3) and effect parameter
types over every installed BOB1 body; parses only the sections before BODY.
Usage: python3 verification/results/bob1-format/material_forms.py"""
from collections import Counter
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1_roundtrip as b  # noqa: E402

assets = b.Assets(b.ROOT)
forms, ptypes, lodflags = Counter(), Counter(), Counter()
for k, v in sorted(assets.entries.items()):
    if not v[-1]['path'].lower().endswith('.pbb'): continue
    d = assets.read_entry(v[-1])
    if d[:4] != b'BOB1': continue
    r = b.R(d); r.expect('BOB1')
    while True:
        t = r.tag()
        if t in ('INFO', 'NAME'): r.cstr()
        elif t in ('VERS', 'SND1'): r.u32()
        elif t in b.MATVER:
            for m in (b.read_material(r, b.MATVER[t]) for _ in range(r.i32())):
                eff = m.get('flags', 0) & 0x2000000
                forms[t + (' effect' if eff else ' classic')] += 1
                for _, typ, _ in m.get('params', []): ptypes[typ] += 1
            t = 'MAT'
        elif t == 'BODY':
            if r.u16() and r.o + 8 <= len(d): r.i32(); lodflags[r.u32()] += 1   # LOD 0 flags only (truncated khaak body skipped)
            break
        else: break
        r.expect('/' + t[:3])
print('material forms:', dict(forms))
print('effect parameter types:', dict(sorted(ptypes.items())))
print('LOD0 flags:', {hex(x): n for x, n in lodflags.items()})
