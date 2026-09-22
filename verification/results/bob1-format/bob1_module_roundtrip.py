#!/usr/bin/env python3
"""Whole-set acceptance for tools/analysis/bob1.py: parse + serialise every installed
.pbb (winning member per resource). Read-only; prints counts and failures only.
Usage: python3 verification/results/bob1-format/bob1_module_roundtrip.py
"""
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
from sector_fog_census import Assets  # noqa: E402

assets = Assets(bob1.DEFAULT_GAME)
keys = sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith('.pbb'))
kinds = Counter(); ok = 0; fails = []
for k in keys:
    e = assets.entries[k][-1]
    data = assets.read_entry(e)
    assets.cache.clear()
    kinds[bob1.kind(data) or 'other'] += 1
    if bob1.kind(data) != 'BOB1':
        continue
    try:
        if bob1.serialise(bob1.parse(data)) != data:
            raise bob1.FormatError('round-trip mismatch')
        ok += 1
    except bob1.FormatError as exc:
        fails.append(f'{e["source"]}:{e["path"]}: {exc}')
print(f'pbb resources={len(keys)} kinds={dict(kinds)} BOB1 round-trip equal={ok} failed={len(fails)}')
for f in fails:
    print('  fail', f)
