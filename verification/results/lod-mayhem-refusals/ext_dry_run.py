#!/usr/bin/env python3
"""Acceptance 3 of the extension-precedence change: a --batch --dry-run over the 30 ship/station rows the
bake record refused as ambiguous_body_ext (named in ext_precedence_out.txt), then per-body outcome counts
from the dry-run record. Nothing is written into the game tree (--out and --record go to SCRATCH).
Usage (repository root):
  PYTHONPATH=tools/analysis python3 verification/results/lod-mayhem-refusals/ext_dry_run.py SCRATCH [game_root] \
      > verification/results/lod-mayhem-refusals/ext_dry_run_out.txt"""
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SCRATCH = Path(sys.argv[1])
GAME = Path(sys.argv[2] if len(sys.argv) > 2 else
            Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
names = [m.group(1) for line in (HERE / 'ext_precedence_out.txt').read_text().splitlines()
         if (m := re.match(r'(?:ship|station)\s+(\S+)\s+case=', line))]
SCRATCH.mkdir(parents=True, exist_ok=True)
(SCRATCH / 'only.txt').write_text('\n'.join(names) + '\n')
record_path = SCRATCH / 'ext_dry_run.json'
cmd = [sys.executable, str(ROOT / 'tools/analysis/lod_overlay.py'), '--batch', '--dry-run', '--game', str(GAME),
       '--out', str(SCRATCH / 'out'), '--record', str(record_path), '--only', str(SCRATCH / 'only.txt'),
       '--jobs', '2']
done = subprocess.run(cmd, capture_output=True, text=True)
print(f'rows {len(names)}; lod_overlay exit {done.returncode}')
if done.returncode:
    print(done.stderr[-2000:])
    sys.exit(1)
record = json.loads(record_path.read_text())
by = {b['name']: b for b in record['bodies']}
outcome = collections.Counter()
for n in names:
    b = by.get(n)
    if b is None:
        state = 'not enumerated'
    elif b.get('refuse'):
        state = 'refused ' + '+'.join(b['refuse'])
    elif b.get('filter'):
        state = 'filtered ' + '+'.join(b['filter'])
    else:
        state = 'baked'
    outcome[state] += 1
    member = b.get('member') if b else '-'
    err = f'  error={b["error"][:120]!r}' if b and b.get('error') and b.get('refuse') else ''
    print(f'{n}  {state}  member={member}  text={member.lower().endswith((".pbd", ".bod"))}{err}')
print('outcomes: ' + json.dumps(dict(sorted(outcome.items()))))
print(f'ambiguous_body_ext rows: {sum("ambiguous_body_ext" in (by.get(n) or {}).get("refuse", []) for n in names)}')
print('record counts: ' + json.dumps(record['counts']) + '; refused ' + json.dumps(record['refused']))
