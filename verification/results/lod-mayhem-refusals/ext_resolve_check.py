#!/usr/bin/env python3
"""Acceptance 2 of the extension-precedence change: bob1.resolve_body (engine rule, body-format-bob1.md
section 7.1) against the winners ext_precedence.py printed for every ambiguous_body_ext row of the bake
record, over the same view (lod_overlay.original_assets: the x3m overlay slots left out). Also counts, over
every body stem of the install, where the new resolver picks another member than the pre-change resolver
(last entry of the one extension group present) for a stem that has only binary or only text members.
Read-only over the game tree. Usage:
  PYTHONPATH=tools/analysis python3 ext_resolve_check.py [game_root] > ext_resolve_check_out.txt"""
import collections
import json
import re
import sys
from pathlib import Path

import bob1
import lod_overlay

HERE = Path(__file__).resolve().parent
GAME = Path(sys.argv[1] if len(sys.argv) > 1 else
            Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')

expected = {}
for line in (HERE / 'ext_precedence_out.txt').read_text().splitlines():
    m = re.match(r'(\w+)\s+(\S+)\s+case=\S+\s+winner=(\S+):(\w+) \((binary|text)\)', line)
    if m:
        expected[m.group(2)] = (m.group(1), m.group(3), m.group(4))
record = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
rows = [b for b in record['bodies'] if 'ambiguous_body_ext' in (b.get('refuse') or [])]
assets, skipped = lod_overlay.original_assets(GAME)
print(f'skipped overlay sources {skipped}; record rows {len(rows)}; expected winners {len(expected)}')
agree, kinds = collections.Counter(), collections.Counter()
for b in rows:
    e = bob1.resolve_body(assets, b['name'])
    got = ('loose' if 'loose' in e else e['source'], Path(e['path']).suffix[1:].lower())
    cat, src, ext = expected[b['name']]
    ok = got == (src, ext)
    agree[ok] += 1
    kind = 'binary' if ext in ('pbb', 'bob') else 'text'
    if cat != 'other':
        kinds[kind] += 1
    record_member = b['member'].lower() == f'{e["source"]}:{e["path"]}'.lower()
    print(f'{cat:7} {b["name"]}  resolved={got[0]}:{got[1]}  expected={src}:{ext}  {"MATCH" if ok else "DIFF"}'
          f'  record_member={"same" if record_member else "differs"}')
print(f'resolved == ext_precedence winner: {agree[True]} of {len(rows)}')
print(f'ship/station winners: {dict(kinds)}')

# pre-change resolver on single-kind stems vs the new one
stems = {bob1.body_stem(k) for k in assets.entries if k.removeprefix('addon/').startswith('objects/')
         and k.endswith(('.bob', '.bod'))}
changed, single = [], 0
for stem in sorted(stems):
    found = [g for g in (assets.candidates(stem + '.pbb'), assets.candidates(stem + '.pbd')) if g]
    if len(found) != 1:
        continue
    single += 1
    old, new = found[0][-1], bob1.resolve_body(assets, stem)
    if old is not new:
        changed.append(f'{stem}: {old["source"]}:{old["path"]} -> {new["source"]}:{new["path"]}')
print(f'single-kind body stems {single}; new resolver differs from the old on {len(changed)}')
for c in changed:
    print('  ' + c)

# review F1-F3: stems the resolver refuses (loose root, language variant) and catalogues after a numbering gap
unmounted = [s for s in assets.layers if re.fullmatch(r'(addon/)?\d\d\.cat', s)
             and not bob1.mounted(assets, dict(source=s))]
refusals = collections.Counter()
for stem in sorted(stems):
    try:
        bob1.resolve_body(assets, stem)
    except bob1.BodyRefused as exc:
        refusals[exc.reason] += 1
        print(f'  refused {stem}: {exc}'[:240])
    except FileNotFoundError:
        refusals['only_after_gap'] += 1
loose_addon = [e['path'] for v in assets.entries.values() for e in v if 'loose' in e
               and e['path'].lower().startswith('addon/objects/')]
lang = [k for k in assets.entries if bob1._LANG_KEY.fullmatch(k)]
print(f'body stems {len(stems)}; unmounted catalogues after a gap {unmounted}; loose files under addon/objects'
      f' {len(loose_addon)}; -L<nnn> body keys {len(lang)}; resolver refusals {dict(refusals)}')
